/*
 * eval_cache.h - LRU cache for poker hand evaluations (canonical header)
 */

#ifndef __EVAL_CACHE_H__
#define __EVAL_CACHE_H__

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/handval.h>
#include <poker_eval/deck/deck_std.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Cache configuration */
#define EVAL_CACHE_DEFAULT_SIZE 16384  /* Default number of entries (must be power of 2) */
#define EVAL_CACHE_MAX_SIZE     65536  /* Maximum cache size */
#define EVAL_CACHE_MIN_SIZE     1024   /* Minimum cache size */

/* Cache statistics */
typedef struct {
    uint64_t hits;
    uint64_t misses;
    uint64_t evictions;
    uint64_t insertions;
} eval_cache_stats_t;

/* Cache entry structure */
typedef struct eval_cache_entry {
    StdDeck_CardMask cards;     /* The card mask (key) */
    uint8_t n_cards;            /* Number of cards */
    uint8_t valid;              /* Entry validity flag */
    uint16_t padding;           /* Padding for alignment */
    HandVal hand_val;           /* Cached evaluation result */
    uint32_t lru_counter;       /* LRU tracking counter */
} eval_cache_entry_t;

/* Main cache structure */
typedef struct eval_cache {
    eval_cache_entry_t *entries;    /* Hash table entries */
    uint32_t size;                  /* Number of entries (power of 2) */
    uint32_t mask;                  /* Size - 1 (for fast modulo) */
    uint32_t lru_clock;             /* Global LRU counter */
    eval_cache_stats_t stats;       /* Performance statistics */
    bool enabled;                   /* Cache enable flag */
} eval_cache_t;

/* Global cache instance (optional, can also create local instances) */
extern eval_cache_t *g_eval_cache;

/* Cache management functions */
eval_cache_t* eval_cache_create(uint32_t size);
void eval_cache_destroy(eval_cache_t *cache);
void eval_cache_clear(eval_cache_t *cache);
void eval_cache_set_enabled(eval_cache_t *cache, bool enabled);

/* Lookups & inserts */
bool eval_cache_lookup(eval_cache_t *cache, StdDeck_CardMask cards, int n_cards, HandVal *result);
void eval_cache_insert(eval_cache_t *cache, StdDeck_CardMask cards, int n_cards, HandVal hand_val);

/* Stats */
const eval_cache_stats_t* eval_cache_get_stats(const eval_cache_t *cache);
void eval_cache_reset_stats(eval_cache_t *cache);
void eval_cache_print_stats(const eval_cache_t *cache);

/* Cached evaluation helpers */
HandVal StdDeck_StdRules_EVAL_N_Cached(StdDeck_CardMask cards, int n_cards);
HandVal StdDeck_StdRules_EVAL_N_CachedEx(eval_cache_t *cache, StdDeck_CardMask cards, int n_cards);

/* Global cache management */
bool eval_cache_init_global(uint32_t size);
void eval_cache_destroy_global(void);
eval_cache_t* eval_cache_get_global(void);

#ifdef __cplusplus
}
#endif

#endif /* __EVAL_CACHE_H__ */
