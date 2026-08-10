////////////////////////////////////////////////////////////////////////////
//  Module      : script_engine.cpp
//  Created     : 01.04.2004
//  Modified    : 01.04.2004
//  Author      : Dmitriy Iassenev
//  Description : XRay Script Engine
////////////////////////////////////////////////////////////////////////////

#include "pch.hpp"

#include "Common/Noncopyable.hpp"
#include "xrCore/ModuleLookup.hpp"

#include "script_engine.hpp"
#include "script_process.hpp"
#include "script_profiler.hpp"
#include "script_thread.hpp"
#include "BindingsDumper.hpp"
#ifdef USE_DEBUGGER
#include "script_debugger.hpp"
#endif
#ifdef DEBUG
#include "script_thread.hpp"
#endif

#include "xrLuaFix/xrLuaFix.h"

#include <tracy/TracyLua.hpp>

#include <luabind/class_info.hpp>

#include <stdarg.h>

Flags32 g_LuaDebug;
int g_LuaDumpDepth = 0;

#define SCRIPT_GLOBAL_NAMESPACE "_G"

static const char* file_header_old = "local function script_name() \
return \"%s\" \
end \
local this = {} \
%s this %s \
setmetatable(this, {__index = " SCRIPT_GLOBAL_NAMESPACE "}) \
setfenv(1, this) ";

static const char* file_header_new = "local function script_name() \
return \"%s\" \
end \
local this = {} \
this." SCRIPT_GLOBAL_NAMESPACE " = " SCRIPT_GLOBAL_NAMESPACE " \
%s this %s \
setfenv(1, this) ";

static const char* file_header = nullptr;

// [DA_PORT] Разбор мусора Lua ПО РАЗМЕРАМ БЛОКОВ.
//
// ЗАЧЕМ. Общий счёт известен — 3607 КБ/сек, из них по обновлениям биндеров разложились только
// 607: NPC 286, актёр 221, монстры 100. Остальные 3023 КБ/сек не привязаны ни к чему. Искать их
// профилировщиком по времени бесполезно: скрипты занимают 0.16 мс кадра, то есть в шуме.
//
// ⭐ Почему именно здесь. lua_alloc — единственная дверь, через которую LuaJIT ходит за памятью, и
// он получает СТАРЫЙ размер вместе с новым. Значит отсюда видно и сколько выделено, и сколько
// освобождено, точно, без выборки и без каких-либо допущений.
//
// ⭐ Почему по размерам, а не по функциям. Размер блока — это подпись структуры, и она называет
// виновника быстрее любого обхода стека: обёртка luabind на возвращённый указатель — это ровно
// 128 байт (замерено), таблица Lua — 64 плюс части, строка — 24 плюс длина. Тем же приёмом
// («гистограмма назвала виновника ДО чтения кода») найдена утечка узлов xr_fixed_map.
//
// ⛔ Хуками этого не сделать: активный call-хук в LuaJIT подменяет все входы функций и ОТКЛЮЧАЕТ
// запись новых трасс (Externals/LuaJIT/src/lj_dispatch.c). Профилировать при этом мы будем не ту
// игру, в которую играют. Счётчик в аллокаторе трассы не трогает вовсе.
namespace
{
constexpr size_t DA_LUA_EXACT = 2048; // точные размеры до 2 КБ: подпись структуры именно здесь
constexpr size_t DA_LUA_BIG = 24;     // выше — по степеням двойки

struct da_lua_counters
{
    u64 new_calls, new_bytes;      // ptr == nullptr: настоящее выделение
    u64 grow_calls, grow_bytes;    // nsize > osize: рост существующего блока
    u64 shrink_calls, shrink_bytes;// nsize < osize: усадка
    u64 free_calls, free_bytes;    // nsize == 0
    u64 exact[DA_LUA_EXACT];       // сколько выделений какого ТОЧНОГО размера
    u64 big[DA_LUA_BIG];
};

// Не atomic и не thread_local намеренно: состояние Lua у нас однопоточное (выход Lua на рабочие
// потоки был дефектом и закрыт), а атомарные счётчики в этом пути стоили бы дороже самого учёта.
da_lua_counters g_da_lua{};
u64 g_da_lua_since = 0;

inline void da_lua_note_new(size_t size)
{
    ++g_da_lua.new_calls;
    g_da_lua.new_bytes += size;
    if (size < DA_LUA_EXACT)
        ++g_da_lua.exact[size];
    else
    {
        size_t b = 0;
        while ((size >>= 1) && b + 1 < DA_LUA_BIG)
            ++b;
        ++g_da_lua.big[b];
    }
}
// Отдаёт счётчик в Lua: всего байт, и отдельно штуки по трём размерам, которые дают 99% блоков.
// Что есть что — установлено калибровкой, а не выведено из заголовков LuaJIT:
//   48 Б  — замыкание с одним upvalue     64 Б — пустая таблица     128 Б — обёртка luabind
// Возвращает числа, а не таблицу: числа в LuaJIT не объекты сборщика, значит сам опрос счётчика
// мусора не создаёт.
int da_lua_bytes_binding(lua_State* L)
{
    lua_pushnumber(L, double(g_da_lua.new_bytes + g_da_lua.grow_bytes));
    lua_pushnumber(L, double(g_da_lua.exact[48]));
    lua_pushnumber(L, double(g_da_lua.exact[128]));
    lua_pushnumber(L, double(g_da_lua.exact[64]));
    return 4;
}
} // namespace

static void* lua_alloc(void* ud, void* ptr, size_t osize, size_t nsize)
{
    (void)ud;
    if (nsize == 0)
    {
        if (ptr)
        {
            ++g_da_lua.free_calls;
            g_da_lua.free_bytes += osize;
        }
        xr_free(ptr);
        return nullptr;
    }
    if (!ptr)
        da_lua_note_new(nsize);
    else if (nsize > osize)
    {
        ++g_da_lua.grow_calls;
        g_da_lua.grow_bytes += nsize - osize;
    }
    else if (nsize < osize)
    {
        ++g_da_lua.shrink_calls;
        g_da_lua.shrink_bytes += osize - nsize;
    }
    return xr_realloc(ptr, nsize);
}

// Счётчики живут в самом LuaJIT (Externals/LuaJIT/src/lj_str.c): только там видно, выделяется ли
// под строку память или она нашлась в таблице интернирования.
extern "C" unsigned long long da_lj_str_new_count;
extern "C" unsigned long long da_lj_str_new_bytes;
// Кто создаёт замыкания — учёт в lj_func.c, где известен прототип с именем файла и строкой.
// Обёртки luabind по классам — счётчик в push_new_instance (Externals/luabind/src/object_rep.cpp).
extern "C" void da_ud_reset();
extern "C" int da_ud_get(int index, const char** name, unsigned long long* count);
extern "C" int da_udsite_get(int index, const char** src, int* line, unsigned long long* count);
extern "C" int da_dep_get(int index, const void** ret, unsigned long long* count);
extern "C" void da_ptr_stat(unsigned long long* total, unsigned long long* distinct, unsigned long long* overflow);
// Кэш обёрток по указателю: сброс привязан к жизни состояния Lua, опт-ин — по имени класса.
extern "C" void da_cache_reset(lua_State* L);
extern "C" void da_cache_enable_class(const char* name);
extern "C" void da_cache_stat(unsigned long long* hit, unsigned long long* miss, unsigned long long* full);
extern "C" unsigned long long da_ud_total_get();
extern "C" void da_tab_capi_reset();
extern "C" unsigned long long da_tab_capi_get();
extern "C" int da_tabc_get(int index, const void** ret, unsigned long long* count);
extern "C" void da_cc_reset();
extern "C" int da_cc_get(int index, void** fn, unsigned long long* count);
extern "C" unsigned long long da_cc_total;
extern "C" void da_fn_reset();
extern "C" int da_fn_get(int index, const char** chunk, int* line, unsigned long long* count);
extern "C" unsigned long long da_fn_total;
extern "C" unsigned long long da_fn_lost;
// Разбор по ТИПАМ объекта сборщика — счётчики в конструкторах LuaJIT. Размер блока типа не
// выдаёт: 48 байт дают и строка в 21 символ, и хеш-часть таблицы на два узла, и пустая userdata.
enum { DA_GC_FUNC, DA_GC_FUNCC, DA_GC_UPVAL, DA_GC_TAB, DA_GC_TABNODE, DA_GC_TABARRAY,
       DA_GC_UDATA, DA_GC_STR, DA_GC_GCO_ALL, DA_GC_RAW_ALL, DA_GC_KINDS };
extern "C" unsigned long long da_gc_count[DA_GC_KINDS];
extern "C" unsigned long long da_gc_bytes[DA_GC_KINDS];
static unsigned long long g_da_gc_base_count[DA_GC_KINDS];
static unsigned long long g_da_gc_base_bytes[DA_GC_KINDS];
// ⚠️ Отсечка окна — при СБРОСЕ, а не при печати. Если считать от прошлой печати, доля строк будет
// относиться к другому отрезку времени, чем всё остальное в отчёте, и разъедется незаметно.
static unsigned long long g_da_str_base_count = 0;
static unsigned long long g_da_str_base_bytes = 0;

