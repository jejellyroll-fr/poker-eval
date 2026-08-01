/*
 * deterministic_benchmark.c - Implementation of deterministic benchmarking system
 */

#include <poker_eval/core/deterministic_benchmark.h>
#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/core/eval_context.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

#ifdef _WIN32
    #include <windows.h>
    #include <sys/types.h>
#else
    #include <sys/time.h>
    #include <unistd.h>
    #include <sys/utsname.h>
#endif

#ifdef _OPENMP
#include "poker_eval/utils/omp_compat.h"
#endif

/*
 * ============================================================================
 * DETERMINISTIC RNG IMPLEMENTATION (PCG)
 * ============================================================================
 */

/* PCG constants */
#define PCG_DEFAULT_MULTIPLIER 6364136223846793005ULL
#define PCG_DEFAULT_INCREMENT  1442695040888963407ULL

void det_rng_seed(DeterministicRNG* rng, uint64_t seed) {
    if (!rng) return;

    rng->state = 0;
    rng->inc = (seed << 1) | 1;  /* Ensure increment is odd */
    rng->calls = 0;

    /* Warm up the generator */
    det_rng_next(rng);
    rng->state += seed;
    det_rng_next(rng);
}

uint32_t det_rng_next(DeterministicRNG* rng) {
    if (!rng) return 0;

    uint64_t old_state = rng->state;
    rng->state = old_state * PCG_DEFAULT_MULTIPLIER + rng->inc;
    rng->calls++;

    /* XorShift and rotate */
    uint32_t xorshifted = (uint32_t)(((old_state >> 18) ^ old_state) >> 27);
    uint32_t rot = (uint32_t)(old_state >> 59);
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
}

uint32_t det_rng_bounded(DeterministicRNG* rng, uint32_t bound) {
    if (!rng || bound == 0) return 0;

    /* Avoid modulo bias using rejection sampling */
    uint32_t threshold = -bound % bound;
    uint32_t r;

    do {
        r = det_rng_next(rng);
    } while (r < threshold);

    return r % bound;
}

uint64_t det_rng_next64(DeterministicRNG* rng) {
    uint64_t high = det_rng_next(rng);
    uint64_t low = det_rng_next(rng);
    return (high << 32) | low;
}

double det_rng_double(DeterministicRNG* rng) {
    uint64_t x = det_rng_next64(rng);
    return (double)(x >> 11) * 0x1.0p-53;  /* Generate double in [0, 1) */
}

mask_t det_sample_k_cards(DeterministicRNG* rng, mask_t available, int k) {
    if (!rng || k <= 0) return 0;

    /* Extract available positions */
    int positions[64];
    int count = 0;
    mask_t tmp = available;

    while (tmp && count < 64) {
        int pos = __builtin_ctzll(tmp);
        positions[count++] = pos;
        tmp &= tmp - 1;
    }

    if (count < k) return 0;

    /* Deterministic Fisher-Yates shuffle for first k positions */
    for (int i = 0; i < k; i++) {
        int j = i + det_rng_bounded(rng, count - i);
        int temp = positions[i];
        positions[i] = positions[j];
        positions[j] = temp;
    }

    /* Build result mask */
    mask_t result = 0;
    for (int i = 0; i < k; i++) {
        result |= (1ULL << positions[i]);
    }

    return result;
}

void det_shuffle_array(DeterministicRNG* rng, void* array, size_t count, size_t elem_size) {
    if (!rng || !array || count <= 1 || elem_size == 0) return;

    char* arr = (char*)array;
    char* temp = malloc(elem_size);
    if (!temp) return;

    for (size_t i = 0; i < count - 1; i++) {
        size_t j = i + det_rng_bounded(rng, (uint32_t)(count - i));

        /* Swap elements i and j */
        memcpy(temp, arr + i * elem_size, elem_size);
        memcpy(arr + i * elem_size, arr + j * elem_size, elem_size);
        memcpy(arr + j * elem_size, temp, elem_size);
    }

    free(temp);
}

