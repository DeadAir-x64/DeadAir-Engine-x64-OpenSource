#include "pch_script.h"
#include "da_memory_probe.h"

#include "Level.h"
#include "ai_space.h"
#include "alife_simulator.h"
#include "alife_object_registry.h"

#include "xrScriptEngine/script_engine.hpp"
#include "xrEngine/IGame_Persistent.h"
#include "xrSound/Sound.h"
#include "xrEngine/XR_IOConsole.h"
#include "Common/object_broker.h"
#include <algorithm>
#include <windows.h>

namespace
{
// Прогонов держим немного: таблица должна оставаться читаемой в логе, а протокол требует трёх.
constexpr size_t MAX_RUNS = 8;
constexpr size_t MAX_MARKS = 24;
constexpr int SETTLE_FRAMES = 180; // три секунды при 60 кадрах

struct Mark
{
    shared_str label;
    size_t committed_kb = 0; // память процесса на момент отметки
    // Разница с предыдущей отметкой. ЗНАКОВАЯ, и это принципиально: отрицательное значение — это
    // память, которая ВЕРНУЛАСЬ. Первая версия обрезала минус в ноль, и самый нужный сигнал —
    // «уничтожение старого симулятора освободило столько-то» — оказывался невидим.
    long long delta_kb = 0;
};

struct Run
{
    shared_str what;
    xr_vector<Mark> marks;
    // Полный снимок по подсистемам, снятый в конце прогона.
    size_t committed_kb = 0;
    size_t textures_kb = 0;
    size_t strings_count = 0; // ЧИСЛО уникальных строк. Именно оно показательно: stat_economy()
                              // возвращает не занятую память, а сэкономленную на разделении, и как
                              // мера потребления не годится вовсе.
    size_t lua_kb = 0;
    size_t objects = 0;
    size_t alife_objects = 0;
    size_t heap_blocks = 0; // ЖИВЫХ блоков в кучах процесса
    size_t heap_kb = 0;     // и сколько они занимают
    // Какие звуки звучат: имя -> {всего копий, из них БЕЗ живого объекта-владельца}.
    // Осиротевший эмиттер — это и есть утечка в чистом виде: объект снесён, звук продолжает играть.
    struct SndItem { shared_str name; u32 total; u32 orphan; };
    xr_vector<SndItem> snd_top;
    size_t snd_playing = 0;   // звуков проигрывается сейчас
    size_t snd_events = 0;    // событий звуковой сцены
    size_t ps_active = 0;     // живых систем частиц
    size_t ps_destroy = 0;    // ждут уничтожения
    size_t ps_needtoplay = 0; // ждут запуска
    bool objects_pending = false; // объекты считаются не сразу, см. DA_MemTick
};

xr_vector<Run> g_runs;
bool g_run_open = false;

size_t committed_kb() { return Memory.mem_usage() / 1024; }

// Lua считает своё потребление сам; функция GC возвращает килобайты.
size_t lua_kb()
{
    if (!GEnv.ScriptEngine || !GEnv.ScriptEngine->lua())
        return 0;
    return (size_t)lua_gc(GEnv.ScriptEngine->lua(), LUA_GCCOUNT, 0);
}

size_t textures_kb()
{
    u32 m_base = 0, c_base = 0, m_lmaps = 0, c_lmaps = 0;
    if (GEnv.Render)
        GEnv.Render->ResourcesGetMemoryUsage(m_base, c_base, m_lmaps, c_lmaps);
    return (m_base + m_lmaps) / 1024;
}

const char* sign(long long v) { return v > 0 ? "+" : ""; }

// Обход куч процесса: считает ЖИВЫЕ блоки, а не закоммиченную память.
//
// Ради этого всё и делается. Memory.mem_usage() возвращает PagefileUsage, в котором сидит и то, что
// приложение освободило, а аллокатор не отдал системе. Поэтому по нему нельзя отличить утечку от
// удержания: обе выглядят как ровный рост. Сумма живых блоков такой двусмысленности не имеет —
// если она растёт от перезагрузки к перезагрузке, память действительно не освобождается.
//
// Стоит дорого: HeapLock останавливает остальные потоки на время обхода. Поэтому вызывается один
// раз за прогон, в момент, когда мир уже устоялся, и гасится крутилкой da_mem_heapwalk.
void walk_heaps(size_t& blocks, size_t& bytes)
{
    blocks = 0;
    bytes = 0;

    HANDLE heaps[64];
    const DWORD count = GetProcessHeaps(64, heaps);
    for (DWORD i = 0; i < count && i < 64; ++i)
    {
        if (!HeapLock(heaps[i]))
            continue;

        PROCESS_HEAP_ENTRY entry{};
        while (HeapWalk(heaps[i], &entry))
        {
            if (entry.wFlags & PROCESS_HEAP_ENTRY_BUSY)
            {
                ++blocks;
                bytes += entry.cbData;
            }
        }
        HeapUnlock(heaps[i]);
    }
}

// Ширина строки В СИМВОЛАХ, а не в байтах: подписи фаз русские, в UTF-8 это два байта на букву,
// и обычное "%-22s" разъезжает колонки ровно на длину подписи.
size_t utf8_width(const char* s)
{
    size_t n = 0;
    for (const unsigned char* p = (const unsigned char*)s; *p; ++p)
        if ((*p & 0xC0) != 0x80) // считаем только ведущие байты
            ++n;
    return n;
}

void pad_to(string512& dst, size_t width)
{
    for (size_t w = utf8_width(dst); w < width; ++w)
        xr_strcat(dst, " ");
}
} // namespace