XRSCRIPTENGINE_API void da_lua_alloc_reset()
{
    g_da_lua = da_lua_counters{};
    g_da_lua_since = CPU::QPC();
    g_da_str_base_count = da_lj_str_new_count;
    g_da_str_base_bytes = da_lj_str_new_bytes;
    da_fn_reset();
    da_cc_reset();
    da_ud_reset();
    da_tab_capi_reset();
    for (int k = 0; k < DA_GC_KINDS; ++k)
    {
        g_da_gc_base_count[k] = da_gc_count[k];
        g_da_gc_base_bytes[k] = da_gc_bytes[k];
    }
}

XRSCRIPTENGINE_API void da_lua_alloc_dump(int frames)
{
    // Снимок до печати: Msg сам выделяет память, и часть её пойдёт через Lua при логировании из
    // скриптов. Прибор, попавший в собственную таблицу, у нас уже был.
    da_lua_counters s = g_da_lua; // копия, а не ссылка: ниже мы вычёркиваем из неё найденные размеры
    const double secs = double(CPU::QPC() - g_da_lua_since) / double(CPU::qpc_freq);
    const double per_s = secs > 0.01 ? 1.0 / secs : 0.0;

    Msg("~ [DA_LUAMEM] окно %.1f с%s", secs, frames > 0 ? "" : " (кадры не заданы)");
    Msg("~ [DA_LUAMEM] выделено   %10llu блоков  %9.1f КБ/с", (unsigned long long)s.new_calls,
        s.new_bytes / 1024.0 * per_s);
    Msg("~ [DA_LUAMEM] рост блока %10llu раз     %9.1f КБ/с", (unsigned long long)s.grow_calls,
        s.grow_bytes / 1024.0 * per_s);
    Msg("~ [DA_LUAMEM] усадка     %10llu раз     %9.1f КБ/с", (unsigned long long)s.shrink_calls,
        s.shrink_bytes / 1024.0 * per_s);
    Msg("~ [DA_LUAMEM] освобождено %9llu блоков  %9.1f КБ/с", (unsigned long long)s.free_calls,
        s.free_bytes / 1024.0 * per_s);
    Msg("~ [DA_LUAMEM] ИТОГО мусора: %.1f КБ/с (выделено + рост)",
        (s.new_bytes + s.grow_bytes) / 1024.0 * per_s);
    if (frames > 0)
        Msg("~ [DA_LUAMEM] НА КАДР: %.0f блоков, %.1f КБ", double(s.new_calls) / frames,
            (s.new_bytes + s.grow_bytes) / 1024.0 / frames);

    // Десять самых частых ТОЧНЫХ размеров. Ради этой таблицы всё и написано: размер — подпись
    // структуры, и по ней виновник ищется в коде быстрее, чем любым чтением подряд.
    // ⭐ Строки отдельной строкой отчёта. Размер их не выдаёт: строка в 21 символ и замыкание с
    // одним upvalue дают ОДИН И ТОТ ЖЕ блок в 48 байт, а лечатся они совершенно по-разному.
    // Поэтому счёт берётся из lj_str_new, уже ПОСЛЕ проверки таблицы интернирования.
    {
        const unsigned long long dc = da_lj_str_new_count - g_da_str_base_count;
        const unsigned long long db = da_lj_str_new_bytes - g_da_str_base_bytes;
        Msg("~ [DA_LUAMEM] из них СТРОК: %llu (%.1f%% блоков), %.1f КБ/с", dc,
            s.new_calls ? dc * 100.0 / s.new_calls : 0.0, db / 1024.0 * per_s);
    }
    // ⭐ Разбор по типам — главная таблица отчёта. Дважды подряд размер блока назвал не того
    // виновника (сначала «замыкания», потом «строки»), потому что 48 байт дают сразу четыре разные
    // структуры. Тип известен в конструкторе и догадок не требует.
    {
        static pcstr names[DA_GC_KINDS] = { "замыкания Lua", "замыкания C (pushcclosure)", "upvalue", "таблицы (заголовок)",
            "таблицы: хеш-часть", "таблицы: массив", "userdata (в т.ч. luabind)", "строки",
            "ВСЕГО объектов сборщика", "ВСЕГО сырых векторов" };
        Msg("~ [DA_LUAMEM] --- ПО ТИПАМ ОБЪЕКТА ---");
        for (int k = 0; k < DA_GC_KINDS; ++k)
        {
            const unsigned long long dc = da_gc_count[k] - g_da_gc_base_count[k];
            const unsigned long long db = da_gc_bytes[k] - g_da_gc_base_bytes[k];
            if (!dc)
                continue;
            Msg("~ [DA_LUAMEM]   %-26s %10llu шт  %9.1f КБ/с  (%4.1f%% блоков)", names[k], dc,
                db / 1024.0 * per_s, s.new_calls ? dc * 100.0 / s.new_calls : 0.0);
        }
        // Откуда таблицы: из C++ через API или из кода Lua. Это разные задачи и разные правки.
        const unsigned long long tab_all = da_gc_count[DA_GC_TAB] - g_da_gc_base_count[DA_GC_TAB];
        const unsigned long long tab_c = da_tab_capi_get();
        if (tab_all)
            Msg("~ [DA_LUAMEM]   из таблиц: из C++ %llu (%.1f%%), из кода Lua %llu (%.1f%%)", tab_c,
                tab_c * 100.0 / tab_all, tab_all > tab_c ? tab_all - tab_c : 0,
                tab_all > tab_c ? (tab_all - tab_c) * 100.0 / tab_all : 0.0);
        if (tab_c)
        {
            struct trow { const void* ret; unsigned long long count; };
            xr_vector<trow> rows;
            const void* ret = nullptr;
            unsigned long long cnt = 0;
            for (int i = 0; da_tabc_get(i, &ret, &cnt); ++i)
                rows.push_back({ ret, cnt });
            std::sort(rows.begin(), rows.end(), [](const trow& a, const trow& b) { return a.count > b.count; });
            Msg("~ [DA_LUAMEM]   кто объявляет зависимость (add_dependency):");
            {
                const void* dret = nullptr;
                unsigned long long dcnt = 0;
                for (int i = 0; i < 6 && da_dep_get(i, &dret, &dcnt); ++i)
                {
                    pcstr mod = "?";
                    uintptr_t rva = 0;
#if defined(XR_PLATFORM_WINDOWS)
                    HMODULE hm = nullptr;
                    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)dret, &hm) && hm)
                    {
                        static string_path dpath;
                        GetModuleFileNameA(hm, dpath, sizeof(dpath));
                        pcstr sl = strrchr(dpath, 0x5C);
                        mod = sl ? sl + 1 : dpath;
                        rva = (uintptr_t)dret - (uintptr_t)hm;
                    }
#endif
                    Msg("~ [DA_LUAMEM]     %10llu  %s+0x%llX", dcnt, mod, (unsigned long long)rva);
                }
            }
            Msg("~ [DA_LUAMEM]   кто зовёт lua_createtable:");
            const size_t ttop = rows.size() < 8 ? rows.size() : 8;
            for (size_t i = 0; i < ttop; ++i)
            {
                pcstr mod = "?";
                uintptr_t rva = 0;
#if defined(XR_PLATFORM_WINDOWS)
                HMODULE hm = nullptr;
                if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)rows[i].ret, &hm) && hm)
                {
                    static string_path path;
                    GetModuleFileNameA(hm, path, sizeof(path));
                    pcstr slash = strrchr(path, 0x5C);
                    mod = slash ? slash + 1 : path;
                    rva = (uintptr_t)rows[i].ret - (uintptr_t)hm;
                }