/*
 * ============================================================================
 * TIMING AND SYSTEM UTILITIES
 * ============================================================================
 */

double benchmark_get_time(void) {
#ifdef _WIN32
    LARGE_INTEGER frequency, counter;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)frequency.QuadPart;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
#endif
}

const char* benchmark_get_system_info(void) {
    static char info[512];
    static bool initialized = false;

    if (!initialized) {
        const char* compiler_name;
        const char* compiler_version;

#ifdef _MSC_VER
        compiler_name = "MSVC";
        #if _MSC_VER >= 1930
            compiler_version = "2022";
        #elif _MSC_VER >= 1920
            compiler_version = "2019";
        #else
            compiler_version = "Unknown";
        #endif
#elif defined(__GNUC__)
        compiler_name = "GCC";
        compiler_version = __VERSION__;
#elif defined(__clang__)
        compiler_name = "Clang";
        compiler_version = __VERSION__;
#else
        compiler_name = "Unknown";
        compiler_version = "Unknown";
#endif

        /* Field widths are capped so the worst case provably fits in info[].
         * Without them GCC reports -Wformat-truncation, since __VERSION__ and
         * the utsname fields can each be long enough to overrun the buffer on
         * their own. Truncating a diagnostic string is harmless; silently
         * risking it is what the warning is about. */
#ifdef _WIN32
        snprintf(info, sizeof(info),
                "System: Windows, Compiler: %.16s %.128s",
                compiler_name, compiler_version);
#else
        struct utsname sys_info;
        if (uname(&sys_info) == 0) {
            snprintf(info, sizeof(info),
                    "System: %.32s %.64s %.32s, Compiler: %.16s %.128s",
                    sys_info.sysname, sys_info.release, sys_info.machine,
                    compiler_name, compiler_version);
        } else {
            snprintf(info, sizeof(info), "%s", "System info unavailable");
        }
#endif
        initialized = true;
    }

    return info;
}

/*
 * ============================================================================
 * STATISTICAL ANALYSIS
 * ============================================================================
 */

static double calculate_mean(const double* values, uint32_t count) {
    if (!values || count == 0) return 0.0;

    double sum = 0.0;
    for (uint32_t i = 0; i < count; i++) {
        sum += values[i];
    }
    return sum / count;
}

static double calculate_std_dev(const double* values, uint32_t count, double mean) {
    if (!values || count <= 1) return 0.0;

    double sum_sq_diff = 0.0;
    for (uint32_t i = 0; i < count; i++) {
        double diff = values[i] - mean;
        sum_sq_diff += diff * diff;
    }
    return sqrt(sum_sq_diff / (count - 1));
}

static double calculate_median(double* values, uint32_t count) {
    if (!values || count == 0) return 0.0;

    /* Sort values for median calculation */
    for (uint32_t i = 0; i < count - 1; i++) {
        for (uint32_t j = 0; j < count - i - 1; j++) {
            if (values[j] > values[j + 1]) {
                double temp = values[j];
                values[j] = values[j + 1];
                values[j + 1] = temp;
            }
        }
    }

    if (count % 2 == 0) {
        return (values[count / 2 - 1] + values[count / 2]) / 2.0;
    } else {
        return values[count / 2];
    }
}

