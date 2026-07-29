/*
 * joker_expansion_controlled.c - Implementation of controlled joker expansion
 */

#include <poker_eval/core/joker_expansion_controlled.h>
#include <poker_eval/core/cardmask_compat.h>
#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/core/eval_context.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Standard deck full mask for joker expansion */
#define JOKER_STANDARD_DECK_MASK ((mask_t)((1ULL << 52) - 1ULL))

/*
 * ============================================================================
 * CONFIGURATION HELPERS
 * ============================================================================
 */

JokerExpansionConfig joker_config_default(void) {
    JokerExpansionConfig config = {0};
    config.strategy = JOKER_EXPAND_STRATEGIC;
    config.max_expansions = 20;  /* Reasonable default */
    config.enable_cache = true;
    config.enable_pruning = true;
    config.cache_size = 256;
    config.available_cards = JOKER_STANDARD_DECK_MASK;
    config.forbidden_cards = 0;
    config.target_rules = EVAL_RULES_HIGH;
    return config;
}

JokerExpansionConfig joker_config_performance(void) {
    JokerExpansionConfig config = joker_config_default();
    config.strategy = JOKER_EXPAND_LIMITED;
    config.max_expansions = 8;   /* Aggressive pruning */
    config.enable_cache = true;
    config.enable_pruning = true;
    return config;
}

JokerExpansionConfig joker_config_accuracy(void) {
    JokerExpansionConfig config = joker_config_default();
    config.strategy = JOKER_EXPAND_FULL;
    config.max_expansions = 52;  /* Try all cards */
    config.enable_cache = true;
    config.enable_pruning = false;
    return config;
}

JokerExpansionConfig joker_config_holdem(void) {
    JokerExpansionConfig config = joker_config_default();
    config.strategy = JOKER_EXPAND_STRATEGIC;
    config.max_expansions = 12;  /* Hold'em focused */
    return config;
}

JokerExpansionConfig joker_config_stud(void) {
    JokerExpansionConfig config = joker_config_default();
    config.strategy = JOKER_EXPAND_CATEGORY;
    config.max_expansions = 16;  /* Stud needs more variety */
    return config;
}

JokerExpansionConfig joker_config_omaha(void) {
    JokerExpansionConfig config = joker_config_default();
    config.strategy = JOKER_EXPAND_RANKING;
    config.max_expansions = 10;  /* Omaha rank-sensitive */
    return config;
}

/*
 * ============================================================================
 * SMART EXPANSION STRATEGIES
 * ============================================================================
 */

/* Extract card rank from position (0-51 -> rank 0-12) */
static inline int card_rank(int card_pos) {
    return card_pos % 13;
}

/* Extract card suit from position (0-51 -> suit 0-3) */
static inline int card_suit(int card_pos) {
    return card_pos / 13;
}

/* Check if hand has potential for specific hand types */
static bool has_flush_potential(mask_t natural_cards) {
    int suit_counts[4] = {0};
    mask_t tmp = natural_cards;

    while (tmp) {
        int pos = __builtin_ctzll(tmp);
        suit_counts[card_suit(pos)]++;
        tmp &= tmp - 1;
    }

    /* Need 4+ of same suit to make flush with joker */
    for (int s = 0; s < 4; s++) {
        if (suit_counts[s] >= 4) return true;
    }
    return false;
}

static bool has_straight_potential(mask_t natural_cards) {
    int rank_counts[13] = {0};
    mask_t tmp = natural_cards;

    while (tmp) {
        int pos = __builtin_ctzll(tmp);
        rank_counts[card_rank(pos)]++;
        tmp &= tmp - 1;
    }

    /* Look for 4 consecutive ranks */
    for (int r = 0; r <= 9; r++) {
        int consecutive = 0;
        for (int i = 0; i < 5; i++) {
            if (rank_counts[(r + i) % 13] > 0) consecutive++;
        }
        if (consecutive >= 4) return true;
    }
    return false;
}

