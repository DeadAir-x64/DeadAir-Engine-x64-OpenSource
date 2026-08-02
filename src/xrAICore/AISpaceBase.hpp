#pragma once

#include "xrCore/xrCore.h"

#ifdef XRAY_STATIC_BUILD
#   define XRAICORE_API
#else
#   ifdef XRAICORE_EXPORTS
#      define XRAICORE_API XR_EXPORT
#   else
#      define XRAICORE_API XR_IMPORT
#   endif
#endif

class CGameGraph;
class CGameLevelCrossTable;
class CLevelGraph;
class CGraphEngine;
class CPatrolPathStorage;

class XRAICORE_API AISpaceBase
{
protected:
    CGameGraph* m_game_graph = nullptr; // not owned by AISpaceBase
    CLevelGraph* m_level_graph = nullptr;
    CGraphEngine* m_graph_engine = nullptr;
    CPatrolPathStorage* m_patrol_path_storage = nullptr;

    // [DA_PORT] One graph engine per task worker; slot 0 aliases m_graph_engine.
    //
    // The shape of this is taken from Dead Air: Refined (github.com/MMadmer/Dead-Air-Refined),
    // an independent x64 port of the same mod, MIT like the rest of OpenXRay.
    //
    // A search mutates the engine's scratch storage - priority queue, vertex manager, path - so two
    // threads searching through one engine corrupt each other. The five ScopeLocks that would have
    // stopped that are commented out in graph_engine_inline.h, upstream and here alike, so there is
    // nothing holding the line at all. It costs us nothing today because the frame's "parallel"
    // sequence is in fact a plain sequential loop (Device.cpp), but that is exactly why this is worth
    // doing now: whoever parallelises that dispatch will not be met by a silent corruption.
    //
    // Engines are built on first use by a worker, not up front. Each one is a few MB (the vertex
    // manager is sized by the level's vertex count), so allocating sixteen eagerly would be real
    // memory spent on threads that may never search.
    mutable xr_vector<CGraphEngine*> m_worker_graph_engines;
    u32 m_graph_engine_vertex_count = 0;

    void CreateGraphEngines(u32 maxVertexCount);
    void DestroyGraphEngines();

protected:
    AISpaceBase();
    void Load(const char* levelName);
    void Unload(bool reload);
    void Initialize();
    void Validate(u32 levelId) const;
    void patrol_path_storage_raw(IReader& stream);
    void patrol_path_storage(IReader& stream);
    void SetGameGraph(CGameGraph* gameGraph);

public:
    virtual ~AISpaceBase();
    inline CGameGraph& game_graph() const;
    inline CGameGraph* get_game_graph() const;
    inline CLevelGraph& level_graph() const;
    inline const CLevelGraph* get_level_graph() const;
    const CGameLevelCrossTable& cross_table() const;
    const CGameLevelCrossTable* get_cross_table() const;
    inline const CPatrolPathStorage& patrol_paths() const;
    CGraphEngine& graph_engine() const;
};

inline CGameGraph& AISpaceBase::game_graph() const
{
    VERIFY(m_game_graph);
    return *m_game_graph;
}

inline CGameGraph* AISpaceBase::get_game_graph() const { return m_game_graph; }
inline CLevelGraph& AISpaceBase::level_graph() const
{
    VERIFY(m_level_graph);
    return *m_level_graph;
}

inline const CLevelGraph* AISpaceBase::get_level_graph() const { return m_level_graph; }

inline const CPatrolPathStorage& AISpaceBase::patrol_paths() const
{
    VERIFY(m_patrol_path_storage);
    return *m_patrol_path_storage;
}
