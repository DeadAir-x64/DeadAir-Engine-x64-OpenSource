////////////////////////////////////////////////////////////////////////////
//  Module      : alife_simulator_base.cpp
//  Created     : 25.12.2002
//  Modified    : 12.05.2004
//  Author      : Dmitriy Iassenev
//  Description : ALife Simulator base class
////////////////////////////////////////////////////////////////////////////

#include "pch_script.h"
#include "alife_simulator_base.h"
#include "alife_simulator_header.h"
#include "alife_time_manager.h"
#include "alife_spawn_registry.h"
#include "alife_object_registry.h"
#include "alife_graph_registry.h"
#include "alife_schedule_registry.h"
#include "alife_story_registry.h"
#include "alife_smart_terrain_registry.h"
#include "alife_group_registry.h"
#include "alife_registry_container.h"
#include "xrServer.h"
#include "xrAICore/Navigation/level_graph.h"
#include "inventory_upgrade_manager.h"
#include "Level.h"

#ifdef DEBUG
#include "alife_simulator_base_inline.h"
#endif

using namespace ALife;

CALifeSimulatorBase::CALifeSimulatorBase(IPureServer* server, LPCSTR section)
{
    m_server = server;
    m_initialized = false;
    m_header = 0;
    m_time_manager = 0;
    m_spawns = 0;
    m_objects = 0;
    m_graph_objects = 0;
    m_scheduled = 0;
    m_story_objects = 0;
    m_smart_terrains = 0;
    m_groups = 0;
    m_registry_container = 0;
    m_upgrade_manager = 0;

    random().seed(u32(CPU::QPC() & 0xffffffff));
    m_can_register_objects = true;
}

CALifeSimulatorBase::~CALifeSimulatorBase() { VERIFY(!m_initialized); }
void CALifeSimulatorBase::destroy() { unload(); }
void CALifeSimulatorBase::unload()
{
    xr_delete(m_objects);
    xr_delete(m_header);
    xr_delete(m_time_manager);
    xr_delete(m_spawns);
    xr_delete(m_graph_objects);
    xr_delete(m_scheduled);
    xr_delete(m_story_objects);
    xr_delete(m_smart_terrains);
    xr_delete(m_groups);
    xr_delete(m_registry_container);
    xr_delete(m_upgrade_manager);
    m_initialized = false;

    if (g_pGameLevel)
        Level().OnAlifeSimulatorUnLoaded();
}

void CALifeSimulatorBase::reload(LPCSTR section)
{
    m_header = xr_new<CALifeSimulatorHeader>(section);
    m_time_manager = xr_new<CALifeTimeManager>(section);
    m_spawns = xr_new<CALifeSpawnRegistry>(section);
    m_objects = xr_new<CALifeObjectRegistry>(section);
    m_graph_objects = xr_new<CALifeGraphRegistry>();
    m_scheduled = xr_new<CALifeScheduleRegistry>();
    m_story_objects = xr_new<CALifeStoryRegistry>();
    m_smart_terrains = xr_new<CALifeSmartTerrainRegistry>();
    m_groups = xr_new<CALifeGroupRegistry>();
    m_registry_container = xr_new<CALifeRegistryContainer>();
    m_upgrade_manager = xr_new<inventory::upgrade::Manager>();
    m_initialized = true;
}

