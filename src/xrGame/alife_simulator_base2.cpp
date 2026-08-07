////////////////////////////////////////////////////////////////////////////
//	Module 		: alife_simulator_base2.cpp
//	Created 	: 25.12.2002
//  Modified 	: 12.05.2004
//	Author		: Dmitriy Iassenev
//	Description : ALife Simulator base class
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "alife_simulator_base.h"
#include "relation_registry.h"
#include "alife_registry_wrappers.h"
#include "xrServer_Objects_ALife_Items.h"
#include "alife_graph_registry.h"
#include "alife_object_registry.h"
#include "alife_story_registry.h"
#include "alife_schedule_registry.h"
#include "alife_smart_terrain_registry.h"
#include "alife_group_registry.h"

using namespace ALife;

void CALifeSimulatorBase::register_object(CSE_ALifeDynamicObject* object, bool add_object)
{
    object->on_before_register();

    // [DA_PORT] Адрес снова живой: аллокатор мог отдать под этот объект память недавно удалённого.
    // Без этого сторож висячих выдач ругался бы на совершенно здоровый объект, просто занявший
    // чужое место. См. da_check_dangling в alife_simulator_script.cpp.
    {
        extern void da_watch_reused(const void* object);
        da_watch_reused(object);
    }

    if (add_object)
        objects().add(object);

    graph().update(object);
    scheduled().add(object);
    story_objects().add(object->m_story_id, object);
    smart_terrains().add(object);
    groups().add(object);

    setup_simulator(object);

    CSE_ALifeInventoryItem* item = smart_cast<CSE_ALifeInventoryItem*>(object);
    if (item && item->attached())
    {
        CSE_ALifeDynamicObject* II = objects().object(item->base()->ID_Parent, true);

        // [DA_PORT] Владельца может не быть, и теперь это возвращается ЧЕСТНЫМ нулём.
        //
        // Раньше реестр отдавал висячий указатель, и падение приходило позже и в другом месте;
        // после того как object() стал проверять метку жизни, здесь появляется nullptr — а ниже
        // стоит push_back по нему. Проверка в DEBUG была, в релизе не было ничего.
        //
        // Предмет без владельца — не катастрофа: он останется незакреплённым, что честнее падения.
        if (!II)
        {
            Msg("! [DA_PORT] владелец предмета не найден: предмет [%s] id [%d], владелец [%d] — привязка пропущена",
                object->name_replace(), object->ID, item->base()->ID_Parent);
        }
        else
        {

#ifdef DEBUG
        if (std::find(II->children.begin(), II->children.end(), item->base()->ID) != II->children.end())
        {
            Msg("[LSS] Specified item [%s][%d] is already attached to the specified object [%s][%d]",
                item->base()->name_replace(), item->base()->ID, II->name_replace(), II->ID);
            FATAL("[LSS] Cannot recover from the previous error!");
        }
#endif

            II->children.push_back(item->base()->ID);
            II->attach(item, true, false);
        }
    }

    if (can_register_objects())
        object->on_register();
}

void CALifeSimulatorBase::unregister_object(CSE_ALifeDynamicObject* object, bool alife_query)
{
    object->on_unregister();

    CSE_ALifeInventoryItem* item = smart_cast<CSE_ALifeInventoryItem*>(object);
    // [DA_PORT] Владелец берётся ОДИН раз и проверяется: здесь было два обращения к реестру
    // подряд, оба с немедленным разыменованием — а реестр теперь честно отвечает нулём, если
    // объект уже разрушен (метка жизни). Нет владельца — отцеплять не от чего.
    if (item && item->attached())
    {
        CSE_ALifeDynamicObject* parent = objects().object(item->base()->ID_Parent, true);
        if (parent)
            graph().detach(*parent, item, parent->m_tGraphID, alife_query);
    }

    // [DA_PORT] no_assert по той же причине, что и у сюжетного реестра ниже: умолчание false, а
    // внутри THROW2, который никто не ловит. Объект мог быть снят раньше — release рекурсивен и
    // доходит до одного и того же ребёнка и по рекурсии, и по внешнему обходу.
    objects().remove(object->ID, true);

    // [DA_PORT] no_assert: отсутствие сюжетной записи здесь — норма, а не повод падать.
    //
    // Умолчание у remove — no_assert = false, а внутри при ненайденной записи стоит THROW(false).
    // Бросок никто не ловит, то есть «Unexpected application termination» на ровном месте. Записи
    // может не быть по двум причинам сразу: объект мог быть снят раньше (release рекурсивен и
    // доходит до одного и того же ребёнка дважды), и m_story_id мог смениться уже после
    // регистрации — тогда ищется один номер, а лежит запись под другим.
    story_objects().remove(object->m_story_id, true);
    smart_terrains().remove(object);
    groups().remove(object);

    // [DA_PORT] С расписания снимаем ВСЕГДА, а не только офлайн-объекты.
    //
    // Было: `scheduled().remove` стоял внутри ветки `if (!m_bOnline)`. Рассуждение за этим понятное —
    // в расписании лежат офлайн-объекты (add_offline их кладёт, add_online убирает), значит у
    // онлайнового там записи нет. Но это допущение, а не факт, и для отрядов оно ломается:
    // CSE_ALifeOnlineOfflineGroup переключается между состояниями, и порядок «стал онлайн, потом
    // освобождён» оставляет запись в расписании висеть на уничтоженном объекте.
    //
    // Дальше CALifeScheduleRegistry::update обходит расписание и зовёт update() у трупа. Через
    // обёртку это уходит в Lua, скрипт отряда зовёт обратно commander_id() — и падение приходит
    // оттуда, за три слоя от настоящей ошибки:
    //   CSE_ALifeOnlineOfflineGroup::commander_id(), чтение по адресу ffffffffffffffff
    //
    // Снятие с расписания у мёртвого объекта безвредно в любом случае: ему там делать нечего
    // независимо от того, был он онлайн или нет. no_assert — потому что записи может и не быть,
    // и это норма, а не ошибка.
    if (scheduled().object(object->ID, true))
        scheduled().remove(object, true);

    if (!object->m_bOnline)
        graph().remove(object, object->m_tGraphID);
    else if (object->ID_Parent == 0xffff)
    {
        //			if (object->used_ai_locations())
        graph().level().remove(object, !object->used_ai_locations());
    }
}

void CALifeSimulatorBase::on_death(CSE_Abstract* killed, CSE_Abstract* killer)
{
    typedef CSE_ALifeOnlineOfflineGroup::MEMBER GROUP_MEMBER;

    CSE_ALifeCreatureAbstract* creature = smart_cast<CSE_ALifeCreatureAbstract*>(killed);
    if (creature)
        creature->on_death(killer);

    GROUP_MEMBER* member = smart_cast<GROUP_MEMBER*>(killed);
    if (!member)
        return;

    if (member->m_group_id == 0xffff)
        return;

    // [DA_PORT] Группа могла уйти раньше бойца — см. object_safe в alife_group_registry.cpp.
    if (CSE_ALifeOnlineOfflineGroup* group = groups().object_safe(member->m_group_id))
        group->notify_on_member_death(member);
}
