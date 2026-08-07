////////////////////////////////////////////////////////////////////////////
//	Module 		: alife_smart_terrain_registry.cpp
//	Created 	: 20.09.2005
//  Modified 	: 20.09.2005
//	Author		: Dmitriy Iassenev
//	Description : ALife smart terrain registry
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "alife_smart_terrain_registry.h"
#include "xrServer_Objects_ALife_Monsters.h"

CALifeSmartTerrainRegistry::~CALifeSmartTerrainRegistry() {}
void CALifeSmartTerrainRegistry::add(CSE_ALifeDynamicObject* object)
{
    CSE_ALifeSmartZone* zone = smart_cast<CSE_ALifeSmartZone*>(object);
    if (!zone)
        return;

    VERIFY(objects().find(object->ID) == objects().end());
    m_objects.insert(std::make_pair(object->ID, zone));
}

void CALifeSmartTerrainRegistry::remove(CSE_ALifeDynamicObject* object)
{
    CSE_ALifeSmartZone* zone = smart_cast<CSE_ALifeSmartZone*>(object);
    if (!zone)
        return;

    OBJECTS::iterator I = m_objects.find(object->ID);

    // [DA_PORT] Проверка вместо VERIFY: тот исчезает в релизной сборке.
    //
    // Было `VERIFY(I != m_objects.end()); m_objects.erase(I);` — то есть в релизе при ненайденной
    // записи выполнялось erase(end()). Это не «ничего не делает», а неопределённое поведение:
    // дерево std::map правится по невалидному узлу, и рушится потом в произвольном месте.
    //
    // Отсутствие записи здесь — законный случай, а не ошибка: зону могли снять с учёта раньше по
    // другому пути, а до нас дойти повторно (освобождение рекурсивно, см. CALifeSimulatorBase::release).
    if (I == m_objects.end())
        return;

    m_objects.erase(I);
}