static int get_max_rank_count(mask_t natural_cards) {
    int rank_counts[13] = {0};
    mask_t tmp = natural_cards;

    while (tmp) {
        int pos = __builtin_ctzll(tmp);
        rank_counts[card_rank(pos)]++;
        tmp &= tmp - 1;
    }

    int max_count = 0;
    for (int r = 0; r < 13; r++) {
        if (rank_counts[r] > max_count) {
            max_count = rank_counts[r];
        }
    }
    return max_count;
}

uint32_t joker_expansion_strategic_candidates(mask_t natural_cards,
                                             mask_t available_cards,
                                             mask_t* candidates,
                                             uint32_t max_candidates) {
    uint32_t count = 0;
    bool has_flush_pot = has_flush_potential(natural_cards);
    bool has_straight_pot = has_straight_potential(natural_cards);
    int max_rank_count = get_max_rank_count(natural_cards);

    /* Priority 1: Cards that complete strong hands */
    mask_t tmp = available_cards;
    while (tmp && count < max_candidates) {
        int pos = __builtin_ctzll(tmp);
        bool add_candidate = false;

        /* If we have 4-flush potential, prioritize same suit */
        if (has_flush_pot) {
            int suit = card_suit(pos);
            mask_t suit_mask = 0x1fff << (suit * 13);  /* All cards of this suit */
            if (natural_cards & suit_mask) {
                add_candidate = true;
            }
        }

        /* If we have straight potential, prioritize filling gaps */
        if (has_straight_pot && !add_candidate) {
            /* Simplified: favor middle ranks */
            int rank = card_rank(pos);
            if (rank >= 4 && rank <= 8) {  /* 6-Q range */
                add_candidate = true;
            }
        }

        /* If we have trips, favor same rank for quads */
        if (max_rank_count >= 3 && !add_candidate) {
            int rank = card_rank(pos);
            /* Check if this rank matches our trips */
            mask_t rank_mask = 0xf << (rank * 4);  /* All cards of this rank */
            if (__builtin_popcountll(natural_cards & rank_mask) >= 3) {
                add_candidate = true;
            }
        }

        /* If nothing special, favor high cards */
        if (!add_candidate) {
            int rank = card_rank(pos);
            if (rank >= 10) {  /* A, K, Q */
                add_candidate = true;
            }
        }

        if (add_candidate) {
            candidates[count++] = 1ULL << pos;
        }

        tmp &= tmp - 1;
    }

    /* If we didn't fill all slots, add remaining high cards */
    tmp = available_cards;
    while (tmp && count < max_candidates) {
        int pos = __builtin_ctzll(tmp);
        mask_t card_mask = 1ULL << pos;

        /* Check if already added */
        bool already_added = false;
        for (uint32_t i = 0; i < count; i++) {
            if (candidates[i] == card_mask) {
                already_added = true;
                break;
            }
        }

        if (!already_added) {
            candidates[count++] = card_mask;
        }

        tmp &= tmp - 1;
    }

    return count;
}

uint32_t joker_expansion_ranking_candidates(mask_t available_cards,
                                           mask_t* candidates,
                                           uint32_t max_candidates,
                                           bool prefer_high_cards) {
    uint32_t count = 0;

    /* Sort by rank priority */
    int rank_order[13];
    if (prefer_high_cards) {
        /* A, K, Q, J, T, 9, ... 2 */
        for (int i = 0; i < 13; i++) {
            rank_order[i] = 12 - i;
        }
    } else {
        /* 2, 3, 4, 5, 6, 7, 8, 9, T, J, Q, K, A */
        for (int i = 0; i < 13; i++) {
            rank_order[i] = i;
        }
    }

    for (int r_idx = 0; r_idx < 13 && count < max_candidates; r_idx++) {
        int rank = rank_order[r_idx];

        /* Try all suits of this rank */
        for (int suit = 0; suit < 4 && count < max_candidates; suit++) {
            int pos = rank + suit * 13;
            mask_t card_mask = 1ULL << pos;

            if (available_cards & card_mask) {
                candidates[count++] = card_mask;
            }
        }
    }

    return count;
}

