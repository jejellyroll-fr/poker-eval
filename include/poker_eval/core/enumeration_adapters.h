/*
 * enumeration_adapters.h - Header-only helpers to migrate legacy macros
 *                          to the modern combination generators.
 *
 * These adapters let legacy enumeration sites (which expect a local
 * StdDeck_CardMask variable and then execute a body) iterate using
 * modern combo generators without refactoring surrounding code.
 */

#ifndef ENUMERATION_ADAPTERS_H
#define ENUMERATION_ADAPTERS_H

#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/core/modern_combinations.h>
#include <poker_eval/core/cardmask_compat.h>
#include <stdlib.h>
#include <stdbool.h>
#include <poker_eval/deck/deck_joker.h>
#ifdef _MSC_VER
#include <malloc.h>
#endif

/* Define RANDOM if not already defined (from enumerate.h) */
#ifndef RANDOM
#define RANDOM rand
#endif

/* Macro helpers to generate unique names */
#ifndef ENUM_ADAPTERS_CONCAT
#define ENUM_ADAPTERS_CONCAT(a, b) a##b
#define ENUM_ADAPTERS_CONCAT2(a, b) ENUM_ADAPTERS_CONCAT(a, b)
#endif

#ifdef _MSC_VER
#define ENUM_ADAPTERS_STACK_ALLOC(type, name, count) \
    type *name = (type *)_alloca(sizeof(type) * (size_t)(count))
#else
#define ENUM_ADAPTERS_STACK_ALLOC(type, name, count) \
    type name[(count)]
#endif

/* Full 52-card deck in modern mask_t space */
#ifndef MODERN_FULL_DECK_MASK
#define MODERN_FULL_DECK_MASK ((mask_t)((1ULL << 52) - 1ULL))
#endif

/* Precomputed mask for ShortDeck (36 cards: ranks 6..A across 4 suits) */
static inline mask_t modern_short_deck_mask(void)
{
    mask_t m = 0;
    for (int suit = 0; suit < 4; ++suit)
    {
        for (int rank = MODERN_RANK_6; rank <= MODERN_RANK_A; ++rank)
        {
            int card = MODERN_MAKE_CARD(rank, suit);
            m |= (1ULL << card);
        }
    }
    return m;
}

/* Optimized uniform sampling without replacement: pick k distinct cards from avail */
static inline mask_t sample_k_from_mask(mask_t avail, int k)
{
    int n = __builtin_popcountll(avail); /* Use builtin popcount */
    if (k <= 0)
        return 0;
    if (n < k)
        return 0;

    /* Fast path for small k - use rejection sampling */
    if (k <= 3 && n >= 8)
    {
        mask_t result = 0;
        int selected = 0;

        while (selected < k)
        {
            /* Random bit position in available mask */
            int pos = RANDOM() % 64;
            mask_t bit = 1ULL << pos;

            if ((avail & bit) && !(result & bit))
            {
                result |= bit;
                selected++;
            }
        }
        return result;
    }

    /* Extract indices using optimized bit scanning */
    int idx[64];
    int count = 0;
    mask_t tmp = avail;

    while (tmp && count < 64)
    {
        int pos = __builtin_ctzll(tmp); /* Count trailing zeros */
        idx[count++] = pos;
        tmp &= tmp - 1; /* Clear lowest set bit */
    }

    /* Fisher-Yates partial shuffle for first k positions */
    for (int i = 0; i < k; ++i)
    {
        int r = i + (RANDOM() % (count - i));
        int t = idx[i];
        idx[i] = idx[r];
        idx[r] = t;
    }

    /* Build result mask with direct bit operations */
    mask_t out = 0;
    for (int i = 0; i < k; ++i)
    {
        out |= (1ULL << idx[i]);
    }
    return out;
}
/* Utility: union of StdDeck_CardMask array to modern mask_t */
static inline mask_t cardmask_array_union(const StdDeck_CardMask *arr, int count)
{
    mask_t u = 0;
    for (int i = 0; i < count; ++i)
    {
        u |= cardmask_to_mask_t(arr[i]);
    }
    return u;
}