// Выключатель: da_mem_probe 0 гасит автоматические отметки и печать в конце загрузки.
// Ручной da_mem_dump работает всегда — он печатает то, что успели накопить.
int g_da_mem_probe = 1;

// Обход куч: даёт ЖИВЫЕ аллокации вместо закоммиченной памяти, но останавливает потоки на время
// обхода. Для охоты за утечкой включён, в обычной игре имеет смысл гасить.
int g_da_mem_heapwalk = 1;

namespace
{
int g_repeat_left = 0;      // сколько загрузок осталось в авто-прогоне
int g_settle_frames = 0;    // пауза, пока мир устаивается, кадров
bool g_finish_pending = false; // итог прогона ещё не снят
} // namespace

void DA_MemReset()
{
    g_runs.clear();
    g_run_open = false;
    Msg("~ [DA_MEM] накопленное сброшено");
}

void DA_MemRunBegin(const char* what)
{
    if (!g_da_mem_probe)
        return;

    if (g_runs.size() >= MAX_RUNS)
        g_runs.erase(g_runs.begin()); // держим последние MAX_RUNS

    g_runs.push_back(Run());
    g_runs.back().what = what ? what : "?";
    g_run_open = true;

    DA_MemMark("начало");
}

void DA_MemMark(const char* label)
{
    if (!g_da_mem_probe)
        return;
    if (!g_run_open || g_runs.empty())
        return;

    Run& run = g_runs.back();
    if (run.marks.size() >= MAX_MARKS)
        return;

    const size_t now = committed_kb();
    Mark m;
    m.label = label ? label : "?";
    m.committed_kb = now;
    m.delta_kb = run.marks.empty() ? 0 : (long long)now - (long long)run.marks.back().committed_kb;
    run.marks.push_back(m);
}

void DA_MemRunEnd()
{
    if (!g_da_mem_probe)
        return;
    if (!g_run_open || g_runs.empty())
        return;

    DA_MemMark("конец загрузки");

    Run& run = g_runs.back();

    // Клиентские объекты в этот момент ещё НЕ созданы (спавн приходит позже), а прогрев после
    // PreCache ещё не закончился. Поэтому ИТОГ прогона снимается не здесь, а когда мир устоится, —
    // см. DA_MemTick. Иначе меряется середина процесса, а не результат.
    run.objects = 0;
    run.objects_pending = true;

    // Имя уровня в начале прогона ещё неизвестно — уточняем его здесь, чтобы в таблице было видно,
    // что сравниваются загрузки ОДНОГО уровня. Сравнивать разные бессмысленно: у них разное
    // население и геометрия, и любая разница объясняется этим, а не утечкой.
    if (g_pGameLevel && Level().name().size())
        run.what = Level().name();

    g_run_open = false;

    Msg("~ [DA_MEM] прогон %u (%s): загрузка закончилась, жду, пока мир устоится",
        (u32)g_runs.size(), run.what.c_str());
}

