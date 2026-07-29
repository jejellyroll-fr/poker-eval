/*
 * omaha_combinations.h - Constrained combination generator for Omaha poker
 *
 * Implements efficient Omaha hand generation with native 2+3 constraints:
 * - Exactly 2 cards from hole (4 cards)
 * - Exactly 3 cards from board (5 cards)
 * - Direct generation without post-filtering
 * - Integration with modern EvalContext
 */

#ifndef OMAHA_COMBINATIONS_H
#define OMAHA_COMBINATIONS_H

#include <stdint.h>
#include <stdbool.h>
#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/core/modern_combinations.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct omaha_generator omaha_generator_t;
typedef struct omaha_config omaha_config_t;

/* Omaha hand constraints */
typedef struct {
    mask_t hole_cards;      /* 4 hole cards */
    mask_t board_cards;     /* 5 board cards */
    mask_t dead_cards;      /* Additional dead cards */

    /* Validation */
    bool validate_constraints;  /* Enforce 2+3 rule */
    bool unique_only;          /* Generate unique combinations only */
} omaha_constraints_t;

/* Omaha generator configuration */
struct omaha_config {
    omaha_constraints_t constraints;

    /* Performance options */
    bool enable_cache;         /* Cache generated combinations */
    uint32_t cache_size_mb;    /* Cache size limit */
    bool enable_stats;         /* Track generation statistics */

    /* Generation mode */
    combo_iteration_t iteration; /* Forward/backward iteration */
    uint64_t max_combinations;   /* Limit total combinations */
    uint64_t start_offset;       /* Skip initial combinations */
};

/* Omaha generation statistics */
typedef struct {
    uint64_t hole_combinations;    /* C(4,2) = 6 combinations generated */
    uint64_t board_combinations;   /* C(5,3) = 10 combinations generated */
    uint64_t total_combinations;   /* Total 5-card combinations: 6 × 10 = 60 */
    uint64_t cache_hits;          /* Cache hits */
    uint64_t cache_misses;        /* Cache misses */
    uint64_t validation_failures; /* Constraint violations */
} omaha_stats_t;

/* Omaha generator internal structure */
struct omaha_generator {
    omaha_config_t config;
    omaha_stats_t stats;

    /* State */
    combo_generator_t* hole_gen;   /* 2-from-4 hole generator */
    combo_generator_t* board_gen;  /* 3-from-5 board generator */
    mask_t current_hole_combo;     /* Current hole combination */
    mask_t current_board_combo;    /* Current board combination */
    bool hole_exhausted;           /* Hole generator exhausted */
    bool generator_exhausted;      /* Overall generator exhausted */

    /* Cache */
    void* cache_data;
    size_t cache_used;
};

/* Configuration functions */
omaha_config_t omaha_config_default(void);
omaha_config_t omaha_config_plo(mask_t hole, mask_t board, mask_t dead);
omaha_config_t omaha_config_plo5(mask_t hole, mask_t board, mask_t dead);
omaha_config_t omaha_config_plo6(mask_t hole, mask_t board, mask_t dead);
omaha_config_t omaha_config_plo8(mask_t hole, mask_t board, mask_t dead);

/* Generator lifecycle */
omaha_generator_t* omaha_generator_create(const omaha_config_t* config);
void omaha_generator_destroy(omaha_generator_t* gen);

/* Validation */
bool omaha_validate_config(const omaha_config_t* config);
bool omaha_validate_constraints(const omaha_constraints_t* constraints);

/* Core generation functions */

/**
 * Generate next valid Omaha combination (2 hole + 3 board)
 *
 * IMPORTANT API CONTRACT:
 * - next() returns false AFTER the last combination has been returned
 * - Once exhausted, the generator remains in "exhausted" state
 * - All subsequent calls to next() will return false until reset()
 *
 * @param gen Omaha generator (must not be NULL)
 * @param combination Output 5-card combination
 * @return true if combination generated, false if exhausted
 */
bool omaha_generator_next(omaha_generator_t* gen, mask_t* combination);

/**
 * Check if generator has more combinations
 * @param gen Omaha generator
 * @return true if more combinations available
 */
bool omaha_generator_has_next(const omaha_generator_t* gen);

/**
 * Get total number of possible combinations
 * @param gen Omaha generator
 * @return total combinations (should be C(4,2) × C(5,3) = 60)
 */
uint64_t omaha_generator_total(const omaha_generator_t* gen);

/**
 * Get remaining combinations count
 * @param gen Omaha generator
 * @return remaining combinations
 */
uint64_t omaha_generator_remaining(const omaha_generator_t* gen);

/**
 * Reset generator to beginning
 *
 * IMPORTANT API CONTRACT:
 * - reset() puts the iterator BEFORE the first combination
 * - The next call to omaha_generator_next() will return the first combination
 * - This ensures consistent behavior across all generator resets
 *
 * @param gen Omaha generator
 * @return true on success
 */
bool omaha_generator_reset(omaha_generator_t* gen);

/* Batch generation */
size_t omaha_generator_next_batch(omaha_generator_t* gen, mask_t* combinations,
                                  size_t max_count);

/* Statistics and debugging */
void omaha_generator_get_stats(const omaha_generator_t* gen, omaha_stats_t* stats);
void omaha_generator_reset_stats(omaha_generator_t* gen);
double omaha_generator_cache_hit_rate(const omaha_generator_t* gen);

/* Performance testing */
bool omaha_generator_self_test(void);
uint64_t omaha_generator_benchmark(uint32_t iterations);

/* Utility functions */
const char* omaha_config_to_string(const omaha_config_t* config, char* buffer, size_t size);
void omaha_generator_print_stats(const omaha_generator_t* gen);

/* Memory management */
size_t omaha_generator_memory_usage(const omaha_generator_t* gen);
bool omaha_generator_optimize_memory(omaha_generator_t* gen);

/* Forward declaration for EvalContext */
struct EvalContext;

/* Integration with EvalContext */

/**
 * Evaluate all Omaha combinations using EvalContext
 * @param gen Omaha generator
 * @param ctx Evaluation context
 * @param results Array to store evaluation results (size ≥ 60)
 * @return number of combinations evaluated
 */
size_t omaha_evaluate_all(omaha_generator_t* gen, const struct EvalContext* ctx,
                          uint32_t* results);

/**
 * Find best Omaha combination using EvalContext
 * @param gen Omaha generator
 * @param ctx Evaluation context
 * @param best_combo Output best 5-card combination
 * @return evaluation value of best combination
 */
uint32_t omaha_find_best(omaha_generator_t* gen, const struct EvalContext* ctx,
                         mask_t* best_combo);

#ifdef __cplusplus
}
#endif

#endif /* OMAHA_COMBINATIONS_H */
