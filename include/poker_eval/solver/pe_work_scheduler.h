/*
 * pe_work_scheduler.h - deterministic distributed work allocation (DIST-03)
 */

#ifndef POKER_EVAL_PE_WORK_SCHEDULER_H
#define POKER_EVAL_PE_WORK_SCHEDULER_H

#include <poker_eval/solver/pe_runtime.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pe_work_allocation_t
{
    pe_compute_kind_t backend;
    size_t unit_count;
    /* Effective work-unit rate used for this allocation. */
    double units_per_s;
} pe_work_allocation_t;

/**
 * Allocate a fixed number of independent work units across usable backends.
 *
 * A backend is usable only when its runtime descriptor says compiled,
 * runtime_available and validated. Allocation is proportional to the
 * measured update rate, falling back to strategy and terminal rates, then to
 * equal weights when no positive rate is known. The result is deterministic:
 * fractional remainders are assigned in backend enum order on ties.
 *
 * Returns the number of output entries, or -1 for invalid arguments or an
 * output buffer too small for the usable backend set. A zero unit count is a
 * valid request and returns zero.
 */
int pe_work_schedule(const pe_runtime_capabilities_t *runtime,
                     size_t unit_count,
                     pe_work_allocation_t *out,
                     size_t capacity);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_WORK_SCHEDULER_H */
