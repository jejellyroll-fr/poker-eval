/*
 * Generic Draw Decision & Equity Optimizer
 *
 * See include/poker_eval/equity/draw_optimizer.h for the mathematical model.
 *
 * This program gives you software freedom; you can copy, convey,
 * propagate, redistribute and/or modify this program under the terms of
 * the GNU General Public License (GPL) as published by the Free Software
 * Foundation (FSF), either version 3 of the License, or (at your option)
 * any later version of the GPL published by the FSF.
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <poker_eval/core/eval.h>
#include <poker_eval/core/handval.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/games/eval_low.h>
#include <poker_eval/games/eval_low27.h>
#include <poker_eval/games/eval_omaha.h>
#include <poker_eval/games/badugi_eval.h>

#include <poker_eval/equity/draw_optimizer.h>

/* Value spaces: each maps a resulting 5-card hand to a comparable integer value
 * whose distribution we precompute so any hand can be turned into a [0,1]
 * strength (the probability it beats a uniformly random hand of the same kind).
 * "higher_better" records whether a larger integer value is the stronger hand. */
typedef enum {
    VS_HIGH,     /* standard 5-card high (StdRules)        */
    VS_LOW27,    /* 2-7 lowball (Lowball27)               */
    VS_LOWA5,    /* A-5 lowball (Lowball, 8-or-better opt) */
    VS_BADUGI,   /* Badugi EVAL_5                         */
    VS_BADACEY,  /* Badacey EVAL_5                        */
    VS_BADEUCY,  /* Badeucy EVAL_5                        */
    VS_DRAWMAHA, /* 0.5 * high + 0.5 * Omaha hi           */
    VS_COUNT
} pe_value_space_t;

typedef struct {
    uint32 *values;     /* sorted unique hand values                */
    uint64 *counts;     /* frequency of each unique value           */
    uint64 *cum_below;  /* prefix sum of counts strictly below idx  */
    uint64 total;       /* total number of hands sampled            */
    int n;              /* number of unique values                  */
    int cap;            /* allocated capacity                      */
    int higher_better;  /* 1 if larger value == stronger hand       */
    int built;          /* 0 until first build completes           */
} pe_strength_table_t;

static pe_strength_table_t g_tables[VS_COUNT];

/* ---- value evaluation per space ---------------------------------------- */

static uint32 pe_eval_space(pe_value_space_t s, StdDeck_CardMask hand)
{
    switch (s) {
    case VS_HIGH:
        return (uint32)StdDeck_StdRules_EVAL_N(hand, 5);
    case VS_LOW27:
        return (uint32)StdDeck_Lowball27_EVAL_N(hand, 5);
    case VS_LOWA5:
        return (uint32)StdDeck_Lowball_EVAL(hand, 5);
    case VS_BADUGI:
        return (uint32)StdDeck_BadugiRules_EVAL_5(hand);
    case VS_BADACEY:
        return (uint32)StdDeck_BadaceyRules_EVAL_5(hand);
    case VS_BADEUCY:
        return (uint32)StdDeck_BadeucyRules_EVAL_5(hand);
    case VS_DRAWMAHA:
    case VS_COUNT:
        return 0;
    default:
        return 0;
    }
}

/* ---- strength table construction --------------------------------------- */

