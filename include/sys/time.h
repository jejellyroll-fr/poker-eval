/*
 * sys/time.h - Windows compatibility shim.
 */

#ifndef POKER_EVAL_SYS_TIME_H
#define POKER_EVAL_SYS_TIME_H

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <stdint.h>
#include <windows.h>
#include <winsock2.h>

static __inline int gettimeofday(struct timeval *tv, void *tz) {
  (void)tz;
  if (!tv)
    return -1;
  FILETIME ft;
  ULARGE_INTEGER uli;
  GetSystemTimeAsFileTime(&ft);
  uli.LowPart = ft.dwLowDateTime;
  uli.HighPart = ft.dwHighDateTime;
  /* Convert from 100-ns since 1601-01-01 to microseconds since 1970-01-01 */
  const uint64_t EPOCH_DIFF = 116444736000000000ULL;
  uint64_t usec = (uli.QuadPart - EPOCH_DIFF) / 10ULL;
  tv->tv_sec = (long)(usec / 1000000ULL);
  tv->tv_usec = (long)(usec % 1000000ULL);
  return 0;
}
#else
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-include-next"
#include_next <sys/time.h>
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC system_header
#include_next <sys/time.h>
#else
#include_next <sys/time.h>
#endif
#endif

#endif /* POKER_EVAL_SYS_TIME_H */
