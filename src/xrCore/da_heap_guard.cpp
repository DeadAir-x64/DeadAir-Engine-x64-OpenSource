#include "stdafx.h"
#include "da_heap_guard.h"
#include "Debug/StackTrace.h"

#include <cstdlib>
#include <cstring>
#include <atomic>

#if defined(XR_PLATFORM_WINDOWS)
#include <malloc.h> // _msize
#else
#include <malloc.h> // malloc_usable_size
#endif

// [DA_PORT] Разбор замысла — в da_heap_guard.h. Здесь только устройство.
//
// ⛔ Модуль имеет смысл лишь при USE_PURE_ALLOC: он опирается на то, что xrMemory сводится к
// malloc/free и что у блока можно спросить пригодный размер. У mimalloc свой учёт, и подмешивать к
// нему карантин нельзя. Флаг ставит CMake (MEMORY_ALLOCATOR), а на Windows его же выбирает
// xrMemory.cpp по _DEBUG — поэтому условие повторяет то, что написано там, слово в слово.
#if defined(XR_PLATFORM_WINDOWS) && !defined(USE_PURE_ALLOC) && !defined(USE_MIMALLOC)
#   ifdef _DEBUG
#       define USE_PURE_ALLOC
#   else
#       define USE_MIMALLOC
#   endif
#endif

#if defined(USE_PURE_ALLOC)

