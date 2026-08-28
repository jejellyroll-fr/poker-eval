#include <poker_eval_api.h>

#include <stdio.h>

static int is_terminal(uint64_t state, void *user)
{
    (void)user;
    return state >= 2u;
}

static int current_player(uint64_t state, void *user)
{
    (void)state;
    (void)user;
    return 0;
}

static int get_actions(uint64_t state, int *actions, int max_actions, void *user)
{
    (void)user;
    if (state >= 2u || max_actions < 2)
        return 0;
    actions[0] = 0;
    actions[1] = 1;
    return 2;
}

static uint64_t apply_action(uint64_t state, int action, void *user)
{
    (void)user;
    return state == 1u && (action == 0 || action == 1) ? 2u + (uint64_t)action : 0u;
}

static double utility(uint64_t state, int player, void *user)
{
    (void)user;
    if (player == 0)
        return state == 2u ? 1.0 : -1.0;
    return state == 2u ? -1.0 : 1.0;
}

static uint64_t infoset_key(uint64_t state, void *user)
{
    (void)user;
    return state == 1u ? 7u : state;
}

static int chance_is_terminal(uint64_t state, void *user)
{
    (void)user;
    return state >= 4u;
}

static int chance_current_player(uint64_t state, void *user)
{
    (void)state;
    (void)user;
    return 0;
}

static int chance_get_actions(uint64_t state, int *actions, int max_actions,
                              void *user)
{
    (void)user;
    if ((state != 2u && state != 3u) || max_actions < 2)
        return 0;
    actions[0] = 0;
    actions[1] = 1;
    return 2;
}

static uint64_t chance_apply_action(uint64_t state, int action, void *user)
{
    (void)user;
    return (state == 2u || state == 3u) && (action == 0 || action == 1)
        ? 4u + (state - 2u) * 2u + (uint64_t)action : 0u;
}

static double chance_utility(uint64_t state, int player, void *user)
{
    (void)user;
    if (player == 0)
        return (state == 4u || state == 6u) ? 1.0 : -1.0;
    return (state == 4u || state == 6u) ? -1.0 : 1.0;
}

static int chance_is_chance(uint64_t state, void *user)
{
    (void)user;
    return state == 1u;
}

static int chance_get_outcomes(uint64_t state, void *user)
{
    (void)user;
    return state == 1u ? 2 : 0;
}

static double chance_get_weight(uint64_t state, int outcome, void *user)
{
    (void)state;
    (void)user;
    return outcome == 0 ? 1.0 : 2.0;
}

static uint64_t chance_apply(uint64_t state, int outcome, void *user)
{
    (void)user;
    return state == 1u && (outcome == 0 || outcome == 1)
        ? 2u + (uint64_t)outcome : 0u;
}

static double hidden_chance_utility(uint64_t state, int player, void *user)
{
    (void)user;
    if (player == 0)
        return (state == 4u || state == 7u) ? 1.0 : -1.0;
    return (state == 4u || state == 7u) ? -1.0 : 1.0;
}

static uint64_t hidden_infoset_key(uint64_t state, void *user)
{
    (void)user;
    return state == 2u || state == 3u ? 9u : state;
}

