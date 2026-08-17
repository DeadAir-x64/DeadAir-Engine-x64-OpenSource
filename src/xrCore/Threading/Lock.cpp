#include "stdafx.h"
#include "Lock.hpp"
#include <mutex>

struct LockImpl
{
#ifdef XR_PLATFORM_WINDOWS
    CRITICAL_SECTION cs;

    LockImpl() { InitializeCriticalSection(&cs); }
    ~LockImpl() { DeleteCriticalSection(&cs); }

    ICF void Lock() { EnterCriticalSection(&cs); }
    ICF void Unlock() { LeaveCriticalSection(&cs); }
    ICF bool TryLock() { return !!TryEnterCriticalSection(&cs); }
#else
    std::recursive_mutex mutex;

    ICF void Lock() { mutex.lock(); }
    ICF void Unlock() { mutex.unlock(); }
    ICF bool TryLock() { return mutex.try_lock(); }
#endif
};

#ifdef CONFIG_PROFILE_LOCKS
static add_profile_portion_callback add_profile_portion = 0;
void set_add_profile_portion(add_profile_portion_callback callback) { add_profile_portion = callback; }
struct profiler
{
    u64 m_time;
    pcstr m_timer_id;

    IC profiler::profiler(pcstr timer_id)
    {
        if (!add_profile_portion)
            return;

        m_timer_id = timer_id;
        m_time = CPU::QPC();
    }

    IC profiler::~profiler()
    {
        if (!add_profile_portion)
            return;

        u64 time = CPU::QPC();
        (*add_profile_portion)(m_timer_id, time - m_time);
    }
};

Lock::Lock(const char* id) : impl(xr_new<LockImpl>()), lockCounter(0), id(id) {}

void Lock::Enter()
{
#if 0 // def DEBUG
    static bool show_call_stack = false;
    if (show_call_stack)
        OutputDebugStackTrace("----------------------------------------------------");
#endif // DEBUG
    profiler temp(id);
    mutex.lock();
    isLocked = true;
}
#else
// [DA_PORT] Lock out-of-line: GCC LTO was eliding the inline Lock() ctor for members
// constructed across DLL boundaries (e.g. PlayersMonitor::csPlayers), leaving
// CRITICAL_SECTION zeroed -> SIGSEGV in RtlEnterCriticalSection. Out-of-line forces
// the ctor symbol to be imported and called.
#ifdef XR_PLATFORM_WINDOWS
Lock::Lock() { InitializeCriticalSection(&cs); }

Lock::~Lock() { DeleteCriticalSection(&cs); }

Lock::Lock(Lock&& other) noexcept(false)
{
    (void)other;
    InitializeCriticalSection(&cs);
}

Lock& Lock::operator=(Lock&& other) noexcept(false)
{
    (void)other;
    return *this;
}

void Lock::Enter() { EnterCriticalSection(&cs); }
bool Lock::TryEnter() { return !!TryEnterCriticalSection(&cs); }
void Lock::Leave() { LeaveCriticalSection(&cs); }
#else
// [DA_PORT] Не-Windows: тот же договор на std::recursive_mutex. Разбор — у поля cs в Lock.hpp.
//
// Перемещение повторяет поведение ветки Windows: чужой замок НЕ переносится, у нового объекта свой
// собственный. Так было и там — InitializeCriticalSection на приёмнике, источник не трогается.
Lock::Lock() = default;
Lock::~Lock() = default;

Lock::Lock(Lock&& other) noexcept(false) { (void)other; }

Lock& Lock::operator=(Lock&& other) noexcept(false)
{
    (void)other;
    return *this;
}

void Lock::Enter() { cs.lock(); }
bool Lock::TryEnter() { return cs.try_lock(); }
void Lock::Leave() { cs.unlock(); }
#endif // XR_PLATFORM_WINDOWS
#endif // CONFIG_PROFILE_LOCKS

#ifdef DEBUG
extern void OutputDebugStackTrace(const char* header);
#endif