namespace
{
// Байт отравления. 0xDD выбран не случайно: восьмибайтовый указатель из него даёт
// 0xDDDDDDDDDDDDDDDD — адрес в некартированной области, то есть немедленное падение, и при этом
// адрес запоминающийся. Ноль или 0xCC для этого не годятся: ноль неотличим от «поле не заполняли»
// (а это ровно тот вопрос, который мы и разрешаем), а 0xCC — это код инструкции int3, и стек с ним
// читается как «прыжок в отладочную заглушку», что уводит разбор в сторону.
constexpr unsigned char DA_POISON = 0xDD;

// ⭐ Отравляем ВОСЬМИБАЙТОВЫМИ СЛОВАМИ, а не байтом: почему именно такое значение — в заголовке,
// там же оно и объявлено, потому что по нему ищут в логе.
constexpr u64 DA_POISON_WORD = DA_HEAP_POISON_WORD;

// Метка карантина. Проверяется ВМЕСТЕ с адресом самого блока: живой объект должен был бы содержать
// и эту константу, и по соседству собственный адрес — совпадение исключено.
//
// ⭐ Лежит В ХВОСТЕ блока, а не в начале, и это не косметика. По смещению 0 у любого объекта движка
// стоит таблица виртуальных функций, и `объект->метод()` после удаления — самое частое обращение к
// мертвецу вообще. Пока метка занимала начало, именно этот случай давал в логе `ffffffffffffffff`
// вместо узнаваемого адреса (метка тоже была неканонической). Проверено живой игрой дважды.
//
// Теперь начало блока — чистая отрава, и вызов метода мертвеца падает по 00007ddddddddddd.
// Значение метки на всякий случай тоже сделано каноническим.
constexpr u64 DA_QUARANTINE_MARK = 0x00007DEAD12DEAD1ull;

// Заполняем не больше этого: у крупных буферов (текстуры, звук, геометрия) отравление целиком
// стоило бы столько, что играть стало бы нельзя. Заголовок объекта, таблица виртуальных функций и
// первые поля — всё в начале блока, а именно через них и происходит обращение к мертвецу.
//
// ⚠️ Плата за это честная: обращение к ХВОСТУ большого блока после освобождения контроль пропустит.
constexpr size_t DA_POISON_MAX = 4096;

constexpr u32 DA_GUARD_SLOTS = 64; // столько же, сколько у счётчика выделений по соседству

// Вместимость кольца НА ПОТОК, в блоках. Считается из предела по байтам, а не берётся с потолка.
//
// ⚠️ Первая версия ставила здесь 32768 наглухо, и это оказалось ошибкой, которую показал замер:
// за 4 секунды запуска из карантина вытеснилось 1.85 МЛН блоков, хотя под ним лежало всего 37 МБ
// при пределе 64 МБ на поток. То есть упирались в ЧИСЛО ячеек, а не в память: блоки у движка
// мелкие (около 270 байт), и 32768 штук — это меньше девяти мегабайт.
//
// ⛔ Чем это плохо: срок жизни блока в карантине падает до долей секунды, а вся ценность карантина
// ровно в том, чтобы обращение к мертвецу через минуту всё ещё попадало в отраву. Настройка
// DA_HEAP_GUARD_MB при этом почти ничего не решала — предел по байтам не наступал никогда.
//
// Отсюда: ячеек столько, чтобы предел по байтам наступал ПЕРВЫМ при типичном мелком блоке.
constexpr size_t DA_GUARD_TYPICAL_BLOCK = 256;
constexpr u32 DA_GUARD_RING_MIN = 65536;

u32 da_ring_capacity(size_t quarantine_bytes)
{
    const size_t wanted = quarantine_bytes / DA_GUARD_TYPICAL_BLOCK;
    if (wanted < DA_GUARD_RING_MIN)
        return DA_GUARD_RING_MIN;
    // Потолок: 4 млн ячеек — это 64 МБ указателей и размеров на поток, дальше сам учёт становится
    // дороже того, что он стережёт.
    return wanted > 4u * 1024 * 1024 ? 4u * 1024 * 1024 : (u32)wanted;
}

// Карантин на поток, без единой блокировки. Устройство повторяет соседний счётчик выделений в
// xrMemory.cpp намеренно: мы внутри аллокатора, и там уже разобрано, почему нельзя ни атомарных
// счётчиков в горячем пути, ни выделения памяти под собственный учёт, ни снятия слота при выходе
// потока. Слот выдаётся один раз и не отбирается.
//
// ⚠️ Блок, выделенный одним потоком, может освобождаться другим — это нормально: в карантин он
// попадёт к тому, кто освобождал, а настоящий free() потокобезопасен.
struct da_guard_slot
{
    void** ring;
    size_t* sizes;
    u32 cap;       // вместимость кольца, считается при первом обращении
    u32 head;      // куда класть следующий
    u32 count;     // сколько лежит
    size_t bytes;  // сколько байт лежит
    u64 double_frees;
    u64 evicted;   // сколько блоков вытеснено и отдано куче по-настоящему
};

da_guard_slot g_slots[DA_GUARD_SLOTS]{};
std::atomic<u32> g_slot_next{ 0 };
thread_local da_guard_slot* g_slot = nullptr;

// Предел карантина на поток. Читается из окружения один раз.
size_t g_quarantine_bytes = 64ull << 20;

// Состояние выключателя: -1 «ещё не спрашивали», 0 выключен, 1 включён.
//
// ⚠️ Ленивая проверка, а не глобальный конструктор. Выделения начинаются в конструкторах глобальных
// объектов, то есть ДО того, как наш конструктор гарантированно отработал бы; порядок между
// единицами трансляции не определён. Гонка здесь безобидна: два потока в худшем случае лишний раз
// прочитают окружение и запишут одно и то же значение.
std::atomic<int> g_enabled{ -1 };

size_t da_usable_size(void* ptr)
{
#if defined(XR_PLATFORM_WINDOWS)
    return _msize(ptr);
#else
    return malloc_usable_size(ptr);
#endif
}

int da_read_environment()
{
    const char* on = std::getenv("DA_HEAP_GUARD");
    const int enabled = (on && on[0] && on[0] != '0') ? 1 : 0;

    if (enabled)
    {
        if (const char* mb = std::getenv("DA_HEAP_GUARD_MB"))
        {
            const long value = std::strtol(mb, nullptr, 10);
            if (value > 0 && value < 4096)
                g_quarantine_bytes = size_t(value) << 20;
        }
    }
    return enabled;
}

da_guard_slot* da_slot_new()
{
    const u32 idx = g_slot_next.fetch_add(1, std::memory_order_relaxed);
    // Последний слот — общий: если потоков окажется больше, карантин не сломается, а станет
    // разделяемым. Это неточно для отчёта, но безопасно для работы.
    da_guard_slot* slot = &g_slots[idx < DA_GUARD_SLOTS ? idx : DA_GUARD_SLOTS - 1];

    // Кольцо заводится ЛЕНИВО и только когда контроль включён: в обычной сборке эти массивы не
    // существуют вовсе, и модуль не стоит ни байта.
    if (!slot->ring)
    {
        slot->cap = da_ring_capacity(g_quarantine_bytes);
        // ⛔ Только сырой malloc. Любой xr_* здесь — это рекурсия в аллокатор из его же нутра.
        slot->ring = (void**)std::malloc(sizeof(void*) * slot->cap);
        slot->sizes = (size_t*)std::malloc(sizeof(size_t) * slot->cap);
    }
    g_slot = slot;
    return slot;
}

inline da_guard_slot* da_slot()
{
    da_guard_slot* slot = g_slot;
    return slot ? slot : da_slot_new();
}

// Сообщение о двойном освобождении. Стек снимается под замком: BuildStackTrace идёт через DbgHelp,
// а он не потокобезопасен — одновременный обход из двух потоков падает внутри самой библиотеки, и
// падает так, что лог просто обрывается. Ровно на эти грабли уже наступала ловушка выделений в
// xrMemory.cpp, повторять не будем.
thread_local bool g_reporting = false;

// Печатается КАЖДОЕ срабатывание со стеком, без ограничителя.
//
// Ограничитель здесь был и снят намеренно: это сборка для поиска ошибок, и находка, которую урезали
// ради размера лога, — потерянная находка. Лог растёт, зато по нему можно искать; замедление от
// снятия стека в отладочном прогоне допустимо.
//
// ⚠️ Полнота отчётов держится не на ограничителе, а на том, что ЛОЖНЫХ срабатываний нет: метка
// гасится при вытеснении из карантина (см. da_heap_guard_free), иначе переиспользованный блок
// обвинялся бы зря. Первый прогон без этого дал 488 ложных сообщений одним потоком.
void da_report_double_free(void* ptr)
{
    if (g_reporting)
        return;

    g_reporting = true;

    static Lock report_lock;
    report_lock.Enter();

    Msg("! [DA_HEAP_GUARD] ДВОЙНОЕ ОСВОБОЖДЕНИЕ блока %p, стек:", ptr);
    const auto trace = BuildStackTrace(32);
    for (const auto& line : trace)
        Msg("! [DA_HEAP_GUARD]   %s", line.c_str());

    report_lock.Leave();
    g_reporting = false;
}
} // namespace