#endif
                Msg("~ [DA_LUAMEM]     %10llu  %5.1f%%  %s+0x%llX", rows[i].count,
                    rows[i].count * 100.0 / tab_c, mod, (unsigned long long)rva);
            }
        }
    }
    Msg("~ [DA_LUAMEM] --- самые частые размеры выделений ---");
    for (int top = 0; top < 10; ++top)
    {
        size_t best = 0;
        u64 best_n = 0;
        for (size_t i = 0; i < DA_LUA_EXACT; ++i)
            if (s.exact[i] > best_n)
            {
                best_n = s.exact[i];
                best = i;
            }
        if (!best_n)
            break;
        Msg("~ [DA_LUAMEM]   %5u Б  x %-10llu = %8.1f КБ/с  (%4.1f%% блоков)%s", (u32)best,
            (unsigned long long)best_n, best_n * best / 1024.0 * per_s,
            s.new_calls ? best_n * 100.0 / s.new_calls : 0.0,
            best == 128 ? "   <- размер обёртки luabind на возвращённый указатель" : "");
        s.exact[best] = 0; // вычёркиваем найденный и ищем следующий
    }
    // ⭐ Какие КЛАССЫ оборачиваются. Первое место в мусоре после того, как убрали C-замыкания.
    const unsigned long long ud_total = da_ud_total_get();
    if (ud_total)
    {
        struct urow { pcstr name; unsigned long long count; };
        xr_vector<urow> rows;
        pcstr name = nullptr;
        unsigned long long count = 0;
        for (int i = 0; da_ud_get(i, &name, &count); ++i)
            rows.push_back({ name, count });
        std::sort(rows.begin(), rows.end(), [](const urow& a, const urow& b) { return a.count > b.count; });
        Msg("~ [DA_LUAMEM] --- КАКИЕ КЛАССЫ ОБОРАЧИВАЮТСЯ (всего %llu) ---", ud_total);
        const size_t top = rows.size() < 15 ? rows.size() : 15;
        for (size_t i = 0; i < top; ++i)
            Msg("~ [DA_LUAMEM]   %10llu  %5.1f%%  %s%s", rows[i].count,
                rows[i].count * 100.0 / ud_total, rows[i].name,
                frames > 0 ? "" : "");
        if (frames > 0)
            Msg("~ [DA_LUAMEM]   обёрток на кадр: %.0f", double(ud_total) / frames);
        {
            unsigned long long pt = 0, pd = 0, po = 0, ch = 0, cm = 0, cf = 0;
            da_ptr_stat(&pt, &pd, &po);
            da_cache_stat(&ch, &cm, &cf);
            if (ch + cm)
                Msg("~ [DA_LUAMEM]   кэш обёрток: попаданий %llu, промахов %llu (%.1f%%)%s", ch, cm,
                    (ch + cm) ? ch * 100.0 / (ch + cm) : 0.0, cf ? "  ⚠️ таблица переполнена" : "");
            if (pt)
                Msg("~ [DA_LUAMEM]   РАЗНЫХ объектов: %llu из %llu обёрток — на объект по %.1f "
                    "обёртке%s", pd, pt, pd ? double(pt) / pd : 0.0,
                    po ? "  ⚠️ таблица переполнена, счёт неполон" : "");
        }

        // И самое нужное: КТО их просит — строка скрипта.
        struct srow { pcstr src; int line; unsigned long long count; };
        xr_vector<srow> sites;
        pcstr src = nullptr;
        int line = 0;
        for (int i = 0; da_udsite_get(i, &src, &line, &count); ++i)
            sites.push_back({ src, line, count });
        std::sort(sites.begin(), sites.end(), [](const srow& a, const srow& b) { return a.count > b.count; });
        Msg("~ [DA_LUAMEM] --- ОТКУДА ПРОСЯТ ОБЁРТКИ (строка скрипта) ---");
        const size_t stop = sites.size() < 15 ? sites.size() : 15;
        for (size_t i = 0; i < stop; ++i)
        {
            pcstr name = strrchr(sites[i].src, 0x5C);
            Msg("~ [DA_LUAMEM]   %10llu  %5.1f%%  %s:%d", sites[i].count,
                sites[i].count * 100.0 / ud_total, name ? name + 1 : sites[i].src, sites[i].line);
        }
    }

    // ⭐ Кто создаёт C-замыкания — по адресу функции. Разбирается снаружи: addr2line по DLL.
    if (da_cc_total)
    {
        struct crow { void* fn; unsigned long long count; };
        xr_vector<crow> rows;
        void* fn = nullptr;
        unsigned long long count = 0;
        for (int i = 0; da_cc_get(i, &fn, &count); ++i)
            rows.push_back({ fn, count });
        std::sort(rows.begin(), rows.end(), [](const crow& a, const crow& b) { return a.count > b.count; });
        Msg("~ [DA_LUAMEM] --- КТО СОЗДАЁТ C-ЗАМЫКАНИЯ (всего %llu) ---", da_cc_total);
        const size_t top = rows.size() < 12 ? rows.size() : 12;
        for (size_t i = 0; i < top; ++i)
        {
            // Модуль и смещение — прямо в отчёте. Блок [DA_MODULES] пишется только при падении, а
            // голый адрес без базы модуля разобрать нечем: addr2line ждёт смещение внутри образа.
            pcstr mod = "?";
            uintptr_t rva = 0;
#if defined(XR_PLATFORM_WINDOWS)
            HMODULE hm = nullptr;
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    (LPCSTR)rows[i].fn, &hm) && hm)
            {
                static string_path path;
                GetModuleFileNameA(hm, path, sizeof(path));
                pcstr slash = strrchr(path, 0x5C); // 0x5C = обратная косая
                mod = slash ? slash + 1 : path;
                rva = (uintptr_t)rows[i].fn - (uintptr_t)hm;
            }
#endif
            Msg("~ [DA_LUAMEM]   %10llu  %5.1f%%  %s+0x%llX", rows[i].count,
                rows[i].count * 100.0 / da_cc_total, mod, (unsigned long long)rva);
        }
        Msg("~ [DA_LUAMEM]   различных функций: %u", (u32)rows.size());
    }

    // ⭐ Откуда берутся замыкания — по МЕСТУ В КОДЕ. Из Lua это недостижимо: классы мода сделаны
    // через luabind и являются userdata, перебрать их методы обёртками нельзя. Здесь же прототип
    // известен точно.
    if (da_fn_total)
    {
        struct row { const char* chunk; int line; unsigned long long count; };
        xr_vector<row> rows;
        const char* chunk = nullptr;
        int line = 0;
        unsigned long long count = 0;
        for (int i = 0; da_fn_get(i, &chunk, &line, &count); ++i)
            rows.push_back({ chunk, line, count });
        std::sort(rows.begin(), rows.end(), [](const row& a, const row& b) { return a.count > b.count; });
        Msg("~ [DA_LUAMEM] --- КТО СОЗДАЁТ ЗАМЫКАНИЯ (всего %llu%s) ---", da_fn_total,
            da_fn_lost ? ", часть не поместилась в таблицу" : "");
        const size_t top = rows.size() < 20 ? rows.size() : 20;
        for (size_t i = 0; i < top; ++i)
            Msg("~ [DA_LUAMEM]   %8llu  %5.1f%%  %s:%d", rows[i].count,
                rows[i].count * 100.0 / da_fn_total, rows[i].chunk, rows[i].line);
        if (frames > 0)
            Msg("~ [DA_LUAMEM]   итого замыканий на кадр: %.0f", double(da_fn_total) / frames);
    }

    u64 big_total = 0;
    for (size_t b = 0; b < DA_LUA_BIG; ++b)
        big_total += s.big[b];
    if (big_total)
        Msg("~ [DA_LUAMEM]   крупнее %u Б: %llu блоков", (u32)DA_LUA_EXACT, (unsigned long long)big_total);
}

static void* __cdecl luabind_allocator(void* context, const void* pointer, size_t const size)
{
    if (!size)
    {
        void* non_const_pointer = const_cast<LPVOID>(pointer);
        xr_free(non_const_pointer);
        return nullptr;
    }
    if (!pointer)
    {
        return xr_malloc(size);
    }
    void* non_const_pointer = const_cast<LPVOID>(pointer);
    return xr_realloc(non_const_pointer, size);
}

namespace
{
void LuaJITLogError(lua_State* ls, const char* msg)
{
    const char* info = nullptr;
    if (!lua_isnil(ls, -1))
    {
        info = lua_tostring(ls, -1);
        lua_pop(ls, 1);
    }
    Msg("! LuaJIT: %s (%s)", msg, info ? info : "no info");
}
// tries to execute 'jit'+command
bool RunJITCommand(lua_State* ls, const char* command)
{
    string128 buf;
    xr_strcpy(buf, "jit.");
    xr_strcat(buf, command);
    if (luaL_dostring(ls, buf))
    {
        LuaJITLogError(ls, "Unrecognized command");
        return false;
    }
    return true;
}
}

const char* const CScriptEngine::GlobalNamespace = SCRIPT_GLOBAL_NAMESPACE;
Lock CScriptEngine::stateMapLock;
xr_unordered_map<lua_State*, CScriptEngine*> CScriptEngine::stateMap;

string4096 CScriptEngine::g_ca_stdout;

