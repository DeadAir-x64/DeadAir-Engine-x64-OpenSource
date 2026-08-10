////////////////////////////////////////////////////////////////////////////
//	Module 		: game_graph_script.cpp
//	Created 	: 02.11.2005
//  Modified 	: 02.11.2005
//	Author		: Dmitriy Iassenev
//	Description : Game graph class script export
////////////////////////////////////////////////////////////////////////////

#include "pch.hpp"

#include "game_graph.h"
#include "AISpaceBase.hpp"

#include "xrScriptEngine/script_space.hpp"
#include "xrScriptEngine/da_lua_singleton.hpp"

// [DA_PORT] Одна обёртка вместо новой на каждый вызов — шестой одиночка к пяти закрытым ранее.
// Замер: 1078 обёрток CGameGraph за 300 кадров при том, что граф ОДИН И ТОТ ЖЕ. Скрипты зовут
// game_graph() в горячих местах (smart_terrain:632 и :1513 — по разу на проверку смарта).
// Кэш чинит себя сам сравнением указателей: на смене уровня граф пересоздаётся, и обёртка
// перестроится, а не укажет в освобождённую память.
static da_lua_singleton<const CGameGraph> s_da_game_graph;

const CGameGraph* get_game_graph() { return &GEnv.AISpace->game_graph(); }

static int da_lua_game_graph(lua_State* L)
{
    return s_da_game_graph.push(L, get_game_graph(), "game_graph");
}
const CGameGraph::CHeader* get_header(const CGameGraph* self_) { return (&self_->header()); }
bool get_accessible1(const CGameGraph* self_, const u32& vertex_id) { return (self_->accessible(vertex_id)); }
void get_accessible2(const CGameGraph* self_, const u32& vertex_id, bool value) { self_->accessible(vertex_id, value); }
Fvector CVertex__level_point(const CGameGraph::CGameVertex* vertex)
{
    THROW(vertex);
    return (vertex->level_point());
}

Fvector CVertex__game_point(const CGameGraph::CGameVertex* vertex)
{
    THROW(vertex);
    return (vertex->game_point());
}

GameGraph::LEVEL_MAP const& get_levels(CGameGraph const* graph)
{
    THROW(graph);
    return graph->header().levels();
}

void CGameGraph::script_register(lua_State* luaState)
{
    using namespace luabind;
    using namespace luabind::policy;

    s_da_game_graph.reset(); // состояние Lua пересоздано — прежняя ссылка недействительна

    module(luaState)
    [
        class_<GameGraph::LEVEL_MAP::value_type>("GameGraph__LEVEL_MAP__value_type")
            .def_readonly("id", &GameGraph::LEVEL_MAP::value_type::first)
            .def_readonly("level", &GameGraph::LEVEL_MAP::value_type::second),

        def("game_graph", &get_game_graph), // перекрывается ниже кэширующей версией

        class_<CGameGraph>("CGameGraph")
            .def("accessible", &get_accessible1)
            .def("accessible", &get_accessible2)
            .def("valid_vertex_id", &CGameGraph::valid_vertex_id)
            .def("vertex", &CGameGraph::vertex)
            .def("vertex_id", &CGameGraph::vertex_id)
            .def("levels", &get_levels, return_stl_iterator()),

        class_<CGameVertex>("GameGraph__CVertex")
            .def("level_point", &CVertex__level_point)
            .def("game_point", &CVertex__game_point)
            .def("level_id", &CGameVertex::level_id)
            .def("level_vertex_id", &CGameVertex::level_vertex_id),

        def("gg_vertex_level_id", +[](u32 vertex_id)
        {
            return GEnv.AISpace->game_graph().vertex(vertex_id)->level_id();
        }),
        def("gg_level_id", +[](_LEVEL_ID idx)
        {
            const GameGraph::LEVEL_MAP& levels = GEnv.AISpace->game_graph().header().levels();
            return (levels.begin() + idx)->second.id();
        }),
        def("gg_levels_count", +[]()
        {
            return GEnv.AISpace->game_graph().header().level_count();
        }),
        def("gg_distance", +[](u32 vid1, u32 vid2)
        {
            const auto game_graph = GEnv.AISpace->game_graph();
            const auto p1 = game_graph.vertex(vid1)->game_point();
            const auto p2 = game_graph.vertex(vid2)->game_point();
            return p1.distance_to(p2);
        })
    ];

    // [DA_PORT] Кэширующая версия ставится ПОСЛЕ module(): она перекрывает объявленную выше
    // обычную. Так — потому что внутри module() глобальную функцию задать нельзя, а править
    // порядок регистрации в чужом файле рискованнее, чем перекрыть готовое.
    lua_pushcfunction(luaState, da_lua_game_graph);
    lua_setglobal(luaState, "game_graph");
}
