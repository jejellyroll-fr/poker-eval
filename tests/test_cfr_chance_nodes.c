/*
 * @file test_cfr_chance_nodes.c
 * @brief FEAT-03: real chance nodes (average over turn/river runouts)
 *
 * Two checks:
 *  1. Exactness: with check-only play, the solve value over chance runouts
 *     must equal the exhaustive enumeration of all turn/river pairs.
 *  2. Betting smoke: a bet-enabled chance solve terminates, produces a
 *     real number of infosets (proving multiple runouts were traversed) and
 *     respects the always-check value as a lower bound.
 */

#include <poker_eval/engine/solvers/cfr/cfr_core.h>
#include <poker_eval/engine/solvers/cfr/multiway_postflop_adapter.h>
#include <poker_eval/solver/pe_chance.h>
#include <poker_eval/solver/pe_range.h>
#include <poker_eval/core/eval_context.h>
#include <poker_eval/core/modern_cardmask.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond, msg)               \
    do {                               \
        if (!(cond)) {                 \
            fprintf(stderr, "%s\n", msg); \
            return 1;                  \
        }                              \
    } while (0)

static mask_t card(int rank, int suit)
{
    return mask_set(MASK_EMPTY, MODERN_MAKE_CARD(rank, suit));
}

static int in_use(int card, const int *used, int used_count)
{
    for (int i = 0; i < used_count; ++i)
        if (used[i] == card)
            return 1;
    return 0;
}

/* Exhaustive check-to-showdown EV for player 0 (pot pre-invested). */
static double enum_check_ev(const EvalContext *ctx, mask_t h0, mask_t h1,
                            const int *board, int board_count, double pot)
{
    int used[16];
    int used_count = 0;
    for (int c = 0; c < 52; ++c)
        if (mask_is_set(h0 | h1, c))
            used[used_count++] = c;
    for (int i = 0; i < board_count; ++i)
        used[used_count++] = board[i];

    double total = 0.0;
    long runouts = 0;
    for (int t = 0; t < 52; ++t)
    {
        if (in_use(t, used, used_count))
            continue;
        int used_t[17];
        memcpy(used_t, used, sizeof(used));
        used_t[used_count] = t;
        for (int r = 0; r < 52; ++r)
        {
            if (in_use(r, used_t, used_count + 1))
                continue;
            mask_t b7 = MASK_EMPTY;
            for (int i = 0; i < board_count; ++i)
                b7 = mask_set(b7, board[i]);
            b7 = mask_set(b7, t);
            b7 = mask_set(b7, r);
            eval_t v0 = pe_eval_7c(ctx, h0 | b7);
            eval_t v1 = pe_eval_7c(ctx, h1 | b7);
            if (v0 > v1)
                total += pot;
            else if (v0 == v1)
                total += pot / 2.0;
            runouts++;
        }
    }
    return total / (double)runouts - pot / 2.0;
}

static int build_and_solve(const EvalContext *ctx, int chance_enabled,
                           int bet_count, double bet_size, double raiser_cap,
                           int iters, mpf_state_t *state, cfr_game_t *game,
                           cfr_storage_t **out_storage, double *out_expl)
{
    mpf_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    int two_board[3] = {
        MODERN_MAKE_CARD(MODERN_RANK_2, MODERN_SUIT_SPADES),
        MODERN_MAKE_CARD(MODERN_RANK_3, MODERN_SUIT_SPADES),
        MODERN_MAKE_CARD(MODERN_RANK_4, MODERN_SUIT_SPADES)
    };

    cfg.ctx = ctx;
    cfg.rules = MPF_RULE_HOLDEM;
    cfg.num_players = 2;
    cfg.button_index = 0;
    cfg.start_street = MPF_STREET_FLOP;
    cfg.hole[0] = card(MODERN_RANK_A, MODERN_SUIT_HEARTS) | card(MODERN_RANK_K, MODERN_SUIT_HEARTS);
    cfg.hole_specified[0] = 1;
    cfg.hole[1] = card(MODERN_RANK_7, MODERN_SUIT_CLUBS) | card(MODERN_RANK_7, MODERN_SUIT_DIAMONDS);
    cfg.hole_specified[1] = 1;
    memcpy(cfg.board_cards, two_board, sizeof(two_board));
    cfg.board_card_count = 5; /* turn/river ignored when chance is enabled */
    cfg.stacks[0] = 100.0;
    cfg.stacks[1] = 100.0;
    cfg.sb = 0.5;
    cfg.bb = 1.0;
    cfg.ante = 0.0;
    cfg.preflop.defined = 1;
    cfg.preflop.has_invested = 1;
    cfg.preflop.invested[0] = 10.0;
    cfg.preflop.invested[1] = 10.0;
    cfg.enable_chance_nodes = chance_enabled;
    cfg.bet_size_count_common = bet_count;
    if (bet_count > 0)
    {
        cfg.bet_sizes_common[0] = bet_size;
        cfg.raise_cap = raiser_cap >= 0 ? (int)raiser_cap : 0;
    }
    else
    {
        cfg.raise_cap = 0;
    }

    if (mpf_build_game(&cfg, game, state) != 0)
        return -1;

    cfr_storage_t *storage = cfr_storage_create();
    if (!storage)
        return -1;

    cfr_config_t solve_cfg;
    memset(&solve_cfg, 0, sizeof(solve_cfg));
    solve_cfg.max_iterations = iters;
    *out_expl = 0.0;
    cfr_solve(game, storage, &solve_cfg, out_expl);
    *out_storage = storage;
    return 0;
}