void CScriptEngine::reinit()
{
    ZoneScoped;
    stateMapLock.Enter();
    stateMap.reserve(32); // 32 lua states should be enough
    stateMapLock.Leave();
    if (m_virtual_machine)
    {
        if (m_profiler)
            m_profiler->OnDispose(m_virtual_machine);

        // [DA_PORT] Кэш обёрток гасим ДО закрытия состояния: номера ссылок принадлежат ему, и
        // после lua_close снимать их некуда. Сравнивать сохранённый lua_State* было бы ошибкой —
        // аллокатор охотно отдаст новому состоянию тот же адрес.
        da_cache_reset(nullptr);
        lua_close(m_virtual_machine);
        UnregisterState(m_virtual_machine);
    }
    m_virtual_machine = lua_newstate(lua_alloc, nullptr);
    if (!m_virtual_machine)
    {
        Log("! ERROR : Cannot initialize script virtual machine!");
        return;
    }
    RegisterState(m_virtual_machine, this);
    // [DA_PORT] Кэш обёрток включаем на новое состояние. Разрешён ровно один класс: у game_object
    // деструктор (~CScriptGameObject) сам зовёт da_cache_forget, а без этого запись пережила бы
    // объект и отдала бы обёртку от покойника, когда адрес достанется другому.
    da_cache_reset(m_virtual_machine);
    da_cache_enable_class("game_object");
    // [DA_PORT] Счётчик мусора Lua, видимый из скриптов. Ради него профилировщик мусора получается
    // ОБЁРТКАМИ, а не хуками: обёртка строится один раз при постановке, а не на каждый вызов, и в
    // измеряемый счёт не попадает вовсе. Ровно этим замер мусора отличается от замера времени, где
    // накладные расходы обёрток сравнимы с измеряемым (скрипты занимают 0.16 мс кадра).
    //
    // Ставится здесь, а не в exporter: состояние Lua пересоздаётся на каждой загрузке, и глобальное
    // имя надо возвращать вместе с ним.
    lua_pushcfunction(m_virtual_machine, da_lua_bytes_binding);
    lua_setglobal(m_virtual_machine, "da_lua_bytes");
    if (strstr(Core.Params, "-_g"))
        file_header = file_header_new;
    else
        file_header = file_header_old;
    // [DA_PORT] Прежний буфер обязан освободиться здесь. Освобождал его только деструктор
    // (~CScriptEngine), а reinit() выделяет новый на КАЖДЫЙ перезапуск скриптового движка — то есть
    // на каждую перезагрузку сохранения и на каждую смену уровня. Мегабайт за раз уходил в никуда.
    //
    // Найдено ловушкой аллокатора на точный размер: замер показал ровно один блок 1 048 584 байт
    // (мегабайт плюс хвост аллокатора) за перезагрузку, стек назвал эту строку.
    //
    // xr_free принимает указатель по ссылке и обнуляет его, на первом вызове (scriptBuffer = nullptr)
    // он безвреден. Буфер ниже может подрасти через xr_realloc, если скрипт не влезет в мегабайт, —
    // поэтому освобождаем именно его, а не полагаемся на равенство размеров.
    xr_free(scriptBuffer);
    scriptBufferSize = 1024 * 1024;
    scriptBuffer = xr_alloc<char>(scriptBufferSize);

    if (m_profiler)
        m_profiler->OnReinit(m_virtual_machine);
}

void CScriptEngine::print_stack(lua_State* L)
{
    if (L == nullptr)
        L = lua();

    // [DA_PORT] Печать стека вызывается из обработчика ОТКАЗА, то есть в момент, когда состояние
    // программы уже недостоверно. Если виртуальной машины нет (её снесли, отказ пришёл раньше её
    // создания или из чужого потока), прежний код шёл в lua_isstring(nullptr) и падал ВТОРОЙ раз —
    // прямо внутри разбора первого отказа. В логе тестера это выглядело так: заголовок «стек
    // скрипта на момент отказа» напечатан, стека нет, дальше второе C0000005 уже в системном
    // модуле. Диагностика обязана молчать, а не добивать отчёт.
    if (!L)
    {
        Log("~ [DA_PORT] стек скрипта недоступен: виртуальная машина Lua не создана или уже снесена");
        return;
    }

    if (lua_isstring(L, -1))
    {
        pcstr err = lua_tostring(L, -1);
        script_log(LuaMessageType::Error, "%s", err);
    }

    lua_Debug l_tDebugInfo;
    for (int i = 0; lua_getstack(L, i, &l_tDebugInfo); i++)
    {
        lua_getinfo(L, "nSlu", &l_tDebugInfo);

        if (!xr_strcmp(l_tDebugInfo.what, "C"))
        {
            script_log(LuaMessageType::Error, "%2d : [C  ] %s", i, l_tDebugInfo.name ? l_tDebugInfo.name : "");
        }
        else
        {
            string_path temp;
            if (l_tDebugInfo.name)
                xr_sprintf(temp, "%s(%d)", l_tDebugInfo.name, l_tDebugInfo.linedefined);
            else
                xr_sprintf(temp, "function <%s:%d>", l_tDebugInfo.short_src, l_tDebugInfo.linedefined);

            script_log(LuaMessageType::Error, "%2d : [%3s] %s(%d) : %s", i, l_tDebugInfo.what,
                l_tDebugInfo.short_src, l_tDebugInfo.currentline, temp);
        }

        // Giperion: verbose log
        if (g_LuaDumpDepth > 0)
        {
            script_log(LuaMessageType::Error, "\t Locals: ");
            int VarID = 1;
            pcstr name;

            while ((name = lua_getlocal(L, &l_tDebugInfo, VarID++)) != nullptr)
            {
                luabind::detail::stack_pop pop{ L, 1 };
                log_value(L, name, 1);
            }
        }
        // -Giperion
    }
}

void CScriptEngine::log_value(lua_State* L, pcstr name, int depth)
{
    using namespace luabind::detail;

    string32 tab_buffer{};
    FillMemory(tab_buffer, std::min(int(sizeof(tab_buffer) - 1), depth), '\t');

    string256 value{};
    char colon{ ':' };
    bool log_table{};

    const int ntype = lua_type(L, -1);
    pcstr type = lua_typename(L, ntype);

    switch (ntype)
    {
    case LUA_TNIL:
    case LUA_TFUNCTION:
    case LUA_TTHREAD:
        colon = '\0';
        break;

    case LUA_TNUMBER:
        xr_sprintf(value, "%f", lua_tonumber(L, -1));
        break;

    case LUA_TBOOLEAN:
        xr_sprintf(value, "%s", lua_toboolean(L, -1) ? "true" : "false");
        break;

    case LUA_TSTRING:
        xr_sprintf(value, "%.255s", lua_tostring(L, -1));
        break;

    case LUA_TTABLE:
        if (depth <= g_LuaDumpDepth)
            log_table = true;
        else
            xr_sprintf(value, "[...]");
        break;

    case LUA_TUSERDATA:
        if (const auto* object = get_instance(L, -1))
        {
            if (const auto* rep = object->crep())
            {
                type = rep->name();
                if (depth <= g_LuaDumpDepth)
                {
                    lua_getfenv(L, -1);
                    log_table = true;
                }
                else
                    xr_sprintf(value, "[...]");
                break;
            }
        }
        [[fallthrough]];

    default:
        xr_strcpy(value, "[not available]");
        break;
    }

    script_log(LuaMessageType::Error, "%s %s %s %c %s", tab_buffer, type, name, colon, value);
    if (log_table)
    {
        luabind::table members(luabind::from_stack(L, -1));
        for (luabind::iterator it(members), end; it != end; ++it)
        {
            auto proxy = *it;
            proxy.push(L);
            stack_pop pop{ L, 1 };
            if (lua_iscfunction(L, -1))
                continue;
            log_value(L, lua_tostring(L, -2), depth + 1);
        }

        if (ntype == LUA_TUSERDATA)
            lua_pop(L, 1);
    }
}

bool CScriptEngine::parse_namespace(pcstr caNamespaceName, pstr b, size_t b_size, pstr c, size_t c_size)
{
    *b = 0;
    *c = 0;
    pstr S2;
    STRCONCAT(S2, caNamespaceName);
    pstr S = S2;
    for (int i = 0;; i++)
    {
        if (!xr_strlen(S))
        {
            script_log(LuaMessageType::Error, "the namespace name %s is incorrect!", caNamespaceName);
            return false;
        }
        pstr S1 = strchr(S, '.');
        if (S1)
            *S1 = 0;
        if (i)
            xr_strcat(b, b_size, "{");
        xr_strcat(b, b_size, S);
        xr_strcat(b, b_size, "=");
        if (i)
            xr_strcat(c, c_size, "}");
        if (S1)
            S = ++S1;
        else
            break;
    }
    return true;
}

