////////////////////////////////////////////////////////////////////////////
//	Module 		: alife_group_registry.cpp
//	Created 	: 28.10.2005
//  Modified 	: 28.10.2005
//	Author		: Dmitriy Iassenev
//	Description : ALife group registry
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "alife_group_registry.h"
#include "xrServer_Objects_ALife_Monsters.h"

CALifeGroupRegistry::~CALifeGroupRegistry() {}
void CALifeGroupRegistry::add(CSE_ALifeDynamicObject* object)
{
    CSE_ALifeOnlineOfflineGroup* group = smart_cast<CSE_ALifeOnlineOfflineGroup*>(object);
    if (!group)
        return;

    VERIFY(objects().find(group->ID) == objects().end());
    m_objects.insert(std::make_pair(group->ID, group));
}

void CALifeGroupRegistry::remove(CSE_ALifeDynamicObject* object)
{
    CSE_ALifeOnlineOfflineGroup* group = smart_cast<CSE_ALifeOnlineOfflineGroup*>(object);
    if (!group)
        return;

    OBJECTS::iterator I = m_objects.find(group->ID);

    // [DA_PORT] Проверка вместо VERIFY — тот же случай, что в alife_smart_terrain_registry.cpp:
    // VERIFY исчезает в релизе, и erase(end()) правит дерево std::map по невалидному узлу. Ломается
    // не здесь, а потом и в стороне. Отряд мог быть снят с учёта раньше по другому пути.
    if (I == m_objects.end())
        return;

    m_objects.erase(I);
}

CALifeGroupRegistry::OBJECT& CALifeGroupRegistry::object(const ALife::_OBJECT_ID& id) const
{
    OBJECTS::const_iterator I = objects().find(id);
    VERIFY(I != objects().end());
    return (*(*I).second);
}

// [DA_PORT] Группы может уже не быть, и это норма, а не ошибка.
//
// Отряд и его бойцы удаляются скриптами через safe_release_manager: тот копит объекты в таблицу и
// снимает их обходом pairs(), а порядок обхода таблицы в Lua ПРОИЗВОЛЕН — отряд запросто уходит
// раньше своих бойцов. Боец при снятии дёргает свою группу (on_unregister, kill,
// notify_on_member_death), и группы к этому моменту уже нет.
//
// Прежний object() на такой случай отвечал VERIFY, а он в релизной сборке вырезан целиком: код
// разыменовывал end() карты и вызывал по мусору метод. Это и есть падение «исполнение по адресу
// 0000000000000000» с пустым стеком — виртуальный вызов по невалидной памяти.
CALifeGroupRegistry::OBJECT* CALifeGroupRegistry::object_safe(const ALife::_OBJECT_ID& id) const
{
    const OBJECTS::const_iterator I = objects().find(id);
    if (I == objects().end())
        return nullptr;

    return ((*I).second);
}

void CALifeGroupRegistry::on_after_game_load()
{
    OBJECTS::iterator I = m_objects.begin();
    OBJECTS::iterator E = m_objects.end();
    for (; I != E; ++I)
        (*I).second->on_after_game_load();
}
