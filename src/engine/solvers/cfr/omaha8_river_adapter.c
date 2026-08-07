/*
 * omaha8_river_adapter.c - HU river adapter for Omaha Hi/Lo 8-or-better
 */

#include <poker_eval/engine/solvers/cfr/omaha8_river_adapter.h>
#include <string.h>
#include <stdlib.h>
#include <poker_eval/core/eval_context.h>
#include <poker_eval/core/low_eval.h>
#include <poker_eval/core/low_qualifier.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/games/eval_omaha.h>

static bool o8_is_terminal(const void *s, double util[2]);
static int o8_num_actions(const void *s);
static bool o8_apply_action(const void *s, int a, void *out);
static int o8_player_to_act(const void *s);

static int o8_is_terminal_wrapper(cfr_game_t* game, uint64_t state_key, void* user_data) {
    double util[2];
    return o8_is_terminal((void*)state_key, util);
}

static double o8_get_utility_wrapper(cfr_game_t* game, uint64_t state_key, int player, void* user_data) {
    double util[2];
    o8_is_terminal((void*)state_key, util);
    return util[player];
}

static int o8_get_actions_wrapper(cfr_game_t* game, uint64_t state_key, int* out_actions, int max_actions, void* user_data) {
    int n = o8_num_actions((void*)state_key);
    for (int i = 0; i < n && i < max_actions; ++i) {
        out_actions[i] = i;
    }
    return n < max_actions ? n : max_actions;
}

static uint64_t o8_apply_action_wrapper(cfr_game_t* game, uint64_t state_key, int action, void* user_data) {
    o8_state_t* next_state = malloc(sizeof(o8_state_t));
    o8_apply_action((void*)state_key, action, next_state);
    return (uint64_t)next_state;
}

static void o8_release_state_wrapper(cfr_game_t* game, uint64_t state_key, void* user_data) {
    (void)game;
    (void)user_data;
    free((void*)state_key);
}

static int o8_current_player_wrapper(cfr_game_t *game, uint64_t state_key, void *user_data)
{
    return o8_player_to_act((const void *)state_key);
}

/* Convert modern mask_t to StdDeck_CardMask */
static inline void mask_to_stddeck(mask_t m, StdDeck_CardMask *out)
{
    StdDeck_CardMask_RESET(*out);
    for (int c = 0; c < 52; c++)
        if (mask_is_set(m, c))
            StdDeck_CardMask_SET(*out, c);
}

static bool o8_is_terminal(const void *s, double util[2])
{
    const o8_state_t *st = (const o8_state_t *)s;
    /* Fold handling identical to Hold'em adapter */
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
        /* showdown: Omaha8 split evaluation */
        StdDeck_CardMask h0s, h1s, bs;
        mask_to_stddeck(st->h0, &h0s);
        mask_to_stddeck(st->h1, &h1s);
        mask_to_stddeck(st->board, &bs);
        HandVal hi0 = HandVal_NOTHING, hi1 = HandVal_NOTHING;
        LowHandVal lo0 = LowHandVal_NOTHING, lo1 = LowHandVal_NOTHING;
        int eval0 = StdDeck_OmahaHiLow8_EVAL(h0s, bs, &hi0, &lo0);
        int eval1 = StdDeck_OmahaHiLow8_EVAL(h1s, bs, &hi1, &lo1);
        if (eval0 != 0)
            hi0 = HandVal_NOTHING;
        if (eval1 != 0)
            hi1 = HandVal_NOTHING;
        enum_gameparams_t *gp = enumGameParams(game_omaha8);
        low_qualifier_t qualifier = gp ? gp->low_qualifier : LOW_QUALIFIER_8;
        if (eval0 != 0 || !pe_low_qualify5(lo0, qualifier))
            lo0 = LowHandVal_NOTHING;
        if (eval1 != 0 || !pe_low_qualify5(lo1, qualifier))
            lo1 = LowHandVal_NOTHING;
        /* Compute pot shares */
        double s0 = 0.0, s1 = 0.0;
        /* High half (or full pot if no low) */
        bool low_any = (lo0 != LowHandVal_NOTHING) || (lo1 != LowHandVal_NOTHING);
        double hi_pool = low_any ? (0.5 * st->pot) : st->pot;
        double lo_pool = low_any ? (0.5 * st->pot) : 0.0;
        if (hi0 > hi1)
            s0 += hi_pool;
        else if (hi1 > hi0)
            s1 += hi_pool;
        else
        {
            s0 += hi_pool * 0.5;
            s1 += hi_pool * 0.5;
        }
        if (low_any)
        {
            if (lo0 == LowHandVal_NOTHING && lo1 != LowHandVal_NOTHING)
                s1 += lo_pool;
            else if (lo1 == LowHandVal_NOTHING && lo0 != LowHandVal_NOTHING)
                s0 += lo_pool;
            else if (lo0 == LowHandVal_NOTHING && lo1 == LowHandVal_NOTHING)
            { /* shouldn't happen due to low_any */
            }
            else
            {
                if (lo0 < lo1)
                    s0 += lo_pool;
                else if (lo1 < lo0)
                    s1 += lo_pool;
                else
                {
                    s0 += lo_pool * 0.5;
                    s1 += lo_pool * 0.5;
                }
            }
        }
        util[0] = s0 - s1;
        util[1] = s1 - s0;
        return true;
    }
    return false;
}

