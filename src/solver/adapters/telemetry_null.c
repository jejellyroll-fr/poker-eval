/*
 * telemetry_null.c - Telemetry sink (architecture v3, CTR-04)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * The adapter a solver installs when the caller supplies none. Its whole job
 * is to make "no telemetry" indistinguishable from "telemetry that ignores
 * everything", so the domain never has a NULL branch on its emit path.
 *
 * The instance is shared, immutable and static: it needs no allocation, cannot
 * fail, and any number of solvers may hold it at once.
 */

#include <poker_eval/solver/pe_telemetry.h>

#include <stddef.h>

/*
 * emit is deliberately NULL rather than a function that discards its argument:
 * pe_telemetry_wants() then reports 0, so a caller that checks first skips
 * building the message entirely. A no-op function would still pay for the
 * formatting at every trace point in the traversal.
 */
static const pe_telemetry_ops_t k_null_ops = {
    NULL,          /* emit  */
    NULL,          /* flush */
    NULL,          /* self  */
    PE_LOG_ERROR   /* max_level; unused with a NULL emit, but not garbage */
};

const pe_telemetry_ops_t *pe_telemetry_null(void)
{
    return &k_null_ops;
}
