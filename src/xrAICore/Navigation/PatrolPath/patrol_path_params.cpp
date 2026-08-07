////////////////////////////////////////////////////////////////////////////
//	Module 		: patrol_path_params.cpp
//	Created 	: 30.09.2003
//  Modified 	: 29.06.2004
//	Author		: Dmitriy Iassenev
//	Description : Patrol path parameters class
////////////////////////////////////////////////////////////////////////////

#include "pch.hpp"

#include "patrol_path_params.h"
#include "patrol_path_storage.h"
#include "AISpaceBase.hpp"

#include "xrScriptEngine/script_engine.hpp"

CPatrolPathParams::CPatrolPathParams(LPCSTR caPatrolPathToGo, EPatrolStartType tPatrolPathStart,
    EPatrolRouteType tPatrolPathStop, bool bRandom, u32 index)
    : m_path_name(caPatrolPathToGo)
{
    m_path = GEnv.AISpace->patrol_paths().path(m_path_name, true);

    // [DA_PORT] Сообщение вместо броска.
    //
    // Было `THROW3(m_path, ...)` — исключение, которое никто не ловит, то есть «Unexpected
    // application termination» из-за одного отсутствующего пути. Поймано на переходе в Военные
    // склады: аномальная зона mil_2c_01_hw_anomal_zone ссылается на mil_2c_01_hw_af_way, путь
    // объявлен в level.game того же уровня, но в реестре его к этому моменту нет.
    //
    // Ронять из-за этого всю игру несоразмерно: без пути зона просто не расставит артефакты, а
    // остальной мир к ней отношения не имеет. Все обращения к m_path ниже закрыты проверками
    // (VERIFY там не годился — он исчезает в релизе), поэтому пустой путь ведёт себя как путь из
    // нуля точек, а не переносит падение из конструктора в первый же вызванный метод.
    //
    // Сообщение подробное намеренно: имя пути — единственное, что позволит найти виновника в
    // данных уровня, а без него в логе останется только факт падения.
    if (!m_path)
        Msg("! [DA_PORT] патрульный путь [%s] не найден — объект останется без маршрута", caPatrolPathToGo);

    m_tPatrolPathStart = tPatrolPathStart;
    m_tPatrolPathStop = tPatrolPathStop;
    m_bRandom = bRandom;
    m_previous_index = index;
}

CPatrolPathParams::~CPatrolPathParams() {}
u32 CPatrolPathParams::count() const
{
    // [DA_PORT] Пустой путь = ноль точек, см. конструктор.
    if (!m_path)
        return 0;

    return (m_path->vertices().size());
}

const Fvector& CPatrolPathParams::point(u32 index) const
{
    static const Fvector nowhere = { 0.f, 0.f, 0.f };
    if (!m_path || m_path->vertices().empty())
        return nowhere;

    if (!m_path->vertex(index))
    {
        GEnv.ScriptEngine->script_log(LuaMessageType::Error,
            "Can't get information about patrol point number %d in the patrol way %s", index, m_path_name.c_str());
        index = (*m_path->vertices().begin()).second->vertex_id();
    }
    VERIFY(m_path->vertex(index));
    return (m_path->vertex(index)->data().position());
}

u32 CPatrolPathParams::level_vertex_id(u32 index) const
{
    if (!m_path || !m_path->vertex(index))
        return u32(-1);

    return (m_path->vertex(index)->data().level_vertex_id());
}

GameGraph::_GRAPH_ID CPatrolPathParams::game_vertex_id(u32 index) const
{
    if (!m_path || !m_path->vertex(index))
        return GameGraph::_GRAPH_ID(-1);

    return (m_path->vertex(index)->data().game_vertex_id());
}

u32 CPatrolPathParams::point(LPCSTR name) const
{
    if (m_path && m_path->point(name))
        return (m_path->point(name)->vertex_id());
    return (u32(-1));
}

u32 CPatrolPathParams::point(const Fvector& point) const
{
    if (!m_path || !m_path->point(point))
        return u32(-1);

    return (m_path->point(point)->vertex_id());
}
bool CPatrolPathParams::flag(u32 index, u8 flag_index) const
{
    if (!m_path || !m_path->vertex(index))
        return false;

    return (!!(m_path->vertex(index)->data().flags() & (u32(1) << flag_index)));
}

Flags32 CPatrolPathParams::flags(u32 index) const
{
    if (!m_path || !m_path->vertex(index))
        return Flags32().zero();

    return (Flags32().assign(m_path->vertex(index)->data().flags()));
}

LPCSTR CPatrolPathParams::name(u32 index) const
{
    if (!m_path || !m_path->vertex(index))
        return "";

    return m_path->vertex(index)->data().name().c_str();
}

bool CPatrolPathParams::terminal(u32 index) const
{
    if (!m_path || !m_path->vertex(index))
        return true;


    return (m_path->vertex(index)->edges().size() == 0);
}
