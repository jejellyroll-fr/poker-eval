/*
 * deterministic_benchmark.h - Deterministic benchmarking system (Phase 3.6)
 *
 * This header provides a reproducible benchmarking framework with:
 * - Fixed-seed RNG for consistent Monte Carlo results
 * - Statistical analysis with confidence intervals
 * - Performance regression detection
 * - Multi-threaded benchmark coordination
 * - Integration with existing poker-eval systems
 */

#ifndef DETERMINISTIC_BENCHMARK_H
#define DETERMINISTIC_BENCHMARK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include "modern_cardmask.h"
#include "eval_context.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * DETERMINISTIC RNG SYSTEM
 * ============================================================================
 */

/* PCG (Permuted Congruential Generator) - Fast, high-quality, reproducible */
typedef struct {
    uint64_t state;      /* RNG internal state */
    uint64_t inc;        /* Stream selection constant (must be odd) */
    uint64_t calls;      /* Number of calls made (for debugging) */
} DeterministicRNG;

/* Initialize RNG with seed */
void det_rng_seed(DeterministicRNG* rng, uint64_t seed);

/* Generate next 32-bit random number */
uint32_t det_rng_next(DeterministicRNG* rng);

/* Generate random number in range [0, bound) */
uint32_t det_rng_bounded(DeterministicRNG* rng, uint32_t bound);

/* Generate random 64-bit number */
uint64_t det_rng_next64(DeterministicRNG* rng);

/* Generate random double in [0.0, 1.0) */
double det_rng_double(DeterministicRNG* rng);

/* Sample k cards from available mask deterministically */
mask_t det_sample_k_cards(DeterministicRNG* rng, mask_t available, int k);

/* Shuffle array deterministically (Fisher-Yates) */
void det_shuffle_array(DeterministicRNG* rng, void* array, size_t count, size_t elem_size);

/*
 * ============================================================================
 * BENCHMARK CONFIGURATION
 * ============================================================================
 */

/* Benchmark types */
typedef enum {
    BENCH_TYPE_EVALUATION = 0,      /* Hand evaluation performance */
    BENCH_TYPE_ENUMERATION,         /* Exhaustive enumeration */
    BENCH_TYPE_MONTE_CARLO,         /* Monte Carlo simulation */
    BENCH_TYPE_RANGE_EQUITY,        /* Range vs range equity */
    BENCH_TYPE_JOKER_EXPANSION,     /* Joker expansion performance */
    BENCH_TYPE_CACHE_PERFORMANCE,   /* Cache hit/miss rates */
    BENCH_TYPE_MEMORY_BANDWIDTH,    /* Memory access patterns */
    BENCH_TYPE_MAX
} benchmark_type_t;

/* Statistical analysis configuration */
typedef struct {
    uint32_t min_iterations;        /* Minimum benchmark iterations */
    uint32_t max_iterations;        /* Maximum benchmark iterations */
    double target_margin_error;     /* Target margin of error (%) */
    double confidence_level;        /* Confidence level (0.95 = 95%) */
    bool auto_convergence;          /* Stop when statistically stable */
    uint32_t warmup_iterations;     /* Warmup iterations (ignored in stats) */
} BenchmarkStatConfig;

/* Benchmark configuration */
typedef struct {
    benchmark_type_t type;          /* Type of benchmark */
    const char* name;               /* Benchmark name */
    const char* description;        /* Detailed description */

    /* Determinism control */
    uint64_t base_seed;             /* Base random seed */
    bool enable_multithreading;     /* Allow multi-threaded execution */
    int thread_count;               /* Number of threads (0 = auto) */

    /* Statistical configuration */
    BenchmarkStatConfig stats;      /* Statistical analysis settings */

    /* Performance bounds (for regression detection) */
    double expected_ops_per_sec;    /* Expected performance */
    double performance_tolerance;   /* Acceptable deviation (%) */

    /* Context-specific settings */
    union {
        struct {
            int hand_size;          /* Cards per hand */
            int num_players;        /* Number of players */
            eval_rules_t rules;     /* Evaluation rules */
        } evaluation;

        struct {
            mask_t dead_cards;      /* Fixed dead cards */
            int combo_size;         /* Combination size */
            uint64_t max_combos;    /* Limit combinations */
        } enumeration;

        struct {
            uint32_t trials;        /* Monte Carlo trials */
            mask_t fixed_board;     /* Fixed board cards */
            int num_players;        /* Number of players */
        } monte_carlo;

        struct {
            const char* range1;     /* First range string */
            const char* range2;     /* Second range string */
            mask_t board;           /* Board cards */
            uint32_t simulations;   /* Number of simulations */
        } range_equity;
    } params;
} BenchmarkConfig;

