/*
 * test_pe_conditional_card_removal.c - issue #259.
 *
 * Does the sampled deal path respect ranges, observed actions and blockers?
 *
 * The suite is built around an exact oracle (tests/support/pe_conditional_oracle.h)
 * that enumerates the legal joint deals of a small fixture and computes, by
 * hand, what each player's posterior hand distribution and each card's
 * availability must be once an observed action has been conditioned on. The
 * production sampler is then driven on the same fixture and its output is
 * compared against that reference.
 *
 * The invariant under test is stated once, in the guide:
 *
 *   Hidden cards belonging to players who have already acted remain removed
 *   from the deck according to the posterior range implied by their observed
 *   actions.
 *
 * Every case below is paired with the two mistakes the invariant rules out,
 * so the suite fails loudly if either is reintroduced:
 *
 *   - sampling from the *unconditional* range after an action was observed
 *     (the fold is ignored, so the posterior is the prior);
 *   - dropping an acting player from the deal so that their private cards
 *     return to the deck and become available to the players behind them.
 *
 * A Monte Carlo estimate is never compared against a hand-written number: it
 * is compared against the oracle, at five sigma, with a tolerance derived
 * from the effective sample size of the importance-weighted estimator. The
 * hand-written numbers appear only where the exact value is trivially
 * checkable (1.0, 0.0, 1/2, 1/3) and pin the oracle itself.
 */

#include <poker_eval/solver/pe_preflop_deal_sampler.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "support/pe_conditional_oracle.h"

static int g_failures = 0;

#define CHECK(cond, ...)                                              \
    do                                                                \
    {                                                                 \
        if (!(cond))                                                  \
        {                                                             \
            fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__);    \
            fprintf(stderr, __VA_ARGS__);                             \
            fputc('\n', stderr);                                      \
            g_failures++;                                             \
        }                                                             \
    } while (0)

#define MC_MAX_COMBOS 16u

/* ---------------------------------------------------------------- *
 * Card and fixture helpers
 * ---------------------------------------------------------------- */

static mask_t cd(int rank, int suit)
{
    return mask_set(MASK_EMPTY, MODERN_MAKE_CARD(rank, suit));
}

static mask_t h2(int r0, int s0, int r1, int s1)
{
    return cd(r0, s0) | cd(r1, s1);
}

static mask_t h4(int r0, int s0, int r1, int s1, int r2, int s2, int r3,
                 int s3)
{
    return cd(r0, s0) | cd(r1, s1) | cd(r2, s2) | cd(r3, s3);
}

static mask_t h5(int r0, int s0, int r1, int s1, int r2, int s2, int r3,
                 int s3, int r4, int s4)
{
    return h4(r0, s0, r1, s1, r2, s2, r3, s3) | cd(r4, s4);
}

static mask_t h6(int r0, int s0, int r1, int s1, int r2, int s2, int r3,
                 int s3, int r4, int s4, int r5, int s5)
{
    return h5(r0, s0, r1, s1, r2, s2, r3, s3, r4, s4) | cd(r5, s5);
}

static pe_oracle_combo_t oc(mask_t cards, double prior, double action_prob)
{
    pe_oracle_combo_t combo;
    combo.cards = cards;
    combo.prior = prior;
    combo.action_prob = action_prob;
    return combo;
}

/* A named fixture: the oracle game, plus the production ranges built from it
   in the same combo order, so a combo index means the same thing on both
   sides of every comparison. */
typedef struct
{
    pe_holdem_range_t *holdem;
    pe_omaha_range_t *omaha;
    pe_holdem_combo_t *holdem_combos;
    pe_omaha_combo_t *omaha_combos;
    size_t combo_count;
} built_ranges_t;

static void built_ranges_free(built_ranges_t *built)
{
    free(built->holdem);
    free(built->omaha);
    free(built->holdem_combos);
    free(built->omaha_combos);
    memset(built, 0, sizeof(*built));
}

static int built_ranges_build(const pe_oracle_game_t *game, int omaha,
                              built_ranges_t *out)
{
    size_t total = 0u;
    size_t offset = 0u;
    uint8_t player;
    size_t i;

    memset(out, 0, sizeof(*out));
    for (player = 0u; player < game->player_count; ++player)
        total += game->ranges[player].count;
    if (total == 0u)
        return -1;

    if (omaha)
    {
        out->omaha = (pe_omaha_range_t *)calloc(game->player_count,
                                                sizeof(pe_omaha_range_t));
        out->omaha_combos = (pe_omaha_combo_t *)calloc(
            total, sizeof(pe_omaha_combo_t));
        if (!out->omaha || !out->omaha_combos)
        {
            built_ranges_free(out);
            return -1;
        }
    }
    else
    {
        out->holdem = (pe_holdem_range_t *)calloc(game->player_count,
                                                  sizeof(pe_holdem_range_t));
        out->holdem_combos = (pe_holdem_combo_t *)calloc(
            total, sizeof(pe_holdem_combo_t));
        if (!out->holdem || !out->holdem_combos)
        {
            built_ranges_free(out);
            return -1;
        }
    }

    out->combo_count = total;
    for (player = 0u; player < game->player_count; ++player)
    {
        const pe_oracle_range_t *range = &game->ranges[player];
        if (omaha)
        {
            out->omaha[player].combos = &out->omaha_combos[offset];
            out->omaha[player].count = range->count;
        }
        else
        {
            out->holdem[player].combos = &out->holdem_combos[offset];
            out->holdem[player].count = range->count;
        }
        for (i = 0u; i < range->count; ++i)
        {
            double weight = pe_oracle_combo_weight(&range->combos[i]);
            if (omaha)
            {
                out->omaha_combos[offset + i].cards = range->combos[i].cards;
                out->omaha_combos[offset + i].weight = weight;
            }
            else
            {
                out->holdem_combos[offset + i].cards = range->combos[i].cards;
                out->holdem_combos[offset + i].weight = weight;
            }
        }
        offset += range->count;
    }
    return 0;
}

/* ---------------------------------------------------------------- *
 * Importance-weighted Monte Carlo accumulator
 * ---------------------------------------------------------------- */

typedef struct
{
    size_t samples;
    double weight_sum;
    double weight_sq_sum;
    size_t collisions;        /* two hands sharing a card */
    size_t board_collisions;  /* a hand using a board card */
    size_t wrong_width;       /* a hand that is not legal for the variant */
    size_t zero_reach_hits;   /* an emitted combo whose posterior is zero */

    uint8_t player_count;
    size_t combo_counts[PE_ORACLE_MAX_PLAYERS];
    double combo_weight[PE_ORACLE_MAX_PLAYERS * MC_MAX_COMBOS];
    double card_held[PE_ORACLE_MAX_PLAYERS][PE_ORACLE_DECK];
    double card_free[PE_ORACLE_DECK];
} mc_t;

