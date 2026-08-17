// [DA_PORT] Прогонный стенд под санитайзеры: дёргает подсистемы напрямую, без рендера и уровня.
//
// ЗАЧЕМ. Санитайзеры собираются (da_port/tools/asan), но запустить под ними САМУ игру нельзя:
// выделенный сервер жёстко требует модуль рендера, R4 бывает только под DX11, GL мы из сборки
// убрали и он отстал на 25 ошибок, а заглушка IRender — это 124 виртуальных метода, большинство из
// которых обязаны возвращать пригодные объекты. Стенд обходит это целиком.
//
// ЧТО ЛОВИТ: обращение к освобождённому, выход за границы, утечки, неопределённое поведение — ровно
// тот класс, что мы весь цикл вычёсывали руками по одному.
// ⛔ ЧЕГО НЕ ЛОВИТ: гонки уровня кадра и всё, что требует загруженного уровня. Для них нужен
// настоящий запуск, и это отдельная задача.
//
// Запуск (внутри контейнера, см. da_port/tools/asan/run_asan.sh):
//     da_asan_probe            — все сценарии
//     da_asan_probe strings    — только названные
//     da_asan_probe --selftest — НАМЕРЕННАЯ ошибка: доказать, что санитайзер взведён
// Своего stdafx у стенда нет намеренно: он должен зависеть только от публичных заголовков ядра,
// иначе перестанет быть проверкой ТОГО ЖЕ, чем пользуется остальной движок.
// ⚠️ Platform.hpp — ПЕРВЫМ и обязательно. Он задаёт XR_PLATFORM_*, а без них xrCore останавливается
// на `#error Define here lenght of the file paths strings for your platform`. Остальные цели
// получают его через предкомпилированный заголовок, у стенда такого нет.
#include "Common/Platform.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ⚠️ xrAPI ДО всех движковых заголовков: xrCDB тянет звук и фабрику рендера, а тем нужен
// глобальный GEnv. Поставить его позже недостаточно — цепочка включений начинается раньше.
#include "Include/xrAPI/xrAPI.h"

#include "xrCore/xrCore.h"
#include "xrCore/da_heap_guard.h"   // карантин освобождённой памяти: проверяем, что взведён
#include "xrCore/xrstring.h"        // shared_str
#include "xrCore/Threading/Lock.hpp" // Lock — правился сегодня, проверяем рекурсивность

// Реестр ALife: шаблон целиком в заголовках, отдельной библиотеки xrServerEntities нет —
// её исходники входят в xrGame, и линковать его ради этого не пришлось.
#include "xrGame/alife_abstract_registry.h"

// Материалы: вылеты #37 и #47 были ровно здесь. Отдельная библиотека xrMaterialSystem.
#include "xrMaterialSystem/GameMtlLib.h"

// Пирамида отсечения и пространственная база: то, вокруг чего крутился весь разбор кадра.
#include "xrCore/_fbox.h"   // Fbox для объёма пространственной базы
#include "xrCDB/Frustum.h"
#include "xrCDB/ISpatial.h"

