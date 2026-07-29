/*
 * test_gpu_cfr_opencl.c - Tests for GPU-CFR OpenCL backend
 *
 * Copyright (C) 2025 poker-eval contributors
 */

#include "unity.h"
#include <poker_eval/gpu/gpu_cfr.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Check if OpenCL is available */
#ifdef HAVE_OPENCL
#include "../src/gpu/gpu_cfr_opencl.h"
#define OPENCL_AVAILABLE 1
#else
#define OPENCL_AVAILABLE 0
#endif

void setUp(void) {}
void tearDown(void) {}

/* ===== Basic Tests ===== */

void test_gpu_cfr_default_config(void) {
    gpu_cfr_config_t cfg = gpu_cfr_default_config();

    TEST_ASSERT_EQUAL_INT(10000, cfg.num_infosets);
    TEST_ASSERT_EQUAL_INT(8, cfg.max_actions);
    TEST_ASSERT_EQUAL_INT(1000, cfg.max_iterations);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, cfg.regret_discount);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, cfg.strategy_weight);
    TEST_ASSERT_TRUE(cfg.use_sparse);
}

void test_matrix_storage_create(void) {
    cfr_matrix_storage_t* storage = cfr_matrix_storage_create(1000, 4);
    TEST_ASSERT_NOT_NULL(storage);

    TEST_ASSERT_EQUAL_INT(1000, storage->num_infosets);
    TEST_ASSERT_EQUAL_INT(4, storage->max_actions);
    TEST_ASSERT_NOT_NULL(storage->regrets);
    TEST_ASSERT_NOT_NULL(storage->avg_strategy);
    TEST_ASSERT_NOT_NULL(storage->curr_strategy);
    TEST_ASSERT_NOT_NULL(storage->action_counts);
    TEST_ASSERT_NOT_NULL(storage->infoset_keys);

    cfr_matrix_storage_free(storage);
}

void test_sparse_matrix_create(void) {
    /* Create simple sparse matrix: 3x3 with 4 non-zeros */
    int from[] = {0, 0, 1, 2};
    int to[] = {0, 1, 2, 0};
    float probs[] = {0.5f, 0.5f, 1.0f, 1.0f};

    sparse_matrix_csr_t* matrix = sparse_matrix_create_csr(3, from, to, probs, 4);
    TEST_ASSERT_NOT_NULL(matrix);

    TEST_ASSERT_EQUAL_INT(3, matrix->num_rows);
    TEST_ASSERT_EQUAL_INT(3, matrix->num_cols);
    TEST_ASSERT_EQUAL_INT(4, matrix->nnz);

    /* Check row pointers */
    TEST_ASSERT_EQUAL_INT(0, matrix->row_ptr[0]);
    TEST_ASSERT_EQUAL_INT(2, matrix->row_ptr[1]);  /* Row 0 has 2 entries */
    TEST_ASSERT_EQUAL_INT(3, matrix->row_ptr[2]);  /* Row 1 has 1 entry */
    TEST_ASSERT_EQUAL_INT(4, matrix->row_ptr[3]);  /* Row 2 has 1 entry */

    sparse_matrix_free(matrix);
}

#if OPENCL_AVAILABLE

void test_gpu_cfr_opencl_init(void) {
    gpu_cfr_config_t cfg = gpu_cfr_default_config();
    cfg.num_infosets = 100;
    cfg.max_actions = 4;
    cfg.verbose = false;

    gpu_cfr_opencl_context_t* ctx = gpu_cfr_init_opencl(&cfg);

    if (ctx == NULL) {
        /* No OpenCL device available - skip test */
        TEST_IGNORE_MESSAGE("No OpenCL device available");
        return;
    }

    TEST_ASSERT_NOT_NULL(ctx);

    const char* device_name = gpu_cfr_get_device_name_opencl(ctx);
    TEST_ASSERT_NOT_NULL(device_name);
    TEST_ASSERT_TRUE(strlen(device_name) > 0);

    gpu_cfr_free_opencl(ctx);
}

