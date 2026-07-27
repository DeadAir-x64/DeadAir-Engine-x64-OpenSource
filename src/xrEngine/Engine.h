#pragma once

#ifdef XRAY_STATIC_BUILD
#    define ENGINE_API
#else
#    ifdef ENGINE_BUILD
#        define ENGINE_API XR_EXPORT
#    else
#        define ENGINE_API XR_IMPORT
#    endif
#endif

#include "pure.h"
#include "EngineAPI.h"
#include "EventAPI.h"
#include "xrSheduler.h"
#include "xrSound/Sound.h"

// TODO: this should be in render configuration
#define R__NUM_SUN_CASCADES         (3u) // csm/s.ligts
#define R__NUM_AUX_CONTEXTS         (1u) // rain/s.lights
#define R__NUM_PARALLEL_CONTEXTS    (R__NUM_SUN_CASCADES + R__NUM_AUX_CONTEXTS)
#define R__NUM_CONTEXTS             (R__NUM_PARALLEL_CONTEXTS + 1/* imm */)

class ENGINE_API CEngine final : public pureFrame, public IEventReceiver
{
    EVENT eQuit;

public:
    // DLL api stuff
    CEngineAPI External;
    CEventAPI Event;
    CSheduler Sheduler;
    CSoundManager Sound;

    void Initialize(GameModule* game, const std::array<RendererModule*, 2>& modules);
    void Destroy();

    void OnEvent(EVENT E, u64 P1, u64 P2) override;
    void OnFrame() override;

    CEngine();
    ~CEngine();
};

ENGINE_API extern CEngine Engine;

// [DA_PORT] Was the game started with "-dev"?
//
// One build serves both audiences. Players get it without the flag: the cheat and developer console
// commands are simply never registered, so the console answers "unknown command" the way a retail
// build would. We start with the flag and have everything.
//
// Deliberately NOT the stock ReleaseMasterGold configuration, which is how the original hid the same
// commands. That configuration also compiles exceptions out, and with them the recovery: a Lua error
// currently throws and is caught in half a dozen places - the script binder clears the failed object
// and play continues - whereas without exceptions the same error goes straight to xrDebug::Fatal and
// closes the game. For a mod carrying several hundred scripts that trade is the wrong way round, so
// stability stays for everyone and only the commands are hidden.
ENGINE_API bool da_dev_mode();
