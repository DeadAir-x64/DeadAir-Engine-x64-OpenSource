#include "stdafx.h"
#include "xrCommon/xr_vector.h"

XRCORE_API bool g_bEnableStatGather = false;

// [DA_PORT] Counters for the GOAP planner, filled in problem_solver_inline.h and printed by the
// performance dump. Live here because that header is included from several modules and the figures
// have to add up across all of them.
XRCORE_API double g_da_goap_actual_ms = 0.0;
XRCORE_API double g_da_goap_search_ms = 0.0;
XRCORE_API u32 g_da_goap_calls = 0;
XRCORE_API u32 g_da_goap_searches = 0;
XRCORE_API double g_da_goap_exec_ms = 0.0;
XRCORE_API u32 g_da_goap_execs = 0;

// [DA_PORT] Разбор ПРОВЕРКИ СВОЙСТВ МИРА по номеру свойства: da_goap_dump <кадров>.
//
// Замер показал: у GOAP дорога не поисковая часть (0.05 мс на вызов), а проверка актуальности плана
// перед ней — 0.85 мс на кадр при худшем вызове 9.9 мс. Проверка перебирает свойства мира и на
// каждое зовёт вычислитель; какой именно взрывается — видно только поимённо.
//
// Массив, а не словарь: номера свойств мелкие и плотные, а код в горячем цикле.
XRCORE_API double g_da_goap_prop_ms[DA_GOAP_PROPS] = {};
XRCORE_API double g_da_goap_prop_max[DA_GOAP_PROPS] = {};
XRCORE_API u32 g_da_goap_prop_calls[DA_GOAP_PROPS] = {};
XRCORE_API int ps_da_goap_dump = 0;
XRCORE_API u32 g_da_goap_prop_frames = 0;
XRCORE_API double g_da_goap_prop_over_ms = 0.0;
XRCORE_API u32 g_da_goap_prop_over_calls = 0;
XRCORE_API da_goap_kind g_da_goap_kinds[DA_GOAP_KINDS] = {};
XRCORE_API u32 g_da_goap_kinds_used = 0;
XRCORE_API double g_da_oh_ms = 0.0;
XRCORE_API u32 g_da_oh_entries = 0;
XRCORE_API u32 g_da_oh_alive = 0;
XRCORE_API u32 g_da_oh_throws = 0;
XRCORE_API double g_da_lpb_ms = 0.0;
XRCORE_API u32 g_da_lpb_calls = 0;

XRCORE_API u32 g_da_lp_nodes_ok[DA_LP_BUCKETS] = {};
XRCORE_API u32 g_da_lp_nodes_fail[DA_LP_BUCKETS] = {};
XRCORE_API u32 g_da_lp_max_ok = 0;
XRCORE_API u32 g_da_lp_max_fail = 0;
XRCORE_API u64 g_da_lp_sum_ok = 0;
XRCORE_API u64 g_da_lp_sum_fail = 0;

XRCORE_API u32 g_da_lp_last_visited = 0;

XRCORE_API void da_lp_record(bool success, u32 visited_nodes)
{
    g_da_lp_last_visited = visited_nodes;

    u32 bucket = 0;
    if (visited_nodes >= 65536) bucket = 6;
    else if (visited_nodes >= 16384) bucket = 5;
    else if (visited_nodes >= 4096) bucket = 4;
    else if (visited_nodes >= 1024) bucket = 3;
    else if (visited_nodes >= 256) bucket = 2;
    else if (visited_nodes >= 64) bucket = 1;

    if (success)
    {
        ++g_da_lp_nodes_ok[bucket];
        g_da_lp_sum_ok += visited_nodes;
        if (visited_nodes > g_da_lp_max_ok)
            g_da_lp_max_ok = visited_nodes;
    }
    else
    {
        ++g_da_lp_nodes_fail[bucket];
        g_da_lp_sum_fail += visited_nodes;
        if (visited_nodes > g_da_lp_max_fail)
            g_da_lp_max_fail = visited_nodes;
    }
}

void CStatTimer::FrameStart()
{
    accum = Duration();
    count = 0;
}

void CStatTimer::FrameEnd()
{
    const float time = GetElapsed_sec() * 1000.0f;
    if (time > result)
        result = time;
    else
        result = 0.99f * result + 0.01f * time;
}

XRCORE_API pauseMngr& g_pauseMngr()
{
    static pauseMngr manager;
    return manager;
}

pauseMngr::pauseMngr() : paused(false) { m_timers.reserve(3); }
void pauseMngr::Pause(const bool b)
{
    if (paused == b)
        return;

    for (auto& timer : m_timers)
    {
        timer->Pause(b);
    }

    paused = b;
}

void pauseMngr::Register(CTimer_paused& t) { m_timers.push_back(&t); }

void pauseMngr::UnRegister(CTimer_paused& t)
{
    const auto it = std::find(m_timers.cbegin(), m_timers.cend(), &t);
    if (it != m_timers.end())
        m_timers.erase(it);
}