/* JokerDeck helpers using 64-bit bitset (0..52) */
static inline uint64_t joker_bits_full(void)
{
    /* 53 bits set */
    return (53 >= 64) ? ~0ULL : ((1ULL << 53) - 1ULL);
}

static inline uint64_t joker_mask_to_bits(JokerDeck_CardMask m)
{
    uint64_t b = 0;
    for (int i = 0; i < JokerDeck_N_CARDS; ++i)
    {
        if (JokerDeck_CardMask_CARD_IS_SET(m, i))
            b |= (1ULL << i);
    }
    return b;
}

static inline JokerDeck_CardMask joker_bits_to_mask(uint64_t bits)
{
    JokerDeck_CardMask m;
    JokerDeck_CardMask_RESET(m);
    for (int i = 0; i < JokerDeck_N_CARDS; ++i)
    {
        if (bits & (1ULL << i))
        {
            JokerDeck_CardMask_SET(m, i);
        }
    }
    return m;
}

static inline uint64_t joker_sample_k(uint64_t avail, int k)
{
    if (k <= 0)
        return 0;

    int n = __builtin_popcountll(avail);
    if (n < k)
        return 0;

    /* Fast path for small k - rejection sampling */
    if (k <= 3 && n >= 8)
    {
        uint64_t result = 0;
        int selected = 0;

        while (selected < k)
        {
            int pos = RANDOM() % JokerDeck_N_CARDS;
            uint64_t bit = 1ULL << pos;

            if ((avail & bit) && !(result & bit))
            {
                result |= bit;
                selected++;
            }
        }
        return result;
    }

    /* Optimized index extraction */
    int idx[64];
    int count = 0;
    uint64_t tmp = avail;

    while (tmp && count < 64)
    {
        int pos = __builtin_ctzll(tmp);
        idx[count++] = pos;
        tmp &= tmp - 1; /* Clear lowest bit */
    }

    /* Fisher-Yates shuffle */
    for (int i = 0; i < k; ++i)
    {
        int r = i + (RANDOM() % (count - i));
        int t = idx[i];
        idx[i] = idx[r];
        idx[r] = t;
    }

    /* Direct bit assembly */
    uint64_t out = 0;
    for (int i = 0; i < k; ++i)
    {
        out |= (1ULL << idx[i]);
    }
    return out;
}

/* Optimized Joker k-combination iterator with precomputed positions */
typedef struct
{
    uint64_t avail_bits;
    int positions[64]; /* Pre-extracted available positions */
    int N;             /* Total available positions */
    int k;             /* Combination size */
    int idx[64];       /* Current indices */
    int started;
    int valid;
    uint64_t current_mask; /* Cached current combination mask */
    int dirty;             /* Flag to rebuild mask when needed */
} joker_k_iter;

static inline void joker_kiter_init(joker_k_iter *it, uint64_t avail, int k)
{
    it->avail_bits = avail;
    it->k = k;
    it->N = 0;
    it->started = 0;
    it->valid = 0;
    it->current_mask = 0;
    it->dirty = 1;

    if (k <= 0)
    {
        it->valid = 1;
        it->current_mask = 0;
        it->dirty = 0;
        return;
    }

    /* Pre-extract positions using bit manipulation optimizations */
    uint64_t tmp = avail;
    while (tmp && it->N < 64)
    {
        int pos = __builtin_ctzll(tmp); /* Count trailing zeros - faster than loop */
        it->positions[it->N++] = pos;
        tmp &= tmp - 1; /* Clear lowest set bit */
    }

    if (it->N < k)
    {
        it->valid = 0;
        return;
    }

    /* Initialize first combination indices */
    for (int i = 0; i < k; ++i)
        it->idx[i] = i;
    it->valid = 1;
}

