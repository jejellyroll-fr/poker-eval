/*
 * @file test_cfr_subgame_resolve.c
 * @brief Tests for subgame re-solving / CFR-D gadget (FEAT-05)
 *
 * Two-player game used for the test:
 *
 *   root (P0 acts):  fold(0)  /  enter(1)
 *     fold  -> T_fold  (P0 +1, P1 -1)        [outside the subgame]
 *     enter -> sub_root (P0 acts): L(0) / R(1)
 *        L -> T_L   (P0 +5, P1 -5)
 *        R -> T_R   (P0 -2, P1 +2)
 *
 * The subgame is rooted at `enter` (== sub_root), and the re-solved player is
 * P0, who acts at sub_root. The boundary infoset is `sub_root`; the opponent
 * P1 may terminate there (take its blueprint counterfactual value) or follow
 * into the subgame. A correct re-solve keeps P1's value at the boundary no
 * worse than the blueprint, so the opponent prefers to terminate
 * (follow_freq ~ 0) -- the value constraint holds.
 *
 * The test also checks:
 *   - the blueprint CFV matches the analytic value,
 *   - trunk-locked seeding locks exactly the infosets outside the subgame,
 *   - the locked root reproduces the blueprint strategy,
 *   - the CFR-D gadget re-solve holds the boundary value constraint,
 *   - multiway gadget re-solving is rejected, but the trunk-locked fallback
 *     (config->lock_trunk) is accepted.
 */

#include <poker_eval/engine/solvers/cfr/cfr_core.h>
#include <poker_eval/engine/solvers/cfr/cfr_resolve.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>

typedef struct
{
    int is_terminal;
    int player;
    double util[2];
} rg_state_t;

static rg_state_t g_root    = {0, 0, {0.0, 0.0}};
static rg_state_t g_subroot = {0, 0, {0.0, 0.0}};
static rg_state_t g_tfold   = {1, -1, {1.0, -1.0}};
static rg_state_t g_tl      = {1, -1, {5.0, -5.0}};
static rg_state_t g_tr      = {1, -1, {-2.0, 2.0}};

static int rg_current_player(cfr_game_t *g, uint64_t k, void *u)
{
    (void)g; (void)u;
    return ((rg_state_t *)(uintptr_t)k)->player;
}
static int rg_is_terminal(cfr_game_t *g, uint64_t k, void *u)
{
    (void)g; (void)u;
    return ((rg_state_t *)(uintptr_t)k)->is_terminal;
}
static double rg_get_utility(cfr_game_t *g, uint64_t k, int p, void *u)
{
    (void)g; (void)u;
    /* The multiway fallback test reuses this small two-player tree with a
     * third neutral player. Keep that deliberately synthetic player within
     * bounds so sanitizer builds validate the resolver rather than the test
     * fixture. */
    if (p < 0 || p >= 2)
        return 0.0;
    return ((rg_state_t *)(uintptr_t)k)->util[p];
}
static int rg_get_actions(cfr_game_t *g, uint64_t k, int *out, int max, void *u)
{
    (void)g; (void)u;
    if (((rg_state_t *)(uintptr_t)k)->is_terminal)
        return 0;
    if (max < 2)
        return 0;
    out[0] = 0;
    out[1] = 1;
    return 2;
}
static uint64_t rg_apply_action(cfr_game_t *g, uint64_t k, int a, void *u)
{
    (void)g; (void)u;
    rg_state_t *st = (rg_state_t *)(uintptr_t)k;
    if (st == &g_root)
        return (uint64_t)(uintptr_t)(a == 0 ? &g_tfold : &g_subroot);
    if (st == &g_subroot)
        return (uint64_t)(uintptr_t)(a == 0 ? &g_tl : &g_tr);
    return k;
}

static void rg_build_game(cfr_game_t *game)
{
    memset(game, 0, sizeof(*game));
    game->current_player = rg_current_player;
    game->is_terminal = rg_is_terminal;
    game->get_utility = rg_get_utility;
    game->get_actions = rg_get_actions;
    game->apply_action = rg_apply_action;
    game->initial_state = (void *)(uintptr_t)&g_root;
    game->state_size = sizeof(rg_state_t);
    game->num_players = 2;
}

#define CHECK(c, m) do { if (!(c)) { fprintf(stderr, "%s\n", m); return 1; } } while (0)

