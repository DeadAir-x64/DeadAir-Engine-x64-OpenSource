#include "pch_script.h"
#include "da_memory_probe.h"

#include "Level.h"
#include "ai_space.h"
#include "alife_simulator.h"
#include "alife_object_registry.h"

#include "xrScriptEngine/script_engine.hpp"
#include "xrEngine/IGame_Persistent.h"
#include "Common/object_broker.h"

namespace
{
// Прогонов держим немного: таблица должна оставаться читаемой в логе, а протокол требует трёх.
constexpr size_t MAX_RUNS = 8;
constexpr size_t MAX_MARKS = 24;

struct Mark
{
    shared_str label;
    size_t committed_kb = 0; // память процесса на момент отметки
    size_t delta_kb = 0;     // сколько прибавилось с предыдущей отметки этого же прогона
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
    m.delta_kb = run.marks.empty() ? 0 : (now > run.marks.back().committed_kb
                                             ? now - run.marks.back().committed_kb
                                             : 0);
    run.marks.push_back(m);
}

void DA_MemRunEnd()
{
    if (!g_da_mem_probe)
        return;
    if (!g_run_open || g_runs.empty())
        return;

    DA_MemMark("конец");

    Run& run = g_runs.back();
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

    // Клиентские объекты в этот момент ещё НЕ созданы: сюда мы попадаем в конце net_start6, а спавн
    // приходит сетевыми сообщениями позже. Первый замер честно показывал ноль. Поэтому счёт объектов
    // откладывается до первого кадра, в котором они появились, — см. DA_MemTick.
    run.objects = 0;
    run.objects_pending = true;

    // Имя уровня в начале прогона ещё неизвестно — уточняем его здесь, чтобы в таблице было видно,
    // что сравниваются загрузки ОДНОГО уровня. Сравнивать разные бессмысленно: у них разное
    // население и геометрия, и любая разница объясняется этим, а не утечкой.
    if (g_pGameLevel && Level().name().size())
        run.what = Level().name();

    g_run_open = false;

    Msg("~ [DA_MEM] прогон %u (%s) завершён: %u МБ", (u32)g_runs.size(), run.what.c_str(),
        (u32)(run.committed_kb / 1024));

    DA_MemDump();
}

void DA_MemTick()
{
    if (!g_da_mem_probe || g_runs.empty())
        return;

    Run& run = g_runs.back();
    if (!run.objects_pending || !g_pGameLevel)
        return;

    const u32 count = Level().Objects.o_count();
    if (count == 0)
        return; // спавн ещё идёт

    run.objects = count;
    run.objects_pending = false;
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
        const long long d1 = (long long)g_runs[1].committed_kb - (long long)g_runs[0].committed_kb;
        const long long dn = (long long)g_runs.back().committed_kb -
            (long long)g_runs[g_runs.size() - 2].committed_kb;
        // Плато: последний шаг заметно меньше первого. Утечка: шаг держится.
        if (dn * 3 < d1)
            Msg("~ [DA_MEM] ВЫВОД: шаг падает (%lld -> %lld МБ) — похоже на плато, то есть удержание "
                "памяти аллокатором, а не утечку",
                d1 / 1024, dn / 1024);
        else
            Msg("~ [DA_MEM] ВЫВОД: шаг держится (%lld -> %lld МБ) — похоже на настоящую утечку на "
                "пути загрузки уровня. Смотрите таблицу фаз ниже: виновата та, что прибавляет из раза "
                "в раз",
                d1 / 1024, dn / 1024);
    }
    else
    {
        Msg("~ [DA_MEM] для вывода нужно ТРИ загрузки одного и того же сохранения подряд, не выходя "
            "из игры. Сделано: %u", (u32)g_runs.size());
    }

    // --- по фазам: где именно прибавляется ---
    Msg("~ [DA_MEM] --- прирост по фазам загрузки, МБ ---");
    const Run& first = g_runs.front();
    for (size_t m = 0; m < first.marks.size(); ++m)
    {
        string512 line;
        xr_sprintf(line, "~ [DA_MEM]   %s", first.marks[m].label.c_str());
        pad_to(line, 36); // выравнивание по символам: подписи русские
        for (size_t i = 0; i < g_runs.size(); ++i)
        {
            string32 cell;
            if (m < g_runs[i].marks.size())
                xr_sprintf(cell, " %7u", (u32)(g_runs[i].marks[m].delta_kb / 1024));
            else
                xr_sprintf(cell, " %7s", "-");
            xr_strcat(line, cell);
        }
        Msg("%s", line);
    }

    // --- подсистемы: что уже отсеяно замером, а что нет ---
    Msg("~ [DA_MEM] --- подсистемы в конце каждого прогона ---");
    Msg("~ [DA_MEM] прогон | текстуры МБ | Lua МБ | строк | объектов | ALife");
    for (size_t i = 0; i < g_runs.size(); ++i)
    {
        const Run& r = g_runs[i];
        Msg("~ [DA_MEM]   %-4u | %11u | %6u | %5u | %8u | %u", (u32)(i + 1),
            (u32)(r.textures_kb / 1024), (u32)(r.lua_kb / 1024), (u32)r.strings_count,
            (u32)r.objects, (u32)r.alife_objects);
    }

    Msg("~ [DA_MEM] Текстуры, строки и Lua уже отсеивались замером — если растут именно они, это "
        "новость. Обычный подозреваемый — нетрекаемые C++-аллокации: геометрия уровня, CDB, spatial, "
        "AI-граф, физика, звук.");
    Msg("~ ===============================================================");
}