namespace
{
// Итог прогона: снимается после того, как объекты заспавнились и прогрев закончился.
void finish_run(Run& run)
{
    g_run_open = true; // чтобы прошла отметка
    DA_MemMark("после прогрева");
    g_run_open = false;

    run.committed_kb = committed_kb();
    run.textures_kb = textures_kb();
    run.lua_kb = lua_kb();

    if (g_pStringContainer)
    {
        const auto [bytes, count] = g_pStringContainer->stat_economy();
        (void)bytes; // не занятая память, а экономия от разделения — вводит в заблуждение
        run.strings_count = (size_t)count;
    }

    run.alife_objects = ai().get_alife() ? ai().alife().objects().objects().size() : 0;

    if (g_da_mem_heapwalk)
    {
        size_t blocks = 0, bytes = 0;
        walk_heaps(blocks, bytes);
        run.heap_blocks = blocks;
        run.heap_kb = bytes / 1024;
    }

    // Звук и частицы: обе подсистемы создаются пачками на каждую загрузку и обе живут вне счётчиков
    // движка, поэтому в прошлых замерах их никто не проверял. В логе после каждой перезагрузки
    // висят жалобы «Playing sound is not completed, but is destroying» — повод посмотреть.
    if (GEnv.Sound)
    {
        CSound_stats s0{};
        CSound_stats_ext s1;
        GEnv.Sound->statistic(&s0, &s1);
        run.snd_playing = s0._rendered + s0._simulated;
        run.snd_events = s0._events;

        // Считаем КОПИИ по имени. Если один и тот же звук с каждой перезагрузкой звучит на копию
        // больше — значит эмиттеры прошлого мира не гасятся, и это уже не догадка, а имя файла.
        for (const auto& item : s1.items)
        {
            const u32 orphan = item.game_object ? 0 : 1;
            bool found = false;
            for (auto& snd : run.snd_top)
                if (snd.name == item.name)
                {
                    ++snd.total;
                    snd.orphan += orphan;
                    found = true;
                    break;
                }
            if (!found)
                run.snd_top.push_back({ item.name, 1, orphan });
        }
        std::sort(run.snd_top.begin(), run.snd_top.end(),
            [](const Run::SndItem& a, const Run::SndItem& b) { return a.total > b.total; });
    }
    if (g_pGamePersistent)
    {
        run.ps_active = g_pGamePersistent->ps_active.size();
        run.ps_destroy = g_pGamePersistent->ps_destroy.size();
        run.ps_needtoplay = g_pGamePersistent->ps_needtoplay.size();
    }

    Msg("~ [DA_MEM] прогон %u (%s) завершён: %u МБ", (u32)g_runs.size(), run.what.c_str(),
        (u32)(run.committed_kb / 1024));

    // Пул моделей и контейнер анимаций — два крупных хранилища, которых нет ни в одном счётчике
    // выше: скелетная геометрия и наборы движений живут отдельно от текстур. Штатные команды
    // печатают итог строкой «--- models: N, mem usage: X Kb», по ней и сравним прогоны.
    // Дампы длинные, поэтому идут после итоговой строки прогона и только при включённом замере.
    Console->Execute("stat_models");
    Console->Execute("stat_motions");

    DA_MemDump();
}
} // namespace

