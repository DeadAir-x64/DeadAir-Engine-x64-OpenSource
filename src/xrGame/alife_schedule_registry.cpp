////////////////////////////////////////////////////////////////////////////
//	Module 		: alife_schedule_registry.сзз
//	Created 	: 15.01.2003
//  Modified 	: 12.05.2004
//	Author		: Dmitriy Iassenev
//	Description : ALife schedule registry
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "alife_schedule_registry.h"
#include "ai_space.h"                 // [DA_PORT] da_alife_object_registered
#include "alife_simulator.h"          // [DA_PORT] da_alife_object_registered
#include "alife_object_registry.h"    // [DA_PORT] da_alife_object_registered
#include "xrEngine/Device.h" // [DA_PORT] ps_da_seq_trap - общий выключатель диагностики задач кадра

// [DA_PORT] Накопление по секциям объектов ALife, разбор в alife_schedule_registry.h.
namespace
{
struct da_alife_stat
{
    u64 calls{};
    double total_ms{};
    float max_ms{};
};

xr_map<shared_str, da_alife_stat> g_da_alife_stats;
} // namespace

da_alife_update_probe::da_alife_update_probe(CSE_ALifeSchedulable* o)
    : obj(o), armed(ps_da_seq_trap > 0.f)
{
    if (armed)
        timer.Start();
}

da_alife_update_probe::~da_alife_update_probe()
{
    if (!armed || !obj)
        return;

    const CSE_Abstract* base = obj->base();
    if (!base)
        return;

    const float ms = timer.GetElapsed_sec() * 1000.f;
    da_alife_stat& st = g_da_alife_stats[base->s_name];
    ++st.calls;
    st.total_ms += ms;
    if (ms > st.max_ms)
        st.max_ms = ms;
}

void da_alife_dump_update_stats()
{
    if (g_da_alife_stats.empty())
        return;

    xr_vector<std::pair<shared_str, da_alife_stat>> rows;
    rows.reserve(g_da_alife_stats.size());
    for (const auto& it : g_da_alife_stats)
        rows.push_back(it);

    std::sort(rows.begin(), rows.end(),
        [](const auto& l, const auto& r) { return l.second.total_ms > r.second.total_ms; });

    double grand = 0.0;
    u64 calls = 0;
    for (const auto& r : rows)
    {
        grand += r.second.total_ms;
        calls += r.second.calls;
    }

    Msg("~ [DA_ALIFE] ---- обновление ALife по секциям: %u секций, %llu обновлений, %.1f мс ----",
        (u32)rows.size(), (unsigned long long)calls, grand);
    Msg("~ [DA_ALIFE] %-10s %10s %9s %9s  %s", "обновлений", "всего мс", "среднее", "максимум",
        "секция");
    for (const auto& r : rows)
    {
        const da_alife_stat& st = r.second;
        if (st.total_ms < 0.5)
            continue; // мелочь не печатаем: секций сотни, а интересны единицы
        Msg("~ [DA_ALIFE] %-10llu %10.2f %9.4f %9.3f  %s", (unsigned long long)st.calls,
            st.total_ms, st.total_ms / double(st.calls ? st.calls : 1), st.max_ms,
            r.first.c_str());
    }
    Msg("~ [DA_ALIFE] ---- конец ----");
    g_da_alife_stats.clear();
}

