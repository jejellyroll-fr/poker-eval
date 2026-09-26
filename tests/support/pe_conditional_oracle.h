/*
 * pe_conditional_oracle.h - test-only exact oracle for conditional card
 * removal (issue #259).
 *
 * The question this answers, without going through the solver at all: given
 * one weighted range per player, an observed action whose likelihood is known
 * per combo, a board, and the rule that no two players may share a card, what
 * is the exact posterior distribution of each player's private hand, and the
 * exact probability that each card is still available to a later chance event?
 *
 * Everything here is exact enumeration over the fixture's own combo lists. It
 * deliberately does not call the production sampler, the production deal
 * iterator, or the production blocker code for anything it is used to
 * validate: the value of the oracle is that it is a second, slower,
 * obviously-correct implementation of the same statement. An oracle that
 * reuses the code under test proves only that the code agrees with itself.
 *
 * The posterior it computes is
 *
 *     P(combo c of player p | observed actions)
 *         proportional to   prior_p(c) * action_prob_p(c)
 *
 * normalised over the legal *joint* deals, i.e. after every collision with
 * the board or with another player's hand has been removed. That ordering
 * matters and is itself one of the invariants under test: blockers are
 * applied before the final normalisation, so a combo that blocks a large part
 * of the other players' ranges carries proportionally more posterior mass.
 *
 * The header is header-only and every function is static, so a test can
 * include it without touching the CMake target list, which builds exactly one
 * source file per test.
 */

#ifndef PE_CONDITIONAL_ORACLE_H
#define PE_CONDITIONAL_ORACLE_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <poker_eval/core/modern_cardmask.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PE_ORACLE_MAX_PLAYERS 8u
#define PE_ORACLE_DECK MODERN_DECK_SIZE

/* One private combo, its prior weight, and the probability that the player
   holding it takes the action that was actually observed. */
typedef struct
{
    mask_t cards;
    double prior;
    double action_prob;
} pe_oracle_combo_t;

typedef struct
{
    const pe_oracle_combo_t *combos;
    size_t count;
} pe_oracle_range_t;

typedef struct
{
    const pe_oracle_range_t *ranges;
    uint8_t player_count;
    /* Board and any other known dead cards. Production deal code takes the
       board as its only dead set, so the oracle does too. */
    mask_t board;
} pe_oracle_game_t;

typedef struct
{
    /* Exact legal joint mass, unnormalised: the sum of the products of
       posterior combo weights over every legal joint deal. */
    double legal_mass;
    /* Joint deals whose weight is strictly positive. */
    size_t legal_deals;

    uint8_t player_count;
    size_t combo_capacity;
    /* Board and dead cards the run was made with, kept for the availability
       derivation and for callers that assert against it. */
    mask_t board;
    /* [player * combo_capacity + combo], normalised: the posterior marginal
       of that combo. Entries past a player's combo count stay zero. */
    double *combo_marginal;

    /* Normalised, per card: held by at least one player, and in no hand and
       off the board (so still drawable by a later chance event). */
    double card_held[PE_ORACLE_DECK];
    double card_available[PE_ORACLE_DECK];
} pe_oracle_result_t;

/* The posterior weight of a combo before joint normalisation: the prior
   weight times the probability that the observed action was taken. */
static double pe_oracle_combo_weight(const pe_oracle_combo_t *combo)
{
    return combo->prior * combo->action_prob;
}

/* ---------------------------------------------------------------- *
 * Exact enumeration
 * ---------------------------------------------------------------- */

typedef struct
{
    const pe_oracle_game_t *game;
    pe_oracle_result_t *out;
    mask_t hands;
    size_t holes[PE_ORACLE_MAX_PLAYERS];
} pe_oracle_walk_t;

static void pe_oracle_visit(pe_oracle_walk_t *walk, uint8_t player,
                            double weight)
{
    const pe_oracle_game_t *game = walk->game;
    size_t i;

    if (player == game->player_count)
    {
        walk->out->legal_mass += weight;
        walk->out->legal_deals++;
        for (uint8_t p = 0u; p < game->player_count; ++p)
            walk->out->combo_marginal[(size_t)p * walk->out->combo_capacity +
                                      walk->holes[p]] += weight;
        for (int card = 0; card < PE_ORACLE_DECK; ++card)
            if (mask_is_set(walk->hands, card))
                walk->out->card_held[card] += weight;
        return;
    }

    for (i = 0u; i < game->ranges[player].count; ++i)
    {
        const pe_oracle_combo_t *combo = &game->ranges[player].combos[i];
        double combo_weight = pe_oracle_combo_weight(combo);
        mask_t saved;

        if (!mask_is_valid(combo->cards) || mask_is_empty(combo->cards) ||
            !(combo_weight > 0.0) || !isfinite(combo_weight) ||
            mask_intersects(combo->cards, game->board) ||
            mask_intersects(combo->cards, walk->hands))
            continue;

        saved = walk->hands;
        walk->hands |= combo->cards;
        walk->holes[player] = i;
        pe_oracle_visit(walk, (uint8_t)(player + 1u), weight * combo_weight);
        walk->hands = saved;
    }
}

