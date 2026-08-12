////////////////////////////////////////////////////////////////////////////
//	Module 		: level_path_manager.h
//	Created 	: 02.10.2001
//  Modified 	: 12.11.2003
//	Author		: Dmitriy Iassenev
//	Description : Level path manager inline functions
////////////////////////////////////////////////////////////////////////////

#pragma once

#include "xrEngine/profiler.h"

#define TEMPLATE_SPECIALIZATION \
    template <typename _VertexEvaluator, typename _vertex_id_type, typename _index_type>

#define CLevelManagerTemplate CBaseLevelPathManager<_VertexEvaluator, _vertex_id_type, _index_type>

TEMPLATE_SPECIALIZATION
IC CLevelManagerTemplate::CBaseLevelPathManager(CRestrictedObject* object) : inherited(object) {}

TEMPLATE_SPECIALIZATION
IC void CLevelManagerTemplate::reinit(const CLevelGraph* graph) { inherited::reinit(graph); }

TEMPLATE_SPECIALIZATION
IC bool CLevelManagerTemplate::actual() const
{
    return (inherited::actual(this->m_object->object().ai_location().level_vertex_id(), this->dest_vertex_id()));
}

TEMPLATE_SPECIALIZATION
IC void CLevelManagerTemplate::build_path(const _vertex_id_type start_vertex_id, const _vertex_id_type dest_vertex_id)
{
    START_PROFILE("Build Path/Level Path");

    THROW(ai().level_graph().valid_vertex_id(start_vertex_id) && ai().level_graph().valid_vertex_id(dest_vertex_id));

    // [DA_PORT] Заведомо недостижимая цель отсекается ЗДЕСЬ, до поиска.
    //
    // Неудачный поиск дороже удачного: чтобы заключить «дороги нет», A* обязан обойти всё
    // достижимое. Компоненты связности графа посчитаны один раз при загрузке уровня, поэтому
    // ответ стоит сравнение двух чисел (см. CLevelGraph::da_build_components).
    //
    // Отказываем ТОЛЬКО при разных компонентах: рестрикторы доступность лишь отнимают, значит
    // «разные компоненты» — это «не дойти никогда», а «одна компонента» ещё ничего не обещает.
    // Ложных отказов при таком порядке не бывает.
    //
    // before_search/after_search пропускаются ПАРОЙ — граница рестрикций остаётся сбалансированной,
    // ровно как в проверке недопустимого номера вершины этажом ниже.
    extern int ps_da_path_islands;
    extern int ps_da_path_max_nodes;
    if (ps_da_path_islands && ai().level_graph().da_unreachable(start_vertex_id, dest_vertex_id))
    {
        this->m_failed = true;
        this->m_current_index = _index_type(-1);
        this->m_intermediate_index = _index_type(-1);
        this->m_actuality = !this->failed();
        // 🪤 STOP_PROFILE здесь звать НЕЛЬЗЯ: этот макрос — закрывающая скобка, а не вызов.
        // Он закрыл бы блок, открытый START_PROFILE, и парный STOP_PROFILE в конце функции
        // съел бы скобку тела. Обычного return достаточно: блок закрывается сам, а замер
        // в отладочной сборке снимается деструктором.
        return;
    }

    inherited::build_path(start_vertex_id, dest_vertex_id);

    // [DA_PORT] Поиск умер РОВНО НА ПОТОЛКЕ — значит потолок мал, а не дороги нет.
    //
    // Это и есть то, что позволяет ставить потолок низко. Без такой строки отказ по исчерпанию
    // бюджета неотличим от честного «не дойти»: NPC просто не идёт, ошибки нет, в логе пусто, и
    // ловить это приходится по жалобе. Со строкой мы узнаём об этом из лога тестера — и тогда
    // число подбирается по данным, а не по запасу «на всякий случай».
    //
    // Замеры худшего ЧЕСТНОГО поиска: болота 1701, Юпитер 4919, Затон 8753 узлов.
    if (this->failed() && g_da_lp_last_visited >= u32(ps_da_path_max_nodes))
    {
        static u32 reported = 0;
        if (reported < 8)
        {
            ++reported;
            const Fvector p = ai().level_graph().vertex_position(start_vertex_id);
            // Координаты ЦЕЛИ печатаются наравне с началом: номер вершины сам по себе ни о чём не
            // говорит, а место на карте сразу показывает, осмысленная это цель или мусорная.
            const bool dest_ok = ai().level_graph().valid_vertex_id(dest_vertex_id);
            const Fvector d = dest_ok ? ai().level_graph().vertex_position(dest_vertex_id) : Fvector{};
            Msg("! [DA] поиск пути упёрся в потолок da_path_max_nodes %d: [%s] из (%.1f, %.1f, %.1f) "
                "в вершину %u (%.1f, %.1f, %.1f)%s%s",
                ps_da_path_max_nodes,
                this->m_object ? this->m_object->object().cName().c_str() : "неизвестно",
                VPUSH(p), u32(dest_vertex_id), VPUSH(d),
                dest_ok ? "" : " ВЕРШИНА НЕДОПУСТИМА",
                (reported == 8) ? " (дальнейшие сообщения подавлены)" : "");
        }
    }

#ifdef DEBUG
    if (this->failed())
    {
        Msg("! NPC %s couldn't build path from \n~ [%d][%f][%f][%f]\n~ to\n~ [%d][%f][%f][%f]",
            this->m_object->object().cName().c_str(), start_vertex_id, VPUSH(ai().level_graph().vertex_position(start_vertex_id)),
            dest_vertex_id, VPUSH(ai().level_graph().vertex_position(dest_vertex_id)));
    }
#endif

    STOP_PROFILE;
}

TEMPLATE_SPECIALIZATION
IC void CLevelManagerTemplate::before_search(
    const _vertex_id_type start_vertex_id, const _vertex_id_type dest_vertex_id)
{
    if (this->m_object)
    {
        this->m_object->add_border(start_vertex_id, dest_vertex_id);
        VERIFY(!this->m_object->applied() || ai().level_graph().is_accessible(start_vertex_id));
        VERIFY(!this->m_object->applied() || ai().level_graph().is_accessible(dest_vertex_id));
    }
}

TEMPLATE_SPECIALIZATION
IC void CLevelManagerTemplate::after_search()
{
    if (this->m_object)
        this->m_object->remove_border();
}

TEMPLATE_SPECIALIZATION
IC bool CLevelManagerTemplate::check_vertex(const _vertex_id_type vertex_id) const
{
    return (inherited::check_vertex(vertex_id) && (!this->m_object || this->object().accessible(vertex_id)));
}

TEMPLATE_SPECIALIZATION
IC void CLevelManagerTemplate::on_restrictions_change()
{
    this->m_failed_start_vertex_id = _vertex_id_type(-1);
    this->m_failed_dest_vertex_id = _vertex_id_type(-1);
}

#undef TEMPLATE_SPECIALIZATION
#undef CLevelManagerTemplate