/*
 * CHN-01: a chance node says what kind it is.
 *
 * The v2 model answered "is a card pending?" with a boolean, and RNG-03 added
 * a second one. Two flags describing four possible node kinds is a state
 * machine nobody wrote down; naming the kinds makes the differences checkable.
 *
 * The one that matters operationally: chance_children[52] is indexed by card,
 * so it is only meaningful for PE_CHANCE_BOARD_ONE. A node kind that is merely
 * implied cannot carry that constraint.
 */
static StdDeck_CardMask no_dead_mask(void)
{
    StdDeck_CardMask m;
    StdDeck_CardMask_RESET(m);
    return m;
}

static int test_chance_kinds(void)
{
    EvalConfig eval_cfg = eval_config_holdem();
    EvalContext *ctx = eval_context_create(&eval_cfg);
    mpf_config_t cfg;
    cfr_game_t game;
    mpf_state_t root;
    pe_range_t *aa = NULL;
    int failures = 0;

    if (!ctx)
        return 1;

    /* A turn state with chance enabled deals one board card. */
    memset(&cfg, 0, sizeof(cfg));
    cfg.ctx = ctx;
    cfg.rules = MPF_RULE_HOLDEM;
    cfg.num_players = 2;
    cfg.start_street = MPF_STREET_TURN;
    cfg.board_cards[0] = 20; cfg.board_cards[1] = 15; cfg.board_cards[2] = 10;
    cfg.board_cards[3] = 5;
    cfg.board_card_count = 4;
    cfg.stacks[0] = 100.0; cfg.stacks[1] = 100.0;
    cfg.sb = 0.5; cfg.bb = 1.0;
    cfg.bet_sizes_common[0] = 0.75;
    cfg.bet_size_count_common = 1;
    cfg.raise_cap = 1;
    cfg.enable_pot_sizing = 1;
    cfg.enable_chance_nodes = 1;
    cfg.hole[0] = mask_set(mask_set(MASK_EMPTY, 51), 46);
    cfg.hole[1] = mask_set(mask_set(MASK_EMPTY, 40), 33);
    cfg.hole_specified[0] = 1;
    cfg.hole_specified[1] = 1;

    if (mpf_build_game(&cfg, &game, &root) != 0)
    {
        fprintf(stderr, "FAILED: turn build\n");
        eval_context_destroy(ctx);
        return 1;
    }
    /* The root of a turn game is a decision, not a deal; the deal appears once
       the betting round completes. What must hold here is that a state with no
       pending card reports NONE rather than a stale flag. */
    if (mpf_state_chance_kind(&root) != PE_CHANCE_NONE)
    {
        fprintf(stderr, "FAILED: a decision node reports %s\n",
                pe_chance_kind_name(mpf_state_chance_kind(&root)));
        failures++;
    }
    mpf_state_cleanup(&root);

    /* A root with a range wider than one combo deals the private hands. */
    if (pe_solver_range_parse(game_holdem, "AA", no_dead_mask(), &aa) != PE_SOLVER_OK)
    {
        fprintf(stderr, "FAILED: range parse\n");
        eval_context_destroy(ctx);
        return failures + 1;
    }
    memset(&cfg, 0, sizeof(cfg));
    cfg.ctx = ctx;
    cfg.rules = MPF_RULE_HOLDEM;
    cfg.num_players = 2;
    cfg.start_street = MPF_STREET_RIVER;
    cfg.board_cards[0] = 20; cfg.board_cards[1] = 15; cfg.board_cards[2] = 10;
    cfg.board_cards[3] = 5;  cfg.board_cards[4] = 1;
    cfg.board_card_count = 5;
    cfg.stacks[0] = 100.0; cfg.stacks[1] = 100.0;
    cfg.sb = 0.5; cfg.bb = 1.0;
    cfg.bet_sizes_common[0] = 0.75;
    cfg.bet_size_count_common = 1;
    cfg.raise_cap = 1;
    cfg.enable_pot_sizing = 1;
    cfg.range[0] = aa;
    cfg.hole[1] = mask_set(mask_set(MASK_EMPTY, 40), 33);
    cfg.hole_specified[1] = 1;

    if (mpf_build_game(&cfg, &game, &root) != 0)
    {
        fprintf(stderr, "FAILED: range build\n");
        pe_range_free(aa);
        eval_context_destroy(ctx);
        return failures + 1;
    }
    if (mpf_state_chance_kind(&root) != PE_CHANCE_PRIVATE_HANDS)
    {
        fprintf(stderr, "FAILED: a range root reports %s, expected private-hands\n",
                pe_chance_kind_name(mpf_state_chance_kind(&root)));
        failures++;
    }
    /* And its outcomes are deals, not cards — which is exactly why the
       card-indexed cache must not be used for this kind. */
    if (game.get_chance_outcomes(&game, (uint64_t)(uintptr_t)game.initial_state, NULL)
        != root.private_deal_count)
    {
        fprintf(stderr, "FAILED: outcome count is not the deal count\n");
        failures++;
    }
    mpf_state_cleanup(&root);
    pe_range_free(aa);

    if (mpf_state_chance_kind(NULL) != PE_CHANCE_NONE)
    {
        fprintf(stderr, "FAILED: a NULL state is a chance node\n");
        failures++;
    }
    if (pe_chance_kind_name(PE_CHANCE_BOARD_ONE) == NULL ||
        pe_chance_kind_name(PE_CHANCE_KIND_COUNT) != NULL)
    {
        fprintf(stderr, "FAILED: chance kind naming\n");
        failures++;
    }

    eval_context_destroy(ctx);
    if (failures == 0)
        printf("  chance kinds reported correctly\n");
    return failures;
}

