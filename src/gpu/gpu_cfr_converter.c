/*
 * gpu_cfr_converter.c - Convert CPU CFR storage (hash-map) to GPU matrix format
 *
 * Copyright (C) 2025 poker-eval contributors
 *
 * Converts between sparse hash-map CFR storage (CPU) and dense matrix storage (GPU).
 */

#include <poker_eval/gpu/gpu_cfr.h>
#include <poker_eval/engine/solvers/cfr/cfr_core.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* Context for iterator callback */
typedef struct
{
    cfr_matrix_storage_t *matrix;
    int current_index;
    int max_actions;
} converter_context_t;

/* Iterator callback - copy one entry from hash-map to matrix row */
static void copy_entry_to_matrix(
    uint64_t key,
    int n,
    const double *regret,
    const double *avg,
    void *user)
{
    converter_context_t *ctx = (converter_context_t *)user;

    if (ctx->current_index >= ctx->matrix->num_infosets)
    {
        fprintf(stderr, "Warning: More infosets than matrix capacity\n");
        return;
    }

    /* Clamp action count to max_actions */
    int actual_actions = (n > ctx->max_actions) ? ctx->max_actions : n;

    /* Store infoset key and action count */
    ctx->matrix->infoset_keys[ctx->current_index] = key;
    ctx->matrix->action_counts[ctx->current_index] = (uint8_t)actual_actions;

    /* Copy regrets to matrix row */
    int base_idx = ctx->current_index * ctx->max_actions;

    for (int a = 0; a < actual_actions; a++)
    {
        ctx->matrix->regrets[base_idx + a] = (float)regret[a];
        ctx->matrix->avg_strategy[base_idx + a] = (float)avg[a];
    }

    /* Zero out unused action slots */
    for (int a = actual_actions; a < ctx->max_actions; a++)
    {
        ctx->matrix->regrets[base_idx + a] = 0.0f;
        ctx->matrix->avg_strategy[base_idx + a] = 0.0f;
    }

    ctx->current_index++;
}

/* ===== Public API ===== */

cfr_matrix_storage_t *cfr_convert_to_matrix(
    cfr_storage_t *cpu_storage,
    int max_actions)
{
    if (!cpu_storage || max_actions <= 0)
        return NULL;

    /* Count infosets in CPU storage */
    size_t num_infosets = cfr_storage_count_infosets(cpu_storage);

    if (num_infosets == 0)
    {
        fprintf(stderr, "cfr_convert_to_matrix: CPU storage is empty\n");
        return NULL;
    }

    /* Create matrix storage */
    cfr_matrix_storage_t *matrix = cfr_matrix_storage_create((int)num_infosets, max_actions);
    if (!matrix)
        return NULL;

    /* Iterate over CPU storage and copy to matrix */
    converter_context_t ctx;
    ctx.matrix = matrix;
    ctx.current_index = 0;
    ctx.max_actions = max_actions;

    cfr_storage_iterate(cpu_storage, copy_entry_to_matrix, &ctx);

    if (ctx.current_index != (int)num_infosets)
    {
        fprintf(stderr, "Warning: Expected %zu infosets but copied %d\n",
                num_infosets, ctx.current_index);
    }

    return matrix;
}

int cfr_convert_from_matrix(
    const cfr_matrix_storage_t *matrix,
    cfr_storage_t *cpu_storage)
{
    if (!matrix || !cpu_storage)
        return -1;

    /* Iterate over matrix rows and update CPU storage */
    for (int i = 0; i < matrix->num_infosets; i++)
    {
        uint64_t key = matrix->infoset_keys[i];
        int n_actions = matrix->action_counts[i];

        if (n_actions == 0)
            continue;

        int base_idx = i * matrix->max_actions;

        /* Convert float arrays to double for CPU storage */
        double *regret_delta = (double *)malloc(n_actions * sizeof(double));
        double *avg_strategy = (double *)malloc(n_actions * sizeof(double));

        if (!regret_delta || !avg_strategy)
        {
            free(regret_delta);
            free(avg_strategy);
            return -1;
        }

        for (int a = 0; a < n_actions; a++)
        {
            regret_delta[a] = (double)matrix->regrets[base_idx + a];
            avg_strategy[a] = (double)matrix->avg_strategy[base_idx + a];
        }

        /* Update CPU storage (replace existing regrets and avg) */
        cfr_storage_update_regret(cpu_storage, key, n_actions, regret_delta, 0.0);
        cfr_storage_update_avg(cpu_storage, key, n_actions, avg_strategy, 1.0);

        free(regret_delta);
        free(avg_strategy);
    }

    return 0;
}

