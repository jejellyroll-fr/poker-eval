/**
 * @file test_mpf_rake.c
 * @brief SOLVER-02: terminal utilities must apply rake from the tree config
 *
 * Verifies that rake_config_t in mpf_config_t reduces terminal utilities by
 * exactly min(percentage * pot, cap), on folds and at showdown, that
 * no_flop_no_drop skips rake preflop, and that solved frequencies shift.
 */

#include <poker_eval/engine/solvers/cfr/multiway_postflop_adapter.h>
#include <poker_eval/engine/solvers/cfr/cfr_core.h>
#include <poker_eval/core/eval_context.h>
#include <poker_eval/core/modern_cardmask.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define CHECK(cond, msg)               \
    do {                               \
        if (!(cond)) {                 \
            fprintf(stderr, "FAIL: %s\n", msg); \
            goto fail;                 \
        }                              \
    } while (0)

/* Two-card hole mask */
static mask_t hole2(int r0, int s0, int r1, int s1)
{
    mask_t m = MASK_EMPTY;
    m = mask_set(m, MODERN_MAKE_CARD(r0, s0));
    m = mask_set(m, MODERN_MAKE_CARD(r1, s1));
    return m;
}

static void setup_common(mpf_config_t *cfg, EvalContext *ctx)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->ctx = ctx;
    cfg->rules = MPF_RULE_HOLDEM;
    cfg->num_players = 2;
    cfg->button_index = 0;
    cfg->start_street = MPF_STREET_PREFLOP;
    cfg->sb = 0.5;
    cfg->bb = 1.0;
    cfg->ante = 0.0;
    cfg->raise_cap = 4;
    cfg->enable_pot_sizing = 0;
    cfg->bet_size_count_common = 1;
    cfg->bet_sizes_common[0] = 3.0;
    cfg->stacks[0] = 100.0;
    cfg->stacks[1] = 100.0;
}

/* Fold-terminal: player 0 folded, player 1 wins the pot. */
static int fold_terminal(EvalContext *ctx, const rake_config_t *rake,
                         double inv, double pot, int street,
                         double *out_u0, double *out_u1)
{
    mpf_config_t cfg;
    cfr_game_t game;
    mpf_state_t state;
    uint64_t key;
    setup_common(&cfg, ctx);
    if (rake)
        cfg.rake = *rake;

    cfg.start_street = (mpf_street_t)street;
    cfg.preflop.defined = 1;
    cfg.preflop.has_active = 1;
    cfg.preflop.active[0] = 0;
    cfg.preflop.active[1] = 1;
    cfg.preflop.has_invested = 1;
    cfg.preflop.invested[0] = inv;
    cfg.preflop.invested[1] = inv;
    cfg.preflop.has_round = 1;
    cfg.preflop.round_contrib[0] = inv;
    cfg.preflop.round_contrib[1] = inv;
    cfg.preflop.has_pot = 1;
    cfg.preflop.pot = pot;
    cfg.preflop.has_to_call = 1;
    cfg.preflop.to_call = 0.0;
    cfg.preflop.has_current_bet = 1;
    cfg.preflop.current_bet = 0.0;
    cfg.preflop.has_to_act = 1;
    cfg.preflop.to_act = 1;

    if (mpf_build_game(&cfg, &game, &state) != 0)
        return -1;
    key = (uint64_t)(uintptr_t)&state;
    if (!game.is_terminal(&game, key, NULL))
        return -1;
    *out_u0 = game.get_utility(&game, key, 0, NULL);
    *out_u1 = game.get_utility(&game, key, 1, NULL);
    mpf_state_cleanup(&state);
    return 0;
}

/* Callback to read the average strategy of the root infoset */
typedef struct {
    uint64_t root_key;
    int found;
    double p_call;
} root_strategy_t;

static void root_strategy_cb(uint64_t key, int n_actions,
                             const double *regret, const double *avg_strategy,
                             void *user_data)
{
    (void)regret;
    root_strategy_t *rs = (root_strategy_t *)user_data;
    if (rs->found || key != rs->root_key)
        return;
    /* Actions at the river root are [fold, call] (no raises configured) */
    rs->found = 1;
    rs->p_call = (n_actions > 1) ? avg_strategy[1] : NAN;
}