bool CScriptEngine::load_buffer(
lua_State* L, LPCSTR caBuffer, size_t tSize, LPCSTR caScriptName, LPCSTR caNameSpaceName)
{
    int l_iErrorCode;
    if (caNameSpaceName && xr_strcmp(GlobalNamespace, caNameSpaceName))
    {
        string512 insert, a, b;
        LPCSTR header = file_header;
        if (!parse_namespace(caNameSpaceName, a, sizeof(a), b, sizeof(b)))
            return false;
        xr_sprintf(insert, header, caNameSpaceName, a, b);
        const size_t str_len = xr_strlen(insert);
        const size_t total_size = str_len + tSize;
        if (total_size >= scriptBufferSize)
        {
            scriptBufferSize = total_size;
            scriptBuffer = (char*)xr_realloc(scriptBuffer, scriptBufferSize);
        }
        xr_strcpy(scriptBuffer, total_size, insert);
        CopyMemory(scriptBuffer + str_len, caBuffer, tSize);
        l_iErrorCode = luaL_loadbuffer(L, scriptBuffer, tSize + str_len, caScriptName);
    }
    else
        l_iErrorCode = luaL_loadbuffer(L, caBuffer, tSize, caScriptName);
    if (l_iErrorCode)
    {
        print_output(L, caScriptName, l_iErrorCode);
        on_error(L);
        return false;
    }
    return true;
}

bool CScriptEngine::do_file(LPCSTR caScriptName, LPCSTR caNameSpaceName)
{
    int start = lua_gettop(lua());
    string_path l_caLuaFileName;
    IReader* l_tpFileReader = FS.r_open(caScriptName);
    if (!l_tpFileReader)
    {
        script_log(LuaMessageType::Error, "Cannot open file \"%s\"", caScriptName);
        return false;
    }
    strconcat(sizeof(l_caLuaFileName), l_caLuaFileName, "@", caScriptName);
    // [DA_PORT] permanent load trace: which scripts actually load and in what order (cheap -
    // fires once per script file). Flush only in trace mode to keep normal runs fast.
    Msg("* [DA_LUA] load: %s -> %s", caScriptName, caNameSpaceName ? caNameSpaceName : "");
    if (da_lua_trace())
        FlushLog();
    if (!load_buffer(lua(), static_cast<LPCSTR>(l_tpFileReader->pointer()), l_tpFileReader->length(),
        l_caLuaFileName, caNameSpaceName))
    {
        // VERIFY(lua_gettop(lua())>=4);
        // lua_pop(lua(), 4);
        // VERIFY(lua_gettop(lua())==start-3);
        lua_settop(lua(), start);
        FS.r_close(l_tpFileReader);
        return false;
    }
    FS.r_close(l_tpFileReader);
    int errFuncId = -1;
#ifdef USE_DEBUGGER
    if (debugger())
        errFuncId = debugger()->PrepareLua(lua());
#endif
    if (0) //.
    {
        for (int i = 0; lua_type(lua(), -i - 1); i++)
            Msg("%2d : %s", -i - 1, lua_typename(lua(), lua_type(lua(), -i - 1)));
    }
    // because that's the first and the only call of the main chunk - there is no point to compile it
    // luaJIT_setmode(lua(), 0, LUAJIT_MODE_ENGINE|LUAJIT_MODE_OFF); // Oles
    int l_iErrorCode = lua_pcall(lua(), 0, 0, (-1 == errFuncId) ? 0 : errFuncId); // new_Andy
// luaJIT_setmode(lua(), 0, LUAJIT_MODE_ENGINE|LUAJIT_MODE_ON); // Oles
#ifdef USE_DEBUGGER
    if (debugger())
        debugger()->UnPrepareLua(lua(), errFuncId);
#endif
    if (l_iErrorCode)
    {
        print_output(lua(), caScriptName, l_iErrorCode);
        on_error(lua());
        lua_settop(lua(), start);
        return false;
    }
    return true;
}

bool CScriptEngine::load_file_into_namespace(LPCSTR caScriptName, LPCSTR caNamespaceName)
{
    int start = lua_gettop(lua());
    if (!do_file(caScriptName, caNamespaceName))
    {
        lua_settop(lua(), start);
        return false;
    }
    VERIFY(lua_gettop(lua()) == start);
    return true;
}

bool CScriptEngine::namespace_loaded(LPCSTR name, bool remove_from_stack)
{
    [[maybe_unused]] int start = lua_gettop(lua());

    lua_pushstring(lua(), GlobalNamespace);
    lua_rawget(lua(), LUA_GLOBALSINDEX);
    string256 S2 = { 0 };
    xr_strcpy(S2, name);
    pstr S = S2;
    for (;;)
    {
        if (!xr_strlen(S))
        {
            VERIFY(lua_gettop(lua()) >= 1);
            lua_pop(lua(), 1);
            VERIFY(start == lua_gettop(lua()));
            return false;
        }
        pstr S1 = strchr(S, '.');
        if (S1)
            *S1 = 0;
        lua_pushstring(lua(), S);
        lua_rawget(lua(), -2);
        if (lua_isnil(lua(), -1))
        {
            // lua_settop(lua(), 0);
            VERIFY(lua_gettop(lua()) >= 2);
            lua_pop(lua(), 2);
            VERIFY(start == lua_gettop(lua()));
            return false; // there is no namespace!
        }
        else if (!lua_istable(lua(), -1))
        {
            // lua_settop(lua(), 0);
            VERIFY(lua_gettop(lua()) >= 1);
            lua_pop(lua(), 1);
            VERIFY(start == lua_gettop(lua()));
            FATAL(" Error : the namespace name is already being used by the non-table object!\n");
            return false;
        }
        lua_remove(lua(), -2);
        if (S1)
            S = ++S1;
        else
            break;
    }
    if (!remove_from_stack)
        VERIFY(lua_gettop(lua()) == start + 1);
    else
    {
        VERIFY(lua_gettop(lua()) >= 1);
        lua_pop(lua(), 1);
        VERIFY(lua_gettop(lua()) == start);
    }
    return true;
}

bool CScriptEngine::object(LPCSTR identifier, int type)
{
    [[maybe_unused]] int start = lua_gettop(lua());

    lua_pushnil(lua());
    while (lua_next(lua(), -2))
    {
        if (lua_type(lua(), -1) == type && !xr_strcmp(identifier, lua_tostring(lua(), -2)))
        {
            VERIFY(lua_gettop(lua()) >= 3);
            lua_pop(lua(), 3);
            VERIFY(lua_gettop(lua()) == start - 1);
            return true;
        }
        lua_pop(lua(), 1);
    }
    VERIFY(lua_gettop(lua()) >= 1);
    lua_pop(lua(), 1);
    VERIFY(lua_gettop(lua()) == start - 1);
    return false;
}

bool CScriptEngine::object(LPCSTR namespace_name, LPCSTR identifier, int type)
{
    [[maybe_unused]] int start = lua_gettop(lua());

    if (xr_strlen(namespace_name) && !namespace_loaded(namespace_name, false))
    {
        VERIFY(lua_gettop(lua()) == start);
        return false;
    }
    bool result = object(identifier, type);
    VERIFY(lua_gettop(lua()) == start);
    return result;
}

luabind::object CScriptEngine::name_space(LPCSTR namespace_name)
{
    string256 S1 = { 0 };
    xr_strcpy(S1, namespace_name);
    pstr S = S1;
    luabind::object lua_namespace = luabind::globals(lua());
    for (;;)
    {
        if (!xr_strlen(S))
            return lua_namespace;
        pstr I = strchr(S, '.');
        if (!I)
            return lua_namespace[(const char*)S];
        *I = 0;
        lua_namespace = lua_namespace[(const char*)S];
        S = I + 1;
    }
}

bool CScriptEngine::print_output(lua_State* L, pcstr caScriptFileName, int errorCode, pcstr caErrorText)
{
    CScriptEngine* scriptEngine = GetInstance(L);
    VERIFY(scriptEngine);

    if (caErrorText)
    {
        const auto [logHeader, luaLogHeader] = get_message_headers(LuaMessageType::Error);
        Msg("%sSCRIPT ERROR: %s\n", logHeader, caErrorText);
    }

    if (errorCode)
        print_error(L, errorCode);

    if (!lua_isstring(L, -1))
        return false;

    const auto S = lua_tostring(L, -1);

    if (!xr_strcmp(S, "cannot resume dead coroutine"))
    {
        VERIFY2("Please do not return any values from main!!!", caScriptFileName);
#if defined(USE_DEBUGGER)
        if (scriptEngine->debugger() && scriptEngine->debugger()->Active())
        {
            scriptEngine->debugger()->Write(S);
            scriptEngine->debugger()->ErrorBreak();
        }
#endif
    }
    else
    {
        if (!errorCode)
            scriptEngine->script_log(LuaMessageType::Info, "Output from %s", caScriptFileName);
#if defined(USE_DEBUGGER)
        if (scriptEngine->debugger() && scriptEngine->debugger()->Active())
        {
            scriptEngine->debugger()->Write(S);
            scriptEngine->debugger()->ErrorBreak();
        }
#endif
    }

    return true;
}

