/*
 * sys/resource.h - Windows compatibility shim.
 */

#ifndef POKER_EVAL_SYS_RESOURCE_H
#define POKER_EVAL_SYS_RESOURCE_H

#if defined(_WIN32)
struct rusage
{
    long ru_maxrss;
};

#ifndef RUSAGE_SELF
#define RUSAGE_SELF 0
#endif

static __inline int getrusage(int who, struct rusage *usage)
{
    (void)who;
    if (usage)
        usage->ru_maxrss = 0;
    return 0;
}
#else
#include_next <sys/resource.h>
#endif

#endif /* POKER_EVAL_SYS_RESOURCE_H */