CSE_Abstract* CALifeSimulatorBase::spawn_item(LPCSTR section, const Fvector& position, u32 level_vertex_id,
    GameGraph::_GRAPH_ID game_vertex_id, u16 parent_id, bool registration)
{
    // [DA_PORT] Scripted spawns (alife():create) reach here with whatever the script passed.
    // A null/unknown section or an out-of-range game vertex used to AV deep inside (raw stack
    // crash at a trader) - reject them loudly and return nullptr so Lua just gets nil.
    if (!section || !*section || !pSettings->section_exist(section))
    {
        Msg("! [DA_PORT] spawn_item: invalid section '%s' - spawn rejected", section ? section : "(null)");
        FlushLog();
        return nullptr;
    }
    if (!ai().game_graph().valid_vertex_id(game_vertex_id))
    {
        Msg("! [DA_PORT] spawn_item: invalid game_vertex_id %u for section '%s' - spawn rejected",
            u32(game_vertex_id), section);
        FlushLog();
        return nullptr;
    }

    // [DA_PORT] Неизвестный движку класс — отказ, а не падение (приём из Dead Air Refined).
    //
    // Здесь стоял R_ASSERT3, и он работает в релизе: опечатка в секции у скрипта мода роняла игру
    // целиком, с фатальным окном вместо строки в логе. Секция при этом СУЩЕСТВУЕТ (её наличие
    // проверено выше) — не найден класс в фабрике, то есть данные ссылаются на сущность, которой в
    // этой сборке движка нет.
    //
    // Скрипту возвращается nil, и это ровно то, чего он ждёт от неудачного create: все вызывающие
    // ниже по файлу и в alife_simulator_script.cpp результат проверяют. Имя секции в логе называет
    // виновника сразу, а стек Lua — место вызова.
    CSE_Abstract* abstract = F_entity_Create(section, true);
    if (!abstract)
    {
        Msg("! [DA_PORT] spawn_item: для секции '%s' не нашлось серверного класса — спавн отклонён",
            section);

        // g_da_lua_stack_printer объявлен в xrCore/xrDebug.h — свой extern здесь дал бы конфликт
        // с импортом из DLL.
        if (g_da_lua_stack_printer)
            g_da_lua_stack_printer();

        FlushLog();
        return nullptr;
    }

    abstract->s_name = section;
    //. abstract->s_gameid          = u8(GAME_SINGLE);
    abstract->s_RP = 0xff;
    abstract->ID = server().PerformIDgen(0xffff);
    abstract->ID_Parent = parent_id;
    abstract->ID_Phantom = 0xffff;
    abstract->o_Position = position;
    abstract->m_wVersion = SPAWN_VERSION;

    string256 s_name_replace;
    xr_strcpy(s_name_replace, abstract->s_name.c_str());
    if (abstract->ID < 1000)
        xr_strcat(s_name_replace, "0");
    if (abstract->ID < 100)
        xr_strcat(s_name_replace, "0");
    if (abstract->ID < 10)
        xr_strcat(s_name_replace, "0");
    string16 S1;
    xr_strcat(s_name_replace, xr_itoa(abstract->ID, S1, 10));
    abstract->set_name_replace(s_name_replace);

    CSE_ALifeDynamicObject* dynamic_object = smart_cast<CSE_ALifeDynamicObject*>(abstract);
    VERIFY(dynamic_object);
    // [DA_PORT] Release strips the VERIFY and the very next field write AV'd on null (found by
    // disassembling a live crash): a section whose server class is not CSE_ALifeDynamicObject
    // in this engine build. Reject loudly instead - the log names the section to fix its class.
    if (!dynamic_object)
    {
        // [DA_PORT] Печатаем и КЛАСС из конфига: без него сообщение называет симптом, но не даёт
        // ничего для починки. Секция `wpn_rpg7_missile` спавнится по авторскому замыслу в начале
        // игры, то есть отказ здесь режет штатный контент, и надо знать, какой именно класс у нас
        // не опознаётся.
        Msg("! [DA_PORT] spawn_item: секция '%s' (класс '%s') не даёт серверный ALife-объект - спавн "
            "отклонён",
            section,
            pSettings->line_exist(section, "class") ? pSettings->r_string(section, "class") : "нет ключа");
        FlushLog();
        server().FreeID(abstract->ID, 0);
        F_entity_Destroy(abstract);
        return nullptr;
    }

    //оружие спавним с полным магазинои
    CSE_ALifeItemWeapon* weapon = smart_cast<CSE_ALifeItemWeapon*>(dynamic_object);
    if (weapon)
        weapon->a_elapsed = weapon->get_ammo_magsize();

    dynamic_object->m_tNodeID = level_vertex_id;
    dynamic_object->m_tGraphID = game_vertex_id;
    dynamic_object->m_tSpawnID = u16(-1);

    if (registration)
        register_object(dynamic_object, true);

    dynamic_object->spawn_supplies();
    dynamic_object->on_spawn();

    //  Msg                         ("LSS : SPAWN : [%s],[%s], level
    //%s",*dynamic_object->s_name,dynamic_object->name_replace(),*ai().game_graph().header().level(ai().game_graph().vertex(dynamic_object->m_tGraphID)->level_id()).name());
    return (dynamic_object);
}