int main(void)
{
    EvalConfig ecfg = eval_config_holdem();
    EvalContext *ctx = eval_context_create(&ecfg);
    CHECK(ctx != NULL, "EvalContext create");

    int board[3] = {
        MODERN_MAKE_CARD(MODERN_RANK_2, MODERN_SUIT_SPADES),
        MODERN_MAKE_CARD(MODERN_RANK_3, MODERN_SUIT_SPADES),
        MODERN_MAKE_CARD(MODERN_RANK_4, MODERN_SUIT_SPADES)
    };
    mask_t h0 = card(MODERN_RANK_A, MODERN_SUIT_HEARTS) | card(MODERN_RANK_K, MODERN_SUIT_HEARTS);
    mask_t h1 = card(MODERN_RANK_7, MODERN_SUIT_CLUBS) | card(MODERN_RANK_7, MODERN_SUIT_DIAMONDS);
    double pot = 20.0;
    double ev_check = enum_check_ev(ctx, h0, h1, board, 3, pot);

    /* 1. Exactness: check-only game, chance runouts. */
    mpf_state_t state;
    cfr_game_t game;
    cfr_storage_t *storage = NULL;
    double expl = 0.0;
    CHECK(build_and_solve(ctx, 1, 0, 0.0, 0, 5, &state, &game, &storage, &expl) == 0,
          "chance check-only build/solve");
    double br0 = cfr_best_response_value(&game, storage, 0, NULL);
    double br1 = cfr_best_response_value(&game, storage, 1, NULL);
    CHECK(fabs(br0 - ev_check) < 1e-6, "P0 check EV must match exhaustive enumeration");
    CHECK(fabs(br1 + ev_check) < 1e-6, "P1 check EV must be zero-sum");
    CHECK(fabs(br0 + br1) < 1e-6, "check-only game must be zero-sum");
    printf(" EXACT: check-EV=%.6f br0=%.6f br1=%.6f infosets=%zu\n",
           ev_check, br0, br1, cfr_storage_count_infosets(storage));
    cfr_storage_destroy(storage);
    mpf_state_cleanup(&state);

    /* 2. Betting smoke: chance solve with one bet size. */
    storage = NULL;
    CHECK(build_and_solve(ctx, 1, 1, 10.0, 1, 30, &state, &game, &storage, &expl) == 0,
          "chance betting build/solve");
    CHECK(isfinite(expl), "exploitability must be finite");
    size_t infosets = cfr_storage_count_infosets(storage);
    CHECK(infosets > 90, "chance solve must traverse many runouts (infoset count)");
    CHECK(infosets < 100000, "infoset count must stay bounded");
    br0 = cfr_best_response_value(&game, storage, 0, NULL);
    cfr_policy_value_result_t policy;
    CHECK(cfr_compute_policy_values_detailed(&game, storage, NULL, &policy) == 0,
          "policy value computation");
    CHECK(br0 + 1e-6 >= policy.ev[0],
          "P0 best response must not be below P0 average-policy value");
    printf(" BET: expl=%.4f infosets=%zu br0=%.6f policy0=%.6f\n",
           expl, infosets, br0, policy.ev[0]);
    cfr_storage_destroy(storage);
    mpf_state_cleanup(&state);

    /* 3. Multiway chance smoke (policy values + N-player BR). */
    cfr_storage_t *mstor = cfr_storage_create();
    CHECK(mstor != NULL, "multiway storage");
    mpf_config_t mcfg;
    memset(&mcfg, 0, sizeof(mcfg));
    mcfg.ctx = ctx;
    mcfg.rules = MPF_RULE_HOLDEM;
    mcfg.num_players = 3;
    mcfg.button_index = 0;
    mcfg.start_street = MPF_STREET_FLOP;
    mcfg.board_card_count = 5;
    memcpy(mcfg.board_cards, board, sizeof(board));
    mcfg.preflop.defined = 1;
    mcfg.preflop.has_invested = 1;
    mcfg.preflop.invested[0] = 10.0;
    mcfg.preflop.invested[1] = 10.0;
    mcfg.preflop.invested[2] = 10.0;
    mcfg.preflop.has_pot = 1;
    mcfg.preflop.pot = 30.0;
    mcfg.stacks[0] = mcfg.stacks[1] = mcfg.stacks[2] = 100.0;
    mcfg.hole[0] = h0;
    mcfg.hole_specified[0] = 1;
    mcfg.hole[1] = h1;
    mcfg.hole_specified[1] = 1;
    mcfg.hole[2] = card(MODERN_RANK_Q, MODERN_SUIT_HEARTS) | card(MODERN_RANK_J, MODERN_SUIT_HEARTS);
    mcfg.hole_specified[2] = 1;
    mcfg.enable_chance_nodes = 1;
    mcfg.bet_size_count_common = 0;
    mcfg.raise_cap = 0;
    mpf_state_t mstate;
    cfr_game_t mgame;
    CHECK(mpf_build_game(&mcfg, &mgame, &mstate) == 0, "multiway chance build");
    cfr_config_t mcfg_solve;
    memset(&mcfg_solve, 0, sizeof(mcfg_solve));
    mcfg_solve.max_iterations = 10;
    double mexp = 0.0;
    cfr_solve(&mgame, mstor, &mcfg_solve, &mexp);
    cfr_exploitability_result_t mres;
    CHECK(cfr_exploitability_multiway(&mgame, mstor, NULL, &mres) == 0, "multiway expl");
    CHECK(mres.num_players == 3, "multiway player count");
    printf(" MULTIWAY: total_expl=%.4f infosets=%zu\n", mres.total_exploitability,
           cfr_storage_count_infosets(mstor));
    cfr_storage_destroy(mstor);
    mpf_state_cleanup(&mstate);

    eval_context_destroy(ctx);

    if (test_chance_kinds() != 0)
    {
        fprintf(stderr, "FAILED: chance kinds\n");
        return 1;
    }

    printf("PASSED\n");
    return 0;
}