/*
 * ============================================================================
 * BENCHMARK RESULTS AND STATISTICS
 * ============================================================================
 */

/* Single benchmark measurement */
typedef struct {
    double elapsed_time;            /* Execution time (seconds) */
    uint64_t operations;            /* Operations performed */
    double ops_per_second;          /* Operations per second */
    uint64_t memory_used;           /* Peak memory usage (bytes) */
    uint32_t cache_hits;            /* Cache hits */
    uint32_t cache_misses;          /* Cache misses */
} BenchmarkMeasurement;

/* Statistical summary */
typedef struct {
    uint32_t sample_count;          /* Number of measurements */
    double mean;                    /* Mean ops/sec */
    double median;                  /* Median ops/sec */
    double std_dev;                 /* Standard deviation */
    double min_value;               /* Minimum ops/sec */
    double max_value;               /* Maximum ops/sec */
    double margin_error;            /* Margin of error at confidence level */
    double confidence_level;        /* Confidence level used */
    bool converged;                 /* Statistical convergence achieved */
} BenchmarkStatistics;

/* Complete benchmark result */
typedef struct {
    BenchmarkConfig config;         /* Configuration used */
    BenchmarkStatistics stats;      /* Statistical summary */
    BenchmarkMeasurement* measurements; /* Raw measurements */
    uint32_t measurement_count;     /* Number of measurements */

    /* Timing breakdown */
    double setup_time;              /* Setup overhead */
    double teardown_time;           /* Cleanup overhead */
    double total_time;              /* Total benchmark time */

    /* System information */
    const char* system_info;        /* System/compiler info */
    const char* timestamp;          /* When benchmark was run */
    uint64_t rng_seed;              /* Seed used (for reproduction) */

    /* Pass/fail status */
    bool passed;                    /* Performance within tolerance */
    const char* failure_reason;     /* Reason for failure (if any) */
} BenchmarkResult;

/*
 * ============================================================================
 * BENCHMARK EXECUTION
 * ============================================================================
 */

/* Initialize benchmark system */
bool benchmark_init(void);

/* Cleanup benchmark system */
void benchmark_cleanup(void);

/* Run single benchmark */
BenchmarkResult* benchmark_run(const BenchmarkConfig* config);

/* Run benchmark suite */
typedef struct {
    const char* suite_name;
    BenchmarkConfig* benchmarks;
    uint32_t benchmark_count;
    uint64_t base_seed;             /* Base seed for entire suite */
} BenchmarkSuite;

typedef struct {
    BenchmarkSuite suite;
    BenchmarkResult** results;     /* Array of benchmark results */
    uint32_t passed_count;          /* Number of passed benchmarks */
    uint32_t failed_count;          /* Number of failed benchmarks */
    double total_time;              /* Total suite execution time */
} BenchmarkSuiteResult;

BenchmarkSuiteResult* benchmark_run_suite(const BenchmarkSuite* suite);

/* Free benchmark results */
void benchmark_result_free(BenchmarkResult* result);
void benchmark_suite_result_free(BenchmarkSuiteResult* suite_result);

/*
 * ============================================================================
 * PREDEFINED BENCHMARK CONFIGURATIONS
 * ============================================================================
 */

/* Standard benchmark configurations */
BenchmarkConfig benchmark_config_holdem_evaluation(void);
BenchmarkConfig benchmark_config_omaha_evaluation(void);
BenchmarkConfig benchmark_config_stud_evaluation(void);
BenchmarkConfig benchmark_config_joker_evaluation(void);

BenchmarkConfig benchmark_config_monte_carlo_standard(void);
BenchmarkConfig benchmark_config_monte_carlo_joker(void);

BenchmarkConfig benchmark_config_range_equity_holdem(void);
BenchmarkConfig benchmark_config_range_equity_omaha(void);

BenchmarkConfig benchmark_config_enumeration_5card(void);
BenchmarkConfig benchmark_config_enumeration_7card(void);

