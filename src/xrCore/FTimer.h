#pragma once

#include "Common/Noncopyable.hpp"
#include "xr_types.h"
#include "xrCommon/xr_vector.h"
#include "_math.h"
#include "log.h"
#include "Threading/ScopeLock.hpp"

#include <chrono>

class CTimer_paused;

class XRCORE_API pauseMngr : Noncopyable
{
    xr_vector<CTimer_paused*> m_timers;
    bool paused;

public:
    pauseMngr();
    bool Paused() const { return paused; }
    void Pause(const bool b);
    void Register(CTimer_paused& t);
    void UnRegister(CTimer_paused& t);
};

extern XRCORE_API pauseMngr& g_pauseMngr();

class XRCORE_API CTimerBase
{
public:
    using Clock = std::chrono::high_resolution_clock;
    using Time = std::chrono::time_point<Clock>;
    using Duration = Time::duration;

protected:
    Time startTime;
    Duration pauseDuration;
    Duration pauseAccum;
    bool paused;

public:
    constexpr CTimerBase() noexcept : startTime(), pauseDuration(), pauseAccum(), paused(false) {}

    ICF void Start()
    {
        if (paused)
            return;
        startTime = Now() - pauseAccum;
    }

    virtual Duration getElapsedTime() const
    {
        if (paused)
            return pauseDuration;
        return Now() - startTime - pauseAccum;
    }

    u64 GetElapsed_ns() const
    {
        using namespace std::chrono;
        return duration_cast<nanoseconds>(getElapsedTime()).count();
    }

    u64 GetElapsed_ms() const
    {
        using namespace std::chrono;
        return duration_cast<milliseconds>(getElapsedTime()).count();
    }

    IC float GetElapsed_sec() const
    {
        using namespace std::chrono;
        return duration_cast<duration<float>>(getElapsedTime()).count();
    }

    Time Now() const { return Clock::now(); }

    IC void Dump() const { Msg("* Elapsed time (sec): %f", GetElapsed_sec()); }
};

class XRCORE_API CTimer : public CTimerBase
{
    using inherited = CTimerBase;

    float m_time_factor;
    Duration realTime;
    Duration time;

    inline Duration getElapsedTime(const Duration& current) const
    {
        const auto delta = current - realTime;
        const double deltaD = double(delta.count());
        const double elapsedTime = deltaD * m_time_factor + .5;
        const auto result = u64(elapsedTime);
        return Duration(this->time.count() + result);
    }

public:
    constexpr CTimer() noexcept : m_time_factor(1.f), realTime(0), time(0) {}

    void Start() noexcept
    {
        if (paused)
            return;

        realTime = std::chrono::nanoseconds(0);
        time = std::chrono::nanoseconds(0);
        inherited::Start();
    }

    float time_factor() const noexcept { return m_time_factor; }
    void time_factor(const float time_factor) noexcept
    {
        const Duration current = inherited::getElapsedTime();
        time = getElapsedTime(current);
        realTime = current;
        m_time_factor = time_factor;
    }

    Duration getElapsedTime() const override
    {
        return getElapsedTime(inherited::getElapsedTime());
    }
};

class XRCORE_API CTimer_paused_ex : public CTimer
{
    Time save_clock;

public:
    CTimer_paused_ex() noexcept : save_clock() {}
    virtual ~CTimer_paused_ex() = default;
    bool Paused() const noexcept { return paused; }
    void Pause(const bool b) noexcept
    {
        if (paused == b)
            return;

        const auto current = Now();
        if (b)
        {
            save_clock = current;
            pauseDuration = CTimerBase::getElapsedTime();
        }
        else
        {
            pauseAccum += current - save_clock;
        }
        paused = b;
    }
};

class XRCORE_API CTimer_paused final : public CTimer_paused_ex
{
public:
    CTimer_paused() { g_pauseMngr().Register(*this); }
    ~CTimer_paused() override { g_pauseMngr().UnRegister(*this); }
};

extern XRCORE_API bool g_bEnableStatGather;

// [DA_PORT] See FTimer.cpp - GOAP planner accounting for the performance dump.
extern XRCORE_API double g_da_goap_actual_ms;
extern XRCORE_API double g_da_goap_search_ms;
extern XRCORE_API u32 g_da_goap_calls;
extern XRCORE_API u32 g_da_goap_searches;
extern XRCORE_API double g_da_goap_exec_ms;
extern XRCORE_API u32 g_da_goap_execs;

// [DA_PORT] См. FTimer.cpp — разбор проверки свойств мира GOAP по номеру свойства.
// 4096, а не 512: у скриптовых свойств номера крупные, и при малом массиве они сворачивались в
// нулевой слот. Тот превращался в свалку — 134 тысячи вызовов против тысячи у настоящих свойств —
// и выглядел как главный виновник. Переполнение теперь считается ОТДЕЛЬНО и печатается своей
// строкой: молча терять данные прибор не должен.
#define DA_GOAP_PROPS 4096
extern XRCORE_API double g_da_goap_prop_ms[DA_GOAP_PROPS];
extern XRCORE_API double g_da_goap_prop_max[DA_GOAP_PROPS];
extern XRCORE_API u32 g_da_goap_prop_calls[DA_GOAP_PROPS];
extern XRCORE_API int ps_da_goap_dump;
extern XRCORE_API u32 g_da_goap_prop_frames;
extern XRCORE_API double g_da_goap_prop_over_ms;
extern XRCORE_API u32 g_da_goap_prop_over_calls;

