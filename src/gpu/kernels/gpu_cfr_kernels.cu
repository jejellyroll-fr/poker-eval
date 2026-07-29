/*
 * gpu_cfr_kernels.cu - CUDA kernels for GPU-CFR
 *
 * Copyright (C) 2025 poker-eval contributors
 *
 * High-performance CUDA kernels for matrix-based CFR solving:
 * - Regret matching (per-row softmax with ReLU)
 * - AXPY updates (regret and strategy accumulation)
 * - SpMV (sparse matrix-vector product for best response)
 */

#include <cuda_runtime.h>
#include <stdint.h>

/* ===== Regret Matching Kernel ===== */

/**
 * Compute current strategy from regrets via regret matching
 *
 * For each infoset i:
 *   sum_pos = Σ max(R[i,a], 0)
 *   π[i,a] = max(R[i,a], 0) / sum_pos  if sum_pos > 0
 *   π[i,a] = 1/n_actions               otherwise (uniform)
 *
 * @param regrets         Input regret matrix [N × A] (row-major)
 * @param curr_strategy   Output strategy matrix [N × A]
 * @param action_counts   Actual actions per infoset [N]
 * @param num_infosets    Number of infosets (N)
 * @param max_actions     Maximum actions per infoset (A)
 */
__global__ void regret_matching_kernel(
    const float* __restrict__ regrets,
    float* __restrict__ curr_strategy,
    const uint8_t* __restrict__ action_counts,
    int num_infosets,
    int max_actions
) {
    int infoset_id = blockIdx.x * blockDim.x + threadIdx.x;

    if (infoset_id >= num_infosets) return;

    int n_actions = action_counts[infoset_id];
    if (n_actions == 0) return;

    int base_idx = infoset_id * max_actions;

    /* Compute sum of positive regrets */
    float sum_pos = 0.0f;
    for (int a = 0; a < n_actions; a++) {
        float r = regrets[base_idx + a];
        if (r > 0.0f) sum_pos += r;
    }

    /* Normalize to get strategy */
    if (sum_pos > 1e-9f) {
        /* Regret matching: proportional to positive regrets */
        for (int a = 0; a < n_actions; a++) {
            float r = regrets[base_idx + a];
            curr_strategy[base_idx + a] = (r > 0.0f) ? (r / sum_pos) : 0.0f;
        }
    } else {
        /* Uniform strategy (all regrets <= 0) */
        float uniform = 1.0f / (float)n_actions;
        for (int a = 0; a < n_actions; a++) {
            curr_strategy[base_idx + a] = uniform;
        }
    }

    /* Zero out unused action slots */
    for (int a = n_actions; a < max_actions; a++) {
        curr_strategy[base_idx + a] = 0.0f;
    }
}

/* ===== AXPY Kernel (y = alpha * x + y) ===== */

/**
 * Fused multiply-add: y = alpha * x + y
 *
 * Used for:
 * - Regret update: R' = discount * R + Δ
 * - Strategy accumulation: S' = S + weight * π
 *
 * @param alpha  Scalar multiplier
 * @param x      Input vector (read-only)
 * @param y      Input/output vector (modified in-place)
 * @param size   Vector size (N × A)
 */
__global__ void axpy_kernel(
    float alpha,
    const float* __restrict__ x,
    float* __restrict__ y,
    int size
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;

    if (idx >= size) return;

    y[idx] = alpha * x[idx] + y[idx];
}

/**
 * Specialized AXPY for regret accumulation with discount
 *
 * R' = alpha * R + Δ
 *
 * @param alpha   Discount factor (e.g., 0.99 for DCFR)
 * @param regrets Input/output regret matrix (modified in-place)
 * @param deltas  Regret deltas to add
 * @param size    Matrix size (N × A)
 */
__global__ void regret_update_kernel(
    float alpha,
    float* __restrict__ regrets,
    const float* __restrict__ deltas,
    int size
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;

    if (idx >= size) return;

    regrets[idx] = alpha * regrets[idx] + deltas[idx];

    /* Optional: clamp negative regrets to prevent overflow */
    /* regrets[idx] = max(regrets[idx], -1e6f); */
}

/**
 * Strategy accumulation: S' = S + weight * π
 *
 * @param weight        Accumulation weight (typically 1.0 or iteration-dependent)
 * @param avg_strategy  Input/output average strategy (modified in-place)
 * @param curr_strategy Current strategy to accumulate
 * @param size          Matrix size (N × A)
 */
__global__ void strategy_accumulate_kernel(
    float weight,
    float* __restrict__ avg_strategy,
    const float* __restrict__ curr_strategy,
    int size
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;

    if (idx >= size) return;

    avg_strategy[idx] += weight * curr_strategy[idx];
}

/* ===== Sparse Matrix-Vector Product (SpMV) ===== */

/**
 * CSR sparse matrix-vector product: y = A * x
 *
 * Used for computing values/utilities via transition matrix.
 *
 * A is in CSR format:
 *   row_ptr[i] to row_ptr[i+1] gives indices in col_idx/values for row i
 *
 * @param row_ptr     CSR row pointers [num_rows + 1]
 * @param col_idx     CSR column indices [nnz]
 * @param values      CSR non-zero values [nnz]
 * @param x           Input vector [num_cols]
 * @param y           Output vector [num_rows]
 * @param num_rows    Number of rows
 */