BenchmarkConfig benchmark_config_joker_expansion(void);
BenchmarkConfig benchmark_config_cache_performance(void);

/* Create benchmark suite with all standard tests */
BenchmarkSuite benchmark_suite_comprehensive(uint64_t base_seed);
BenchmarkSuite benchmark_suite_quick(uint64_t base_seed);
BenchmarkSuite benchmark_suite_regression(uint64_t base_seed);

/*
 * ============================================================================
 * BENCHMARK REPORTING
 * ============================================================================
 */

/* Report formats */
typedef enum {
    BENCH_REPORT_CONSOLE = 0,       /* Human-readable console output */
    BENCH_REPORT_CSV,               /* CSV format for analysis */
    BENCH_REPORT_JSON,              /* JSON format for tools */
    BENCH_REPORT_XML,               /* XML format for CI systems */
    BENCH_REPORT_MARKDOWN           /* Markdown for documentation */
} benchmark_report_format_t;

/* Generate benchmark report */
bool benchmark_report_generate(const BenchmarkResult* result,
                               benchmark_report_format_t format,
                               const char* output_file);

bool benchmark_suite_report_generate(const BenchmarkSuiteResult* suite_result,
                                     benchmark_report_format_t format,
                                     const char* output_file);

/* Print results to console */
void benchmark_result_print(const BenchmarkResult* result);
void benchmark_suite_result_print(const BenchmarkSuiteResult* suite_result);

/* Compare two benchmark results */
typedef struct {
    double speedup_ratio;           /* New vs old performance ratio */
    double performance_change;      /* Performance change percentage */
    bool significant_change;        /* Statistically significant */
    const char* interpretation;    /* Human-readable interpretation */
} BenchmarkComparison;

BenchmarkComparison benchmark_compare(const BenchmarkResult* baseline,
                                     const BenchmarkResult* current);

/*
 * ============================================================================
 * BENCHMARK UTILITIES
 * ============================================================================
 */

/* Get high-resolution timestamp */
double benchmark_get_time(void);

/* Get system information string */
const char* benchmark_get_system_info(void);

/* Validate benchmark configuration */
bool benchmark_config_validate(const BenchmarkConfig* config);

/* Set global benchmark options */
void benchmark_set_verbose(bool verbose);
void benchmark_set_output_dir(const char* dir);
void benchmark_set_thread_affinity(bool enable);

/* Benchmark-specific RNG helpers */
DeterministicRNG* benchmark_get_thread_rng(void);  /* Thread-local RNG */
void benchmark_seed_all_rngs(uint64_t base_seed);  /* Seed all thread RNGs */

/*
 * ============================================================================
 * INTEGRATION WITH EXISTING SYSTEMS
 * ============================================================================
 */

/* Integration with poker evaluation systems */
typedef struct {
    EvalContext* eval_context;      /* Evaluation context to benchmark */
    const char* context_name;       /* Context description */
} BenchmarkEvalContext;

/* Benchmark a specific evaluation context */
BenchmarkResult* benchmark_eval_context(const BenchmarkEvalContext* ctx,
                                       const BenchmarkConfig* config);

/* Benchmark Monte Carlo with deterministic sampling */
BenchmarkResult* benchmark_monte_carlo_deterministic(mask_t dead_cards,
                                                    int num_players,
                                                    uint32_t iterations,
                                                    uint64_t seed);

/* Memory allocation tracking for benchmarks */
void benchmark_enable_memory_tracking(bool enable);
uint64_t benchmark_get_peak_memory_usage(void);
void benchmark_reset_memory_tracking(void);

/*
 * ============================================================================
 * COMMAND LINE INTERFACE
 * ============================================================================
 */

/* Command line benchmark runner */
typedef struct {
    const char* suite_name;         /* Suite to run (or "all") */
    uint64_t seed;                  /* Random seed */
    int iterations;                 /* Override iteration count */
    benchmark_report_format_t format; /* Output format */
    const char* output_file;        /* Output file (NULL = stdout) */
    bool verbose;                   /* Verbose output */
    bool regression_mode;           /* Regression testing mode */
    const char* baseline_file;      /* Baseline results for comparison */
} BenchmarkCLIOptions;

int benchmark_cli_main(int argc, char** argv, const BenchmarkCLIOptions* options);

#ifdef __cplusplus
}
#endif

#endif /* DETERMINISTIC_BENCHMARK_H */