void CScriptEngine::print_error(lua_State* L, int iErrorCode)
{
    CScriptEngine* scriptEngine = GetInstance(L);
    VERIFY(scriptEngine);

    switch (iErrorCode)
    {
    case LUA_ERRRUN:
        scriptEngine->script_log(LuaMessageType::Error, "SCRIPT RUNTIME ERROR");
        break;
    case LUA_ERRMEM:
        scriptEngine->script_log(LuaMessageType::Error, "SCRIPT ERROR (memory allocation)");
        break;
    case LUA_ERRERR:
        scriptEngine->script_log(LuaMessageType::Error, "SCRIPT ERROR (while running the error handler function)");
        break;
    case LUA_ERRFILE:
        scriptEngine->script_log(LuaMessageType::Error, "SCRIPT ERROR (while running file)");
        break;
    case LUA_ERRSYNTAX:
        scriptEngine->script_log(LuaMessageType::Error, "SCRIPT SYNTAX ERROR");
        break;
    case LUA_YIELD:
        scriptEngine->script_log(LuaMessageType::Info, "Thread is yielded");
        break;
    default: NODEFAULT;
    }
}

void CScriptEngine::flush_log()
{
    string_path log_file_name;
    strconcat(sizeof(log_file_name), log_file_name, Core.ApplicationName, "_", Core.UserName, "_lua.log");
    FS.update_path(log_file_name, "$logs$", log_file_name);
    m_output.save_to(log_file_name);
}

CScriptEngine::CScriptEngine(bool is_editor, bool is_with_profiler)
{
    luabind::allocator = &luabind_allocator;
    luabind::allocator_context = nullptr;
    m_current_thread = nullptr;
    m_virtual_machine = nullptr;
    m_profiler = is_with_profiler && !is_editor ? xr_new<CScriptProfiler>(this) : nullptr;
    m_stack_level = 0;
    m_reload_modules = false;
    m_last_no_file_length = 0;
    *m_last_no_file = 0;
#ifdef USE_DEBUGGER
#ifndef USE_LUA_STUDIO
    static_assert(false, "Do not define USE_LUA_STUDIO macro without USE_DEBUGGER macro");
    m_scriptDebugger = nullptr;
    restartDebugger();
#endif
#endif
    m_is_editor = is_editor;
}

CScriptEngine::~CScriptEngine()
{
    if (m_profiler)
    {
        if (m_virtual_machine)
            m_profiler->OnDispose(m_virtual_machine);

        xr_delete(m_profiler);
    }

    if (m_virtual_machine)
    {
        da_cache_reset(nullptr); // [DA_PORT] см. reinit: гасим кэш до закрытия состояния
        lua_close(m_virtual_machine);
    }
    while (!m_script_processes.empty())
        remove_script_process(m_script_processes.begin()->first);
#ifdef DEBUG
    flush_log();
#endif
#ifdef USE_DEBUGGER
    xr_delete(m_scriptDebugger);
#endif
    if (scriptBuffer)
        xr_free(scriptBuffer);
}

void CScriptEngine::unload()
{
    lua_settop(lua(), m_stack_level);
    m_last_no_file_length = 0;
    *m_last_no_file = 0;
}

int CScriptEngine::lua_panic(lua_State* L)
{
    print_output(L, "", LUA_ERRRUN, "PANIC");
    FATAL("Lua panic");
    return 0;
}

void CScriptEngine::lua_error(lua_State* L)
{
    print_output(L, "", LUA_ERRRUN);
    on_error(L);

// [DA_PORT] Условие обязано следовать за флагом LUABIND, а не за флагом движка.
//
// В отгружаемой сборке стоят ОБА флага сразу: LUABIND_NO_EXCEPTIONS (luabind собран без исключений)
// и XRAY_EXCEPTIONS=1. Прежнее `#if !XRAY_EXCEPTIONS` из-за второго флага выбирало ветку `throw`,
// а ловить этот бросок некому: `catch` в диспетчере luabind компилируется только под
// `#ifndef LUABIND_NO_EXCEPTIONS` (make_function.hpp). Бросок ушёл бы в зону без обработчика.
#if !XRAY_EXCEPTIONS || defined(LUABIND_NO_EXCEPTIONS)
    xrDebug::Fatal(DEBUG_INFO, "LUA error: %s", lua_tostring(L, -1));
#else
    throw lua_tostring(L, -1);
#endif
}

// [DA_PORT] "-da_lua_trace" launch flag: whole-mod engine->script dispatch tracing
bool CScriptEngine::da_lua_trace()
{
    static const bool enabled = !!strstr(Core.Params, "-da_lua_trace");
    return enabled;
}

int CScriptEngine::lua_pcall_failed(lua_State* L)
{
    print_output(L, "", LUA_ERRRUN);
    on_error(L);

    // [DA_PORT] this pcall error handler runs BEFORE the stack unwinds - the only moment the
    // full Lua traceback of a runtime error is still available. Always print + flush it, so
    // every script error in any Dead Air script lands in the log even if the game dies next.
    if (CScriptEngine* se = GetInstance(L))
        se->print_stack(L);
    FlushLog();

    luabind::detail::stack_pop pop{ L, lua_isstring(L, -1) ? 1 : 0 };

    if (xrDebug::WouldShowErrorMessage())
    {
        const auto err = lua_tostring(L, -1);

        static bool ignoreAlways;
        const auto result = xrDebug::Fail(ignoreAlways, DEBUG_INFO, "LUA error", err);

        if (result == AssertionResult::tryAgain || result == AssertionResult::ignore)
            return LUA_OK;
    }

    return LUA_ERRRUN;
}

#if !XRAY_EXCEPTIONS || defined(LUABIND_NO_EXCEPTIONS)
void CScriptEngine::lua_cast_failed(lua_State* L, const luabind::type_id& info)
{
    string128 buf;
    xr_sprintf(buf, "cannot cast lua value to %s", info.name());
    print_output(L, "", LUA_ERRRUN, buf);
    xrDebug::Fatal(DEBUG_INFO, "LUA error: cannot cast lua value to %s", info.name());
}
#endif

void CScriptEngine::setup_callbacks()
{
#ifdef USE_DEBUGGER
    if (debugger())
        debugger()->PrepareLuaBind();

    if (!debugger() || !debugger()->Active())
#endif
    {
        // [DA_PORT] Оба обработчика ОБЯЗАНЫ быть выставлены, раз luabind собран без исключений.
        //
        // Прежнее условие `#if !XRAY_EXCEPTIONS` в нашей сборке ложно (XRAY_EXCEPTIONS=1), поэтому
        // не выставлялся ни один. Для ошибки ВЫЗОВА это скрадывал обработчик pcall ниже — он
        // регистрируется безусловно и печатает стек первым. А вот ошибка ПРИВЕДЕНИЯ ТИПА идёт мимо
        // pcall: luabind зовёт cast_error (detail/call_shared.hpp), тот не находит обработчика и
        // делает std::terminate() — в релизе assert вырезан, поэтому процесс исчезал БЕЗ единого
        // сообщения и без стека.
        //
        // Воспроизведено на стенде: колбэк ai_stalker.update_best_weapon объявлен движком как
        // luabind::functor<CScriptGameObject*>; стоило скрипту вернуть оттуда строку — игра
        // пропадала, в логе ноль записей об ошибке. Это и есть класс «лог обрывается на пустом
        // месте», приходивший от тестеров.
#if !XRAY_EXCEPTIONS || defined(LUABIND_NO_EXCEPTIONS)
        luabind::set_error_callback(CScriptEngine::lua_error);
#endif

        // [DA_PORT] Обработчик ошибок для защищённого вызова — из реестра, а не заново каждый раз.
        //
        // ЧТО БЫЛО. Здесь стоял голый `lua_pushcfunction`, а luabind зовёт этот колбэк ПЕРЕД КАЖДЫМ
        // защищённым вызовом из Lua в движок (`Externals/luabind/src/pcall.cpp:34-46`). Каждый
        // `lua_pushcfunction` создаёт новый GCfuncC — 48 байт, которые тут же становятся мусором.
        //
        // ЦЕНА. Замер: 432 015 таких замыканий за 600 кадров, то есть **720 на кадр** — 99.4% всех
        // C-замыканий и **52% всего мусора Lua** (935 КБ/сек из 2553). Найдено не чтением кода:
        // гистограмма по типам объектов назвала GCfuncC, а гистограмма по указателю функции —
        // конкретно эту. Две догадки «по размеру блока» до этого промахнулись.
        //
        // ЧТО СТАЛО. Замыкание строится один раз на состояние Lua и лежит в реестре под ключом-
        // адресом статической переменной (числовой ключ брать нельзя: реестр раздаёт их luaL_ref).
        // Горячий путь — один `lua_rawget`, ноль выделений.
        //
        // ⚠️ Контракт luabind: колбэк обязан оставить на стеке РОВНО ОДНО значение, дальше его
        // `lua_insert` уводит под аргументы. Обе ветки ниже это соблюдают.
        //
        // Пересоздания состояния учитывать не нужно: `reinit()` делает новый lua_State с чистым
        // реестром, и кэш восстановится сам на первом же вызове.
        luabind::set_pcall_callback([](lua_State* L)
        {
            static const char da_pcall_key = 0; // ключом служит АДРЕС, значение неважно
            lua_pushlightuserdata(L, (void*)&da_pcall_key);
            lua_rawget(L, LUA_REGISTRYINDEX);
            if (lua_isfunction(L, -1))
                return; // горячий путь
            lua_pop(L, 1);
            lua_pushcfunction(L, CScriptEngine::lua_pcall_failed);
            lua_pushlightuserdata(L, (void*)&da_pcall_key);
            lua_pushvalue(L, -2);
            lua_rawset(L, LUA_REGISTRYINDEX); // снимает ключ и копию, обработчик остаётся на стеке
        });
    }
#if !XRAY_EXCEPTIONS || defined(LUABIND_NO_EXCEPTIONS)
    luabind::set_cast_failed_callback(CScriptEngine::lua_cast_failed);
#endif
    lua_atpanic(lua(), CScriptEngine::lua_panic);
}