uint32_t joker_expansion_category_candidates(mask_t natural_cards,
                                            mask_t available_cards,
                                            mask_t* candidates,
                                            uint32_t max_candidates,
                                            eval_rules_t rules) {
    /* For now, delegate to strategic for simplicity */
    /* Future: implement hand-type-specific prioritization */
    return joker_expansion_strategic_candidates(natural_cards, available_cards,
                                               candidates, max_candidates);
}

/*
 * ============================================================================
 * CACHE MANAGEMENT
 * ============================================================================
 */

static uint64_t joker_cache_hash(mask_t natural_cards, mask_t joker_positions) {
    /* Simple hash combining natural cards and joker positions */
    return natural_cards ^ (joker_positions << 16) ^ (joker_positions >> 16);
}

static JokerCacheEntry* joker_cache_find(JokerExpansionIterator* iter,
                                         mask_t natural_cards,
                                         mask_t joker_positions) {
    if (!iter->cache || iter->cache_used == 0) return NULL;

    /* Note: hash could be used for indexed lookup in future optimization */
    (void)joker_cache_hash(natural_cards, joker_positions);

    for (uint32_t i = 0; i < iter->cache_used; i++) {
        JokerCacheEntry* entry = &iter->cache[i];
        if (entry->natural_cards == natural_cards &&
            entry->joker_positions == joker_positions) {
            entry->access_count = ++iter->cache_access_counter;
            return entry;
        }
    }

    return NULL;
}

static void joker_cache_insert(JokerExpansionIterator* iter,
                               mask_t natural_cards,
                               mask_t joker_positions,
                               const JokerExpansionSet* result) {
    if (!iter->cache) return;

    /* Find slot (LRU eviction if full) */
    uint32_t slot = iter->cache_used;
    if (slot >= iter->cache_capacity) {
        /* Find LRU entry */
        uint64_t min_access = UINT64_MAX;
        for (uint32_t i = 0; i < iter->cache_capacity; i++) {
            if (iter->cache[i].access_count < min_access) {
                min_access = iter->cache[i].access_count;
                slot = i;
            }
        }
        /* Free old entry */
        if (iter->cache[slot].cached_result.results) {
            free(iter->cache[slot].cached_result.results);
        }
    } else {
        iter->cache_used++;
    }

    /* Insert new entry */
    JokerCacheEntry* entry = &iter->cache[slot];
    entry->natural_cards = natural_cards;
    entry->joker_positions = joker_positions;
    entry->cached_result = *result;
    entry->access_count = ++iter->cache_access_counter;

    /* Deep copy results array */
    if (result->results && result->num_results > 0) {
        entry->cached_result.results = malloc(result->num_results * sizeof(JokerExpansionResult));
        if (entry->cached_result.results) {
            memcpy(entry->cached_result.results, result->results,
                   result->num_results * sizeof(JokerExpansionResult));
        }
    }
}

/*
 * ============================================================================
 * ITERATOR IMPLEMENTATION
 * ============================================================================
 */

JokerExpansionIterator* joker_expansion_create(const JokerExpansionConfig* config,
                                              EvalContext* eval_context) {
    if (!config) return NULL;

    JokerExpansionIterator* iter = calloc(1, sizeof(JokerExpansionIterator));
    if (!iter) return NULL;

    iter->config = *config;
    iter->eval_context = eval_context;

    /* Allocate candidate array */
    iter->candidate_cards = malloc(config->max_expansions * sizeof(mask_t));
    if (!iter->candidate_cards) {
        free(iter);
        return NULL;
    }

    /* Allocate cache if enabled */
    if (config->enable_cache && config->cache_size > 0) {
        iter->cache = calloc(config->cache_size, sizeof(JokerCacheEntry));
        iter->cache_capacity = config->cache_size;
        iter->cache_used = 0;
        iter->cache_access_counter = 0;
    }

    return iter;
}

