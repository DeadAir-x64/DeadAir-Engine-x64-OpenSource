#include "pch_script.h"
#include "da_memory_probe.h"

#include "Level.h"
#include "ai_space.h"
#include "alife_simulator.h"
#include "alife_object_registry.h"

#include "xrScriptEngine/script_engine.hpp"
#include "xrEngine/IGame_Persistent.h"
#include "xrSound/Sound.h"
#include "game_sv_event_queue.h"
#include "game_sv_base.h"
#include "xrServer.h"
#include "xrEngine/XR_IOConsole.h"
#include "Common/object_broker.h"
#include <algorithm>
#include <windows.h>
#include <malloc.h> // _get_heap_handle: куча CRT, единственная, где живут наши аллокации

// Крутилки объявлены здесь, а определены ниже: обход куч лежит в безымянном пространстве имён выше
// по файлу и обращается к ним.
extern int g_da_mem_trap_size;
// Ловушка в аллокаторе живёт в ядре (xrMemory.cpp); отсюда её только взводим.
extern int g_da_alloc_trap_size;
extern int g_da_alloc_trap_left;

namespace
{
// Прогонов держим немного: таблица должна оставаться читаемой в логе, а протокол требует трёх.
constexpr size_t MAX_RUNS = 8;
constexpr size_t MAX_MARKS = 24;
constexpr int SETTLE_FRAMES = 180; // три секунды при 60 кадрах
// Точные размеры блоков от 0 до 64 КБ. Порог поднят с 4 КБ после того, как замер показал: весь вес
// утечки лежит в блоках около двадцати килобайт, а мелочь объясняла четыре процента роста.
// 64 тысячи счётчиков по четыре байта — четверть мегабайта на прогон, это ничто.
constexpr size_t SIZE_BUCKETS = 65537;
// Крупные блоки — по степеням двойки от 4 КБ и выше. Первая версия гистограммы обрывалась на 4 КБ,
// и это было ошибкой: мелочь объяснила лишь несколько мегабайт из ста восьмидесяти, а весь вес
// оказался в блоках, которых она не видела.
constexpr size_t BIG_BUCKETS = 24;
// Сколько РАЗЛИЧНЫХ точных размеров крупных блоков помещается в снимок. Замер на Кордоне даёт около
// пятисот блоков свыше 64 КБ, различных размеров среди них меньше; с запасом хватает.
constexpr size_t BIG_EXACT_MAX = 4096;

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
    // Номер кадра на момент снимка. Для покадровой утечки это ЕДИНСТВЕННАЯ верная ось: при
    // rs_always_active игра без фокуса продолжает рисовать, и за одну и ту же минуту может пройти
    // и три тысячи кадров, и сорок. Утечка на кадр, поделённая на часы, выглядит то смертельной,
    // то отсутствующей — ровно на этом и обманулись, пока считали по времени.
    u32 frame = 0;
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
    // Сколько живых блоков какого РАЗМЕРА. Размер — это подпись структуры: если от прогона к
    // прогону прибавляется тридцать тысяч блоков ровно по 96 байт, остаётся найти, что такое эти
    // 96 байт, и утечка названа. Хранятся только размеры до SIZE_BUCKETS, крупных блоков мало и они
    // видны по общей сумме.
    xr_vector<u32> size_hist;
    xr_vector<u32> big_hist;  // штук
    xr_vector<size_t> big_kb; // и сколько килобайт в каждом ведре
    // Крупные блоки ТОЧНЫМ размером, а не ведром. Вёдра по степеням двойки сказали «течёт один блок
    // 8-16 МБ на перезагрузку» — этого мало: чтобы навести на него ловушку аллокатора и получить
    // стек, нужен ровный размер. Пар немного (несколько сотен различных размеров), поэтому храним
    // списком, а не гистограммой на четыре миллиона ведёр.
    xr_vector<std::pair<u32, u32>> big_exact; // размер -> сколько таких блоков
    // По кучам: у процесса их несколько, и каждую заводит своя библиотека. Если растущие блоки
    // сидят в отдельной куче, подсистема названа без всякой возни со стеками вызовов.
    xr_vector<size_t> heap_kb_by_heap;
    xr_vector<u32> heap_susp_by_heap; // блоков размером 16..32 КБ — тех самых
    // Какие звуки звучат: имя -> {всего копий, из них БЕЗ живого объекта-владельца}.
    // Осиротевший эмиттер — это и есть утечка в чистом виде: объект снесён, звук продолжает играть.
    struct SndItem { shared_str name; u32 total; u32 orphan; };
    xr_vector<SndItem> snd_top;
    size_t snd_playing = 0;   // звуков проигрывается сейчас
    size_t snd_events = 0;    // событий звуковой сцены
    size_t ps_active = 0;     // живых систем частиц
    size_t ps_destroy = 0;    // ждут уничтожения
    size_t ps_needtoplay = 0; // ждут запуска
    size_t ev_ready = 0;      // событий сервера в очереди
    size_t ev_unused = 0;     // и в пуле свободных — каждое несёт пакет на 16 КБ
    size_t net_ready = 0;     // входящих пакетов у клиента
    size_t net_unused = 0;    // и в его пуле — вот это и есть NET_Packet по 16405 байт
    bool objects_pending = false; // объекты считаются не сразу, см. DA_MemTick
};

