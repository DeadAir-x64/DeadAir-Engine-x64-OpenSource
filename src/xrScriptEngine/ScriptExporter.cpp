#include "pch.hpp"

#include "ScriptExporter.hpp"

#include "xrCommon/xr_unordered_map.h"

namespace xray::script_export
{
static size_t nodes_count = 0;

node* node::first_node = nullptr;

node::node(export_func export_func, dependencies_getter deps_getter, const char* name)
    : m_next_node(first_node), m_export_func(export_func), m_deps_getter(deps_getter), m_name(name)
{
    first_node = this;
    ++nodes_count;
}

node::~node()
{
    if (first_node == this)
        first_node = m_next_node;
    else
    {
        node* prev = first_node;
        while (prev && prev->m_next_node != this)
            prev = prev->m_next_node;
        if (prev)
            prev->m_next_node = m_next_node;
    }
}

void node::sort()
{
    enum class state
    {
        not_visited, visiting, done
    };
    xr_unordered_map<const node*, state> map;
    map.reserve(nodes_count);

    Msg("! [DA_PORT] sort: nodes_count=%zu, building map", nodes_count); FlushLog();

    size_t build_count = 0;
    for (auto n = first_node; n; n = n->m_next_node)
    {
        ++build_count;
        if (build_count > 500) { Msg("! [DA_PORT] sort: LOOP DETECTED in node list at %zu", build_count); FlushLog(); break; }
        map[n] = state::not_visited;
    }
    Msg("! [DA_PORT] sort: map built, size=%zu build_count=%zu", map.size(), build_count); FlushLog();

    xr_vector<node*> sorted;
    sorted.reserve(map.size());

    size_t dfs_calls = 0;
    std::function<void(const node*)> depth_first_search = [&](const node* n)
    {
        ++dfs_calls;
        const auto it = map.find(n);
        if (it != map.end())
        {
            R_ASSERT2(it->second != state::visiting, "Cyclic dependency in script export!");
            if (it->second == state::done)
                return;
        }

        map[n] = state::visiting;
        Msg("! [DA_PORT] sort: dfs=%zu visiting n=%p, before deps_getter", dfs_calls, (void*)n); FlushLog();

        const auto& [deps, deps_count] = n->m_deps_getter();
        Msg("! [DA_PORT] sort: dfs=%zu n=%p deps_count=%zu", dfs_calls, (void*)n, deps_count); FlushLog();
        for (size_t i = 0; i < deps_count; i++)
            depth_first_search(deps[i]);

        map[n] = state::done;
        sorted.push_back(const_cast<node*>(n));
    };

    size_t outer = 0;
    for (auto& [n, _] : map)
    {
        ++outer;
        if ((outer & 0x3F) == 0)
        {
            Msg("! [DA_PORT] sort: outer loop %zu/%zu", outer, map.size()); FlushLog();
        }
        depth_first_search(n);
    }
    Msg("! [DA_PORT] sort: dfs done, total dfs_calls=%zu, sorted=%zu", dfs_calls, sorted.size()); FlushLog();

    node* prev = nullptr;
    for (auto it = sorted.rbegin(); it != sorted.rend(); ++it)
    {
        (*it)->m_next_node = prev;
        prev = *it;
    }
    first_node = prev;

    // This is always logged to help find out if some nodes are missing
    Msg("* Script exporter has %zu nodes registered.", nodes_count);
}

void node::export_all(lua_State* luaState)
{
    if (!first_node)
    {
        Msg("! [DA_PORT] export_all: no nodes registered"); FlushLog();
        return;
    }

    ZoneScoped;

    static bool sorted = false;
    if (!sorted)
    {
        Msg("! [DA_PORT] export_all: before sort()"); FlushLog();
        sort();
        Msg("! [DA_PORT] export_all: after sort(), starting exports"); FlushLog();
        sorted = true;
    }

    size_t idx = 0;
    for (auto node = first_node; node; node = node->m_next_node)
    {
        Msg("! [DA_PORT] export_all: node[%zu] before export_func addr=%p name=%s", idx, (void*)node, node->m_name ? node->m_name : "?"); FlushLog();
        node->m_export_func(luaState);
        Msg("! [DA_PORT] export_all: node[%zu] after export_func name=%s", idx, node->m_name ? node->m_name : "?"); FlushLog();
        ++idx;
    }
    Msg("! [DA_PORT] export_all: DONE, exported %zu nodes", idx); FlushLog();
}
} // namespace xray::script_export
