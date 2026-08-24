#include <poker_eval/engine/solvers/cfr/cfr_parallel.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static int game_player(cfr_game_t *game, uint64_t state, void *user)
{
    (void)game; (void)state; (void)user;
    return 0;
}

static int game_actions(cfr_game_t *game, uint64_t state,
                        int *actions, int max_actions, void *user)
{
    (void)game; (void)user;
    if (state != 1u || max_actions < 2)
        return 0;
    actions[0] = 0;
    actions[1] = 1;
    return 2;
}

static uint64_t game_apply(cfr_game_t *game, uint64_t state,
                           int action, void *user)
{
    (void)game; (void)user;
    return state == 1u ? (uint64_t)(2 + action) : state;
}

static int game_terminal(cfr_game_t *game, uint64_t state, void *user)
{
    (void)game; (void)user;
    return state >= 2u;
}

static double game_utility(cfr_game_t *game, uint64_t state,
                           int player, void *user)
{
    (void)game; (void)user;
    if (player == 0)
        return state == 2u ? 1.0 : -1.0;
    return state == 2u ? -1.0 : 1.0;
}

static uint64_t game_infoset(const void *state)
{
    return (uint64_t)(uintptr_t)state;
}

static int game_factory(int worker_id, cfr_game_t *out, void *user)
{
    (void)worker_id; (void)user;
    memset(out, 0, sizeof(*out));
    out->current_player = game_player;
    out->get_actions = game_actions;
    out->apply_action = game_apply;
    out->get_infoset_key = game_infoset;
    out->is_terminal = game_terminal;
    out->get_utility = game_utility;
    out->initial_state = (void *)(uintptr_t)1u;
    out->num_players = 2;
    return 0;
}

int main(void)
{
    cfr_config_t config;
    cfr_parallel_config_t parallel;
    cfr_storage_t *storage = cfr_storage_create();
    cfr_storage_t *rerun = cfr_storage_create();
    double exploitability = -1.0;
    double strategy[2];
    double rerun_strategy[2];

    if (!storage || !rerun)
        return 1;
    memset(&config, 0, sizeof(config));
    config.max_iterations = 64;
    config.seed = 17;
    memset(&parallel, 0, sizeof(parallel));
    parallel.worker_count = 2;
    parallel.max_iterations = 64;

    if (cfr_solve_parallel_batch(game_factory, NULL, NULL, storage,
                                 &config, &parallel, &exploitability) != 0 ||
        !isfinite(exploitability) ||
        cfr_storage_count_infosets(storage) == 0)
    {
        fprintf(stderr, "parallel CFR run failed\n");
        cfr_storage_destroy(storage);
        cfr_storage_destroy(rerun);
        return 1;
    }
    cfr_storage_get_avg_strategy(storage, 1u, 2, strategy);
    if (fabs(strategy[0] + strategy[1] - 1.0) > 1e-9 ||
        strategy[0] < 0.0 || strategy[1] < 0.0)
    {
        fprintf(stderr, "parallel CFR strategy is not normalized\n");
        cfr_storage_destroy(storage);
        cfr_storage_destroy(rerun);
        return 1;
    }
    /* Action 0 strictly dominates in this game, so every independent worker
     * solve must converge toward it and the merged average must preserve the
     * direction. */
    if (strategy[0] < 0.9 || strategy[0] <= strategy[1])
    {
        fprintf(stderr,
                "parallel CFR did not converge to the dominant action "
                "(got %.6f / %.6f)\n",
                strategy[0], strategy[1]);
        cfr_storage_destroy(storage);
        cfr_storage_destroy(rerun);
        return 1;
    }
    /* Utilities are +/-1 here, so the merged strategy must stay near
     * exploitable-by-nothing regardless of the reporting convention. */
    if (exploitability < 0.0 || exploitability > 0.25)
    {
        fprintf(stderr, "parallel CFR exploitability out of range: %.6f\n",
                exploitability);
        cfr_storage_destroy(storage);
        cfr_storage_destroy(rerun);
        return 1;
    }
    /* The batch merge is documented as deterministic: a second run with the
     * same seed must reproduce the same merged strategy. */
    {
        double rerun_exploitability = -1.0;
        if (cfr_solve_parallel_batch(game_factory, NULL, NULL, rerun,
                                     &config, &parallel,
                                     &rerun_exploitability) != 0)
        {
            fprintf(stderr, "parallel CFR rerun failed\n");
            cfr_storage_destroy(storage);
            cfr_storage_destroy(rerun);
            return 1;
        }
        cfr_storage_get_avg_strategy(rerun, 1u, 2, rerun_strategy);
        /* Bit-exact comparison: determinism means identical bytes, which
         * also sidesteps -Wfloat-equal. */
        if (memcmp(strategy, rerun_strategy, sizeof(strategy)) != 0 ||
            memcmp(&exploitability, &rerun_exploitability,
                   sizeof(exploitability)) != 0)
        {
            fprintf(stderr,
                    "parallel CFR merge is not deterministic "
                    "(%.6f/%.6f vs %.6f/%.6f)\n",
                    strategy[0], strategy[1], rerun_strategy[0],
                    rerun_strategy[1]);
            cfr_storage_destroy(storage);
            cfr_storage_destroy(rerun);
            return 1;
        }
    }
    cfr_storage_destroy(storage);
    cfr_storage_destroy(rerun);
    return 0;
}