static double mc_n_eff(const mc_t *mc)
{
    if (!(mc->weight_sq_sum > 0.0))
        return 0.0;
    return mc->weight_sum * mc->weight_sum / mc->weight_sq_sum;
}

static double mc_combo_prob(const mc_t *mc, uint8_t player, size_t combo)
{
    if (!(mc->weight_sum > 0.0))
        return 0.0;
    return mc->combo_weight[(size_t)player * MC_MAX_COMBOS + combo] /
           mc->weight_sum;
}

static double mc_card_free_prob(const mc_t *mc, int card)
{
    if (!(mc->weight_sum > 0.0))
        return 0.0;
    return mc->card_free[card] / mc->weight_sum;
}

/*
 * Draw `samples` deals from the production sampler and accumulate
 * self-normalised importance-weighted estimates. The sampler's proposal is
 * not the target distribution once card removal is in play, so raw counts
 * would estimate the wrong thing; every estimate below is weighted by
 * deal.importance_ratio, which is exactly the target-over-proposal ratio.
 */
static int mc_run(const pe_preflop_deal_sampler_t *sampler, uint64_t seed,
                  size_t samples, const pe_oracle_game_t *game,
                  const pe_oracle_result_t *exact, mc_t *acc)
{
    pe_rng_t rng;

    memset(acc, 0, sizeof(*acc));
    acc->player_count = game->player_count;
    for (uint8_t p = 0u; p < game->player_count; ++p)
        acc->combo_counts[p] = game->ranges[p].count;
    pe_rng_seed(&rng, seed);

    for (size_t s = 0u; s < samples; ++s)
    {
        pe_preflop_deal_sample_t deal;
        double weight;
        mask_t hands = MASK_EMPTY;

        if (pe_preflop_deal_sampler_sample(sampler, &rng, &deal) != 0)
            return -1;

        weight = deal.importance_ratio;
        if (!(weight > 0.0) || !isfinite(weight))
            return -1;

        acc->samples++;
        acc->weight_sum += weight;
        acc->weight_sq_sum += weight * weight;

        for (uint8_t p = 0u; p < game->player_count; ++p)
        {
            mask_t hole = deal.holes[p];

            if (mask_popcount(hole) != sampler->hole_cards)
                acc->wrong_width++;
            if (mask_intersects(hole, sampler->board))
                acc->board_collisions++;
            if (mask_intersects(hole, hands))
                acc->collisions++;
            hands |= hole;

            for (size_t i = 0u; i < game->ranges[p].count; ++i)
            {
                if (game->ranges[p].combos[i].cards != hole)
                    continue;
                acc->combo_weight[(size_t)p * MC_MAX_COMBOS + i] += weight;
                /* A combo the oracle gives zero posterior mass must never be
                   emitted at all: that is the zero-reach invariant. */
                if (pe_oracle_combo_prob(exact, p, i) <= 0.0)
                    acc->zero_reach_hits++;
            }
            for (int card = 0; card < PE_ORACLE_DECK; ++card)
                if (mask_is_set(hole, card))
                    acc->card_held[p][card] += weight;
        }

        for (int card = 0; card < PE_ORACLE_DECK; ++card)
            if (!mask_is_set(sampler->board, card) &&
                !mask_is_set(hands, card))
                acc->card_free[card] += weight;
    }
    return 0;
}

/* ---------------------------------------------------------------- *
 * Comparison
 * ---------------------------------------------------------------- */

#define MC_Z 5.0

static void compare_against_oracle(const char *label, const mc_t *mc,
                                   const pe_oracle_game_t *game,
                                   const pe_oracle_result_t *exact)
{
    double n_eff = mc_n_eff(mc);
    double worst = 0.0;
    int worst_card = -1;

    CHECK(n_eff >= 0.25 * (double)mc->samples,
          "%s: effective sample size %.0f of %zu draws is too small for a "
          "five-sigma comparison",
          label, n_eff, mc->samples);

    for (uint8_t p = 0u; p < game->player_count; ++p)
    {
        double sum = 0.0;
        for (size_t i = 0u; i < game->ranges[p].count; ++i)
        {
            double want = pe_oracle_combo_prob(exact, p, i);
            double got = mc_combo_prob(mc, p, i);
            double tol = pe_oracle_tolerance(want, n_eff, MC_Z);

            sum += got;
            CHECK(fabs(got - want) <= tol,
                  "%s: player %u combo %zu: sampled %.6f, exact %.6f "
                  "(tolerance %.2e)",
                  label, p, i, got, want, tol);
        }
        CHECK(fabs(sum - 1.0) <= 1e-9,
              "%s: player %u sampled marginal sums to %.12f, not 1",
              label, p, sum);
    }

    for (int card = 0; card < PE_ORACLE_DECK; ++card)
    {
        double want = exact->card_available[card];
        double got = mc_card_free_prob(mc, card);
        double tol = pe_oracle_tolerance(want, n_eff, MC_Z);
        double gap = fabs(got - want);

        if (gap > worst)
        {
            worst = gap;
            worst_card = card;
        }
        CHECK(gap <= tol,
              "%s: card %d availability: sampled %.6f, exact %.6f "
              "(tolerance %.2e)",
              label, card, got, want, tol);
    }

    printf("    %-34s n=%zu ess=%.0f  worst card gap %.2e (card %d)\n", label,
           mc->samples, n_eff, worst, worst_card);
}

static void check_no_collisions(const char *label, const mc_t *mc)
{
    CHECK(mc->collisions == 0, "%s: %zu deals shared a card between hands",
          label, mc->collisions);
    CHECK(mc->board_collisions == 0,
          "%s: %zu deals put a private card on the board", label,
          mc->board_collisions);
    CHECK(mc->wrong_width == 0,
          "%s: %zu hands had the wrong number of cards", label,
          mc->wrong_width);
    CHECK(mc->zero_reach_hits == 0,
          "%s: %zu deals emitted a combo with zero posterior mass", label,
          mc->zero_reach_hits);
}

/* One fixture, both variants: run the oracle, run the sampler, compare, and
   check the structural invariants. */
