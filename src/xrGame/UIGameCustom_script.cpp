#include "pch_script.h"
#include "xrScriptEngine/da_lua_singleton.hpp"

#include "xrUICore/Static/UIStatic.h"

#include "UIGameCustom.h"
#include "Level.h"

// DA: одна обёртка вместо новой на каждый вызов, см. da_lua_singleton.hpp
static da_lua_singleton<CUIGameCustom> s_da_hud;

static int da_lua_get_hud(lua_State* L)
{
    return s_da_hud.push(L, CurrentGameUI(), "get_hud");
}

void CUIGameCustom::script_register(lua_State* luaState)
{
    s_da_hud.reset();

    using namespace luabind;

    module(luaState)
    [
        class_<StaticDrawableWrapper>("StaticDrawableWrapper")
            .def_readwrite("m_endTime", &StaticDrawableWrapper::m_endTime)
            .def("wnd", &StaticDrawableWrapper::wnd),

        class_<CUIGameCustom, CDialogHolder>("CUIGameCustom")
            .def("AddDialogToRender", &CUIGameCustom::AddDialogToRender)
            .def("RemoveDialogToRender", &CUIGameCustom::RemoveDialogToRender)
            .def("AddCustomStatic", +[](CUIGameCustom* self, pcstr id) // Dead Air: 1-arg variant (scripts call hud:AddCustomStatic(id))
            {
                return self->AddCustomStatic(id, true);
            })
            .def("AddCustomStatic", +[](CUIGameCustom* self, pcstr id, bool singleInstance)
            {
                return self->AddCustomStatic(id, singleInstance);
            })
            .def("AddCustomStatic", &CUIGameCustom::AddCustomStatic)
            .def("RemoveCustomStatic", &CUIGameCustom::RemoveCustomStatic)
            .def("HideActorMenu", &CUIGameCustom::HideActorMenu)
             //Alundaio
            .def("ShowActorMenu", &CUIGameCustom::ShowActorMenu)
            .def("UpdateActorMenu", &CUIGameCustom::UpdateActorMenu)
            .def("CurrentItemAtCell", &CUIGameCustom::CurrentItemAtCell)
            //-Alundaio
            .def("HidePdaMenu", &CUIGameCustom::HidePdaMenu)
            .def("show_messages", &CUIGameCustom::ShowMessagesWindow)
            .def("hide_messages", &CUIGameCustom::HideMessagesWindow)
            .def("GetCustomStatic", &CUIGameCustom::GetCustomStatic)
            .def("update_fake_indicators", &CUIGameCustom::update_fake_indicators)
            .def("enable_fake_indicators", &CUIGameCustom::enable_fake_indicators),

        def("get_hud", +[]() -> CUIGameCustom* { return CurrentGameUI(); })
    ];

    // DA: перекрываем биндинг luabind своей функцией с кэшем обёртки
    lua_pushcfunction(luaState, &da_lua_get_hud);
    lua_setglobal(luaState, "get_hud");
}