static void calculate_statistics(BenchmarkMeasurement* measurements,
                                uint32_t count,
                                double confidence_level,
                                BenchmarkStatistics* stats) {
    if (!measurements || !stats || count == 0) return;

    /* Extract ops/sec values */
    double* ops_values = NULL;
    ops_values = malloc(count * sizeof(double));
    if (!ops_values) return;

    for (uint32_t i = 0; i < count; i++) {
        ops_values[i] = measurements[i].ops_per_second;
    }

    /* Calculate basic statistics */
    stats->sample_count = count;
    stats->mean = calculate_mean(ops_values, count);
    stats->std_dev = calculate_std_dev(ops_values, count, stats->mean);
    stats->median = calculate_median(ops_values, count);  /* Note: this sorts the array */
    stats->confidence_level = confidence_level;

    /* Find min/max - median sorts the array, so min is at [0], max at [count-1] */
    stats->min_value = ops_values[0];
    stats->max_value = ops_values[count - 1];

    /* Calculate margin of error (assuming t-distribution) */
    if (count > 1) {
        /* Simplified: use t ≈ 2.0 for 95% confidence with reasonable sample sizes */
        double t_value = (confidence_level >= 0.95) ? 2.0 : 1.65;
        stats->margin_error = t_value * stats->std_dev / sqrt(count);
    } else {
        stats->margin_error = 0.0;
    }

    /* Check convergence (coefficient of variation < 5%) */
    if (stats->mean > 0) {
        double cv = stats->std_dev / stats->mean;
        stats->converged = (cv < 0.05);
    } else {
        stats->converged = false;
    }

    free(ops_values);
}

/*
 * ============================================================================
 * BENCHMARK CONFIGURATIONS
 * ============================================================================
 */

static BenchmarkStatConfig default_stat_config(void) {
    BenchmarkStatConfig config = {0};
    config.min_iterations = 10;
    config.max_iterations = 1000;
    config.target_margin_error = 5.0;  /* 5% */
    config.confidence_level = 0.95;    /* 95% */
    config.auto_convergence = true;
    config.warmup_iterations = 3;
    return config;
}

BenchmarkConfig benchmark_config_holdem_evaluation(void) {
    BenchmarkConfig config = {0};
    config.type = BENCH_TYPE_EVALUATION;
    config.name = "holdem_evaluation";
    config.description = "Texas Hold'em hand evaluation performance";
    config.base_seed = 12345;
    config.enable_multithreading = false;
    config.thread_count = 1;
    config.stats = default_stat_config();
    config.expected_ops_per_sec = 10000000.0;  /* 10M ops/sec */
    config.performance_tolerance = 20.0;       /* 20% tolerance */

    config.params.evaluation.hand_size = 7;
    config.params.evaluation.num_players = 2;
    config.params.evaluation.rules = EVAL_RULES_HIGH;

    return config;
}

BenchmarkConfig benchmark_config_monte_carlo_standard(void) {
    BenchmarkConfig config = {0};
    config.type = BENCH_TYPE_MONTE_CARLO;
    config.name = "monte_carlo_standard";
    config.description = "Standard Monte Carlo simulation performance";
    config.base_seed = 54321;
    config.enable_multithreading = true;
    config.thread_count = 0;  /* Auto-detect */
    config.stats = default_stat_config();
    config.stats.min_iterations = 5;
    config.expected_ops_per_sec = 100000.0;
    config.performance_tolerance = 25.0;

    config.params.monte_carlo.trials = 10000;
    config.params.monte_carlo.fixed_board = 0;  /* No fixed board */
    config.params.monte_carlo.num_players = 6;

    return config;
}

BenchmarkConfig benchmark_config_joker_expansion(void) {
    BenchmarkConfig config = {0};
    config.type = BENCH_TYPE_JOKER_EXPANSION;
    config.name = "joker_expansion";
    config.description = "Joker expansion performance";
    config.base_seed = 98765;
    config.enable_multithreading = false;
    config.thread_count = 1;
    config.stats = default_stat_config();
    config.expected_ops_per_sec = 50000.0;
    config.performance_tolerance = 30.0;

    return config;
}

/*
 * ============================================================================
 * BENCHMARK EXECUTION
 * ============================================================================
 */

static bool benchmark_system_initialized = false;
static DeterministicRNG global_rng;

bool benchmark_init(void) {
    if (benchmark_system_initialized) return true;

    det_rng_seed(&global_rng, 1234567890ULL);
    benchmark_system_initialized = true;

    return true;
}

void benchmark_cleanup(void) {
    benchmark_system_initialized = false;
}

