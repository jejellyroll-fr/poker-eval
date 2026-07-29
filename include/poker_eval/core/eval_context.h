/*
 * eval_context.h - Thread-safe immutable evaluation context
 *
 * Provides a unified interface for poker hand evaluation across different variants:
 * - Standard 52-card deck
 * - Joker deck (53 cards)
 * - Short deck (36 cards)
 * - Asian stud (32 cards)
 * - Manila deck (32 cards)
 *
 * Design principles:
 * - Immutable after creation (thread-safe)
 * - Fast 5-card canonical evaluation via LUT
 * - 7-card evaluation via optimized 7→5 enumeration
 * - Pluggable deck types and rule variants
 */

#ifndef EVAL_CONTEXT_H
#define EVAL_CONTEXT_H

#include <stdint.h>
#include <stdbool.h>
#include <poker_eval/core/modern_cardmask.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct EvalContext EvalContext;
typedef struct EvalConfig EvalConfig;

/* Evaluation result type - higher values are stronger hands */
typedef uint32_t eval_t;

/* Special evaluation values */
#define EVAL_INVALID    0
#define EVAL_HIGH_CARD  1000000
#define EVAL_PAIR       2000000
#define EVAL_TWO_PAIR   3000000
#define EVAL_TRIPS      4000000
#define EVAL_STRAIGHT   5000000
#define EVAL_FLUSH      6000000
#define EVAL_FULL_HOUSE 7000000
#define EVAL_QUADS      8000000
#define EVAL_STRAIGHT_FLUSH 9000000

/* Deck types */
typedef enum {
    EVAL_DECK_STANDARD = 0,   /* 52 cards */
    EVAL_DECK_JOKER,          /* 53 cards with joker */
    EVAL_DECK_SHORT,          /* 36 cards (6+ only) */
    EVAL_DECK_ASTUD,          /* 32 cards for Asian stud */
    EVAL_DECK_MANILA,         /* 32 cards (7+ only) */
    EVAL_DECK_MAX
} eval_deck_type_t;

/* Game rule variants */
typedef enum {
    EVAL_RULES_HIGH = 0,      /* High only */
    EVAL_RULES_LOW,           /* Low only (A-5) */
    EVAL_RULES_HILO,          /* High-low split */
    EVAL_RULES_RAZZ,          /* Razz (A-5 low) */
    EVAL_RULES_STUD8,         /* Stud Hi/Lo */
    EVAL_RULES_OMAHA8,        /* Omaha Hi/Lo */
    EVAL_RULES_MAX
} eval_rules_t;

/* Evaluation mode */
typedef enum {
    EVAL_MODE_EXACT = 0,      /* Exact evaluation via LUT */
    EVAL_MODE_MONTE_CARLO,    /* Monte Carlo simulation */
    EVAL_MODE_HYBRID,         /* Adaptive (exact for small, MC for large) */
    EVAL_MODE_MAX
} eval_mode_t;

/* Evaluation configuration */
struct EvalConfig {
    eval_deck_type_t deck_type;     /* Deck variant */
    eval_rules_t rules;             /* Rule variant */
    eval_mode_t mode;               /* Evaluation mode */
    uint32_t monte_carlo_trials;    /* MC sample size (if applicable) */
    bool enable_cache;              /* Enable result caching */
    bool enable_simd;               /* Enable SIMD optimizations */
    uint32_t cache_size_mb;         /* Cache size limit */
};

/* Default configurations */
EvalConfig eval_config_default(void);
EvalConfig eval_config_holdem(void);
EvalConfig eval_config_omaha(void);
EvalConfig eval_config_stud(void);
EvalConfig eval_config_shortdeck(void);

/* Context creation and management */
EvalContext* eval_context_create(const EvalConfig* config);
void eval_context_destroy(EvalContext* ctx);
const EvalConfig* eval_context_get_config(const EvalContext* ctx);

/* Cache access (eval_cache_t defined in eval_cache.h) */
struct eval_cache;  /* Forward declaration */
struct eval_cache* eval_context_get_cache(const EvalContext* ctx);

