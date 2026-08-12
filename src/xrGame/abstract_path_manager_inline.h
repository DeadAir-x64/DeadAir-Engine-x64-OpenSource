////////////////////////////////////////////////////////////////////////////
//	Module 		: abstract_path_manager.h
//	Created 	: 02.10.2001
//  Modified 	: 19.11.2003
//	Author		: Dmitriy Iassenev
//	Description : Abstract path manager inline functions
////////////////////////////////////////////////////////////////////////////

#pragma once

#include "ai_space.h"
#include "xrAICore/Navigation/graph_engine.h"

#define TEMPLATE_SPECIALIZATION \
    template <typename _Graph, typename _VertexEvaluator, typename _vertex_id_type, typename _index_type>

#define CPathManagerTemplate CAbstractPathManager<_Graph, _VertexEvaluator, _vertex_id_type, _index_type>

TEMPLATE_SPECIALIZATION
IC CPathManagerTemplate::CAbstractPathManager(CRestrictedObject* object) : m_object(object) {}

TEMPLATE_SPECIALIZATION
IC void CPathManagerTemplate::reinit(const _Graph* graph)
{
    m_actuality = false;
    m_failed = false;
    m_evaluator = 0;
    m_graph = graph;
    m_current_index = _index_type(-1);
    m_intermediate_index = _index_type(-1);
    m_dest_vertex_id = _index_type(-1);
    m_path.clear();
    m_failed_start_vertex_id = _vertex_id_type(-1);
    m_failed_dest_vertex_id = _vertex_id_type(-1);
}

TEMPLATE_SPECIALIZATION
IC void CPathManagerTemplate::build_path(const _vertex_id_type start_vertex_id, const _vertex_id_type dest_vertex_id)
{
    // [DA_PORT] The VERIFY below is compiled out in Release, and CLevelGraph::vertex() is
    // plain pointer arithmetic with no bounds check of its own (it cannot afford one - it
    // runs per node). An out-of-range vertex id therefore used to walk off the node array
    // instead of failing the request. Report it and take the same route as a failed search;
    // before_search/after_search are skipped as a pair, so the border stays balanced.
    if (!m_graph || !m_evaluator || !m_graph->valid_vertex_id(start_vertex_id) ||
        !m_graph->valid_vertex_id(dest_vertex_id))
    {
        static u32 reported = 0;
        if (reported < 8)
        {
            ++reported;
            Msg("! [DA] build_path: vertex id out of range, start [%u] dest [%u]%s", u32(start_vertex_id),
                u32(dest_vertex_id), (reported == 8) ? " (further reports suppressed)" : "");
        }

        m_failed = true;
        m_current_index = _index_type(-1);
        m_intermediate_index = _index_type(-1);
        m_actuality = !failed();
        return;
    }

    if ((m_failed_start_vertex_id == start_vertex_id) && (m_failed_dest_vertex_id == dest_vertex_id))
    {
        before_search(start_vertex_id, dest_vertex_id);
        m_failed = true;
        after_search();
        m_current_index = _index_type(-1);
        m_intermediate_index = _index_type(-1);
        m_actuality = !failed();
        return;
    }

    before_search(start_vertex_id, dest_vertex_id);
    m_failed = !ai().graph_engine().search(*m_graph, start_vertex_id, dest_vertex_id, &m_path, *m_evaluator);
    after_search();
    m_current_index = _index_type(-1);
    m_intermediate_index = _index_type(-1);
    m_actuality = !failed();

    if (!m_failed)
        return;

    m_failed_start_vertex_id = start_vertex_id;
    m_failed_dest_vertex_id = dest_vertex_id;
}

TEMPLATE_SPECIALIZATION
IC void CPathManagerTemplate::select_intermediate_vertex()
{
    VERIFY(!failed() && !m_path.empty());
    m_intermediate_index = m_path.size() - 1;
}

TEMPLATE_SPECIALIZATION
IC _vertex_id_type CPathManagerTemplate::intermediate_vertex_id() const
{
    VERIFY(m_intermediate_index < m_path.size());
    return (m_path[intermediate_index()]);
}

