////////////////////////////////////////////////////////////////////////////
//	Module 		: alife_graph_registry.cpp
//	Created 	: 15.01.2003
//  Modified 	: 12.05.2004
//	Author		: Dmitriy Iassenev
//	Description : ALife graph registry
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "alife_graph_registry.h"
#include "xrServerEntities/xrMessages.h"

using namespace ALife;

CALifeGraphRegistry::CALifeGraphRegistry()
{
    m_level = 0;
    m_process_time = 0;
    m_actor = 0;
}

CALifeGraphRegistry::~CALifeGraphRegistry() { xr_delete(m_level); }
void CALifeGraphRegistry::on_load()
{
    for (int i = 0; i < GameGraph::LOCATION_TYPE_COUNT; ++i)
    {
        {
            for (int j = 0; j < GameGraph::LOCATION_COUNT; ++j)
                m_terrain[i][j].clear();
        }
        for (GameGraph::_GRAPH_ID j = 0; j < (GameGraph::_GRAPH_ID)ai().game_graph().header().vertex_count(); ++j)
            m_terrain[i][ai().game_graph().vertex(j)->vertex_type()[i]].push_back(j);
    }

    m_objects.resize(ai().game_graph().header().vertex_count());

    {
        GRAPH_REGISTRY::iterator I = m_objects.begin();
        GRAPH_REGISTRY::iterator E = m_objects.end();
        for (; I != E; ++I)
            (*I).objects().clear();
    }
}

void CALifeGraphRegistry::update(CSE_ALifeDynamicObject* object)
{
    if (!object->m_bDirectControl)
        return;

    if (object->s_flags.is(M_SPAWN_OBJECT_ASPLAYER))
    {
        m_actor = smart_cast<CSE_ALifeCreatureActor*>(object);
        R_ASSERT2(m_actor, "Invalid flag M_SPAWN_OBJECT_ASPLAYER for non-actor object!");

        if (g_start_game_vertex_id)
        {
            m_actor->m_tGraphID = g_start_game_vertex_id;
            m_actor->o_Position = g_start_position;
        }
    }

    if (m_actor && !m_level)
        setup_current_level();

    CSE_ALifeInventoryItem* item = smart_cast<CSE_ALifeInventoryItem*>(object);
    if (!item || !item->attached())
        add(object, object->m_tGraphID);
}

void CALifeGraphRegistry::setup_current_level()
{
    m_level = xr_new<CALifeLevelRegistry>(ai().game_graph().vertex(actor()->m_tGraphID)->level_id());
    level().set_process_time(m_process_time);
    for (int i = 0, n = ai().game_graph().header().vertex_count(); i < n; ++i)
        if (ai().game_graph().vertex(i)->level_id() == level().level_id())
        {
            D_OBJECT_P_MAP::const_iterator I = m_objects[i].objects().objects().begin();
            D_OBJECT_P_MAP::const_iterator E = m_objects[i].objects().objects().end();
            for (; I != E; ++I)
                level().add((*I).second);
        }

    {
        // [DA_PORT] The loop above has already added everything the graph points hold for this level.
        // m_temp collects objects registered while there was no level registry to put them in - and
        // nothing stops an object being in both lists, because add() chooses between them on the state
        // the object had at the time, and that state changes as it goes online and offline. Adding it
        // a second time is a hard assert, which is how a level change could end the game outright.
        xr_vector<CSE_ALifeDynamicObject*>::const_iterator I = m_temp.begin();
        xr_vector<CSE_ALifeDynamicObject*>::const_iterator E = m_temp.end();
        for (; I != E; ++I)
        {
            if (level().object((*I)->ID, true))
            {
                Msg("! [DA_PORT] ALife: object [%s][%d] already on the level registry - skipping",
                    (*I)->name_replace(), (*I)->ID);
                continue;
            }
            level().add(*I);
        }

        m_temp.clear();
    }
    GameGraph::LEVEL_MAP::const_iterator I =
        ai().game_graph().header().levels().find(ai().game_graph().vertex(actor()->m_tGraphID)->level_id());
    R_ASSERT2(ai().game_graph().header().levels().end() != I, "Graph point level ID not found!");

    [[maybe_unused]] const int id = g_pGamePersistent->Level_ID(I->second.name().c_str(), "1.0", true);
    VERIFY3(id >= 0, "Level is corrupted or doesn't exist", I->second.name().c_str());
    ai().load(I->second.name().c_str());

    g_start_game_vertex_id = 0;
}

void CALifeGraphRegistry::attach(CSE_Abstract& object, CSE_ALifeInventoryItem* item,
    GameGraph::_GRAPH_ID game_vertex_id, bool alife_query, bool add_children)
{
#ifdef DEBUG
    if (psAI_Flags.test(aiALife))
    {
        Msg("[LSS] Attaching item [%s][%d] to [%s][%d]", item->base()->name_replace(), item->base()->ID,
            object.name_replace(), object.ID);
    }
#endif
    if (alife_query)
        remove(smart_cast<CSE_ALifeDynamicObject*>(item), game_vertex_id);
    else
    {
        // [DA_PORT] tolerate an item that was never level().add()'ed (spawned already attached
        // to a parent) — same crash class as remove() above.
        CSE_ALifeDynamicObject* it = smart_cast<CSE_ALifeDynamicObject*>(item);
        level().remove(it, level().object(it->ID, true) == nullptr);
    }

    CSE_ALifeDynamicObject* dynamic_object = smart_cast<CSE_ALifeDynamicObject*>(&object);
    R_ASSERT2(!alife_query || dynamic_object, "Cannot attach an item to a non-alife object object");

    dynamic_object->attach(item, alife_query, add_children);
}