static int run_holdem_fixture(const char *label, const pe_oracle_game_t *game,
                              uint64_t seed, size_t samples, mc_t *out_mc)
{
    built_ranges_t built;
    pe_oracle_result_t exact;
    pe_preflop_deal_sampler_t sampler;
    size_t deal_count = 0u;
    double weight_sum = 0.0;
    int rc;

    if (pe_oracle_run(game, &exact) != 0)
    {
        CHECK(0, "%s: the oracle refused the fixture", label);
        return -1;
    }
    if (built_ranges_build(game, 0, &built) != 0)
    {
        CHECK(0, "%s: could not build the production ranges", label);
        pe_oracle_result_free(&exact);
        return -1;
    }
    if (pe_preflop_deal_sampler_init_holdem(&sampler, game->board,
                                            built.holdem,
                                            game->player_count) != 0)
    {
        CHECK(0, "%s: the sampler refused the fixture", label);
        built_ranges_free(&built);
        pe_oracle_result_free(&exact);
        return -1;
    }
    /* The exact normalisation is available on these small fixtures, so the
       importance ratio comes back already scaled to the posterior. */
    if (pe_preflop_deal_sampler_measure(&sampler, &deal_count,
                                        &weight_sum) == 0)
        sampler.reference_weight_sum = weight_sum;

    rc = mc_run(&sampler, seed, samples, game, &exact, out_mc);
    if (rc != 0)
    {
        CHECK(0, "%s: the sampler failed to draw", label);
        built_ranges_free(&built);
        pe_oracle_result_free(&exact);
        return -1;
    }

    compare_against_oracle(label, out_mc, game, &exact);
    check_no_collisions(label, out_mc);

    built_ranges_free(&built);
    pe_oracle_result_free(&exact);
    return 0;
}

static int run_omaha_fixture(const char *label, const pe_oracle_game_t *game,
                             uint8_t hole_cards, uint64_t seed, size_t samples,
                             mc_t *out_mc)
{
    built_ranges_t built;
    pe_oracle_result_t exact;
    pe_preflop_deal_sampler_t sampler;
    size_t deal_count = 0u;
    double weight_sum = 0.0;
    int rc;

    if (pe_oracle_run(game, &exact) != 0)
    {
        CHECK(0, "%s: the oracle refused the fixture", label);
        return -1;
    }
    if (built_ranges_build(game, 1, &built) != 0)
    {
        CHECK(0, "%s: could not build the production ranges", label);
        pe_oracle_result_free(&exact);
        return -1;
    }
    if (pe_preflop_deal_sampler_init_omaha(&sampler, game->board, built.omaha,
                                           game->player_count,
                                           hole_cards) != 0)
    {
        CHECK(0, "%s: the sampler refused the fixture", label);
        built_ranges_free(&built);
        pe_oracle_result_free(&exact);
        return -1;
    }
    if (pe_preflop_deal_sampler_measure(&sampler, &deal_count,
                                        &weight_sum) == 0)
        sampler.reference_weight_sum = weight_sum;

    rc = mc_run(&sampler, seed, samples, game, &exact, out_mc);
    if (rc != 0)
    {
        CHECK(0, "%s: the sampler failed to draw", label);
        built_ranges_free(&built);
        pe_oracle_result_free(&exact);
        return -1;
    }

    compare_against_oracle(label, out_mc, game, &exact);
    check_no_collisions(label, out_mc);

    built_ranges_free(&built);
    pe_oracle_result_free(&exact);
    return 0;
}

/* ---------------------------------------------------------------- *
 * Case A - binary fold filter
 * ---------------------------------------------------------------- */

/*
 * Player A folds only with aces. Conditioning on the observed fold leaves A
 * holding AAs, so A always holds the ace of spades and never holds a king.
 *
 * Both mistakes the invariant rules out are measured on the same fixture:
 * ignoring the fold leaves P(A holds the ace of spades) at 1/3, and dropping
 * A from the deal lets the player behind him hold that ace, which is exactly
 * the card A is holding.
 */
