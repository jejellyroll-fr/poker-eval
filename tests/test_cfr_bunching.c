/*
 * @file test_cfr_bunching.c
 * @brief FEAT-14 (#150): folded-range card bunching effect estimator
 *
 * Checks:
 *  1. Weight unit: with a folded player modeled by a range distribution, the
 *     chance-deal weights equal the normalized card-survival probabilities
 *     (depleted cards get strictly smaller weights, weights sum to 1), and
 *     disabling the estimator restores uniform weights.
 *  2. Solver integration: a check-only weighted solve matches an independent
 *     manual weighted expectation over all turn/river runouts (best response
 *     and policy value).
 *  3. 2+ preflop folds: with two folded players whose ranges favor spades,
 *     spade deal weights are depressed, the solve EV shifts versus the
 *     uniform-chance solve, and the 2-active-player game stays zero-sum.
 */

#include <poker_eval/engine/solvers/cfr/cfr_core.h>
#include <poker_eval/engine/solvers/cfr/multiway_postflop_adapter.h>
#include <poker_eval/core/eval_context.h>
#include <poker_eval/core/modern_cardmask.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond, msg)                 \
    do                                   \
    {                                    \
        if (!(cond))                     \
        {                                \
            fprintf(stderr, "%s\n", msg); \
            return 1;                    \
        }                                \
    } while (0)

static mask_t card(int rank, int suit)
{
    return mask_set(MASK_EMPTY, MODERN_MAKE_CARD(rank, suit));
}

static int card_suit(int c)
{
    return c / 13;
}

/* Independent weighted expectation over the game tree: at chance nodes the
 * adapter's own per-outcome weights are used, at player nodes every action is
 * averaged (check-only games have exactly one action). Returns player 0's EV. */
static double manual_weighted_value(cfr_game_t *game, uint64_t key)
{
    if (game->is_terminal(game, key, NULL))
        return game->get_utility(game, key, 0, NULL);
    if (game->is_chance && game->is_chance(game, key, NULL))
    {
        int n = game->get_chance_outcomes ? game->get_chance_outcomes(game, key, NULL) : 0;
        if (n <= 0 || !game->apply_chance)
            return 0.0;
        double v = 0.0;
        double wsum = 0.0;
        for (int c = 0; c < n; ++c)
        {
            double w = cfr_chance_weight(game, key, c, NULL);
            wsum += w;
            uint64_t ck = game->apply_chance(game, key, c, NULL);
            v += w * manual_weighted_value(game, ck);
            if (game->release_state)
                game->release_state(game, ck, NULL);
        }
        return wsum > 0.0 ? v / wsum : v / (double)n;
    }
    int actions[16];
    int n = game->get_actions(game, key, actions, 16, NULL);
    if (n <= 0)
        return 0.0;
    double v = 0.0;
    for (int a = 0; a < n; ++a)
    {
        uint64_t ck = game->apply_action(game, key, actions[a], NULL);
        v += manual_weighted_value(game, ck);
        if (game->release_state)
            game->release_state(game, ck, NULL);
    }
    return v / (double)n;
}

static void fill_bunching_config(mpf_config_t *cfg, const EvalContext *ctx,
                                 int num_players, int active_count,
                                 const double folded_prob[52],
                                 int bunching, int *board3)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->ctx = ctx;
    cfg->rules = MPF_RULE_HOLDEM;
    cfg->num_players = num_players;
    cfg->button_index = 0;
    cfg->start_street = MPF_STREET_FLOP;
    for (int i = 0; i < 3; ++i)
        cfg->board_cards[i] = board3[i];
    cfg->board_card_count = 3; /* turn/river dealt by chance */
    cfg->preflop.defined = 1;
    cfg->preflop.has_active = 1;
    for (int i = 0; i < num_players; ++i)
        cfg->preflop.active[i] = (i < active_count) ? 1 : 0;
    cfg->preflop.has_invested = 1;
    for (int i = 0; i < num_players; ++i)
        cfg->preflop.invested[i] = (i < active_count) ? 10.0 : 0.0;
    cfg->preflop.has_pot = 1;
    cfg->preflop.pot = (double)active_count * 20.0;
    for (int i = 0; i < num_players; ++i)
        cfg->stacks[i] = 100.0;
    cfg->hole[0] = card(MODERN_RANK_A, MODERN_SUIT_HEARTS) | card(MODERN_RANK_K, MODERN_SUIT_HEARTS);
    cfg->hole_specified[0] = 1;
    if (active_count >= 2)
    {
        cfg->hole[1] = card(MODERN_RANK_7, MODERN_SUIT_CLUBS) | card(MODERN_RANK_7, MODERN_SUIT_DIAMONDS);
        cfg->hole_specified[1] = 1;
    }
    cfg->enable_chance_nodes = 1;
    cfg->bet_size_count_common = 0;
    cfg->raise_cap = 0;
    cfg->enable_card_bunching = bunching ? 1 : 0;
    for (int p = active_count; p < num_players; ++p)
    {
        cfg->folded_range_provided[p] = 1;
        for (int c = 0; c < 52; ++c)
            cfg->folded_range_prob[p][c] = folded_prob[c];
    }
}

