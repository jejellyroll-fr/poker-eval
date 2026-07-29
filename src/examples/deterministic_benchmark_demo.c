/*
 * deterministic_benchmark_demo.c - Demonstration of deterministic benchmarking
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

#include <poker_eval/core/deterministic_benchmark.h>
#include <poker_eval/core/modern_cardmask.h>

static void demo_rng_determinism(void) {
    printf("\n=== RNG Determinism Demo ===\n");

    uint64_t seed = 42;
    DeterministicRNG rng1, rng2;

    /* Seed both RNGs with same value */
    det_rng_seed(&rng1, seed);
    det_rng_seed(&rng2, seed);

    printf("Testing RNG determinism with seed %llu:\n", (unsigned long long)seed);

    /* Generate sequences and verify they match */
    bool sequences_match = true;
    for (int i = 0; i < 10; i++) {
        uint32_t val1 = det_rng_next(&rng1);
        uint32_t val2 = det_rng_next(&rng2);

        printf("  Step %d: RNG1=%u, RNG2=%u %s\n",
               i + 1, val1, val2, (val1 == val2) ? "✓" : "✗");

        if (val1 != val2) {
            sequences_match = false;
        }
    }

    printf("Determinism test: %s\n", sequences_match ? "PASSED" : "FAILED");

    /* Test card sampling determinism */
    printf("\nTesting card sampling determinism:\n");
    det_rng_seed(&rng1, seed);
    det_rng_seed(&rng2, seed);

    mask_t full_deck = (1ULL << 52) - 1;

    for (int trial = 0; trial < 3; trial++) {
        mask_t hand1 = det_sample_k_cards(&rng1, full_deck, 5);
        mask_t hand2 = det_sample_k_cards(&rng2, full_deck, 5);

        printf("  Trial %d: Hand1=0x%llx, Hand2=0x%llx %s\n",
               trial + 1, (unsigned long long)hand1, (unsigned long long)hand2,
               (hand1 == hand2) ? "✓" : "✗");
    }
}

static void demo_statistical_analysis(void) {
    printf("\n=== Statistical Analysis Demo ===\n");

    BenchmarkConfig config = benchmark_config_holdem_evaluation();
    config.stats.min_iterations = 20;
    config.stats.max_iterations = 50;
    config.stats.auto_convergence = true;
    config.stats.target_margin_error = 5.0;

    printf("Running benchmark: %s\n", config.description);
    printf("Configuration:\n");
    printf("  Min iterations: %u\n", config.stats.min_iterations);
    printf("  Max iterations: %u\n", config.stats.max_iterations);
    printf("  Target margin of error: %.1f%%\n", config.stats.target_margin_error);
    printf("  Confidence level: %.1f%%\n", config.stats.confidence_level * 100);

    BenchmarkResult* result = benchmark_run(&config);
    if (!result) {
        printf("Benchmark failed to run\n");
        return;
    }

    benchmark_result_print(result);

    /* Test reproducibility */
    printf("\n--- Reproducibility Test ---\n");
    BenchmarkResult* result2 = benchmark_run(&config);
    if (!result2) {
        printf("Second benchmark failed to run\n");
        benchmark_result_free(result);
        return;
    }

    printf("First run mean:  %.0f ops/sec\n", result->stats.mean);
    printf("Second run mean: %.0f ops/sec\n", result2->stats.mean);
    printf("Difference: %.2f%% %s\n",
           fabs(result->stats.mean - result2->stats.mean) / result->stats.mean * 100,
           (fabs(result->stats.mean - result2->stats.mean) < result->stats.margin_error) ? "(within margin)" : "(outside margin)");

    benchmark_result_free(result);
    benchmark_result_free(result2);
}

static void demo_performance_comparison(void) {
    printf("\n=== Performance Comparison Demo ===\n");

    /* Simulate different algorithm performance */
    BenchmarkConfig config_fast = benchmark_config_monte_carlo_standard();
    config_fast.name = "fast_algorithm";
    config_fast.description = "Fast Monte Carlo algorithm";
    config_fast.expected_ops_per_sec = 150000.0;

    BenchmarkConfig config_slow = benchmark_config_monte_carlo_standard();
    config_slow.name = "slow_algorithm";
    config_slow.description = "Slower but more accurate algorithm";
    config_slow.base_seed = config_fast.base_seed;  /* Same seed for fair comparison */
    config_slow.expected_ops_per_sec = 75000.0;

    /* Reduce iterations for demo */
    config_fast.stats.min_iterations = 10;
    config_fast.stats.max_iterations = 20;
    config_slow.stats.min_iterations = 10;
    config_slow.stats.max_iterations = 20;

    printf("Comparing two algorithms with same random seed...\n");

    BenchmarkResult* result_fast = benchmark_run(&config_fast);
    BenchmarkResult* result_slow = benchmark_run(&config_slow);

    if (result_fast && result_slow) {
        printf("\nResults Comparison:\n");
        printf("Fast algorithm: %.0f ± %.0f ops/sec\n",
               result_fast->stats.mean, result_fast->stats.margin_error);
        printf("Slow algorithm: %.0f ± %.0f ops/sec\n",
               result_slow->stats.mean, result_slow->stats.margin_error);

        double speedup = result_fast->stats.mean / result_slow->stats.mean;
        printf("Speedup ratio: %.2fx\n", speedup);

        /* Check if difference is statistically significant */
        double combined_error = sqrt(result_fast->stats.margin_error * result_fast->stats.margin_error +
                                   result_slow->stats.margin_error * result_slow->stats.margin_error);
        double difference = fabs(result_fast->stats.mean - result_slow->stats.mean);

        printf("Statistical significance: %s\n",
               (difference > combined_error) ? "Significant" : "Not significant");
    }

    benchmark_result_free(result_fast);
    benchmark_result_free(result_slow);
}