static inline int joker_kiter_next(joker_k_iter *it, uint64_t *comb)
{
    if (!it->valid)
        return 0;

    if (it->k <= 0)
    {
        *comb = 0;
        it->valid = 0;
        return 1;
    }

    /* First combination - build and cache mask */
    if (!it->started)
    {
        it->current_mask = 0;
        for (int i = 0; i < it->k; ++i)
        {
            it->current_mask |= (1ULL << it->positions[it->idx[i]]);
        }
        *comb = it->current_mask;
        it->started = 1;
        it->dirty = 0;
        return 1;
    }

    /* Find rightmost index that can be incremented */
    int N = it->N, k = it->k;
    int i;
    for (i = k - 1; i >= 0; --i)
    {
        if (it->idx[i] != i + (N - k))
            break;
    }

    if (i < 0)
    {
        it->valid = 0;
        return 0;
    }

    /* Incremental mask update - more efficient than full rebuild */
    int old_pos = it->positions[it->idx[i]];
    it->idx[i]++;
    int new_pos = it->positions[it->idx[i]];

    /* Update mask: remove old bit, add new bit */
    it->current_mask &= ~(1ULL << old_pos);
    it->current_mask |= (1ULL << new_pos);

    /* Reset subsequent positions and update mask */
    for (int j = i + 1; j < k; ++j)
    {
        int prev_pos = it->positions[it->idx[j]];
        it->idx[j] = it->idx[j - 1] + 1;
        int curr_pos = it->positions[it->idx[j]];

        /* Incremental update */
        it->current_mask &= ~(1ULL << prev_pos);
        it->current_mask |= (1ULL << curr_pos);
    }

    *comb = it->current_mask;
    return 1;
}

/* Exhaustive: JokerDeck multi-hand disjoint enumeration */
#define ENUM_JOKERDECK_ENUMERATE_MULTI_FROM_DEAD(num_sets, set_sizes, jdead_mask, out_sets_var, body) \
    do                                                                                                \
    {                                                                                                 \
        int _ns = (num_sets);                                                                         \
        if (_ns <= 0)                                                                                 \
        {                                                                                             \
            body;                                                                                     \
            break;                                                                                    \
        }                                                                                             \
        ENUM_ADAPTERS_STACK_ALLOC(uint64_t, _avail_stack, (_ns + 1));                                  \
        ENUM_ADAPTERS_STACK_ALLOC(joker_k_iter, _iters, (_ns));                                        \
        for (int _gi = 0; _gi < _ns; ++_gi)                                                           \
            _iters[_gi].valid = 0;                                                                    \
        _avail_stack[0] = joker_bits_full() & ~joker_mask_to_bits(jdead_mask);                        \
        int _lvl = 0;                                                                                 \
        while (_lvl >= 0)                                                                             \
        {                                                                                             \
            int _k = (set_sizes)[_lvl];                                                               \
            if (_k <= 0)                                                                              \
            {                                                                                         \
                if (_iters[_lvl].valid)                                                               \
                {                                                                                     \
                    /* Already emitted the (empty) combination; backtrack one level. */                 \
                    _iters[_lvl].valid = 0;                                                           \
                    _lvl--;                                                                           \
                    continue;                                                                         \
                }                                                                                     \
                /* First visit: emit empty hand and descend. Use valid as a sentinel so we know */      \
                /* this level is exhausted on the way back up (it only ever yields once). */             \
                _iters[_lvl].valid = 1;                                                               \
                JokerDeck_CardMask_RESET((out_sets_var)[_lvl]);                                       \
                _avail_stack[_lvl + 1] = _avail_stack[_lvl];                                         \
                _lvl++;                                                                               \
                if (_lvl == _ns)                                                                      \
                {                                                                                     \
                    body;                                                                             \
                    _lvl--;                                                                           \
                }                                                                                     \
                continue;                                                                             \
            }                                                                                         \
            if (!_iters[_lvl].valid)                                                                  \
            {                                                                                         \
                joker_kiter_init(&_iters[_lvl], _avail_stack[_lvl], _k);                              \
                if (!_iters[_lvl].valid)                                                              \
                {                                                                                     \
                    _lvl--;                                                                           \
                    continue;                                                                         \
                }                                                                                     \
            }                                                                                         \
            uint64_t _c;                                                                              \
            if (joker_kiter_next(&_iters[_lvl], &_c))                                                 \
            {                                                                                         \
                (out_sets_var)[_lvl] = joker_bits_to_mask(_c);                                        \
                _avail_stack[_lvl + 1] = _avail_stack[_lvl] & ~_c;                                    \
                _lvl++;                                                                               \
                if (_lvl == _ns)                                                                      \
                {                                                                                     \
                    body;                                                                             \
                    _lvl--;                                                                           \
                }                                                                                     \
            }                                                                                         \
            else                                                                                      \
            {                                                                                         \
                _iters[_lvl].valid = 0;                                                               \
                _lvl--;                                                                               \
            }                                                                                         \
        }                                                                                             \
    } while (0)

