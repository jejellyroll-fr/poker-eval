/*
 * pe_compute.h - Compute port and CPU-parallel adapter contract (PAR-03)
 */

#ifndef POKER_EVAL_PE_COMPUTE_H
#define POKER_EVAL_PE_COMPUTE_H

#include <poker_eval/solver/pe_batch.h>
#include <poker_eval/solver/pe_capabilities.h>
#include <poker_eval/solver/pe_solver_config.h>
#include <poker_eval/solver/pe_vector.h>
#include <poker_eval/core/enumdefs.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pe_compute_config_t
{
    int cpu_threads;
    int deterministic;
    size_t sample_batch_size;
    size_t terminal_batch_size;
    size_t update_batch_size;

    /* Optional storage target for apply_update_batch. A NULL pair keeps the
       adapter usable as a compute-only validator until the full solver loop
       injects its storage port. */
    const pe_storage_ops_t *storage;
    void *storage_self;

    /* Update semantics are part of the compute contract.  Keeping them here
       prevents a backend from silently treating CFR+, DCFR or a weighted
       average as vanilla CFR. Appending these fields preserves source
       compatibility for existing positional initializers. */
    pe_regret_mode_t regret_mode;
    pe_policy_mode_t policy_mode;
    pe_averaging_mode_t averaging_mode;
    double dcfr_alpha;
    double dcfr_beta;
    double dcfr_gamma;
    int averaging_delay;
    double exponential_lambda;
} pe_compute_config_t;

/*
 * Ragged strategy batches. For infoset i, the owned span is
 * [offsets[i], offsets[i + 1]) in `regrets`/`strategies`; action_counts[i]
 * says how many entries in that span are live actions. The remaining entries
 * are padding owned by the caller and are cleared by strategy_batch().
 *
 * The layout is deliberately independent of storage IDs: a strategy kernel
 * only needs contiguous values and shape metadata. Update batches retain
 * infoset IDs because they are applied through the storage port.
 */
typedef struct pe_infoset_batch_t
{
    size_t count;
    const uint32_t *offsets;       /* count + 1 entries                    */
    const uint16_t *action_counts; /* count entries                        */
    const float *regrets;          /* offsets[count] entries               */
} pe_infoset_batch_t;

typedef struct pe_strategy_batch_t
{
    size_t count;
    size_t capacity;               /* number of float entries              */
    const uint32_t *offsets;       /* same ragged layout as the input       */
    float *strategies;
} pe_strategy_batch_t;

/* Shape metadata used to flatten a storage update into a GPU slot. `infosets`
 * need not be dense; the resolver performs a deterministic linear lookup. */
typedef struct pe_infoset_layout_t
{
    size_t count;
    const pe_infoset_id_t *infosets;
    const uint32_t *offsets;       /* count + 1 entries                    */
    const uint16_t *action_counts; /* count entries                        */
    const uint16_t *combo_counts;  /* count entries                        */
} pe_infoset_layout_t;

/**
 * Resolve one logical update to its flattened ragged slot.
 *
 * The search is linear: the layout need not be sorted or dense. A caller
 * resolving many updates against one layout should build its own index and
 * use pe_infoset_layout_slot_at, or the batch becomes quadratic.
 */
int pe_infoset_layout_resolve_slot(const pe_infoset_layout_t *layout,
                                   const pe_update_t *update,
                                   uint32_t *out_slot);

/**
 * The same resolution for a caller that already knows which entry the update
 * belongs to. Validates that `index` really is that entry, so a wrong index
 * is refused rather than silently addressing another infoset's span.
 */
int pe_infoset_layout_slot_at(const pe_infoset_layout_t *layout, size_t index,
                              const pe_update_t *update, uint32_t *out_slot);
typedef struct pe_showdown_job_t pe_showdown_job_t;

/*
 * Terminal evaluation is the first compute operation with a stable public
 * batch contract. Hold'em/stud/razz consume `cards`; Omaha consumes `hole` and
 * `board`. Card values are the standard 0..51 deck indices used by the GPU
 * batched evaluator. Unused pointers are ignored for the selected game.
 */
typedef struct pe_terminal_batch_t
{
    enum_game_t game;
    const uint8_t *cards;
    const uint8_t *hole;
    const uint8_t *board;
    size_t count;
} pe_terminal_batch_t;

typedef struct pe_value_batch_t
{
    uint32_t *values;
    size_t capacity;
    size_t count;
} pe_value_batch_t;

/* pe_ports.h forward-declares this tag; keep the typedef guarded so either
   header can be included first without a C99 typedef redefinition. */
#ifndef POKER_EVAL_PE_COMPUTE_OPS_T_DEFINED
typedef struct pe_compute_ops_t pe_compute_ops_t;
#define POKER_EVAL_PE_COMPUTE_OPS_T_DEFINED
#endif

struct pe_compute_ops_t
{
    const char *name;
    uint64_t (*capabilities)(void *self);
    int (*create)(void **self, const pe_compute_config_t *cfg);
    void (*destroy)(void *self);

    int (*strategy_batch)(void *self, const pe_infoset_batch_t *in,
                          pe_strategy_batch_t *out);
    int (*apply_update_batch)(void *self, const pe_update_batch_t *batch);
    int (*terminal_eval_batch)(void *self, const pe_terminal_batch_t *in,
                               pe_value_batch_t *out);
    int (*vector_showdown)(void *self, const pe_showdown_job_t *job,
                           pe_value_vec_t *out);
    int (*sync)(void *self);
};

/** The deterministic CPU-parallel adapter. */
const pe_compute_ops_t *pe_compute_cpu_par_ops(void);

/** The one-thread F64 reference adapter used as the backend parity oracle. */
const pe_compute_ops_t *pe_compute_cpu_ref_ops(void);

/** CUDA terminal evaluator; capability is gated until GPU-05 parity passes. */
const pe_compute_ops_t *pe_compute_cuda_ops(void);

/** OpenCL terminal evaluator; capability is gated until GPU-05 parity passes. */
const pe_compute_ops_t *pe_compute_opencl_ops(void);

/** HIP/ROCm adapter. Shares its kernels with CUDA; same parity gate. */
const pe_compute_ops_t *pe_compute_hip_ops(void);

/** Metal adapter for Apple GPUs. Unified memory, same parity gate. */
const pe_compute_ops_t *pe_compute_metal_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_COMPUTE_H */
