/*
 * gpu_cfr.h - GPU-accelerated CFR solver
 *
 * Copyright (C) 2025 poker-eval contributors
 *
 * Matrix formulation of CFR for massive parallelization on GPU.
 * This compatibility API is a matrix maintenance primitive. It does not
 * claim a tree-solver speedup; use pe_solver_run with a compute port for a
 * complete CFR solve.
 *
 * Key idea: Reformulate CFR updates as matrix operations (GEMV/SpMV)
 * that can be efficiently computed on GPU with thousands of threads.
 */

#ifndef POKER_EVAL_GPU_CFR_H
#define POKER_EVAL_GPU_CFR_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <poker_eval/engine/solvers/cfr/cfr_core.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The autonomous matrix solver predates the v3 port-based solver API. */
#if defined(__GNUC__) || defined(__clang__)
#define PE_GPU_CFR_DEPRECATED(message) __attribute__((deprecated(message)))
#else
#define PE_GPU_CFR_DEPRECATED(message)
#endif

/**
 * GPU-CFR Matrix Representation
 *
 * CFR state is represented as:
 * - R: Regret matrix [N_infosets × Max_actions] (dense or sparse)
 * - S: Average strategy matrix [N_infosets × Max_actions]
 * - T: Transition matrix [N_infosets × N_infosets] (sparse CSR format)
 *
 * Update formulas (vectorized):
 * - R' = discount * R + Δ     (regret update, AXPY)
 * - S' = S + weight * π       (strategy accumulation, AXPY)
 * - π = normalize_pos(R)      (regret matching, per-row softmax)
 */

/* Forward declarations */
typedef struct gpu_cfr_context_t gpu_cfr_context_t;
typedef struct gpu_cfr_config_t gpu_cfr_config_t;
typedef struct gpu_cfr_stats_t gpu_cfr_stats_t;

/**
 * Sparse matrix storage (CSR format)
 *
 * For transition matrix T where T[i,j] = probability of reaching
 * infoset j from infoset i given an action.
 */
typedef struct {
    int num_rows;               /* Number of infosets */
    int num_cols;               /* Same as num_rows typically */
    int nnz;                    /* Number of non-zero entries */

    int* row_ptr;               /* Row pointers [num_rows+1] */
    int* col_idx;               /* Column indices [nnz] */
    float* values;              /* Non-zero values [nnz] */
} sparse_matrix_csr_t;

/**
 * Dense regret/strategy storage
 *
 * Row-major layout: infoset_id * max_actions + action_id
 */
typedef struct {
    int num_infosets;           /* Number of information sets */
    int max_actions;            /* Maximum actions per infoset */

    float* regrets;             /* Regret matrix [num_infosets × max_actions] */
    float* avg_strategy;        /* Cumulative strategy [num_infosets × max_actions] */
    float* curr_strategy;       /* Current strategy π(regret) [num_infosets × max_actions] */

    /* Per-infoset metadata */
    uint8_t* action_counts;     /* Actual actions per infoset [num_infosets] */
    uint64_t* infoset_keys;     /* Hash keys for mapping [num_infosets] */
} cfr_matrix_storage_t;

/**
 * GPU-CFR configuration
 */
struct gpu_cfr_config_t {
    int num_infosets;           /* Number of information sets */
    int max_actions;            /* Maximum actions per infoset */
    int max_iterations;         /* CFR iterations to run */

    /* Discounting (DCFR) */
    float regret_discount;      /* Per-iteration discount (1.0 = no discount) */
    float strategy_weight;      /* Strategy accumulation weight */

    /* GPU settings */
    int device_id;              /* GPU device (-1 = auto) */
    bool use_sparse;            /* Use sparse transitions (CSR) */
    int threads_per_block;      /* Threads per block (default 256) */
    bool enable_profiling;      /* Track GPU kernel times */
    bool verbose;               /* Print progress */
};

/**
 * GPU-CFR statistics
 */
struct gpu_cfr_stats_t {
    int iterations_completed;
    double total_time_ms;
    double avg_iteration_time_ms;
    double exploitability;      /* Approximate exploitability */

    /* GPU kernel breakdown */
    double regret_update_time_ms;
    double strategy_compute_time_ms;
    double normalization_time_ms;
    double spmv_time_ms;
};

/**
 * Get default GPU-CFR configuration
 *
 * @return Default configuration
 */
gpu_cfr_config_t gpu_cfr_default_config(void);

/**
 * Initialize GPU-CFR context
 *
 * Allocates GPU memory and prepares kernels for CFR solving.
 *
 * @param config  Configuration
 * @return GPU-CFR context or NULL on failure
 */
gpu_cfr_context_t* gpu_cfr_init(const gpu_cfr_config_t* config);

/**
 * Free GPU-CFR context
 *
 * @param ctx  Context to free
 */
void gpu_cfr_free(gpu_cfr_context_t* ctx);

/**
 * Load initial regrets and strategies from CPU
 *
 * @param ctx      GPU-CFR context
 * @param storage  Matrix storage (host memory)
 * @return 0 on success, negative on error
 */
int gpu_cfr_load_state(
    gpu_cfr_context_t* ctx,
    const cfr_matrix_storage_t* storage
);