/* Monte Carlo: JokerDeck draw multiple disjoint hands per iteration */
#define MC_JOKERDECK_DRAW_MULTI_FROM_DEAD(num_sets, set_sizes, jdead_mask, out_sets_var, body) \
    do                                                                                         \
    {                                                                                          \
        uint64_t _base = joker_bits_full() & ~joker_mask_to_bits(jdead_mask);                  \
        int _N = niter;                                                                        \
        for (int _it = 0; _it < _N; ++_it)                                                     \
        {                                                                                      \
            uint64_t _avail = _base;                                                           \
            bool _ok = true;                                                                   \
            for (int _s = 0; _s < (num_sets); ++_s)                                            \
            {                                                                                  \
                int _k = (set_sizes)[_s];                                                      \
                uint64_t _sel = joker_sample_k(_avail, _k);                                    \
                if (_k > 0 && _sel == 0)                                                       \
                {                                                                              \
                    _ok = false;                                                               \
                    break;                                                                     \
                }                                                                              \
                (out_sets_var)[_s] = joker_bits_to_mask(_sel);                                 \
                _avail &= ~_sel;                                                               \
            }                                                                                  \
            if (!_ok)                                                                          \
                continue;                                                                      \
            body;                                                                              \
        }                                                                                      \
    } while (0)

/* Recursive exhaustive enumeration of multiple disjoint StdDeck hands */
static inline void enumerate_multi_stddeck(const int *sizes,
                                           int num_sets,
                                           mask_t avail,
                                           StdDeck_CardMask *out_sets,
                                           int index,
                                           void (*cb)(StdDeck_CardMask *sets, void *user),
                                           void *user)
{
    if (index >= num_sets)
    {
        cb(out_sets, user);
        return;
    }
    int k = sizes[index];
    if (k <= 0)
    {
        /* No cards to select for this set */
        StdDeck_CardMask_RESET(out_sets[index]);
        enumerate_multi_stddeck(sizes, num_sets, avail, out_sets, index + 1, cb, user);
        return;
    }
    combo_generator_t *gen = combo_generator_create_simple(avail, (uint32_t)k);
    if (!gen)
        return;
    mask_t combo;
    while (combo_generator_next(gen, &combo))
    {
        out_sets[index] = mask_t_to_cardmask(combo);
        mask_t next_avail = mask_subtract(avail, combo);
        enumerate_multi_stddeck(sizes, num_sets, next_avail, out_sets, index + 1, cb, user);
    }
    combo_generator_destroy(gen);
}

