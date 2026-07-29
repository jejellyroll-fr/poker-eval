#ifndef POKER_EVAL_UTILS_OMP_COMPAT_H
#define POKER_EVAL_UTILS_OMP_COMPAT_H

#if defined(_OPENMP) && __has_include(<omp.h>)
#include <stdint.h>
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpedantic"
#pragma clang diagnostic ignored "-Wundef"
#endif
#include <omp.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#else
#undef _OPENMP
typedef int omp_lock_t;
static inline int omp_get_max_threads(void) { return 1; }
static inline void omp_set_num_threads(int n) { (void)n; }
static inline int omp_get_thread_num(void) { return 0; }
static inline void omp_init_lock(omp_lock_t *lock) { if (lock) *lock = 0; }
static inline void omp_destroy_lock(omp_lock_t *lock) { (void)lock; }
static inline void omp_set_lock(omp_lock_t *lock) { (void)lock; }
static inline void omp_unset_lock(omp_lock_t *lock) { (void)lock; }
#endif

#endif /* POKER_EVAL_UTILS_OMP_COMPAT_H */