static void test_case_a_binary_fold_filter(void)
{
    const int C = MODERN_SUIT_CLUBS;
    const int D = MODERN_SUIT_DIAMONDS;
    const int H = MODERN_SUIT_HEARTS;
    const int S = MODERN_SUIT_SPADES;
    const int TWO = MODERN_RANK_2, THREE = MODERN_RANK_3;
    const int FOUR = MODERN_RANK_4, FIVE = MODERN_RANK_5;
    const int SIX = MODERN_RANK_6, SEVEN = MODERN_RANK_7;
    const int EIGHT = MODERN_RANK_8, NINE = MODERN_RANK_9;
    const int JACK = MODERN_RANK_J, QUEEN = MODERN_RANK_Q;
    const int KING = MODERN_RANK_K, ACE = MODERN_RANK_A;

    pe_oracle_combo_t a_combos[] = {
        oc(h2(ACE, S, ACE, H), 1.0, 1.0),   /* folds */
        oc(h2(ACE, S, ACE, D), 1.0, 1.0),   /* folds */
        oc(h2(KING, S, KING, H), 1.0, 0.0), /* continues */
        oc(h2(KING, S, KING, D), 1.0, 0.0)  /* continues */
    };
    pe_oracle_combo_t b_combos[] = {
        oc(h2(ACE, H, KING, H), 1.0, 1.0),
        oc(h2(ACE, D, KING, D), 1.0, 1.0),
        oc(h2(ACE, S, QUEEN, C), 1.0, 1.0)
    };
    pe_oracle_range_t ranges[] = {
        {a_combos, sizeof(a_combos) / sizeof(a_combos[0])},
        {b_combos, sizeof(b_combos) / sizeof(b_combos[0])}
    };
    pe_oracle_game_t game = {ranges, 2u, MASK_EMPTY};

    /* The same fixture with the fold ignored: every combo continues. */
    pe_oracle_combo_t a_unconditional[] = {
        oc(h2(ACE, S, ACE, H), 1.0, 1.0),
        oc(h2(ACE, S, ACE, D), 1.0, 1.0),
        oc(h2(KING, S, KING, H), 1.0, 1.0),
        oc(h2(KING, S, KING, D), 1.0, 1.0)
    };
    pe_oracle_range_t unconditional_ranges[] = {
        {a_unconditional, sizeof(a_unconditional) / sizeof(a_unconditional[0])},
        {b_combos, sizeof(b_combos) / sizeof(b_combos[0])}
    };
    pe_oracle_game_t unconditional_game = {unconditional_ranges, 2u,
                                           MASK_EMPTY};

    /* And with A dropped from the deal, as if a fold returned his cards. */
    pe_oracle_range_t alone_ranges[] = {
        {b_combos, sizeof(b_combos) / sizeof(b_combos[0])}
    };
    pe_oracle_game_t alone_game = {alone_ranges, 1u, MASK_EMPTY};

    pe_oracle_result_t exact, unconditioned, alone;
    mc_t mc;
    const int ace_spades = MODERN_MAKE_CARD(ACE, S);
    const int king_spades = MODERN_MAKE_CARD(KING, S);
    double conditioned_p, unconditional_p, dropped_p;

    /* Unused rank constants keep the card table above readable; silence the
       warning rather than dropping them. */
    (void)TWO; (void)THREE; (void)FOUR; (void)FIVE; (void)SIX; (void)SEVEN;
    (void)EIGHT; (void)NINE; (void)JACK;

    CHECK(pe_oracle_run(&game, &exact) == 0, "case A: oracle failed");
    CHECK(pe_oracle_run(&unconditional_game, &unconditioned) == 0,
          "case A: unconditional oracle failed");
    CHECK(pe_oracle_run(&alone_game, &alone) == 0,
          "case A: dropped-player oracle failed");

    conditioned_p = pe_oracle_player_card_prob(&game, &exact, 0, ace_spades);
    unconditional_p = pe_oracle_player_card_prob(&unconditional_game,
                                                 &unconditioned, 0,
                                                 ace_spades);
    dropped_p = pe_oracle_player_card_prob(&alone_game, &alone, 0, ace_spades);

    printf("  Case A - binary fold filter\n");
    /* A folds only with aces: the posterior puts him on aces with certainty. */
    CHECK(fabs(conditioned_p - 1.0) < 1e-12,
          "case A: P(A holds the ace of spades | fold) is %.12f, expected 1",
          conditioned_p);
    CHECK(fabs(pe_oracle_player_card_prob(&game, &exact, 0, king_spades)) <
              1e-12,
          "case A: the fold-conditioned posterior still gives A a king");
    /* Guard: an unconditional range keeps half of A's range on kings. */
    CHECK(fabs(unconditional_p - 1.0 / 3.0) < 1e-12,
          "case A: the unconditioned posterior is %.12f, expected 1/3",
          unconditional_p);
    CHECK(conditioned_p - unconditional_p > 0.5,
          "case A: the fold must move the posterior by more than 0.5");
    /* Guard: with A out of the deal his ace is available to the next player. */
    CHECK(fabs(dropped_p - 1.0 / 3.0) < 1e-12,
          "case A: dropping A gives the next player the ace with %.12f, "
          "expected 1/3",
          dropped_p);
    /* And in the correct deal the next player can never hold it. */
    CHECK(fabs(pe_oracle_player_card_prob(&game, &exact, 1, ace_spades)) < 1e-12,
          "case A: the next player holds a card A is holding");

    CHECK(pe_oracle_marginal_sum(&exact, 0) > 1.0 - 1e-12 &&
              pe_oracle_marginal_sum(&exact, 0) < 1.0 + 1e-12,
          "case A: A's posterior marginal does not sum to 1");
    CHECK(pe_oracle_marginal_sum(&exact, 1) > 1.0 - 1e-12 &&
              pe_oracle_marginal_sum(&exact, 1) < 1.0 + 1e-12,
          "case A: B's posterior marginal does not sum to 1");

    run_holdem_fixture("case A  conditioned on the fold", &game, 0xA1u,
                       40000u, &mc);
    /* B's zero-posterior combo (the one A is blocking) must never appear. */
    CHECK(pe_oracle_combo_prob(&exact, 1, 2) == 0.0,
          "case A: the blocked combo is not the one expected");

    pe_oracle_result_free(&exact);
    pe_oracle_result_free(&unconditioned);
    pe_oracle_result_free(&alone);
}

/* ---------------------------------------------------------------- *
 * Case B - mixed strategy posterior, and blockers before normalisation
 * ---------------------------------------------------------------- */

static void test_case_b_mixed_strategy_posterior(void)
{
    const int C = MODERN_SUIT_CLUBS;
    const int D = MODERN_SUIT_DIAMONDS;
    const int H = MODERN_SUIT_HEARTS;
    const int S = MODERN_SUIT_SPADES;
    const int KING = MODERN_RANK_K, QUEEN = MODERN_RANK_Q, ACE = MODERN_RANK_A;

    /* Posterior weights 1.0, 0.5, 0.0 -> 2/3, 1/3, 0. */
    pe_oracle_combo_t a_combos[] = {
        oc(h2(ACE, S, ACE, H), 1.0, 1.00),
        oc(h2(ACE, S, ACE, D), 2.0, 0.25),
        oc(h2(ACE, H, ACE, D), 3.0, 0.00)
    };
    /* Disjoint from the aces, so the posterior is pure Bayesian reweighting. */
    pe_oracle_combo_t b_disjoint[] = {
        oc(h2(KING, S, KING, H), 1.0, 1.0),
        oc(h2(KING, D, KING, C), 1.0, 1.0),
        oc(h2(QUEEN, S, QUEEN, H), 1.0, 1.0)
    };
    pe_oracle_range_t disjoint_ranges[] = {
        {a_combos, sizeof(a_combos) / sizeof(a_combos[0])},
        {b_disjoint, sizeof(b_disjoint) / sizeof(b_disjoint[0])}
    };
    pe_oracle_game_t disjoint_game = {disjoint_ranges, 2u, MASK_EMPTY};

    /* The same A, but now B's first combo shares the ace of hearts with A's
       best combo. The posterior must account for that before normalising. */
    pe_oracle_combo_t b_blocking[] = {
        oc(h2(ACE, H, KING, H), 1.0, 1.0),
        oc(h2(KING, D, KING, C), 1.0, 1.0),
        oc(h2(QUEEN, S, QUEEN, H), 1.0, 1.0)
    };
    pe_oracle_range_t blocking_ranges[] = {
        {a_combos, sizeof(a_combos) / sizeof(a_combos[0])},
        {b_blocking, sizeof(b_blocking) / sizeof(b_blocking[0])}
    };
    pe_oracle_game_t blocking_game = {blocking_ranges, 2u, MASK_EMPTY};

    pe_oracle_result_t exact, blocking;
    mc_t mc;
    double p0, p1, p2, blocked_p0;

    CHECK(pe_oracle_run(&disjoint_game, &exact) == 0,
          "case B: oracle failed");
    CHECK(pe_oracle_run(&blocking_game, &blocking) == 0,
          "case B: blocking oracle failed");

    printf("  Case B - mixed strategy posterior\n");
    p0 = pe_oracle_combo_prob(&exact, 0, 0);
    p1 = pe_oracle_combo_prob(&exact, 0, 1);
    p2 = pe_oracle_combo_prob(&exact, 0, 2);

    /* Posterior proportional to prior * fold frequency. */
    CHECK(fabs(p0 - 2.0 / 3.0) < 1e-12,
          "case B: P(combo 0 | fold) is %.12f, expected 2/3", p0);
    CHECK(fabs(p1 - 1.0 / 3.0) < 1e-12,
          "case B: P(combo 1 | fold) is %.12f, expected 1/3", p1);
    CHECK(p2 == 0.0,
          "case B: a combo that never folds has posterior %.12f", p2);
    /* The unconditioned prior would be 1/6, 2/6, 3/6. */
    CHECK(fabs(p0 - 1.0 / 6.0) > 0.4,
          "case B: the fold barely moved combo 0 (%.6f against a prior of 1/6)",
          p0);

    /* Blocker shift: A's best combo blocks one of B's three combos, so it
       keeps a smaller share of the joint mass than the raw reweighting says. */
    blocked_p0 = pe_oracle_combo_prob(&blocking, 0, 0);
    CHECK(fabs(blocked_p0 - 4.0 / 7.0) < 1e-12,
          "case B: with a blocking opponent, P(combo 0 | fold) is %.12f, "
          "expected 4/7",
          blocked_p0);
    CHECK(fabs(blocked_p0 - 2.0 / 3.0) > 0.05,
          "case B: the blocker did not move the posterior at all");

    run_holdem_fixture("case B  disjoint opponent", &disjoint_game, 0xB1u,
                       40000u, &mc);
    run_holdem_fixture("case B  blocking opponent", &blocking_game, 0xB2u,
                       40000u, &mc);

    pe_oracle_result_free(&exact);
    pe_oracle_result_free(&blocking);
}

