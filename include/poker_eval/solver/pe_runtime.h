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
#define PE_RUNTIME_DESCRIPTOR_MAX 4096
#define PE_RUNTIME_DESCRIPTOR_VERSION 1

typedef struct pe_runtime_backend_info_t
{
    pe_compute_kind_t kind;
    int compiled;
    int runtime_available;
    int validated;
    int device_count;
    uint64_t capabilities;
    /* Optional measured rates, in elements per second. Zero means unknown. */
    double strategy_elements_per_s;
    double update_elements_per_s;
    double terminal_elements_per_s;
    /* Smallest terminal batch for which this backend should be considered.
       Zero means that no launch threshold is known. */
    size_t terminal_min_batch_size;
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

/**
 * Choose a backend while honoring a known terminal launch threshold.
 * `terminal_batch_size == 0` preserves the unconstrained resolver behavior.
 */
pe_compute_kind_t pe_runtime_recommended_backend_for_batch(
    const pe_runtime_capabilities_t *runtime, size_t terminal_batch_size);

/** Return a stable user-facing name for the selected SIMD capability. */
const char *pe_runtime_simd_name(simd_capability_t capability);

/** Render a backend status line with availability, reason, and measured rates. */
int pe_runtime_backend_status(const pe_runtime_backend_info_t *backend,
                              char *out, size_t capacity);

/**
 * Serialize a runtime descriptor in the stable PE_RUNTIME_V1 text format.
 * Unknown capability bits are carried as hexadecimal values. The return value
 * is the required length excluding the NUL terminator; a short buffer is
 * safely truncated. Passing NULL with capacity 0 measures the descriptor.
 */
size_t pe_runtime_descriptor_to_string(
    const pe_runtime_capabilities_t *runtime, char *out, size_t capacity);

/** Parse a PE_RUNTIME_V1 descriptor, preserving unknown capability bits. */
int pe_runtime_descriptor_from_string(
    const char *text, pe_runtime_capabilities_t *out);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_RUNTIME_H */
