////////////////////////////////////////////////////////////////////////////
//	Module 		: stalker_movement_manager_obstacles_path.cpp
//	Created 	: 18.04.2007
//  Modified 	: 18.04.2007
//	Author		: Dmitriy Iassenev
//	Description : Stalker movement manager: dynamic obstacles avoidance: build path
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "stalker_movement_manager_obstacles.h"
#include "stalker_movement_manager_space.h"
#include "ai_space.h"
#include "ai/stalker/ai_stalker.h"
#include "restricted_object_obstacle.h"
#include "level_path_manager.h"
#include "xrAICore/Navigation/ai_object_location.h"
#include "moving_objects.h"
#include "detail_path_manager.h"
#include "level_path_builder.h"
#include "ai_obstacle.h"

#ifndef MASTER_GOLD
#include "ai_debug.h"
#include "xrAICore/Navigation/level_graph.h"
#include "PHMovementControl.h"
#include "CharacterPhysicsSupport.h"

// [DA_PORT] Putting a stalker that has slid off the navigation mesh back onto it - see below.
// A switch, because it moves an NPC, and anything that moves an NPC should be possible to turn off.
extern ENGINE_API int ps_ai_unstick;
extern ENGINE_API float ps_ai_unstick_range;
#endif // MASTER_GOLD

static const float check_time_delta = 1.f;

bool stalker_movement_manager_obstacles::simulate_path_navigation()
{
    Fvector current_position = object().Position();
    Fvector previous_position = current_position;
    u32 current_travel_point = 0;
    while (!detail().completed(current_position, !detail().state_patrol_path(), current_travel_point))
    {
        m_static_obstacles.on_before_query();
        m_static_obstacles.query(current_position, previous_position);

        if (!m_static_obstacles.process_query(false))
        {
            m_last_fail_time = Device.dwTimeGlobal;
            m_failed_to_build_path = true;
            restore_current_state();
            return (false);
        }

        if (m_static_obstacles.need_path_to_rebuild())
            return (false);

        //		float						dist_to_target;
        //		Fvector						dir_to_target;
        //		float						distance;
        //		current_position			=
        // path_position(1.f,current_position,check_time_delta,current_travel_point,distance,dist_to_target,dir_to_target);
        previous_position = current_position;
        current_position = predict_position(check_time_delta, current_position, current_travel_point, 1.f);
    }

    return (true);
}

void stalker_movement_manager_obstacles::save_current_state()
{
    m_saved_state = false;

    if (level_path().path().empty())
        return;

    if (level_path().path().back() != level_path_builder().dest_vertex_id())
        return;

    if (detail().path().empty())
        return;

    if (detail().dest_vertex_id() != level_path_builder().dest_vertex_id())
        return;

    m_saved_state = true;
    m_level_path.swap(level_path_path());
    m_detail_current_index = detail().path().empty() ? u32(-1) : detail().curr_travel_point_index();
    m_detail_path.swap(detail().path());
#ifdef DEBUG
    m_detail_key_points.swap(detail().key_points());
#endif // DEBUG
    m_detail_last_patrol_point = detail().last_patrol_point();
    m_saved_current_iteration.copy(m_static_obstacles.current_iteration());
}

void stalker_movement_manager_obstacles::restore_current_state()
{
    if (!m_saved_state)
        return;

    m_level_path.swap(level_path_path());
    m_detail_path.swap(detail().path());
    detail().m_current_travel_point = m_detail_current_index;
#ifdef DEBUG
    m_detail_key_points.swap(detail().key_points());
#endif // DEBUG
    detail().last_patrol_point(m_detail_last_patrol_point);
    m_saved_current_iteration.swap(m_static_obstacles.current_iteration());
}

IC void stalker_movement_manager_obstacles::remove_query_objects(const Fvector& position, const float& radius)
{
    m_static_obstacles.inactive_query().remove_objects(position, radius);
    m_static_obstacles.active_query().remove_objects(position, radius);
}