int main(void)
{
    EvalContext *ctx = NULL;
    EvalConfig ecfg;
    rake_config_t rake = {0.05, 3.0, 0.0, 0};

    ecfg = eval_config_holdem();
    ctx = eval_context_create(&ecfg);
    CHECK(ctx != NULL, "EvalContext create");

    /* ---------- 1. Fold terminal, exact rake ---------- */
    {
        double u00, u01, u10, u11;
        rake_config_t none = {0.0, 0.0, 0.0, 0};

        CHECK(fold_terminal(ctx, &none, 10.0, 20.0, MPF_STREET_PREFLOP, &u00, &u01) == 0,
              "fold no-rake build");
        CHECK(fold_terminal(ctx, &rake, 10.0, 20.0, MPF_STREET_PREFLOP, &u10, &u11) == 0,
              "fold rake build");

        /* 5% of 20 = 1.0, below the 3.0 cap */
        CHECK(fabs((u01 - u11) - 1.0) < 1e-9, "fold utility reduced by exactly min(0.05*pot,3)");
        CHECK(fabs(u00 - u10) < 1e-9, "loser utility untouched by rake");

        /* Cap: 5% of 200 = 10 > 3 -> rake exactly 3.0 */
        CHECK(fold_terminal(ctx, &rake, 100.0, 200.0, MPF_STREET_PREFLOP, &u00, &u01) == 0,
              "fold cap build");
        CHECK(fabs(u01 - (-100.0 + 200.0 - 3.0)) < 1e-9, "cap at 3.0 applied exactly");

        /* No-flop-no-drop: preflop fold pays no rake */
        rake_config_t nfnd = {0.05, 3.0, 0.0, 1};
        CHECK(fold_terminal(ctx, &nfnd, 10.0, 20.0, MPF_STREET_PREFLOP, &u00, &u01) == 0,
              "nofnd build");
        CHECK(fabs(u01 - (-10.0 + 20.0)) < 1e-9, "no-flop-no-drop skips rake preflop");

        /* Same hand on the FLOP: rake applies again */
        CHECK(fold_terminal(ctx, &nfnd, 10.0, 20.0, MPF_STREET_FLOP, &u00, &u01) == 0,
              "flop fold build");
        CHECK(fabs(u01 - (-10.0 + 20.0 - 1.0)) < 1e-9, "rake applies postflop with no-flop-no-drop");
        printf("RAKE: fold-terminal exact utilities OK\n");
    }

    /* ---------- 2. Showdown terminal, exact rake ---------- */
    {
        int board[5] = {
            MODERN_MAKE_CARD(MODERN_RANK_A, MODERN_SUIT_CLUBS),
            MODERN_MAKE_CARD(MODERN_RANK_Q, MODERN_SUIT_HEARTS),
            MODERN_MAKE_CARD(MODERN_RANK_9, MODERN_SUIT_DIAMONDS),
            MODERN_MAKE_CARD(MODERN_RANK_8, MODERN_SUIT_SPADES),
            MODERN_MAKE_CARD(MODERN_RANK_2, MODERN_SUIT_CLUBS)
        };
        double u00, u01, u10, u11;
        mpf_config_t cfg;
        cfr_game_t game;
        mpf_state_t st;
        uint64_t key;

        setup_common(&cfg, ctx);
        cfg.start_street = MPF_STREET_RIVER;
        cfg.board_card_count = 5;
        memcpy(cfg.board_cards, board, sizeof(board));
        cfg.hole[0] = hole2(MODERN_RANK_A, MODERN_SUIT_SPADES, MODERN_RANK_A, MODERN_SUIT_DIAMONDS);
        cfg.hole[1] = hole2(MODERN_RANK_2, MODERN_SUIT_CLUBS, MODERN_RANK_7, MODERN_SUIT_HEARTS);
        cfg.preflop.defined = 1;
        cfg.preflop.has_active = 1;
        cfg.preflop.active[0] = 1;
        cfg.preflop.active[1] = 1;
        cfg.preflop.has_invested = 1;
        cfg.preflop.invested[0] = 10.0;
        cfg.preflop.invested[1] = 10.0;
        cfg.preflop.has_round = 1;
        cfg.preflop.round_contrib[0] = 10.0;
        cfg.preflop.round_contrib[1] = 10.0;
        cfg.preflop.has_pot = 1;
        cfg.preflop.pot = 20.0;
        cfg.preflop.has_to_call = 1;
        cfg.preflop.to_call = 10.0;
        cfg.preflop.has_current_bet = 1;
        cfg.preflop.current_bet = 10.0;
        cfg.preflop.has_to_act = 1;
        cfg.preflop.to_act = 1;

        /* Without rake, P1's river call returns the full pot */
        cfg.rake = (rake_config_t){0.0, 0.0, 0.0, 0};
        CHECK(mpf_build_game(&cfg, &game, &st) == 0, "showdown build");
        key = (uint64_t)(uintptr_t)&st;
        key = game.apply_action(&game, key, MPF_ACTION_CALL, NULL);
        CHECK(key != 0, "showdown apply call");
        CHECK(game.is_terminal(&game, key, NULL), "showdown reached");
        u00 = game.get_utility(&game, key, 0, NULL);
        u01 = game.get_utility(&game, key, 1, NULL);
        mpf_state_cleanup(&st);

        /* With rake 5%, cap 3: pot 20 -> rake 1.0 */
        cfg.rake = (rake_config_t){0.05, 3.0, 0.0, 0};
        CHECK(mpf_build_game(&cfg, &game, &st) == 0, "showdown rake build");
        key = (uint64_t)(uintptr_t)&st;
        key = game.apply_action(&game, key, MPF_ACTION_CALL, NULL);
        CHECK(key != 0, "showdown rake apply call");
        u10 = game.get_utility(&game, key, 0, NULL);
        u11 = game.get_utility(&game, key, 1, NULL);
        mpf_state_cleanup(&st);

        /* AA beats KK on this board; P0 wins the raked pot */
        CHECK(fabs(u00 - (-10.0 + 20.0)) < 1e-9, "no-rake showdown winner");
        CHECK(fabs(u10 - (-10.0 + 20.0 - 1.0)) < 1e-9, "rake showdown winner exact");
        CHECK(fabs(u00 - u10 - 1.0) < 1e-9, "showdown rake delta == min(0.05*pot,3)");
        CHECK(fabs(u01 - u11) < 1e-9, "loser utility untouched at showdown");

        /* Split pot: both play the board, so the pot is split. */
        {
            int sboard[5] = {
                MODERN_MAKE_CARD(MODERN_RANK_T, MODERN_SUIT_SPADES),
                MODERN_MAKE_CARD(MODERN_RANK_J, MODERN_SUIT_SPADES),
                MODERN_MAKE_CARD(MODERN_RANK_Q, MODERN_SUIT_SPADES),
                MODERN_MAKE_CARD(MODERN_RANK_K, MODERN_SUIT_SPADES),
                MODERN_MAKE_CARD(MODERN_RANK_A, MODERN_SUIT_SPADES)
            };
            mpf_config_t c;
            cfr_game_t g;
            mpf_state_t st2;
            uint64_t k;
            memcpy(&c, &cfg, sizeof(c));
            c.rake = (rake_config_t){0.05, 3.0, 0.0, 0};
            memcpy(c.board_cards, sboard, sizeof(sboard));
            c.hole[0] = hole2(MODERN_RANK_2, MODERN_SUIT_CLUBS, MODERN_RANK_9, MODERN_SUIT_DIAMONDS);
            c.hole[1] = hole2(MODERN_RANK_3, MODERN_SUIT_CLUBS, MODERN_RANK_9, MODERN_SUIT_HEARTS);
            CHECK(mpf_build_game(&c, &g, &st2) == 0, "split build");
            k = (uint64_t)(uintptr_t)&st2;
            k = g.apply_action(&g, k, MPF_ACTION_CALL, NULL);
            CHECK(k != 0 && g.is_terminal(&g, k, NULL), "split showdown");
            u00 = g.get_utility(&g, k, 0, NULL);
            u01 = g.get_utility(&g, k, 1, NULL);
            CHECK(fabs(u00 - (19.0 / 2.0 - 10.0)) < 1e-9, "split winner share exact");
            CHECK(fabs(u01 - (19.0 / 2.0 - 10.0)) < 1e-9, "split share exact");
            /* each invested 10, won 9.5: net -0.5 - 0.5 = -1.0 (rake 1 leaves) */
            CHECK(fabs(u00 + u01 - (-1.0)) < 1e-9, "rake leaves the table (split)");
            mpf_state_cleanup(&st2);
        }
        printf("RAKE: showdown terminal exact utilities OK\n");
    }

    /* ---------- 3. Frequency regression: rake moves the strategy ---------- */
    {
 int board[5] = {
            MODERN_MAKE_CARD(MODERN_RANK_T, MODERN_SUIT_SPADES),
            MODERN_MAKE_CARD(MODERN_RANK_J, MODERN_SUIT_SPADES),
            MODERN_MAKE_CARD(MODERN_RANK_Q, MODERN_SUIT_SPADES),
            MODERN_MAKE_CARD(MODERN_RANK_K, MODERN_SUIT_SPADES),
            MODERN_MAKE_CARD(MODERN_RANK_A, MODERN_SUIT_SPADES)
        };
        mpf_config_t cfg;
        cfr_game_t game;
        mpf_state_t st;
        cfr_storage_t *storage = NULL;
        cfr_config_t sconf;
        double exploit = 0.0;
        double v_no_rake = 0.0, v_rake = 0.0;
        root_strategy_t rs_no = {0, 0, NAN};
        root_strategy_t rs_rake = {0, 0, NAN};

        /* River spot where both players play the board (A-high straight
         * on the table), so every showdown is a guaranteed split.
         * P1 must pay 1 to call a 100 pot: calling is profitable without
         * rake and unprofitable with 5% rake, so the root call frequency
         * must flip. */
        setup_common(&cfg, ctx);
        cfg.start_street = MPF_STREET_RIVER;
        cfg.board_card_count = 5;
        memcpy(cfg.board_cards, board, sizeof(board));
        cfg.hole[0] = hole2(MODERN_RANK_3, MODERN_SUIT_CLUBS, MODERN_RANK_8, MODERN_SUIT_DIAMONDS);
        cfg.hole[1] = hole2(MODERN_RANK_K, MODERN_SUIT_SPADES, MODERN_RANK_K, MODERN_SUIT_HEARTS);
        cfg.bet_size_count_common = 0; /* no raises: fold/call only */
        cfg.raise_cap = 0;
        cfg.preflop.defined = 1;
        cfg.preflop.has_active = 1;
        cfg.preflop.active[0] = 1;
        cfg.preflop.active[1] = 1;
        cfg.preflop.has_invested = 1;
        cfg.preflop.invested[0] = 50.0;
        cfg.preflop.invested[1] = 50.0;
        cfg.preflop.has_round = 1;
        cfg.preflop.round_contrib[0] = 50.0;
        cfg.preflop.round_contrib[1] = 50.0;
        cfg.preflop.has_pot = 1;
        cfg.preflop.pot = 100.0;
        cfg.preflop.has_to_call = 1;
        cfg.preflop.to_call = 51.0;
        cfg.preflop.has_current_bet = 1;
        cfg.preflop.current_bet = 51.0;
        cfg.preflop.has_to_act = 1;
        cfg.preflop.to_act = 1;

        memset(&sconf, 0, sizeof(sconf));
        sconf.max_iterations = 300;

        /* Without rake the call is +0.5, so CFR plays it (nearly) always;
         * with rake (5% of 102 = 5.1) it is negative, so folding dominates.
         * The root CALL frequency must flip. */
        cfg.rake = (rake_config_t){0.0, 0.0, 0.0, 0};
        CHECK(mpf_build_game(&cfg, &game, &st) == 0, "freq build no-rake");
        storage = cfr_storage_create();
        CHECK(storage != NULL, "storage");
        cfr_solve(&game, storage, &sconf, &exploit);
        v_no_rake = cfr_compute_policy_value(&game, storage, 0, NULL);
        rs_no.root_key = mpf_state_infoset_key(&st);
        cfr_storage_iterate(storage, root_strategy_cb, &rs_no);
        CHECK(rs_no.found, "root strategy found (no-rake)");
        cfr_storage_destroy(storage);
        mpf_state_cleanup(&st);

        /* Rake 5% capped at 3 bb */
        cfg.rake = (rake_config_t){0.05, 3.0, 0.0, 0};
        CHECK(mpf_build_game(&cfg, &game, &st) == 0, "freq build rake");
        storage = cfr_storage_create();
        CHECK(storage != NULL, "storage2");
        cfr_solve(&game, storage, &sconf, &exploit);
        v_rake = cfr_compute_policy_value(&game, storage, 0, NULL);
        rs_rake.root_key = mpf_state_infoset_key(&st);
        cfr_storage_iterate(storage, root_strategy_cb, &rs_rake);
        CHECK(rs_rake.found, "root strategy found (rake)");
        cfr_storage_destroy(storage);
        mpf_state_cleanup(&st);

        CHECK(v_rake < v_no_rake - 0.005, "policy value lower under rake");
        CHECK(rs_rake.p_call < rs_no.p_call - 0.2, "rake flips root call frequency");

        printf("RAKE: frequency regression OK (v0=%.6f v1=%.6f, p_call %.4f -> %.4f)\n",
               v_no_rake, v_rake, rs_no.p_call, rs_rake.p_call);
    }

    eval_context_destroy(ctx);
    printf("ALL RAKE TESTS PASSED\n");
    return 0;

fail:
    return 1;
}