/* ===== Utilities ===== */

void cfr_matrix_print_summary(const cfr_matrix_storage_t *matrix)
{
    if (!matrix)
        return;

    printf("Matrix Storage Summary:\n");
    printf("  Infosets: %d\n", matrix->num_infosets);
    printf("  Max actions: %d\n", matrix->max_actions);
    printf("  Matrix size: %d × %d\n", matrix->num_infosets, matrix->max_actions);
    printf("  Memory: %.2f MB\n",
           (double)(matrix->num_infosets * matrix->max_actions * 3 * sizeof(float) +
            matrix->num_infosets * (sizeof(uint8_t) + sizeof(uint64_t))) /
               (1024.0 * 1024.0));

    /* Count non-zero regrets */
    int nnz_regrets = 0;
    int nnz_strategy = 0;

    for (int i = 0; i < matrix->num_infosets; i++)
    {
        int base_idx = i * matrix->max_actions;
        int n_actions = matrix->action_counts[i];

        for (int a = 0; a < n_actions; a++)
        {
            if (fabsf(matrix->regrets[base_idx + a]) > 1e-9f)
                nnz_regrets++;
            if (fabsf(matrix->avg_strategy[base_idx + a]) > 1e-9f)
                nnz_strategy++;
        }
    }

    printf("  Non-zero regrets: %d / %d (%.1f%% sparse)\n",
           nnz_regrets, matrix->num_infosets * matrix->max_actions,
           100.0 * (1.0 - (double)nnz_regrets / (matrix->num_infosets * matrix->max_actions)));
    printf("  Non-zero strategy: %d / %d (%.1f%% sparse)\n",
           nnz_strategy, matrix->num_infosets * matrix->max_actions,
           100.0 * (1.0 - (double)nnz_strategy / (matrix->num_infosets * matrix->max_actions)));
}

int cfr_matrix_validate(const cfr_matrix_storage_t *matrix)
{
    if (!matrix)
        return -1;

    int errors = 0;

    for (int i = 0; i < matrix->num_infosets; i++)
    {
        int n_actions = matrix->action_counts[i];

        if (n_actions <= 0 || n_actions > matrix->max_actions)
        {
            fprintf(stderr, "Invalid action count at infoset %d: %d\n", i, n_actions);
            errors++;
            continue;
        }

        int base_idx = i * matrix->max_actions;

        /* Check strategy normalization (sum should be ~1.0 for non-zero strategies) */
        float strategy_sum = 0.0f;
        for (int a = 0; a < n_actions; a++)
        {
            strategy_sum += matrix->avg_strategy[base_idx + a];
        }

        /* Skip validation if strategy is all zeros (not yet trained) */
        if (strategy_sum > 1e-6f && (strategy_sum < 0.9f || strategy_sum > 1.1f))
        {
            fprintf(stderr, "Strategy not normalized at infoset %d: sum = %.6f\n",
                    i, strategy_sum);
            errors++;
        }

        /* Check for NaN/Inf */
        for (int a = 0; a < matrix->max_actions; a++)
        {
            if (!isfinite(matrix->regrets[base_idx + a]))
            {
                fprintf(stderr, "Invalid regret at [%d,%d]: %f\n",
                        i, a, matrix->regrets[base_idx + a]);
                errors++;
            }
            if (!isfinite(matrix->avg_strategy[base_idx + a]))
            {
                fprintf(stderr, "Invalid strategy at [%d,%d]: %f\n",
                        i, a, matrix->avg_strategy[base_idx + a]);
                errors++;
            }
        }
    }

    if (errors > 0)
    {
        fprintf(stderr, "Matrix validation found %d errors\n", errors);
        return -1;
    }

    return 0;
}