void test_gpu_cfr_opencl_load_state(void) {
    gpu_cfr_config_t cfg = gpu_cfr_default_config();
    cfg.num_infosets = 100;
    cfg.max_actions = 4;
    cfg.verbose = false;

    gpu_cfr_opencl_context_t* ctx = gpu_cfr_init_opencl(&cfg);
    if (!ctx) {
        TEST_IGNORE_MESSAGE("No OpenCL device available");
        return;
    }

    /* Create and populate storage */
    cfr_matrix_storage_t* storage = cfr_matrix_storage_create(100, 4);
    TEST_ASSERT_NOT_NULL(storage);

    /* Initialize with test data */
    for (int i = 0; i < 100; i++) {
        storage->action_counts[i] = 3;
        for (int a = 0; a < 3; a++) {
            storage->regrets[i * 4 + a] = (float)(i + a);
            storage->avg_strategy[i * 4 + a] = 1.0f / 3.0f;
        }
    }

    /* Load to GPU */
    int result = gpu_cfr_load_state_opencl(ctx, storage);
    TEST_ASSERT_EQUAL_INT(0, result);

    /* Download and verify */
    cfr_matrix_storage_t* download = cfr_matrix_storage_create(100, 4);
    result = gpu_cfr_download_state_opencl(ctx, download);
    TEST_ASSERT_EQUAL_INT(0, result);

    /* Verify regrets match */
    for (int i = 0; i < 100; i++) {
        for (int a = 0; a < 3; a++) {
            TEST_ASSERT_FLOAT_WITHIN(0.001f,
                storage->regrets[i * 4 + a],
                download->regrets[i * 4 + a]);
        }
    }

    cfr_matrix_storage_free(storage);
    cfr_matrix_storage_free(download);
    gpu_cfr_free_opencl(ctx);
}

void test_gpu_cfr_opencl_solve(void) {
    gpu_cfr_config_t cfg = gpu_cfr_default_config();
    cfg.num_infosets = 50;
    cfg.max_actions = 3;
    cfg.verbose = false;
    cfg.regret_discount = 1.0f;
    cfg.strategy_weight = 1.0f;

    gpu_cfr_opencl_context_t* ctx = gpu_cfr_init_opencl(&cfg);
    if (!ctx) {
        TEST_IGNORE_MESSAGE("No OpenCL device available");
        return;
    }

    /* Create storage with initial regrets */
    cfr_matrix_storage_t* storage = cfr_matrix_storage_create(50, 3);
    for (int i = 0; i < 50; i++) {
        storage->action_counts[i] = 3;
        /* Initial regrets: all positive */
        storage->regrets[i * 3 + 0] = 1.0f;
        storage->regrets[i * 3 + 1] = 2.0f;
        storage->regrets[i * 3 + 2] = 3.0f;
    }

    int result = gpu_cfr_load_state_opencl(ctx, storage);
    TEST_ASSERT_EQUAL_INT(0, result);

    /* Run a few iterations */
    result = gpu_cfr_solve_opencl(ctx, 5);
    TEST_ASSERT_EQUAL_INT(0, result);

    /* Get stats */
    gpu_cfr_stats_t stats;
    result = gpu_cfr_get_stats_opencl(ctx, &stats);
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_INT(5, stats.iterations_completed);

    cfr_matrix_storage_free(storage);
    gpu_cfr_free_opencl(ctx);
}