void CScriptEngine::lua_hook_call(lua_State* L, lua_Debug* dbg)
{
    CScriptEngine* scriptEngine = GetInstance(L);
    VERIFY(scriptEngine);

#ifdef DEBUG
    if (scriptEngine->current_thread())
        scriptEngine->current_thread()->script_hook(L, dbg);
#endif

    if (scriptEngine->m_profiler)
        scriptEngine->m_profiler->OnLuaHookCall(L, dbg);
}

int CScriptEngine::auto_load(lua_State* L)
{
    if (lua_gettop(L) < 2 || !lua_istable(L, 1) || !lua_isstring(L, 2))
    {
        lua_pushnil(L);
        return 1;
    }
    CScriptEngine* scriptEngine = GetInstance(L);
    VERIFY(scriptEngine);
    scriptEngine->process_file_if_exists(lua_tostring(L, 2), false);
    lua_rawget(L, 1);
    return 1;
}

void CScriptEngine::setup_auto_load()
{
    luaL_newmetatable(lua(), "XRAY_AutoLoadMetaTable");
    lua_pushstring(lua(), "__index");
    lua_pushcfunction(lua(), CScriptEngine::auto_load);
    lua_settable(lua(), -3);
    lua_pushstring(lua(), GlobalNamespace);
    lua_gettable(lua(), LUA_GLOBALSINDEX);
    luaL_getmetatable(lua(), "XRAY_AutoLoadMetaTable");
    lua_setmetatable(lua(), -2);
    //. ??????????
    // lua_settop(lua(), 0);
}

// initialize lua standard library functions
struct luajit
{
    static void open_lib(lua_State* L, pcstr module_name, lua_CFunction function)
    {
        lua_pushcfunction(L, function);
        lua_pushstring(L, module_name);
        lua_call(L, 1, 0);
    }

    static void allow_escape_sequences(bool allowed)
    {
        lj_allow_escape_sequences(allowed ? 1 : 0);
    }
};

void CScriptEngine::init(export_func exporter, bool loadGlobalNamespace)
{
    // [DA_PORT] Оставляем обработчику отказов способ напечатать стек Lua. Машинный стек при переходе
    // по нулевому адресу пуст, а стек интерпретатора цел — и только он назовёт строку скрипта.
    g_da_lua_stack_printer = []()
    {
        if (GEnv.ScriptEngine)
            GEnv.ScriptEngine->print_stack();
    };

    // [DA_PORT] Полная сборка мусора под экраном загрузки — см. вызов в device.cpp.
    //
    // После загрузки уровня в интерпретаторе остаётся много мусора: разобранные конфиги, временные
    // таблицы схем логики, всё созданное при спавне. Раньше это разгребалось шагами по кадру и было
    // незаметно, пока сборка крутилась на рабочем потоке. После перевода её в главный (гонка с
    // lua_State, см. mtLUA_GC) каждый шаг стал стоить 9-11 мс при бюджете кадра 16.6 — ловушка
    // da_seq_trap ловила их пачками сразу после загрузки.
    g_da_lua_full_gc = []() -> int
    {
        if (!GEnv.ScriptEngine || !GEnv.ScriptEngine->lua())
            return -1;

        lua_State* L = GEnv.ScriptEngine->lua();
        const int before = lua_gc(L, LUA_GCCOUNT, 0);
        lua_gc(L, LUA_GCCOLLECT, 0);
        const int after = lua_gc(L, LUA_GCCOUNT, 0);
        return before - after;
    };

    ZoneScoped;

    Msg("* [DA_PORT] ScriptEngine::init: before reinit"); FlushLog();
    reinit();
    Msg("* [DA_PORT] ScriptEngine::init: after reinit, before luabind::open"); FlushLog();
    luabind::open(lua());
    Msg("* [DA_PORT] ScriptEngine::init: after luabind::open"); FlushLog();

    // Workarounds to preserve backwards compatibility with game scripts
    {
        const bool nilConversion =
            pSettingsOpenXRay->read_if_exists<bool>("lua_scripting", "allow_nil_conversion", true);

        luabind::allow_nil_conversion(nilConversion);
        luabind::disable_super_deprecation();

        const bool escapeSequences =
            pSettingsOpenXRay->read_if_exists<bool>("lua_scripting", "allow_escape_sequences", false);
        luajit::allow_escape_sequences(escapeSequences);
    }

    luabind::bind_class_info(lua());
    Msg("* [DA_PORT] ScriptEngine::init: before setup_callbacks"); FlushLog();
    setup_callbacks();
    Msg("* [DA_PORT] ScriptEngine::init: after setup_callbacks, before exporter"); FlushLog();
    if (exporter)
        exporter(lua());
    Msg("* [DA_PORT] ScriptEngine::init: after exporter"); FlushLog();
    if (std::strstr(Core.Params, "-dump_bindings") && !bindingsDumped)
    {
        bindingsDumped = true;
        static int dumpId = 1;
        string_path filePath;
        xr_sprintf(filePath, "ScriptBindings_%d.txt", dumpId++);
        FS.update_path(filePath, "$app_data_root$", filePath);
        IWriter* writer = FS.w_open(filePath);
        BindingsDumper dumper;
        BindingsDumper::Options options = {};
        options.ShiftWidth = 4;
        options.IgnoreDerived = true;
        options.StripThis = true;
        dumper.Dump(lua(), writer, options);
        FS.w_close(writer);
    }

    Msg("* [DA_PORT] ScriptEngine::init: before open_lib base"); FlushLog();
    luajit::open_lib(lua(), "", luaopen_base);
    luajit::open_lib(lua(), LUA_LOADLIBNAME, luaopen_package);
    luajit::open_lib(lua(), LUA_TABLIBNAME, luaopen_table);
    luajit::open_lib(lua(), LUA_IOLIBNAME, luaopen_io);
    luajit::open_lib(lua(), LUA_OSLIBNAME, luaopen_os);
    luajit::open_lib(lua(), LUA_MATHLIBNAME, luaopen_math);
    luajit::open_lib(lua(), LUA_STRLIBNAME, luaopen_string);
    luajit::open_lib(lua(), LUA_BITLIBNAME, luaopen_bit);
    luajit::open_lib(lua(), LUA_FFILIBNAME, luaopen_ffi);
#ifndef MASTER_GOLD
    luajit::open_lib(lua(), LUA_DBLIBNAME, luaopen_debug);
#endif

    luaopen_xrluafix(lua());

    tracy::LuaRegister(lua());
    Msg("* [DA_PORT] ScriptEngine::init: after open_lib, before randomize"); FlushLog();

    // [DA_PORT] Dead Air compat: register globals that DA scripts expect early
    lua_register(lua(), "is_enough_address_space_available", [](lua_State* L) -> int {
        lua_pushboolean(L, 1);
        return 1;
    });

    // Game scripts doesn't call randomize but use random
    // So, we should randomize in the engine.
    {
        pcstr randomSeed = "math.randomseed(os.time())";
        pcstr mathRandom = "math.random()";

        luaL_dostring(lua(), randomSeed);
        // It's a good practice to call random few times before using it
        for (int i = 0; i < 3; ++i)
            luaL_dostring(lua(), mathRandom);
    }

    // Adds gamedata folder as module root for lua `require` and allows usage of built-in lua module system.
    // Notes:
    // - Does not resolve files inside archived game files
    // Example:
    // `local example = require("scripts.folder.file")` tries to import `gamedata\scripts\folder\file.script`
    {
        string_path gamedataPath;
        string_path packagePath;

        FS.update_path(gamedataPath, "$game_data$", "?.script;");
        xr_sprintf(packagePath, "package.path = package.path .. [[%s]]", gamedataPath);

        luaL_dostring(lua(), packagePath);
     }

    // XXX nitrocaster: with vanilla scripts, '-nojit' option requires script profiler to be disabled. The reason
    // is that lua hooks somehow make 'super' global unavailable (is's used all over the vanilla scripts).
    // You can disable script profiler by commenting out the following lines in the beginning of _g.script:
    // if (jit == nil) then
    //     profiler.setup_hook()
    // end
    //
    // Update: '-nojit' option adds garbage to stack and luabind calls fail
    if (!strstr(Core.Params, ARGUMENT_ENGINE_NOJIT))
    {
        luajit::open_lib(lua(), LUA_JITLIBNAME, luaopen_jit);
        // Xottab_DUTY: commented this. Let's use default opt level, which is 3
        //RunJITCommand(lua(), "opt.start(2)");
    }
    setup_auto_load();

#if defined(DEBUG) && !defined(USE_LUA_STUDIO)
#if defined(USE_DEBUGGER)
    if (!debugger() || !debugger()->Active())
#endif
        lua_sethook(lua(), CScriptEngine::lua_hook_call, LUA_MASKLINE | LUA_MASKCALL | LUA_MASKRET, 0);
#endif
    if (loadGlobalNamespace)
    {
        bool save = m_reload_modules;
        m_reload_modules = true;
        Msg("* [DA_PORT] ScriptEngine::init: before process_file_if_exists(_G)"); FlushLog();
        process_file_if_exists(GlobalNamespace, false);
        Msg("* [DA_PORT] ScriptEngine::init: after process_file_if_exists(_G)"); FlushLog();
        m_reload_modules = save;
    }
    m_stack_level = lua_gettop(lua());
    setvbuf(stderr, g_ca_stdout, _IOFBF, sizeof(g_ca_stdout));
    Msg("* [DA_PORT] ScriptEngine::init: DONE"); FlushLog();
}