static BenchmarkMeasurement run_single_measurement(const BenchmarkConfig* config,
                                                   DeterministicRNG* rng) {
    BenchmarkMeasurement measurement = {0};

    double start_time = benchmark_get_time();

    /* Simulate workload based on benchmark type */
    uint64_t operations = 0;
    switch (config->type) {
        case BENCH_TYPE_EVALUATION: {
            /* Simulate hand evaluations */
            mask_t deck = (1ULL << 52) - 1;  /* Full deck */
            for (int i = 0; i < 10000; i++) {
                mask_t hand = det_sample_k_cards(rng, deck,
                                               config->params.evaluation.hand_size);
                if (hand) operations++;
            }
            break;
        }

        case BENCH_TYPE_MONTE_CARLO: {
            /* Simulate Monte Carlo iterations */
            mask_t deck = (1ULL << 52) - 1;
            uint32_t trials = config->params.monte_carlo.trials;
            for (uint32_t i = 0; i < trials; i++) {
                for (int p = 0; p < config->params.monte_carlo.num_players; p++) {
                    mask_t hand = det_sample_k_cards(rng, deck, 2);
                    if (hand) operations++;
                }
            }
            break;
        }

        case BENCH_TYPE_JOKER_EXPANSION: {
            /* Simulate joker expansions */
            for (int i = 0; i < 1000; i++) {
                /* Simulate expansion to multiple cards */
                for (int exp = 0; exp < 10; exp++) {
                    uint32_t dummy = det_rng_next(rng);
                    operations += (dummy & 1);  /* Simulated evaluation */
                }
            }
            break;
        }

        case BENCH_TYPE_ENUMERATION:
        case BENCH_TYPE_RANGE_EQUITY:
        case BENCH_TYPE_CACHE_PERFORMANCE:
        case BENCH_TYPE_MEMORY_BANDWIDTH:
        case BENCH_TYPE_MAX:
        default:
            /* Default workload */
            for (int i = 0; i < 10000; i++) {
                uint32_t dummy = det_rng_next(rng);
                operations += (dummy & 1);
            }
            break;
    }

    double end_time = benchmark_get_time();

    measurement.elapsed_time = end_time - start_time;
    measurement.operations = operations;
    measurement.ops_per_second = (measurement.elapsed_time > 0) ?
                               (double)operations / measurement.elapsed_time : 0.0;
    measurement.memory_used = 0;  /* Would need platform-specific code */
    measurement.cache_hits = 0;
    measurement.cache_misses = 0;

    return measurement;
}

BenchmarkResult* benchmark_run(const BenchmarkConfig* config) {
    if (!config || !benchmark_config_validate(config)) return NULL;

    if (!benchmark_system_initialized) {
        benchmark_init();
    }

    BenchmarkResult* result = calloc(1, sizeof(BenchmarkResult));
    if (!result) return NULL;

    result->config = *config;

    /* Setup deterministic RNG */
    DeterministicRNG rng;
    det_rng_seed(&rng, config->base_seed);

    /* Allocate measurement array */
    uint32_t max_measurements = config->stats.max_iterations + config->stats.warmup_iterations;
    result->measurements = malloc(max_measurements * sizeof(BenchmarkMeasurement));
    if (!result->measurements) {
        free(result);
        return NULL;
    }

    double setup_start = benchmark_get_time();

    /* Warmup iterations */
    for (uint32_t i = 0; i < config->stats.warmup_iterations; i++) {
        BenchmarkMeasurement warmup = run_single_measurement(config, &rng);
        (void)warmup;  /* Discard warmup results */
    }

    result->setup_time = benchmark_get_time() - setup_start;

    /* Actual benchmark iterations */
    uint32_t measurement_count = 0;

    for (uint32_t i = 0; i < config->stats.max_iterations; i++) {
        result->measurements[measurement_count] = run_single_measurement(config, &rng);
        measurement_count++;

        /* Check for early convergence */
        if (measurement_count >= config->stats.min_iterations && config->stats.auto_convergence) {
            BenchmarkStatistics temp_stats;
            calculate_statistics(result->measurements, measurement_count,
                                config->stats.confidence_level, &temp_stats);

            if (temp_stats.converged &&
                temp_stats.margin_error <= config->stats.target_margin_error) {
                break;
            }
        }
    }

    result->measurement_count = measurement_count;

    double benchmark_end = benchmark_get_time();
    result->teardown_time = 0.001;  /* Minimal teardown */
    result->total_time = benchmark_end - setup_start;

    /* Calculate final statistics */
    calculate_statistics(result->measurements, result->measurement_count,
                        config->stats.confidence_level, &result->stats);

    /* Performance validation */
    double performance_ratio = result->stats.mean / config->expected_ops_per_sec;
    double deviation = fabs(1.0 - performance_ratio) * 100.0;

    result->passed = (deviation <= config->performance_tolerance);
    if (!result->passed) {
        static char failure_msg[256];
        snprintf(failure_msg, sizeof(failure_msg),
                "Performance deviation %.1f%% exceeds tolerance %.1f%%",
                deviation, config->performance_tolerance);
        result->failure_reason = failure_msg;
    }

    /* System information */
    result->system_info = benchmark_get_system_info();
    result->rng_seed = config->base_seed;

    static char timestamp[64];
    time_t now = time(NULL);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
    result->timestamp = timestamp;

    return result;
}

