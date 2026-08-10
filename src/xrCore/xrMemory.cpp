#include "stdafx.h"
#include "Debug/StackTrace.h" // [DA_PORT] ловушка на выделение памяти

#include <SDL.h>

#if defined(XR_PLATFORM_WINDOWS)
#include <Psapi.h>
#elif defined(XR_PLATFORM_LINUX)
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <sys/resource.h>
#elif defined(XR_PLATFORM_BSD)
#include <sys/time.h>
#include <sys/resource.h>
#elif defined(XR_PLATFORM_HAIKU)
#include <OS.h>
#include <sys/time.h>
#include <sys/resource.h>
#endif

// On other platforms these options are controlled by CMake
#if defined(XR_PLATFORM_WINDOWS) && !defined(USE_PURE_ALLOC) && !defined(USE_MIMALLOC)
#   ifdef _DEBUG
#       define USE_PURE_ALLOC
#   else
#       define USE_MIMALLOC
#   endif
#endif

#if defined(USE_MIMALLOC)
    #include "mimalloc.h"

    static_assert(xrMemory::SMALL_SIZE_MAX <= MI_SMALL_SIZE_MAX, "Please, adjust SMALL_SIZE_ALLOC_MAX");

    #define xr_internal_malloc(size) mi_malloc(size)
    #define xr_internal_malloc_aligned(size, alignment) mi_malloc_aligned(size, alignment)
    #define xr_internal_malloc_nothrow(size) mi_malloc(size)
    #define xr_internal_malloc_nothrow_aligned(size, alignment) mi_malloc_aligned(size, alignment)
    #define xr_internal_small_alloc(size) mi_malloc_small(size)
    #define xr_internal_small_free(ptr) mi_free(ptr)

    #define xr_internal_realloc(ptr, size) mi_realloc(ptr, size)
    #define xr_internal_realloc_aligned(ptr, size, alignment) mi_realloc_aligned(ptr, size, alignment)

    #define xr_internal_free(ptr) mi_free(ptr)
    #define xr_internal_free_size(ptr, size) mi_free_size(ptr, size)
    #define xr_internal_free_aligned(ptr, alignment) mi_free_aligned(ptr, alignment)
    #define xr_internal_free_size_aligned(ptr, size, alignment) mi_free_size_aligned(ptr, size, alignment)
#elif defined(USE_XR_ALIGNED_MALLOC)
    #include "Memory/xrMemory_align.h"

    #define xr_internal_malloc(size) malloc(size)
    #define xr_internal_malloc_aligned(size, alignment) xr_aligned_malloc(size, alignment)
    #define xr_internal_malloc_nothrow(size) xr_malloc(size)
    #define xr_internal_malloc_nothrow_aligned(size, alignment) mi_malloc_aligned(size, alignment)
    #define xr_internal_small_alloc(size) xr_aligned_malloc(size)
    #define xr_internal_small_free(ptr) xr_aligned_free(ptr)

    #define xr_internal_realloc(ptr, size) xr_aligned_realloc(ptr, size)
    #define xr_internal_realloc_aligned(ptr, size, alignment) xr_aligned_realloc(ptr, size, alignment)

    #define xr_internal_free(ptr) xr_aligned_free(ptr)
    #define xr_internal_free_size(ptr, size) xr_aligned_free(ptr)
    #define xr_internal_free_aligned(ptr, alignment) xr_aligned_free(ptr)
    #define xr_internal_free_size_aligned(ptr, size, alignment) xr_aligned_free(ptr)
#elif defined(USE_PURE_ALLOC)
    // Additional bytes of memory to hide memory problems on Release
    // But for Debug we don't need this if we want to find these problems
    #ifdef NDEBUG
        constexpr size_t xr_reserved_tail = 8;
    #else
        constexpr size_t xr_reserved_tail = 0;
    #endif

    #define xr_internal_malloc(size) malloc(size + xr_reserved_tail)
    #define xr_internal_malloc_aligned(size, alignment) malloc(size + xr_reserved_tail)
    #define xr_internal_malloc_nothrow(size) malloc(size + xr_reserved_tail)
    #define xr_internal_malloc_nothrow_aligned(size, alignment) malloc(size + xr_reserved_tail)
    #define xr_internal_small_alloc(size) malloc(size + xr_reserved_tail)
    #define xr_internal_small_free(ptr) free(ptr)

    #define xr_internal_realloc(ptr, size) realloc(ptr, size + xr_reserved_tail)
    #define xr_internal_realloc_aligned(ptr, size, alignment) realloc(ptr, size + xr_reserved_tail)

    #define xr_internal_free(ptr) free(ptr)
    #define xr_internal_free_size(ptr, size) free(ptr)
    #define xr_internal_free_aligned(ptr, alignment) free(ptr)
    #define xr_internal_free_size_aligned(ptr, size, alignment) free(ptr)
#else
    #error Please, define explicitly which allocator you want to use
#endif

xrMemory Memory;
// Also used in src\xrCore\xrDebug.cpp to prevent use of g_pStringContainer before it initialized
bool shared_str_initialized = false;