void stalker_movement_manager_obstacles::build_level_path()
{
#ifndef MASTER_GOLD
    if (!psAI_Flags.test(aiObstaclesAvoiding))
    {
        inherited::build_level_path();
        return;
    }
#endif // MASTER_GOLD

#ifdef DEBUG
    CTimer timer;
    timer.Start();
#endif // DEBUG

    if (m_last_dest_vertex_id != level_path().dest_vertex_id())
        remove_query_objects(object().Position(), 5.f);

    m_last_fail_time = 0;

    m_failed_to_build_path = false;
    //	Msg								("[%6d] m_failed_to_build_path = %s
    //(stalker_movement_manager_obstacles::build_level_path)",Device.dwTimeGlobal,m_failed_to_build_path ? "true" :
    //"false");

    save_current_state();
    m_static_obstacles.inactive_query().copy(m_static_obstacles.active_query());
    m_static_obstacles.inactive_query().update_objects(object().Position(), 10000.f);

#ifndef MASTER_GOLD
    if (!psAI_Flags.test(aiObstaclesAvoidingStatic))
        m_dynamic_obstacles.inactive_query().copy(m_dynamic_obstacles.active_query());
#endif // MASTER_GOLD

    bool pure_search_tried = false;
    bool pure_search_result = false;

    do
    {
        if (m_failed_to_build_path)
            break;

        inherited::build_level_path();

        if (level_path().failed())
        {
            if (!pure_search_tried)
            {
                pure_search_tried = true;

                m_static_obstacles.clear();
                m_saved_current_iteration.clear();

                level_path().invalidate_failed_info();

                inherited::build_level_path();

                pure_search_result = !level_path().failed();
            }

            if (!pure_search_result)
            {
                // [DA_PORT] Put a stalker that has fallen off the navigation mesh back onto it.
                //
                // Measured on the swamps: two stalkers stood at one spot at height 0.2 and failed to
                // build a path a hundred times a second each, for the whole session. A stalker standing
                // where there is no mesh cannot path ANYWHERE, so it retries every frame forever - and
                // every retry is two full A* searches, because the failure path clears the obstacles and
                // searches again. Two of them cost some two thousand wasted searches a second.
                //
                // This is a SNAP, not a teleport: each one is moved to the nearest valid vertex to
                // ITSELF, which for someone who has slid a step off the edge is a step back onto it.
                // There is no shared destination and nothing to gather them into one place.
                //
                // Guarded three ways, because moving an NPC is a real change to the world:
                //  - only after it has failed repeatedly, so an ordinary unreachable target never
                //    triggers it;
                //  - only if the mesh is within ai_unstick_range, so nobody is dragged across the map -
                //    if there is nothing near, it is left where it is and said so;
                //  - once, then a cooldown, so a genuinely hopeless case cannot turn into a warp loop.
                if (ps_ai_unstick)
                {
                    static xr_map<u32, std::pair<u32, u32>> s_stuck; // id -> (failures, time of last snap)
                    auto& st = s_stuck[object().ID()];
                    ++st.first;

                    if (st.first >= 20 && Device.dwTimeGlobal - st.second > 10000)
                    {
                        st.first = 0;
                        st.second = Device.dwTimeGlobal;

                        const Fvector position = object().Position();
                        const u32 hint = object().ai_location().level_vertex_id();
                        const u32 vertex_id = ai().level_graph().valid_vertex_id(hint) ?
                            ai().level_graph().vertex(hint, position) :
                            ai().level_graph().vertex_id(position);

                        if (ai().level_graph().valid_vertex_id(vertex_id))
                        {
                            const Fvector target = ai().level_graph().vertex_position(vertex_id);
                            const float distance = position.distance_to(target);
                            if (distance <= ps_ai_unstick_range)
                            {
                                object().character_physics_support()->movement()->SetPosition(target);
                                object().Position() = target;
                                object().ai_location().level_vertex(vertex_id);
                                Msg("~ [DA_PORT] AI: [%s] was off the navigation mesh, moved %.2fm back "
                                    "onto it",
                                    object().cName().c_str(), distance);
                            }
                            else
                                Msg("! [DA_PORT] AI: [%s] is stuck off the navigation mesh and the "
                                    "nearest node is %.1fm away - left alone",
                                    object().cName().c_str(), distance);
                        }
                    }
                }

#ifndef MASTER_GOLD
                // [DA_PORT] Say WHO, and stop saying it every frame.
                //
                // This fires when a path cannot be built even after every obstacle has been cleared and
                // the search re-run, so each line already stands for two full A* searches thrown away.
                // Six and a half thousand of them in one session is not a map with awkward corners, it
                // is somebody stuck in a retry loop - but the message named no one, so there was no way
                // to tell one hopeless stalker from a hundred ordinary failures. It also wrote to the
                // log on every single occurrence, which is thousands of file writes for one stuck NPC.
                //
                // Throttled per object rather than globally: one talkative stalker must not hide the
                // rest, and the suppressed count is what distinguishes "stuck" from "occasionally
                // fails" at a glance.
                {
                    static xr_map<u32, std::pair<u32, u32>> s_last; // id -> (time of last report, suppressed)
                    auto& entry = s_last[object().ID()];
                    if (Device.dwTimeGlobal - entry.first > 5000)
                    {
                        Msg("! level_path().failed() during navigation: [%s] at (%3.1f, %3.1f, %3.1f)%s",
                            object().cName().c_str(), VPUSH(object().Position()),
                            entry.second ? make_string(", %d more suppressed", entry.second).c_str() : "");
                        entry.first = Device.dwTimeGlobal;
                        entry.second = 0;
                    }
                    else
                        ++entry.second;
                }
#endif // #ifndef MASTER_GOLD
                break;
            }
        }
    } while (!simulate_path_navigation());

    m_last_dest_vertex_id = level_path().dest_vertex_id();
    //	Msg								("[%6d][%6d][%s][%f]
    // build_level_path",Device.dwFrame,Device.dwTimeGlobal,*object().cName(),timer.GetElapsed_sec()*1000.f);
}
