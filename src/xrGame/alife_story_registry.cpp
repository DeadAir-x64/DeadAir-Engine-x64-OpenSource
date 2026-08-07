////////////////////////////////////////////////////////////////////////////
//	Module 		: alife_story_registry.cpp
//	Created 	: 02.06.2004
//  Modified 	: 02.06.2004
//	Author		: Dmitriy Iassenev
//	Description : ALife story registry
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "alife_story_registry.h"
#include "xrServer_Objects_ALife.h"
#include "ai_space.h"
#include "xrAICore/Navigation/game_graph.h"

CALifeStoryRegistry::~CALifeStoryRegistry() {}
void CALifeStoryRegistry::add(ALife::_STORY_ID id, CSE_ALifeDynamicObject* object, bool no_assert)
{
    if (id == INVALID_STORY_ID)
        return;

#ifdef DEBUG
    Msg("Adding Story item ID [%u], Object [%s] at level [%s]", id, object->name_replace(),
        ai().game_graph().header().level(ai().game_graph().vertex(object->m_tGraphID)->level_id()).name().c_str());
#endif

    auto I = m_objects.find(id);
    if (I != m_objects.end())
    {
        // [DA_PORT] Сообщение вместо падения.
        //
        // Было `R_ASSERT2(no_assert, ...)`, а зовут add без второго аргумента — то есть с
        // no_assert = false, и любой дубликат сюжетного номера валил игру. R_ASSERT2, в отличие от
        // VERIFY, работает и в релизной сборке.
        //
        // Дубликат при этом штатно возможен: снятие записи (remove) ищет по m_story_id, и если
        // номер сменился уже после регистрации, старая запись остаётся висеть под прежним номером.
        // Следующий объект с этим номером приходит сюда и получает падение за чужую ошибку.
        //
        // Имя старого объекта НЕ читаем: он мог быть уже освобождён — ровно на этом падала первая
        // версия такой же диагностики в реестре объектов.
        Msg("! [DA_PORT] сюжетный реестр: номер [%d] занят, запись [%p] сохранена, новый объект %s "
            "не зарегистрирован",
            id, (void*)(*I).second, object->name_replace());
        return;
    }

    m_objects.insert(std::make_pair(id, object));
}
