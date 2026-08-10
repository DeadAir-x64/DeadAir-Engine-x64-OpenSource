////////////////////////////////////////////////////////////////////////////
//	Module 		: alife_level_registry_inline.h
//	Created 	: 15.01.2003
//  Modified 	: 12.05.2004
//	Author		: Dmitriy Iassenev
//	Description : ALife level registry inline functions
////////////////////////////////////////////////////////////////////////////

#pragma once

#include "ai_space.h"

IC CALifeLevelRegistry::CALifeLevelRegistry(const GameGraph::_LEVEL_ID& level_id) { m_level_id = level_id; }
IC GameGraph::_LEVEL_ID CALifeLevelRegistry::level_id() const { return (m_level_id); }
IC void CALifeLevelRegistry::add(CSE_ALifeDynamicObject* object)
{
    // [DA_PORT] Проверка вершины перед чтением: m_tGraphID у объекта может быть недопустимым, и
    // тогда vertex() читает за пределами массива вершин. VERIFY внутри vertex() в релизе пуст, то
    // есть защиты нет вовсе — тот же механизм, что уже стоил нам падения в _Rb_tree_decrement без
    // единого внятного кадра стека (см. разбор в alife_graph_registry.cpp).
    if (!ai().game_graph().valid_vertex_id(object->m_tGraphID))
    {
        Msg("! [DA_PORT] реестр уровня: у объекта [%s] секция[%s] id[%d] вершина графа %u при "
            "общем числе %u — на учёт не берём",
            object->name_replace(), object->s_name.c_str(), object->ID, u32(object->m_tGraphID),
            u32(ai().game_graph().header().vertex_count()));
        return;
    }

    if (ai().game_graph().vertex(object->m_tGraphID)->level_id() != level_id())
        return;

#ifdef DEBUG
    if (psAI_Flags.test(aiALife))
    {
        Msg("[LSS] adding object [%s][%d] to current level", object->name_replace(), object->ID);
    }
#endif

    // [DA_PORT] Терпимость к повтору перенесена СЮДА, в единственную точку.
    //
    // Раньше её писали у каждого вызывающего: три места в alife_graph_registry.cpp несут дословно
    // одну и ту же пятистрочную проверку. Но входов в реестр уровня шесть, и три оставались
    // открытыми — сборка состава уровня при переходе, откат при недопустимой вершине графа и
    // отцепление предмета от владельца. Последнее особенно показательно: у парной ему функции
    // attach снятие с учёта уже прикрыто, а у detach постановка — нет.
    //
    // Повтор для ALife — штатное состояние, а не поломка: объекты сознательно остаются
    // полузарегистрированными (см. отцепление участника отряда в alife_group_abstract.cpp — он
    // снимается с точки графа, но НАМЕРЕННО остаётся в карте уровня). Именно поэтому inherited::add
    // с его THROW2 здесь не годится: XRAY_EXCEPTIONS в сборке включён для всех конфигураций, кроме
    // ReleaseMasterGold, бросок доходит до std::terminate, и игрок видит «Unexpected application
    // termination» без единого слова о причине. Собранный THROW3 текст не печатает никто.
    //
    // Пропуск здесь не маскировка: объект УЖЕ на учёте, повторная постановка не могла бы ничего
    // добавить. Сообщение остаётся, потому что попадание сюда значит, что кто-то поставил на учёт,
    // не сняв, — и имя объекта единственное, что этого «кого-то» опознаёт.
    if (objects().find(object->ID) != objects().end())
    {
        Msg("! [DA_PORT] реестр уровня: объект [%s] секция[%s] id[%d] уже на учёте — повтор пропущен "
            "(поставлен без снятия)",
            object->name_replace(), object->s_name.c_str(), object->ID);
        return;
    }

    inherited::add(object->ID, object);
}

IC void CALifeLevelRegistry::remove(CSE_ALifeDynamicObject* object, bool no_assert)
{
#ifdef DEBUG
    if (psAI_Flags.test(aiALife))
    {
        Msg("[LSS] removing object [%s][%d] from current level", object->name_replace(), object->ID);
    }
#endif
    inherited::remove(object->ID, no_assert);
}

template <typename _update_predicate>
IC void CALifeLevelRegistry::update(const _update_predicate& predicate, bool const iterate_as_first_time_next_time)
{
    //	u32					object_count =
    inherited::update(predicate, iterate_as_first_time_next_time);
#ifdef FULL_LEVEL_UPDATE
    m_first_update = true;
#endif
#ifdef DEBUG
    if (psAI_Flags.test(aiALife))
    {
        //		Msg				("[LSS][OOS][%d : %d]",object_count, objects().size());
    }
#endif
}

IC CSE_ALifeDynamicObject* CALifeLevelRegistry::object(const ALife::_OBJECT_ID& id, bool no_assert) const
{
    _REGISTRY::const_iterator I = objects().find(id);
    if (I == objects().end())
    {
        THROW2(no_assert, "The spesified object hasn't been found in the current level!");
        return (0);
    }
    return ((*I).second);
}