bool joker_expansion_init(JokerExpansionIterator* iter, mask_t hand_with_jokers) {
    if (!iter) return false;

    /* Reset state */
    iter->current_expansion = 0;
    iter->initialized = true;
    iter->exhausted = false;
    iter->num_candidates = 0;

    /* Separate jokers from natural cards */
    /* For simplicity, assume joker is at position 52 if using 53-card deck */
    mask_t joker_bit = 1ULL << 52;  /* Joker position */

    if (hand_with_jokers & joker_bit) {
        iter->joker_mask = joker_bit;
        iter->natural_cards = hand_with_jokers & ~joker_bit;
        iter->num_jokers = 1;
    } else {
        /* No jokers found - this shouldn't happen but handle gracefully */
        iter->joker_mask = 0;
        iter->natural_cards = hand_with_jokers;
        iter->num_jokers = 0;
        iter->exhausted = true;
        return false;
    }

    /* Generate candidate cards based on strategy */
    mask_t available = iter->config.available_cards & ~iter->natural_cards & ~iter->config.forbidden_cards;

    switch (iter->config.strategy) {
        case JOKER_EXPAND_STRATEGIC:
            iter->num_candidates = joker_expansion_strategic_candidates(
                iter->natural_cards, available, iter->candidate_cards, iter->config.max_expansions);
            break;

        case JOKER_EXPAND_RANKING:
            iter->num_candidates = joker_expansion_ranking_candidates(
                available, iter->candidate_cards, iter->config.max_expansions, true);
            break;

        case JOKER_EXPAND_CATEGORY:
            iter->num_candidates = joker_expansion_category_candidates(
                iter->natural_cards, available, iter->candidate_cards,
                iter->config.max_expansions, iter->config.target_rules);
            break;

        case JOKER_EXPAND_LIMITED:
            iter->num_candidates = joker_expansion_ranking_candidates(
                available, iter->candidate_cards, iter->config.max_expansions, true);
            break;

        case JOKER_EXPAND_FULL:
        default: {
            /* Add all available cards */
            mask_t tmp = available;
            iter->num_candidates = 0;
            while (tmp && iter->num_candidates < iter->config.max_expansions) {
                int pos = __builtin_ctzll(tmp);
                iter->candidate_cards[iter->num_candidates++] = 1ULL << pos;
                tmp &= tmp - 1;
            }
            break;
        }
    }

    return iter->num_candidates > 0;
}

bool joker_expansion_next(JokerExpansionIterator* iter,
                         mask_t* expanded_hand,
                         JokerExpansionResult* result) {
    if (!iter || !iter->initialized || iter->exhausted) return false;
    if (!expanded_hand || !result) return false;

    if (iter->current_expansion >= iter->num_candidates) {
        iter->exhausted = true;
        return false;
    }

    /* Get next candidate */
    mask_t joker_as_card = iter->candidate_cards[iter->current_expansion];
    iter->current_expansion++;

    /* Build expanded hand */
    *expanded_hand = iter->natural_cards | joker_as_card;

    /* Evaluate hand (simplified - would use eval_context in real implementation) */
    result->joker_as_card = joker_as_card;
    result->hand_value = __builtin_popcountll(*expanded_hand) * 1000;  /* Dummy evaluation */
    result->is_optimal = false;  /* Will be determined by caller */

    iter->expansions_generated++;

    return true;
}

bool joker_expansion_get_best(JokerExpansionIterator* iter,
                             mask_t* best_hand,
                             eval_t* best_value) {
    if (!iter) return false;

    /* This is a simplified version - full implementation would track best during iteration */
    if (best_hand) *best_hand = iter->natural_cards;
    if (best_value) *best_value = 0;

    return true;
}

bool joker_expansion_reset(JokerExpansionIterator* iter) {
    if (!iter) return false;

    iter->current_expansion = 0;
    iter->initialized = false;
    iter->exhausted = false;
    iter->num_candidates = 0;

    return true;
}