/*
 * ============================================================================
 * REPORTING
 * ============================================================================
 */

void benchmark_result_print(const BenchmarkResult* result) {
    if (!result) return;

    printf("\n=== %s ===\n", result->config.name);
    printf("Description: %s\n", result->config.description);
    printf("Timestamp: %s\n", result->timestamp);
    printf("System: %s\n", result->system_info);
    printf("Random seed: %llu\n", (unsigned long long)result->rng_seed);
    printf("\nPerformance Results:\n");
    printf("  Measurements: %u\n", result->stats.sample_count);
    printf("  Mean ops/sec: %.0f\n", result->stats.mean);
    printf("  Median ops/sec: %.0f\n", result->stats.median);
    printf("  Std deviation: %.0f (%.1f%%)\n",
           result->stats.std_dev,
           result->stats.mean > 0 ? (result->stats.std_dev / result->stats.mean * 100) : 0);
    printf("  Min/Max ops/sec: %.0f / %.0f\n", result->stats.min_value, result->stats.max_value);
    printf("  Margin of error: ±%.0f (%.1f%% at %.0f%% confidence)\n",
           result->stats.margin_error,
           result->stats.mean > 0 ? (result->stats.margin_error / result->stats.mean * 100) : 0,
           result->stats.confidence_level * 100);
    printf("  Converged: %s\n", result->stats.converged ? "Yes" : "No");

    printf("\nTiming:\n");
    printf("  Setup time: %.3f ms\n", result->setup_time * 1000);
    printf("  Benchmark time: %.3f ms\n", (result->total_time - result->setup_time - result->teardown_time) * 1000);
    printf("  Total time: %.3f ms\n", result->total_time * 1000);

    printf("\nValidation:\n");
    printf("  Expected ops/sec: %.0f\n", result->config.expected_ops_per_sec);
    printf("  Performance ratio: %.2fx\n", result->stats.mean / result->config.expected_ops_per_sec);
    printf("  Status: %s\n", result->passed ? "PASSED" : "FAILED");
    if (!result->passed && result->failure_reason) {
        printf("  Failure reason: %s\n", result->failure_reason);
    }
}

/*
 * ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================
 */

bool benchmark_config_validate(const BenchmarkConfig* config) {
    if (!config) return false;
    if (!config->name || strlen(config->name) == 0) return false;
    if (config->stats.min_iterations > config->stats.max_iterations) return false;
    if (config->stats.confidence_level <= 0.0 || config->stats.confidence_level >= 1.0) return false;
    if (config->expected_ops_per_sec <= 0.0) return false;
    if (config->performance_tolerance < 0.0) return false;

    return true;
}

void benchmark_result_free(BenchmarkResult* result) {
    if (!result) return;

    if (result->measurements) {
        free(result->measurements);
    }

    free(result);
}