void xrMemory::_initialize()
{
    ZoneScoped;
    g_pStringContainer = xr_new<str_container>();
    shared_str_initialized = true;
    g_pSharedMemoryContainer = xr_new<smem_container>();
}

void xrMemory::_destroy()
{
    ZoneScoped;
    xr_delete(g_pSharedMemoryContainer);
    xr_delete(g_pStringContainer);
}

XRCORE_API void vminfo(size_t* _free, size_t* reserved, size_t* committed)
{
#if defined(XR_PLATFORM_WINDOWS)
    MEMORY_BASIC_INFORMATION memory_info;
    memory_info.BaseAddress = nullptr;
    *_free = *reserved = *committed = 0;
    while (VirtualQuery(memory_info.BaseAddress, &memory_info, sizeof(memory_info))) //-V575
    {
        switch (memory_info.State)
        {
        case MEM_FREE: *_free += memory_info.RegionSize; break;
        case MEM_RESERVE: *reserved += memory_info.RegionSize; break;
        case MEM_COMMIT: *committed += memory_info.RegionSize; break;
        }
        memory_info.BaseAddress = (char*)memory_info.BaseAddress + memory_info.RegionSize;
    }
#elif defined(XR_PLATFORM_LINUX)
    struct sysinfo si;
    sysinfo(&si);
    *_free = si.freeram * si.mem_unit;
    *reserved = si.bufferram * si.mem_unit;
    *committed = (si.totalram - si.freeram + si.totalswap - si.freeswap) * si.mem_unit;
#elif defined(XR_PLATFORM_HAIKU)
    *_free = *reserved = *committed = 0;
    system_info info;
    if (get_system_info(&info) == B_OK)
    {
        *_free = B_PAGE_SIZE * (uint64)(info.max_pages - info.used_pages);
        *reserved = B_PAGE_SIZE * (uint64)info.cached_pages;
        *committed = B_PAGE_SIZE * (uint64)info.used_pages;
    }
#endif
}

XRCORE_API void log_vminfo()
{
    size_t w_free, w_reserved, w_committed;
    vminfo(&w_free, &w_reserved, &w_committed);
    Msg("* [ %s ]: free[%zu K], reserved[%zu K], committed[%zu K]", SDL_GetPlatform(), w_free / 1024, w_reserved / 1024, w_committed / 1024);
}

size_t xrMemory::mem_usage()
{
#if defined(XR_PLATFORM_WINDOWS)
    PROCESS_MEMORY_COUNTERS pmc = {};
    if (HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, GetCurrentProcessId()))
    {
        GetProcessMemoryInfo(h, &pmc, sizeof(pmc));
        CloseHandle(h);
    }
    return pmc.PagefileUsage;
#elif defined(XR_PLATFORM_LINUX) || defined(XR_PLATFORM_BSD) || defined(XR_PLATFORM_APPLE)
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return (size_t)ru.ru_maxrss;
#elif defined(XR_PLATFORM_HAIKU)
    system_info info;
    get_system_info(&info);
    return B_PAGE_SIZE * (uint64)info.used_pages;
#else
    return 0;
#endif
}

void xrMemory::mem_compact()
{
#if defined(XR_PLATFORM_WINDOWS)
    RegFlushKey(HKEY_CLASSES_ROOT);
    RegFlushKey(HKEY_CURRENT_USER);
#endif

    /*
    Следующая команда, в целом, не нужна.
    Современные аллокаторы достаточно грамотно и когда нужно возвращают память операционной системе.
    Эта строчка нужна, скорее всего, в определённых ситуациях, вроде использования файлов отображаемых в память,
    которые требуют большие свободные области памяти.
    */
    //HeapCompact(GetProcessHeap(), 0);

    // [DA_PORT] Приведённое выше рассуждение для нашей сборки НЕ работает, и замер это показал.
    //
    // Восемь переходов между уровнями: закоммиченная память 3532 -> 5713 МБ, то есть +720 МБ на
    // каждый заход на ОДИН И ТОТ ЖЕ уровень. При этом ЖИВЫЕ аллокации за те же заходы выросли всего
    // с 855 до 905 МБ, по +17 МБ. Сорокакратный разрыв означает, что память освобождена, но не
    // отдана: куча раздувается, а не течёт.
    //
    // ⚠️ И главное: закомментированная строка метила НЕ В ТУ КУЧУ. У нас USE_PURE_ALLOC, то есть
    // обычный malloc из CRT, и все наши аллокации живут в куче CRT (_get_heap_handle). GetProcessHeap
    // — соседняя куча, и по замеру она как раз стоит на месте (169 МБ от прогона к прогону). Так что
    // раскомментировать её было бы бесполезно, и это ровно та ошибка, из-за которой строку легко
    // счесть проверенной и ненужной.
    //
    // Вызывается mem_compact() раз на загрузку уровня, так что цена приемлема; сколько именно она
    // стоит и сколько возвращает — печатается ниже, чтобы это была измеренная цена, а не вера.
#if defined(XR_PLATFORM_WINDOWS)
    {
        const size_t before = mem_usage();
        const u64 t0 = CPU::QPC();

        // Порядок и состав взяты из ИСХОДНОГО движка: CoC-Xray (x-ray 1.6, база Dead Air) в
        // xrMemory.cpp:144 делает _heapmin() и HeapCompact, и делает это ВКЛЮЧЁННЫМ. OpenXRay оставил
        // от всей связки только очистку контейнеров строк ниже, а возврат памяти закомментировал.
        // То есть это не наша самодеятельность, а восстановление того, что в оригинале работало.
        //
        // _heapmin — штатный способ CRT вернуть системе неиспользованную часть своей кучи, и для
        // сборки на обычном malloc он уместнее HeapCompact. Держим оба: первый отдаёт, второй
        // дефрагментирует остаток.
        _heapmin();
        if (const HANDLE crt_heap = (HANDLE)_get_heap_handle())
            HeapCompact(crt_heap, 0);
        HeapCompact(GetProcessHeap(), 0);

        const size_t after = mem_usage();
        const float ms = float(double(CPU::QPC() - t0) / double(CPU::qpc_freq) * 1000.0);
        Msg("* [DA_PORT] mem_compact: %u -> %u МБ (вернулось %d МБ) за %.1f мс",
            (u32)(before / 1024 / 1024), (u32)(after / 1024 / 1024),
            (int)((long long)before - (long long)after) / 1024 / 1024, ms);
    }
#endif
    if (g_pStringContainer)
        g_pStringContainer->clean();
    if (g_pSharedMemoryContainer)
        g_pSharedMemoryContainer->clean();

#if defined(XR_PLATFORM_WINDOWS)
    if (strstr(Core.Params, "-swap_on_compact"))
        SetProcessWorkingSetSize(GetCurrentProcess(), size_t(-1), size_t(-1));
#endif
}


