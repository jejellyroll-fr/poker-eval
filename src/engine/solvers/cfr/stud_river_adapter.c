/* stud_river_adapter.c - 7-Stud High HU river */
#include <poker_eval/engine/solvers/cfr/stud_river_adapter.h>
#include <string.h>
#include <stdlib.h>
#include <poker_eval/core/eval_context.h>

static bool stud_is_terminal(const void *s, double util[2]);
static int stud_num_actions(const void *s);
static bool stud_apply_action(const void *s, int a, void *out);
static int stud_player_to_act(const void *s);

static int stud_is_terminal_wrapper(cfr_game_t* game, uint64_t state_key, void* user_data) {
    double util[2];
    return stud_is_terminal((void*)state_key, util);
}

static double stud_get_utility_wrapper(cfr_game_t* game, uint64_t state_key, int player, void* user_data) {
    double util[2];
    stud_is_terminal((void*)state_key, util);
    return util[player];
}

static int stud_get_actions_wrapper(cfr_game_t* game, uint64_t state_key, int* out_actions, int max_actions, void* user_data) {
    int n = stud_num_actions((void*)state_key);
    for (int i = 0; i < n && i < max_actions; ++i) {
        out_actions[i] = i;
    }
    return n < max_actions ? n : max_actions;
}

static uint64_t stud_apply_action_wrapper(cfr_game_t* game, uint64_t state_key, int action, void* user_data) {
    stud_state_t* next_state = malloc(sizeof(stud_state_t));
    stud_apply_action((void*)state_key, action, next_state);
    return (uint64_t)next_state;
}

static int stud_current_player_wrapper(cfr_game_t *game, uint64_t state_key, void *user_data)
{
    return stud_player_to_act((const void *)state_key);
}

static bool stud_is_terminal(const void *s, double util[2])
{
    const stud_state_t *st = (const stud_state_t *)s;
    if (st->hist & (1u << 5))
    {
        int winner = 1 - st->to_act;
        util[winner] = st->pot;
        util[1 - winner] = -st->pot;
        return true;
    }
    bool check_check = ((st->hist & 0x3) == 0x3);
    bool bet_call = (((st->hist & (1u << 2)) && (st->hist & (1u << 4))) || ((st->hist & (1u << 3)) && (st->hist & (1u << 4))));
    if (check_check || bet_call)
    {
        eval_t v0 = pe_eval_7c(st->ctx, st->seven0);
        eval_t v1 = pe_eval_7c(st->ctx, st->seven1);
        if (v0 > v1)
        {
            util[0] = st->pot;
            util[1] = -st->pot;
        }
        else if (v1 > v0)
        {
            util[1] = st->pot;
            util[0] = -st->pot;
        }
        else
        {
            util[0] = 0.0;
            util[1] = 0.0;
        }
        return true;
    }
    return false;
}

static int stud_player_to_act(const void *s) { return ((const stud_state_t *)s)->to_act; }
static int stud_num_actions(const void *s)
{
    const stud_state_t *st = (const stud_state_t *)s;
    return (st->to_call > 0.0) ? ((st->raises_left > 0) ? (2 + st->n_bet_sizes) : 2) : (1 + st->n_bet_sizes);
}

static bool stud_apply_action(const void *s, int a, void *out)
{
    const stud_state_t *st = (const stud_state_t *)s;
    stud_state_t *ns = (stud_state_t *)out;
    *ns = *st;
    if (ns->to_call > 0.0)
    {
        if (a == 0)
        {
            ns->pot += ns->to_call;
            ns->to_call = 0.0;
            ns->hist |= (1u << 4);
            ns->to_act = 1 - ns->to_act;
            return true;
        }
        else if (a == 1)
        {
            ns->hist |= (1u << 5);
            return true;
        }
        else if (a >= 2 && ns->raises_left > 0)
        {
            int idx = a - 2;
            if (idx < 0 || idx >= ns->n_bet_sizes)
                return false;
            double raise_amt = ns->bet_fracs[idx] * ns->pot;
            ns->pot += ns->to_call + raise_amt;
            ns->to_call = raise_amt;
            ns->raises_left -= 1;
            ns->to_act = 1 - ns->to_act;
            return true;
        }
        return false;
    }
    else
    {
        if (a == 0)
        {
            ns->hist |= (1u << (ns->to_act));
            ns->to_act = 1 - ns->to_act;
            return true;
        }
        else if (a >= 1)
        {
            int idx = a - 1;
            if (idx < 0 || idx >= ns->n_bet_sizes)
                return false;
            ns->hist |= (1u << (2 + ns->to_act));
            double amt = ns->bet_fracs[idx] * ns->pot;
            ns->pot += amt;
            ns->to_act = 1 - ns->to_act;
            ns->to_call = amt;
            ns->raises_left = ns->raise_cap;
            return true;
        }
        return false;
    }
}

