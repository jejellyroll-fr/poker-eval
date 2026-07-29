/*
 * eval_cache.c - Implementation of LRU cache for poker hand evaluations
 *
 * This implementation uses a hash table with linear probing for collision
 * resolution and LRU eviction policy. The hash function is optimized for
 * card masks to provide good distribution.
 */

#include <poker_eval/core/eval_cache.h>
#include <poker_eval/core/eval.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

/* Global cache instance */
eval_cache_t *g_eval_cache = NULL;

/* Hash function for card masks
 * Uses a combination of XOR and multiplication to spread bits
 */
static inline uint32_t hash_cardmask(StdDeck_CardMask cards, int n_cards) {
    uint64_t hash = 0;

    /* Combine all parts of the card mask */
#ifdef USE_INT64
    hash = cards.cards_n;
#else
    hash = ((uint64_t)cards.cards_nn.n1 << 32) | cards.cards_nn.n2;
#endif

    /* Mix the bits */
    hash ^= (hash >> 33);
    hash *= 0xff51afd7ed558ccdULL;
    hash ^= (hash >> 33);
    hash *= 0xc4ceb9fe1a85ec53ULL;
    hash ^= (hash >> 33);

    /* Include n_cards in the hash */
    hash ^= (uint64_t)n_cards * 0x9e3779b97f4a7c15ULL;

    return (uint32_t)hash;
}

/* Check if two card masks are equal */
static inline bool cardmask_equal(StdDeck_CardMask a, StdDeck_CardMask b) {
#ifdef USE_INT64
    return a.cards_n == b.cards_n;
#else
    return (a.cards_nn.n1 == b.cards_nn.n1) && (a.cards_nn.n2 == b.cards_nn.n2);
#endif
}

/* Round up to next power of 2 */
static uint32_t next_power_of_2(uint32_t n) {
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n++;
    return n;
}

/* Create a new evaluation cache */
eval_cache_t* eval_cache_create(uint32_t size) {
    eval_cache_t *cache;

    /* Validate and adjust size */
    if (size < EVAL_CACHE_MIN_SIZE) {
        size = EVAL_CACHE_MIN_SIZE;
    } else if (size > EVAL_CACHE_MAX_SIZE) {
        size = EVAL_CACHE_MAX_SIZE;
    }

    /* Round to power of 2 */
    size = next_power_of_2(size);

    /* Allocate cache structure */
    cache = (eval_cache_t*)calloc(1, sizeof(eval_cache_t));
    if (!cache) {
        return NULL;
    }

    /* Allocate entries array */
    cache->entries = (eval_cache_entry_t*)calloc(size, sizeof(eval_cache_entry_t));
    if (!cache->entries) {
        free(cache);
        return NULL;
    }

    /* Initialize cache */
    cache->size = size;
    cache->mask = size - 1;
    cache->lru_clock = 0;
    cache->enabled = true;
    memset(&cache->stats, 0, sizeof(cache->stats));

    return cache;
}

/* Destroy an evaluation cache */
void eval_cache_destroy(eval_cache_t *cache) {
    if (cache) {
        if (cache->entries) {
            free(cache->entries);
        }
        free(cache);
    }
}

/* Clear all entries in the cache */
void eval_cache_clear(eval_cache_t *cache) {
    if (cache && cache->entries) {
        memset(cache->entries, 0, cache->size * sizeof(eval_cache_entry_t));
        cache->lru_clock = 0;
        /* Preserve statistics */
    }
}

/* Enable or disable the cache */
void eval_cache_set_enabled(eval_cache_t *cache, bool enabled) {
    if (cache) {
        cache->enabled = enabled;
    }
}

/* Look up a hand evaluation in the cache */
bool eval_cache_lookup(eval_cache_t *cache, StdDeck_CardMask cards, int n_cards, HandVal *result) {
    uint32_t hash, index;
    eval_cache_entry_t *entry;
    int probes = 0;
    const int max_probes = 8;  /* Limit linear probing */

    if (!cache || !cache->enabled || !result) {
        return false;
    }

    /* Calculate hash and initial index */
    hash = hash_cardmask(cards, n_cards);
    index = hash & cache->mask;

    /* Linear probing */
    while (probes < max_probes) {
        entry = &cache->entries[index];

        if (entry->valid &&
            entry->n_cards == n_cards &&
            cardmask_equal(entry->cards, cards)) {
            /* Cache hit */
            cache->stats.hits++;
            entry->lru_counter = ++cache->lru_clock;
            *result = entry->hand_val;
            return true;
        }

        if (!entry->valid) {
            /* Empty slot, not in cache */
            break;
        }

        /* Continue probing */
        index = (index + 1) & cache->mask;
        probes++;
    }

    /* Cache miss */
    cache->stats.misses++;
    return false;
}