/* Descend from the root by playing each player's first action (check/call,
 * the only action in a check-only game) until the first chance node is
 * reached. Returns 0 on success, 1 on error. */
static int walk_to_first_chance(cfr_game_t *game, uint64_t root, uint64_t *out)
{
    uint64_t key = root;
    for (int depth = 0; depth < 16; ++depth)
    {
        if (game->is_chance && game->is_chance(game, key, NULL))
        {
            *out = key;
            return 0;
        }
        int actions[16];
        int n = game->get_actions(game, key, actions, 16, NULL);
        if (n <= 0)
            return 1;
        key = game->apply_action(game, key, actions[0], NULL);
    }
    return 1;
}

/* Build a chance-node game with one or more preflop-folded players modeled by
 * ranges. active_count players 0..active_count-1 are active and hold the fixed
 * holes AhKh / 7c7d; the rest fold. folded_prob[52] is copied to every folded
 * seat. Returns through out_state / out_game; caller must mpf_state_cleanup(). */
static int build_bunching_game(const EvalContext *ctx,
                               int num_players, int active_count,
                               const double folded_prob[52],
                               int bunching, int *board3,
                               mpf_state_t *out_state, cfr_game_t *out_game)
{
    mpf_config_t cfg;
    fill_bunching_config(&cfg, ctx, num_players, active_count, folded_prob,
                         bunching, board3);
    return mpf_build_game(&cfg, out_game, out_state);
}

static void fill_uniform(double *prob, double favored, double other)
{
    for (int c = 0; c < 52; ++c)
        prob[c] = (card_suit(c) == MODERN_SUIT_HEARTS) ? favored : other;
}

static void fill_spade_favored(double *prob, double favored, double other)
{
    for (int c = 0; c < 52; ++c)
        prob[c] = (card_suit(c) == MODERN_SUIT_SPADES) ? favored : other;
}