CSE_Abstract* CALifeSimulatorBase::create(CSE_ALifeGroupAbstract* tpALifeGroupAbstract, CSE_ALifeDynamicObject* j)
{
    NET_Packet tNetPacket;
    LPCSTR S = pSettings->r_string(tpALifeGroupAbstract->base()->s_name, "monster_section");
    CSE_Abstract* l_tpAbstract = F_entity_Create(S);
    R_ASSERT2(l_tpAbstract, "Can't create entity.");
    CSE_ALifeDynamicObject* k = smart_cast<CSE_ALifeDynamicObject*>(l_tpAbstract);
    R_ASSERT2(k, "Non-ALife object in the 'game.spawn'");

    j->Spawn_Write(tNetPacket, TRUE);
    k->Spawn_Read(tNetPacket);
    tNetPacket.w_begin(M_UPDATE);
    j->UPDATE_Write(tNetPacket);
    u16 id;
    tNetPacket.r_begin(id);
    k->UPDATE_Read(tNetPacket);
    k->s_name = S;
    k->m_tSpawnID = j->m_tSpawnID;
    k->ID = server().PerformIDgen(0xffff);
    k->m_bDirectControl = false;
    k->m_bALifeControl = true;

    string256 s_name_replace;
    xr_strcpy(s_name_replace, k->s_name.c_str());
    if (k->ID < 1000)
        xr_strcat(s_name_replace, "0");
    if (k->ID < 100)
        xr_strcat(s_name_replace, "0");
    if (k->ID < 10)
        xr_strcat(s_name_replace, "0");
    string16 S1;
    xr_strcat(s_name_replace, xr_itoa(k->ID, S1, 10));
    k->set_name_replace(s_name_replace);

    register_object(k, true);
    k->spawn_supplies();
    k->on_spawn();
    return (k);
}

void CALifeSimulatorBase::create(CSE_ALifeDynamicObject*& i, CSE_ALifeDynamicObject* j, const _SPAWN_ID& tSpawnID)
{
    CSE_Abstract* tpSE_Abstract = F_entity_Create(j->s_name.c_str());
    R_ASSERT3(tpSE_Abstract, "Cannot find item with section", j->s_name.c_str());
    i = smart_cast<CSE_ALifeDynamicObject*>(tpSE_Abstract);
    R_ASSERT2(i, "Non-ALife object in the 'game.spawn'");

    NET_Packet tNetPacket;
    j->Spawn_Write(tNetPacket, TRUE);
    i->Spawn_Read(tNetPacket);
    tNetPacket.w_begin(M_UPDATE);
    j->UPDATE_Write(tNetPacket);
    u16 id;
    tNetPacket.r_begin(id);
    i->UPDATE_Read(tNetPacket);

    R_ASSERT3(!(i->used_ai_locations()) || (i->m_tNodeID != u32(-1)), "Invalid vertex for object ", i->name_replace());

    i->m_tSpawnID = tSpawnID;
    if (!graph().actor() && smart_cast<CSE_ALifeCreatureActor*>(i))
        i->ID = 0;
    else
        i->ID = server().PerformIDgen(0xffff);

    register_object(i, true);
    i->m_bALifeControl = true;

    CSE_ALifeMonsterAbstract* monster = smart_cast<CSE_ALifeMonsterAbstract*>(i);
    if (monster)
        graph().assign(monster);

    CSE_ALifeGroupAbstract* group = smart_cast<CSE_ALifeGroupAbstract*>(i);
    if (group)
    {
        group->m_tpMembers.resize(group->m_wCount);
        OBJECT_IT I = group->m_tpMembers.begin();
        OBJECT_IT E = group->m_tpMembers.end();
        for (; I != E; ++I)
        {
            CSE_Abstract* object = create(group, j);
            *I = object->ID;
        }
    }
    else
        i->spawn_supplies();

    i->on_spawn();
}