/**
 * Download current regrets and strategies to CPU
 *
 * @param ctx      GPU-CFR context
 * @param storage  Output matrix storage (host memory)
 * @return 0 on success, negative on error
 */
int gpu_cfr_download_state(
    gpu_cfr_context_t* ctx,
    cfr_matrix_storage_t* storage
);

/**
 * Run matrix maintenance iterations
 *
 * Performs the following per iteration:
 * The compatibility API has no game callback, so it performs the matrix-side
 * regret matching and average-strategy accumulation only. Complete tree
 * traversal belongs to pe_solver_run and its compute ports.
 *
 * @param ctx         GPU-CFR context
 * @param iterations  Number of iterations to run
 * @return 0 on success, negative on error
 */
int gpu_cfr_solve(
    gpu_cfr_context_t* ctx,
    int iterations
) PE_GPU_CFR_DEPRECATED("use pe_solver_run with a GPU compute port instead");

/**
 * Get GPU-CFR statistics
 *
 * @param ctx    GPU-CFR context
 * @param stats  Output statistics
 * @return 0 on success, negative on error
 */
int gpu_cfr_get_stats(
    const gpu_cfr_context_t* ctx,
    gpu_cfr_stats_t* stats
);

/**
 * Reset regrets and strategies to zero
 *
 * @param ctx  GPU-CFR context
 * @return 0 on success, negative on error
 */
int gpu_cfr_reset(gpu_cfr_context_t* ctx);

/* ===== Matrix utility functions ===== */

/**
 * Create matrix storage (host)
 *
 * @param num_infosets  Number of information sets
 * @param max_actions   Maximum actions per infoset
 * @return Allocated storage or NULL on failure
 */
cfr_matrix_storage_t* cfr_matrix_storage_create(
    int num_infosets,
    int max_actions
);

/**
 * Free matrix storage
 *
 * @param storage  Storage to free
 */
void cfr_matrix_storage_free(cfr_matrix_storage_t* storage);

/**
 * Convert CPU CFR storage to matrix format
 *
 * Converts hash-map based CFR storage to dense matrix representation
 * suitable for GPU processing.
 *
 * @param cpu_storage  CPU hash-map storage (from cfr_storage.c)
 * @param max_actions  Maximum actions per infoset
 * @return Matrix storage or NULL on failure
 */
cfr_matrix_storage_t* cfr_convert_to_matrix(
    cfr_storage_t* cpu_storage,  /* cfr_storage_t* */
    int max_actions
);

/**
 * Convert matrix format back to CPU CFR storage
 *
 * @param matrix       Matrix storage (host memory)
 * @param cpu_storage  Output CPU hash-map storage
 * @return 0 on success, negative on error
 */
int cfr_convert_from_matrix(
    const cfr_matrix_storage_t* matrix,
    cfr_storage_t* cpu_storage
);

/**
 * Print a summary of the matrix storage
 *
 * @param matrix  Matrix storage to summarize
 */
void cfr_matrix_print_summary(const cfr_matrix_storage_t* matrix);

/**
 * Validate the contents of the matrix storage
 *
 * @param matrix  Matrix storage to validate
 * @return 0 on success, negative on error
 */
int cfr_matrix_validate(const cfr_matrix_storage_t* matrix);

/**
 * Create sparse transition matrix
 *
 * Builds CSR sparse matrix representing state transitions.
 *
 * @param num_infosets     Number of information sets
 * @param from_indices     Source infoset indices
 * @param to_indices       Destination infoset indices
 * @param probabilities    Transition probabilities
 * @param num_transitions  Number of transitions
 * @return Sparse matrix or NULL on failure
 */
sparse_matrix_csr_t* sparse_matrix_create_csr(
    int num_infosets,
    const int* from_indices,
    const int* to_indices,
    const float* probabilities,
    int num_transitions
);

/**
 * Free sparse matrix
 *
 * @param matrix  Matrix to free
 */
void sparse_matrix_free(sparse_matrix_csr_t* matrix);

typedef struct gpu_cfr_adapter_t gpu_cfr_adapter_t;

gpu_cfr_adapter_t *gpu_cfr_adapter_create(
    cfr_game_t *game,
    cfr_matrix_storage_t *matrix,
    int mc_samples
) PE_GPU_CFR_DEPRECATED("use pe_solver_run with a GPU compute port instead");

void gpu_cfr_adapter_free(gpu_cfr_adapter_t *adapter)
    PE_GPU_CFR_DEPRECATED("use pe_solver_run with a GPU compute port instead");

int gpu_cfr_adapter_compute_deltas(gpu_cfr_adapter_t *adapter)
    PE_GPU_CFR_DEPRECATED("use pe_solver_run with a GPU compute port instead");

const float *gpu_cfr_adapter_get_deltas(const gpu_cfr_adapter_t *adapter)
    PE_GPU_CFR_DEPRECATED("use pe_solver_run with a GPU compute port instead");

int gpu_cfr_adapter_get_visited_infosets(const gpu_cfr_adapter_t *adapter)
    PE_GPU_CFR_DEPRECATED("use pe_solver_run with a GPU compute port instead");

#ifdef __cplusplus
}
#endif

#undef PE_GPU_CFR_DEPRECATED

#endif /* POKER_EVAL_GPU_CFR_H */