xr_vector<Run> g_runs;
bool g_run_open = false;

// [DA_PORT] Короткая сводка по каждой загрузке — отдельно от Run и с куда большим запасом.
//
// Зачем отдельно: Run весит четверть мегабайта (гистограмма на 65537 ведёр), поэтому их держим восемь.
// Для поиска утечки на СМЕНЕ УРОВНЯ этого мало: в моде 34 локации, и чтобы увидеть, растёт ли
// стоимость одного и того же места при повторных заходах, нужен десяток-другой переходов в один
// сеанс. Здесь на запись уходят несколько чисел, так что их можно хранить много.
constexpr size_t MAX_SUMMARY = 64;

struct Summary
{
    shared_str what;
    u32 frame = 0;
    size_t committed_kb = 0;
    size_t heap_kb = 0;
    size_t heap_blocks = 0;
};

xr_vector<Summary> g_summary;

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
// Ведро по старшему значащему биту: границы — честные степени двойки. Прошлая версия делила размер
// пополам до 4096 и для «некруглых» размеров промахивалась на ведро, из-за чего подпись в таблице
// не сходилась с суммой мегабайт.
size_t big_bucket(size_t size)
{
    size_t bucket = 0;
    while (size >= (size_t(1) << (17 + bucket)) && bucket + 1 < BIG_BUCKETS)
        ++bucket;
    return bucket;
}

// Образцы содержимого пойманных блоков: начало каждого, столько байт, сколько влезает в строку лога.
constexpr size_t SAMPLES = 4;
constexpr size_t SAMPLE_BYTES = 96;
u8 g_samples[SAMPLES][SAMPLE_BYTES];
size_t g_samples_taken = 0;

