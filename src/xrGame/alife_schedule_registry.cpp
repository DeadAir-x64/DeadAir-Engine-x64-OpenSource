////////////////////////////////////////////////////////////////////////////
//	Module 		: alife_schedule_registry.сзз
//	Created 	: 15.01.2003
//  Modified 	: 12.05.2004
//	Author		: Dmitriy Iassenev
//	Description : ALife schedule registry
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "alife_schedule_registry.h"

CALifeScheduleRegistry::~CALifeScheduleRegistry() {}
void CALifeScheduleRegistry::add(CSE_ALifeDynamicObject* object)
{
    CSE_ALifeSchedulable* schedulable = smart_cast<CSE_ALifeSchedulable*>(object);
    if (!schedulable)
        return;

    if (!schedulable->need_update(object))
        return;

    // [DA_PORT] The same duplicate tolerance the graph and level registries needed.
    //
    // register_object() feeds five registries in a row and this is the third. ALife deliberately leaves
    // objects half-registered - see the group-detach path in alife_group_abstract.cpp, which registers a
    // member and then removes it from the graph point while explicitly keeping it in the level map - so
    // a second registration is a state the engine produces on purpose, not a fault.
    //
    // Without this the class of crash already fixed twice today would simply resurface here: this is a
    // CSafeMapIterator like the level registry, so a duplicate asserts in the very same place,
    // safe_map_iterator_inline.h. The other two registries in that chain (smart terrains, groups) are
    // already immune - their check is a VERIFY, which Release compiles out, and the map insert behind it
    // does nothing on a duplicate key.
    if (objects().find(object->ID) != objects().end())
    {
        Msg("! [DA_PORT] ALife: object [%s] section[%s] id[%d] already in the schedule registry - "
            "skipping (registered without being unregistered first)",
            object->name_replace(), object->s_name.c_str(), object->ID);
        return;
    }

    inherited::add(object->ID, schedulable);
}

void CALifeScheduleRegistry::remove(CSE_ALifeDynamicObject* object, bool no_assert)
{
    CSE_ALifeSchedulable* schedulable = smart_cast<CSE_ALifeSchedulable*>(object);
    if (!schedulable)
        return;

    inherited::remove(object->ID, no_assert || !schedulable->need_update(object));
}