void CALifeSimulatorBase::create(CSE_ALifeObject* object)
{
    CSE_ALifeDynamicObject* dynamic_object = smart_cast<CSE_ALifeDynamicObject*>(object);
    if (!dynamic_object)
        return;

    if (!dynamic_object->can_save())
    {
        dynamic_object->m_bALifeControl = false;
        return;
    }
    VERIFY(dynamic_object->m_bOnline);

#ifdef DEBUG
//  Msg                         ("Creating object from client spawn
//[%d][%d][%s][%s]",dynamic_object->ID,dynamic_object->ID_Parent,dynamic_object->name(),dynamic_object->name_replace());
#endif

    if (0xffff != dynamic_object->ID_Parent)
    {
        u16 id = dynamic_object->ID_Parent;
        CSE_ALifeDynamicObject* parent = objects().object(id, true);
        // [DA_PORT] An object spawned into a parent that is not registered used to end the
        // session here (the registry throws and nobody catches it). Register it standalone
        // instead of losing the level load over one object.
        if (!parent)
        {
            Msg("! [DA] create: object [%d][%s] has unregistered parent [%d]", dynamic_object->ID,
                dynamic_object->name_replace(), id);
            register_object(dynamic_object, true);
            return;
        }
        dynamic_object->m_tGraphID = parent->m_tGraphID;
        dynamic_object->o_Position = parent->o_Position;
        dynamic_object->m_tNodeID = parent->m_tNodeID;
        dynamic_object->ID_Parent = 0xffff;
        register_object(dynamic_object, true);
        dynamic_object->ID_Parent = id;
    }
    else
        register_object(dynamic_object, true);
}

void CALifeSimulatorBase::release(CSE_Abstract* abstract, bool alife_query)
{
#ifdef DEBUG
    if (psAI_Flags.test(aiALife))
    {
        Msg("[LSS] Releasing object [%s][%s][%d][%x]", abstract->name_replace(), abstract->s_name.c_str(), abstract->ID,
            smart_cast<void*>(abstract));
    }
#endif
    // [DA_PORT] Освобождение обязано переживать повторный заход, а не падать на нём.
    //
    // Было: object(id) с проверкой только через VERIFY. В релизной сборке VERIFY разворачивается в
    // пустоту, а сам поиск при ненайденном объекте делает THROW2 -- то есть бросает исключение,
    // которое здесь никто не ловит. Обе «проверки» вместе не проверяют ничего.
    //
    // Повторный заход тут обычное дело: освобождение РЕКУРСИВНОЕ (объект тянет за собой детей), и до
    // одного и того же ребёнка добираются и рекурсия, и внешний проход по реестру. Второй раз
    // `abstract` указывает на уже освобождённую память, а любой виртуальный вызов по ней -- это
    // переход по нулевому адресу: аллокатор обнуляет указатель на таблицу методов. Ровно так и
    // выглядело падение: «исполнение по адресу 0000000000000000» с пустым стеком.
    //
    // Проявилось после того, как включили уборку трупов по таймеру: до этого путь удаления в моде не
    // исполнялся НИКОГДА (интервал стоял 65535 часов), и мина лежала невзведённой.
    //
    // Ищем с no_assert: отсутствие объекта здесь -- не ошибка, а «уже убрали».
    extern ENGINE_API int ps_da_alife_release_log;
    if (ps_da_alife_release_log)
        Msg("~ [DA_REL] вход: id [%d]", abstract->ID);

    CSE_ALifeDynamicObject* object = objects().object(abstract->ID, true);
    if (!object)
    {
        // Не молча: если это начнёт случаться часто, значит что-то освобождает объекты дважды по
        // другой причине, и знать об этом надо.
        Msg("~ [DA_PORT] ALife: объект [%d] уже отсутствует в реестре, повторное освобождение пропущено",
            abstract->ID);
        return;
    }

    if (!object->children.empty())
    {
        u32 children_count = object->children.size();
        u32 bytes = children_count * sizeof(ALife::_OBJECT_ID);
        ALife::_OBJECT_ID* children = (ALife::_OBJECT_ID*)xr_alloca(bytes);
        CopyMemory(children, &*object->children.begin(), bytes);

        ALife::_OBJECT_ID* I = children;
        ALife::_OBJECT_ID* E = children + children_count;
        for (; I != E; ++I)
        {
            CSE_ALifeDynamicObject* child = objects().object(*I, true);
            if (!child)
                continue;

            release(child, alife_query);
        }
    }

    if (ps_da_alife_release_log)
        Msg("~ [DA_REL] снимаю: id [%d] секция [%s] детей [%u]", object->ID, object->s_name.c_str(),
            u32(object->children.size()));

    unregister_object(object, alife_query);

    object->m_bALifeControl = false;

    if (alife_query)
        server().entity_Destroy(abstract);

    if (ps_da_alife_release_log)
        Msg("~ [DA_REL] снят");
}

