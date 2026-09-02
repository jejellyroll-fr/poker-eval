/*
 * blockers.c - Card removal at terminal nodes (RNG-05)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * The inclusion-exclusion is short enough to state in full for two-card hands.
 * Let T be the total opponent reach, S[c] the reach of the opponent combos
 * using card c, and P{a,b} the reach of the single opponent combo that is
 * exactly {a, b}. A hero hand {a, b} blocks S[a] + S[b], except that P{a,b}
 * was counted in both, so:
 *
 *     compatible({a,b}) = T - S[a] - S[b] + P{a,b}
 *
 * Everything is summed with the same compensation as pe_vec_sum, because T is
 * a sum over the whole opponent range and the subtraction below it is where
 * cancellation bites: T and S[a] + S[b] are close whenever a card is common,
 * so a relative error in T becomes a much larger one in the difference.
 */

#include <poker_eval/solver/pe_blockers.h>

#include <math.h>
#include <stddef.h>
#include <string.h>

#define PE_DECK 52

/* Neumaier accumulation, same reasoning as pe_vec_sum. */
typedef struct { double sum; double c; } pe_acc_t;

static void acc_add(pe_acc_t *a, double x)
{
    double t = a->sum + x;
    if (fabs(a->sum) >= fabs(x))
        a->c += (a->sum - t) + x;
    else
        a->c += (x - t) + a->sum;
    a->sum = t;
}

static double acc_value(const pe_acc_t *a)
{
    return a->sum + a->c;
}

static int mask_card_count(mask_t m, int *out_cards, int max)
{
    int n = 0;
    for (int c = 0; c < PE_DECK; ++c)
    {
        if (!mask_is_set(m, c))
            continue;
        if (n < max)
            out_cards[n] = c;
        n++;
    }
    return n;
}

pe_solver_status_t pe_blockers_compatible_sum_pairwise(const mask_t *hero_masks,
                                                       size_t hero_n,
                                                       const mask_t *opp_masks,
                                                       const double *opp_reach,
                                                       size_t opp_n,
                                                       mask_t dead,
                                                       double *out)
{
    size_t i;
    size_t j;

    if (!hero_masks || !opp_masks || !opp_reach || !out)
        return PE_SOLVER_ERR_NULL_ARGUMENT;
    if (hero_n == 0 || opp_n == 0)
        return PE_SOLVER_ERR_INVALID_CONFIG;

    for (i = 0; i < hero_n; ++i)
    {
        pe_acc_t acc = { 0.0, 0.0 };

        if ((hero_masks[i] & dead) != 0)
        {
            out[i] = 0.0;
            continue;
        }
        for (j = 0; j < opp_n; ++j)
        {
            if ((opp_masks[j] & dead) != 0)
                continue;
            if ((opp_masks[j] & hero_masks[i]) != 0)
                continue;
            acc_add(&acc, opp_reach[j]);
        }
        out[i] = acc_value(&acc);
    }
    return PE_SOLVER_OK;
}

pe_solver_status_t pe_blockers_compatible_sum(const mask_t *hero_masks,
                                              size_t hero_n,
                                              const mask_t *opp_masks,
                                              const double *opp_reach,
                                              size_t opp_n,
                                              mask_t dead,
                                              double *out,
                                              pe_blockers_path_t *out_path)
{
    pe_acc_t total = { 0.0, 0.0 };
    pe_acc_t card[PE_DECK];
    /* Reach of the opponent combo that is exactly {a, b}, for the one double
       count two-card inclusion-exclusion produces. 21 KiB, and rebuilt per
       call: a terminal node is reached once per traversal, not per combo. */
    static double pair[PE_DECK][PE_DECK];
    int two_card_only = 1;
    size_t i;
    size_t j;

    if (!hero_masks || !opp_masks || !opp_reach || !out)
        return PE_SOLVER_ERR_NULL_ARGUMENT;
    if (hero_n == 0 || opp_n == 0)
        return PE_SOLVER_ERR_INVALID_CONFIG;

    /* The fast path is exact for two-card hands only. Anything wider needs
       three- and four-way intersection terms that are not built here, so it
       takes the pairwise reference rather than a correction that is nearly
       right — a blocker sum that is close produces a solve that is quietly
       wrong. */
    for (i = 0; i < hero_n && two_card_only; ++i)
    {
        int cards[3];
        if (mask_card_count(hero_masks[i], cards, 3) != 2)
            two_card_only = 0;
    }
    for (j = 0; j < opp_n && two_card_only; ++j)
    {
        int cards[3];
        if (mask_card_count(opp_masks[j], cards, 3) != 2)
            two_card_only = 0;
    }

    if (!two_card_only)
    {
        if (out_path)
            *out_path = PE_BLOCKERS_PATH_PAIRWISE;
        return pe_blockers_compatible_sum_pairwise(hero_masks, hero_n, opp_masks,
                                                   opp_reach, opp_n, dead, out);
    }

    memset(card, 0, sizeof(card));
    memset(pair, 0, sizeof(pair));

    for (j = 0; j < opp_n; ++j)
    {
        int cards[2];
        if ((opp_masks[j] & dead) != 0)
            continue;
        if (mask_card_count(opp_masks[j], cards, 2) != 2)
            continue;
        acc_add(&total, opp_reach[j]);
        acc_add(&card[cards[0]], opp_reach[j]);
        acc_add(&card[cards[1]], opp_reach[j]);
        /* A range may list the same hand twice only if it was not prepared;
           adding rather than assigning keeps this correct either way. */
        pair[cards[0]][cards[1]] += opp_reach[j];
        pair[cards[1]][cards[0]] += opp_reach[j];
    }

    for (i = 0; i < hero_n; ++i)
    {
        int cards[2];
        pe_acc_t acc = { 0.0, 0.0 };

        if ((hero_masks[i] & dead) != 0)
        {
            out[i] = 0.0;
            continue;
        }
        mask_card_count(hero_masks[i], cards, 2);

        acc_add(&acc, acc_value(&total));
        acc_add(&acc, -acc_value(&card[cards[0]]));
        acc_add(&acc, -acc_value(&card[cards[1]]));
        acc_add(&acc, pair[cards[0]][cards[1]]);

        /* The result is a sum of probabilities and cannot be negative; a tiny
           negative is cancellation noise, and letting it through would put a
           negative reach into the traversal. */
        out[i] = (acc_value(&acc) > 0.0) ? acc_value(&acc) : 0.0;
    }

    if (out_path)
        *out_path = PE_BLOCKERS_PATH_ACCUMULATED;
    return PE_SOLVER_OK;
}

pe_solver_status_t pe_blockers_fold_vector(const mask_t *hero_masks,
                                           size_t hero_n,
                                           const mask_t *opp_masks,
                                           const double *opp_reach,
                                           size_t opp_n,
                                           mask_t dead,
                                           double pot,
                                           pe_value_vec_t *out_values,
                                           pe_blockers_path_t *out_path)
{
    pe_solver_status_t status;
    size_t i;

    if (!out_values)
        return PE_SOLVER_ERR_NULL_ARGUMENT;
    if (hero_n == 0 || out_values->n != hero_n || !out_values->v ||
        pot < 0.0 || isnan(pot))
        return PE_SOLVER_ERR_INVALID_CONFIG;

    status = pe_blockers_compatible_sum(hero_masks, hero_n, opp_masks,
                                        opp_reach, opp_n, dead,
                                        out_values->v, out_path);
    if (status != PE_SOLVER_OK)
        return status;

    for (i = 0; i < hero_n; ++i)
        out_values->v[i] *= pot;
    return PE_SOLVER_OK;
}