int main(void)
{
    EvalConfig ecfg = eval_config_holdem();
    EvalContext *ctx = eval_context_create(&ecfg);
    CHECK(ctx != NULL, "EvalContext create");

    int board3[3] = {
        MODERN_MAKE_CARD(MODERN_RANK_2, MODERN_SUIT_SPADES),
        MODERN_MAKE_CARD(MODERN_RANK_3, MODERN_SUIT_SPADES),
        MODERN_MAKE_CARD(MODERN_RANK_4, MODERN_SUIT_SPADES)
    };

    /* ---- 1. weight unit test: one folded player, hearts favored ---- */
    double hearts_prob[52];
    fill_uniform(hearts_prob, 0.9, 0.05);
    mpf_state_t state;
    cfr_game_t game;
    CHECK(build_bunching_game(ctx, 3, 2, hearts_prob, 1, board3, &state, &game) == 0,
          "bunching game build");
    CHECK(game.get_chance_weight != NULL, "get_chance_weight wired");

    /* Expected survival: hearts 0.10, others 0.95. */
    uint64_t root_key = (uint64_t)(uintptr_t)game.initial_state;
    CHECK(walk_to_first_chance(&game, root_key, &root_key) == 0,
          "flop betting round must end in a turn chance node");
    CHECK(game.is_chance && game.is_chance(&game, root_key, NULL),
          "root must be a chance node (turn)");
    int outcomes = game.get_chance_outcomes(&game, root_key, NULL);
    CHECK(outcomes > 40, "turn outcome count plausible");
    double sum_w = 0.0;
    double sum_w_hearts = 0.0;
    double sum_w_others = 0.0;
    int n_hearts = 0;
    int n_others = 0;
    double survival_expect[52];
    for (int c = 0; c < 52; ++c)
        survival_expect[c] = (card_suit(c) == MODERN_SUIT_HEARTS) ? 0.10 : 0.95;

    /* unused cards: 52 - 4 hole (AhKh, 7c7d) - 3 board = 45, enumerated in
       the same ascending-card order the adapter uses. */
    {
        int unused[52];
        int unused_count = 0;
        for (int c = 0; c < 52; ++c)
        {
            if (c == MODERN_MAKE_CARD(MODERN_RANK_A, MODERN_SUIT_HEARTS) ||
                c == MODERN_MAKE_CARD(MODERN_RANK_K, MODERN_SUIT_HEARTS) ||
                c == MODERN_MAKE_CARD(MODERN_RANK_7, MODERN_SUIT_CLUBS) ||
                c == MODERN_MAKE_CARD(MODERN_RANK_7, MODERN_SUIT_DIAMONDS))
                continue;
            if (c == board3[0] || c == board3[1] || c == board3[2])
                continue;
            unused[unused_count++] = c;
        }
        CHECK(unused_count == 45, "turn has 45 unused cards");
        double expect_sum = 0.0;
        for (int i = 0; i < unused_count; ++i)
            expect_sum += survival_expect[unused[i]];
        CHECK(expect_sum > 0.0, "survival normalization denominator nonzero");
        CHECK(outcomes == unused_count, "outcome count matches unused cards");
        for (int o = 0; o < outcomes; ++o)
        {
            int c = unused[o];
            double expect = survival_expect[c] / expect_sum;
            double w = cfr_chance_weight(&game, root_key, o, NULL);
            CHECK(fabs(w - expect) < 1e-12, "weight must equal normalized survival");
            sum_w += w;
            if (card_suit(c) == MODERN_SUIT_HEARTS)
            {
                sum_w_hearts += w;
                n_hearts++;
            }
            else
            {
                sum_w_others += w;
                n_others++;
            }
        }
        CHECK(n_hearts == 11, "11 hearts unseen");
        CHECK(n_others == 34, "34 non-hearts unseen");
        CHECK(fabs(sum_w - 1.0) < 1e-9, "chance weights of a node must sum to 1");
        CHECK(sum_w_hearts / (double)n_hearts < sum_w_others / (double)n_others,
              "depleted (hearts) cards must have strictly smaller deal weights");
        printf(" WEIGHTS: hearts mean %.6f < others mean %.6f (sum=%.6f)\n",
               sum_w_hearts / (double)n_hearts, sum_w_others / (double)n_others, sum_w);
    }
    mpf_state_cleanup(&state);

    /* Disabled estimator restores uniform weights. */
    CHECK(build_bunching_game(ctx, 3, 2, hearts_prob, 0, board3, &state, &game) == 0,
          "uniform game build");
    root_key = (uint64_t)(uintptr_t)game.initial_state;
    CHECK(walk_to_first_chance(&game, root_key, &root_key) == 0,
          "uniform: reach turn chance node");
    outcomes = game.get_chance_outcomes(&game, root_key, NULL);
    for (int o = 0; o < outcomes; ++o)
        CHECK(cfr_chance_weight(&game, root_key, o, NULL) == 1.0,
              "bunching off -> weight 1.0 (uniform)");
    mpf_state_cleanup(&state);

    /* ---- 2. solver integration: manual weighted EV vs best response ---- */
    {
        double border_prob[52];
        fill_uniform(border_prob, 0.9, 0.05); /* one folded player, hearts favored */
        CHECK(build_bunching_game(ctx, 3, 2, border_prob, 1, board3, &state, &game) == 0,
              "integration game build");
        root_key = (uint64_t)(uintptr_t)game.initial_state;
        double manual = manual_weighted_value(&game, root_key);
        cfr_storage_t *storage = cfr_storage_create();
        CHECK(storage != NULL, "storage create");
        cfr_config_t solve_cfg;
        memset(&solve_cfg, 0, sizeof(solve_cfg));
        solve_cfg.max_iterations = 10;
        double expl = 0.0;
        cfr_solve(&game, storage, &solve_cfg, &expl);
        double br0 = cfr_best_response_value_multiway(&game, storage, 0, NULL);
        /* check-only: best response must equal manual weighted EV */
        CHECK(fabs(br0 - manual) < 1e-6,
              "best response must match manual weighted expectation");
        CHECK(isfinite(expl), "exploitability finite");
        cfr_storage_destroy(storage);
        printf(" INTEGRATION: manual=%.6f br0=%.6f\n", manual, br0);
        mpf_state_cleanup(&state);
    }

    /* ---- 3. two folded players, spade-favored ranges ---- */
    {
        double spade_prob[52];
        fill_spade_favored(spade_prob, 0.85, 0.02);
        mpf_state_t st_b; /* bunching */
        mpf_state_t st_u; /* uniform  */
        cfr_game_t g_b;
        cfr_game_t g_u;
        CHECK(build_bunching_game(ctx, 4, 2, spade_prob, 1, board3, &st_b, &g_b) == 0,
              "4-player bunching build");
        CHECK(build_bunching_game(ctx, 4, 2, spade_prob, 0, board3, &st_u, &g_u) == 0,
              "4-player uniform build");

        /* deal-weight bias: spades depressed */
        root_key = (uint64_t)(uintptr_t)g_b.initial_state;
        CHECK(walk_to_first_chance(&g_b, root_key, &root_key) == 0,
              "bunching: reach turn chance node");
        outcomes = g_b.get_chance_outcomes(&g_b, root_key, NULL);
        {
            int unused_b[52];
            int unused_count = 0;
            for (int c = 0; c < 52; ++c)
            {
                if (c == MODERN_MAKE_CARD(MODERN_RANK_A, MODERN_SUIT_HEARTS) ||
                    c == MODERN_MAKE_CARD(MODERN_RANK_K, MODERN_SUIT_HEARTS) ||
                    c == MODERN_MAKE_CARD(MODERN_RANK_7, MODERN_SUIT_CLUBS) ||
                    c == MODERN_MAKE_CARD(MODERN_RANK_7, MODERN_SUIT_DIAMONDS))
                    continue;
                if (c == board3[0] || c == board3[1] || c == board3[2])
                    continue;
                unused_b[unused_count++] = c;
            }
            CHECK(unused_count == outcomes, "outcome mapping consistent");
            double w_spade = 0.0;
            double w_other = 0.0;
            int n_spade = 0;
            int n_other = 0;
            for (int o = 0; o < outcomes; ++o)
            {
                double w = cfr_chance_weight(&g_b, root_key, o, NULL);
                if (card_suit(unused_b[o]) == MODERN_SUIT_SPADES)
                {
                    w_spade += w;
                    n_spade++;
                }
                else
                {
                    w_other += w;
                    n_other++;
                }
            }
            CHECK(n_spade == 10, "10 spades unseen (3 spades on board)");
            CHECK(n_other == 35, "35 non-spades unseen");
            CHECK(w_spade / (double)n_spade < w_other / (double)n_other,
                  "spades (depleted by both folds) must be deal-depressed");
            printf(" BIAS: spade mean %.6f < other mean %.6f\n",
                   w_spade / (double)n_spade, w_other / (double)n_other);

            /* config-level survival API: survival = (1-f)^2 per suit */
            mpf_config_t survival_cfg;
            fill_bunching_config(&survival_cfg, ctx, 4, 2, spade_prob, 1, board3);
            double survival[52];
            mpf_bunching_compute_survival(&survival_cfg, survival);
            int spade_card = MODERN_MAKE_CARD(MODERN_RANK_5, MODERN_SUIT_SPADES);
            int club_card = MODERN_MAKE_CARD(MODERN_RANK_5, MODERN_SUIT_CLUBS);
            CHECK(fabs(survival[spade_card] - 0.0225) < 1e-12,
                  "spade survival must be (1-0.85)^2");
            CHECK(fabs(survival[club_card] - 0.9604) < 1e-12,
                  "non-spade survival must be (1-0.02)^2");
            printf(" SURVIVAL: spade %.4f, club %.4f\n",
                   survival[spade_card], survival[club_card]);
        }

        /* EV shift + zero-sum over the two active players */
        cfr_storage_t *s_b = cfr_storage_create();
        cfr_storage_t *s_u = cfr_storage_create();
        CHECK(s_b && s_u, "storages");
        cfr_config_t solve_cfg;
        memset(&solve_cfg, 0, sizeof(solve_cfg));
        solve_cfg.max_iterations = 15;
        double expl_b = 0.0;
        double expl_u = 0.0;
        cfr_solve(&g_b, s_b, &solve_cfg, &expl_b);
        cfr_solve(&g_u, s_u, &solve_cfg, &expl_u);
        double br0_b = cfr_best_response_value_multiway(&g_b, s_b, 0, NULL);
        double br1_b = cfr_best_response_value_multiway(&g_b, s_b, 1, NULL);
        double br0_u = cfr_best_response_value_multiway(&g_u, s_u, 0, NULL);
        CHECK(fabs(br0_b + br1_b) < 1e-6, "bunching solve stays zero-sum");
        CHECK(fabs(br0_b - br0_u) > 1e-4,
              "card bunching must shift the solve EV vs uniform chance deals");
        printf(" EV: bunching br0=%.6f vs uniform br0=%.6f (delta %.6f)\n",
               br0_b, br0_u, br0_b - br0_u);
        cfr_storage_destroy(s_b);
        cfr_storage_destroy(s_u);
        mpf_state_cleanup(&st_b);
        mpf_state_cleanup(&st_u);
    }

    eval_context_destroy(ctx);
    printf("PASSED\n");
    return 0;
}