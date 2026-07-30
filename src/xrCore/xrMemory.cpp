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

void* xrMemory::mem_alloc(size_t size)
{
    const auto result = xr_internal_malloc(size);
    //TracyAlloc(result, size);
    da_alloc_trap(size); // [DA_PORT]
    return result;
}

void* xrMemory::mem_alloc(size_t size, size_t alignment)
{
    const auto result = xr_internal_malloc_aligned(size, alignment);
    //TracyAlloc(result, size);
    return result;
}

void* xrMemory::mem_alloc(size_t size, const std::nothrow_t&) noexcept
{
    const auto result = xr_internal_malloc_nothrow(size);
    //TracyAlloc(result, size);
    return result;
}

void* xrMemory::mem_alloc(size_t size, size_t alignment, const std::nothrow_t&) noexcept
{
    const auto result = xr_internal_malloc_nothrow_aligned(size, alignment);
    //TracyAlloc(result, size);
    return result;
}

void* xrMemory::small_alloc(size_t size) noexcept
{
    const auto result = xr_internal_small_alloc(size);
    //TracyAllocN(result, size, "small alloc");
    return result;
}

void xrMemory::small_free(void* ptr) noexcept
{
    //TracyFree(ptr);
    xr_internal_small_free(ptr);
}

void* xrMemory::mem_realloc(void* ptr, size_t size)
{
    //TracyFree(ptr);
    const auto result = xr_internal_realloc(ptr, size);
    //TracyAllocN(result, size, "realloc");
    return result;
}

void* xrMemory::mem_realloc(void* ptr, size_t size, size_t alignment)
{
    //TracyFree(ptr);
    const auto result = xr_internal_realloc_aligned(ptr, size, alignment);
    //TracyAllocN(result, size, "realloc");
    return result;
}

void xrMemory::mem_free(void* ptr)
{
    //TracyFree(ptr);
    xr_internal_free(ptr);
}

void xrMemory::mem_free(void* ptr, size_t alignment)
{
    //TracyFree(ptr);
    xr_internal_free_aligned(ptr, alignment);
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