namespace
{
int g_failures = 0;

void step(const char* name) { std::printf("== %s\n", name); std::fflush(stdout); }

// ---------------------------------------------------------------------------------------------
// Разделяемые строки: таблица с подсчётом ссылок. Мы её крепко трогали, когда резали мусор Lua
// (обёртки luabind, кэш по указателю), и любая ошибка учёта здесь — это освобождённая строка,
// которую кто-то ещё держит.
void probe_shared_str()
{
    step("shared_str: создание, копирование, сравнение, освобождение");

    std::vector<shared_str> keep;
    keep.reserve(4096);

    for (int i = 0; i < 4096; ++i)
    {
        string64 buf;
        std::snprintf(buf, sizeof(buf), "da_probe_item_%d", i % 512); // намеренные повторы
        shared_str s(buf);

        if (0 == (i % 3))
            keep.push_back(s);

        shared_str copy = s;      // +1 ссылка
        if (!(copy == s))         // сравнение по указателю таблицы
            ++g_failures;
    }

    // Отпускаем в обратном порядке: так вероятнее вскрыть ошибку порядка освобождения.
    while (!keep.empty())
        keep.pop_back();
}

// ---------------------------------------------------------------------------------------------
// Аллокатор и контейнеры. Под санитайзерами движок собирается в Debug, а там xrMemory работает на
// чистом malloc (USE_PURE_ALLOC) — именно поэтому ASan вообще видит наши блоки, а не один кусок
// mimalloc.
void probe_allocator()
{
    step("xr_vector и аллокатор: рост, перевыделение, освобождение");

    for (int round = 0; round < 64; ++round)
    {
        xr_vector<u32> v;
        for (u32 i = 0; i < 1024; ++i)
            v.push_back(i);

        // Перевыделение с копированием: любимое место для чтения за границей.
        v.insert(v.begin(), 777);
        v.erase(v.begin() + 3);
        v.resize(2048, 0);

        u64 sum = 0;
        for (u32 x : v)
            sum += x;
        if (0 == sum)
            ++g_failures;
    }

    // Выравненные выделения: отдельный путь в xrMemory.
    for (int i = 0; i < 256; ++i)
    {
        void* p = Memory.mem_alloc(1024 + i, 16);
        if (!p) { ++g_failures; continue; }
        std::memset(p, 0xA5, 1024 + i);
        Memory.mem_free(p);
    }
}

// ---------------------------------------------------------------------------------------------
// Замок. Правился сегодня же: Windows-ветка оставлена байт в байт, для прочих платформ добавлен
// путь на std::recursive_mutex. Рекурсивность обязана сохраниться — движок на неё опирается.
void probe_lock()
{
    step("Lock: рекурсивный захват и попытка захвата");

    Lock lock;
    lock.Enter();
    lock.Enter();       // ⚠️ рекурсивно: CRITICAL_SECTION так умеет, замена обязана тоже
    if (!lock.TryEnter())
        ++g_failures;
    lock.Leave();
    lock.Leave();
    lock.Leave();
}

// ---------------------------------------------------------------------------------------------
// Разбор строк и путей: сюда же смотрел cppcheck (LocatorAPI, invalidscanf).
void probe_strings()
{
    step("строковые утилиты: разбор, склейка, границы");

    for (int i = 0; i < 512; ++i)
    {
        string_path path;
        xr_strcpy(path, "gamedata\\configs\\");
        xr_strcat(path, "misc\\");

        string64 name;
        std::snprintf(name, sizeof(name), "item_%03d.ltx", i);
        xr_strcat(path, name);

        // Обрезка по границе буфера — классическое место переполнения на единицу. Источник
        // заведомо длиннее приёмника: обязано обрезать, а не писать за границу.
        char tiny[16];
        xr_strcpy(tiny, sizeof(tiny), path);
        if (xr_strlen(tiny) >= sizeof(tiny))
            ++g_failures;
    }
}

// ---------------------------------------------------------------------------------------------
// ⭐ Реестры ALife: терпимость к дублям.
//
// ЗАЧЕМ ИМЕННО ЭТО. В логах тестера 195 предупреждений трёх видов — «already registered at graph
// point», «already in the level registry», «already in the schedule registry». Это НАШИ защиты: до
// них те же случаи были вылетами при смене уровня. Значит дублирование идёт массово, и поведение
// реестра под ним надо проверять, а не считать доказанным.
//
// Реестр — шаблон CALifeAbstractRegistry поверх xr_map, целиком в заголовках. Поэтому его можно
// прогнать ЗДЕСЬ, не линкуя xrGame со скриптовым движком: сами исходники xrServerEntities входят в
// xrGame, отдельной библиотеки нет, и путь «подключить её» закрыт.
//
// ⛔ Проверяем ТОЛЬКО терпимый путь (no_assert = true). Без флага там THROW2, а THROW у нас
// БРОСАЕТ и никто не ловит — стенд просто умер бы, ничего не показав.
void probe_alife_registry()
{
    step("реестры ALife: дубли, удаление отсутствующего, поиск отсутствующего");

    CALifeAbstractRegistry<u32, u32> registry;

    // Наполняем.
    for (u32 i = 0; i < 512; ++i)
    {
        u32 value = i * 10;
        registry.add(i, value, true);
    }
    if (registry.objects().size() != 512)
        ++g_failures;

    // ⭐ Дубль: обязан быть пропущен, а ПЕРВОЕ значение — уцелеть. Если реестр начнёт затирать,
    // объект в игре молча сменит содержимое, и по логу это не увидеть.
    for (u32 i = 0; i < 512; ++i)
    {
        u32 other = 7777;
        registry.add(i, other, true);
    }
    if (registry.objects().size() != 512)
        ++g_failures;

    for (u32 i = 0; i < 512; ++i)
    {
        const u32* got = registry.object(i, true);
        if (!got || *got != i * 10)   // именно первое значение
            ++g_failures;
    }

    // Удаление отсутствующего и повторное удаление.
    for (u32 i = 1000; i < 1100; ++i)
        registry.remove(i, true);
    for (u32 i = 0; i < 512; ++i)
    {
        registry.remove(i, true);
        registry.remove(i, true);   // второй раз: уже нет
    }
    if (!registry.objects().empty())
        ++g_failures;

    // Поиск в пустом.
    if (registry.object(0, true))
        ++g_failures;

    // Цикл «наполнить-очистить»: здесь вскрывается освобождение узлов карты.
    for (int round = 0; round < 32; ++round)
    {
        CALifeAbstractRegistry<u32, u32> tmp;
        for (u32 i = 0; i < 256; ++i)
        {
            u32 v = i;
            tmp.add(i, v, true);
        }
        for (u32 i = 0; i < 256; ++i)
            tmp.remove(i, true);
    }
}

// ---------------------------------------------------------------------------------------------
// ⭐ Материалы по номеру: вылеты #37 и #47.
//
// Корень был в GetMaterialIdx — он возвращал РОВНО size() при ненайденном имени, а GetMaterialByIdx
// этим числом индексировал вектор. Классическое чтение за границей, причём тихое: номер выглядит
// правдоподобным. Сейчас первый отдаёт GAMEMTL_NONE_IDX, второй проверяет границу и отдаёт ноль.
//
// ⚠️ Библиотека НАМЕРЕННО пустая. Ровно этого случая (номер >= размера) и касается правка, а
// загружать настоящие материалы значило бы тянуть gamedata — то, от чего стенд и уходит.
void probe_materials()
{
    step("материалы: номер за границей, поиск несуществующего");

    // ⛔ Номер за границей проверяем ТОЛЬКО в релизной сборке, и это не лень, а находка.
    //
    // В GetMaterialByIdx перед нашим мягким отказом стоит VERIFY(idx < materials.size()). В Debug
    // он ЖИВОЙ и валит процесс раньше, чем дело дойдёт до отказа; в релизе он исчезает, и работает
    // наша ветка. То есть защита от вылета #37/#47 достижима только в релизе — а стенд собирается
    // в Debug, иначе санитайзер не увидит выделений (там xrMemory идёт на чистом malloc).
    //
    // Проверено прогоном: под Debug сюда прилетает «FATAL ERROR: idx < materials.size()».
#ifdef NDEBUG
    for (u16 idx : { u16(0), u16(1), u16(100), u16(0xFFFE), u16(0xFFFF) })
    {
        if (GMLib.GetMaterialByIdx(idx))
            ++g_failures;   // при пустой библиотеке обязан быть ноль, а не «что-то»
    }
#endif

    // Поиск по несуществующему имени и номеру: обязан вернуть признак «нет», а не size().
    const u16 by_name = GMLib.GetMaterialIdx("da_probe_no_such_material");
    if (by_name != u16(GAMEMTL_NONE_IDX))
        ++g_failures;

    const u16 by_id = GMLib.GetMaterialIdx(0x7FFFFFFF);
    if (by_id != u16(GAMEMTL_NONE_IDX))
        ++g_failures;

    // И сразу связка, которой падало: полученный номер уходит обратно в GetMaterialByIdx.
    // Та же оговорка про Debug — см. выше.
#ifdef NDEBUG
    if (GMLib.GetMaterialByIdx(by_name))
        ++g_failures;
#endif
}

// ---------------------------------------------------------------------------------------------
// Пирамида отсечения. Через неё проходит КАЖДЫЙ объект кадра (у нас их 354), а ошибка в маске не
// падает, а тихо теряет объекты — тот самый класс «молчаливых» дефектов рендера.
void probe_frustum()
{
    step("пирамида отсечения: сферы внутри, снаружи и на границе");

    Fmatrix proj;
    proj.build_projection(deg2rad(67.f), 1.777f, 0.2f, 500.f);

    CFrustum frustum;
    frustum.CreateFromMatrix(proj, FRUSTUM_P_ALL);

    int inside = 0, outside = 0;
    for (int i = 0; i < 4096; ++i)
    {
        Fvector c;
        c.set(float((i % 41) - 20), float(((i / 41) % 41) - 20), float(10 + (i % 200)));

        u32 mask = frustum.getMask();
        const auto r = frustum.testSphere(c, 1.0f, mask);
        if (fcvNone == r) ++outside; else ++inside;

        // Второй путь, которым идёт динамика: он дешевле и не трогает маску.
        (void)frustum.testSphere_dirty(c, 1.0f);
    }

    // Обе ветки обязаны встретиться: если всё внутри или всё снаружи — проверка ничего не проверила.
    if (0 == inside || 0 == outside)
        ++g_failures;
}

// ---------------------------------------------------------------------------------------------
// Пространственная база: вставка, удаление, запросы. Здесь живут q_box (общая выборка динамики для
// ламп) и обход дерева объектов, который мы сегодня мерили.
// Подставной пространственный объект: SpatialBase делает всю работу, нам остаётся четыре
// приведения. ⚠️ Конструктор требует ССЫЛКУ на базу — она же прописывается в spatial.space, и без
// неё spatial_register некуда вставлять.
class da_probe_spatial final : public SpatialBase
{
public:
    explicit da_probe_spatial(ISpatial_DB& space) : SpatialBase(space) {}

