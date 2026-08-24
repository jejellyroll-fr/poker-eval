/* Lane B integration: correlated private deals enter a real betting state. */

#include <poker_eval/solver/pe_preflop_betting.h>
#include <poker_eval/solver/pe_ports.h>
#include <poker_eval/solver/pe_solver.h>
#include <poker_eval/solver/pe_solver_config.h>

#include <stdio.h>

static uint16_t action_count(const pe_preflop_betting_state_t *state,
                             void *user)
{
    (void)state;
    (void)user;
    return 2u;
}

static pe_action_status_t action_at(const pe_preflop_betting_state_t *state,
                                    uint16_t action, pe_action_t *out,
                                    void *user)
{
    (void)user;
    if (!out || action > 1u)
        return PE_ACTION_ERR_NULL_ARGUMENT;
    *out = (pe_action_t){0};
    if (state->betting.to_call > 0.0)
        out->kind = action == 0u ? PE_ACTION_FOLD : PE_ACTION_CALL;
    else
    {
        out->kind = action == 0u ? PE_ACTION_CHECK : PE_ACTION_BET;
        out->amount_kind = action == 0u ? PE_AMOUNT_NONE : PE_AMOUNT_CHIPS;
        out->amount = action == 0u ? 0.0 : 1.0;
    }
    return PE_ACTION_OK;
}

static uint64_t infoset_key(const pe_preflop_betting_state_t *state,
                            void *user)
{
    (void)user;
    return (uint64_t)(unsigned)state->betting.to_act |
           ((uint64_t)(state->betting.to_call > 0.0) << 8) |
           ((uint64_t)state->betting.raises_made << 16);
}

static double terminal_value(const pe_preflop_betting_state_t *state,
                             int player, void *user)
{
    (void)state;
    (void)player;
    (void)user;
    return 0.0;
}

int main(void)
{
    const int hand0[] = {0, 13};
    const int hand1[] = {1, 14};
    pe_holdem_combo_t combo0 = {MASK_EMPTY, 1.0};
    pe_holdem_combo_t combo1 = {MASK_EMPTY, 1.0};
    pe_holdem_range_t ranges[2];
    pe_preflop_deal_sampler_t sampler;
    pe_betting_rules_t rules;
    pe_preflop_betting_state_t root;
    pe_preflop_betting_ops_t ops;
    pe_preflop_betting_game_t game;
    pe_solver_config_t cfg = pe_solver_config_default();
    pe_solver_deps_t deps = pe_solver_deps_default();
    pe_solver_t *solver;
    pe_solver_status_t status;
    double stacks[] = {10.0, 10.0};

    combo0.cards = mask_set(mask_set(MASK_EMPTY, hand0[0]), hand0[1]);
    combo1.cards = mask_set(mask_set(MASK_EMPTY, hand1[0]), hand1[1]);
    ranges[0] = (pe_holdem_range_t){&combo0, 1u};
    ranges[1] = (pe_holdem_range_t){&combo1, 1u};
    if (pe_preflop_deal_sampler_init_holdem(&sampler, MASK_EMPTY, ranges, 2u) != 0)
        return 1;
    pe_betting_rules_default(&rules, 2u);
    if (pe_betting_state_init(&root.betting, &rules, stacks, 2u, 0, 0.0, 0.0) !=
            PE_BETTING_OK)
        return 1;
    root.is_chance = 0;
    root.holes[0] = MASK_EMPTY;
    root.holes[1] = MASK_EMPTY;
    ops.action_count = action_count;
    ops.action_at = action_at;
    ops.infoset_key = infoset_key;
    ops.terminal_value = terminal_value;
    if (pe_preflop_betting_game_init(&game, &sampler, &rules, &root, &ops, NULL) != 0)
        return 1;

    cfg.algorithm.preset = PE_PRESET_EXTERNAL_MCCFR;
    cfg.max_iterations = 64u;
    cfg.problem.expected_infosets = 8u;
    cfg.problem.expected_actions = 2u;
    cfg.problem.expected_combos = 1u;
    cfg.seed = 0x5501u;
    deps.external_game = pe_preflop_betting_external(&game);
    solver = pe_solver_create(&cfg, &deps);
    status = solver ? pe_solver_run(solver) : PE_SOLVER_ERR_NULL_ARGUMENT;
    if (!solver || status != PE_SOLVER_OK)
    {
        fprintf(stderr, "test_pe_preflop_betting: betting adapter failed (%d)\n",
                (int)status);
        pe_solver_destroy(solver);
        pe_preflop_betting_game_destroy(&game);
        return 1;
    }
    pe_solver_destroy(solver);
    pe_preflop_betting_game_destroy(&game);
    puts("test_pe_preflop_betting: private deals enter betting tree");
    return 0;
}