int main(void)
{
    pe_handle_t root = pe_init(NULL);
    pe_cfr_game_desc_t desc = {0};
    pe_cfr_handle_t solver;
    double strategy[2];
    double exploitability = -1.0;

    desc.initial_state = 1u;
    desc.num_players = 2;
    desc.is_terminal = is_terminal;
    desc.current_player = current_player;
    desc.get_actions = get_actions;
    desc.apply_action = apply_action;
    desc.get_utility = utility;
    desc.get_infoset_key = infoset_key;
    solver = pe_cfr_create_callbacks(root, &desc, 10);
    if (!root || !solver || pe_cfr_solve(solver, 10) != PE_OK ||
        pe_cfr_get_strategy(solver, 7u, strategy, 2) != 2 ||
        pe_cfr_get_exploitability(solver, &exploitability) != PE_OK)
    {
        fprintf(stderr, "callback C CFR façade regression failed\n");
        pe_cfr_free(solver);
        pe_free(root);
        return 1;
    }
    if (pe_cfr_get_strategy(solver, 7u, strategy, 8) != 2 ||
        pe_cfr_get_strategy(solver, 7u, strategy, 1) != -1)
    {
        fprintf(stderr, "callback C CFR action-capacity contract failed\n");
        pe_cfr_free(solver);
        pe_free(root);
        return 1;
    }
    if (strategy[0] < 0.0 || strategy[1] < 0.0 ||
        strategy[0] + strategy[1] < 0.999 || strategy[0] + strategy[1] > 1.001)
    {
        fprintf(stderr, "callback C CFR strategy is not normalized\n");
        pe_cfr_free(solver);
        pe_free(root);
        return 1;
    }

    {
        char path[128];
        pe_cfr_handle_t empty_solver = pe_cfr_create_callbacks(root, &desc, 2);
        (void)snprintf(path, sizeof(path),
                       "poker_eval_api_cfr_load_%p.pe_sol",
                       (void *)empty_solver);
        if (!empty_solver ||
            pe_cfr_save(empty_solver, path) != PE_OK ||
            pe_cfr_load(solver, path) != PE_OK ||
            pe_cfr_get_strategy(solver, 7u, strategy, 2) != -1 ||
            pe_cfr_get_exploitability(solver, &exploitability) != PE_OK) {
            fprintf(stderr, "callback C CFR load replacement contract failed\n");
            pe_cfr_free(empty_solver);
            remove(path);
            pe_cfr_free(solver);
            pe_free(root);
            return 1;
        }
        pe_cfr_free(empty_solver);
        remove(path);
    }
    pe_cfr_free(solver);

    {
        pe_cfr_game_desc_t chance_desc = {0};
        pe_cfr_handle_t chance_solver;
        double chance_strategy[2];
        chance_desc.initial_state = 1u;
        chance_desc.num_players = 2;
        chance_desc.is_terminal = chance_is_terminal;
        chance_desc.current_player = chance_current_player;
        chance_desc.get_actions = chance_get_actions;
        chance_desc.apply_action = chance_apply_action;
        chance_desc.get_utility = chance_utility;
        chance_desc.is_chance = chance_is_chance;
        chance_desc.get_chance_outcomes = chance_get_outcomes;
        chance_desc.get_chance_weight = chance_get_weight;
        chance_desc.apply_chance = chance_apply;
        chance_solver = pe_cfr_create_callbacks(root, &chance_desc, 4);
        if (!chance_solver || pe_cfr_solve(chance_solver, 4) != PE_OK ||
            pe_cfr_get_strategy(chance_solver, 2u, chance_strategy, 2) != 2 ||
            chance_strategy[0] < 0.0 || chance_strategy[1] < 0.0 ||
            chance_strategy[0] + chance_strategy[1] < 0.999 ||
            chance_strategy[0] + chance_strategy[1] > 1.001) {
            fprintf(stderr, "callback C CFR chance-node contract failed\n");
            pe_cfr_free(chance_solver);
            pe_free(root);
            return 1;
        }
        pe_cfr_free(chance_solver);

        chance_desc.get_utility = hidden_chance_utility;
        chance_desc.get_infoset_key = hidden_infoset_key;
        chance_solver = pe_cfr_create_callbacks(root, &chance_desc, 4);
        if (!chance_solver || pe_cfr_solve(chance_solver, 4) != PE_OK ||
            pe_cfr_get_strategy(chance_solver, 9u, chance_strategy, 2) != 2 ||
            pe_cfr_get_exploitability(chance_solver, &exploitability) != PE_OK ||
            exploitability < -1e-9 || exploitability >= 1.0) {
            fprintf(stderr, "callback C CFR infoset exploitability contract failed: %.17g\n",
                    exploitability);
            pe_cfr_free(chance_solver);
            pe_free(root);
            return 1;
        }
        pe_cfr_free(chance_solver);
    }
    pe_free(root);
    return 0;
}
