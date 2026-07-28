////////////////////////////////////////////////////////////////////////////
//	Module 		: xrGame.cpp
//	Created 	: 07.01.2001
//  Modified 	: 27.05.2004
//	Author		: Aleksandr Maksimchuk and Oles' Shyshkovtsov
//	Description : Defines the entry point for the DLL application.
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "xrGame.h"

#include "GamePersistent.h"
#include "object_factory.h"

#include "xrEngine/xr_level_controller.h"
#include "xrEngine/profiler.h"

#include "xrUICore/XML/xrUIXmlParser.h"
#include "xrUICore/ui_styles.h"

xrGameModule xrGame;

void CCC_RegisterCommands();

extern float g_fTimeFactor;

extern "C"
{
XRGAME_API IFactoryObject* __cdecl xrFactory_Create(CLASS_ID clsid)
{
    IFactoryObject* object = object_factory().client_object(clsid);
#ifdef DEBUG
    if (!object)
        return (0);
#endif
    // XXX nitrocaster XRFACTORY: set clsid during factory initialization
    object->GetClassId() = clsid;
    return (object);
}

XRGAME_API void __cdecl xrFactory_Destroy(IFactoryObject* O) { xr_delete(O); }
}

void xrGameModule::initialize(Factory_Create*& pCreate, Factory_Destroy*& pDestroy)
{
    ZoneScoped;

    pCreate = &xrFactory_Create;
    pDestroy = &xrFactory_Destroy;

    // [DA_PORT] Dead Air's own [alife]/time_factor is 10 (confirmed via trace) - genuinely their
    // config, not an engine bug, but it silently clobbered the "time_factor_single" console command
    // (which shares g_fTimeFactor) on every single launch, making that setting impossible to
    // actually override via user.ltx. Per project decision: stop reading it here so the persisted
    // console var (defaulting to 1x, see game_base.cpp) is the one real, always-honored setting.
    // g_fTimeFactor = pSettings->r_float("alife", "time_factor"); // XXX: find a better place

    // Fill ui style token
    UIStyles = xr_new<UIStyleManager>();
    // register console commands
    CCC_RegisterCommands();
    // register localization
    StringTable().Init();
    // keyboard binding
    CCC_RegisterInput(); // XXX: Move to xrEngine
#ifdef DEBUG
    g_profiler = xr_new<CProfiler>();
#endif

    ImGui::SetAllocatorFunctions(
        [](size_t size, void* /*user_data*/)
        {
            return xr_malloc(size);
        },
        [](void* ptr, void* /*user_data*/)
        {
            xr_free(ptr);
        }
    );
    ImGui::SetCurrentContext(Device.GetImGuiContext());
}

void xrGameModule::finalize()
{
    xr_delete(UIStyles);
    StringTable().Destroy();
    CCC_DeregisterInput(); // XXX: Remove if possible

#ifdef DEBUG
    xr_delete(g_profiler);
#endif
}

IGame_Persistent* xrGameModule::create_persistent()
{
    ZoneScoped;
    Msg("* [DA_PORT] create_persistent: before object_factory"); FlushLog();
    object_factory(); // XXX: remove this call
    Msg("* [DA_PORT] create_persistent: after object_factory, before CGamePersistent"); FlushLog();
    auto* p = xr_new<CGamePersistent>();
    Msg("* [DA_PORT] create_persistent: after CGamePersistent"); FlushLog();
    return p;
}

void xrGameModule::destroy_persistent(IGame_Persistent*& persistent)
{
    xr_delete(persistent);
}