// [DA_PORT] Ловушка на выделение памяти заданного размера — со снимком стека.
//
// Замер довёл поиск утечки до подписи: блоки РОВНО по 16413 байт, две с половиной тысячи за
// перезагрузку сохранения, внутри сетевой пакет с событием GE_WPN_STATE_CHANGE. Дальше статикой
// не пройти: оба известных пула таких пакетов оказались пусты, а искать глазами по всему движку,
// кто ещё их держит, — гадание. Стек в момент выделения называет место сразу.
//
// По умолчанию выключена: g_da_alloc_trap_size = 0, и тогда это одно сравнение целых на аллокацию.
XRCORE_API int g_da_alloc_trap_size = 0;
XRCORE_API int g_da_alloc_trap_left = 0;
// Допуск: обход куч показывает размер БЛОКА, а он больше запрошенного на заголовок аллокатора.
// Первая попытка ловила ровно 16413 и молчала — запрашивают 16405. Чтобы не угадывать это число
// заново на каждом шаге, ловим окрестность и печатаем настоящий размер.
XRCORE_API int g_da_alloc_trap_slack = 16;
// Прореживание выборки. Для крупных редких блоков хватало «первых N»: они и были искомыми. Для
// мелких так нельзя — выделений по 24 байта тысячи в секунду, и первые N окажутся случайными
// прохожими. Берём каждое N-е совпадение, чтобы выборка равномерно накрыла окно перезагрузки.
XRCORE_API int g_da_alloc_trap_every = 1;

// [DA_PORT] Пока счётчик больше нуля, ловушка молчит. Взводит его логгер (log.cpp) на время своей
// работы, и это не перестраховка, а починка настоящего вылета.
//
// Как ронялось: ловушку навели на 512 000 байт — ровно столько занимает буфер САМОГО ЛОГА
// (xr_vector<xr_string> LogFile, элемент 32 байта, ёмкость 16 000 строк). Ловушка сработала внутри
// Msg, а печатает она тоже через Msg — то есть логгер вошёл в себя посреди перекладывания своего
// вектора. Лог в этот момент обрывался на середине стека, игра падала.
//
// Прежней защиты (g_da_trap_inside) не хватало по существу: она запрещает рекурсию В ЛОВУШКУ, а
// сломалась переиспользованная не-реентерабельная подсистема. ⛔ Класс ошибки: инструмент отладки,
// который пользуется тем же механизмом, что и наблюдаемый код.
thread_local int g_da_trap_suspend = 0;

namespace
{
// Снятие стека само выделяет память, поэтому без защиты ловушка ушла бы в бесконечную рекурсию.
thread_local bool g_da_trap_inside = false;

void da_alloc_trap(size_t size)
{
    if (g_da_alloc_trap_size <= 0)
        return;
    if (g_da_trap_suspend > 0)
        return;
    const int delta = (int)size - g_da_alloc_trap_size;
    if (delta > g_da_alloc_trap_slack || delta < -g_da_alloc_trap_slack)
        return;
    if (g_da_alloc_trap_left <= 0 || g_da_trap_inside)
        return;

    static u32 matches = 0;
    ++matches;
    if (g_da_alloc_trap_every > 1 && (matches % (u32)g_da_alloc_trap_every) != 0)
        return;

    g_da_trap_inside = true;
    --g_da_alloc_trap_left;

    // [DA_PORT] Снятие стека сериализуется. BuildStackTrace идёт через DbgHelp, а он НЕ
    // потокобезопасен: одновременный обход из двух потоков падает внутри самой библиотеки, и падает
    // так, что обработчик уже ничего не успевает написать — лог просто обрывается.
    //
    // Именно так и терялась игра: ловушка, оставленная взведённой в user.ltx, срабатывала во время
    // загрузки уровня, где выделяют память сразу несколько загрузочных потоков. Симптом — обрыв лога
    // без стека падения, то есть без единой подсказки на то, что виноват инструмент отладки.
    //
    // Замок держится только на время печати и только при совпавшем размере, то есть редко.
    // g_da_trap_inside не спасал: он thread_local и защищает от рекурсии, а не от параллельности.
    static Lock trap_lock;
    trap_lock.Enter();

    Msg("~ [DA_TRAP] выделение %u байт, стек:", (u32)size);
    const auto trace = BuildStackTrace(32);
    for (const auto& line : trace)
        Msg("~ [DA_TRAP]   %s", line.c_str());

    trap_lock.Leave();
    g_da_trap_inside = false;
}
} // namespace