/* Insert a hand evaluation result into the cache */
void eval_cache_insert(eval_cache_t *cache, StdDeck_CardMask cards, int n_cards, HandVal hand_val) {
    uint32_t hash, index, victim_index;
    eval_cache_entry_t *entry, *victim;
    int probes = 0;
    const int max_probes = 8;
    uint32_t oldest_lru = UINT32_MAX;

    if (!cache || !cache->enabled) {
        return;
    }

    /* Calculate hash and initial index */
    hash = hash_cardmask(cards, n_cards);
    index = hash & cache->mask;
    victim_index = index;

    /* Linear probing to find insertion point */
    while (probes < max_probes) {
        entry = &cache->entries[index];

        if (!entry->valid) {
            /* Found empty slot */
            entry->cards = cards;
            entry->n_cards = (uint8_t)n_cards;
            entry->hand_val = hand_val;
            entry->lru_counter = ++cache->lru_clock;
            entry->valid = 1;
            cache->stats.insertions++;
            return;
        }

        /* Check if already in cache (update case) */
        if (entry->n_cards == n_cards && cardmask_equal(entry->cards, cards)) {
            entry->hand_val = hand_val;
            entry->lru_counter = ++cache->lru_clock;
            return;
        }

        /* Track oldest entry for eviction */
        if (entry->lru_counter < oldest_lru) {
            oldest_lru = entry->lru_counter;
            victim_index = index;
        }

        /* Continue probing */
        index = (index + 1) & cache->mask;
        probes++;
    }

    /* Need to evict - use LRU victim */
    victim = &cache->entries[victim_index];
    victim->cards = cards;
    victim->n_cards = (uint8_t)n_cards;
    victim->hand_val = hand_val;
    victim->lru_counter = ++cache->lru_clock;
    victim->valid = 1;
    cache->stats.evictions++;
    cache->stats.insertions++;
}

/* Get cache statistics */
const eval_cache_stats_t* eval_cache_get_stats(const eval_cache_t *cache) {
    return cache ? &cache->stats : NULL;
}

/* Reset cache statistics */
void eval_cache_reset_stats(eval_cache_t *cache) {
    if (cache) {
        memset(&cache->stats, 0, sizeof(cache->stats));
    }
}

/* Print cache statistics */
void eval_cache_print_stats(const eval_cache_t *cache) {
    if (!cache) {
        fprintf(stderr, "Cache: Not initialized\n");
        return;
    }

    uint64_t total = cache->stats.hits + cache->stats.misses;
    double hit_rate = total > 0 ? (100.0 * (double)cache->stats.hits / (double)total) : 0.0;

    fprintf(stderr, "=== Evaluation Cache Statistics ===\n");
    fprintf(stderr, "Status:      %s\n", cache->enabled ? "Enabled" : "Disabled");
    fprintf(stderr, "Size:        %u entries\n", cache->size);
    fprintf(stderr, "Hits:        %llu\n", (unsigned long long)cache->stats.hits);
    fprintf(stderr, "Misses:      %llu\n", (unsigned long long)cache->stats.misses);
    fprintf(stderr, "Hit Rate:    %.2f%%\n", hit_rate);
    fprintf(stderr, "Insertions:  %llu\n", (unsigned long long)cache->stats.insertions);
    fprintf(stderr, "Evictions:   %llu\n", (unsigned long long)cache->stats.evictions);
    fprintf(stderr, "==================================\n");
}

/* Cached version of StdDeck_StdRules_EVAL_N */
HandVal StdDeck_StdRules_EVAL_N_Cached(StdDeck_CardMask cards, int n_cards) {
    HandVal result;

    /* Try global cache first */
    if (g_eval_cache && eval_cache_lookup(g_eval_cache, cards, n_cards, &result)) {
        return result;
    }

    /* Cache miss - evaluate normally */
    result = StdDeck_StdRules_EVAL_N(cards, n_cards);

    /* Insert into cache */
    if (g_eval_cache) {
        eval_cache_insert(g_eval_cache, cards, n_cards, result);
    }

    return result;
}

/* Cached version using specific cache instance */
HandVal StdDeck_StdRules_EVAL_N_CachedEx(eval_cache_t *cache, StdDeck_CardMask cards, int n_cards) {
    HandVal result;

    /* Try cache first */
    if (cache && eval_cache_lookup(cache, cards, n_cards, &result)) {
        return result;
    }

    /* Cache miss - evaluate normally */
    result = StdDeck_StdRules_EVAL_N(cards, n_cards);

    /* Insert into cache */
    if (cache) {
        eval_cache_insert(cache, cards, n_cards, result);
    }

    return result;
}

/* Initialize global cache */
bool eval_cache_init_global(uint32_t size) {
    if (g_eval_cache) {
        /* Already initialized */
        return true;
    }

    if (size == 0) {
        size = EVAL_CACHE_DEFAULT_SIZE;
    }

    g_eval_cache = eval_cache_create(size);
    return (g_eval_cache != NULL);
}

/* Destroy global cache */
void eval_cache_destroy_global(void) {
    if (g_eval_cache) {
        eval_cache_destroy(g_eval_cache);
        g_eval_cache = NULL;
    }
}

/* Get global cache instance */
eval_cache_t* eval_cache_get_global(void) {
    return g_eval_cache;
}
