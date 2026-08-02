#pragma once

// [DA_PORT] Safe lookup of a named Lua function.
//
// The engine guarded these lookups with THROW/THROW3. XRAY_EXCEPTIONS is defined by the
// build (not in the sources), so THROW does fire in Release - but it fires by throwing,
// and on the MinGW path WinMain has no handler at all. The exception reaches
// std::terminate -> xr_terminate, which reports the generic "Unexpected application
// termination" and exits. The descriptive text THROW3 assembled - the name of the missing
// function - is built into the thrown buffer and never printed by anyone.
//
// So a dialog naming a function that does not exist took the whole game down and told the
// player nothing about why. da_functor() resolves the name, and on failure names it in the
// log and lets the caller carry on with a safe default.

#include "xrScriptEngine/script_engine.hpp"
#include "xrScriptEngine/Functor.hpp"
#include "GameObject.h"

// Logs "function not found" once per unique name. Always returns false.
bool da_functor_missing(pcstr name, pcstr what, pcstr where);

template <typename TResult>
bool da_functor(pcstr name, luabind::functor<TResult>& lua_function, pcstr what, pcstr where)
{
    if (!name || !name[0])
        return da_functor_missing("<empty>", what, where);

    if (GEnv.ScriptEngine->functor(name, lua_function))
        return true;

    return da_functor_missing(name, what, where);
}