__global__ void spmv_csr_kernel(
    const int* __restrict__ row_ptr,
    const int* __restrict__ col_idx,
    const float* __restrict__ values,
    const float* __restrict__ x,
    float* __restrict__ y,
    int num_rows
) {
    int row = blockIdx.x * blockDim.x + threadIdx.x;

    if (row >= num_rows) return;

    float sum = 0.0f;

    /* Accumulate dot product for this row */
    for (int k = row_ptr[row]; k < row_ptr[row + 1]; k++) {
        int col = col_idx[k];
        sum += values[k] * x[col];
    }

    y[row] = sum;
}

/* ===== Utility Kernels ===== */

/**
 * Zero out a device array
 *
 * @param data  Array to zero
 * @param size  Number of elements
 */
__global__ void zero_kernel(
    float* __restrict__ data,
    int size
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;

    if (idx >= size) return;

    data[idx] = 0.0f;
}

/**
 * Normalize strategy matrix (per-row normalization)
 *
 * Ensures each row (infoset) sums to 1.0
 *
 * @param strategy      Strategy matrix to normalize (in-place)
 * @param action_counts Actions per infoset
 * @param num_infosets  Number of infosets
 * @param max_actions   Max actions per infoset
 */
__global__ void normalize_strategy_kernel(
    float* __restrict__ strategy,
    const uint8_t* __restrict__ action_counts,
    int num_infosets,
    int max_actions
) {
    int infoset_id = blockIdx.x * blockDim.x + threadIdx.x;

    if (infoset_id >= num_infosets) return;

    int n_actions = action_counts[infoset_id];
    if (n_actions == 0) return;

    int base_idx = infoset_id * max_actions;

    /* Compute sum */
    float sum = 0.0f;
    for (int a = 0; a < n_actions; a++) {
        sum += strategy[base_idx + a];
    }

    /* Normalize */
    if (sum > 1e-9f) {
        for (int a = 0; a < n_actions; a++) {
            strategy[base_idx + a] /= sum;
        }
    } else {
        /* Fallback to uniform if sum is zero */
        float uniform = 1.0f / (float)n_actions;
        for (int a = 0; a < n_actions; a++) {
            strategy[base_idx + a] = uniform;
        }
    }
}

/* ===== Kernel Launch Wrappers (extern "C" for C linkage) ===== */

extern "C" {

void launch_regret_matching(
    const float* d_regrets,
    float* d_curr_strategy,
    const uint8_t* d_action_counts,
    int num_infosets,
    int max_actions,
    cudaStream_t stream
) {
    int threads_per_block = 256;
    int blocks = (num_infosets + threads_per_block - 1) / threads_per_block;

    regret_matching_kernel<<<blocks, threads_per_block, 0, stream>>>(
        d_regrets,
        d_curr_strategy,
        d_action_counts,
        num_infosets,
        max_actions
    );
}

void launch_regret_update(
    float alpha,
    float* d_regrets,
    const float* d_deltas,
    int size,
    cudaStream_t stream
) {
    int threads_per_block = 256;
    int blocks = (size + threads_per_block - 1) / threads_per_block;

    regret_update_kernel<<<blocks, threads_per_block, 0, stream>>>(
        alpha,
        d_regrets,
        d_deltas,
        size
    );
}

void launch_strategy_accumulate(
    float weight,
    float* d_avg_strategy,
    const float* d_curr_strategy,
    int size,
    cudaStream_t stream
) {
    int threads_per_block = 256;
    int blocks = (size + threads_per_block - 1) / threads_per_block;

    strategy_accumulate_kernel<<<blocks, threads_per_block, 0, stream>>>(
        weight,
        d_avg_strategy,
        d_curr_strategy,
        size
    );
}

void launch_spmv_csr(
    const int* d_row_ptr,
    const int* d_col_idx,
    const float* d_values,
    const float* d_x,
    float* d_y,
    int num_rows,
    cudaStream_t stream
) {
    int threads_per_block = 256;
    int blocks = (num_rows + threads_per_block - 1) / threads_per_block;

    spmv_csr_kernel<<<blocks, threads_per_block, 0, stream>>>(
        d_row_ptr,
        d_col_idx,
        d_values,
        d_x,
        d_y,
        num_rows
    );
}

void launch_zero_array(
    float* d_data,
    int size,
    cudaStream_t stream
) {
    int threads_per_block = 256;
    int blocks = (size + threads_per_block - 1) / threads_per_block;

    zero_kernel<<<blocks, threads_per_block, 0, stream>>>(
        d_data,
        size
    );
}

void launch_normalize_strategy(
    float* d_strategy,
    const uint8_t* d_action_counts,
    int num_infosets,
    int max_actions,
    cudaStream_t stream
) {
    int threads_per_block = 256;
    int blocks = (num_infosets + threads_per_block - 1) / threads_per_block;

    normalize_strategy_kernel<<<blocks, threads_per_block, 0, stream>>>(
        d_strategy,
        d_action_counts,
        num_infosets,
        max_actions
    );
}

} /* extern "C" */