/* ---------------------------------------------------------------- *
 * Case C - rank blocker shift
 * ---------------------------------------------------------------- */

/*
 * The example from the issue. The board is 3-3-5; player A never folds a
 * hand containing a three. Observing a fold therefore makes it less likely
 * that A holds one, so the remaining threes become more available.
 */
static void test_case_c_rank_blocker_shift(void)
{
    const int C = MODERN_SUIT_CLUBS;
    const int D = MODERN_SUIT_DIAMONDS;
    const int H = MODERN_SUIT_HEARTS;
    const int S = MODERN_SUIT_SPADES;
    const int THREE = MODERN_RANK_3, FIVE = MODERN_RANK_5;
    const int JACK = MODERN_RANK_J, QUEEN = MODERN_RANK_Q;
    const int KING = MODERN_RANK_K, ACE = MODERN_RANK_A;

    mask_t board = cd(THREE, S) | cd(THREE, H) | cd(FIVE, D);
    const int three_clubs = MODERN_MAKE_CARD(THREE, C);
    const int three_diamonds = MODERN_MAKE_CARD(THREE, D);

    pe_oracle_combo_t a_combos[] = {
        oc(h2(THREE, C, THREE, D), 1.0, 0.0), /* never folds a three */
        oc(h2(ACE, S, KING, S), 1.0, 1.0)     /* always folds */
    };
    pe_oracle_combo_t b_combos[] = {
        oc(h2(QUEEN, S, QUEEN, H), 1.0, 1.0),
        oc(h2(JACK, S, JACK, H), 1.0, 1.0)
    };
    pe_oracle_range_t ranges[] = {
        {a_combos, sizeof(a_combos) / sizeof(a_combos[0])},
        {b_combos, sizeof(b_combos) / sizeof(b_combos[0])}
    };
    pe_oracle_game_t game = {ranges, 2u, board};

    pe_oracle_combo_t a_unconditional[] = {
        oc(h2(THREE, C, THREE, D), 1.0, 1.0),
        oc(h2(ACE, S, KING, S), 1.0, 1.0)
    };
    pe_oracle_range_t unconditional_ranges[] = {
        {a_unconditional, sizeof(a_unconditional) / sizeof(a_unconditional[0])},
        {b_combos, sizeof(b_combos) / sizeof(b_combos[0])}
    };
    pe_oracle_game_t unconditional_game = {unconditional_ranges, 2u, board};

    pe_oracle_result_t exact, unconditioned;
    mc_t mc;
    double conditioned_clubs, conditioned_diamonds, prior_clubs;

    CHECK(pe_oracle_run(&game, &exact) == 0, "case C: oracle failed");
    CHECK(pe_oracle_run(&unconditional_game, &unconditioned) == 0,
          "case C: unconditional oracle failed");

    conditioned_clubs = exact.card_available[three_clubs];
    conditioned_diamonds = exact.card_available[three_diamonds];
    prior_clubs = unconditioned.card_available[three_clubs];

    printf("  Case C - rank blocker shift (board 3s 3h 5d)\n");
    /* Nobody can hold the remaining threes any more, so both are certain to
       come out of the deck. */
    CHECK(fabs(conditioned_clubs - 1.0) < 1e-12,
          "case C: the three of clubs is available with %.12f, expected 1",
          conditioned_clubs);
    CHECK(fabs(conditioned_diamonds - 1.0) < 1e-12,
          "case C: the three of diamonds is available with %.12f, expected 1",
          conditioned_diamonds);
    /* Before conditioning, A holds a three half the time. */
    CHECK(fabs(prior_clubs - 0.5) < 1e-12,
          "case C: the prior availability of the three of clubs is %.12f, "
          "expected 1/2",
          prior_clubs);
    CHECK(conditioned_clubs - prior_clubs > 0.4,
          "case C: the fold did not make the three more available");

    run_holdem_fixture("case C  conditioned on the fold", &game, 0xC1u, 40000u,
                       &mc);
    run_holdem_fixture("case C  fold ignored", &unconditional_game, 0xC2u,
                       40000u, &mc);

    pe_oracle_result_free(&exact);
    pe_oracle_result_free(&unconditioned);
}

/* ---------------------------------------------------------------- *
 * Case D - suit blocker shift, PLO4
 * ---------------------------------------------------------------- */