CALifeScheduleRegistry::~CALifeScheduleRegistry() {}
void CALifeScheduleRegistry::add(CSE_ALifeDynamicObject* object)
{
    CSE_ALifeSchedulable* schedulable = smart_cast<CSE_ALifeSchedulable*>(object);
    if (!schedulable)
        return;

    if (!schedulable->need_update(object))
        return;

    // [DA_PORT] The same duplicate tolerance the graph and level registries needed.
    //
    // register_object() feeds five registries in a row and this is the third. ALife deliberately leaves
    // objects half-registered - see the group-detach path in alife_group_abstract.cpp, which registers a
    // member and then removes it from the graph point while explicitly keeping it in the level map - so
    // a second registration is a state the engine produces on purpose, not a fault.
    //
    // Without this the class of crash already fixed twice today would simply resurface here: this is a
    // CSafeMapIterator like the level registry, so a duplicate asserts in the very same place,
    // safe_map_iterator_inline.h. The other two registries in that chain (smart terrains, groups) are
    // already immune - their check is a VERIFY, which Release compiles out, and the map insert behind it
    // does nothing on a duplicate key.
    if (objects().find(object->ID) != objects().end())
    {
        Msg("! [DA_PORT] ALife: object [%s] section[%s] id[%d] already in the schedule registry - "
            "skipping (registered without being unregistered first)",
            object->name_replace(), object->s_name.c_str(), object->ID);
        return;
    }

    inherited::add(object->ID, schedulable);
}

void CALifeScheduleRegistry::remove(CSE_ALifeDynamicObject* object, bool no_assert)
{
    CSE_ALifeSchedulable* schedulable = smart_cast<CSE_ALifeSchedulable*>(object);
    if (!schedulable)
        return;

    inherited::remove(object->ID, no_assert || !schedulable->need_update(object));
}

// [DA_PORT] Числится ли объект в реестре ALife — см. CUpdatePredicate в заголовке.
//
// no_assert обязателен: отсутствие записи здесь не ошибка, а ровно тот случай, ради которого
// проверка и заведена. Симулятора может не быть вовсе (главное меню, выгрузка) — тогда обновлять
// тем более нечего.
bool da_alife_object_registered(u16 id, const CSE_ALifeSchedulable* scheduled)
{
    const CALifeSimulator* alife = ai().get_alife();
    if (!alife)
        return false;

    CSE_ALifeDynamicObject* object = alife->objects().object(id, true);

    // [DA_PORT] Сверяем ЛИЧНОСТЬ объекта, а не номер.
    //
    // Первая версия проверки спрашивала только «числится ли такой номер» — и этого не хватило.
    // Номера у нас уходят в общий пул при живом объекте (скриптовый release для онлайн-объекта его
    // не уничтожает) и раздаются заново: реестр честно отвечает «номер занят», а в расписании при
    // этом лежит указатель на СТАРЫЙ объект, чья память уже освобождена. Проверка проходила, и
    // планировщик звал update() по мёртвому адресу.
    //
    // Так выглядел вылет 07.08: `CSE_ALifeOnlineOfflineGroup::commander_id()`, чтение по мусорному
    // адресу, стек — ALife → обёртка скриптового отряда → Lua → обратно в C++. Сама commander_id
    // безупречна (проверяет пустоту списка), падал доступ по `this`.
    //
    // Сравнение адресов законно: слева — живой объект из реестра, приведённый к тому же подтипу,
    // что хранится в расписании (множественное наследование сдвигает указатель, поэтому приводим,
    // а не сравниваем как есть). Справа — то, что планировщик собирается вызвать. Разошлись —
    // значит номер переиспользован, и трогать его нельзя.
    if (object && smart_cast<CSE_ALifeSchedulable*>(object) == scheduled)
        return true;

    // [DA_PORT] Считаем отказы: молчаливая защита неотличима от неработающей.
    //
    // Если это число растёт — значит в расписании действительно лежат номера, которых в реестре
    // объектов уже нет, и без проверки планировщик звал бы update() по мёртвой памяти. Если оно
    // остаётся нулём, а падения ушли — причина была в другом, и это надо знать, а не додумывать.
    extern int ps_da_registry_log;
    if (ps_da_registry_log)
    {
        static u32 s_da_skipped = 0;
        ++s_da_skipped;
        if (s_da_skipped <= 5 || (s_da_skipped % 100) == 0)
            Msg("~ [DA_REG] расписание: номер [%d] %s — обновление пропущено, всего %u", id,
                object ? "занят ДРУГИМ объектом (номер переиспользован)" : "не числится в реестре",
                s_da_skipped);
    }

    return false;
}