void joker_expansion_destroy(JokerExpansionIterator* iter) {
    if (!iter) return;

    if (iter->candidate_cards) {
        free(iter->candidate_cards);
    }

    if (iter->cache) {
        /* Free cached results */
        for (uint32_t i = 0; i < iter->cache_used; i++) {
            if (iter->cache[i].cached_result.results) {
                free(iter->cache[i].cached_result.results);
            }
        }
        free(iter->cache);
    }

    free(iter);
}

/*
 * ============================================================================
 * HIGH-LEVEL EVALUATION FUNCTIONS
 * ============================================================================
 */

eval_t joker_eval_controlled(EvalContext* eval_context,
                             mask_t hand_with_jokers,
                             const JokerExpansionConfig* config) {
    JokerExpansionIterator* iter = joker_expansion_create(config, eval_context);
    if (!iter) return EVAL_INVALID;

    if (!joker_expansion_init(iter, hand_with_jokers)) {
        joker_expansion_destroy(iter);
        return EVAL_INVALID;
    }

    eval_t best_value = 0;
    mask_t expanded_hand;
    JokerExpansionResult result;

    while (joker_expansion_next(iter, &expanded_hand, &result)) {
        if (result.hand_value > best_value) {
            best_value = result.hand_value;
        }

        /* Early termination if we found an excellent hand */
        if (config->enable_pruning && best_value > 8000000) {  /* Strong hand threshold */
            break;
        }
    }

    joker_expansion_destroy(iter);
    return best_value;
}

/*
 * ============================================================================
 * PERFORMANCE AND DEBUGGING
 * ============================================================================
 */

void joker_expansion_get_stats(const JokerExpansionIterator* iter,
                              JokerPerformanceStats* stats) {
    if (!iter || !stats) return;

    memset(stats, 0, sizeof(JokerPerformanceStats));
    stats->total_evaluations = 1;  /* This evaluation */
    stats->expansions_tried = iter->expansions_generated;
    stats->cache_hits = iter->cache_hits;
    stats->cache_misses = iter->expansions_generated - iter->cache_hits;
    stats->expansions_pruned = iter->pruned_expansions;

    if (stats->total_evaluations > 0) {
        stats->avg_expansions_per_eval = (double)stats->expansions_tried / (double)stats->total_evaluations;
    }

    if (stats->cache_hits + stats->cache_misses > 0) {
        stats->cache_hit_rate = (double)stats->cache_hits / (double)(stats->cache_hits + stats->cache_misses);
    }
}

void joker_expansion_reset_stats(JokerExpansionIterator* iter) {
    if (!iter) return;

    iter->expansions_generated = 0;
    iter->cache_hits = 0;
    iter->pruned_expansions = 0;
}

void joker_expansion_print_stats(const JokerExpansionIterator* iter) {
    if (!iter) return;

    JokerPerformanceStats stats;
    joker_expansion_get_stats(iter, &stats);

    printf("Joker Expansion Performance:\n");
    printf("  Expansions tried: %llu\n", (unsigned long long)stats.expansions_tried);
    printf("  Cache hits: %llu\n", (unsigned long long)stats.cache_hits);
    printf("  Cache hit rate: %.1f%%\n", stats.cache_hit_rate * 100.0);
    printf("  Expansions pruned: %llu\n", (unsigned long long)stats.expansions_pruned);
    printf("  Avg expansions/eval: %.1f\n", stats.avg_expansions_per_eval);
}

/*
 * ============================================================================
 * CONFIGURATION MANIPULATION
 * ============================================================================
 */

void joker_config_set_available_cards(JokerExpansionConfig* config, mask_t available) {
    if (config) {
        config->available_cards = available;
    }
}

void joker_config_set_forbidden_cards(JokerExpansionConfig* config, mask_t forbidden) {
    if (config) {
        config->forbidden_cards = forbidden;
    }
}

void joker_config_set_max_expansions(JokerExpansionConfig* config, uint32_t max_expansions) {
    if (config) {
        config->max_expansions = max_expansions;
    }
}