// [DA_PORT] Счётчик выделений: сколько их, каких размеров и из скольких потоков.
//
// ЗАЧЕМ. Вопрос «менять ли аллокатор» до сих пор решался рассуждением. Выигрыш mimalloc над
// системным malloc берётся ровно из двух вещей: множества МЕЛКИХ выделений (у него для них
// отдельный путь без блокировок) и состязания ПОТОКОВ за общую кучу. Обе величины измеримы, и без
// них смена аллокатора — гадание, а цена её высока: под MinGW mimalloc придётся собирать из
// исходников, в дереве лежат только MSVC-овские .lib.
//
// ⚠️ Устройство продиктовано тем, что мы ВНУТРИ аллокатора, и обычные приёмы здесь запрещены:
//   • нельзя атомарные счётчики — общая строка кэша станет точкой состязания ровно там, где мы
//     состязание и измеряем, то есть прибор подделает свой же ответ;
//   • нельзя выделять память под учёт — это рекурсия в самих себя;
//   • нельзя списки с удалением слота на выходе потока — получится висячий указатель в горячем
//     пути, а платить за него будет не отладочная сборка, а игрок.
// Отсюда: постоянный массив слотов, слот выдаётся потоку один раз атомарным счётчиком и уже
// никогда не отбирается. Ценой одного лишнего указателя в TLS счёт становится неблокирующим.
//
// Цена в горячем пути: чтение указателя из TLS, два-три сложения и один интринсик старшего бита.
// Против сотни с лишним тактов самого malloc это шум, поэтому выключателя нет: прибор, который
// надо не забыть включить, к моменту нужды всегда выключен.
namespace
{
constexpr u32 DA_ALLOC_SLOTS = 64;
constexpr u32 DA_ALLOC_BUCKETS = 32; // до 2 ГБ одним блоком — с запасом

struct da_alloc_slot
{
    u32 thread_id;
    u64 alloc_calls, alloc_bytes;
    u64 realloc_calls, realloc_bytes;
    u64 free_calls;
    u64 small_calls; // small_alloc: отдельный путь аллокатора, смешивать с обычным нельзя
    u64 hist[DA_ALLOC_BUCKETS];
};

// Нулевая инициализация статикой: счёт идёт с первого же выделения, а они случаются задолго до
// main() — в конструкторах глобальных объектов.
da_alloc_slot g_da_slots[DA_ALLOC_SLOTS]{};
std::atomic<u32> g_da_slot_next{ 0 };
thread_local da_alloc_slot* g_da_slot = nullptr;
// Отметка времени последнего сброса. ⚠️ Именно CPU::QPC, а не SDL_GetTicks: у тиков SDL начало
// отсчёта привязано к инициализации своего таймера, и «сколько прошло» по ним читается как время
// от старта подсистемы, а не от нашего сброса. QPC той же функцией меряет соседний код в этом же
// файле (mem_compact), путать два источника незачем.
u64 g_da_stat_since = 0;

da_alloc_slot* da_slot_new()
{
    const u32 idx = g_da_slot_next.fetch_add(1, std::memory_order_relaxed);
    // Последний слот — общий «прочие». Потоков больше шестидесяти четырёх быть не должно, но если
    // станет, счёт не потеряется: он перестанет разделяться по потокам, о чём отчёт и скажет.
    da_alloc_slot* slot = &g_da_slots[idx < DA_ALLOC_SLOTS ? idx : DA_ALLOC_SLOTS - 1];
    slot->thread_id = (u32)SDL_ThreadID();
    g_da_slot = slot;
    return slot;
}

inline da_alloc_slot* da_slot()
{
    da_alloc_slot* slot = g_da_slot;
    return slot ? slot : da_slot_new();
}

// Ведро по старшему значащему биту: границы — честные степени двойки, промаха на «некруглых»
// размерах не бывает.
inline u32 da_bucket(size_t size)
{
    if (!size)
        return 0;
#if defined(__GNUC__) || defined(__clang__)
    const u32 bit = (u32)(63 - __builtin_clzll((unsigned long long)size));
#elif defined(_MSC_VER)
    unsigned long bit = 0;
    _BitScanReverse64(&bit, (unsigned __int64)size);
#else
    u32 bit = 0;
    for (size_t v = size; v >>= 1;)
        ++bit;
#endif
    return (u32)bit < DA_ALLOC_BUCKETS ? (u32)bit : DA_ALLOC_BUCKETS - 1;
}

inline void da_count_alloc(size_t size)
{
    da_alloc_slot* slot = da_slot();
    ++slot->alloc_calls;
    slot->alloc_bytes += size;
    ++slot->hist[da_bucket(size)];
}
} // namespace

