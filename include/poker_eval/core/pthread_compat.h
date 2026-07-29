/*
 * pthread_compat.h - Minimal pthread compatibility layer for Windows.
 */

#ifndef POKER_EVAL_PTHREAD_COMPAT_H
#define POKER_EVAL_PTHREAD_COMPAT_H

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <process.h>
#include <stdlib.h>
#include <stdint.h>

typedef struct
{
    HANDLE handle;
    unsigned int id;
} pthread_t;

typedef CRITICAL_SECTION pthread_mutex_t;
typedef CONDITION_VARIABLE pthread_cond_t;

typedef struct
{
    void *(*start_routine)(void *);
    void *arg;
} pe_pthread_start_t;

static unsigned __stdcall pe_pthread_start(void *arg)
{
    pe_pthread_start_t *start = (pe_pthread_start_t *)arg;
    void *ret = NULL;
    if (start && start->start_routine)
    {
        ret = start->start_routine(start->arg);
    }
    free(start);
    return (unsigned)(uintptr_t)ret;
}

static int pthread_create(pthread_t *thr, const void *attr,
                          void *(*start_routine)(void *), void *arg)
{
    (void)attr;
    if (!thr || !start_routine)
        return -1;
    pe_pthread_start_t *start = (pe_pthread_start_t *)malloc(sizeof(*start));
    if (!start)
        return -1;
    start->start_routine = start_routine;
    start->arg = arg;
    uintptr_t handle = _beginthreadex(NULL, 0, pe_pthread_start, start, 0, &thr->id);
    if (!handle)
    {
        free(start);
        return -1;
    }
    thr->handle = (HANDLE)handle;
    return 0;
}

static int pthread_join(pthread_t thr, void **ret)
{
    if (!thr.handle)
        return -1;
    DWORD wait_rc = WaitForSingleObject(thr.handle, INFINITE);
    if (wait_rc != WAIT_OBJECT_0)
        return -1;
    if (ret)
    {
        DWORD code = 0;
        if (GetExitCodeThread(thr.handle, &code))
            *ret = (void *)(uintptr_t)code;
        else
            *ret = NULL;
    }
    CloseHandle(thr.handle);
    return 0;
}

static pthread_t pthread_self(void)
{
    pthread_t self;
    self.handle = NULL;
    self.id = GetCurrentThreadId();
    return self;
}

static int pthread_equal(pthread_t a, pthread_t b)
{
    return a.id == b.id;
}

static int pthread_mutex_init(pthread_mutex_t *mutex, const void *attr)
{
    (void)attr;
    InitializeCriticalSection(mutex);
    return 0;
}

static int pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    DeleteCriticalSection(mutex);
    return 0;
}

static int pthread_mutex_lock(pthread_mutex_t *mutex)
{
    EnterCriticalSection(mutex);
    return 0;
}

static int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    LeaveCriticalSection(mutex);
    return 0;
}

static int pthread_cond_init(pthread_cond_t *cond, const void *attr)
{
    (void)attr;
    InitializeConditionVariable(cond);
    return 0;
}

static int pthread_cond_destroy(pthread_cond_t *cond)
{
    (void)cond;
    return 0;
}

static int pthread_cond_signal(pthread_cond_t *cond)
{
    WakeConditionVariable(cond);
    return 0;
}

static int pthread_cond_broadcast(pthread_cond_t *cond)
{
    WakeAllConditionVariable(cond);
    return 0;
}

static int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
    SleepConditionVariableCS(cond, mutex, INFINITE);
    return 0;
}

#else
#include <pthread.h>
#endif

#endif /* POKER_EVAL_PTHREAD_COMPAT_H */