static int o8_player_to_act(const void *s) { return ((const o8_state_t *)s)->to_act; }

static int o8_num_actions(const void *s)
{
    const o8_state_t *st = (const o8_state_t *)s;
    if (st->to_call > 0.0)
        return (st->raises_left > 0) ? (2 + st->n_bet_sizes) : 2;
    return 1 + st->n_bet_sizes;
}

static bool o8_apply_action(const void *s, int a, void *out)
{
    const o8_state_t *st = (const o8_state_t *)s;
    o8_state_t *ns = (o8_state_t *)out;
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
            double r = ns->bet_fracs[idx] * ns->pot;
            ns->pot += ns->to_call + r;
            ns->to_call = r;
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

static uint64_t o8_infoset_key(const void *s)
{
    const o8_state_t *st = (const o8_state_t *)s;
    int p = st->to_act;
    uint64_t act = ((uint64_t)(st->hist & 0xFFFF) << 8) | ((st->to_call > 0.0) ? 1ULL : 0ULL);
    if (st->bucket_mode == 0)
    {
        uint64_t tf = ((uint64_t)(st->extra_feats & 0xFF) << 40);
        return tf | (act << 16) | (uint64_t)(st->raises_left & 0xF) | ((uint64_t)(p & 1) << 4);
    }
    /* Board class via 5-card public board */
    eval_t b_eval = pe_eval_5c(st->ctx, st->board);
    hand_class_t bcl = eval_get_hand_class(b_eval);
    int b_cls = ((int)bcl) & 0xF;
    if (st->bucket_mode == 1)
    {
        uint64_t tf = ((uint64_t)(st->extra_feats & 0xFF) << 40);
        return ((uint64_t)b_cls << 56) | tf | (act << 16) | (uint64_t)(st->raises_left & 0xF) | ((uint64_t)(p & 1) << 4);
    }
    /* Private class from Omaha hi value under 2-from-hole/3-from-board */
    StdDeck_CardMask hs, bs;
    mask_to_stddeck((p == 0) ? st->h0 : st->h1, &hs);
    mask_to_stddeck(st->board, &bs);
    HandVal hival = HandVal_NOTHING;
    if (StdDeck_OmahaHiLow8_EVAL(hs, bs, &hival, NULL) != 0)
        hival = HandVal_NOTHING;
    hand_class_t pcl = eval_get_hand_class(hival);
    int p_cls = ((int)pcl) & 0xF;
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
        uint32_t off = (hival > base) ? (hival - base) : 0u;
        if (off > 999999u)
            off = 999999u;
        uint32_t coarse = 0;
        if (st->bucket_thresh_count > 0)
        {
            unsigned char i;
            for (i = 0; i < st->bucket_thresh_count; i++)
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
        uint64_t tf = ((uint64_t)(st->extra_feats & 0xFF) << 40);
        return ((uint64_t)b_cls << 56) | ((uint64_t)p_cls << 52) | ((uint64_t)coarse << 48) | tf | (act << 16) | (uint64_t)(st->raises_left & 0xF) | ((uint64_t)(p & 1) << 4);
    }
    {
        uint64_t tf = ((uint64_t)(st->extra_feats & 0xFF) << 40);
        return ((uint64_t)b_cls << 56) | ((uint64_t)p_cls << 52) | tf | (act << 16) | (uint64_t)(st->raises_left & 0xF) | ((uint64_t)(p & 1) << 4);
    }
}

void o8_build_game(const EvalContext *ctx, mask_t h0, mask_t h1, mask_t board, cfr_game_t *out_game, o8_state_t *out_state)
{
    memset(out_state, 0, sizeof(*out_state));
    memset(out_game, 0, sizeof(*out_game));
    out_state->h0 = h0;
    out_state->h1 = h1;
    out_state->board = board;
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
    out_state->extra_feats = 0;
    out_game->initial_state = out_state;
    out_game->game_data = out_state;
    out_game->is_terminal = o8_is_terminal_wrapper;
    out_game->get_utility = o8_get_utility_wrapper;
    out_game->get_actions = o8_get_actions_wrapper;
    out_game->apply_action = o8_apply_action_wrapper;
    out_game->release_state = o8_release_state_wrapper;
    out_game->current_player = o8_current_player_wrapper;
    out_game->num_players = 2;
    out_game->state_size = sizeof(*out_state);
}