void DA_MemTestStart(int runs)
{
    if (runs < 2)
        runs = 3;

    DA_MemReset();
    g_repeat_left = runs - 1; // первую загрузку запускаем прямо сейчас
    g_settle_frames = 0;

    Msg("~ [DA_MEM] авто-прогон: %d загрузок последнего сохранения подряд. Не выходите из игры.", runs);
    Console->Execute("load_last_save");
}

void DA_MemTick()
{
    if (!g_da_mem_probe || g_runs.empty())
        return;

    if (!g_pGameLevel)
        return;

    Run& run = g_runs.back();

    if (run.objects_pending)
    {
        const u32 count = Level().Objects.o_count();
        if (count == 0)
            return; // спавн ещё идёт

        run.objects = count;
        run.objects_pending = false;

        g_run_open = true;
        DA_MemMark("объекты заспавнены");
        g_run_open = false;

        // Дать миру устояться, прежде чем снимать итог и грузить снова: спавн продолжается и после
        // появления первых объектов, а PreCache прогревает кадры уже за пределами загрузки.
        // Замер показал, что весь прирост даёт именно это окно, поэтому оно размечено посекундно —
        // видно, растёт ли равномерно (значит что-то продолжает создаваться) или ступенькой.
        g_settle_frames = SETTLE_FRAMES;
        g_finish_pending = true;
        return;
    }

    if (g_settle_frames > 0)
    {
        --g_settle_frames;

        const int passed = SETTLE_FRAMES - g_settle_frames;
        if (passed % 60 == 0)
        {
            string64 label;
            xr_sprintf(label, "прогрев +%dс", passed / 60);
            g_run_open = true;
            DA_MemMark(label);
            g_run_open = false;
        }
        return;
    }

    if (g_finish_pending)
    {
        g_finish_pending = false;
        finish_run(run);
        return; // следующую загрузку запускаем со следующего кадра, чтобы таблица успела лечь в лог
    }

    if (g_repeat_left <= 0)
        return;

    --g_repeat_left;
    Msg("~ [DA_MEM] авто-прогон: следующая загрузка, осталось после неё %d", g_repeat_left);
    Console->Execute("load_last_save");
}