TEMPLATE_SPECIALIZATION
IC u32 CPathManagerTemplate::intermediate_index() const { return (m_intermediate_index); }
TEMPLATE_SPECIALIZATION
IC bool CPathManagerTemplate::actual(
    const _vertex_id_type /*start_vertex_id*/, const _vertex_id_type /*dest_vertex_id*/) const
{
    return (m_actuality);
}

TEMPLATE_SPECIALIZATION
IC bool CPathManagerTemplate::completed() const { return (m_intermediate_index == m_path.size() - 1); }
TEMPLATE_SPECIALIZATION
IC bool CPathManagerTemplate::failed() const { return (m_failed); }
TEMPLATE_SPECIALIZATION
IC void CPathManagerTemplate::set_evaluator(_VertexEvaluator* evaluator)
{
    if ((evaluator != m_evaluator) || !m_evaluator->actual())
        m_actuality = false;
    m_evaluator = evaluator;
}

TEMPLATE_SPECIALIZATION
IC const typename CPathManagerTemplate::PATH& CPathManagerTemplate::path() const { return (m_path); }
TEMPLATE_SPECIALIZATION
IC _vertex_id_type CPathManagerTemplate::dest_vertex_id() const { return (m_dest_vertex_id); }
TEMPLATE_SPECIALIZATION
IC void CPathManagerTemplate::set_dest_vertex(const _vertex_id_type vertex_id, const char* da_from)
{
    // [DA_PORT] Прибор: кто назначает целью НУЛЕВУЮ вершину уровня.
    //
    // В Баре трое сталкеров Долга ищут дорогу в вершину 0 — дальний угол карты, метров за 530, —
    // и упираются в потолок поиска ~849 раз за заход. Ноль там не «цель не выставлена»: незаданная
    // цель это u32(-1), её ловит проверка в build_path, и она не срабатывала ни разу. Значит ноль
    // положили осознанно, и вопрос ровно один — где.
    //
    // Мест, откуда назначается цель уровня, шесть, и по логу их не различить: сообщение об упоре
    // печатается уже в поиске, к тому времени вызвавшего не видно. Поэтому метку места передаём
    // сюда явным аргументом. Нулевая вершина сама по себе законна, поэтому это не отказ и не
    // защита — только запись в лог, чтобы следующий заход назвал виновника вместо догадок.
    //
    // ⚠️ Метка ставится ТОЛЬКО на путях уровня. Для графа игры вершина 0 — обычная точка, и там
    // da_from не передаётся, иначе лог зальёт ложными срабатываниями.
    if (da_from && vertex_id == _vertex_id_type(0))
    {
        static u32 da_hits = 0;
        ++da_hits;
        if (da_hits <= 10 || (da_hits % 500) == 0)
            Msg("! [DA] целью пути уровня назначена вершина 0, место вызова [%s] (случаев %u)", da_from,
                da_hits);
    }

    VERIFY(check_vertex(vertex_id));
    m_actuality = m_actuality && (dest_vertex_id() == vertex_id);
    m_dest_vertex_id = vertex_id;
}

TEMPLATE_SPECIALIZATION
IC const _VertexEvaluator* CPathManagerTemplate::evaluator() const { return (m_evaluator); }
TEMPLATE_SPECIALIZATION
IC void CPathManagerTemplate::make_inactual() { m_actuality = false; }
TEMPLATE_SPECIALIZATION
IC void CPathManagerTemplate::before_search(const _vertex_id_type start_vertex_id, const _vertex_id_type dest_vertex_id)
{
}

TEMPLATE_SPECIALIZATION
IC void CPathManagerTemplate::after_search() {}
TEMPLATE_SPECIALIZATION
IC bool CPathManagerTemplate::check_vertex(const _vertex_id_type vertex_id) const
{
    return (m_graph->valid_vertex_id(vertex_id));
}

TEMPLATE_SPECIALIZATION
IC CRestrictedObject& CPathManagerTemplate::object() const
{
    VERIFY(m_object);
    return (*m_object);
}

TEMPLATE_SPECIALIZATION
IC void CPathManagerTemplate::reset() { m_failed = false; }
TEMPLATE_SPECIALIZATION
IC void CPathManagerTemplate::invalidate_failed_info()
{
    reset();
    m_failed_start_vertex_id = u32(-1);
    m_failed_dest_vertex_id = u32(-1);
}

#undef CPathManagerTemplate
#undef TEMPLATE_SPECIALIZATION
