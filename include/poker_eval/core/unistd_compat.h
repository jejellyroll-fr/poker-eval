/*
 * unistd_compat.h - Minimal POSIX compatibility helpers for Windows.
 */

#ifndef POKER_EVAL_UNISTD_COMPAT_H
#define POKER_EVAL_UNISTD_COMPAT_H

#if defined(__MINGW32__)
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#include <io.h>

#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

#ifndef _SSIZE_T_DEFINED
typedef SSIZE_T ssize_t;
#define _SSIZE_T_DEFINED
#endif

static __inline int isatty(int fd)
{
    return _isatty(fd);
}

static __inline int read(int fd, void *buf, unsigned int count)
{
    return _read(fd, buf, count);
}

static __inline int sleep(unsigned int seconds)
{
    Sleep(seconds * 1000U);
    return 0;
}

static __inline int usleep(unsigned int usec)
{
    DWORD ms = (usec + 999U) / 1000U;
    Sleep(ms);
    return 0;
}

#else
#include <unistd.h>
#endif

#endif /* POKER_EVAL_UNISTD_COMPAT_H */
