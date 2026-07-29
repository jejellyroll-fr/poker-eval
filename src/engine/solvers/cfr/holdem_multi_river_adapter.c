#include <poker_eval/engine/solvers/cfr/holdem_multi_river_adapter.h>
#include <poker_eval/core/eval.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static int hrm_is_terminal(const holdem_multi_state_t *st);
static void hrm_compute_utilities(holdem_multi_state_t *st);
static int hrm_get_actions_internal(const holdem_multi_state_t *st);
static void hrm_apply_action_internal(const holdem_multi_state_t *st, int action, holdem_multi_state_t *out);

static int hrm_current_player_wrapper(cfr_game_t *game, uint64_t state_key, void *user_data)
{
    const holdem_multi_state_t *st = (const holdem_multi_state_t *)(uintptr_t)state_key;
    return st->to_act;
}

static int hrm_is_terminal_wrapper(cfr_game_t *game, uint64_t state_key, void *user_data)
{
    const holdem_multi_state_t *st = (const holdem_multi_state_t *)(uintptr_t)state_key;
    return hrm_is_terminal(st);
}

static double hrm_get_utility_wrapper(cfr_game_t *game, uint64_t state_key, int player, void *user_data)
{
    holdem_multi_state_t *st = (holdem_multi_state_t *)(uintptr_t)state_key;
    if (!st->util_ready)
        hrm_compute_utilities(st);
    if (player < 0 || player >= st->num_players)
        return 0.0;
    return st->utilities[player];
}

static int hrm_get_actions_wrapper(cfr_game_t *game, uint64_t state_key, int *out_actions, int max_actions, void *user_data)
{
    const holdem_multi_state_t *st = (const holdem_multi_state_t *)(uintptr_t)state_key;
    int n = hrm_get_actions_internal(st);
    for (int i = 0; i < n && i < max_actions; ++i)
        out_actions[i] = i;
    return n;
}

static uint64_t hrm_apply_action_wrapper(cfr_game_t *game, uint64_t state_key, int action, void *user_data)
{
    const holdem_multi_state_t *st = (const holdem_multi_state_t *)(uintptr_t)state_key;
    holdem_multi_state_t *next_state = (holdem_multi_state_t *)malloc(sizeof(holdem_multi_state_t));
    if (!next_state)
        return 0;
    hrm_apply_action_internal(st, action, next_state);
    return (uint64_t)(uintptr_t)next_state;
}

static int hrm_active_count(const holdem_multi_state_t *st)
{
    int cnt = 0;
    for (int i = 0; i < st->num_players; ++i)
        if (st->active[i])
            cnt++;
    return cnt;
}

static int hrm_is_terminal(const holdem_multi_state_t *st)
{
    if (st->util_ready)
        return 1;
    if (st->to_act >= st->num_players)
        return 1;
    if (hrm_active_count(st) <= 1)
        return 1;
    return 0;
}

static void hrm_compute_utilities(holdem_multi_state_t *st)
{
    if (st->util_ready)
        return;
    double pot = st->pot;
    int active_players[HRM_MAX_PLAYERS];
    int active_count = 0;
    for (int i = 0; i < st->num_players; ++i)
    {
        st->utilities[i] = -st->contributions[i];
        if (st->active[i])
            active_players[active_count++] = i;
    }
    if (active_count == 0)
    {
        st->util_ready = 1;
        return;
    }
    if (active_count == 1)
    {
        int winner = active_players[0];
        st->utilities[winner] += pot;
        st->util_ready = 1;
        return;
    }
    eval_t best_val = 0;
    int winners[HRM_MAX_PLAYERS];
    int winner_count = 0;
    for (int idx = 0; idx < active_count; ++idx)
    {
        int p = active_players[idx];
        mask_t seven = st->hands[p] | st->board;
        eval_t val = pe_eval_7c(st->ctx, seven);
        if (winner_count == 0 || val > best_val)
        {
            best_val = val;
            winners[0] = p;
            winner_count = 1;
        }
        else if (val == best_val)
        {
            winners[winner_count++] = p;
        }
    }
    if (winner_count > 0)
    {
        double share = pot / winner_count;
        for (int i = 0; i < winner_count; ++i)
            st->utilities[winners[i]] += share;
    }
    st->util_ready = 1;
}

static int hrm_get_actions_internal(const holdem_multi_state_t *st)
{
    if (hrm_is_terminal(st))
        return 0;
    if (!st->active[st->to_act])
        return 0;
    /* Action 0 = call/check, Action 1 = fold */
    return 2;
}

static void hrm_apply_action_internal(const holdem_multi_state_t *st, int action, holdem_multi_state_t *out)
{
    *out = *st;
    if (hrm_is_terminal(out))
        return;
    int player = out->to_act;
    if (!out->active[player])
        return;
    if (action == 0)
    {
        if (out->bet_size > 0.0)
        {
            out->contributions[player] += out->bet_size;
            out->pot += out->bet_size;
        }
    }
    else if (action == 1)
    {
        out->active[player] = 0;
    }
    out->to_act += 1;
    if (hrm_is_terminal(out) && !out->util_ready)
        hrm_compute_utilities(out);
}

void hrm_build_game(const EvalContext *ctx,
                    int num_players,
                    const mask_t *hands,
                    mask_t board,
                    double bet_size,
                    cfr_game_t *out_game,
                    holdem_multi_state_t *out_state)
{
    memset(out_state, 0, sizeof(*out_state));
    memset(out_game, 0, sizeof(*out_game));
    out_state->ctx = ctx;
    out_state->num_players = num_players;
    out_state->bet_size = bet_size;
    out_state->board = board;
    out_state->to_act = 0;
    out_state->pot = 0.0;
    out_state->util_ready = 0;
    for (int i = 0; i < num_players; ++i)
    {
        out_state->hands[i] = hands[i];
        out_state->active[i] = 1;
        out_state->contributions[i] = 0.0;
        out_state->utilities[i] = 0.0;
    }

    out_game->initial_state = out_state;
    out_game->game_data = out_state;
    out_game->is_terminal = hrm_is_terminal_wrapper;
    out_game->get_utility = hrm_get_utility_wrapper;
    out_game->get_actions = hrm_get_actions_wrapper;
    out_game->apply_action = hrm_apply_action_wrapper;
    out_game->current_player = hrm_current_player_wrapper;
    out_game->num_players = num_players;
    out_game->state_size = sizeof(*out_state);
}