void* xrMemory::mem_alloc(size_t size)
{
    const auto result = xr_internal_malloc(size);
    //TracyAlloc(result, size);
    da_count_alloc(size); // [DA_PORT]
    da_alloc_trap(size); // [DA_PORT]
    return result;
}

void* xrMemory::mem_alloc(size_t size, size_t alignment)
{
    const auto result = xr_internal_malloc_aligned(size, alignment);
    //TracyAlloc(result, size);
    da_count_alloc(size); // [DA_PORT]
    return result;
}

void* xrMemory::mem_alloc(size_t size, const std::nothrow_t&) noexcept
{
    const auto result = xr_internal_malloc_nothrow(size);
    //TracyAlloc(result, size);
    da_count_alloc(size); // [DA_PORT]
    return result;
}

void* xrMemory::mem_alloc(size_t size, size_t alignment, const std::nothrow_t&) noexcept
{
    const auto result = xr_internal_malloc_nothrow_aligned(size, alignment);
    //TracyAlloc(result, size);
    da_count_alloc(size); // [DA_PORT]
    return result;
}

void* xrMemory::small_alloc(size_t size) noexcept
{
    const auto result = xr_internal_small_alloc(size);
    //TracyAllocN(result, size, "small alloc");
    // [DA_PORT] Отдельной строкой: у аллокатора это отдельный путь (у mimalloc — без блокировок),
    // и смешивать его с обычными выделениями значит потерять именно тот сигнал, ради которого
    // всё считается.
    {
        da_alloc_slot* slot = da_slot();
        ++slot->small_calls;
        slot->alloc_bytes += size;
        ++slot->hist[da_bucket(size)];
    }
    return result;
}

void xrMemory::small_free(void* ptr) noexcept
{
    //TracyFree(ptr);
    if (ptr)
        ++da_slot()->free_calls; // [DA_PORT]
    xr_internal_small_free(ptr);
}

void* xrMemory::mem_realloc(void* ptr, size_t size)
{
    //TracyFree(ptr);
    const auto result = xr_internal_realloc(ptr, size);
    //TracyAllocN(result, size, "realloc");
    // [DA_PORT] Здесь же проходит ВСЯ память LuaJIT: lua_newstate получает lua_alloc, а тот зовёт
    // xr_realloc даже на первое выделение (ptr = nullptr). Ловушки da_alloc_trap в этом пути нет и
    // никогда не было — она стоит только в mem_alloc.
    {
        da_alloc_slot* slot = da_slot();
        ++slot->realloc_calls;
        slot->realloc_bytes += size;
        ++slot->hist[da_bucket(size)];
    }
    return result;
}

void* xrMemory::mem_realloc(void* ptr, size_t size, size_t alignment)
{
    //TracyFree(ptr);
    const auto result = xr_internal_realloc_aligned(ptr, size, alignment);
    //TracyAllocN(result, size, "realloc");
    {
        da_alloc_slot* slot = da_slot(); // [DA_PORT]
        ++slot->realloc_calls;
        slot->realloc_bytes += size;
        ++slot->hist[da_bucket(size)];
    }
    return result;
}

void xrMemory::mem_free(void* ptr)
{
    //TracyFree(ptr);
    // [DA_PORT] Пустые указатели не считаем: освобождение nullptr — не работа аллокатора, а в коде
    // движка оно встречается сплошь и рядом и исказило бы долю освобождений.
    if (ptr)
        ++da_slot()->free_calls;
    xr_internal_free(ptr);
}

void xrMemory::mem_free(void* ptr, size_t alignment)
{
    //TracyFree(ptr);
    if (ptr)
        ++da_slot()->free_calls; // [DA_PORT]
    xr_internal_free_aligned(ptr, alignment);
}