int main(void)
{
    cfr_game_t game;
    rg_build_game(&game);

    cfr_storage_t *blueprint = cfr_storage_create();
    CHECK(blueprint != NULL, "create blueprint storage");
    cfr_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_iterations = 2000;
    cfg.enable_dcfr = 1;
    double expl = 0.0;
    double r = cfr_solve(&game, blueprint, &cfg, &expl);
    CHECK(r >= 0.0, "blueprint solve succeeded");

    /* Blueprint average strategy at the boundary (sub_root) where P0 acts. */
    uint64_t subroot_key = (uint64_t)(uintptr_t)&g_subroot;
    double sub_avg[2] = {0.0, 0.0};
    cfr_storage_get_avg_strategy(blueprint, subroot_key, 2, sub_avg);
    double sum = sub_avg[0] + sub_avg[1];
    CHECK(fabs(sum - 1.0) < 1e-6, "blueprint boundary strategy normalizes");

    /* Compute the blueprint CFV for P1 at the boundary via the resolver. */
    pe_cfr_boundary_t bd;
    memset(&bd, 0, sizeof(bd));
    bd.infoset = subroot_key;
    int rc = pe_cfr_blueprint_cfv(&game, blueprint, 1 /* P1 */, NULL, &bd, 1);
    CHECK(rc == PE_CFR_RESOLVE_OK, "blueprint cfv computed");
    CHECK(bd.reach > 0.0, "boundary reachable in blueprint walk");

    double expected_cfv = -5.0 * sub_avg[0] + 2.0 * (1.0 - sub_avg[0]);
    CHECK(fabs(bd.cfv - expected_cfv) < 1e-6, "boundary CFV matches analytic value");
    CHECK(bd.cfv < -4.9, "blueprint at boundary is near P0 best response (P1 <= -4.9)");

    /* ---- Trunk-locked seeding ----
     * Re-solving the subgame rooted at `enter` (== sub_root) should lock every
     * infoset outside the subgame (root + T_fold) and leave the subgame
     * infosets (sub_root + T_L + T_R) unlocked. */
    uint64_t sub_root = (uint64_t)(uintptr_t)&g_subroot;
    cfr_storage_t *seed_storage = cfr_storage_create();
    CHECK(seed_storage != NULL, "create seed storage");
    size_t locked = 0, free_n = 0;
    rc = pe_cfr_seed_resolve_storage(&game, blueprint, seed_storage, sub_root, NULL, &locked, &free_n);
    CHECK(rc == PE_CFR_RESOLVE_OK, "seed resolve storage");
    CHECK(locked == 2, "exactly 2 infosets locked outside subgame");
    CHECK(free_n == 3, "exactly 3 subgame infosets left free");

    uint64_t root_key = (uint64_t)(uintptr_t)&g_root;
    const double *locked_p = NULL;
    CHECK(cfr_storage_get_locked_strategy(seed_storage, root_key, 2, &locked_p) == 1,
          "root is locked");
    double root_avg[2] = {0.0, 0.0};
    cfr_storage_get_avg_strategy(blueprint, root_key, 2, root_avg);
    CHECK(fabs(locked_p[0] - root_avg[0]) < 1e-12 && fabs(locked_p[1] - root_avg[1]) < 1e-12,
          "locked root matches blueprint");

    /* ---- CFR-D gadget re-solve ---- */
    pe_cfr_boundary_t gbd;
    memset(&gbd, 0, sizeof(gbd));
    gbd.infoset = subroot_key;
    /* zero cfv/reach => resolver computes them from the blueprint */
    pe_cfr_subgame_t sub;
    memset(&sub, 0, sizeof(sub));
    sub.root_state_key = sub_root;
    sub.resolve_player = 0; /* P0 refined */
    sub.boundary = &gbd;
    sub.boundary_count = 1;

    cfr_storage_t *resolve_storage = cfr_storage_create();
    CHECK(resolve_storage != NULL, "create resolve storage");

    pe_cfr_resolve_config_t rcfg;
    memset(&rcfg, 0, sizeof(rcfg));
    rcfg.cfr.max_iterations = 10000;
    rcfg.cfr.enable_dcfr = 1;
    /* This deliberately tiny game is solved approximately; allow the small
     * residual from the finite CFR run while still detecting a real breach. */
    rcfg.margin_tolerance = 0.005;

    pe_cfr_resolve_result_t result;
    memset(&result, 0, sizeof(result));
    rc = pe_cfr_resolve_subgame(&game, blueprint, resolve_storage, &sub, &rcfg, NULL, &result);
    CHECK(rc == PE_CFR_RESOLVE_OK, "gadget resolve succeeded");
    CHECK(result.boundary_count == 1, "one boundary reported");
    /* The resolver keeps the caller's const boundary descriptor untouched;
     * expose the computed value through the result instead. */
    CHECK(fabs(result.margins[0].blueprint_cfv - bd.cfv) < 1e-9,
          "resolver computed the same boundary CFV as direct call");

    /* The opponent must not profit by entering: follow_freq ~ 0 (terminate). */
    CHECK(result.constraints_satisfied == 1, "boundary value constraint satisfied");
    CHECK(result.worst_margin >= -rcfg.margin_tolerance - 1e-9,
          "worst margin within tolerance");
    CHECK(result.margins[0].follow_freq < 0.01,
          "opponent chooses to terminate at the boundary (follow ~ 0)");

    /* The resolved P0 strategy at the boundary should still pick L. */
    double p0_sub[2] = {0.0, 0.0};
    cfr_storage_get_avg_strategy(resolve_storage, subroot_key, 2, p0_sub);
    CHECK(fabs(p0_sub[0] + p0_sub[1] - 1.0) < 1e-6, "resolved boundary strategy normalizes");
    CHECK(p0_sub[0] > 0.99, "resolved P0 still plays L at the boundary");

    /* ---- Multiway: gadget unsupported, but trunk-locked fallback works ---- */
    cfr_game_t mw = game;
    mw.num_players = 3;
    cfr_storage_t *rs3 = cfr_storage_create();
    rc = pe_cfr_resolve_subgame(&mw, blueprint, rs3, &sub, &rcfg, NULL, NULL);
    CHECK(rc == PE_CFR_RESOLVE_UNSUPPORTED, "multiway gadget without lock_trunk rejected");

    pe_cfr_resolve_config_t rcfg_mw;
    memset(&rcfg_mw, 0, sizeof(rcfg_mw));
    rcfg_mw.cfr.max_iterations = 200;
    rcfg_mw.lock_trunk = 1; /* enable the multiway fallback */
    rc = pe_cfr_resolve_subgame(&mw, blueprint, rs3, &sub, &rcfg_mw, NULL, NULL);
    CHECK(rc == PE_CFR_RESOLVE_OK, "multiway trunk-locked fallback accepted");
    CHECK(cfr_storage_count_infosets(rs3) > 0, "multiway fallback seeded storage");
    cfr_storage_destroy(rs3);

    cfr_storage_destroy(blueprint);
    cfr_storage_destroy(seed_storage);
    cfr_storage_destroy(resolve_storage);

    printf(" PASSED\n");
    return 0;
}