void CScriptEngine::remove_script_process(const ScriptProcessor& process_id)
{
    CScriptProcessStorage::iterator I = m_script_processes.find(process_id);
    if (I != m_script_processes.end())
    {
        xr_delete((*I).second);
        m_script_processes.erase(I);
    }
}

bool CScriptEngine::load_file(const char* scriptName, const char* namespaceName)
{
    if (!process_file(scriptName))
        return false;
    string1024 initializerName;
    xr_strcpy(initializerName, scriptName);
    xr_strcat(initializerName, "_initialize");
    if (object(namespaceName, initializerName, LUA_TFUNCTION))
    {
        // lua_dostring(lua(), xr_strcat(initializerName, "()"));
        luabind::functor<void> f;
        R_ASSERT(functor(initializerName, f));
        f();
    }
    return true;
}

bool CScriptEngine::process_file_if_exists(LPCSTR file_name, bool warn_if_not_exist)
{
    const size_t string_length = xr_strlen(file_name);
    if (!warn_if_not_exist && no_file_exists(file_name, string_length))
        return false;
    string_path S, S1;
    if (m_reload_modules || (*file_name && !namespace_loaded(file_name)))
    {
        FS.update_path(S, "$game_scripts$", strconcat(sizeof(S1), S1, file_name, ".script"));
        if (!warn_if_not_exist && !FS.exist(S))
        {
#ifdef DEBUG
            if (false) // XXX: restore (check script engine flags)
            {
                print_stack();
                Msg("! WARNING: Access to nonexistent variable '%s' or loading nonexistent script '%s'", file_name, S1);
            }
#endif
            add_no_file(file_name, string_length);
            return false;
        }
#ifndef MASTER_GOLD
        Msg("* Loading script: %s", S1);
#endif
        m_reload_modules = false;
        return load_file_into_namespace(S, *file_name ? file_name : GlobalNamespace);
    }
    return true;
}

bool CScriptEngine::process_file(LPCSTR file_name) { return process_file_if_exists(file_name, true); }
bool CScriptEngine::process_file(LPCSTR file_name, bool reload_modules)
{
    m_reload_modules = reload_modules;
    bool result = process_file_if_exists(file_name, true);
    m_reload_modules = false;
    return result;
}

bool CScriptEngine::function_object(LPCSTR function_to_call, luabind::object& object, int type)
{
    if (!xr_strlen(function_to_call))
        return false;
    string256 name_space = { 0 }, function = { 0 };
    parse_script_namespace(function_to_call, name_space, sizeof(name_space), function, sizeof(function));
    if (xr_strcmp(name_space, GlobalNamespace))
    {
        pstr file_name = strchr(name_space, '.');
        if (!file_name)
            process_file_if_exists(name_space, false);
        else
        {
            *file_name = 0;
            process_file_if_exists(name_space, false);
            *file_name = '.';
        }
    }
    if (!this->object(name_space, function, type))
        return false;
    luabind::object lua_namespace = this->name_space(name_space);
    object = lua_namespace[function];
    return true;
}

void CScriptEngine::add_script_process(const ScriptProcessor& process_id, CScriptProcess* script_process)
{
    VERIFY(m_script_processes.find(process_id) == m_script_processes.end());
    m_script_processes.emplace(process_id, script_process);
}

CScriptProcess* CScriptEngine::script_process(const ScriptProcessor& process_id) const
{
    auto it = m_script_processes.find(process_id);
    if (it != m_script_processes.end())
        return it->second;
    return nullptr;
}

void CScriptEngine::parse_script_namespace(pcstr name, pstr ns, size_t nsSize, pstr func, size_t funcSize)
{
    const char* p = strrchr(name, '.');
    if (!p)
    {
        xr_strcpy(ns, nsSize, GlobalNamespace);
        p = name - 1;
    }
    else
    {
        VERIFY(size_t(p - name + 1) <= nsSize);
        strncpy(ns, name, p - name);
        ns[p - name] = 0;
    }
    xr_strcpy(func, funcSize, p + 1);
}

#if defined(USE_DEBUGGER)
void CScriptEngine::stopDebugger()
{
    if (debugger())
    {
        xr_delete(m_scriptDebugger);
        Msg("Script debugger stopped.");
    }
    else
        Msg("Script debugger not present.");
}

void CScriptEngine::restartDebugger()
{
    if (debugger())
        stopDebugger();
    m_scriptDebugger = xr_new<CScriptDebugger>(this);
    debugger()->PrepareLuaBind();
    Msg("Script debugger restarted.");
}
#endif

CScriptEngine* CScriptEngine::GetInstance(lua_State* state)
{
    CScriptEngine* instance = nullptr;
    stateMapLock.Enter();
    auto it = stateMap.find(state);
    if (it != stateMap.end())
        instance = it->second;
    stateMapLock.Leave();
    return instance;
}

bool CScriptEngine::RegisterState(lua_State* state, CScriptEngine* scriptEngine)
{
    bool result = false;
    stateMapLock.Enter();
    auto it = stateMap.find(state);
    if (it == stateMap.end())
    {
        stateMap.insert({state, scriptEngine});
        result = true;
    }
    stateMapLock.Leave();
    return result;
}

bool CScriptEngine::UnregisterState(lua_State* state)
{
    if (!state)
        return true;
    bool result = false;
    stateMapLock.Enter();
    auto it = stateMap.find(state);
    if (it != stateMap.end())
    {
        stateMap.erase(it);
        result = true;
    }
    stateMapLock.Leave();
    return result;
}

bool CScriptEngine::no_file_exists(pcstr file_name, size_t string_length)
{
    if (m_last_no_file_length != string_length)
        return false;
    return !memcmp(m_last_no_file, file_name, string_length);
}

void CScriptEngine::add_no_file(pcstr file_name, size_t string_length)
{
    m_last_no_file_length = string_length;
    CopyMemory(m_last_no_file, file_name, string_length + 1);
}

void CScriptEngine::collect_all_garbage()
{
    lua_gc(lua(), LUA_GCCOLLECT, 0);
    lua_gc(lua(), LUA_GCCOLLECT, 0);
}

void CScriptEngine::on_error(lua_State* state)
{
    [[maybe_unused]] CScriptEngine* scriptEngine = GetInstance(state);
    VERIFY(scriptEngine);
}

CScriptProcess* CScriptEngine::CreateScriptProcess(shared_str name, shared_str scripts)
{
    return xr_new<CScriptProcess>(this, name, scripts);
}

CScriptThread* CScriptEngine::CreateScriptThread(LPCSTR caNamespaceName, bool do_string, bool reload)
{
    auto thread = xr_new<CScriptThread>(this, caNamespaceName, do_string, reload);
    lua_State* threadLua = thread->lua();
    if (threadLua)
        RegisterState(threadLua, this);
    else
        xr_delete(thread);
    return thread;
}

void CScriptEngine::DestroyScriptThread(const CScriptThread* thread)
{
#ifdef DEBUG
    Msg("* Destroying script thread %s", thread->script_name().c_str());
#endif
    try
    {
#ifndef LUABIND_HAS_BUGS_WITH_LUA_THREADS
        luaL_unref(lua(), LUA_REGISTRYINDEX, thread->thread_reference());
#endif
    }
    catch (...)
    {
    }
    UnregisterState(thread->lua());
}

bool CScriptEngine::is_editor()
{
    return m_is_editor;
}
