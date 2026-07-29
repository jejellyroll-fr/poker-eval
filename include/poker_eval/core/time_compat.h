/*
 * time_compat.h - Cross-platform clock_gettime helpers.
 */

#ifndef POKER_EVAL_TIME_COMPAT_H
#define POKER_EVAL_TIME_COMPAT_H

#include <time.h>

#if defined(_WIN32)
#include <windows.h>

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

/*
 * Only MSVC needs the shim. MinGW-w64 declares clock_gettime and struct
 * timespec in its own <time.h>, so defining them here made every MinGW build
 * fail with "redefinition of 'clock_gettime'" - on every translation unit that
 * includes this header.
 */
#if defined(_MSC_VER)

static __inline int clock_gettime(int clk_id, struct timespec *ts)
{
    (void)clk_id;
    if (!ts)
        return -1;
    LARGE_INTEGER freq;
    LARGE_INTEGER counter;
    if (!QueryPerformanceFrequency(&freq) || !QueryPerformanceCounter(&counter))
        return -1;
    double seconds = (double)counter.QuadPart / (double)freq.QuadPart;
    ts->tv_sec = (time_t)seconds;
    ts->tv_nsec = (long)((seconds - (double)ts->tv_sec) * 1e9);
    return 0;
}
#endif /* _MSC_VER */

#endif /* _WIN32 */

#endif /* POKER_EVAL_TIME_COMPAT_H */
