/*
 * modern_combinations.h - Modern combinatorial generator for poker hands
 *
 * Provides efficient generation of card combinations using 64-bit masks
 * and modern optimization techniques.
 */

#ifndef MODERN_COMBINATIONS_H
#define MODERN_COMBINATIONS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <poker_eval/core/modern_cardmask.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct combo_generator combo_generator_t;
typedef struct combo_state combo_state_t;
typedef struct combo_iterator combo_iterator_t;

/* Combination generation modes */
typedef enum {
    COMBO_MODE_EXACT = 0,      /* Generate exactly N cards */
    COMBO_MODE_UP_TO,          /* Generate up to N cards */
    COMBO_MODE_AT_LEAST,       /* Generate at least N cards */
    COMBO_MODE_RANGE           /* Generate between min and max cards */
} combo_mode_t;

/* Generator optimization hints */
typedef enum {
    COMBO_OPT_NONE = 0,        /* No specific optimization */
    COMBO_OPT_MEMORY,          /* Optimize for memory usage */
    COMBO_OPT_SPEED,           /* Optimize for speed */
    COMBO_OPT_BALANCED,        /* Balance memory and speed */
    COMBO_OPT_STREAMING        /* Optimize for streaming (large datasets) */
} combo_optimization_t;

/* Iterator styles */
typedef enum {
    COMBO_ITER_FORWARD = 0,    /* Forward iteration (lexicographic order) */
    COMBO_ITER_REVERSE,        /* Reverse iteration */
    COMBO_ITER_RANDOM,         /* Random order iteration */
    COMBO_ITER_BALANCED        /* Balanced tree traversal order */
} combo_iteration_t;

/* Combination state for resumable generation */
struct combo_state {
    mask_t current_combo;      /* Current combination */
    uint64_t position;         /* Current position in sequence */
    uint64_t total_combos;     /* Total combinations possible */
    uint32_t combo_size;       /* Size of current combination */
    uint32_t remaining_cards;  /* Cards available for selection */

    /* Internal state */
    uint32_t indices[52];      /* Current card indices */
    uint32_t limits[52];       /* Upper limits for each position */
    int32_t current_level;     /* Current recursion level */
    bool exhausted;            /* Generation complete */
};

/* Configuration for combination generation */
typedef struct {
    mask_t available_cards;    /* Cards available for selection */
    mask_t excluded_cards;     /* Cards to exclude */

    uint32_t combo_size;       /* Number of cards to select */
    uint32_t min_size;         /* Minimum size (for range mode) */
    uint32_t max_size;         /* Maximum size (for range mode) */

    combo_mode_t mode;         /* Generation mode */
    combo_optimization_t optimization; /* Optimization preference */
    combo_iteration_t iteration; /* Iteration style */

    uint64_t max_combinations; /* Limit on combinations to generate */
    uint64_t start_offset;     /* Skip first N combinations */

    bool unique_only;          /* Only generate unique combinations */
    bool sorted_output;        /* Return combinations in sorted order */
} combo_config_t;

/* Main combination generator */
struct combo_generator {
    combo_config_t config;     /* Generator configuration */
    combo_state_t state;       /* Current generation state */

    /* Performance tracking */
    uint64_t combinations_generated;
    uint64_t total_iterations;
    uint64_t cache_hits;
    uint64_t cache_misses;

    /* Internal optimization data */
    mask_t cached_masks[256];  /* Cache for frequent combinations */
    uint32_t cache_indices[256];
    uint32_t cache_size;

    /* Memory management */
    void* internal_buffer;
    size_t buffer_size;
    size_t buffer_used;
};

/* Iterator interface for large datasets */
struct combo_iterator {
    combo_generator_t* generator;
    combo_state_t private_state;
    bool owns_generator;       /* Whether iterator owns the generator */

    /* Batch processing */
    mask_t* batch_buffer;
    size_t batch_size;
    size_t batch_count;
    size_t batch_index;
};

/* Generator creation and management */
combo_generator_t* combo_generator_create(const combo_config_t* config);
combo_generator_t* combo_generator_create_simple(mask_t available_cards,
                                                 uint32_t combo_size);