static void demo_benchmark_suite(void) {
    printf("\n=== Benchmark Suite Demo ===\n");

    /* Create a mini benchmark suite */
    BenchmarkConfig benchmarks[3];
    benchmarks[0] = benchmark_config_holdem_evaluation();
    benchmarks[1] = benchmark_config_monte_carlo_standard();
    benchmarks[2] = benchmark_config_joker_expansion();

    /* Reduce iterations for demo */
    for (int i = 0; i < 3; i++) {
        benchmarks[i].stats.min_iterations = 5;
        benchmarks[i].stats.max_iterations = 10;
        benchmarks[i].base_seed = 987654321;  /* Common seed */
    }

    BenchmarkSuite suite = {
        .suite_name = "Demo Suite",
        .benchmarks = benchmarks,
        .benchmark_count = 3,
        .base_seed = 987654321
    };

    printf("Running benchmark suite: %s\n", suite.suite_name);
    printf("Number of benchmarks: %u\n", suite.benchmark_count);

    double suite_start = benchmark_get_time();

    /* Run each benchmark individually for demo */
    int passed = 0, failed = 0;
    for (uint32_t i = 0; i < suite.benchmark_count; i++) {
        printf("\nRunning: %s\n", suite.benchmarks[i].name);

        BenchmarkResult* result = benchmark_run(&suite.benchmarks[i]);
        if (result) {
            printf("  Status: %s\n", result->passed ? "PASSED" : "FAILED");
            printf("  Performance: %.0f ops/sec\n", result->stats.mean);

            if (result->passed) passed++;
            else failed++;

            benchmark_result_free(result);
        } else {
            printf("  Status: ERROR\n");
            failed++;
        }
    }

    double suite_time = benchmark_get_time() - suite_start;

    printf("\nSuite Summary:\n");
    printf("  Total time: %.2f seconds\n", suite_time);
    printf("  Passed: %d\n", passed);
    printf("  Failed: %d\n", failed);
    printf("  Success rate: %.1f%%\n", (double)passed / (passed + failed) * 100);
}

static void demo_regression_detection(void) {
    printf("\n=== Regression Detection Demo ===\n");

    BenchmarkConfig baseline_config = benchmark_config_holdem_evaluation();
    baseline_config.name = "baseline_version";
    baseline_config.stats.min_iterations = 15;
    baseline_config.stats.max_iterations = 25;

    BenchmarkConfig current_config = baseline_config;
    current_config.name = "current_version";
    current_config.expected_ops_per_sec = baseline_config.expected_ops_per_sec * 0.85;  /* Simulate 15% regression */

    printf("Simulating performance regression detection...\n");
    printf("Baseline expected: %.0f ops/sec\n", baseline_config.expected_ops_per_sec);
    printf("Current expected: %.0f ops/sec\n", current_config.expected_ops_per_sec);

    BenchmarkResult* baseline = benchmark_run(&baseline_config);
    BenchmarkResult* current = benchmark_run(&current_config);

    if (baseline && current) {
        double performance_change = (current->stats.mean / baseline->stats.mean - 1.0) * 100;
        bool regression_detected = (performance_change < -10.0);  /* More than 10% slower */

        printf("\nRegression Analysis:\n");
        printf("  Baseline: %.0f ± %.0f ops/sec\n",
               baseline->stats.mean, baseline->stats.margin_error);
        printf("  Current:  %.0f ± %.0f ops/sec\n",
               current->stats.mean, current->stats.margin_error);
        printf("  Change: %.1f%%\n", performance_change);
        printf("  Regression detected: %s\n", regression_detected ? "YES" : "No");

        if (regression_detected) {
            printf("  ⚠️  Performance regression detected!\n");
        } else {
            printf("  ✅ Performance within acceptable range\n");
        }
    }

    benchmark_result_free(baseline);
    benchmark_result_free(current);
}

int main(void) {
    printf("Deterministic Benchmarking System Demonstration\n");
    printf("===============================================\n");

    if (!benchmark_init()) {
        printf("Failed to initialize benchmark system\n");
        return 1;
    }

    demo_rng_determinism();
    demo_statistical_analysis();
    demo_performance_comparison();
    demo_benchmark_suite();
    demo_regression_detection();

    benchmark_cleanup();

    printf("\nDemonstration completed successfully.\n");
    printf("\nKey Features Demonstrated:\n");
    printf("✓ Deterministic RNG with reproducible results\n");
    printf("✓ Statistical analysis with confidence intervals\n");
    printf("✓ Performance comparison with significance testing\n");
    printf("✓ Benchmark suite execution\n");
    printf("✓ Regression detection capabilities\n");

    return 0;
}
