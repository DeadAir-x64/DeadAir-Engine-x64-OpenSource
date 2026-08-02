#include "StdAfx.h"
#include "da_script_functor.h"

#include "xrCore/xr_types.h"

// [DA_PORT] See da_script_functor.h. Preconditions are re-evaluated every time a
// dialog list is rebuilt, so a broken name would otherwise flood the log; report
// each distinct name once per session.
bool da_functor_missing(pcstr name, pcstr what, pcstr where)
{
    static xr_vector<shared_str> reported;

    const shared_str key = name;
    for (const auto& it : reported)
    {
        if (it == key)
            return false;
    }
    reported.push_back(key);

    Msg("! [DA] %s: script function [%s] does not exist%s%s", what ? what : "script call", name,
        (where && where[0]) ? ", used by " : "", (where && where[0]) ? where : "");
    return false;
}
