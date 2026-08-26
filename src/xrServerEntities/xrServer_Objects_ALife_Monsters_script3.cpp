////////////////////////////////////////////////////////////////////////////
//	Module 		: xrServer_Objects_ALife_Monsters_script3.cpp
//	Created 	: 19.09.2002
//  Modified 	: 04.06.2003
//	Author		: Dmitriy Iassenev
//	Description : Server monsters for ALife simulator, script export, the second part
////////////////////////////////////////////////////////////////////////////

#include "pch_script.h"

#include "xrServer_Objects_ALife_Monsters.h"
#include "xrServer_script_macroses.h"

void CSE_ALifeCreatureActor::script_register(lua_State* luaState)
{
    using namespace luabind;

    module(luaState)
    [
        luabind_class_creature3(CSE_ALifeCreatureActor, "cse_alife_creature_actor", CSE_ALifeCreatureAbstract,
                                CSE_ALifeTraderAbstract, CSE_PHSkeleton)
    ];
}

void CSE_ALifeTorridZone::script_register(lua_State* luaState)
{
    using namespace luabind;

    module(luaState)
    [
        luabind_class_dynamic_alife2(CSE_ALifeTorridZone, "cse_torrid_zone", CSE_ALifeCustomZone, CSE_Motion)
    ];
}

void CSE_ALifeZoneVisual::script_register(lua_State* luaState)
{
    using namespace luabind;

    module(luaState)
    [
        luabind_class_dynamic_alife2(CSE_ALifeZoneVisual, "cse_zone_visual", CSE_ALifeAnomalousZone, CSE_Visual)
    ];
}

void CSE_ALifeCreaturePhantom::script_register(lua_State* luaState)
{
    using namespace luabind;

    module(luaState)
    [
        luabind_class_creature1(CSE_ALifeCreaturePhantom, "cse_alife_creature_phantom", CSE_ALifeCreatureAbstract)
    ];
}

void CSE_ALifeCreatureAbstract::script_register(lua_State* luaState)
{
    using namespace luabind;

    module(luaState)
    [
        luabind_class_creature1(CSE_ALifeCreatureAbstract, "cse_alife_creature_abstract", CSE_ALifeDynamicObjectVisual)
            .def("health", &CSE_ALifeCreatureAbstract::get_health)
            .def("alive", &CSE_ALifeCreatureAbstract::g_Alive)
            .def_readwrite("team", &CSE_ALifeCreatureAbstract::s_team)
            .def_readwrite("squad", &CSE_ALifeCreatureAbstract::s_squad)
            .def_readwrite("group", &CSE_ALifeCreatureAbstract::s_group)
            .def("o_torso", +[](CSE_ALifeCreatureAbstract* self) { return &self->o_torso; })
    ];
}

#ifdef XRGAME_EXPORTS
namespace
{
// [DA_PORT] Обращения к отряду читают вектор участников ПРЯМО по указателю, а скрипты ALife держат
// ссылки на отряды через смену уровня: доска симуляции хранит отряд, который сервер уже освободил.
// На таком указателе begin() — это то, что аллокатор оставил в блоке, и дальше идёт разыменование.
// Симптом у соседнего порта: краш через несколько секунд после перехода, чтение по адресу 0x30 из
// commander_id, вызванного из sim_squad_scripted:update.
//
// Отказ отвечает НЕЙТРАЛЬНЫМ значением — тем же, что вернул бы пустой отряд, — чтобы вызывающий
// скрипт продолжил работу штатной веткой, а не получил новую ошибку вместо краша.
//
// Перенесено по смыслу из Dead-Air-Refined (MMadmer, MIT).
CSE_ALifeOnlineOfflineGroup::MEMBERS& da_empty_members()
{
    static CSE_ALifeOnlineOfflineGroup::MEMBERS nobody;
    return nobody;
}

ALife::_OBJECT_ID da_group_commander_id(CSE_ALifeOnlineOfflineGroup* group)
{
    return script_object_usable(group, "commander_id") ? group->commander_id() : ALife::_OBJECT_ID(-1);
}

CSE_ALifeOnlineOfflineGroup::MEMBERS const& da_group_squad_members(CSE_ALifeOnlineOfflineGroup* group)
{
    return script_object_usable(group, "squad_members") ? group->squad_members() : da_empty_members();
}

u32 da_group_npc_count(CSE_ALifeOnlineOfflineGroup* group)
{
    return script_object_usable(group, "npc_count") ? group->npc_count() : 0;
}

void da_group_register_member(CSE_ALifeOnlineOfflineGroup* group, ALife::_OBJECT_ID member_id)
{
    if (script_object_usable(group, "register_member"))
        group->register_member(member_id);
}

void da_group_unregister_member(CSE_ALifeOnlineOfflineGroup* group, ALife::_OBJECT_ID member_id)
{
    if (script_object_usable(group, "unregister_member"))
        group->unregister_member(member_id);
}

void da_group_add_location_type(CSE_ALifeOnlineOfflineGroup* group, pcstr mask)
{
    if (script_object_usable(group, "add_location_type"))
        group->add_location_type(mask);
}

void da_group_clear_location_types(CSE_ALifeOnlineOfflineGroup* group)
{
    if (script_object_usable(group, "clear_location_types"))
        group->clear_location_types();
}

void da_group_force_change_position(CSE_ALifeOnlineOfflineGroup* group, Fvector position)
{
    if (script_object_usable(group, "force_change_position"))
        group->force_change_position(position);
}
} // namespace
#endif

void CSE_ALifeOnlineOfflineGroup::script_register(lua_State* luaState)
{
    using namespace luabind;
    using namespace luabind::policy;

    module(luaState)
    [
        class_<MEMBERS::value_type>("MEMBERS__value_type")
            .def_readonly("id", &MEMBERS::value_type::first)
            .def_readonly("object", &MEMBERS::value_type::second),

        luabind_class_online_offline_group2(CSE_ALifeOnlineOfflineGroup, "cse_alife_online_offline_group",
                                            CSE_ALifeDynamicObject, CSE_ALifeSchedulable)
#ifdef XRGAME_EXPORTS
            // [DA_PORT] Через обёртки, а не напрямую: они спрашивают реестр живых объектов, разбор
            // выше у da_empty_members.
            .def("register_member", &da_group_register_member)
            .def("unregister_member", &da_group_unregister_member)
            .def("commander_id", &da_group_commander_id)
            .def("squad_members", &da_group_squad_members, return_stl_iterator())
            .def("npc_count", &da_group_npc_count)
            .def("add_location_type", &da_group_add_location_type)
            .def("clear_location_types", &da_group_clear_location_types)
            .def("force_change_position", &da_group_force_change_position)
            //.def("force_change_game_vertex_id", &CSE_ALifeOnlineOfflineGroup::force_change_game_vertex_id)
            // Dead Air compat: DA's x32 engine picks squad actions engine-side. The only live
            // script caller (alun_utils.assign_squad_to_smart, debug spawn command) nils
            // current_action right before this, and sim_squad_scripted:update() re-picks an
            // action on the next tick anyway - so a no-op keeps the flow correct.
            .def("get_next_action", +[](CSE_ALifeOnlineOfflineGroup*, bool) {})
#endif
    ];
}
