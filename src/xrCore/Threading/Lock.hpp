#pragma once
#include <atomic>
#include <windows.h>

#include "Common/Noncopyable.hpp"

#ifdef CONFIG_PROFILE_LOCKS
#include "xrCore.h"
typedef void (*add_profile_portion_callback)(pcstr id, const u64& time);
void XRCORE_API set_add_profile_portion(add_profile_portion_callback callback);

#define MUTEX_PROFILE_PREFIX_ID #mutexes /
#define MUTEX_PROFILE_ID(a) MACRO_TO_STRING(CONCATENIZE(MUTEX_PROFILE_PREFIX_ID, a))
#endif // CONFIG_PROFILE_LOCKS

class XRCORE_API Lock // [DA_PORT] out-of-line methods (see Lock.cpp): GCC LTO elided the
    // inline ctor for members constructed across DLL boundaries, leaving CRITICAL_SECTION
    // zeroed. Out-of-line forces the symbol to be imported and called.
{
    CRITICAL_SECTION cs; // [DA_PORT] NO default member initializer: cs{} makes GCC LTO
    // skip the Lock() constructor (it sees cs as "already initialized" via the zero-init {}),
    // so InitializeCriticalSection is never called -> CRITICAL_SECTION stays zeroed ->
    // RtlEnterCriticalSection crashes. Leaving cs uninitialized forces the ctor to run.
#ifdef CONFIG_PROFILE_LOCKS
    struct LockImpl* impl{};
    std::atomic_int lockCounter{};
    const char* id{};
#endif

public:
#ifdef CONFIG_PROFILE_LOCKS
    Lock(const char* id);
#else
    Lock();
#endif
    ~Lock();

    Lock(Lock& other) = delete;
    Lock& operator=(Lock& other) = delete;

    Lock(Lock&& other) noexcept(false);
    Lock& operator=(Lock&& other) noexcept(false);

    void Enter();
    bool TryEnter();
    void Leave();

    bool IsLocked() const { return false; }
};
