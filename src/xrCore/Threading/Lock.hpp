#pragma once
#include <atomic>

// [DA_PORT] Windows-путь оставлен БАЙТ В БАЙТ, остальным платформам дан запасной на std::mutex.
//
// ЗАЧЕМ. Наш x64-порт заменил кроссплатформенный замок на CRITICAL_SECTION с безусловным
// `#include <windows.h>` — по веской причине (см. комментарий у поля ниже про LTO), но заодно
// убил сборку под все прочие платформы. Обнаружилось это не рассуждением: под санитайзеры движок
// собирается в контейнере Ubuntu (в MinGW libasan нет вовсе), и первый же прогон встал на
// `windows.h: No such file or directory`.
//
// ⚠️ Ветка Windows НЕ ТРОГАЕТСЯ ни на строку: там лежит обход тонкой ошибки GCC LTO, и любая
// правка в ней — риск на пустом месте. Здесь добавлено только то, чего раньше не было вовсе.
#ifdef XR_PLATFORM_WINDOWS
#include <windows.h>
#else
#include <mutex>
#endif

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
#ifndef XR_PLATFORM_WINDOWS
    // [DA_PORT] Запасной замок для не-Windows. Рекурсивный намеренно: CRITICAL_SECTION рекурсивен,
    // и движок на это опирается — повторный Enter из того же потока обязан проходить.
    std::recursive_mutex cs;
#else
    CRITICAL_SECTION cs; // [DA_PORT] NO default member initializer: cs{} makes GCC LTO
    // skip the Lock() constructor (it sees cs as "already initialized" via the zero-init {}),
    // so InitializeCriticalSection is never called -> CRITICAL_SECTION stays zeroed ->
    // RtlEnterCriticalSection crashes. Leaving cs uninitialized forces the ctor to run.
#endif // XR_PLATFORM_WINDOWS
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