/* Validation */
bool eval_context_validate_config(const EvalConfig* config);
bool eval_context_validate_mask(const EvalContext* ctx, mask_t hand);

/* Core evaluation functions */

/**
 * Fast 5-card evaluation using canonical lookup table
 * @param ctx Evaluation context (must not be NULL)
 * @param five_mask Exactly 5 cards as mask_t
 * @return eval_t value (higher = stronger), EVAL_INVALID on error
 */
eval_t pe_eval_5c(const EvalContext* ctx, mask_t five_mask);

/**
 * 7-card evaluation via optimized 7→5 enumeration
 * @param ctx Evaluation context (must not be NULL)
 * @param seven_mask Exactly 7 cards as mask_t
 * @return eval_t value of strongest 5-card subset, EVAL_INVALID on error
 */
eval_t pe_eval_7c(const EvalContext* ctx, mask_t seven_mask);

/**
 * N-card evaluation (adaptive: exact for N≤7, MC for N>7)
 * @param ctx Evaluation context (must not be NULL)
 * @param hand_mask N cards as mask_t (N ≥ 5)
 * @return eval_t value of strongest 5-card subset, EVAL_INVALID on error
 */
eval_t pe_eval_nc(const EvalContext* ctx, mask_t hand_mask);

/* Hi/Lo single-pass evaluation (A-5 low, 8-or-better) on 7 cards.
 * Returns true on success and fills outputs:
 *  - hi_out: strongest high 5-card hand
 *  - lo_out: best qualifying low value (lower numbers are better), undefined if !lo_qual_out
 *  - lo_qual_out: whether any 5-card subset qualifies for 8-or-better low
 */
bool pe_eval_7c_hilo(const EvalContext* ctx, mask_t seven_mask, eval_t* hi_out, eval_t* lo_out, bool* lo_qual_out);

/* Hand strength classification */
typedef enum {
    HAND_HIGH_CARD = 0,
    HAND_PAIR,
    HAND_TWO_PAIR,
    HAND_THREE_KIND,
    HAND_STRAIGHT,
    HAND_FLUSH,
    HAND_FULL_HOUSE,
    HAND_FOUR_KIND,
    HAND_STRAIGHT_FLUSH,
    HAND_MAX
} hand_class_t;

hand_class_t eval_get_hand_class(eval_t eval_value);
const char* eval_hand_class_name(hand_class_t hand_class);

/* Utility functions */
int eval_compare(eval_t a, eval_t b);  /* -1, 0, +1 comparison */
bool eval_is_valid(eval_t eval_value);

/* Statistics and debugging */
typedef struct {
    uint64_t evaluations_5c;      /* 5-card evaluations performed */
    uint64_t evaluations_7c;      /* 7-card evaluations performed */
    uint64_t evaluations_nc;      /* N-card evaluations performed */
    uint64_t cache_hits;          /* Cache hits */
    uint64_t cache_misses;        /* Cache misses */
    uint64_t lut_lookups;         /* LUT lookups performed */
    uint64_t combinations_enum;   /* Combinations enumerated */
} eval_stats_t;

void eval_context_get_stats(const EvalContext* ctx, eval_stats_t* stats);
void eval_context_reset_stats(EvalContext* ctx);
double eval_context_cache_hit_rate(const EvalContext* ctx);

/* Performance testing */
bool eval_context_self_test(const EvalContext* ctx);
uint64_t eval_context_benchmark_5c(const EvalContext* ctx, uint32_t iterations);
uint64_t eval_context_benchmark_7c(const EvalContext* ctx, uint32_t iterations);

/* Memory management */
size_t eval_context_memory_usage(const EvalContext* ctx);
bool eval_context_optimize_memory(EvalContext* ctx);

/**
 * @brief A backdoor to increment the combinations counter for testing/benchmarking purposes.
 * 
 * @param ctx The evaluation context.
 * @param n The number of combinations to add.
 */
void eval_context_add_combinations(const EvalContext* ctx, uint32_t n);

#ifdef __cplusplus
}
#endif

#endif /* EVAL_CONTEXT_H */