static void table_insert(pe_strength_table_t *t, uint32 v)
{
    int lo = 0, hi = t->n;
    while (lo < hi) {
        int mid = (lo + hi) >> 1;
        if (t->values[mid] == v) {
            t->counts[mid]++;
            return;
        } else if (t->values[mid] < v) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (t->n >= t->cap) {
        int newcap = t->cap ? t->cap * 2 : 1024;
        t->values = (uint32 *)realloc(t->values, (size_t)newcap * sizeof(uint32));
        t->counts = (uint64 *)realloc(t->counts, (size_t)newcap * sizeof(uint64));
        t->cum_below = (uint64 *)realloc(t->cum_below, (size_t)newcap * sizeof(uint64));
        t->cap = newcap;
    }
    if (lo < t->n) {
        memmove(&t->values[lo + 1], &t->values[lo],
                (size_t)(t->n - lo) * sizeof(uint32));
        memmove(&t->counts[lo + 1], &t->counts[lo],
                (size_t)(t->n - lo) * sizeof(uint64));
    }
    t->values[lo] = v;
    t->counts[lo] = 1;
    t->n++;
}

static void table_build(pe_value_space_t s)
{
    pe_strength_table_t *t = &g_tables[s];
    t->higher_better = (s == VS_HIGH || s == VS_BADUGI ||
                        s == VS_BADACEY || s == VS_BADEUCY);

    /* Enumerate every 5-card hand (C(52,5) = 2,598,960) and record its value. */
    for (int i = 0; i < StdDeck_N_CARDS; i++) {
        for (int j = i + 1; j < StdDeck_N_CARDS; j++) {
            for (int k = j + 1; k < StdDeck_N_CARDS; k++) {
                for (int l = k + 1; l < StdDeck_N_CARDS; l++) {
                    for (int m = l + 1; m < StdDeck_N_CARDS; m++) {
                        StdDeck_CardMask hand;
                        StdDeck_CardMask_RESET(hand);
                        StdDeck_CardMask_SET(hand, i);
                        StdDeck_CardMask_SET(hand, j);
                        StdDeck_CardMask_SET(hand, k);
                        StdDeck_CardMask_SET(hand, l);
                        StdDeck_CardMask_SET(hand, m);
                        table_insert(t, pe_eval_space(s, hand));
                    }
                }
            }
        }
    }

    t->cum_below[0] = 0;
    for (int i = 1; i < t->n; i++)
        t->cum_below[i] = t->cum_below[i - 1] + t->counts[i - 1];
    t->total = (t->n > 0) ? t->cum_below[t->n - 1] + t->counts[t->n - 1] : 0;
    t->built = 1;
}

static pe_strength_table_t *table_get(pe_value_space_t s)
{
    /* Guard the array access: game_to_space produces VS_COUNT for unsupported
     * games, so an unchecked g_tables[s] indexing is out of bounds (the caller
     * rejects such games before reaching here, but GCC's value-range analysis
     * of the inlined path still flags -- and rightly so -- the unchecked
     * subscript). */
    if ((int)s < 0 || (int)s >= VS_COUNT)
        return NULL;
    pe_strength_table_t *t = &g_tables[s];
    if (!t->built)
        table_build(s);
    return t;
}

/* Strength of a hand value: probability it beats a uniformly random hand. */
static double pe_strength(pe_value_space_t s, uint32 value)
{
    pe_strength_table_t *t = table_get(s);
    if (t == NULL || t->n == 0 || t->total == 0)
        return 0.0;

    int lo = 0, hi = t->n - 1, idx = -1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if (t->values[mid] == value) {
            idx = mid;
            break;
        } else if (t->values[mid] < value) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    if (idx < 0)
        idx = (hi < 0) ? 0 : hi; /* largest value <= given */

    uint64 below = t->cum_below[idx];
    uint64 cnt = t->counts[idx];

    if (t->higher_better)
        return (double)(below + cnt / 2) / (double)t->total +
               (double)(cnt % 2) / (2.0 * (double)t->total);
    return (double)(t->total - below - cnt + cnt / 2) / (double)t->total +
           (double)(cnt % 2) / (2.0 * (double)t->total);
}

/* ---- combination helpers ----------------------------------------------- */

static int next_combination(int *c, int k, int n)
{
    int i = k - 1;
    while (i >= 0 && c[i] == n - k + i)
        i--;
    if (i < 0)
        return 0;
    c[i]++;
    for (int j = i + 1; j < k; j++)
        c[j] = c[j - 1] + 1;
    return 1;
}

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    if (x < y) return -1;
    if (x > y) return 1;
    return 0;
}

/* Fraction of the sorted opponent Omaha values strictly below `val`
 * (counting ties as half). opp must be sorted ascending and have nopp > 0. */
static double omaha_frac(double val, const double *opp, int nopp)
{
    int lo = 0, hi = nopp - 1, idx = -1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if ((uint32)opp[mid] == (uint32)val) {
            idx = mid;
            break;
        } else if (opp[mid] < val) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    if (idx < 0)
        idx = (hi < 0) ? -1 : hi;

    int64_t less = (int64_t)(idx + 1); /* number of opponents strictly below */
    int equal = 0;
    if (idx >= 0) {
        /* count ties at idx */
        int j = idx;
        while (j >= 0 && (uint32)opp[j] == (uint32)val) { equal++; j--; }
        j = idx + 1;
        while (j < nopp && (uint32)opp[j] == (uint32)val) { equal++; j++; }
    }
    return ((double)less + 0.5 * (double)equal) / (double)nopp;
}

/* Unseen-card pool: deck minus hand, board and dead cards. Returns the number
 * of cards written (ascending card order) into pool[]. */
static int unseen_pool(StdDeck_CardMask hand, StdDeck_CardMask board,
                       StdDeck_CardMask dead_cards, int *pool)
{
    StdDeck_CardMask used, avail;
    StdDeck_CardMask_RESET(used);
    StdDeck_CardMask_OR(used, used, hand);
    StdDeck_CardMask_OR(used, used, board);
    StdDeck_CardMask_OR(used, used, dead_cards);
    StdDeck_CardMask_NOT(avail, used);

    int npool = 0;
    for (int c = 0; c < StdDeck_N_CARDS; c++)
        if (StdDeck_CardMask_CARD_IS_SET(avail, c))
            pool[npool++] = c;
    return npool;
}

