#include "stdafx.h"
#include "string_concatenations.h"
#include <windows.h>

#if !defined(XR_PLATFORM_WINDOWS) // XXX: remove or cleanup
int _cdecl _resetstkoflw(void)
{
    return 0;
}
#endif

namespace xray
{
namespace core
{
namespace detail
{
namespace strconcat_error
{
void process(u32 const index, u32 const count, pcstr* strings)
{
    u32 const max_string_size = 1024;
    pstr temp = (pstr)xr_alloca((count * (max_string_size + 4) + 1) * sizeof(**strings));
    pstr k = temp;
    *k++ = '[';
    for (u32 i = 0; i < count; ++i)
    {
        for (pcstr j = strings[i], e = j + max_string_size; *j && j < e; ++k, ++j)
            *k = *j;

        *k++ = ']';

        if (i + 1 >= count)
            continue;

        *k++ = '[';
        *k++ = '\r';
        *k++ = '\n';
    }
    *k = 0;

    xrDebug::Fatal(
        DEBUG_INFO, make_string("buffer overflow: cannot concatenate strings(%d):\r\n%s", index, temp).c_str());
}

template <u32 count>
static inline void process(pstr& i, pcstr e, u32 const index, pcstr (&strings)[count])
{
    VERIFY(i <= e);
    VERIFY(index < count);

    if (i != e)
        return;

#ifndef MASTER_GOLD
    process(index, count, strings);
#else // #ifndef MASTER_GOLD
    --i;
#endif // #ifndef MASTER_GOLD
}

} // namespace strconcat_error

int stack_overflow_exception_filter(int exception_code)
{
    if (exception_code == static_cast<int>(EXCEPTION_STACK_OVERFLOW))
    {
        // Do not call _resetstkoflw here, because
        // at this point, the stack is not yet unwound.
        // Instead, signal that the handler (the __except block)
        // is to be executed.
        return EXCEPTION_EXECUTE_HANDLER;
    }
    else
        return EXCEPTION_CONTINUE_SEARCH;
}

void check_stack_overflow(u32 stack_increment)
{
#if defined(XR_PLATFORM_WINDOWS) && defined(XR_COMPILER_MSVC)
    __try
    {
        void* p = xr_alloca(stack_increment);
        (void)p;
    }
    __except (xray::core::detail::stack_overflow_exception_filter(GetExceptionCode()))
    {
        _resetstkoflw();
    }
#elif defined(XR_PLATFORM_WINDOWS)
    // Use GetCurrentThreadStackLimits (available on modern Windows) to compute
    // whether the requested stack increment would cross the guard/limit and
    // call _resetstkoflw() to mirror MSVC behavior.
    HMODULE hKernel = GetModuleHandleA("kernel32.dll");
    if (hKernel)
    {
        typedef VOID(WINAPI* GetCurrentThreadStackLimits_t)(PULONG_PTR, PULONG_PTR);
        GetCurrentThreadStackLimits_t pGetLimits =
            reinterpret_cast<GetCurrentThreadStackLimits_t>(GetProcAddress(hKernel, "GetCurrentThreadStackLimits"));
        if (pGetLimits)
        {
            ULONG_PTR low = 0, high = 0;
            pGetLimits(&low, &high);
            char probe;
            uintptr_t sp = reinterpret_cast<uintptr_t>(&probe);
            if (sp - static_cast<uintptr_t>(stack_increment) < static_cast<uintptr_t>(low))
            {
                Msg("! [DA_PORT] check_stack_overflow: stack near limit (sp=0x%IX low=0x%IX inc=%u)",
                    sp, low, stack_increment);
            }
            return;
        }
    }
    // Fallback: conservative no-op if OS API unavailable.
    (void)stack_increment;
#endif
}

void string_tupples::error_process() const
{
    pcstr strings[MAX_ITEM_COUNT];

    u32 part_size = 0;
    u32 overrun_string_index = (u32)-1;
    for (u32 i = 0; i < m_count; ++i)
    {
        strings[i] = m_strings[i].first;

        if (overrun_string_index == (u32)-1)
        {
            part_size += m_strings[i].second;
            if (part_size > MAX_CONCAT_RESULT_SIZE)
            {
                overrun_string_index = i;
            }
        }
    }
    VERIFY(overrun_string_index != u32(-1));

    strconcat_error::process(overrun_string_index, m_count, strings);
}

} // namespace detail
} // namespace core
} // namespace xray