bool da_heap_guard_enabled()
{
    int state = g_enabled.load(std::memory_order_relaxed);
    if (state < 0)
    {
        state = da_read_environment();
        g_enabled.store(state, std::memory_order_relaxed);
    }
    return state != 0;
}

bool da_heap_guard_free(void* ptr)
{
    if (!ptr || !da_heap_guard_enabled())
        return false;

    const size_t size = da_usable_size(ptr);

    // Блоку меньше метки карантин не положен: распознать в нём повторное освобождение нечем, а
    // отравить так, чтобы это было заметно, всё равно не выйдет.
    if (size < 16)
        return false;

    u64* const words = (u64*)ptr;

    // Метки живут в ДВУХ ПОСЛЕДНИХ словах блока — начало обязано остаться чистой отравой (см.
    // пояснение у DA_QUARANTINE_MARK). Блок не меньше 16 байт, значит эти два слова есть всегда.
    u64* const marks = words + (size / sizeof(u64)) - 2;

    // Двойное освобождение: метка на месте И соседнее слово указывает на сам блок.
    if (marks[0] == DA_QUARANTINE_MARK && marks[1] == (u64)(uintptr_t)ptr)
    {
        da_slot()->double_frees++;
        da_report_double_free(ptr);
        // ⛔ Возвращаем true — то есть НЕ отдаём блок куче. Второй free() испортил бы её списки, и
        // падение приехало бы позже, в непричастном месте: как раз то, от чего мы здесь и уходим.
        return true;
    }

    const size_t poison = size < DA_POISON_MAX ? size : DA_POISON_MAX;

    // Слова целиком — ими и читаются указатели. Хвост, не кратный восьми, добиваем байтом: там
    // указателю всё равно не поместиться.
    const size_t whole = poison / sizeof(u64);
    for (size_t i = 0; i < whole; ++i)
        words[i] = DA_POISON_WORD;
    if (const size_t rest = poison - whole * sizeof(u64))
        std::memset((char*)ptr + whole * sizeof(u64), DA_POISON, rest);

    // Метки пишутся ПОСЛЕ отравления: у мелких блоков они попадают внутрь отравленной области и
    // должны её перекрыть, иначе следующий заход не опознал бы повторное освобождение.
    marks[0] = DA_QUARANTINE_MARK;
    marks[1] = (u64)(uintptr_t)ptr;

    da_guard_slot* slot = da_slot();
    if (!slot->ring || !slot->sizes)
        return false; // памяти под кольцо не нашлось — отравили и отдаём обычным путём

    // Освобождаем по-настоящему только то, что вытесняется. Пока карантин не полон, блок остаётся
    // за нами — и именно поэтому обращение к нему падает на отраве, а не на чужом живом объекте.
    while (slot->count == slot->cap || slot->bytes + size > g_quarantine_bytes)
    {
        if (!slot->count)
            break;

        const u32 tail = (slot->head + slot->cap - slot->count) % slot->cap;

        // ⛔ Метку ГАСИМ перед возвратом куче, и это не мелочь, а заслон от ложного обвинения.
        //
        // Иначе выходит так: блок отлежал карантин, вытеснился, куча выдала ту же память заново,
        // новый хозяин первые 16 байт не тронул — и при его честном освобождении мы видим свою же
        // старую метку вместе с адресом, который совпал просто потому, что это тот же адрес. Прибор
        // доложил бы о двойном освобождении там, где кода вины нет вовсе.
        //
        // ⚠️ Писать только ДО free: после него это обращение к чужой памяти.
        // ⚠️ И ровно туда, где метка лежит — в хвост, а не в начало.
        void* const evict = slot->ring[tail];
        ((u64*)evict)[slot->sizes[tail] / sizeof(u64) - 2] = 0;

        std::free(evict);
        slot->bytes -= slot->sizes[tail];
        --slot->count;
        ++slot->evicted;
    }

    slot->ring[slot->head] = ptr;
    slot->sizes[slot->head] = size;
    slot->head = (slot->head + 1) % slot->cap;
    ++slot->count;
    slot->bytes += size;
    return true;
}

