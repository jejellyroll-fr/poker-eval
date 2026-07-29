/*
 * gpu_cfr_kernels.cl - OpenCL kernels for GPU-CFR
 *
 * Copyright (C) 2025 poker-eval contributors
 *
 * High-performance OpenCL kernels for matrix-based CFR solving:
 * - Regret matching (per-row softmax with ReLU)
 * - AXPY updates (regret and strategy accumulation)
 * - SpMV (sparse matrix-vector product for best response)
 *
 * OpenCL port of CUDA gpu_cfr_kernels.cu for AMD/Intel GPU support.
 */

/* ===== Regret Matching Kernel ===== */

/**
 * Compute current strategy from regrets via regret matching
 *
 * For each infoset i:
 *   sum_pos = sum of max(R[i,a], 0) for all actions a
 *   pi[i,a] = max(R[i,a], 0) / sum_pos  if sum_pos > 0
 *   pi[i,a] = 1/n_actions               otherwise (uniform)
 */
__kernel void regret_matching_kernel(
    __global const float* restrict regrets,
    __global float* restrict curr_strategy,
    __global const uchar* restrict action_counts,
    int num_infosets,
    int max_actions
) {
    int infoset_id = get_global_id(0);

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
 * - Regret update: R' = discount * R + delta
 * - Strategy accumulation: S' = S + weight * pi
 */
__kernel void axpy_kernel(
    float alpha,
    __global const float* restrict x,
    __global float* restrict y,
    int size
) {
    int idx = get_global_id(0);

    if (idx >= size) return;

    y[idx] = alpha * x[idx] + y[idx];
}

/**
 * Specialized AXPY for regret accumulation with discount
 *
 * R' = alpha * R + delta
 */
__kernel void regret_update_kernel(
    float alpha,
    __global float* restrict regrets,
    __global const float* restrict deltas,
    int size
) {
    int idx = get_global_id(0);

    if (idx >= size) return;

    regrets[idx] = alpha * regrets[idx] + deltas[idx];
}

/**
 * Strategy accumulation: S' = S + weight * pi
 */
__kernel void strategy_accumulate_kernel(
    float weight,
    __global float* restrict avg_strategy,
    __global const float* restrict curr_strategy,
    int size
) {
    int idx = get_global_id(0);

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
 */
__kernel void spmv_csr_kernel(
    __global const int* restrict row_ptr,
    __global const int* restrict col_idx,
    __global const float* restrict values,
    __global const float* restrict x,
    __global float* restrict y,
    int num_rows
) {
    int row = get_global_id(0);

    if (row >= num_rows) return;

    float sum = 0.0f;

    /* Accumulate dot product for this row */
    int start = row_ptr[row];
    int end = row_ptr[row + 1];

    for (int k = start; k < end; k++) {
        int col = col_idx[k];
        sum += values[k] * x[col];
    }

    y[row] = sum;
}

/* ===== Utility Kernels ===== */

/**
 * Zero out a device array
 */
__kernel void zero_kernel(
    __global float* restrict data,
    int size
) {
    int idx = get_global_id(0);

    if (idx >= size) return;

    data[idx] = 0.0f;
}

/**
 * Normalize strategy matrix (per-row normalization)
 *
 * Ensures each row (infoset) sums to 1.0
 */
__kernel void normalize_strategy_kernel(
    __global float* restrict strategy,
    __global const uchar* restrict action_counts,
    int num_infosets,
    int max_actions
) {
    int infoset_id = get_global_id(0);

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

/* ===== Additional Utility Kernels ===== */

/**
 * Copy array: dst = src
 */
__kernel void copy_kernel(
    __global const float* restrict src,
    __global float* restrict dst,
    int size
) {
    int idx = get_global_id(0);

    if (idx >= size) return;

    dst[idx] = src[idx];
}

/**
 * Scale array: x = alpha * x
 */
__kernel void scale_kernel(
    float alpha,
    __global float* restrict x,
    int size
) {
    int idx = get_global_id(0);

    if (idx >= size) return;

    x[idx] = alpha * x[idx];
}

/**
 * Compute max element in an array (reduction kernel)
 * This is a simple single work-group reduction.
 * For large arrays, multi-level reduction would be needed.
 */
__kernel void max_reduction_kernel(
    __global const float* restrict input,
    __global float* restrict output,
    __local float* scratch,
    int size
) {
    int local_id = get_local_id(0);
    int global_id = get_global_id(0);
    int local_size = get_local_size(0);

    /* Load data to local memory */
    scratch[local_id] = (global_id < size) ? input[global_id] : -INFINITY;
    barrier(CLK_LOCAL_MEM_FENCE);

    /* Reduction in local memory */
    for (int stride = local_size / 2; stride > 0; stride >>= 1) {
        if (local_id < stride) {
            scratch[local_id] = fmax(scratch[local_id], scratch[local_id + stride]);
        }
        barrier(CLK_LOCAL_MEM_FENCE);
    }

    /* Write result */
    if (local_id == 0) {
        output[get_group_id(0)] = scratch[0];
    }
}

/**
 * Compute sum of elements (reduction kernel)
 */
__kernel void sum_reduction_kernel(
    __global const float* restrict input,
    __global float* restrict output,
    __local float* scratch,
    int size
) {
    int local_id = get_local_id(0);
    int global_id = get_global_id(0);
    int local_size = get_local_size(0);

    /* Load data to local memory */
    scratch[local_id] = (global_id < size) ? input[global_id] : 0.0f;
    barrier(CLK_LOCAL_MEM_FENCE);

    /* Reduction in local memory */
    for (int stride = local_size / 2; stride > 0; stride >>= 1) {
        if (local_id < stride) {
            scratch[local_id] += scratch[local_id + stride];
        }
        barrier(CLK_LOCAL_MEM_FENCE);
    }

    /* Write result */
    if (local_id == 0) {
        output[get_group_id(0)] = scratch[0];
    }
}

/**
 * Compute exploitability: sum of max(regret, 0) for each infoset
 * Returns sum of positive regrets which is a proxy for exploitability
 */
__kernel void exploitability_kernel(
    __global const float* restrict regrets,
    __global float* restrict exploitability_per_infoset,
    __global const uchar* restrict action_counts,
    int num_infosets,
    int max_actions
) {
    int infoset_id = get_global_id(0);

    if (infoset_id >= num_infosets) return;

    int n_actions = action_counts[infoset_id];
    if (n_actions == 0) {
        exploitability_per_infoset[infoset_id] = 0.0f;
        return;
    }

    int base_idx = infoset_id * max_actions;

    /* Sum of positive regrets */
    float sum_pos = 0.0f;
    for (int a = 0; a < n_actions; a++) {
        float r = regrets[base_idx + a];
        if (r > 0.0f) sum_pos += r;
    }

    exploitability_per_infoset[infoset_id] = sum_pos;
}
