/*
 * joker_expansion_controlled.h - Controlled Joker Expansion Pipeline (Phase 3.5)
 *
 * This header provides an optimized, controlled approach to joker expansion:
 * - Limits expansion scope for performance
 * - Caches expansion results
 * - Smart pruning strategies
 * - Integration with modern mask_t system
 * - Thread-safe operations
 */

#ifndef JOKER_EXPANSION_CONTROLLED_H
#define JOKER_EXPANSION_CONTROLLED_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/core/eval_context.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * ============================================================================
     * CONTROLLED EXPANSION CONFIGURATION
     * ============================================================================
     */

    /* Expansion strategy options */
    typedef enum
    {
        JOKER_EXPAND_FULL = 0,  /* Expand to all available cards (legacy) */
        JOKER_EXPAND_STRATEGIC, /* Smart expansion based on hand context */
        JOKER_EXPAND_RANKING,   /* Expand by card ranking priority */
        JOKER_EXPAND_CATEGORY,  /* Expand by hand type potential */
        JOKER_EXPAND_LIMITED    /* Limit to N best candidates */
    } joker_expansion_strategy_t;

    /* Expansion control configuration */
    typedef struct
    {
        joker_expansion_strategy_t strategy; /* Expansion approach */
        uint32_t max_expansions;             /* Maximum cards to try */
        bool enable_cache;                   /* Cache expansion results */
        bool enable_pruning;                 /* Early termination when possible */
        uint32_t cache_size;                 /* Cache entry limit */

        /* Context-specific constraints */
        mask_t available_cards;    /* Cards joker can become */
        mask_t forbidden_cards;    /* Cards joker cannot become */
        eval_rules_t target_rules; /* Hi/Lo/Split rules context */
    } JokerExpansionConfig;

    /*
     * ============================================================================
     * EXPANSION RESULT AND CACHE
     * ============================================================================
     */

    /* Single expansion result */
    typedef struct
    {
        mask_t joker_as_card; /* Which card the joker became */
        eval_t hand_value;    /* Resulting hand value */
        bool is_optimal;      /* True if this is the best found */
    } JokerExpansionResult;

    /* Multi-joker expansion result */
    typedef struct
    {
        uint32_t num_results;          /* Number of valid expansions */
        JokerExpansionResult *results; /* Array of results */
        eval_t best_value;             /* Best hand value found */
        mask_t best_hand;              /* Best hand mask */
        uint32_t expansions_tried;     /* Performance metric */
        uint32_t cache_hits;           /* Cache performance */
    } JokerExpansionSet;

    /* Expansion cache entry */
    typedef struct
    {
        mask_t natural_cards;   /* Cards without jokers */
        mask_t joker_positions; /* Position of jokers */
        JokerExpansionSet cached_result;
        uint64_t access_count; /* For LRU eviction */
    } JokerCacheEntry;

    /*
     * ============================================================================
     * EXPANSION ITERATOR
     * ============================================================================
     */

    /* Controlled expansion iterator */
    typedef struct JokerExpansionIterator JokerExpansionIterator;

    struct JokerExpansionIterator
    {
        /* Configuration */
        JokerExpansionConfig config;
        EvalContext *eval_context;

        /* Current state */
        mask_t natural_cards; /* Non-joker cards */
        mask_t joker_mask;    /* Joker positions */
        uint32_t num_jokers;  /* Number of jokers */

        /* Iteration state */
        uint32_t current_expansion; /* Current expansion index */
        mask_t *candidate_cards;    /* Candidate cards for jokers */
        uint32_t num_candidates;    /* Number of candidates */
        bool initialized;           /* Iterator state */
        bool exhausted;             /* No more expansions */

        /* Performance tracking */
        uint32_t expansions_generated; /* Total expansions tried */
        uint32_t cache_hits;           /* Cache hits during iteration */
        uint32_t pruned_expansions;    /* Expansions skipped via pruning */

        /* Cache management */
        JokerCacheEntry *cache;        /* Expansion cache */
        uint32_t cache_capacity;       /* Maximum cache entries */
        uint32_t cache_used;           /* Current cache usage */
        uint64_t cache_access_counter; /* For LRU tracking */
    };

    /*
     * ============================================================================
     * SMART EXPANSION STRATEGIES
     * ============================================================================
     */

    /* Strategic expansion: prioritize cards by hand improvement potential */
    uint32_t joker_expansion_strategic_candidates(mask_t natural_cards,
                                                  mask_t available_cards,
                                                  mask_t *candidates,
                                                  uint32_t max_candidates);

    /* Ranking-based expansion: prioritize by card rank (Ace, King, etc.) */
    uint32_t joker_expansion_ranking_candidates(mask_t available_cards,
                                                mask_t *candidates,
                                                uint32_t max_candidates,
                                                bool prefer_high_cards);

    /* Category-based expansion: prioritize by hand type potential */
    uint32_t joker_expansion_category_candidates(mask_t natural_cards,
                                                 mask_t available_cards,
                                                 mask_t *candidates,
                                                 uint32_t max_candidates,
                                                 eval_rules_t rules);

    /*
     * ============================================================================
     * ITERATOR LIFECYCLE
     * ============================================================================
     */

    /* Create expansion iterator */
    JokerExpansionIterator *joker_expansion_create(const JokerExpansionConfig *config,
                                                   EvalContext *eval_context);

    /* Initialize iterator for specific hand */
    bool joker_expansion_init(JokerExpansionIterator *iter,
                              mask_t hand_with_jokers);

    /* Get next expansion */
    bool joker_expansion_next(JokerExpansionIterator *iter,
                              mask_t *expanded_hand,
                              JokerExpansionResult *result);

    /* Get best expansion found so far */
    bool joker_expansion_get_best(JokerExpansionIterator *iter,
                                  mask_t *best_hand,
                                  eval_t *best_value);

    /* Reset iterator for reuse */
    bool joker_expansion_reset(JokerExpansionIterator *iter);

    /* Destroy iterator */
    void joker_expansion_destroy(JokerExpansionIterator *iter);

    /*
     * ============================================================================
     * HIGH-LEVEL EVALUATION FUNCTIONS
     * ============================================================================
     */

    /* Evaluate hand with jokers using controlled expansion */
    eval_t joker_eval_controlled(EvalContext *eval_context,
                                 mask_t hand_with_jokers,
                                 const JokerExpansionConfig *config);

    /* Get all possible expansions */
    JokerExpansionSet *joker_expand_all_controlled(EvalContext *eval_context,
                                                   mask_t hand_with_jokers,
                                                   const JokerExpansionConfig *config);

    /* Hi/Lo evaluation with joker expansion */
    typedef struct
    {
        eval_t high_value;
        eval_t low_value;
        mask_t best_high_hand;
        mask_t best_low_hand;
        bool has_low;
        bool is_scoop;
    } JokerHiLoResult;

    JokerHiLoResult joker_eval_hilo_controlled(EvalContext *eval_context,
                                               mask_t hand_with_jokers,
                                               const JokerExpansionConfig *config);

    /*
     * ============================================================================
     * CONFIGURATION HELPERS
     * ============================================================================
     */

    /* Default configurations for different scenarios */
    JokerExpansionConfig joker_config_default(void);
    JokerExpansionConfig joker_config_performance(void); /* Speed over accuracy */
    JokerExpansionConfig joker_config_accuracy(void);    /* Accuracy over speed */
    JokerExpansionConfig joker_config_holdem(void);      /* Hold'em optimized */
    JokerExpansionConfig joker_config_stud(void);        /* Stud optimized */
    JokerExpansionConfig joker_config_omaha(void);       /* Omaha optimized */

    /* Update configuration for specific constraints */
    void joker_config_set_available_cards(JokerExpansionConfig *config,
                                          mask_t available);
    void joker_config_set_forbidden_cards(JokerExpansionConfig *config,
                                          mask_t forbidden);
    void joker_config_set_max_expansions(JokerExpansionConfig *config,
                                         uint32_t max_expansions);

    /*
     * ============================================================================
     * PERFORMANCE AND DEBUGGING
     * ============================================================================
     */

    /* Performance statistics */
    typedef struct
    {
        uint64_t total_evaluations;     /* Total joker evaluations */
        uint64_t cache_hits;            /* Cache hit count */
        uint64_t cache_misses;          /* Cache miss count */
        uint64_t expansions_tried;      /* Total expansions attempted */
        uint64_t expansions_pruned;     /* Expansions skipped via pruning */
        double avg_expansions_per_eval; /* Average expansions per evaluation */
        double cache_hit_rate;          /* Cache effectiveness */
    } JokerPerformanceStats;

    void joker_expansion_get_stats(const JokerExpansionIterator *iter,
                                   JokerPerformanceStats *stats);
    void joker_expansion_reset_stats(JokerExpansionIterator *iter);
    void joker_expansion_print_stats(const JokerExpansionIterator *iter);

    /* Debug utilities */
    char *joker_expansion_config_to_string(const JokerExpansionConfig *config);
    char *joker_expansion_result_to_string(const JokerExpansionResult *result);

    /*
     * ============================================================================
     * INTEGRATION WITH EXISTING SYSTEMS
     * ============================================================================
     */

    /* Convert legacy JokerDeck_CardMask to modern system */
    mask_t joker_legacy_to_mask(const void *legacy_joker_cardmask);
    void joker_mask_to_legacy(mask_t modern_mask, void *legacy_joker_cardmask);

/* Integration with enumeration adapters */
#define JOKER_EXPAND_CONTROLLED(hand_var, config_var, expanded_var, body)            \
    do                                                                               \
    {                                                                                \
        JokerExpansionIterator *_iter = joker_expansion_create(&(config_var), NULL); \
        if (_iter && joker_expansion_init(_iter, (hand_var)))                        \
        {                                                                            \
            mask_t _expanded;                                                        \
            JokerExpansionResult _result;                                            \
            while (joker_expansion_next(_iter, &_expanded, &_result))                \
            {                                                                        \
                (expanded_var) = _expanded;                                          \
                body;                                                                \
            }                                                                        \
        }                                                                            \
        joker_expansion_destroy(_iter);                                              \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* JOKER_EXPANSION_CONTROLLED_H */