void DA_MemDump()
{
    if (g_runs.empty())
    {
        Msg("~ [DA_MEM] замеров ещё нет: загрузите сохранение");
        return;
    }

    Msg("~ ================= [DA_MEM] память по загрузкам =================");

    // --- итог по прогонам: та самая форма кривой ---
    Msg("~ [DA_MEM] прогон | уровень              | всего МБ | прирост к прошлому");
    for (size_t i = 0; i < g_runs.size(); ++i)
    {
        const Run& r = g_runs[i];
        const long long d = (i == 0) ? 0
                                     : (long long)r.committed_kb - (long long)g_runs[i - 1].committed_kb;
        Msg("~ [DA_MEM]   %-4u | %-20s | %8u | %s%lld МБ", (u32)(i + 1), r.what.c_str(),
            (u32)(r.committed_kb / 1024), sign(d), d / 1024);
    }

    if (g_runs.size() >= 3)
    {
        // Вердикт по ТЕНДЕНЦИИ шагов, а не по крайним значениям: единичный шаг шумит на десятки
        // мегабайт, и сравнение первого с последним легко даёт противоположный ответ на одних и тех
        // же данных. Сравниваются средние по первой и второй половине.
        xr_vector<long long> steps;
        for (size_t i = 1; i < g_runs.size(); ++i)
            steps.push_back((long long)g_runs[i].committed_kb - (long long)g_runs[i - 1].committed_kb);

        const size_t half = steps.size() / 2;
        long long first_sum = 0, last_sum = 0;
        for (size_t i = 0; i < half; ++i)
            first_sum += steps[i];
        for (size_t i = steps.size() - half; i < steps.size(); ++i)
            last_sum += steps[i];

        const long long a = half ? first_sum / (long long)half / 1024 : 0;
        const long long b = half ? last_sum / (long long)half / 1024 : 0;

        Msg("~ [DA_MEM] шаг: первая половина %lld МБ, вторая %lld МБ", a, b);
        if (half == 0)
            Msg("~ [DA_MEM] ВЫВОД: прогонов мало, тенденцию не построить");
        else if (b * 2 < a)
            Msg("~ [DA_MEM] ВЫВОД: шаг УБЫВАЕТ вдвое и сильнее — похоже на выход на плато, то есть "
                "удержание памяти аллокатором, а не утечку. Прогоните ещё раз с большим N: если шаг "
                "дойдёт до нуля, вопрос закрыт");
        else if (b < a)
            Msg("~ [DA_MEM] ВЫВОД: шаг убывает, но медленно — однозначного ответа нет. Нужен прогон "
                "подлиннее (da_mem_test 8): плато уйдёт в ноль, утечка встанет на полке");
        else
            Msg("~ [DA_MEM] ВЫВОД: шаг НЕ убывает — это утечка. Ищите в таблицах ниже то, что растёт "
                "монотонно от прогона к прогону");
    }
    else
    {
        Msg("~ [DA_MEM] для вывода нужно ТРИ загрузки одного и того же сохранения подряд, не выходя "
            "из игры. Сделано: %u", (u32)g_runs.size());
    }

    // --- сколько памяти было НА ВХОДЕ в каждую загрузку ---
    // Это самая говорящая строка таблицы: отметка «начало» снимается уже после выгрузки предыдущего
    // уровня. Если память возвращается — числа стоят на месте. Если каждая следующая загрузка
    // начинается с большего, значит выгрузка не отдаёт, и вот это и есть утечка.
    {
        string512 line;
        xr_strcpy(line, "~ [DA_MEM]   на входе (после выгрузки)");
        pad_to(line, 36 + 11);
        for (size_t i = 0; i < g_runs.size(); ++i)
        {
            string32 cell;
            if (!g_runs[i].marks.empty())
                xr_sprintf(cell, " %7u", (u32)(g_runs[i].marks.front().committed_kb / 1024));
            else
                xr_sprintf(cell, " %7s", "-");
            xr_strcat(line, cell);
        }
        Msg("%s", line);
    }

    // --- по фазам: где именно прибавляется ---
    // Строки собираются ПО ИМЕНИ фазы, а не по её порядковому номеру. Это не педантизм: полная
    // загрузка уровня и перезагрузка сохранения проходят РАЗНЫЕ наборы фаз, и первая версия,
    // сопоставлявшая их по позиции, печатала цифры перезагрузки под подписями полной загрузки.
    // Таблица выглядела осмысленной и врала.
    Msg("~ [DA_MEM] --- изменение памяти по фазам, МБ (минус = память вернулась) ---");

    xr_vector<shared_str> labels;
    for (const Run& r : g_runs)
        for (const Mark& m : r.marks)
        {
            bool known = false;
            for (const shared_str& l : labels)
                if (l == m.label)
                {
                    known = true;
                    break;
                }
            if (!known)
                labels.push_back(m.label);
        }

    for (const shared_str& label : labels)
    {
        string512 line;
        xr_sprintf(line, "~ [DA_MEM]   %s", label.c_str());
        pad_to(line, 36); // выравнивание по символам: подписи русские
        for (const Run& r : g_runs)
        {
            const Mark* found = nullptr;
            for (const Mark& m : r.marks)
                if (m.label == label)
                {
                    found = &m;
                    break;
                }

            string32 cell;
            if (found)
                xr_sprintf(cell, " %7lld", found->delta_kb / 1024);
            else
                xr_sprintf(cell, " %7s", "-"); // такой фазы в этом типе загрузки нет
            xr_strcat(line, cell);
        }
        Msg("%s", line);
    }

    // --- подсистемы: что уже отсеяно замером, а что нет ---
    Msg("~ [DA_MEM] --- подсистемы в конце каждого прогона ---");
    Msg("~ [DA_MEM] --- ЖИВЫЕ аллокации (обход куч процесса) ---");
    Msg("~ [DA_MEM] прогон | живых МБ | блоков | закоммичено МБ | разница");
    for (size_t i = 0; i < g_runs.size(); ++i)
    {
        const Run& r = g_runs[i];
        Msg("~ [DA_MEM]   %-4u | %8u | %6u | %14u | %u", (u32)(i + 1), (u32)(r.heap_kb / 1024),
            (u32)r.heap_blocks, (u32)(r.committed_kb / 1024),
            (u32)((r.committed_kb - r.heap_kb) / 1024));
    }
    Msg("~ [DA_MEM] Растут ЖИВЫЕ мегабайты — это утечка. Растёт только закоммичено, а живые стоят —"
        " память держит аллокатор, и это не дефект.");

    Msg("~ [DA_MEM] --- подсистемы в конце каждого прогона ---");
    Msg("~ [DA_MEM] прогон | текстуры МБ | Lua МБ | строк | объектов | ALife | звуков | событий | частиц");
    for (size_t i = 0; i < g_runs.size(); ++i)
    {
        const Run& r = g_runs[i];
        // Объекты у ТОЛЬКО ЧТО завершившейся загрузки ещё не посчитаны: спавн приходит сетевыми
        // сообщениями уже после того, как эта таблица печатается. Пишем прочерк, а не ноль, —
        // ноль читался бы как настоящий замер. Значение появится в следующем da_mem_dump.
        string32 objects;
        if (r.objects_pending)
            xr_sprintf(objects, "%8s", "-");
        else
            xr_sprintf(objects, "%8u", (u32)r.objects);

        Msg("~ [DA_MEM]   %-4u | %11u | %6u | %5u | %s | %5u | %6u | %7u | %u+%u+%u", (u32)(i + 1),
            (u32)(r.textures_kb / 1024), (u32)(r.lua_kb / 1024), (u32)r.strings_count,
            objects, (u32)r.alife_objects, (u32)r.snd_playing, (u32)r.snd_events,
            (u32)r.ps_active, (u32)r.ps_destroy, (u32)r.ps_needtoplay);
    }

    // --- звуки: кто именно накапливается ---
    // Числа в колонке «звуков» растут монотонно — значит эмиттеры не гасятся. Здесь видно, какие
    // именно: если строка идёт 1, 2, 3, 4, 5 по прогонам, виновник назван по имени.
    if (!g_runs.back().snd_top.empty())
    {
        Msg("~ [DA_MEM] --- звучит копий по именам, в скобках без владельца (первые 12) ---");
        const Run& last = g_runs.back();
        const size_t shown = last.snd_top.size() < 12 ? last.snd_top.size() : 12;
        for (size_t k = 0; k < shown; ++k)
        {
            string512 line;
            xr_sprintf(line, "~ [DA_MEM]   %s", last.snd_top[k].name.c_str());
            pad_to(line, 56);
            for (const Run& r : g_runs)
            {
                u32 total = 0, orphan = 0;
                for (const auto& snd : r.snd_top)
                    if (snd.name == last.snd_top[k].name)
                    {
                        total = snd.total;
                        orphan = snd.orphan;
                        break;
                    }
                string32 cell;
                xr_sprintf(cell, " %4u(%u)", total, orphan);
                xr_strcat(line, cell);
            }
            Msg("%s", line);
        }
    }

    // Полный список ресурсов рендера: кроме текстур там геометрия, шейдеры, состояния и константы,
    // которых не видно ни в одном счётчике выше. Список длинный, поэтому печатается только когда
    // авто-прогон закончился, — иначе он повторился бы после каждой загрузки и утопил лог.
    if (GEnv.Render && g_repeat_left <= 0)
    {
        Msg("~ [DA_MEM] --- ресурсы рендера (полный список движка) ---");
        GEnv.Render->ResourcesDumpMemoryUsage();
    }

    Msg("~ [DA_MEM] Текстуры, строки и Lua уже отсеивались замером — если растут именно они, это "
        "новость. Обычный подозреваемый — нетрекаемые C++-аллокации: геометрия уровня, CDB, spatial, "
        "AI-граф, физика, звук.");
    Msg("~ ===============================================================");
}