    IGameObject* dcast_GameObject() override { return nullptr; }
    Feel::Sound* dcast_FeelSound() override { return nullptr; }
    IRenderable* dcast_Renderable() override { return nullptr; }
    IRender_Light* dcast_Light() override { return nullptr; }
};

void probe_spatial_db()
{
    step("пространственная база: вставка, запрос, удаление, повторное удаление");

    ISpatial_DB db("da_probe");
    Fbox bb;
    bb.set(-256.f, -256.f, -256.f, 256.f, 256.f, 256.f);
    db.initialize(bb);

    std::vector<da_probe_spatial*> objects;
    objects.reserve(512);

    for (int i = 0; i < 512; ++i)
    {
        auto* o = new da_probe_spatial(db);
        SpatialData& sd = o->GetSpatialData();
        sd.type = STYPE_RENDERABLE;
        sd.sphere.P.set(float((i % 32) * 8 - 128), 0.f, float((i / 32) * 8 - 128));
        sd.sphere.R = 2.f;
        sd.node_ptr = nullptr;
        sd.sector_id = IRender_Sector::INVALID_SECTOR_ID;

        db.insert(o);
        objects.push_back(o);
    }

    // Запрос по объёму — то же, чем фаза света собирает динамику одним заходом.
    xr_vector<ISpatial*> found;
    Fvector center, size;
    center.set(0.f, 0.f, 0.f);
    size.set(64.f, 64.f, 64.f);
    db.q_box(found, 0, STYPE_RENDERABLE, center, size);
    if (found.empty())
        ++g_failures;   // в этом объёме объекты заведомо есть

    // Запрос, заведомо пустой: обязан вернуть пусто, а не мусор.
    center.set(10000.f, 10000.f, 10000.f);
    db.q_box(found, 0, STYPE_RENDERABLE, center, size);
    if (!found.empty())
        ++g_failures;

    // Удаление — и попытка удалить второй раз. Именно эта пара давала вылеты при выгрузке уровня.
    for (auto* o : objects)
    {
        db.remove(o);
        if (o->GetSpatialData().node_ptr)
            ++g_failures;   // после удаления узел обязан обнулиться
    }

    for (auto* o : objects)
        delete o;
    objects.clear();
}

// ---------------------------------------------------------------------------------------------
// ⭐ Карантин освобождённой памяти: доказать, что прибор взведён.
//
// Сценарий НАМЕРЕННО читает уже освобождённый блок. В обычной сборке это неопределённое поведение,
// и прочиталось бы что угодно; под карантином блок придержан за нами и заведомо жив — на этом
// свойстве вся затея и стоит, поэтому проверяем именно его.
//
// ⛔ Без DA_HEAP_GUARD=1 сценарий пропускает себя сам и говорит об этом вслух. Молча «пройти» здесь
// было бы худшим из исходов: отчёт сказал бы «чисто» там, где прибор попросту выключен.
void probe_heap_guard()
{
    step("карантин кучи: отравление, придержание, двойное освобождение");

    if (!da_heap_guard_enabled())
    {
        std::printf("   ПРОПУЩЕНО: DA_HEAP_GUARD не выставлена — проверять нечего\n");
        return;
    }

    // Больше метки карантина (16 байт) и меньше предела отравления (4 КБ) — то есть блок обязан
    // быть отравлен целиком.
    constexpr size_t N = 256;
    unsigned char* p = (unsigned char*)Memory.mem_alloc(N);
    std::memset(p, 0xA5, N);
    Memory.mem_free(p);

    // 1. Отрава на месте. Проверяем ВОСЬМИБАЙТОВЫМИ СЛОВАМИ, а не байтами: указатель читается
    //    словом, и значение имеет смысл именно как слово. Метка карантина и адрес блока лежат в
    //    ДВУХ ПОСЛЕДНИХ словах — их пропускаем, начало обязано быть чистой отравой.
    //
    // ⚠️ Прежняя проверка считала байты, равные 0xDD, и после перехода на канонический образец
    //    честно дала 150 из 240 — потому что в каждом слове таких байт пять из восьми. Устарела
    //    была ПРОВЕРКА, а не защита; байтовый счёт тут просто мерил не то.
    const u64* words = (const u64*)p;
    const size_t total_words = N / sizeof(u64);
    size_t poisoned = 0;
    for (size_t i = 0; i < total_words - 2; ++i)
        if (DA_HEAP_POISON_WORD == words[i])
            ++poisoned;

    // ⭐ Отдельно и в первую очередь — СМЕЩЕНИЕ 0. Там у объекта таблица виртуальных функций, и
    // `объект->метод()` после удаления это самое частое обращение к мертвецу вообще. Пока метка
    // карантина занимала это место, именно такой случай давал в логе нечитаемое ffffffffffffffff.
    if (DA_HEAP_POISON_WORD != words[0])
    {
        std::printf("   ✗ СМЕЩЕНИЕ 0 НЕ ОТРАВЛЕНО (%016llx): вызов метода мертвеца не опознается\n",
            (unsigned long long)words[0]);
        ++g_failures;
    }
    else
        std::printf("   ✓ смещение 0 (таблица виртуальных функций) отравлено\n");

    if (poisoned != total_words - 2)
    {
        std::printf("   ✗ ОТРАВЛЕНИЕ НЕ СРАБОТАЛО: %zu из %zu слов\n", poisoned, total_words - 2);
        ++g_failures;
    }
    else
        std::printf("   ✓ отравлено %zu из %zu слов образцом %016llx\n",
            poisoned, total_words - 2, (unsigned long long)DA_HEAP_POISON_WORD);

    // 2. Блок ПРИДЕРЖАН. Тысяча чужих выделений и освобождений подряд: если бы наш блок вернули
    //    куче сразу, любое из них легло бы поверх и отрава не уцелела бы. Это и есть та отсрочка,
    //    ради которой карантин существует, — без неё падение приезжает далеко от ошибки.
    for (int i = 0; i < 1000; ++i)
        Memory.mem_free(Memory.mem_alloc(64));

    if (0xDD != p[64])
    {
        std::printf("   ✗ БЛОК ПЕРЕИСПОЛЬЗОВАН: карантин не держит\n");
        ++g_failures;
    }
    else
        std::printf("   ✓ блок пережил 1000 чужих выделений: карантин держит\n");

    // 3. Двойное освобождение обязано быть ПОЙМАНО, а не испортить списки кучи молча.
    //
    // ⚠️ Проверяем счётчиком, а не сообщением в логе: у стенда лог не поднят, и находка ушла бы в
    // никуда. Именно так прибор и становится неотличим от исправного кода.
    const u64 before = da_heap_guard_double_frees();
    Memory.mem_free(p);
    const u64 after = da_heap_guard_double_frees();

    if (after != before + 1)
    {
        std::printf("   ✗ ДВОЙНОЕ ОСВОБОЖДЕНИЕ НЕ ПОЙМАНО: счётчик %llu -> %llu\n",
            (unsigned long long)before, (unsigned long long)after);
        ++g_failures;
    }
    else
        std::printf("   ✓ двойное освобождение поймано (счётчик %llu -> %llu)\n",
            (unsigned long long)before, (unsigned long long)after);

    da_heap_guard_stat();
}

// ---------------------------------------------------------------------------------------------
// ⭐ Самопроверка: НАМЕРЕННЫЙ выход за границу.
//
// Прибор, который молчит, неотличим от прибора, который ничего не нашёл. Эта ветка доказывает, что
// санитайзер взведён и печатает отчёт. Ровно та же мысль, что у da_crash_test для отчётов о вылетах.
//
// ⛔ Если этот сценарий НЕ дал отчёта ASan — значит сборка собрана без санитайзеров, и всем прочим
// «всё чисто» верить нельзя.
void selftest_deliberate_overflow()
{
    step("САМОПРОВЕРКА: намеренный выход за границу (ожидается отчёт ASan)");

    volatile int* p = new int[4];
    p[7] = 1;                       // <- вот оно
    const int v = p[7];
    delete[] p;
    std::printf("   прочитано %d (если ASan молчит — санитайзера в сборке НЕТ)\n", v);
}
} // namespace

int main(int argc, char** argv)
{
    // Файловую систему не поднимаем: стенду нужны примитивы ядра, а не gamedata. Ей нужен fsgame.ltx
    // и весь набор данных, а это ровно та зависимость, ради ухода от которой стенд и делался.
    Core.Initialize("da_asan_probe", nullptr, false);

    const char* only = (argc > 1) ? argv[1] : nullptr;
    const auto want = [&](const char* name) { return !only || 0 == std::strcmp(only, name); };

    std::printf("== прогонный стенд: %s\n", only ? only : "все сценарии");

    if (only && 0 == std::strcmp(only, "--selftest"))
    {
        selftest_deliberate_overflow();
    }
    else
    {
        if (want("strings")) { probe_shared_str(); probe_strings(); }
        if (want("alloc"))   probe_allocator();
        if (want("lock"))    probe_lock();
        if (want("alife"))   probe_alife_registry();
        if (want("mtl"))     probe_materials();
        if (want("frustum")) probe_frustum();
        if (want("spatial")) probe_spatial_db();
        if (want("guard"))   probe_heap_guard();
    }

    Core._destroy();

    std::printf("== готово, несоответствий: %d\n", g_failures);
    return g_failures ? 1 : 0;
}
