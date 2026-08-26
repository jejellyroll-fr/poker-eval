/*
 * pe_runtime.h - Runtime capability discovery for solver frontends.
 *
 * This is deliberately separate from the solver plan registry.  The
 * registry describes what a build can do; this probe describes what this
 * process can actually use on this machine.  Frontends must use the latter
 * before offering a backend to a user.
 */
#ifndef POKER_EVAL_PE_RUNTIME_H
#define POKER_EVAL_PE_RUNTIME_H

#include <poker_eval/equity/simd_operations.h>
#include <poker_eval/solver/pe_compute.h>
#include <poker_eval/solver/pe_solver_plan.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PE_RUNTIME_NAME_MAX 64
#define PE_RUNTIME_REASON_MAX 192

typedef struct pe_runtime_backend_info_t
{
    pe_compute_kind_t kind;
    int compiled;
    int runtime_available;
    int validated;
    int device_count;
    uint64_t capabilities;
    char name[PE_RUNTIME_NAME_MAX];
    char reason[PE_RUNTIME_REASON_MAX];
} pe_runtime_backend_info_t;

typedef struct pe_runtime_capabilities_t
{
    uint32_t logical_cpus;
    int openmp_available;
    /* SIMD capability reported by the host, independent of compiler flags. */
    simd_capability_t simd_machine;
    /* Highest SIMD kernel actually linked into this binary. */
    simd_capability_t simd_compiled;
    /* Capability safe to dispatch: intersection of machine and binary. */
    simd_capability_t simd;
    pe_runtime_backend_info_t backends[PE_COMPUTE_COUNT];
} pe_runtime_capabilities_t;

/** Probe the current process and host. Returns 0 on success, -1 on NULL. */
int pe_runtime_probe(pe_runtime_capabilities_t *out);

/** Choose the best validated backend for an automatic frontend request. */
pe_compute_kind_t pe_runtime_recommended_backend(
    const pe_runtime_capabilities_t *runtime);

/** Return a stable user-facing name for the selected SIMD capability. */
const char *pe_runtime_simd_name(simd_capability_t capability);

/** Render a compact backend status line for logs and CLI output. */
int pe_runtime_backend_status(const pe_runtime_backend_info_t *backend,
                              char *out, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_RUNTIME_H */