void CALifeSimulatorBase::append_item_vector(OBJECT_VECTOR& tObjectVector, ITEM_P_VECTOR& tItemList)
{
    OBJECT_IT I = tObjectVector.begin();
    OBJECT_IT E = tObjectVector.end();
    for (; I != E; ++I)
    {
        CSE_ALifeInventoryItem* l_tpALifeInventoryItem = smart_cast<CSE_ALifeInventoryItem*>(objects().object(*I));
        if (l_tpALifeInventoryItem)
            tItemList.push_back(l_tpALifeInventoryItem);
    }
}

void CALifeSimulatorBase::assign_death_position(CSE_ALifeCreatureAbstract* tpALifeCreatureAbstract,
    GameGraph::_GRAPH_ID tGraphID, CSE_ALifeSchedulable* tpALifeSchedulable)
{
    tpALifeCreatureAbstract->set_health(0.f);

    if (tpALifeSchedulable)
    {
        CSE_ALifeAnomalousZone* l_tpALifeAnomalousZone = smart_cast<CSE_ALifeAnomalousZone*>(tpALifeSchedulable);
        if (l_tpALifeAnomalousZone)
        {
            spawns().assign_artefact_position(l_tpALifeAnomalousZone, tpALifeCreatureAbstract);
            CSE_ALifeMonsterAbstract* l_tpALifeMonsterAbstract =
                smart_cast<CSE_ALifeMonsterAbstract*>(tpALifeCreatureAbstract);
            if (l_tpALifeMonsterAbstract)
                l_tpALifeMonsterAbstract->m_tPrevGraphID = l_tpALifeMonsterAbstract->m_tNextGraphID =
                    l_tpALifeMonsterAbstract->m_tGraphID;
            return;
        }
    }

    CGameGraph::const_spawn_iterator i, e;
    ai().game_graph().begin_spawn(tGraphID, i, e);
    VERIFY(e == i + ai().game_graph().vertex(tGraphID)->death_point_count());
    i += (e != i) ? random().random(s32(e - i)) : 0;
    tpALifeCreatureAbstract->m_tGraphID = tGraphID;
#ifdef DEBUG
    if (psAI_Flags.test(aiALife))
    {
        Msg("[LSS] Generated death position %s[%f][%f][%f] -> [%f][%f][%f] : [%d]",
            tpALifeCreatureAbstract->name_replace(), VPUSH(tpALifeCreatureAbstract->o_Position),
            VPUSH((*i).level_point()), (*i).level_vertex_id());
    }
#endif
    tpALifeCreatureAbstract->o_Position = (*i).level_point();
    tpALifeCreatureAbstract->m_tNodeID = (*i).level_vertex_id();
    R_ASSERT2((ai().game_graph().vertex(tGraphID)->level_id() != graph().level().level_id()) ||
            ai().level_graph().valid_vertex_id(tpALifeCreatureAbstract->m_tNodeID),
        "Invalid vertex");
    tpALifeCreatureAbstract->m_fDistance = (*i).distance();
    CSE_ALifeMonsterAbstract* l_tpALifeMonsterAbstract = smart_cast<CSE_ALifeMonsterAbstract*>(tpALifeCreatureAbstract);
    if (l_tpALifeMonsterAbstract)
        l_tpALifeMonsterAbstract->m_tPrevGraphID = l_tpALifeMonsterAbstract->m_tNextGraphID =
            l_tpALifeMonsterAbstract->m_tGraphID;
}

shared_str CALifeSimulatorBase::level_name() const
{
    return (ai().game_graph().header().level(ai().level_graph().level_id()).name());
}