// [DA_PORT] Отчёт счётчика выделений (консоль: da_alloc_stat, с аргументом reset — сбросить окно).
XRCORE_API void da_alloc_stat_dump(bool reset, int frames)
{
    // ⚠️ Снимаем всё в локальные переменные ДО первой печати. Msg выделяет память сам, и без
    // снимка прибор досчитывал бы собственный вывод — ошибка того же класса, что уже была с
    // буфером лога, всплывшим в таблице роста как «утечка» ([[level-graph-node-leak]]).
    da_alloc_slot sum{};
    struct thread_row { u32 id; u64 calls; };
    thread_row rows[DA_ALLOC_SLOTS]{};
    u32 used = g_da_slot_next.load(std::memory_order_relaxed);
    if (used > DA_ALLOC_SLOTS)
        used = DA_ALLOC_SLOTS;
    for (u32 i = 0; i < used; ++i)
    {
        const da_alloc_slot& s = g_da_slots[i];
        sum.alloc_calls += s.alloc_calls;
        sum.alloc_bytes += s.alloc_bytes;
        sum.realloc_calls += s.realloc_calls;
        sum.realloc_bytes += s.realloc_bytes;
        sum.free_calls += s.free_calls;
        sum.small_calls += s.small_calls;
        for (u32 b = 0; b < DA_ALLOC_BUCKETS; ++b)
            sum.hist[b] += s.hist[b];
        rows[i].id = s.thread_id;
        rows[i].calls = s.alloc_calls + s.small_calls + s.realloc_calls;
    }

    const u64 now = CPU::QPC();
    const float secs = (now > g_da_stat_since) ? float(double(now - g_da_stat_since) / double(CPU::qpc_freq)) : 0.0f;
    const float per_s = secs > 0.01f ? 1.0f / secs : 0.0f;

    Msg("~ [DA_ALLOC] окно %.1f с, потоков %u (слотов занято %u из %u)", secs, used, used, DA_ALLOC_SLOTS);
    if (g_da_slot_next.load(std::memory_order_relaxed) > DA_ALLOC_SLOTS)
        Msg("~ [DA_ALLOC] ⚠️ потоков было БОЛЬШЕ %u: лишние сведены в последний слот, "
            "разбивка по потокам неполна", DA_ALLOC_SLOTS);

    Msg("~ [DA_ALLOC] выделений    %12llu  %9.0f/с  %8.1f МБ  %7.1f МБ/с", (unsigned long long)sum.alloc_calls,
        sum.alloc_calls * per_s, sum.alloc_bytes / 1048576.0, sum.alloc_bytes / 1048576.0 * per_s);
    Msg("~ [DA_ALLOC] мелких       %12llu  %9.0f/с   (отдельный путь аллокатора)",
        (unsigned long long)sum.small_calls, sum.small_calls * per_s);
    Msg("~ [DA_ALLOC] перевыделен. %12llu  %9.0f/с  %8.1f МБ   (сюда идёт ВСЯ память LuaJIT)",
        (unsigned long long)sum.realloc_calls, sum.realloc_calls * per_s, sum.realloc_bytes / 1048576.0);
    Msg("~ [DA_ALLOC] освобождений %12llu  %9.0f/с", (unsigned long long)sum.free_calls, sum.free_calls * per_s);

    // ⚠️ НА КАДР — главное число, и вот почему. «В секунду» зависит от того, сколько кадров машина
    // успела, а стенд идёт на 27 кадрах против полутора-двух сотен в живой игре: одна и та же
    // нагрузка читается там вшестеро меньшей. На кадр — величина той же природы, что и бюджет
    // кадра, и сравнивается с ним напрямую.
    if (frames > 0)
        Msg("~ [DA_ALLOC] НА КАДР: %.0f операций (выделений %.0f, перевыделений %.0f, освобождений %.0f)",
            double(sum.alloc_calls + sum.small_calls + sum.realloc_calls + sum.free_calls) / frames,
            double(sum.alloc_calls + sum.small_calls) / frames, double(sum.realloc_calls) / frames,
            double(sum.free_calls) / frames);

    // Размеры. Границы выбраны не «по кругу», а по тому, что различает аллокатор: у mimalloc
    // MI_SMALL_SIZE_MAX = 1024 байта, и всё, что ниже, идёт по быстрому пути без блокировок.
    {
        static const struct { u32 upto_bit; pcstr name; } ranges[] = {
            { 5, "=<32" }, { 6, "=<64" }, { 7, "=<128" }, { 8, "=<256" }, { 9, "=<512" },
            { 10, "=<1K" }, { 12, "=<4K" }, { 16, "=<64K" }, { 20, "=<1M" }, { 31, ">1M" },
        };
        string512 line;
        xr_strcpy(line, "~ [DA_ALLOC] размеры: ");
        u32 from = 0;
        u64 upto_1k = 0;
        u64 hist_total = 0;
        for (u32 b = 0; b < DA_ALLOC_BUCKETS; ++b)
            hist_total += sum.hist[b];
        for (const auto& r : ranges)
        {
            u64 n = 0;
            for (u32 b = from; b <= r.upto_bit && b < DA_ALLOC_BUCKETS; ++b)
                n += sum.hist[b];
            if (r.upto_bit <= 10)
                upto_1k += n;
            from = r.upto_bit + 1;
            string64 piece;
            xr_sprintf(piece, " %s %.1f%%", r.name, hist_total ? n * 100.0 / hist_total : 0.0);
            xr_strcat(line, piece);
        }
        Msg("%s", line);
        Msg("~ [DA_ALLOC] до 1 КБ: %.1f%% запросов — именно эту долю mimalloc уводит на путь "
            "без блокировок; если она мала, менять аллокатор незачем",
            hist_total ? upto_1k * 100.0 / hist_total : 0.0);
    }

    // Потоки. Второй сигнал: за общую кучу состязаются только те потоки, которые в неё ходят.
    {
        std::sort(rows, rows + used, [](const thread_row& a, const thread_row& b) { return a.calls > b.calls; });
        u64 all = 0;
        for (u32 i = 0; i < used; ++i)
            all += rows[i].calls;
        string512 line;
        xr_strcpy(line, "~ [DA_ALLOC] по потокам:");
        u64 shown = 0;
        const u32 top = used < 4 ? used : 4;
        for (u32 i = 0; i < top; ++i)
        {
            string64 piece;
            xr_sprintf(piece, " %u %.1f%%,", rows[i].id, all ? rows[i].calls * 100.0 / all : 0.0);
            xr_strcat(line, piece);
            shown += rows[i].calls;
        }
        string64 rest;
        xr_sprintf(rest, " прочие %.1f%%", all ? (all - shown) * 100.0 / all : 0.0);
        xr_strcat(line, rest);
        Msg("%s", line);
        Msg("~ [DA_ALLOC] один поток тянет %.1f%% запросов. Чем ровнее раскладка, тем больше "
            "смысла в аллокаторе с отдельной кучей на поток", all ? rows[0].calls * 100.0 / all : 0.0);
    }

    Msg("~ [DA_ALLOC] аллокатор сейчас: %s",
#if defined(USE_MIMALLOC)
        "mimalloc"
#elif defined(USE_XR_ALIGNED_MALLOC)
        "xr_aligned_malloc"
#else
        "системный malloc (USE_PURE_ALLOC)"
#endif
    );

    if (reset)
    {
        da_alloc_stat_reset();
        Msg("~ [DA_ALLOC] счётчики сброшены, окно начато заново");
    }
}