void combo_generator_destroy(combo_generator_t* gen);
combo_generator_t* combo_generator_clone(const combo_generator_t* gen);

/* Configuration helpers */
combo_config_t combo_config_default(void);
combo_config_t combo_config_holdem(mask_t board, mask_t dead_cards);
combo_config_t combo_config_omaha(mask_t board, mask_t dead_cards,
                                  uint32_t hole_cards);
combo_config_t combo_config_stud(mask_t dead_cards, uint32_t hand_size);

/* Core generation functions */
bool combo_generator_next(combo_generator_t* gen, mask_t* combination);
bool combo_generator_has_next(const combo_generator_t* gen);
uint64_t combo_generator_remaining(const combo_generator_t* gen);
uint64_t combo_generator_total(const combo_generator_t* gen);

/* Batch generation for efficiency */
size_t combo_generator_next_batch(combo_generator_t* gen, mask_t* combinations,
                                  size_t max_count);
size_t combo_generator_generate_all(combo_generator_t* gen, mask_t** combinations);

/* State management */
bool combo_generator_reset(combo_generator_t* gen);
bool combo_generator_seek(combo_generator_t* gen, uint64_t position);
combo_state_t combo_generator_save_state(const combo_generator_t* gen);
bool combo_generator_restore_state(combo_generator_t* gen,
                                   const combo_state_t* state);

/* Iterator interface */
combo_iterator_t* combo_iterator_create(combo_generator_t* gen);
combo_iterator_t* combo_iterator_create_simple(mask_t available_cards,
                                               uint32_t combo_size,
                                               size_t batch_size);
void combo_iterator_destroy(combo_iterator_t* iter);

bool combo_iterator_next(combo_iterator_t* iter, mask_t* combination);
size_t combo_iterator_next_batch(combo_iterator_t* iter, mask_t* combinations,
                                 size_t max_count);
bool combo_iterator_has_next(const combo_iterator_t* iter);
void combo_iterator_reset(combo_iterator_t* iter);

/* Utility functions */
uint64_t combo_count_combinations(mask_t available_cards, uint32_t combo_size);
uint64_t combo_count_combinations_range(mask_t available_cards,
                                        uint32_t min_size, uint32_t max_size);
bool combo_is_valid_combination(mask_t combination, const combo_config_t* config);

/* Mathematical utilities */
uint64_t combo_binomial(uint32_t n, uint32_t k);
uint64_t combo_position_to_combination(uint64_t position, uint32_t n, uint32_t k,
                                       uint32_t* indices);
uint64_t combo_combination_to_position(const uint32_t* indices, uint32_t n, uint32_t k);

/* Performance and statistics */
void combo_generator_get_stats(const combo_generator_t* gen,
                              uint64_t* generated, uint64_t* iterations,
                              uint64_t* cache_hits, uint64_t* cache_misses);
void combo_generator_reset_stats(combo_generator_t* gen);
double combo_generator_cache_hit_rate(const combo_generator_t* gen);

/* Memory management and optimization */
bool combo_generator_optimize_memory(combo_generator_t* gen);
bool combo_generator_set_cache_size(combo_generator_t* gen, size_t cache_size);
size_t combo_generator_memory_usage(const combo_generator_t* gen);

/* Advanced features */
bool combo_generator_skip(combo_generator_t* gen, uint64_t count);
bool combo_generator_filter(combo_generator_t* gen,
                           bool (*filter_fn)(mask_t combination, void* user_data),
                           void* user_data);

/* String and debug utilities */
char* combo_config_to_string(const combo_config_t* config, char* buffer, size_t size);
char* combo_state_to_string(const combo_state_t* state, char* buffer, size_t size);
void combo_generator_print_stats(const combo_generator_t* gen);

/* Validation and testing */
bool combo_generator_validate_config(const combo_config_t* config);
bool combo_generator_self_test(void);

#ifdef __cplusplus
}
#endif

#endif /* MODERN_COMBINATIONS_H */