/* Exhaustive: enumerate multiple disjoint hands from StdDeck − dead (stack-based, no callbacks) */
#define ENUM_STDDECK_ENUMERATE_MULTI_FROM_DEAD(num_sets, set_sizes, dead_mask, out_sets_var, body) \
    do                                                                                             \
    {                                                                                              \
        int _ns = (num_sets);                                                                      \
        if (_ns <= 0)                                                                              \
        {                                                                                          \
            body;                                                                                  \
            break;                                                                                 \
        }                                                                                          \
        ENUM_ADAPTERS_STACK_ALLOC(mask_t, _avail_stack, (_ns + 1));                                   \
        ENUM_ADAPTERS_STACK_ALLOC(combo_generator_t *, _gens, (_ns));                                \
        for (int _gi = 0; _gi < _ns; ++_gi)                                                        \
            _gens[_gi] = NULL;                                                                     \
        _avail_stack[0] = MODERN_FULL_DECK_MASK & ~cardmask_to_mask_t(dead_mask);                  \
        int _lvl = 0;                                                                              \
        while (_lvl >= 0)                                                                          \
        {                                                                                          \
            int _k = (set_sizes)[_lvl];                                                            \
            if (_k <= 0)                                                                           \
            {                                                                                      \
                if (_gens[_lvl] != NULL)                                                           \
                {                                                                                  \
                    /* Already emitted the (empty) combination; backtrack one level. */              \
                    _gens[_lvl] = NULL;                                                            \
                    _lvl--;                                                                        \
                    continue;                                                                      \
                }                                                                                  \
                /* First visit: emit empty hand for this set and descend. Use a sentinel so we      \
                 * know this level is exhausted on the way back up (it only ever yields once). */   \
                _gens[_lvl] = (combo_generator_t *)1;                                              \
                StdDeck_CardMask_RESET((out_sets_var)[_lvl]);                                      \
                _avail_stack[_lvl + 1] = _avail_stack[_lvl];                                       \
                _lvl++;                                                                            \
                if (_lvl == _ns)                                                                   \
                {                                                                                  \
                    body;                                                                          \
                    _lvl--;                                                                        \
                }                                                                                  \
                continue;                                                                          \
            }                                                                                      \
            if (!_gens[_lvl])                                                                      \
            {                                                                                      \
                _gens[_lvl] = combo_generator_create_simple(_avail_stack[_lvl], (uint32_t)_k);     \
                if (!_gens[_lvl])                                                                  \
                {                                                                                  \
                    _lvl--;                                                                        \
                    continue;                                                                      \
                }                                                                                  \
            }                                                                                      \
            mask_t _c;                                                                             \
            if (combo_generator_next(_gens[_lvl], &_c))                                            \
            {                                                                                      \
                (out_sets_var)[_lvl] = mask_t_to_cardmask(_c);                                     \
                _avail_stack[_lvl + 1] = mask_subtract(_avail_stack[_lvl], _c);                    \
                _lvl++;                                                                            \
                if (_lvl == _ns)                                                                   \
                {                                                                                  \
                    body;                                                                          \
                    _lvl--;                                                                        \
                }                                                                                  \
            }                                                                                      \
            else                                                                                   \
            {                                                                                      \
                combo_generator_destroy(_gens[_lvl]);                                              \
                _gens[_lvl] = NULL;                                                                \
                _lvl--;                                                                            \
            }                                                                                      \
        }                                                                                          \
    } while (0)

/*
 * Enumerate k-card combinations from a StdDeck using the modern generator,
 * excluding all cards present in legacy 'dead' CardMask. For each generated
 * combination, assign it to 'shared_var' (StdDeck_CardMask) and execute body.
 *
 * Example usage:
 *   StdDeck_CardMask shared;
 *   ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(5, dead, shared, {
 *       // use 'shared' here
 *   });
 */
#define ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(k, dead_mask, shared_var, body)      \
    do                                                                          \
    {                                                                           \
        mask_t _avail = MODERN_FULL_DECK_MASK & ~cardmask_to_mask_t(dead_mask); \
        combo_generator_t *_gen = combo_generator_create_simple(_avail, (k));   \
        if (_gen)                                                               \
        {                                                                       \
            mask_t _combo;                                                      \
            while (combo_generator_next(_gen, &_combo))                         \
            {                                                                   \
                (shared_var) = mask_t_to_cardmask(_combo);                      \
                body;                                                           \
            }                                                                   \
            combo_generator_destroy(_gen);                                      \
        }                                                                       \
    } while (0)

/*
 * Enumerate two disjoint k1-card and k2-card StdDeck combinations from deck-dead,
 * assigning to shared_var1 and shared_var2, and execute body for each pair.
 * Ensures shared_var1 and shared_var2 do not overlap (disjoint boards).
 */