// [DA_PORT] Цена одной операции аллокатора — замером, а не через справочную константу.
//
// ЗАЧЕМ. Счётчик даёт ЧИСЛО операций, а решение про аллокатор требует ВРЕМЕНИ. Перевести одно в
// другое, подставив «ну, наносекунд сорок», — это выдать допущение за результат. Здесь время
// меряется на этой самой сборке, этим самым аллокатором.
//
// ⚠️ Образец нагрузки выбран не «выделил-освободил в цикле»: так меряется лучший случай, когда
// блок возвращается из горячего кэша сразу же. Настоящий движок держит десятки тысяч живых блоков
// и трогает их вразнобой. Поэтому здесь установившийся режим: пул живых указателей, на каждом шаге
// один освобождается и на его место приходит новый — как оно и происходит в игре.
//
// ⚠️ Замер ходит в xr_internal_* напрямую, мимо xrMemory, поэтому в счётчик он не попадает по
// построению; слот всё равно снимается и восстанавливается — на случай, если макрос когда-нибудь
// заведут через учитываемый путь. Прибор, попавший в собственную таблицу, у нас уже был: буфер
// лога однажды прочитался как утечка.
XRCORE_API void da_alloc_bench(int frames)
{
    constexpr u32 LIVE = 16384; // живых блоков: столько же порядком, сколько держит уровень
    constexpr u32 ROUNDS = 400000;
    static const size_t sizes[] = { 24, 32, 40, 56, 64, 96, 128 }; // из наблюдённой раскладки

    // ⚠️ Наблюдённую частоту снимаем ДО замера, а не после. Сам замер занимает секунды, и если
    // считать окно потом, эти секунды разбавят частоту и ответ выйдет заниженным.
    double ops_per_sec = 0.0;
    double ops_total = 0.0;
    {
        double ops = 0.0;
        u32 used = g_da_slot_next.load(std::memory_order_relaxed);
        if (used > DA_ALLOC_SLOTS)
            used = DA_ALLOC_SLOTS;
        for (u32 i = 0; i < used; ++i)
        {
            const da_alloc_slot& s = g_da_slots[i];
            ops += double(s.alloc_calls + s.small_calls + s.realloc_calls + s.free_calls);
        }
        const u64 now = CPU::QPC();
        const double window = (now > g_da_stat_since) ? double(now - g_da_stat_since) / double(CPU::qpc_freq) : 0.0;
        ops_total = ops;
        if (window > 0.5 && ops > 0.0)
            ops_per_sec = ops / window;
    }

    da_alloc_slot* slot = da_slot();
    const da_alloc_slot saved = *slot; // снимок: замер не должен попасть в отчёт

    void** pool = (void**)xr_internal_malloc(LIVE * sizeof(void*));
    if (!pool)
    {
        Msg("! [DA_ALLOC] замер не выполнен: не удалось выделить пул");
        return;
    }

    u32 rnd = 123456789;
    auto next = [&rnd]() { rnd = rnd * 1664525u + 1013904223u; return rnd; };

    for (u32 i = 0; i < LIVE; ++i)
        pool[i] = xr_internal_malloc(sizes[next() % std::size(sizes)]);

    const u64 t0 = CPU::QPC();
    for (u32 r = 0; r < ROUNDS; ++r)
    {
        const u32 slot_idx = next() % LIVE;
        xr_internal_free(pool[slot_idx]);
        pool[slot_idx] = xr_internal_malloc(sizes[next() % std::size(sizes)]);
    }
    const u64 t1 = CPU::QPC();

    for (u32 i = 0; i < LIVE; ++i)
        xr_internal_free(pool[i]);
    xr_internal_free(pool);

    *slot = saved;

    const double secs = double(t1 - t0) / double(CPU::qpc_freq);
    const double ns_pair = secs * 1e9 / double(ROUNDS);
    Msg("~ [DA_ALLOC] цена операции: %.1f нс на пару «освободить+выделить» (%u повторов, %u живых блоков)",
        ns_pair, ROUNDS, LIVE);

    // Пересчёт в долю кадра — то, ради чего всё и меряется.
    if (frames > 0 && ops_total > 0.0)
    {
        // Вот он, ответ на весь вопрос: сколько миллисекунд КАДРА уходит в аллокатор.
        const double ms_frame = (ops_total / frames) * (ns_pair / 2.0) / 1e6;
        Msg("~ [DA_ALLOC] ИТОГ: %.0f операций на кадр по %.1f нс = %.3f мс кадра", ops_total / frames,
            ns_pair / 2.0, ms_frame);
        Msg("~ [DA_ALLOC] при 60 кадрах это %.1f%% бюджета кадра, при 144 — %.1f%%", ms_frame / 16.67 * 100.0,
            ms_frame / 6.94 * 100.0);
        Msg("~ [DA_ALLOC] ⚠️ выигрыш от смены аллокатора — ДОЛЯ от этого числа, а не оно само: "
            "другой аллокатор не убирает операции, а удешевляет их");
    }
    else if (ops_per_sec > 0.0)
    {
        const double ms_per_sec = ops_per_sec * (ns_pair / 2.0) / 1e6;
        Msg("~ [DA_ALLOC] при наблюдённых %.0f операций/с это %.1f мс на секунду игры", ops_per_sec, ms_per_sec);
        Msg("~ [DA_ALLOC] ⚠️ число кадров окна неизвестно — доля кадра не посчитана. "
            "Мерить через `da_alloc_stat <кадров> bench`");
    }
    else
        Msg("~ [DA_ALLOC] окно замера пусто: сперва `da_alloc_stat <кадров>`, потом сюда");
}