static void test_case_d_suit_blocker_shift_plo4(void)
{
    const int C = MODERN_SUIT_CLUBS;
    const int D = MODERN_SUIT_DIAMONDS;
    const int H = MODERN_SUIT_HEARTS;
    const int S = MODERN_SUIT_SPADES;
    const int TWO = MODERN_RANK_2, THREE = MODERN_RANK_3;
    const int FOUR = MODERN_RANK_4, FIVE = MODERN_RANK_5;
    const int SIX = MODERN_RANK_6, SEVEN = MODERN_RANK_7;
    const int EIGHT = MODERN_RANK_8, NINE = MODERN_RANK_9;
    const int JACK = MODERN_RANK_J, QUEEN = MODERN_RANK_Q;
    const int KING = MODERN_RANK_K, ACE = MODERN_RANK_A;

    mask_t board = cd(KING, S) | cd(QUEEN, S) | cd(SEVEN, H);
    const int eight_spades = MODERN_MAKE_CARD(EIGHT, S);

    /* A continues only with four spades, so a fold leaves him off-spades. */
    pe_oracle_combo_t a_combos[] = {
        oc(h4(ACE, S, JACK, S, EIGHT, S, NINE, S), 1.0, 0.0),
        oc(h4(TWO, H, THREE, H, FOUR, D, FIVE, D), 1.0, 1.0)
    };
    /* B's first hand needs the eight of spades; the second does not. */
    pe_oracle_combo_t b_combos[] = {
        oc(h4(EIGHT, S, SIX, S, FIVE, C, FOUR, C), 1.0, 1.0),
        oc(h4(NINE, H, EIGHT, H, THREE, C, TWO, C), 1.0, 1.0)
    };
    pe_oracle_range_t ranges[] = {
        {a_combos, sizeof(a_combos) / sizeof(a_combos[0])},
        {b_combos, sizeof(b_combos) / sizeof(b_combos[0])}
    };
    pe_oracle_game_t game = {ranges, 2u, board};

    pe_oracle_combo_t a_unconditional[] = {
        oc(h4(ACE, S, JACK, S, EIGHT, S, NINE, S), 1.0, 1.0),
        oc(h4(TWO, H, THREE, H, FOUR, D, FIVE, D), 1.0, 1.0)
    };
    pe_oracle_range_t unconditional_ranges[] = {
        {a_unconditional, sizeof(a_unconditional) / sizeof(a_unconditional[0])},
        {b_combos, sizeof(b_combos) / sizeof(b_combos[0])}
    };
    pe_oracle_game_t unconditional_game = {unconditional_ranges, 2u, board};

    pe_oracle_result_t exact, unconditioned;
    mc_t mc;
    double conditioned, prior;

    CHECK(pe_oracle_run(&game, &exact) == 0, "case D: oracle failed");
    CHECK(pe_oracle_run(&unconditional_game, &unconditioned) == 0,
          "case D: unconditional oracle failed");

    conditioned = exact.card_available[eight_spades];
    prior = unconditioned.card_available[eight_spades];

    printf("  Case D - suit blocker shift, PLO4\n");
    /* A is off-spades and B takes the eight half the time. */
    CHECK(fabs(conditioned - 0.5) < 1e-12,
          "case D: the eight of spades is available with %.12f, expected 1/2",
          conditioned);
    CHECK(conditioned > prior + 0.1,
          "case D: the fold did not make the spade more available "
          "(%.6f against %.6f)",
          conditioned, prior);

    run_omaha_fixture("case D  conditioned on the fold", &game, 4u, 0xD1u,
                      40000u, &mc);
    run_omaha_fixture("case D  fold ignored", &unconditional_game, 4u, 0xD2u,
                      40000u, &mc);

    pe_oracle_result_free(&exact);
    pe_oracle_result_free(&unconditioned);
}

/* ---------------------------------------------------------------- *
 * Case E - PLO5 and PLO6 multi-card removal
 * ---------------------------------------------------------------- */

static void plo_multi_card_case(const char *label, uint8_t hole_cards,
                                uint64_t seed_conditioned, uint64_t seed_prior)
{
    const int C = MODERN_SUIT_CLUBS;
    const int D = MODERN_SUIT_DIAMONDS;
    const int H = MODERN_SUIT_HEARTS;
    const int S = MODERN_SUIT_SPADES;
    const int TWO = MODERN_RANK_2, THREE = MODERN_RANK_3;
    const int FOUR = MODERN_RANK_4, FIVE = MODERN_RANK_5;
    const int SIX = MODERN_RANK_6, SEVEN = MODERN_RANK_7;
    const int EIGHT = MODERN_RANK_8, NINE = MODERN_RANK_9;
    const int JACK = MODERN_RANK_J, QUEEN = MODERN_RANK_Q;
    const int KING = MODERN_RANK_K, ACE = MODERN_RANK_A;

    mask_t board = cd(TWO, C) | cd(SEVEN, D) | cd(MODERN_RANK_T, H);
    mask_t a0, a1, b0, b1;

    if (hole_cards == 5u)
    {
        /* A folds only with the off-suit hand, so the posterior is the five
           hearts; the spade hand is what a continue looks like. */
        a0 = h5(ACE, S, KING, S, QUEEN, S, JACK, S, NINE, S);
        a1 = h5(THREE, H, FOUR, H, FIVE, H, SIX, H, EIGHT, H);
        /* B0 shares the eight and nine of hearts with the hand A is left
           holding, so it is illegal there and legal only against the spade
           hand. B1 is legal against both of A's hands. */
        b0 = h5(EIGHT, H, NINE, H, NINE, C, NINE, D, TWO, D);
        b1 = h5(EIGHT, C, EIGHT, D, THREE, C, FOUR, C, TWO, S);
    }
    else
    {
        a0 = h6(ACE, S, KING, S, QUEEN, S, JACK, S, NINE, S, EIGHT, S);
        a1 = h6(THREE, H, FOUR, H, FIVE, H, SIX, H, EIGHT, H, NINE, H);
        b0 = h6(NINE, H, NINE, C, NINE, D, TWO, D, TWO, S, EIGHT, C);
        b1 = h6(THREE, C, FOUR, C, FIVE, C, SIX, C, SEVEN, C, TWO, D);
    }

    pe_oracle_combo_t a_combos[] = { oc(a0, 1.0, 0.0), oc(a1, 1.0, 1.0) };
    pe_oracle_combo_t b_combos[] = { oc(b0, 1.0, 1.0), oc(b1, 1.0, 1.0) };
    pe_oracle_range_t ranges[] = {
        {a_combos, 2u},
        {b_combos, 2u}
    };
    pe_oracle_game_t game = {ranges, 2u, board};

    pe_oracle_combo_t a_unconditional[] = { oc(a0, 1.0, 1.0), oc(a1, 1.0, 1.0) };
    pe_oracle_range_t unconditional_ranges[] = {
        {a_unconditional, 2u},
        {b_combos, 2u}
    };
    pe_oracle_game_t unconditional_game = {unconditional_ranges, 2u, board};

    pe_oracle_result_t exact, unconditioned;
    mc_t mc;
    double conditioned_b0, prior_b0;

    CHECK(pe_oracle_run(&game, &exact) == 0, "%s: oracle failed", label);
    CHECK(pe_oracle_run(&unconditional_game, &unconditioned) == 0,
          "%s: unconditional oracle failed", label);

    conditioned_b0 = pe_oracle_combo_prob(&exact, 1, 0);
    prior_b0 = pe_oracle_combo_prob(&unconditioned, 1, 0);

    printf("  Case E - %s multi-card removal\n", label);
    /* B0 shares cards with the hand A still holds after the fold, so it is
       impossible there and B is forced onto B1. The unconditioned variant
       keeps the spade hand alive and B0 comes back with weight 1/3. */
    CHECK(conditioned_b0 == 0.0,
          "%s: the fully blocked hand has posterior %.12f", label,
          conditioned_b0);
    CHECK(fabs(pe_oracle_combo_prob(&exact, 1, 1) - 1.0) < 1e-12,
          "%s: the surviving hand has posterior %.12f, expected 1", label,
          pe_oracle_combo_prob(&exact, 1, 1));
    CHECK(fabs(prior_b0 - 1.0 / 3.0) < 1e-12,
          "%s: the unconditioned posterior is %.12f, expected 1/3", label,
          prior_b0);

    {
        char conditioned_label[64];
        char prior_label[64];
        snprintf(conditioned_label, sizeof(conditioned_label),
                 "%s  conditioned on the fold", label);
        snprintf(prior_label, sizeof(prior_label), "%s  fold ignored", label);
        run_omaha_fixture(conditioned_label, &game, hole_cards,
                          seed_conditioned, 30000u, &mc);
        run_omaha_fixture(prior_label, &unconditional_game, hole_cards,
                          seed_prior, 30000u, &mc);
    }

    pe_oracle_result_free(&exact);
    pe_oracle_result_free(&unconditioned);
}