#define ENUM_STDDECK_ENUMERATE_KxK_FROM_DEAD(k1, k2, dead_mask, shared_var1, shared_var2, body) \
    do                                                                                          \
    {                                                                                           \
        mask_t _avail = MODERN_FULL_DECK_MASK & ~cardmask_to_mask_t(dead_mask);                 \
        combo_generator_t *_g1 = combo_generator_create_simple(_avail, (k1));                   \
        if (_g1)                                                                                \
        {                                                                                       \
            mask_t _c1;                                                                         \
            while (combo_generator_next(_g1, &_c1))                                             \
            {                                                                                   \
                mask_t _avail2 = mask_subtract(_avail, _c1);                                    \
                combo_generator_t *_g2 = combo_generator_create_simple(_avail2, (k2));          \
                if (_g2)                                                                        \
                {                                                                               \
                    mask_t _c2;                                                                 \
                    while (combo_generator_next(_g2, &_c2))                                     \
                    {                                                                           \
                        (shared_var1) = mask_t_to_cardmask(_c1);                                \
                        (shared_var2) = mask_t_to_cardmask(_c2);                                \
                        body;                                                                   \
                    }                                                                           \
                    combo_generator_destroy(_g2);                                               \
                }                                                                               \
            }                                                                                   \
            combo_generator_destroy(_g1);                                                       \
        }                                                                                       \
    } while (0)

/* Monte Carlo: draw multiple disjoint hands (StdDeck) per iteration
 * - num_sets: number of hands (typically npockets)
 * - set_sizes: int[] array of size num_sets
 * - dead_mask: StdDeck_CardMask of cards to exclude
 * - out_sets_var: StdDeck_CardMask[] to receive drawn hands each iteration
 * - Uses variables in scope: pockets (StdDeck_CardMask[]), npockets, board (StdDeck_CardMask)
 */
#define MC_STDDECK_DRAW_MULTI_FROM_DEAD(num_sets, set_sizes, dead_mask, out_sets_var, body) \
    do                                                                                      \
    {                                                                                       \
        mask_t _deadm = cardmask_to_mask_t(dead_mask);                                      \
        mask_t _pocketsm = cardmask_array_union(pockets, npockets);                         \
        mask_t _boardm = cardmask_to_mask_t(board);                                         \
        mask_t _base_avail = MODERN_FULL_DECK_MASK & ~(_deadm | _pocketsm | _boardm);       \
        int _N = niter;                                                                     \
        for (int _it = 0; _it < _N; ++_it)                                                  \
        {                                                                                   \
            mask_t _avail = _base_avail;                                                    \
            bool _ok = true;                                                                \
            for (int _s = 0; _s < (num_sets); ++_s)                                         \
            {                                                                               \
                int _k = (set_sizes)[_s];                                                   \
                mask_t _c = sample_k_from_mask(_avail, _k);                                 \
                if (_k > 0 && _c == 0)                                                      \
                {                                                                           \
                    _ok = false;                                                            \
                    break;                                                                  \
                }                                                                           \
                (out_sets_var)[_s] = mask_t_to_cardmask(_c);                                \
                _avail = mask_subtract(_avail, _c);                                         \
            }                                                                               \
            if (!_ok)                                                                       \
                continue;                                                                   \
            body;                                                                           \
        }                                                                                   \
    } while (0)

/* Monte Carlo: StdDeck draw k cards per iteration from (full - dead) */
#define MC_STDDECK_DRAW_K_FROM_DEAD(k, dead_mask, n_iter, shared_var, body)     \
    do                                                                          \
    {                                                                           \
        mask_t _avail = MODERN_FULL_DECK_MASK & ~cardmask_to_mask_t(dead_mask); \
        int _N = (n_iter);                                                      \
        for (int _it = 0; _it < _N; ++_it)                                      \
        {                                                                       \
            mask_t _c = sample_k_from_mask(_avail, (k));                        \
            if (_c == 0)                                                        \
                continue;                                                       \
            (shared_var) = mask_t_to_cardmask(_c);                              \
            body;                                                               \
        }                                                                       \
    } while (0)

