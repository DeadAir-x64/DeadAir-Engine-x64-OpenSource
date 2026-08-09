////////////////////////////////////////////////////////////////////////////
//	Module 		: alife_monster_brain.cpp
//	Created 	: 06.10.2005
//  Modified 	: 22.11.2005
//	Author		: Dmitriy Iassenev
//	Description : ALife monster brain class
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "alife_monster_brain.h"
#include "Common/object_broker.h"
#include "xrServer_Objects_ALife_Monsters.h"

#include "alife_monster_movement_manager.h"
#include "alife_monster_detail_path_manager.h"
#include "alife_monster_patrol_path_manager.h"
#include "ai_space.h"
#include "ef_storage.h"
#include "ef_primary.h"
#include "alife_simulator.h"
#include "alife_graph_registry.h"
#include "movement_manager_space.h"
#include "alife_smart_terrain_registry.h"
#include "alife_time_manager.h"
#include "date_time.h"
#ifdef DEBUG
#include "Level.h"
#include "map_location.h"
#include "map_manager.h"
#endif

CALifeMonsterBrain::CALifeMonsterBrain(object_type* object)
{
    VERIFY(object);
    m_object = object;
    m_last_search_time = 0;
    m_smart_terrain = nullptr;

    m_movement_manager = xr_new<CALifeMonsterMovementManager>(object);

    u32 hours, minutes, seconds;
    sscanf(pSettings->r_string(this->object().name(), "smart_terrain_choose_interval"), "%d:%d:%d", &hours, &minutes,
        &seconds);
    m_time_interval = (u32)generate_time(1, 1, 1, hours, minutes, seconds);

    m_can_choose_alife_tasks = true;
}

CALifeMonsterBrain::~CALifeMonsterBrain()
{
    xr_delete(m_movement_manager);
}

void CALifeMonsterBrain::on_state_write(NET_Packet& /*packet*/) {}
void CALifeMonsterBrain::on_state_read(NET_Packet& /*packet*/) {}

bool CALifeMonsterBrain::perform_attack() { return (false); }
ALife::EMeetActionType CALifeMonsterBrain::action_type(
    CSE_ALifeSchedulable* tpALifeSchedulable, const int& iGroupIndex, const bool& bMutualDetection)
{
    return (ALife::eMeetActionTypeIgnore);
}

void CALifeMonsterBrain::on_register() {}
void CALifeMonsterBrain::on_unregister() {}
void CALifeMonsterBrain::on_location_change() {}
CSE_ALifeSmartZone& CALifeMonsterBrain::smart_terrain()
{
    VERIFY(object().m_smart_terrain_id != 0xffff);
    if (m_smart_terrain && (object().m_smart_terrain_id == m_smart_terrain->ID))
        return (*m_smart_terrain);

    m_smart_terrain = ai().alife().smart_terrains().object(object().m_smart_terrain_id);
    VERIFY(m_smart_terrain);
    return (*m_smart_terrain);
}

void CALifeMonsterBrain::process_task()
{
    CALifeSmartTerrainTask* task = smart_terrain().task(&object());
    THROW3(task, "smart terrain returned nil task, while npc is registered in it", smart_terrain().name_replace());
    movement().path_type(MovementManager::ePathTypeGamePath);
    movement().detail().target(*task);
}

void CALifeMonsterBrain::select_task(const bool forced)
{
    if (object().m_smart_terrain_id != 0xffff)
        return;

    if (!can_choose_alife_tasks())
        return;

    ALife::_TIME_ID current_time = ai().alife().time_manager().game_time();

    if (!forced && m_last_search_time + m_time_interval > current_time)
        return;

    m_last_search_time = current_time;

    float best_value = flt_min;
    CALifeSmartTerrainRegistry::OBJECTS::const_iterator I = ai().alife().smart_terrains().objects().begin();
    CALifeSmartTerrainRegistry::OBJECTS::const_iterator E = ai().alife().smart_terrains().objects().end();
    for (; I != E; ++I)
    {
        if (!(*I).second->enabled(&object()))
            continue;

        float value = (*I).second->suitable(&object());
        if (value > best_value)
        {
            best_value = value;
            object().m_smart_terrain_id = (*I).second->ID;
        }
    }

    if (object().m_smart_terrain_id != 0xffff)
    {
        smart_terrain().register_npc(&object());
        m_last_search_time = 0;
    }
}

// [DA_PORT] Разбор обновления оффлайнового NPC по фазам. Порог — da_alife_dump, мс. 0 = молчит.
//
// Это то, что планировщик зовёт на каждого сталкера в оффлайне. da_sched_dump показал у таких
// объектов худшие вызовы по 6-7 мс при общей нагрузке планировщика 0.2 мс на кадр, то есть
// выброс делает один вызов. Здесь видно, какая из трёх его фаз столько стоит.
//
// Печать только по превышению: обновлений тысячи, а интересных единицы.
extern ENGINE_API float ps_da_alife_dump;

void CALifeMonsterBrain::update(const bool forced)
{
    const bool da_watch = ps_da_alife_dump > 0.f;
    CTimer da_t;
    float da_select = 0.f, da_task = 0.f, da_move = 0.f;
    if (da_watch)
        da_t.Start();

#if 0 // def DEBUG
    if (!Level().MapManager().HasMapLocation("debug_stalker",object().ID)) {
        CMapLocation				*map_location =
            Level().MapManager().AddMapLocation(
                "debug_stalker",
                object().ID
            );

        map_location->SetHint		(object().name_replace());
    }
#endif

    select_task(forced);
    if (da_watch) { da_select = da_t.GetElapsed_sec() * 1000.f; da_t.Start(); }

    if (object().m_smart_terrain_id != 0xffff)
        process_task();
    else
        default_behaviour();
    if (da_watch) { da_task = da_t.GetElapsed_sec() * 1000.f; da_t.Start(); }

    movement().update();

    if (da_watch)
    {
        da_move = da_t.GetElapsed_sec() * 1000.f;
        const float total = da_select + da_task + da_move;
        if (total >= ps_da_alife_dump)
            Msg("~ [DA_ALIFE] %s: всего %.3f мс | выбор задачи %.3f | задача %.3f | движение %.3f | смарт %s",
                object().name_replace(), total, da_select, da_task, da_move,
                object().m_smart_terrain_id != 0xffff ? "есть" : "нет");
    }
}

void CALifeMonsterBrain::default_behaviour() { movement().path_type(MovementManager::ePathTypeNoPath); }
void CALifeMonsterBrain::on_switch_online() { movement().on_switch_online(); }
void CALifeMonsterBrain::on_switch_offline() { movement().on_switch_offline(); }