void walk_heaps(size_t& blocks, size_t& bytes, xr_vector<u32>& hist, xr_vector<u32>& big,
    xr_vector<size_t>& big_kb, xr_vector<size_t>& per_heap_kb, xr_vector<u32>& per_heap_susp,
    xr_vector<std::pair<u32, u32>>& big_exact)
{
    // [DA_PORT] Ловушка аллокатора обязана молчать на время обхода, иначе игра падает в HeapLock.
    // Как это выглядело: ловушка стояла на 128 байт (буферы графа сцены), da_mem_test при старте
    // прогона сам перевзводит её (см. ниже g_da_alloc_trap_left = 6), и она срабатывала ИЗ РАБОЧИХ
    // ПОТОКОВ рендера. Снятие стека само зовёт DbgHelp и выделяет память — а мы в этот момент
    // держим кучу залоченной из главного потока. Отсюда падение в walk_heaps.
    // Гасим по значению, а не по счётчику: счётчик прогон перевзведёт заново.
    const int trap_size_saved = g_da_alloc_trap_size;
    g_da_alloc_trap_size = 0;

    g_samples_taken = 0;
    blocks = 0;
    bytes = 0;
    hist.assign(SIZE_BUCKETS, 0);
    big.assign(BIG_BUCKETS, 0);
    big_kb.assign(BIG_BUCKETS, 0);
    per_heap_kb.clear();
    per_heap_susp.clear();
    // Ёмкость берётся ДО блокировки кучи и внутри обхода уже не растёт — см. пояснение на вставке.
    big_exact.clear();
    big_exact.reserve(BIG_EXACT_MAX);

    // [DA_PORT] Обходим ТОЛЬКО СВОИ кучи, а не все кучи процесса.
    //
    // Прежняя версия брала список через GetProcessHeaps и лочила каждую. Это роняло игру: часть куч
    // процесса заводят и рушат чужие библиотеки — драйвер видеокарты и NGX, — и между получением
    // списка и HeapLock хендл успевал протухнуть. Падение приходило внутри RtlLockHeap, то есть уже
    // за пределами нашего кода, и выглядело как порча кучи, хотя это была гонка. Окно широкое:
    // обход многогигабайтной кучи занимает секунды, и всё это время список считается актуальным.
    //
    // Чужие кучи нам не нужны ни для чего. При USE_PURE_ALLOC (наша конфигурация, см.
    // MEMORY_ALLOCATOR=standard) весь xrMemory сводится к malloc/free, то есть к куче CRT; плюс
    // куча процесса по умолчанию, откуда идут прямые HeapAlloc. Всё, что мы способны утечь, лежит
    // здесь, и обе найденные утечки нашлись ровно в этих числах.
    //
    // Разбивка по чужим кучам, которую мы теряем, ни разу никого не назвала — виновника оба раза
    // назвала гистограмма по размеру блока. Так что размен односторонний: уходит только риск.
    HANDLE heaps[4];
    DWORD count = 0;
    if (const HANDLE crt = (HANDLE)_get_heap_handle())
        heaps[count++] = crt;
    if (const HANDLE def = GetProcessHeap())
    {
        if (count == 0 || def != heaps[0])
            heaps[count++] = def;
    }

    for (DWORD i = 0; i < count; ++i)
    {
        per_heap_kb.push_back(0);
        per_heap_susp.push_back(0);

        if (!HeapLock(heaps[i]))
            continue;

        PROCESS_HEAP_ENTRY entry{};
        while (HeapWalk(heaps[i], &entry))
        {
            if (entry.wFlags & PROCESS_HEAP_ENTRY_BUSY)
            {
                ++blocks;
                bytes += entry.cbData;
                // Образец содержимого — только пока держим кучу залоченной, иначе блок могут
                // освободить у нас под руками.
                if ((int)entry.cbData == g_da_mem_trap_size && g_samples_taken < SAMPLES &&
                    entry.lpData)
                {
                    CopyMemory(g_samples[g_samples_taken], entry.lpData, SAMPLE_BYTES);
                    ++g_samples_taken;
                }

                per_heap_kb.back() += entry.cbData / 1024;
                if (entry.cbData >= 16 * 1024 && entry.cbData < 32 * 1024)
                    ++per_heap_susp.back();

                if (entry.cbData < SIZE_BUCKETS)
                    ++hist[entry.cbData];
                else
                {
                    const size_t b = big_bucket(entry.cbData);
                    ++big[b];
                    big_kb[b] += entry.cbData / 1024;

                    // И тот же блок — точным размером.
                    //
                    // ⚠️ Здесь нельзя ВЫДЕЛЯТЬ память: куча залочена нами, и HeapWalk требует, чтобы
                    // во время обхода её не меняли — своя же аллокация способна сбить перебор.
                    // Поэтому ёмкость выбрана заранее (см. reserve выше), а рост списка ограничен
                    // ею же; линейный поиск по той же причине, карта звала бы аллокатор.
                    const u32 sz = (u32)entry.cbData;
                    bool found = false;
                    for (auto& it : big_exact)
                        if (it.first == sz)
                        {
                            ++it.second;
                            found = true;
                            break;
                        }
                    if (!found && big_exact.size() < big_exact.capacity())
                        big_exact.push_back({ sz, 1 });
                }
            }
        }
        HeapUnlock(heaps[i]);
    }

    g_da_alloc_trap_size = trap_size_saved;
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

// Выключатель: da_mem_probe 1 включает автоматические отметки и печать в конце загрузки.
// Ручной da_mem_dump работает всегда — он печатает то, что успели накопить.
//
// [DA_PORT] По умолчанию ВЫКЛЮЧЕНО. Было включено на время охоты за утечкой, и это оказалось
// миной: зонд сам запускался на КАЖДОЙ загрузке уровня и ронял игру в обходе куч (см. ниже).
// Инструмент отладки не должен работать у игрока — включать руками, когда что-то ищем.
int g_da_mem_probe = 0;

// Обход куч: даёт ЖИВЫЕ аллокации вместо закоммиченной памяти — только он отличает утечку от
// удержания памяти аллокатором.
//
// ⚠️ ИСТОРИЯ, чтобы не вернуться к прежнему устройству: до 30.07 обход брал список через
// GetProcessHeaps и лочил ВСЕ кучи процесса — и ронял игру через раз, падением внутри RtlLockHeap
// (наблюдалось четырежды). Причиной считали неустранимую гонку и записали «закрыть нечем: SEH в
// MinGW недоступен». Вывод был неверен: гонку не надо было закрывать, в неё не надо было входить.
// Чужие кучи заводят драйвер и NGX, они же их и рушат, а нам они не нужны — наши аллокации лежат
// в куче CRT. Обход сузили до своих куч (см. walk_heaps), и причина падения исчезла вместе с
// перебором. Урок: «неустранимо» стоит перечитывать как «не тот вопрос».
//
// По умолчанию всё равно выключен, но уже по другой причине: обход держит кучу залоченной, а на
// многогигабайтной куче это заметная пауза. Это цена замера, а не риск вылета.
int g_da_mem_heapwalk = 0;

// Размер блока, по которому идёт охота. Замер назвал 16413 байт: их прибавляется ровно 2547 штук на
// каждую перезагрузку. Ни файла, ни структуры такого размера не нашлось, поэтому смотрим внутрь —
// содержимое блока называет владельца быстрее любых догадок.
int g_da_mem_trap_size = 16413;

namespace
{
int g_repeat_left = 0;      // сколько загрузок осталось в авто-прогоне
int g_settle_frames = 0;    // пауза, пока мир устаивается, кадров
bool g_finish_pending = false; // итог прогона ещё не снят
} // namespace

void DA_MemReset()
{
    g_runs.clear();
    g_summary.clear();
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

    // Взвести ловушку аллокатора на каждую загрузку. Вручную это бесполезно: счётчик срабатываний
    // расходуется на первых же выделениях при старте игры, а нужны те, что происходят В МОМЕНТ
    // перезагрузки мира. Ровно на этом первая попытка и промахнулась.
    if (g_da_alloc_trap_size > 0)
        g_da_alloc_trap_left = 6;

    DA_MemMark("начало");
}

namespace
{
// Отметка без оглядки на выключатель: прогон уже существует, значит замер идёт. Выключатель
// проверяют ВХОДЫ (автоматические хуки загрузки), а не сама запись.
void push_mark(Run& run, const char* label)
{
    if (run.marks.size() >= MAX_MARKS)
        return;

    const size_t now = committed_kb();
    Mark m;
    m.label = label ? label : "?";
    m.committed_kb = now;
    m.delta_kb = run.marks.empty() ? 0 : (long long)now - (long long)run.marks.back().committed_kb;
    run.marks.push_back(m);
}
} // namespace

void DA_MemMark(const char* label)
{
    if (!g_da_mem_probe)
        return;
    if (!g_run_open || g_runs.empty())
        return;

    push_mark(g_runs.back(), label);
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
// Подпись отметки задаётся снаружи: тот же код снимает и итог загрузки, и снимок по команде
// (da_mem_snap), а «после прогрева» в снимке посреди игры читалось бы как ложь.
void finish_run(Run& run, const char* mark_label = "после прогрева")
{
    push_mark(run, mark_label);

    run.frame = Device.dwFrame;
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
        walk_heaps(blocks, bytes, run.size_hist, run.big_hist, run.big_kb, run.heap_kb_by_heap,
            run.heap_susp_by_heap, run.big_exact);
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
    // Очередь событий сервера: каждое событие несёт NET_Packet на 16 КБ, и пул свободных событий
    // движок сокращает лишь по одному за вызов и только если минуту никто ничего не создавал.
    if (g_pGameLevel && Level().Server)
    {
        // GetGameState отдаёт интерфейс; очередь событий живёт в конкретной реализации.
        if (game_sv_GameState* game = smart_cast<game_sv_GameState*>(Level().Server->GetGameState()))
        {
            if (GameEventQueue* q = game->event_queue())
            {
                run.ev_ready = q->ready_count();
                run.ev_unused = q->unused_count();
            }
        }
    }

    if (g_pGameLevel)
    {
        run.net_ready = Level().net_queue_ready();
        run.net_unused = Level().net_queue_unused();
    }

    if (g_pGamePersistent)
    {
        run.ps_active = g_pGamePersistent->ps_active.size();
        run.ps_destroy = g_pGamePersistent->ps_destroy.size();
        run.ps_needtoplay = g_pGamePersistent->ps_needtoplay.size();
    }

    // Короткая сводка живёт дольше подробных прогонов — см. MAX_SUMMARY.
    {
        if (g_summary.size() >= MAX_SUMMARY)
            g_summary.erase(g_summary.begin());
        Summary s;
        s.what = run.what;
        s.frame = run.frame;
        s.committed_kb = run.committed_kb;
        s.heap_kb = run.heap_kb;
        s.heap_blocks = run.heap_blocks;
        g_summary.push_back(s);
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

// Размеры кандидатов: сравнить с размером пойманного блока и не гадать по полям.
void DA_MemSizes()
{
    Msg("~ [DA_MEM] sizeof: NET_Buffer=%u NET_Packet=%u GameEvent=%u",
        (u32)sizeof(NET_Buffer), (u32)sizeof(NET_Packet), (u32)sizeof(GameEvent));
}

void DA_MemTestStart(int runs)
{
    DA_MemSizes();

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

// Снимок по команде — замер для ОБЫЧНОЙ ИГРЫ, без перезагрузок.
//
// Зачем отдельно от da_mem_test: тот меряет только путь загрузки, а обе найденные утечки жили в
// разных местах. Первая (пакеты) действительно копилась и на перезагрузках, но вторая — узлы
// xr_fixed_map в графе сцены — текла ПОКАДРОВО, во время игры, и на протоколе «загрузить сейв
// трижды» была почти не видна: за три загрузки набегает несколько тысяч кадров, а за четверть часа
// игры — сотни тысяч. Проверять покадровую утечку тестом загрузок бессмысленно.
//
// Как пользоваться: `da_mem_probe 1`, `da_mem_heapwalk 1`, затем `da_mem_snap` — играть 5-10 минут —
// `da_mem_snap` — ещё столько же — `da_mem_snap`. Дальше решает НЕ разница закоммиченного, а таблицы
// живых блоков и «какие блоки прибавились»: они называют размер структуры-виновника.
//
// ⚠️ Обход куч гоночный и роняет игру через раз (см. g_da_mem_heapwalk). Снимайте снимок в
// спокойном месте, не в бою, и не чаще, чем нужно.
void DA_MemSnap()
{
    if (!g_pGameLevel)
    {
        Msg("~ [DA_MEM] снимок снимается в игре: уровень не загружен");
        return;
    }

    if (g_run_open)
    {
        Msg("~ [DA_MEM] идёт загрузка, снимок отложите до её конца");
        return;
    }

    if (g_runs.size() >= MAX_RUNS)
        g_runs.erase(g_runs.begin());

    g_runs.push_back(Run());
    Run& run = g_runs.back();

    string128 what;
    xr_sprintf(what, "игра, кадр %u", Device.dwFrame);
    run.what = what;

    // Объекты здесь считаются сразу, в отличие от загрузки: мир уже живой, ждать спавна не нужно.
    run.objects = Level().Objects.o_count();
    run.objects_pending = false;

    finish_run(run, "снимок");
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
    Msg("~ [DA_MEM] замер | что                  | всего МБ | прирост | кадров с прошлого | МБ/1000 кадров");
    for (size_t i = 0; i < g_runs.size(); ++i)
    {
        const Run& r = g_runs[i];
        const long long d = (i == 0) ? 0
                                     : (long long)r.committed_kb - (long long)g_runs[i - 1].committed_kb;
        // Прирост, приведённый к кадрам. Покадровую утечку видно ТОЛЬКО так: в мегабайтах за замер
        // она смешана с тем, сколько кадров успело пройти, а это зависит от частоты кадров и от
        // того, сидел ли игрок в альт-табе. Полка в этой колонке = утечка на кадр.
        const long long frames = (i == 0 || r.frame <= g_runs[i - 1].frame)
            ? 0
            : (long long)r.frame - (long long)g_runs[i - 1].frame;
        string32 per_kf;
        if (frames > 0)
            xr_sprintf(per_kf, "%s%lld", sign(d * 1000 / frames), d * 1000 / frames / 1024);
        else
            xr_sprintf(per_kf, "%s", "-");

        Msg("~ [DA_MEM]   %-4u | %-20s | %8u | %s%lld МБ | %17lld | %s", (u32)(i + 1), r.what.c_str(),
            (u32)(r.committed_kb / 1024), sign(d), d / 1024, frames, per_kf);
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
                "монотонно от замера к замеру");
    }
    else
    {
        Msg("~ [DA_MEM] для вывода нужно ТРИ замера подряд, не выходя из игры: либо три загрузки "
            "одного сохранения (da_mem_test 3), либо три снимка da_mem_snap с игрой между ними. "
            "Сделано: %u", (u32)g_runs.size());
    }

    // --- по уровням: одно и то же место при ПОВТОРНЫХ посещениях ---
    //
    // Для переходов между уровнями таблица выше бесполезна: «прирост к прошлому» сравнивает соседние
    // прогоны, а у разных уровней разное население и геометрия, и любая разница объясняется этим, а
    // не утечкой. Единственный осмысленный вопрос — сколько стоит ОДИН И ТОТ ЖЕ уровень при первом и
    // при повторном заходе, поэтому прогоны группируются по имени.
    //
    // Протокол: A -> B -> A -> B -> A. Если живые мегабайты уровня A растут от захода к заходу,
    // выгрузка не отдаёт, и это утечка на пути СМЕНЫ уровня — том самом, который перезагрузка
    // сохранения не задействует вовсе (при ней уровень остаётся в памяти).
    // Считается по КОРОТКОЙ сводке, а не по прогонам: подробных прогонов восемь, а переходов между
    // локациями за сеанс нужно куда больше (см. MAX_SUMMARY).
    {
        xr_vector<shared_str> levels;
        for (const Summary& s : g_summary)
        {
            bool known = false;
            for (const shared_str& l : levels)
                if (l == s.what)
                {
                    known = true;
                    break;
                }
            if (!known)
                levels.push_back(s.what);
        }

        bool repeated = false;
        for (const shared_str& lv : levels)
        {
            size_t visits = 0;
            for (const Summary& s : g_summary)
                if (s.what == lv)
                    ++visits;
            if (visits < 2)
                continue;

            if (!repeated)
            {
                Msg("~ [DA_MEM] --- повторные заходы на ОДИН уровень: живых МБ / блоков / закоммичено ---");
                repeated = true;
            }

            string512 line;
            xr_sprintf(line, "~ [DA_MEM]   %s", lv.c_str());
            pad_to(line, 30);
            for (const Summary& s : g_summary)
            {
                if (s.what != lv)
                    continue;
                string64 cell;
                xr_sprintf(cell, " %5u/%7u/%5u", (u32)(s.heap_kb / 1024), (u32)s.heap_blocks,
                    (u32)(s.committed_kb / 1024));
                xr_strcat(line, cell);
            }
            Msg("%s", line);
        }

        if (repeated)
            Msg("~ [DA_MEM] Растут ЖИВЫЕ мегабайты одного и того же уровня от захода к заходу — это "
                "утечка на пути СМЕНЫ уровня. Стоят — путь чист, а рост закоммиченного объясняется "
                "удержанием памяти аллокатором.");
        else if (levels.size() > 1)
            Msg("~ [DA_MEM] Уровни все РАЗНЫЕ, сравнивать их между собой нельзя: у них разное "
                "население и геометрия. Для поиска утечки на переходах пройдите A -> B -> A -> B -> A, "
                "тогда появится сводка по повторным заходам.");
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
    Msg("~ [DA_MEM] замер | живых МБ | блоков | закоммичено МБ | разница | блоков/1000 кадров");
    for (size_t i = 0; i < g_runs.size(); ++i)
    {
        const Run& r = g_runs[i];
        // Живые блоки на тысячу кадров — самая прямая мера покадровой утечки: она не зависит ни от
        // частоты кадров, ни от того, сколько аллокатор придержал. Именно эта величина отличила
        // утечку узлов графа сцены от обычного удержания памяти.
        const long long frames = (i == 0 || r.frame <= g_runs[i - 1].frame)
            ? 0
            : (long long)r.frame - (long long)g_runs[i - 1].frame;
        const long long db = (i == 0) ? 0
            : (long long)r.heap_blocks - (long long)g_runs[i - 1].heap_blocks;
        string32 per_kf;
        if (frames > 0 && r.heap_blocks)
            xr_sprintf(per_kf, "%s%lld", sign(db * 1000 / frames), db * 1000 / frames);
        else
            xr_sprintf(per_kf, "%s", "-");

        Msg("~ [DA_MEM]   %-4u | %8u | %6u | %14u | %7u | %s", (u32)(i + 1), (u32)(r.heap_kb / 1024),
            (u32)r.heap_blocks, (u32)(r.committed_kb / 1024),
            (u32)((r.committed_kb - r.heap_kb) / 1024), per_kf);
    }
    // Какие размеры блоков прибавляются от первого прогона к последнему: подпись утечки.
    const Run& first_run = g_runs.front();
    const Run& last_run = g_runs.back();
    if (first_run.size_hist.size() == SIZE_BUCKETS && last_run.size_hist.size() == SIZE_BUCKETS)
    {
        xr_vector<std::pair<size_t, long long>> growth;
        for (size_t sz = 0; sz < SIZE_BUCKETS; ++sz)
        {
            const long long d = (long long)last_run.size_hist[sz] - (long long)first_run.size_hist[sz];
            if (d > 0)
                growth.push_back({ sz, d });
        }
        std::sort(growth.begin(), growth.end(),
            [](const auto& a, const auto& b) { return a.second * (long long)a.first > b.second * (long long)b.first; });

        Msg("~ [DA_MEM] --- какие блоки прибавились с первого прогона (по весу; размеры до 64 КБ) ---");
        Msg("~ [DA_MEM] размер, байт | прибавилось штук | это МБ | было -> стало");
        const size_t shown = growth.size() < 12 ? growth.size() : 12;
        for (size_t k = 0; k < shown; ++k)
        {
            const size_t sz = growth[k].first;
            Msg("~ [DA_MEM]   %10u | %16lld | %6lld | %u -> %u", (u32)sz, growth[k].second,
                growth[k].second * (long long)sz / 1024 / 1024, first_run.size_hist[sz],
                last_run.size_hist[sz]);
        }
    }

    // Крупные блоки: именно они несут вес.
    if (first_run.big_hist.size() == BIG_BUCKETS && last_run.big_hist.size() == BIG_BUCKETS)
    {
        Msg("~ [DA_MEM] --- крупные блоки: от->до, штук и МБ по прогонам ---");
        for (size_t b = 0; b < BIG_BUCKETS; ++b)
        {
            if (last_run.big_hist[b] == 0 && first_run.big_hist[b] == 0)
                continue;

            const size_t from_kb = (size_t(64) << b); // ведро b начинается с 64 КБ << b
            string512 line;
            xr_sprintf(line, "~ [DA_MEM]   %6u..%-6u КБ", (u32)from_kb, (u32)(from_kb * 2));
            pad_to(line, 32);
            for (const Run& r : g_runs)
            {
                string64 cell;
                if (r.big_hist.size() == BIG_BUCKETS)
                    xr_sprintf(cell, " %6u/%-6u", r.big_hist[b], (u32)(r.big_kb[b] / 1024));
                else
                    xr_sprintf(cell, " %13s", "-");
                xr_strcat(line, cell);
            }
            Msg("%s", line);
        }
        Msg("~ [DA_MEM] (в каждой клетке: штук блоков / их суммарные МБ)");
    }

    // Крупные блоки ТОЧНЫМ размером: что именно прибавилось. Ведро «8-16 МБ» говорит, что течёт один
    // блок на перезагрузку, но навести ловушку аллокатора можно только на ровный размер — вот он.
    if (!first_run.big_exact.empty() && !last_run.big_exact.empty())
    {
        xr_vector<std::pair<u32, long long>> growth;
        for (const auto& last : last_run.big_exact)
        {
            long long before = 0;
            for (const auto& first : first_run.big_exact)
                if (first.first == last.first)
                {
                    before = first.second;
                    break;
                }
            const long long d = (long long)last.second - before;
            if (d > 0)
                growth.push_back({ last.first, d });
        }
        std::sort(growth.begin(), growth.end(),
            [](const auto& a, const auto& b) { return a.second * (long long)a.first > b.second * (long long)b.first; });

        if (!growth.empty())
        {
            Msg("~ [DA_MEM] --- крупные блоки, ТОЧНЫЙ размер: что прибавилось с первого замера ---");
            Msg("~ [DA_MEM] размер, байт | прибавилось штук | это МБ | стало штук | для ловушки");
            const size_t shown = growth.size() < 12 ? growth.size() : 12;
            for (size_t k = 0; k < shown; ++k)
            {
                u32 now = 0;
                for (const auto& it : last_run.big_exact)
                    if (it.first == growth[k].first)
                    {
                        now = it.second;
                        break;
                    }
                Msg("~ [DA_MEM]   %10u | %16lld | %6lld | %10u | da_alloc_trap_size %u",
                    growth[k].first, growth[k].second,
                    growth[k].second * (long long)growth[k].first / 1024 / 1024, now, growth[k].first);
            }
            // Подсказка печатается по одной команде на строку НАМЕРЕННО: консоль разбирает ввод как
            // «первое слово — команда, остаток — аргументы», разделителя ';' у неё нет. Список через
            // запятую уже был понят как одна команда: da_alloc_trap_size молча остался нулём, а
            // прогон прошёл без единого стека.
            Msg("~ [DA_MEM] Дальше — по ОДНОЙ команде на строку (';' консоль не понимает):");
            Msg("~ [DA_MEM]   da_alloc_trap_slack 64");
            Msg("~ [DA_MEM]   da_alloc_trap_size %u", growth[0].first);
            Msg("~ [DA_MEM]   da_mem_test 2");
            Msg("~ [DA_MEM] В логе появится [DA_TRAP] со стеком в момент выделения. Потом вернуть "
                "da_alloc_trap_size 0 — крутилка сохраняется в user.ltx.");
        }
    }

    // По кучам: где именно сидят растущие блоки.
    {
        size_t heaps_count = 0;
        for (const Run& r : g_runs)
            if (r.heap_kb_by_heap.size() > heaps_count)
                heaps_count = r.heap_kb_by_heap.size();

        if (heaps_count)
        {
            Msg("~ [DA_MEM] --- по кучам процесса: МБ / блоков 16-32 КБ ---");
            for (size_t h = 0; h < heaps_count; ++h)
            {
                string512 line;
                xr_sprintf(line, "~ [DA_MEM]   куча %u", (u32)h);
                pad_to(line, 20);
                for (const Run& r : g_runs)
                {
                    string64 cell;
                    if (h < r.heap_kb_by_heap.size())
                        xr_sprintf(cell, " %6u/%-6u", (u32)(r.heap_kb_by_heap[h] / 1024),
                            r.heap_susp_by_heap[h]);
                    else
                        xr_sprintf(cell, " %13s", "-");
                    xr_strcat(line, cell);
                }
                Msg("%s", line);
            }
        }
    }

    // Что лежит внутри пойманных блоков.
    if (g_samples_taken)
    {
        Msg("~ [DA_MEM] --- начало блоков размером %d байт ---", g_da_mem_trap_size);
        for (size_t k = 0; k < g_samples_taken; ++k)
        {
            string512 text, hex;
            xr_strcpy(text, "");
            xr_strcpy(hex, "");
            for (size_t b = 0; b < 48; ++b)
            {
                const u8 c = g_samples[k][b];
                string16 t;
                xr_sprintf(t, "%c", (c >= 32 && c < 127) ? (char)c : '.');
                xr_strcat(text, t);
                xr_sprintf(t, "%02X ", c);
                xr_strcat(hex, t);
            }
            Msg("~ [DA_MEM]   #%u текст: %s", (u32)k, text);
            Msg("~ [DA_MEM]   #%u байты: %s", (u32)k, hex);
        }
    }

    Msg("~ [DA_MEM] Растут ЖИВЫЕ мегабайты — это утечка. Растёт только закоммичено, а живые стоят —"
        " память держит аллокатор, и это не дефект.");

    Msg("~ [DA_MEM] --- подсистемы в конце каждого прогона ---");
    Msg("~ [DA_MEM] --- очередь событий сервера (каждое событие = пакет 16 КБ) ---");
    Msg("~ [DA_MEM] прогон | события: в очереди/в пуле | пакеты клиента: в очереди/в пуле | пул МБ");
    for (size_t i = 0; i < g_runs.size(); ++i)
    {
        const Run& r = g_runs[i];
        Msg("~ [DA_MEM]   %-4u | %11u/%-11u | %16u/%-10u | %u", (u32)(i + 1), (u32)r.ev_ready,
            (u32)r.ev_unused, (u32)r.net_ready, (u32)r.net_unused,
            (u32)((r.net_ready + r.net_unused) * 16405 / 1024 / 1024));
    }

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

// [DA_PORT] ---- Отложенное выполнение команды: da_after_load ---------------------------------------
//
// Зачем. Вылет у тестера воспроизводится связкой «загрузить сейв -> сбросить устройство», а сброс
// делает консольная команда (vid_restart, смена апскейлера). Загрузку сейва можно задать из
// командной строки (-start server(...)), а вот команду В ИГРЕ выполнить нечем: user.ltx исполняется
// до того, как уровень появился, и `Device.b_is_Ready` там ещё ложь - сброса не происходит.
//
// Отсюда механизм: `da_after_load <кадров> <команда>` ждёт, пока уровень действительно загрузится,
// отсчитывает заданное число кадров и выполняет команду. Этого хватает, чтобы прогнать сценарий
// целиком без единого нажатия клавиши - и повторить его столько раз, сколько нужно, чтобы поймать
// гонку, которая случается не каждый раз.
//
// Полезно не только для этого вылета: тем же способом проверяются пункты «проверить в игре» из
// дорожной карты.
namespace
{
string512 g_after_load_cmd = {};
int g_after_load_frames = -1; // -1 = выключено
}

void DA_AfterLoadArm(int frames, pcstr command)
{
    if (frames < 0 || !command || !*command)
    {
        g_after_load_frames = -1;
        g_after_load_cmd[0] = 0;
        Msg("~ [DA_AFTER] отложенная команда снята");
        return;
    }
    g_after_load_frames = frames;
    xr_strcpy(g_after_load_cmd, sizeof(g_after_load_cmd), command);
    Msg("~ [DA_AFTER] через %d кадров после загрузки уровня будет выполнено: %s", frames,
        g_after_load_cmd);
}

void DA_AfterLoadTick()
{
    if (g_after_load_frames < 0)
        return;
    // Достаточно самого уровня: этот тик и зовётся из CLevel::OnFrame, то есть уровень уже есть.
    // Флаг bReady проверять НЕЛЬЗЯ - он у IGame_Level никем не выставляется, и условие не срабатывало
    // никогда: первый прогон воспроизведения молча простоял в меню и ничего не выполнил.
    if (!g_pGameLevel)
        return;

    if (g_after_load_frames > 0)
    {
        --g_after_load_frames;
        return;
    }

    g_after_load_frames = -1;
    Msg("~ [DA_AFTER] выполняю: %s", g_after_load_cmd);
    FlushLog();
    Console->Execute(g_after_load_cmd);
}

// [DA_PORT] ---- Команда из командной строки: -da_cmd "<консольная команда>" -------------------
//
// Нужен автотестам (da_port/tools/run_headless.ps1). Своего механизма «выполнить консольную
// команду при запуске» у движка нет: `-r4`, `-nointro` и прочие разбираются поимённо, а всё
// остальное игнорируется. Без этого моста прогон на скрытом рабочем столе умеет только
// запуститься и висеть до таймаута - ни `da_after_load`, ни `quit` ему не отдать.
//
// Один раз за сессию, на первом же кадре с уровнем ИЛИ главным меню. Кавычки обязательны,
// если в команде есть пробелы: `-da_cmd "da_after_load 300 quit"`.
//
// ⚠️ Берётся ТОЛЬКО ПЕРВОЕ вхождение. Несколько команд задаются через консольный разделитель
// внутри одних кавычек: `-da_cmd "load_last_save ; da_after_load 600 quit"`.
void DA_StartupCommandTick()
{
    static bool done = false;
    if (done)
        return;

    pcstr params = Core.Params;
    if (!params)
    {
        done = true;
        return;
    }

    pcstr at = strstr(params, "-da_cmd");
    if (!at)
    {
        done = true;
        return;
    }

    done = true;

    if (strstr(at + 1, "-da_cmd"))
        Msg("! [DA] -da_cmd указан несколько раз: выполнится только первый, остальные потеряны");

    at += xr_strlen("-da_cmd");
    while (*at == ' ')
        ++at;

    string512 command = {};
    if (*at == '"')
    {
        ++at;
        pcstr end = strchr(at, '"');
        if (!end)
        {
            Msg("! [DA] -da_cmd: незакрытая кавычка, команда не выполнена");
            return;
        }
        const size_t len = _min(size_t(end - at), sizeof(command) - 1);
        strncpy(command, at, len);
    }
    else
    {
        // Без кавычек берём до первого пробела - иначе съели бы остаток командной строки.
        sscanf(at, "%511s", command);
    }

    if (!command[0])
    {
        Msg("! [DA] -da_cmd: пустая команда");
        return;
    }

    Msg("~ [DA_STARTUP] выполняю: %s", command);
    FlushLog();
    Console->Execute(command);
}