static void pe_oracle_result_free(pe_oracle_result_t *result)
{
    if (result == NULL)
        return;
    free(result->combo_marginal);
    memset(result, 0, sizeof(*result));
}

/* Normalise the accumulated mass into probabilities. A fixture with no legal
   joint deal is a broken fixture, not a probability distribution, and is
   reported as an error by pe_oracle_run. */
static void pe_oracle_result_normalise(pe_oracle_result_t *result)
{
    double total = result->legal_mass;
    size_t cells = result->combo_capacity * (size_t)result->player_count;
    size_t i;
    int card;

    if (!(total > 0.0))
        return;

    for (i = 0u; i < cells; ++i)
        result->combo_marginal[i] /= total;
    for (card = 0; card < PE_ORACLE_DECK; ++card)
        result->card_held[card] /= total;
    for (card = 0; card < PE_ORACLE_DECK; ++card)
        result->card_available[card] =
            mask_is_set(result->board, card)
                ? 0.0
                : 1.0 - result->card_held[card];
}

/*
 * Enumerate every legal joint deal exactly.
 *
 * `out` must be zeroed by the caller; its combo_marginal buffer is allocated
 * here and must be released with pe_oracle_result_free.
 *
 * Returns 0, or -1 on a malformed fixture (no player, empty range, invalid
 * combo width, or no legal joint deal at all).
 */
static int pe_oracle_run(const pe_oracle_game_t *game,
                         pe_oracle_result_t *out)
{
    pe_oracle_walk_t walk;
    size_t capacity = 0u;
    size_t cells;
    uint8_t player;

    if (game == NULL || out == NULL || game->ranges == NULL ||
        game->player_count == 0u ||
        game->player_count > PE_ORACLE_MAX_PLAYERS ||
        !mask_is_valid(game->board))
        return -1;

    for (player = 0u; player < game->player_count; ++player)
    {
        const pe_oracle_range_t *range = &game->ranges[player];
        if (range->count == 0u || range->combos == NULL)
            return -1;
        if (range->count > capacity)
            capacity = range->count;
    }
    if (capacity == 0u)
        return -1;

    memset(out, 0, sizeof(*out));
    out->player_count = game->player_count;
    out->combo_capacity = capacity;
    out->board = game->board;

    cells = capacity * (size_t)game->player_count;
    out->combo_marginal = (double *)calloc(cells, sizeof(double));
    if (out->combo_marginal == NULL)
        return -1;

    memset(&walk, 0, sizeof(walk));
    walk.game = game;
    walk.out = out;
    walk.hands = MASK_EMPTY;
    pe_oracle_visit(&walk, 0u, 1.0);

    if (!(out->legal_mass > 0.0) || out->legal_deals == 0u)
    {
        pe_oracle_result_free(out);
        return -1;
    }

    pe_oracle_result_normalise(out);
    return 0;
}

/* ---------------------------------------------------------------- *
 * Queries
 * ---------------------------------------------------------------- */

static double pe_oracle_combo_prob(const pe_oracle_result_t *result,
                                   uint8_t player, size_t combo)
{
    if (result == NULL || result->combo_marginal == NULL ||
        player >= result->player_count || combo >= result->combo_capacity)
        return 0.0;
    return result->combo_marginal[(size_t)player * result->combo_capacity +
                                  combo];
}

/* P(player p's private hand contains `card`). */
static double pe_oracle_player_card_prob(const pe_oracle_game_t *game,
                                         const pe_oracle_result_t *result,
                                         uint8_t player, int card)
{
    double total = 0.0;
    size_t i;

    if (game == NULL || result == NULL || player >= game->player_count)
        return 0.0;
    for (i = 0u; i < game->ranges[player].count; ++i)
        if (mask_is_set(game->ranges[player].combos[i].cards, card))
            total += pe_oracle_combo_prob(result, player, i);
    return total;
}

/* The posterior marginal of one player, with every other player summed out.
   The entries must sum to 1. */
static double pe_oracle_marginal_sum(const pe_oracle_result_t *result,
                                     uint8_t player)
{
    double total = 0.0;
    size_t i;
    if (result == NULL || result->combo_marginal == NULL ||
        player >= result->player_count)
        return 0.0;
    for (i = 0u; i < result->combo_capacity; ++i)
        total += result->combo_marginal[(size_t)player *
                                            result->combo_capacity + i];
    return total;
}

/* ---------------------------------------------------------------- *
 * Statistical tolerance
 * ---------------------------------------------------------------- */

/*
 * A z-sigma binomial tolerance for a Monte Carlo estimate of `p` made from
 * `n_eff` effectively-independent draws. The caller is expected to check that
 * n_eff is a healthy fraction of the raw sample count; a tolerance computed
 * from a collapsed effective sample size would be meaningless. The absolute
 * floor only absorbs floating-point noise, not sampling error.
 */
static double pe_oracle_tolerance(double p, double n_eff, double z)
{
    double se;
    if (!(n_eff > 1.0) || !isfinite(n_eff))
        return 1.0;
    se = (p > 0.0 && p < 1.0) ? sqrt(p * (1.0 - p) / n_eff) : 0.0;
    return z * se + 1e-6;
}

#ifdef __cplusplus
}
#endif

#endif /* PE_CONDITIONAL_ORACLE_H */
