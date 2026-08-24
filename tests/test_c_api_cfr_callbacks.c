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
    solver = pe_cfr_create_callbacks(root, &desc, 10);
    if (!root || !solver || pe_cfr_solve(solver, 10) != PE_OK ||
        pe_cfr_get_strategy(solver, 1u, strategy, 2) != 2 ||
        pe_cfr_get_exploitability(solver, &exploitability) != PE_OK)
    {
        fprintf(stderr, "callback C CFR façade regression failed\n");
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
    pe_cfr_free(solver);
    pe_free(root);
    return 0;
}