bool da_heap_guard_realloc(void* ptr, size_t size, void** out)
{
    // Пустой указатель — это обычное выделение, старого блока нет и отравлять нечего.
    if (!ptr || !da_heap_guard_enabled())
        return false;

    const size_t old_size = da_usable_size(ptr);

    void* fresh = std::malloc(size);
    if (!fresh)
        return false; // памяти нет — уходим на обычный путь, старый блок ещё цел

    std::memcpy(fresh, ptr, old_size < size ? old_size : size);

    // Старый блок уходит через тот же карантин, что и обычное освобождение. Если контроль его не
    // принял (слишком мал, или кольцо не завелось), отдаём куче руками — иначе это была бы утечка.
    if (!da_heap_guard_free(ptr))
        std::free(ptr);

    *out = fresh;
    return true;
}

u64 da_heap_guard_double_frees()
{
    u64 total = 0;
    const u32 used = g_slot_next.load(std::memory_order_relaxed);
    for (u32 i = 0; i < (used < DA_GUARD_SLOTS ? used : DA_GUARD_SLOTS); ++i)
        total += g_slots[i].double_frees;
    return total;
}

void da_heap_guard_stat()
{
    if (!da_heap_guard_enabled())
    {
        Msg("~ [DA_HEAP_GUARD] выключен. Включается переменной окружения DA_HEAP_GUARD=1 ДО запуска.");
        return;
    }

    u64 blocks = 0, bytes = 0, doubles = 0, threads = 0, evicted = 0;
    const u32 used = g_slot_next.load(std::memory_order_relaxed);
    for (u32 i = 0; i < (used < DA_GUARD_SLOTS ? used : DA_GUARD_SLOTS); ++i)
    {
        if (!g_slots[i].ring)
            continue;
        ++threads;
        blocks += g_slots[i].count;
        bytes += g_slots[i].bytes;
        doubles += g_slots[i].double_frees;
        evicted += g_slots[i].evicted;
    }

    Msg("~ [DA_HEAP_GUARD] карантин: %llu блоков, %.1f МБ в %llu потоках (предел %llu МБ на поток)",
        blocks, double(bytes) / (1024.0 * 1024.0), threads, (u64)(g_quarantine_bytes >> 20));
    // ⚠️ Вытеснение печатается рядом намеренно. Пока оно НОЛЬ, память из карантина куче не
    // возвращалась ни разу, а значит ложное срабатывание на переиспользованном блоке НЕВОЗМОЖНО в
    // принципе — и любое сообщение о двойном освобождении приходится считать настоящим.
    Msg("~ [DA_HEAP_GUARD] вытеснено из карантина (отдано куче): %llu", evicted);
    Msg("~ [DA_HEAP_GUARD] двойных освобождений поймано: %llu", doubles);
    Msg("~ [DA_HEAP_GUARD] падение по адресу 00007ddddddddddd = обращение к ОСВОБОЖДЁННОЙ памяти");
}

#else // !USE_PURE_ALLOC

// При mimalloc контроль недоступен: _msize к его блокам неприменим. Заглушки, чтобы не разводить
// условную компиляцию по местам вызова.
bool da_heap_guard_enabled() { return false; }
bool da_heap_guard_free(void*) { return false; }
bool da_heap_guard_realloc(void*, size_t, void**) { return false; }
u64 da_heap_guard_double_frees() { return 0; }

void da_heap_guard_stat()
{
    Msg("~ [DA_HEAP_GUARD] недоступен: сборка с mimalloc. Нужен MEMORY_ALLOCATOR=standard.");
}

#endif