// [DA_PORT] Разбор по КЛАССУ вычислителя, а не по номеру свойства.
//
// Номера у каждого планировщика свои и накладываются: 124 у сталкера и 124 у обработчика предметов —
// разные вещи, и по числу имя не восстановить. Имя класса однозначно. Ключ — указатель на строку
// typeid, он постоянен для типа, поэтому сравнение указателей, а не строк.
struct da_goap_kind
{
    const char* name;
    double total_ms;
    double max_ms;
    u32 calls;
};
#define DA_GOAP_KINDS 128
extern XRCORE_API da_goap_kind g_da_goap_kinds[DA_GOAP_KINDS];
extern XRCORE_API u32 g_da_goap_kinds_used;
extern XRCORE_API double g_da_oh_ms;
extern XRCORE_API u32 g_da_oh_entries;
extern XRCORE_API u32 g_da_oh_alive;
extern XRCORE_API u32 g_da_oh_throws;
extern XRCORE_API double g_da_lpb_ms;
extern XRCORE_API u32 g_da_lpb_calls;

// [DA_PORT] Сколько узлов обходит поиск пути по уровню — отдельно для удачных и неудачных.
//
// Зачем: у поиска ЕСТЬ потолок (max_visited_node_count, по умолчанию 65500), но он выкручен так
// высоко, что безнадёжный поиск успевает обойти полграфа. Опустить его наугад нельзя — срежем
// настоящие дальние маршруты. Поэтому сперва распределение, потом число.
//
// Гистограмма по степеням двойки: [0..63], [64..255], [256..1К], [1К..4К], [4К..16К], [16К..64К],
// [64К и выше]. Считается всегда: это один инкремент на поиск.
#define DA_LP_BUCKETS 7
extern XRCORE_API u32 g_da_lp_nodes_ok[DA_LP_BUCKETS];
extern XRCORE_API u32 g_da_lp_nodes_fail[DA_LP_BUCKETS];
extern XRCORE_API u32 g_da_lp_max_ok;
extern XRCORE_API u32 g_da_lp_max_fail;
extern XRCORE_API u64 g_da_lp_sum_ok;
extern XRCORE_API u64 g_da_lp_sum_fail;
// Сколько узлов обошёл ПОСЛЕДНИЙ поиск. Нужен на стороне игры: потолок живёт там, и только там
// можно сказать, упёрлись мы в него или честно исчерпали достижимое.
extern XRCORE_API u32 g_da_lp_last_visited;
XRCORE_API void da_lp_record(bool success, u32 visited_nodes);
class XRCORE_API CStatTimer
{
    using Duration = CTimerBase::Duration;

public:
    CTimer T;
    Duration accum;
    float result;
    u32 count;

    CStatTimer() : T(), accum(), result(.0f), count(0) {}
    void FrameStart();
    void FrameEnd();

    ICF void Begin()
    {
        if (!g_bEnableStatGather)
            return;
        count++;
        T.Start();
    }

    ICF void End()
    {
        if (!g_bEnableStatGather)
            return;
        accum += T.getElapsedTime();
    }

    // Instead of making the entire timer thread-safe,
    // we can create stat. timers on stack and append their results
    // to the main timer
    // Takes external lock because not every timer should be multi-threaded.
    void AppendResults(Lock& lock, const CStatTimer& other) // thread-safe
    {
        if (!g_bEnableStatGather)
            return;
        ScopeLock scope(&lock);
        VERIFY2(fis_zero(other.result), "Appended timer is supposed to not have frame result.");
        accum += other.accum;
        count += other.count;
    }

    Duration getElapsedTime() const { return accum; }

    u64 GetElapsed_ns() const
    {
        using namespace std::chrono;
        return duration_cast<nanoseconds>(getElapsedTime()).count();
    }

    u64 GetElapsed_ms() const
    {
        using namespace std::chrono;
        return duration_cast<milliseconds>(getElapsedTime()).count();
    }

    float GetElapsed_sec() const
    {
        using namespace std::chrono;
        return duration_cast<duration<float>>(getElapsedTime()).count();
    }
};

class ScopeStatTimer : public CStatTimer
{
    CStatTimer& baseTimer;
    Lock& baseTimerLock;

public:
    ScopeStatTimer(CStatTimer& base, Lock& lock) : baseTimer(base), baseTimerLock(lock)
    {
        if (!g_bEnableStatGather)
            return;
        Begin();
    }

    ~ScopeStatTimer()
    {
        if (!g_bEnableStatGather)
            return;
        End();
        baseTimer.AppendResults(baseTimerLock, *this);
    }
};