void test_gpu_cfr_opencl_reset(void) {
    gpu_cfr_config_t cfg = gpu_cfr_default_config();
    cfg.num_infosets = 100;
    cfg.max_actions = 4;
    cfg.verbose = false;

    gpu_cfr_opencl_context_t* ctx = gpu_cfr_init_opencl(&cfg);
    if (!ctx) {
        TEST_IGNORE_MESSAGE("No OpenCL device available");
        return;
    }

    /* Create storage with non-zero values */
    cfr_matrix_storage_t* storage = cfr_matrix_storage_create(100, 4);
    for (int i = 0; i < 100 * 4; i++) {
        storage->regrets[i] = 1.0f;
        storage->avg_strategy[i] = 1.0f;
    }

    gpu_cfr_load_state_opencl(ctx, storage);

    /* Reset */
    int result = gpu_cfr_reset_opencl(ctx);
    TEST_ASSERT_EQUAL_INT(0, result);

    /* Download and verify zeros */
    cfr_matrix_storage_t* download = cfr_matrix_storage_create(100, 4);
    gpu_cfr_download_state_opencl(ctx, download);

    for (int i = 0; i < 100 * 4; i++) {
        TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, download->regrets[i]);
        TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, download->avg_strategy[i]);
    }

    cfr_matrix_storage_free(storage);
    cfr_matrix_storage_free(download);
    gpu_cfr_free_opencl(ctx);
}

void test_gpu_cfr_opencl_sparse_matrix(void) {
    gpu_cfr_config_t cfg = gpu_cfr_default_config();
    cfg.num_infosets = 10;
    cfg.max_actions = 3;
    cfg.verbose = false;

    gpu_cfr_opencl_context_t* ctx = gpu_cfr_init_opencl(&cfg);
    if (!ctx) {
        TEST_IGNORE_MESSAGE("No OpenCL device available");
        return;
    }

    /* Create simple sparse transition matrix */
    int from[] = {0, 0, 1, 2, 3, 4};
    int to[] = {1, 2, 3, 4, 5, 6};
    float probs[] = {0.5f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f};

    sparse_matrix_csr_t* matrix = sparse_matrix_create_csr(10, from, to, probs, 6);
    TEST_ASSERT_NOT_NULL(matrix);

    int result = gpu_cfr_load_sparse_matrix_opencl(ctx, matrix);
    TEST_ASSERT_EQUAL_INT(0, result);

    sparse_matrix_free(matrix);
    gpu_cfr_free_opencl(ctx);
}

void test_gpu_cfr_opencl_device_count(void) {
    int count = gpu_cfr_get_device_count_opencl();
    /* Should return 0 or more - just verify it doesn't crash */
    TEST_ASSERT_TRUE(count >= 0);
}

#else /* !OPENCL_AVAILABLE */

void test_gpu_cfr_opencl_init(void) {
    TEST_IGNORE_MESSAGE("OpenCL not available - skipping test");
}

void test_gpu_cfr_opencl_load_state(void) {
    TEST_IGNORE_MESSAGE("OpenCL not available - skipping test");
}

void test_gpu_cfr_opencl_solve(void) {
    TEST_IGNORE_MESSAGE("OpenCL not available - skipping test");
}

void test_gpu_cfr_opencl_reset(void) {
    TEST_IGNORE_MESSAGE("OpenCL not available - skipping test");
}

void test_gpu_cfr_opencl_sparse_matrix(void) {
    TEST_IGNORE_MESSAGE("OpenCL not available - skipping test");
}

void test_gpu_cfr_opencl_device_count(void) {
    TEST_IGNORE_MESSAGE("OpenCL not available - skipping test");
}

#endif /* OPENCL_AVAILABLE */

int main(void) {
    UNITY_BEGIN();

    /* Basic tests (always run) */
    RUN_TEST(test_gpu_cfr_default_config);
    RUN_TEST(test_matrix_storage_create);
    RUN_TEST(test_sparse_matrix_create);

    /* OpenCL-specific tests */
    RUN_TEST(test_gpu_cfr_opencl_init);
    RUN_TEST(test_gpu_cfr_opencl_load_state);
    RUN_TEST(test_gpu_cfr_opencl_solve);
    RUN_TEST(test_gpu_cfr_opencl_reset);
    RUN_TEST(test_gpu_cfr_opencl_sparse_matrix);
    RUN_TEST(test_gpu_cfr_opencl_device_count);

    return UNITY_END();
}