/* Sorted Omaha hi values of every possible opponent 2-card pocket built from
 * the unseen pool (the board must have at least 3 cards for this to make
 * sense). Pockets whose evaluation fails are skipped rather than sentinel
 * marked: every stored value is a non-negative integer-valued double, which
 * keeps the (uint32) casts in omaha_frac() well-defined. On success stores a
 * malloc'd array and its size; the caller frees it. */
static void build_omaha_opponents(const int *pool, int npool,
                                  StdDeck_CardMask board,
                                  double **opp_out, int *nopp_out)
{
    *opp_out = NULL;
    *nopp_out = 0;
    if (npool < 2)
        return;

    int max_opp = npool * (npool - 1) / 2;
    double *opp = (double *)malloc((size_t)max_opp * sizeof(double));
    if (opp == NULL)
        return;

    int oi = 0;
    for (int i = 0; i < npool; i++) {
        for (int j = i + 1; j < npool; j++) {
            StdDeck_CardMask oh;
            StdDeck_CardMask_RESET(oh);
            StdDeck_CardMask_SET(oh, pool[i]);
            StdDeck_CardMask_SET(oh, pool[j]);
            HandVal ov;
            if (StdDeck_OmahaHi_EVAL(oh, board, &ov) == 0)
                opp[oi++] = (double)(uint32)ov;
        }
    }

    if (oi == 0) {
        free(opp);
        return;
    }
    qsort(opp, (size_t)oi, sizeof(double), cmp_double);
    *opp_out = opp;
    *nopp_out = oi;
}

/* Value of a single resulting hand for the given space. For Drawmaha the value
 * is the 0.5 * high_strength + 0.5 * Omaha_strength combination; opp/nopp are
 * the sorted Omaha values of every possible opponent 2-card pocket (NULL when
 * the board is too small for Omaha, in which case only the high half counts). */
static double pe_hand_value(pe_value_space_t s, StdDeck_CardMask hand,
                            StdDeck_CardMask board, const double *opp, int nopp)
{
    if (s == VS_DRAWMAHA) {
        double high_s = pe_strength(VS_HIGH,
                                    (uint32)StdDeck_StdRules_EVAL_N(hand, 5));
        double omaha_s = 0.5;
        if (opp != NULL && nopp > 0) {
            HandVal ov;
            if (StdDeck_OmahaHi_EVAL(hand, board, &ov) == 0)
                omaha_s = omaha_frac((double)(uint32)ov, opp, nopp);
        }
        return 0.5 * high_s + 0.5 * omaha_s;
    }
    return pe_strength(s, pe_eval_space(s, hand));
}

/* ---- game -> value space mapping --------------------------------------- */

static int game_to_space(enum_game_t game, pe_value_space_t *out)
{
    /* Map each supported draw game to its value space. Any other game is
     * unsupported; the caller treats a non-zero return as an error. */
    if (game == game_5draw || game == game_5drawnsq) {
        *out = VS_HIGH;
    } else if (game == game_5draw8 || game == game_lowball ||
               game == game_a5_triple_draw) {
        *out = VS_LOWA5;
    } else if (game == game_lowball27 || game == game_27_triple_draw) {
        *out = VS_LOW27;
    } else if (game == game_badugi) {
        *out = VS_BADUGI;
    } else if (game == game_badacey) {
        *out = VS_BADACEY;
    } else if (game == game_badeucy) {
        *out = VS_BADEUCY;
    } else if (game == game_drawmaha) {
        *out = VS_DRAWMAHA;
    } else {
        *out = VS_COUNT;
        return 1;
    }
    return 0;
}

/* ---- public API -------------------------------------------------------- */

int pe_draw_hand_strength(enum_game_t game, StdDeck_CardMask hand,
                          StdDeck_CardMask board, StdDeck_CardMask dead_cards,
                          double *out_strength)
{
    if (out_strength == NULL)
        return 1;
    if (StdDeck_numCards(hand) != 5)
        return 1;

    pe_value_space_t s;
    if (game_to_space(game, &s) != 0)
        return 1;

    double *opp = NULL;
    int nopp = 0;

    if (s == VS_DRAWMAHA && StdDeck_numCards(board) >= 3) {
        int pool[StdDeck_N_CARDS];
        int npool = unseen_pool(hand, board, dead_cards, pool);
        build_omaha_opponents(pool, npool, board, &opp, &nopp);
    }

    *out_strength = pe_hand_value(s, hand, board, opp, nopp);
    free(opp);
    return 0;
}

/* ---- value-function based enumeration ---------------------------------- */

/* Context glue for the built-in per-game value function: lets the shared
 * enumeration below call pe_hand_value() through the generic callback. */