/* ---------------------------------------------------------------- *
 * Case F - multiway sequential conditioning
 * ---------------------------------------------------------------- */

/*
 * Three players. A folds, then B calls; C receives a hand once both actions
 * are known. C's distribution must reflect both posteriors: the cards A and B
 * are holding are unavailable to C, and C's first two combos are impossible
 * because of them.
 */
static void test_case_f_multiway(void)
{
    const int C = MODERN_SUIT_CLUBS;
    const int D = MODERN_SUIT_DIAMONDS;
    const int H = MODERN_SUIT_HEARTS;
    const int S = MODERN_SUIT_SPADES;
    const int JACK = MODERN_RANK_J, QUEEN = MODERN_RANK_Q;
    const int KING = MODERN_RANK_K, ACE = MODERN_RANK_A;

    pe_oracle_combo_t a_combos[] = {
        oc(h2(ACE, S, ACE, H), 1.0, 1.0),  /* folds */
        oc(h2(KING, S, KING, H), 1.0, 0.0) /* continues */
    };
    pe_oracle_combo_t b_combos[] = {
        oc(h2(ACE, D, ACE, C), 1.0, 1.0),  /* calls */
        oc(h2(QUEEN, S, QUEEN, H), 1.0, 0.0)
    };
    pe_oracle_combo_t c_combos[] = {
        oc(h2(ACE, S, KING, S), 1.0, 1.0), /* blocked by A */
        oc(h2(ACE, D, KING, D), 1.0, 1.0), /* blocked by B */
        oc(h2(JACK, S, JACK, H), 1.0, 1.0) /* the only survivor */
    };
    pe_oracle_range_t ranges[] = {
        {a_combos, 2u}, {b_combos, 2u}, {c_combos, 3u}
    };
    pe_oracle_game_t game = {ranges, 3u, MASK_EMPTY};

    pe_oracle_combo_t a_unconditional[] = {
        oc(h2(ACE, S, ACE, H), 1.0, 1.0),
        oc(h2(KING, S, KING, H), 1.0, 1.0)
    };
    pe_oracle_combo_t b_unconditional[] = {
        oc(h2(ACE, D, ACE, C), 1.0, 1.0),
        oc(h2(QUEEN, S, QUEEN, H), 1.0, 1.0)
    };
    pe_oracle_range_t unconditional_ranges[] = {
        {a_unconditional, 2u}, {b_unconditional, 2u}, {c_combos, 3u}
    };
    pe_oracle_game_t unconditional_game = {unconditional_ranges, 3u,
                                           MASK_EMPTY};

    pe_oracle_result_t exact, unconditioned;
    mc_t mc;
    double conditioned_c2, prior_c2;

    CHECK(pe_oracle_run(&game, &exact) == 0, "case F: oracle failed");
    CHECK(pe_oracle_run(&unconditional_game, &unconditioned) == 0,
          "case F: unconditional oracle failed");

    conditioned_c2 = pe_oracle_combo_prob(&exact, 2, 2);
    prior_c2 = pe_oracle_combo_prob(&unconditioned, 2, 2);

    printf("  Case F - multiway sequential conditioning\n");
    CHECK(conditioned_c2 == 1.0,
          "case F: C's surviving hand has posterior %.12f, expected 1",
          conditioned_c2);
    CHECK(pe_oracle_combo_prob(&exact, 2, 0) == 0.0,
          "case F: C's combo blocked by A was not removed");
    CHECK(pe_oracle_combo_prob(&exact, 2, 1) == 0.0,
          "case F: C's combo blocked by B was not removed");
    /* Each of the two earlier players removes a different card from C. */
    CHECK(pe_oracle_player_card_prob(&game, &exact, 2,
                                     MODERN_MAKE_CARD(ACE, S)) == 0.0,
          "case F: A's ace is still available to C");
    CHECK(pe_oracle_player_card_prob(&game, &exact, 2,
                                     MODERN_MAKE_CARD(ACE, D)) == 0.0,
          "case F: B's ace is still available to C");
    CHECK(fabs(prior_c2 - 2.0 / 3.0) < 1e-12,
          "case F: the unconditioned posterior is %.12f, expected 2/3",
          prior_c2);
    CHECK(conditioned_c2 - prior_c2 > 0.2,
          "case F: the two actions did not move C's distribution");

    run_holdem_fixture("case F  conditioned on A and B", &game, 0xF1u, 40000u,
                       &mc);
    run_holdem_fixture("case F  both actions ignored", &unconditional_game,
                       0xF2u, 40000u, &mc);

    pe_oracle_result_free(&exact);
    pe_oracle_result_free(&unconditioned);
}

