/*
 * test_mpf_tree.c - FEAT-06: effective all-in and dynamic STPR rules for
 * pot-limit game tree construction.
 *
 * Verifies:
 *  1. Pot-limit short-stack nodes expose an ALL_IN action candidate.
 *  2. Duplicate saplings (a pot bet that equals an all-in) are pruned.
 *  3. STPR is computed dynamically (effective stack / current pot) at the node.
 */

#include <poker_eval/engine/solvers/cfr/cfr_core.h>
#include <poker_eval/engine/solvers/cfr/multiway_postflop_adapter.h>
#include <poker_eval/core/eval_context.h>
#include <poker_eval/core/modern_cardmask.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond, msg)                             \
    do                                               \
    {                                                \
        if (!(cond))                                 \
        {                                            \
            fprintf(stderr, "FAIL: %s\n", msg);      \
            return 1;                                \
        }                                            \
    } while (0)

#define MPF_ACTION_MAX 32

static mpf_state_t g_state;
static cfr_game_t g_game;
static EvalContext *g_ctx;

/* Build a heads-up PLO4 game on the flop with the supplied effective stack
 * (the remaining stack behind the acting player) and pot. The shared
 * EvalContext (g_ctx) is created once in main and reused across calls so we do
 * not leak an eval context (and its lookup tables) per invocation. */
static void setup_plo4(double eff_stack, double pot, double committal_pct)
{
    EvalContext *ctx = g_ctx;

    mpf_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.ctx = ctx;
    cfg.rules = MPF_RULE_PLO4;
    cfg.num_players = 2;
    cfg.button_index = 0;
    cfg.start_street = MPF_STREET_FLOP;
    cfg.bet_size_count_common = 1;
    cfg.bet_sizes_common[0] = 1.0; /* 100% pot bet, absolute via fractions */
    cfg.raise_cap = 4;
    cfg.enable_pot_sizing = 1;     /* bet sizes are fractions of the pot */
    cfg.sb = 0.5;
    cfg.bb = 1.0;
    cfg.ante = 0.0;
    cfg.stacks[0] = eff_stack;
    cfg.stacks[1] = eff_stack;
    cfg.is_pot_limit = -1;         /* auto-derive from PLO4 rules */
    cfg.committal_threshold_percent = committal_pct;

    /* A concrete flop so the game builds a valid node. */
    cfg.board_card_count = 3;
    cfg.board_cards[0] = MODERN_MAKE_CARD(MODERN_RANK_2, MODERN_SUIT_CLUBS);
    cfg.board_cards[1] = MODERN_MAKE_CARD(MODERN_RANK_7, MODERN_SUIT_DIAMONDS);
    cfg.board_cards[2] = MODERN_MAKE_CARD(MODERN_RANK_K, MODERN_SUIT_HEARTS);

    memset(&g_state, 0, sizeof(g_state));
    memset(&g_game, 0, sizeof(g_game));
    if (mpf_build_game(&cfg, &g_game, &g_state) != 0)
    {
        fprintf(stderr, "FAIL: mpf_build_game failed\n");
        exit(1);
    }

    /* Force the acting player's effective stack and the current pot so we can
     * test sizing deterministically at the decision node. */
    g_state.stacks[0] = eff_stack;
    g_state.pot = pot;
    g_state.to_call = 0.0;
    g_state.round_contrib[0] = 0.0;
    g_state.round_contrib[1] = 0.0;
    g_state.to_act = 0;
}

static int collect_actions(int *out, int max_n)
{
    memset(out, -1, (size_t)max_n * sizeof(int));
    return g_game.get_actions(&g_game, (uintptr_t)&g_state, out, max_n, NULL);
}

static int has_action(const int *acts, int n, int action)
{
    for (int i = 0; i < n; ++i)
        if (acts[i] == action)
            return 1;
    return 0;
}