void CALifeGraphRegistry::detach(CSE_Abstract& object, CSE_ALifeInventoryItem* item,
    GameGraph::_GRAPH_ID game_vertex_id, bool alife_query, bool remove_children)
{
#ifdef DEBUG
    if (psAI_Flags.test(aiALife))
    {
        Msg("[LSS] Detaching item [%s][%d] from [%s][%d]", item->base()->name_replace(), item->base()->ID,
            object.name_replace(), object.ID);
    }
#endif
    if (alife_query)
        add(smart_cast<CSE_ALifeDynamicObject*>(item), game_vertex_id);
    else
    {
        CSE_ALifeDynamicObject* object = smart_cast<CSE_ALifeDynamicObject*>(item);
        VERIFY(object);
        object->m_tGraphID = game_vertex_id;
        level().add(object);
    }

    CSE_ALifeDynamicObject* dynamic_object = smart_cast<CSE_ALifeDynamicObject*>(&object);
    R_ASSERT2(!alife_query || dynamic_object, "Cannot detach an item from non-alife object");

    VERIFY(alife_query || !smart_cast<CSE_ALifeDynamicObject*>(&object) ||
        (ai().game_graph().vertex(smart_cast<CSE_ALifeDynamicObject*>(&object)->m_tGraphID)->level_id() ==
            level().level_id()));

    if (dynamic_object)
        dynamic_object->detach(item, 0, alife_query, remove_children);
    else
    {
#ifdef DEBUG
        bool value =
            std::find(object.children.begin(), object.children.end(), item->base()->ID) != object.children.end();
        if (!value)
        {
            Msg("! ERROR: can't detach independant object. entity[%s:%d], parent[%s:%d], section[%s]",
                item->base()->name_replace(), item->base()->ID, object.name_replace(), object.ID,
                item->base()->s_name.c_str());
        }
#endif // DEBUG
        //		R_ASSERT2				(value,"Can't detach an item which is not on my own");
    }
}

void CALifeGraphRegistry::add(CSE_ALifeDynamicObject* object, GameGraph::_GRAPH_ID game_vertex_id, bool update)
{
#ifdef DEBUG
    if (psAI_Flags.test(aiALife))
    {
        Msg("[LSS] adding object [%s][%d] to graph point %d", object->name_replace(), object->ID, game_vertex_id);
    }
#endif
    if (!object->m_bOnline && object->used_ai_locations() /**&& object->interactive()**/)
    {
        VERIFY(ai().game_graph().valid_vertex_id(game_vertex_id));

        // [DA_PORT] Registering the same object at the same graph point twice is a hard assert inside
        // the registry, and it took down the game on a level change. The second registration is a
        // no-op for an object already there, so it is skipped rather than fatal - but it is reported,
        // because arriving here at all means something registered an object it had not unregistered,
        // and the log line is what identifies who. Crashing instead tells nobody anything.
        const auto& registered = m_objects[game_vertex_id].objects().objects();
        if (registered.find(object->ID) != registered.end())
        {
            Msg("! [DA_PORT] ALife: object [%s][%d] already registered at graph point %d - skipping",
                object->name_replace(), object->ID, game_vertex_id);
            object->m_tGraphID = game_vertex_id;
        }
        else
        {
            m_objects[game_vertex_id].objects().add(object->ID, object);
            object->m_tGraphID = game_vertex_id;
        }
    }
    else if (!m_level && update)
    {
        m_temp.push_back(object);
        object->m_tGraphID = game_vertex_id;
    }

    if (update && m_level && ai().game_graph().valid_vertex_id(game_vertex_id))
        level().add(object);
}

void CALifeGraphRegistry::remove(CSE_ALifeDynamicObject* object, GameGraph::_GRAPH_ID game_vertex_id, bool update)
{
    if (object->used_ai_locations() /**&& object->interactive()**/)
    {
#ifdef DEBUG
        if (psAI_Flags.test(aiALife))
        {
            Msg("[LSS] removing object [%s][%d] from graph point %d", object->name_replace(), object->ID,
                game_vertex_id);
        }
#endif
        // [DA_PORT] no_assert=true. An item spawned already attached to a parent
        // (alife():create with a parent id) is never added to the world registry —
        // register_object()'s graph().update() skips attached items (see update():66) — so a
        // later attach()/remove() finds no key here. Asserting hard-crashed the game (repro:
        // an NPC gathering an artefact via xr_gather_items while the actor stood at a trader).
        // Removing an absent object from this per-vertex list is a benign no-op.
        m_objects[game_vertex_id].objects().remove(object->ID, true);
    }
    if (update && m_level)
    {
        const bool cross_level =
            ai().game_graph().vertex(game_vertex_id)->level_id() != level().level_id();
        // Same tolerance for the per-level registry: an attached-spawn item was never level().add()'ed.
        const bool absent = (level().object(object->ID, true) == nullptr);
        if (absent && !cross_level)
            Msg("~ [DA_PORT] graph().remove: object id[%u] absent from level registry "
                "(attached-spawn) - skipping instead of asserting",
                u32(object->ID));
        level().remove(object, cross_level || absent);
    }
}