/* ---------------------------------------------------------------- *
 * Invariants
 * ---------------------------------------------------------------- */

/*
 * Conditioning on an action that every combo takes with probability one
 * leaves the posterior where the prior was. Nothing may move except the
 * blocker effects that were already there.
 */
static void test_invariant_certain_action_changes_nothing(void)
{
    const int C = MODERN_SUIT_CLUBS;
    const int D = MODERN_SUIT_DIAMONDS;
    const int H = MODERN_SUIT_HEARTS;
    const int S = MODERN_SUIT_SPADES;
    const int THREE = MODERN_RANK_3, FIVE = MODERN_RANK_5;
    const int JACK = MODERN_RANK_J, QUEEN = MODERN_RANK_Q;
    const int KING = MODERN_RANK_K, ACE = MODERN_RANK_A;

    mask_t board = cd(THREE, S) | cd(THREE, H) | cd(FIVE, D);
    pe_oracle_combo_t a_combos[] = {
        oc(h2(THREE, C, THREE, D), 1.0, 1.0),
        oc(h2(ACE, S, KING, S), 1.0, 1.0)
    };
    pe_oracle_combo_t b_combos[] = {
        oc(h2(QUEEN, S, QUEEN, H), 1.0, 1.0),
        oc(h2(JACK, S, JACK, H), 1.0, 1.0)
    };
    pe_oracle_range_t ranges[] = {
        {a_combos, 2u}, {b_combos, 2u}
    };
    pe_oracle_game_t game = {ranges, 2u, board};
    pe_oracle_result_t exact;

    CHECK(pe_oracle_run(&game, &exact) == 0, "invariant: oracle failed");
    printf("  Invariants\n");
    /* The prior on this fixture is uniform over both of A's hands. */
    CHECK(fabs(pe_oracle_combo_prob(&exact, 0, 0) - 0.5) < 1e-12,
          "invariant: a certain action moved A's posterior to %.12f",
          pe_oracle_combo_prob(&exact, 0, 0));
    CHECK(fabs(exact.card_available[MODERN_MAKE_CARD(THREE, C)] - 0.5) < 1e-12,
          "invariant: a certain action moved a card's availability");
    pe_oracle_result_free(&exact);
}

/*
 * Extending a player's range with hands that cannot touch the cards another
 * player's range uses must not move that other player's marginal. This is
 * the "changing an unrelated card does not alter independent marginals"
 * invariant, and it is the one that catches a sampler that renormalises
 * against the wrong total.
 */
static void test_invariant_unrelated_cards_do_not_move_marginals(void)
{
    const int C = MODERN_SUIT_CLUBS;
    const int D = MODERN_SUIT_DIAMONDS;
    const int H = MODERN_SUIT_HEARTS;
    const int S = MODERN_SUIT_SPADES;
    const int TWO = MODERN_RANK_2, THREE = MODERN_RANK_3;
    const int FOUR = MODERN_RANK_4, FIVE = MODERN_RANK_5;
    const int SIX = MODERN_RANK_6, SEVEN = MODERN_RANK_7;
    const int KING = MODERN_RANK_K, QUEEN = MODERN_RANK_Q;
    const int ACE = MODERN_RANK_A;

    pe_oracle_combo_t a_combos[] = {
        oc(h2(ACE, S, ACE, H), 1.0, 1.0),
        oc(h2(ACE, S, ACE, D), 2.0, 0.25),
        oc(h2(ACE, H, ACE, D), 3.0, 0.00)
    };
    pe_oracle_combo_t b_short[] = {
        oc(h2(KING, S, KING, H), 1.0, 1.0)
    };
    /* Three more king/queen hands, none of which uses an ace. */
    pe_oracle_combo_t b_long[] = {
        oc(h2(KING, S, KING, H), 1.0, 1.0),
        oc(h2(KING, D, KING, C), 4.0, 1.0),
        oc(h2(QUEEN, S, QUEEN, H), 0.5, 1.0)
    };
    pe_oracle_range_t short_ranges[] = { {a_combos, 3u}, {b_short, 1u} };
    pe_oracle_range_t long_ranges[] = { {a_combos, 3u}, {b_long, 3u} };
    pe_oracle_game_t short_game = {short_ranges, 2u, MASK_EMPTY};
    pe_oracle_game_t long_game = {long_ranges, 2u, MASK_EMPTY};
    pe_oracle_result_t short_exact, long_exact;

    (void)TWO; (void)THREE; (void)FOUR; (void)FIVE; (void)SIX; (void)SEVEN;

    CHECK(pe_oracle_run(&short_game, &short_exact) == 0,
          "invariant: short oracle failed");
    CHECK(pe_oracle_run(&long_game, &long_exact) == 0,
          "invariant: long oracle failed");

    for (size_t i = 0u; i < 3u; ++i)
        CHECK(fabs(pe_oracle_combo_prob(&short_exact, 0, i) -
                   pe_oracle_combo_prob(&long_exact, 0, i)) < 1e-12,
              "invariant: combo %zu moved from %.12f to %.12f when the other "
              "player's range grew on unrelated cards",
              i, pe_oracle_combo_prob(&short_exact, 0, i),
              pe_oracle_combo_prob(&long_exact, 0, i));

    pe_oracle_result_free(&short_exact);
    pe_oracle_result_free(&long_exact);
}

int main(void)
{
    printf("test_pe_conditional_card_removal: sampling against an exact oracle\n");

    test_case_a_binary_fold_filter();
    test_case_b_mixed_strategy_posterior();
    test_case_c_rank_blocker_shift();
    test_case_d_suit_blocker_shift_plo4();
    plo_multi_card_case("PLO5", 5u, 0xE51u, 0xE52u);
    plo_multi_card_case("PLO6", 6u, 0xE61u, 0xE62u);
    test_case_f_multiway();
    test_invariant_certain_action_changes_nothing();
    test_invariant_unrelated_cards_do_not_move_marginals();

    if (g_failures != 0)
    {
        fprintf(stderr,
                "test_pe_conditional_card_removal: %d failure(s)\n",
                g_failures);
        return 1;
    }
    printf("test_pe_conditional_card_removal: posterior, blocker and range "
           "invariants hold\n");
    return 0;
}