static int run_all_in_threshold(void)
{
    /* Short stack: a 100% pot bet (pot-sizing, fraction 1.0) would commit the
     * whole effective stack, leaving 0 <= 1.0 * pot. This must collapse into a
     * distinct ALL_IN candidate rather than a standalone pot-bet raise. */
    const double stack = 10.0;
    const double pot = 20.0; /* STPR = 0.5 at this node */

    setup_plo4(stack, pot, 100.0);

    int acts[MPF_ACTION_MAX];
    int n = collect_actions(acts, MPF_ACTION_MAX);
    CHECK(n > 0, "expected actions at short-stack node");

    CHECK(has_action(acts, n, MPF_ACTION_ALL_IN),
          "short-stack pot-limit node must expose ALL_IN candidate");
    CHECK(has_action(acts, n, MPF_ACTION_CALL),
          "node must expose check (call) when to_call == 0");

    /* The 100% pot bet that equals the all-in must NOT also appear as a plain
     * raise candidate (duplicate pruned). The only raise-style actions allowed
     * are the ALL_IN sentinel. */
    for (int i = 0; i < n; ++i)
        if (acts[i] >= MPF_ACTION_RAISE_BASE && acts[i] < MPF_ACTION_ALL_IN)
            CHECK(0, "duplicate 100%-pot raise must be pruned into ALL_IN");

    /* Dynamic STPR: effective stack / pot. */
    double expected_stpr = stack / pot;
    CHECK(fabs(g_state.stpr - expected_stpr) < 1e-9,
          "STPR must equal effective_stack / pot at the node");

    printf("  all-in threshold + dedup ok (stpr=%.3f)\n", g_state.stpr);
    return 0;
}

static int run_no_collapse_when_deep(void)
{
    /* Deep stack: a 100% pot bet leaves a large remainder (> threshold * pot),
     * so it stays a normal raise and no all-in is forced. */
    const double stack = 1000.0;
    const double pot = 20.0;

    setup_plo4(stack, pot, 100.0);

    int acts[MPF_ACTION_MAX];
    int n = collect_actions(acts, MPF_ACTION_MAX);
    CHECK(n > 0, "expected actions at deep-stack node");
    CHECK(has_action(acts, n, MPF_ACTION_RAISE_BASE + 0),
          "deep-stack node must keep 100% pot raise");
    CHECK(!has_action(acts, n, MPF_ACTION_ALL_IN),
          "deep-stack node must NOT auto-jam");

    printf("  no-collapse deep stack ok\n");
    return 0;
}

static int run_configurable_threshold(void)
{
    /* Codex P1: a legal pot-sized bet that merely leaves a short remainder must
     * NOT be turned into an all-in overbet. With stack=40, pot=20, a 100% pot
     * bet commits 20 (well within the 40 stack); the remainder (20) is <= 200%
     * of the pot, but the player can still make the legal bet, so the ALL_IN
     * action (which would commit the entire 40) must stay absent. */
    const double stack = 40.0;
    const double pot = 20.0;

    setup_plo4(stack, pot, 200.0);

    int acts[MPF_ACTION_MAX];
    int n = collect_actions(acts, MPF_ACTION_MAX);
    CHECK(n > 0, "expected actions at threshold node");
    CHECK(!has_action(acts, n, MPF_ACTION_ALL_IN),
          "legal pot bet leaving short remainder must NOT become an all-in overbet");
    CHECK(has_action(acts, n, MPF_ACTION_RAISE_BASE + 0),
          "legal 100% pot raise must remain available");

    printf("  no-overbet configurable threshold ok\n");
    return 0;
}

static int run_capped_all_in_collapse(void)
{
    /* The all-in collapse is only valid when the raise was already capped by the
     * stack. stack=15, pot=20, 100% pot sizing => the full pot bet (20) exceeds
     * the 15 stack, so it is capped to 15; the remainder is 0 (<= 1.0*pot), and
     * the emitted ALL_IN commits the same legal 15 chips. */
    const double stack = 15.0;
    const double pot = 20.0;

    setup_plo4(stack, pot, 100.0);

    int acts[MPF_ACTION_MAX];
    int n = collect_actions(acts, MPF_ACTION_MAX);
    CHECK(n > 0, "expected actions at capped node");
    CHECK(has_action(acts, n, MPF_ACTION_ALL_IN),
          "stack-capped raise must collapse into a legal all-in");

    printf("  capped all-in collapse ok\n");
    return 0;
}

int main(void)
{
    EvalConfig ecfg = eval_config_default();
    g_ctx = eval_context_create(&ecfg);
    if (!g_ctx)
    {
        fprintf(stderr, "FAIL: EvalContext create\n");
        return 1;
    }

    printf("mpf_tree FEAT-06: effective all-in + dynamic STPR\n");
    CHECK(run_all_in_threshold() == 0, "all-in threshold + dedup");
    CHECK(run_no_collapse_when_deep() == 0, "no collapse when deep");
    CHECK(run_configurable_threshold() == 0, "configurable threshold");
    CHECK(run_capped_all_in_collapse() == 0, "capped all-in collapse");
    printf("test_mpf_tree passed.\n");

    /* Clean up the eval context and game state so LSan/ASan stay green. */
    mpf_state_cleanup(&g_state);
    eval_context_destroy(g_ctx);
    g_ctx = NULL;

    return 0;
}
