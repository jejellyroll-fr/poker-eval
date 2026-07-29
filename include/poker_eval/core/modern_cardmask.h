/*
 * modern_cardmask.h - Modern unified 64-bit card mask architecture
 *
 * This header provides a modern, unified approach to card masks using
 * 64-bit integers for optimal performance on modern processors.
 */

#ifndef MODERN_CARDMASK_H
#define MODERN_CARDMASK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <poker_eval/core/builtin_compat.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Modern unified mask type - always 64-bit */
typedef uint64_t mask_t;

/* Constants for modern mask operations */
#define MASK_EMPTY          ((mask_t)0)
#define MASK_FULL           ((mask_t)0xFFFFFFFFFFFFFFFFULL)

/* Standard deck constants */
#define MODERN_DECK_SIZE    52
#define MODERN_RANK_COUNT   13
#define MODERN_SUIT_COUNT   4

/* Ranks (0-12) */
#define MODERN_RANK_2       0
#define MODERN_RANK_3       1
#define MODERN_RANK_4       2
#define MODERN_RANK_5       3
#define MODERN_RANK_6       4
#define MODERN_RANK_7       5
#define MODERN_RANK_8       6
#define MODERN_RANK_9       7
#define MODERN_RANK_T       8
#define MODERN_RANK_J       9
#define MODERN_RANK_Q       10
#define MODERN_RANK_K       11
#define MODERN_RANK_A       12

/* Suits (0-3) */
#define MODERN_SUIT_CLUBS    0
#define MODERN_SUIT_DIAMONDS 1
#define MODERN_SUIT_HEARTS   2
#define MODERN_SUIT_SPADES   3

/* Card indexing: card = rank + 13 * suit */
#define MODERN_MAKE_CARD(rank, suit) ((rank) + 13 * (suit))
#define MODERN_GET_RANK(card) ((card) % 13)
#define MODERN_GET_SUIT(card) ((card) / 13)

/* Core mask operations */
static inline bool mask_is_set(mask_t mask, int card) {
    return (mask & (1ULL << card)) != 0;
}

static inline mask_t mask_set(mask_t mask, int card) {
    return mask | (1ULL << card);
}

static inline mask_t mask_unset(mask_t mask, int card) {
    return mask & ~(1ULL << card);
}

static inline mask_t mask_toggle(mask_t mask, int card) {
    return mask ^ (1ULL << card);
}

static inline mask_t mask_union(mask_t a, mask_t b) {
    return a | b;
}

static inline mask_t mask_intersect(mask_t a, mask_t b) {
    return a & b;
}

static inline mask_t mask_subtract(mask_t a, mask_t b) {
    return a & ~b;
}

static inline bool mask_is_empty(mask_t mask) {
    return mask == MASK_EMPTY;
}

static inline bool mask_is_subset(mask_t subset, mask_t superset) {
    return (subset & superset) == subset;
}

static inline bool mask_intersects(mask_t a, mask_t b) {
    return (a & b) != 0;
}

/* Population count (number of set bits) */
static inline int mask_popcount(mask_t mask) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_popcountll(mask);
#elif defined(_MSC_VER)
    return (int)__popcnt64(mask);
#else
    /* Fallback implementation */
    int count = 0;
    while (mask) {
        count += mask & 1;
        mask >>= 1;
    }
    return count;
#endif
}

/* Find first/last set bit */
static inline int mask_first_set(mask_t mask) {
#if defined(__GNUC__) || defined(__clang__)
    return mask ? __builtin_ctzll(mask) : -1;
#elif defined(_MSC_VER)
    unsigned long index;
    return _BitScanForward64(&index, mask) ? (int)index : -1;
#else
    /* Fallback implementation */
    if (!mask) return -1;
    int pos = 0;
    while (!(mask & 1)) {
        mask >>= 1;
        pos++;
    }
    return pos;
#endif
}

static inline int mask_last_set(mask_t mask) {
#if defined(__GNUC__) || defined(__clang__)
    return mask ? 63 - __builtin_clzll(mask) : -1;
#elif defined(_MSC_VER)
    unsigned long index;
    return _BitScanReverse64(&index, mask) ? (int)index : -1;
#else
    /* Fallback implementation */
    if (!mask) return -1;
    int pos = 63;
    while (!(mask & (1ULL << pos))) {
        pos--;
    }
    return pos;
#endif
}

/* Iteration support */
#define MASK_FOR_EACH_SET_BIT(mask, bit) \
    for (int bit = mask_first_set(mask); bit >= 0; \
         mask = mask_unset(mask, bit), bit = mask_first_set(mask))

/* Rank and suit extraction for entire mask */
static inline mask_t mask_get_rank_mask(mask_t cards, int rank) {
    mask_t rank_mask = 0;
    for (int suit = 0; suit < 4; suit++) {
        if (mask_is_set(cards, MODERN_MAKE_CARD(rank, suit))) {
            rank_mask = mask_set(rank_mask, suit);
        }
    }
    return rank_mask;
}

static inline mask_t mask_get_suit_mask(mask_t cards, int suit) {
    mask_t suit_mask = 0;
    for (int rank = 0; rank < 13; rank++) {
        if (mask_is_set(cards, MODERN_MAKE_CARD(rank, suit))) {
            suit_mask = mask_set(suit_mask, rank);
        }
    }
    return suit_mask;
}

/* Conversion utilities */
typedef struct {
    int cards[52];
    int count;
} card_list_t;

static inline card_list_t mask_to_list(mask_t mask) {
    card_list_t list = {0};
    MASK_FOR_EACH_SET_BIT(mask, card) {
        list.cards[list.count++] = card;
    }
    return list;
}

static inline mask_t list_to_mask(const int* cards, int count) {
    mask_t mask = MASK_EMPTY;
    for (int i = 0; i < count; i++) {
        mask = mask_set(mask, cards[i]);
    }
    return mask;
}

/* Debug and utility functions */
void mask_print(mask_t mask);
void mask_print_binary(mask_t mask);
bool mask_validate_hand(mask_t mask, int expected_cards);

/* Performance testing */
typedef struct {
    double set_ops_per_sec;
    double popcount_ops_per_sec;
    double iteration_ops_per_sec;
} mask_performance_t;

mask_performance_t mask_benchmark(int iterations);

/* String conversion support */
const char* mask_to_string(mask_t mask, char* buffer, size_t buffer_size);
mask_t string_to_mask(const char* str);

/* Validation */
static inline bool mask_is_valid_card(int card) {
    return card >= 0 && card < 52;
}

static inline bool mask_is_valid(mask_t mask) {
    /* Check that no bits beyond card 51 are set */
    return (mask & ~((1ULL << 52) - 1)) == 0;
}

#ifdef __cplusplus
}
#endif

#endif /* MODERN_CARDMASK_H */