static uint64_t stud_infoset_key(const void *s)
{
    const stud_state_t *st = (const stud_state_t *)s;
    int p = st->to_act;
    uint64_t act = ((uint64_t)(st->hist & 0xFFFF) << 8) | ((st->to_call > 0.0) ? 1ULL : 0ULL);
    if (st->bucket_mode == 0)
        return (act << 16) | (uint64_t)(st->raises_left & 0xF) | ((uint64_t)(p & 1) << 4);
    mask_t hp = (p == 0) ? st->seven0 : st->seven1;
    eval_t p_eval = pe_eval_7c(st->ctx, hp);
    hand_class_t pcl = eval_get_hand_class(p_eval);
    int p_cls = ((int)pcl) & 0xF;
    if (st->bucket_mode >= 2)
    {
        if (st->bucket_mode == 3)
        {
            uint32_t base = 0;
            switch (pcl)
            {
            case HAND_HIGH_CARD:
                base = EVAL_HIGH_CARD;
                break;
            case HAND_PAIR:
                base = EVAL_PAIR;
                break;
            case HAND_TWO_PAIR:
                base = EVAL_TWO_PAIR;
                break;
            case HAND_THREE_KIND:
                base = EVAL_TRIPS;
                break;
            case HAND_STRAIGHT:
                base = EVAL_STRAIGHT;
                break;
            case HAND_FLUSH:
                base = EVAL_FLUSH;
                break;
            case HAND_FULL_HOUSE:
                base = EVAL_FULL_HOUSE;
                break;
            case HAND_FOUR_KIND:
                base = EVAL_QUADS;
                break;
            case HAND_STRAIGHT_FLUSH:
                base = EVAL_STRAIGHT_FLUSH;
                break;
            case HAND_MAX:
            default:
                base = 0;
                break;
            }
            uint32_t off = (p_eval > base) ? (p_eval - base) : 0u;
            if (off > 999999u)
                off = 999999u;
            uint32_t coarse = 0;
            if (st->bucket_thresh_count > 0)
            {
                for (int i = 0; i < st->bucket_thresh_count; i++)
                {
                    if (off < st->bucket_thresh[i])
                        break;
                    coarse++;
                }
                if (coarse > 15u)
                    coarse = 15u;
            }
            else
            {
                unsigned char bins = st->bucket_bins ? st->bucket_bins : 8;
                if (bins < 1)
                    bins = 1;
                if (bins > 16)
                    bins = 16;
                uint32_t width = 1000000u / (uint32_t)bins;
                if (width == 0)
                    width = 1;
                coarse = off / width;
                if (coarse >= bins)
                    coarse = bins - 1;
            }
            return ((uint64_t)p_cls << 56) | ((uint64_t)coarse << 48) | (act << 16) | (uint64_t)(st->raises_left & 0xF) | ((uint64_t)(p & 1) << 4);
        }
        return ((uint64_t)p_cls << 56) | (act << 16) | (uint64_t)(st->raises_left & 0xF) | ((uint64_t)(p & 1) << 4);
    }
    return (act << 16) | (uint64_t)(st->raises_left & 0xF) | ((uint64_t)(p & 1) << 4);
}

void stud_build_game(const EvalContext *ctx, mask_t seven0, mask_t seven1, cfr_game_t *out_game, stud_state_t *out_state)
{
    memset(out_state, 0, sizeof(*out_state));
    memset(out_game, 0, sizeof(*out_game));
    out_state->seven0 = seven0;
    out_state->seven1 = seven1;
    out_state->to_act = 0;
    out_state->pot = 1.0;
    out_state->to_call = 0.0;
    out_state->raises_left = 0;
    out_state->raise_cap = 2;
    out_state->n_bet_sizes = 4;
    out_state->bet_fracs[0] = 1.0 / 3.0;
    out_state->bet_fracs[1] = 0.5;
    out_state->bet_fracs[2] = 0.75;
    out_state->bet_fracs[3] = 1.0;
    out_state->ctx = ctx;
    out_state->bucket_mode = 3;
    out_state->bucket_bins = 8;
    out_state->bucket_thresh_count = 0;
    out_game->initial_state = out_state;
    out_game->game_data = out_state;
    out_game->is_terminal = stud_is_terminal_wrapper;
    out_game->get_utility = stud_get_utility_wrapper;
    out_game->get_actions = stud_get_actions_wrapper;
    out_game->apply_action = stud_apply_action_wrapper;
    out_game->current_player = stud_current_player_wrapper;
    out_game->num_players = 2;
    out_game->state_size = sizeof(*out_state);
}