// [DA_PORT] Сброс без печати: нужен, чтобы окно замера начиналось чисто, а не с отчёта о том,
// что было до него.
XRCORE_API void da_alloc_stat_reset()
{
    for (u32 i = 0; i < DA_ALLOC_SLOTS; ++i)
    {
        da_alloc_slot& s = g_da_slots[i];
        s.alloc_calls = s.alloc_bytes = s.realloc_calls = s.realloc_bytes = 0;
        s.free_calls = s.small_calls = 0;
        for (u32 b = 0; b < DA_ALLOC_BUCKETS; ++b)
            s.hist[b] = 0;
    }
    g_da_stat_since = CPU::QPC();
}

// xr_strdup
XRCORE_API pstr xr_strdup(pcstr string)
{
#ifdef USE_MIMALLOC
    return mi_strdup(string);
#else
    VERIFY(string);
    size_t len = xr_strlen(string) + 1;
    auto memory = static_cast<char*>(xr_malloc(len));
    CopyMemory(memory, string, len);
    return memory;
#endif
}

[[nodiscard]] void* operator new(size_t size)
{
    return Memory.mem_alloc(size);
}

[[nodiscard]] void* operator new[](size_t size)
{
    return Memory.mem_alloc(size);
}

[[nodiscard]] void* operator new(size_t size, const std::nothrow_t&) noexcept
{
    return Memory.mem_alloc(size);
}

[[nodiscard]] void* operator new[](size_t size, const std::nothrow_t&) noexcept
{
    return Memory.mem_alloc(size);
}

[[nodiscard]] void* operator new(size_t size, std::align_val_t alignment)
{
    return Memory.mem_alloc(size, static_cast<size_t>(alignment));
}

[[nodiscard]] void* operator new[](size_t size, std::align_val_t alignment)
{
    return Memory.mem_alloc(size, static_cast<size_t>(alignment));
}

[[nodiscard]] void* operator new(size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    return Memory.mem_alloc(size, static_cast<size_t>(alignment));
}

[[nodiscard]] void* operator new[](size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    return Memory.mem_alloc(size, static_cast<size_t>(alignment));
}

void operator delete(void* ptr) noexcept
{
    Memory.mem_free(ptr);
}

void operator delete[](void* ptr) noexcept
{
    Memory.mem_free(ptr);
}

void operator delete(void* ptr, std::align_val_t alignment) noexcept
{
    Memory.mem_free(ptr, static_cast<size_t>(alignment));
}

void operator delete[](void* ptr, std::align_val_t alignment) noexcept
{
    Memory.mem_free(ptr, static_cast<size_t>(alignment));
}

void operator delete(void* ptr, size_t) noexcept
{
    Memory.mem_free(ptr);
}

void operator delete[](void* ptr, size_t) noexcept
{
    Memory.mem_free(ptr);
}

void operator delete(void* ptr, size_t, std::align_val_t alignment) noexcept
{
    Memory.mem_free(ptr, static_cast<size_t>(alignment));
}

void operator delete[](void* ptr, size_t, std::align_val_t alignment) noexcept
{
    Memory.mem_free(ptr, static_cast<size_t>(alignment));
}

XRCORE_API void* xr_malloc(size_t size)
{
    return Memory.mem_alloc(size);
}

XRCORE_API void* xr_realloc(void* ptr, size_t size)
{
    return Memory.mem_realloc(ptr, size);
}
