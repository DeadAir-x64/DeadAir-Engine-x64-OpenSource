////////////////////////////////////////////////////////////////////////////
//	Module 		: alife_smart_terrain_registry_inline.h
//	Created 	: 20.09.2005
//  Modified 	: 20.09.2005
//	Author		: Dmitriy Iassenev
//	Description : ALife smart terrain registry inline functions
////////////////////////////////////////////////////////////////////////////

#pragma once

IC const CALifeSmartTerrainRegistry::OBJECTS& CALifeSmartTerrainRegistry::objects() const { return (m_objects); }
IC CSE_ALifeSmartZone* CALifeSmartTerrainRegistry::object(const ALife::_OBJECT_ID& id, bool no_assert) const
{
    OBJECTS::const_iterator I = objects().find(id);
    // [DA_PORT] В релизе VERIFY вырезан: отсутствие умного места в реестре разыменовывало
    // итератор end() и возвращало мусор. Возвращаем nullptr; штатные вызовы (no_assert=false)
    // по-прежнему падают в отладке, но не роняют релиз.
    if (I == objects().end())
    {
        VERIFY(no_assert);
        return nullptr;
    }
    return ((*I).second);
}