/* Monte Carlo: ShortDeck draw k cards per iteration from (short - dead) */
#define MC_SHORTDECK_DRAW_K_FROM_DEAD(k, dead_mask, n_iter, shared_var, body) \
    do                                                                        \
    {                                                                         \
        mask_t _short = modern_short_deck_mask();                             \
        mask_t _avail = _short & ~cardmask_to_mask_t(dead_mask);              \
        int _N = (n_iter);                                                    \
        for (int _it = 0; _it < _N; ++_it)                                    \
        {                                                                     \
            mask_t _c = sample_k_from_mask(_avail, (k));                      \
            if (_c == 0)                                                      \
                continue;                                                     \
            (shared_var) = mask_t_to_cardmask(_c);                            \
            body;                                                             \
        }                                                                     \
    } while (0)

/* Monte Carlo: draw two disjoint hands (k1 and k2) per iteration from full-deck minus dead */
#define MC_STDDECK_DRAW_KxK_FROM_DEAD(k1, k2, dead_mask, n_iter, shared_var1, shared_var2, body) \
    do                                                                                           \
    {                                                                                            \
        mask_t _avail = MODERN_FULL_DECK_MASK & ~cardmask_to_mask_t(dead_mask);                  \
        int _N = (n_iter);                                                                       \
        for (int _it = 0; _it < _N; ++_it)                                                       \
        {                                                                                        \
            mask_t _c1 = sample_k_from_mask(_avail, (k1));                                       \
            if (_c1 == 0)                                                                        \
                continue;                                                                        \
            mask_t _avail2 = mask_subtract(_avail, _c1);                                         \
            mask_t _c2 = sample_k_from_mask(_avail2, (k2));                                      \
            if (_c2 == 0)                                                                        \
                continue;                                                                        \
            (shared_var1) = mask_t_to_cardmask(_c1);                                             \
            (shared_var2) = mask_t_to_cardmask(_c2);                                             \
            body;                                                                                \
        }                                                                                        \
    } while (0)

/* ShortDeck variants (restrict available to 36-card short deck) */
#define ENUM_SHORTDECK_ENUMERATE_K_FROM_DEAD(k, dead_mask, shared_var, body)  \
    do                                                                        \
    {                                                                         \
        mask_t _short = modern_short_deck_mask();                             \
        mask_t _avail = _short & ~cardmask_to_mask_t(dead_mask);              \
        combo_generator_t *_gen = combo_generator_create_simple(_avail, (k)); \
        if (_gen)                                                             \
        {                                                                     \
            mask_t _combo;                                                    \
            while (combo_generator_next(_gen, &_combo))                       \
            {                                                                 \
                (shared_var) = mask_t_to_cardmask(_combo);                    \
                body;                                                         \
            }                                                                 \
            combo_generator_destroy(_gen);                                    \
        }                                                                     \
    } while (0)

#define ENUM_SHORTDECK_ENUMERATE_KxK_FROM_DEAD(k1, k2, dead_mask, shared_var1, shared_var2, body) \
    do                                                                                            \
    {                                                                                             \
        mask_t _short = modern_short_deck_mask();                                                 \
        mask_t _avail = _short & ~cardmask_to_mask_t(dead_mask);                                  \
        combo_generator_t *_g1 = combo_generator_create_simple(_avail, (k1));                     \
        if (_g1)                                                                                  \
        {                                                                                         \
            mask_t _c1;                                                                           \
            while (combo_generator_next(_g1, &_c1))                                               \
            {                                                                                     \
                mask_t _avail2 = mask_subtract(_avail, _c1);                                      \
                combo_generator_t *_g2 = combo_generator_create_simple(_avail2, (k2));            \
                if (_g2)                                                                          \
                {                                                                                 \
                    mask_t _c2;                                                                   \
                    while (combo_generator_next(_g2, &_c2))                                       \
                    {                                                                             \
                        (shared_var1) = mask_t_to_cardmask(_c1);                                  \
                        (shared_var2) = mask_t_to_cardmask(_c2);                                  \
                        body;                                                                     \
                    }                                                                             \
                    combo_generator_destroy(_g2);                                                 \
                }                                                                                 \
            }                                                                                     \
            combo_generator_destroy(_g1);                                                         \
        }                                                                                         \
    } while (0)

#endif /* ENUMERATION_ADAPTERS_H */