typedef struct {
    pe_value_space_t s;
    StdDeck_CardMask board;
    const double *opp;
    int nopp;
} pe_space_value_ctx_t;

static double pe_space_value(StdDeck_CardMask hand, void *vctx)
{
    pe_space_value_ctx_t *c = (pe_space_value_ctx_t *)vctx;
    return pe_hand_value(c->s, hand, c->board, c->opp, c->nopp);
}

/* Shared 32-mask expected-value enumeration. `value_fn` maps a resulting
 * 5-card hand to a comparable number (higher is better); the expected value
 * of each discard mask is the mean of value_fn over all replacement sets
 * drawn from the unseen pool (52 cards minus hand, board and dead). */
static int draw_optima_enum(StdDeck_CardMask hand, StdDeck_CardMask board,
                            StdDeck_CardMask dead_cards,
                            pe_draw_value_fn value_fn, void *ctx,
                            pe_draw_result_t *out_result)
{
    if (out_result == NULL || value_fn == NULL)
        return 1;
    if (StdDeck_numCards(hand) != 5)
        return 1;

    /* Unseen cards = deck minus hand, board and dead. */
    int pool[StdDeck_N_CARDS];
    int npool = unseen_pool(hand, board, dead_cards, pool);

    /* Ordered hand cards (ascending card index) so discard bit i refers to the
     * i-th card of the hand, matching the Drawmaha mask convention. */
    int hc[5];
    int nh = 0;
    for (int c = 0; c < StdDeck_N_CARDS; c++)
        if (StdDeck_CardMask_CARD_IS_SET(hand, c))
            hc[nh++] = c;

    for (int mask = 0; mask < 32; mask++) {
        int k = 0;
        for (int b = 0; b < 5; b++)
            if (mask & (1 << b))
                k++;

        out_result->options[mask].discard_mask = mask;
        out_result->options[mask].cards_drawn = k;

        if (k > 5 || k > npool) {
            out_result->options[mask].expected_equity = 0.0;
            continue;
        }

        /* Build the kept subset by removing discarded card positions. */
        StdDeck_CardMask kept = hand;
        for (int b = 0; b < 5; b++)
            if (mask & (1 << b))
                StdDeck_CardMask_UNSET(kept, hc[b]);

        double sum = 0.0;
        uint64_t ncombos = 0;

        if (k == 0) {
            sum = value_fn(kept, ctx);
            ncombos = 1;
        } else {
            int combo[5];
            for (int b = 0; b < k; b++)
                combo[b] = b;
            do {
                StdDeck_CardMask drawn;
                StdDeck_CardMask_RESET(drawn);
                for (int b = 0; b < k; b++)
                    StdDeck_CardMask_SET(drawn, pool[combo[b]]);
                StdDeck_CardMask res;
                StdDeck_CardMask_OR(res, kept, drawn);
                sum += value_fn(res, ctx);
                ncombos++;
            } while (next_combination(combo, k, npool));
        }

        out_result->options[mask].expected_equity =
            (ncombos > 0) ? (sum / (double)ncombos) : 0.0;
    }

    out_result->num_options = 32;
    int best = 0;
    for (int m = 1; m < 32; m++)
        if (out_result->options[m].expected_equity >
            out_result->options[best].expected_equity)
            best = m;
    out_result->optimal_mask = best;
    out_result->max_equity = out_result->options[best].expected_equity;

    return 0;
}

int pe_compute_draw_optima(enum_game_t game, StdDeck_CardMask hand,
                           StdDeck_CardMask board, StdDeck_CardMask dead_cards,
                           pe_draw_result_t *out_result)
{
    if (out_result == NULL)
        return 1;
    if (StdDeck_numCards(hand) != 5)
        return 1;

    pe_value_space_t s;
    if (game_to_space(game, &s) != 0)
        return 1;

    /* For Drawmaha with a sufficient board, precompute the Omaha strength of
     * every possible opponent 2-card pocket once. */
    double *opp = NULL;
    int nopp = 0;
    if (s == VS_DRAWMAHA && StdDeck_numCards(board) >= 3) {
        int pool[StdDeck_N_CARDS];
        int npool = unseen_pool(hand, board, dead_cards, pool);
        build_omaha_opponents(pool, npool, board, &opp, &nopp);
    }

    pe_space_value_ctx_t vctx = { s, board, opp, nopp };
    int rc = draw_optima_enum(hand, board, dead_cards, pe_space_value, &vctx,
                              out_result);

    free(opp);
    return rc;
}

int pe_compute_draw_optima_fn(StdDeck_CardMask hand, StdDeck_CardMask board,
                              StdDeck_CardMask dead_cards,
                              pe_draw_value_fn value_fn, void *ctx,
                              pe_draw_result_t *out_result)
{
    return draw_optima_enum(hand, board, dead_cards, value_fn, ctx,
                            out_result);
}
