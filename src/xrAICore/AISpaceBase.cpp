#include "pch.hpp"
#include "AISpaceBase.hpp"
#include "Navigation/game_graph.h"
#include "Navigation/level_graph.h"
#include "Navigation/PatrolPath/patrol_path_storage.h"
#include "Navigation/graph_engine.h"
#include "xrCore/Threading/TaskManager.hpp"

#include <thread>

AISpaceBase::AISpaceBase() { GEnv.AISpace = this; }
AISpaceBase::~AISpaceBase()
{
    xr_delete(m_patrol_path_storage);
    DestroyGraphEngines();
    VERIFY(!m_game_graph);
    GEnv.AISpace = nullptr;
}

// [DA_PORT] See the header for why these exist. Slot 0 is the engine everything used before and
// stays reachable through m_graph_engine; the rest are filled in lazily by graph_engine().
void AISpaceBase::CreateGraphEngines(u32 maxVertexCount)
{
    DestroyGraphEngines();

    const size_t workerCount =
        TaskScheduler ? TaskScheduler->GetWorkersCount() : std::thread::hardware_concurrency();

    m_graph_engine_vertex_count = maxVertexCount;
    m_worker_graph_engines.resize(_max<size_t>(workerCount, 1));
    m_graph_engine = xr_new<CGraphEngine>(maxVertexCount);
    m_worker_graph_engines.front() = m_graph_engine;
}

void AISpaceBase::DestroyGraphEngines()
{
    for (CGraphEngine*& engine : m_worker_graph_engines)
        xr_delete(engine);

    m_worker_graph_engines.clear();
    m_graph_engine = nullptr;
    m_graph_engine_vertex_count = 0;
}

CGraphEngine& AISpaceBase::graph_engine() const
{
    VERIFY(m_graph_engine);

    if (!TaskScheduler || m_worker_graph_engines.empty())
        return *m_graph_engine;

    const size_t worker = TaskScheduler->GetCurrentWorkerID();
    if (worker >= m_worker_graph_engines.size())
        return *m_graph_engine;

    CGraphEngine*& engine = m_worker_graph_engines[worker];
    if (!engine)
    {
        // Only this worker ever writes this slot, and the vector is never resized after
        // CreateGraphEngines, so there is nothing here for another thread to race against.
        engine = xr_new<CGraphEngine>(m_graph_engine_vertex_count);

        // Считаем сами: сколько таких экземпляров живёт и на какой граф они рассчитаны. Иначе цена
        // правки остаётся прикидкой - а их создаётся столько, сколько РАЗНЫХ воркеров хоть раз
        // искали путь, и это оказалось всё их число, а не один-два.
        u32 live = 0;
        for (const CGraphEngine* e : m_worker_graph_engines)
            if (e)
                ++live;

        Msg("* [DA] graph engine for worker %u: живых экземпляров %u, вершин в графе %u", u32(worker),
            live, m_graph_engine_vertex_count);
    }
    return *engine;
}

void AISpaceBase::Load(const char* levelName)
{
    ZoneScoped;
    const CGameGraph::SLevel& currentLevel = game_graph().header().level(levelName);
    m_level_graph = xr_new<CLevelGraph>();
    game_graph().set_current_level(currentLevel.id());
    auto& crossHeader = cross_table().header();
    auto& levelHeader = level_graph().header();
    auto& gameHeader = game_graph().header();
    R_ASSERT2(crossHeader.level_guid() == levelHeader.guid(), "cross_table doesn't correspond to the AI-map");
    R_ASSERT2(crossHeader.game_guid() == gameHeader.guid(), "graph doesn't correspond to the cross table");
    u32 vertexCount = _max(gameHeader.vertex_count(), levelHeader.vertex_count());
    CreateGraphEngines(vertexCount);
    R_ASSERT2(currentLevel.guid() == levelHeader.guid(), "graph doesn't correspond to the AI-map");
    if (!xr_strcmp(currentLevel.name(), levelName))
        Validate(currentLevel.id());
    level_graph().level_id(currentLevel.id());
}

void AISpaceBase::Unload(bool reload)
{
    if (GEnv.isDedicatedServer)
        return;
    DestroyGraphEngines();
    xr_delete(m_level_graph);
    if (!reload && m_game_graph)
        CreateGraphEngines(game_graph().header().vertex_count());
}

void AISpaceBase::Initialize()
{
    if (GEnv.isDedicatedServer)
        return;
    VERIFY(!m_graph_engine);
    CreateGraphEngines(1024);
    VERIFY(!m_patrol_path_storage);
    m_patrol_path_storage = xr_new<CPatrolPathStorage>();
}

void AISpaceBase::Validate(u32 levelId) const
{
#ifdef DEBUG
    VERIFY(level_graph().header().vertex_count() == cross_table().header().level_vertex_count());
    for (GameGraph::_GRAPH_ID i = 0, n = game_graph().header().vertex_count(); i < n; i++)
    {
        const GameGraph::CGameVertex& vertex = *game_graph().vertex(i);
        if (levelId != vertex.level_id())
            continue;
        u32 vid = vertex.level_vertex_id();
        if (!level_graph().valid_vertex_id(vid) || cross_table().vertex(vid).game_vertex_id() != i ||
            !level_graph().inside(vid, vertex.level_point()))
        {
            Msg("! Graph doesn't correspond to the cross table");
            R_ASSERT2(false, "Graph doesn't correspond to the cross table");
        }
    }
    // Msg("death graph point id : %d", cross_table().vertex(455236).game_vertex_id());
    for (u32 i = 0, n = game_graph().header().vertex_count(); i < n; i++)
    {
        if (levelId != game_graph().vertex(i)->level_id())
            continue;
        CGameGraph::const_spawn_iterator it, end;
        game_graph().begin_spawn(i, it, end);
        // Msg("vertex [%d] has %d death points", i, game_graph().vertex(i)->death_point_count());
        for (; it != end; it++)
            VERIFY(cross_table().vertex(it->level_vertex_id()).game_vertex_id() == i);
    }
// Msg("* Graph corresponds to the cross table");
#endif
}

void AISpaceBase::patrol_path_storage_raw(IReader& stream)
{
    if (GEnv.isDedicatedServer)
        return;
    ZoneScoped;
    xr_delete(m_patrol_path_storage);
    m_patrol_path_storage = xr_new<CPatrolPathStorage>();
    m_patrol_path_storage->load_raw(get_level_graph(), get_cross_table(), get_game_graph(), stream);
}

void AISpaceBase::patrol_path_storage(IReader& stream)
{
    if (GEnv.isDedicatedServer)
        return;
    ZoneScoped;
    xr_delete(m_patrol_path_storage);
    m_patrol_path_storage = xr_new<CPatrolPathStorage>();
    m_patrol_path_storage->load(stream);
}

void AISpaceBase::SetGameGraph(CGameGraph* gameGraph)
{
    if (gameGraph)
    {
        VERIFY(!m_game_graph);
        m_game_graph = gameGraph;
        CreateGraphEngines(game_graph().header().vertex_count());
    }
    else
    {
        VERIFY(m_game_graph);
        m_game_graph = nullptr;
        DestroyGraphEngines();
    }
}

const CGameLevelCrossTable& AISpaceBase::cross_table() const { return game_graph().cross_table(); }
const CGameLevelCrossTable* AISpaceBase::get_cross_table() const { return &game_graph().cross_table(); }
