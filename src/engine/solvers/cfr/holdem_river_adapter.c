/*
 * holdem_river_adapter.c - HU river minimal adapter
 */

#include <poker_eval/engine/solvers/cfr/holdem_river_adapter.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <poker_eval/core/eval_context.h>
#include <poker_eval/engine/solvers/cfr/board_canonical.h>

static bool hr_is_terminal(const void *s, double util[2]);
static int hr_num_actions(const void *s);
static void hr_apply_action(const void *s, int a, void *out);
static int hr_should_trace(void);
static int hr_player_to_act(const void *s);

// Wrapper functions to adapt to new cfr_game_t interface

static int hr_is_terminal_wrapper(cfr_game_t *game, uint64_t state_key, void *user_data)
{
    double util[2];
    return hr_is_terminal((void *)state_key, util);
}

static double hr_get_utility_wrapper(cfr_game_t *game, uint64_t state_key, int player, void *user_data)
{
    double util[2];
    hr_is_terminal((void *)state_key, util); // This is inefficient, but necessary with the old interface
    return util[player];
}

static int hr_get_actions_wrapper(cfr_game_t *game, uint64_t state_key, int *out_actions, int max_actions, void *user_data)
{
    int n = hr_num_actions((void *)state_key);
    for (int i = 0; i < n && i < max_actions; ++i)
    {
        out_actions[i] = i;
    }
    return n < max_actions ? n : max_actions;
}

static uint64_t hr_apply_action_wrapper(cfr_game_t *game, uint64_t state_key, int action, void *user_data)
{
    holdem_river_state_t *next_state = malloc(sizeof(holdem_river_state_t));
    hr_apply_action((void *)state_key, action, next_state);
    return (uint64_t)next_state;
}

static void hr_release_state_wrapper(cfr_game_t *game, uint64_t state_key, void *user_data)
{
    (void)game;
    (void)user_data;
    free((void *)state_key);
}

static int hr_current_player_wrapper(cfr_game_t *game, uint64_t state_key, void *user_data)
{
    return hr_player_to_act((const void *)state_key);
}

static bool hr_is_terminal(const void *s, double util[2])
{
    const holdem_river_state_t *st = (const holdem_river_state_t *)s;
    /* hist bits: 0=checked by P0, 1=checked by P1; 2=P0 bet; 3=P1 bet; 4=call completed; 5=fold flag */
    if (st->hist & (1u << 5))
    {
        /* someone folded: the other wins the entire pot */
        int winner = 1 - st->to_act; /* the player who just bet */
        util[winner] = st->pot;
        util[1 - winner] = -st->pot;
        if (hr_should_trace())
        {
            fprintf(stderr, "[hr] terminal fold hist=0x%X to_act=%d pot=%.6f util0=%.6f util1=%.6f\n",
                    st->hist,
                    st->to_act,
                    st->pot,
                    util[0],
                    util[1]);
        }
        return true;
    }
    /* showdown if check-check (bits 0 and 1) or bet-call (bits 2 or 3 and 4) */
    bool check_check = ((st->hist & 0x3) == 0x3);
    bool bet_call = (((st->hist & (1u << 2)) && (st->hist & (1u << 4))) || ((st->hist & (1u << 3)) && (st->hist & (1u << 4))));
    if (check_check || bet_call)
    {
        /* showdown: evaluate best 5-card hands */
        mask_t seven0 = st->h0 | st->board;
        mask_t seven1 = st->h1 | st->board;
        eval_t v0 = pe_eval_7c(st->ctx, seven0);
        eval_t v1 = pe_eval_7c(st->ctx, seven1);
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
        if (hr_should_trace())
        {
            fprintf(stderr, "[hr] terminal showdown hist=0x%X to_act=%d pot=%.6f util0=%.6f util1=%.6f\n",
                    st->hist,
                    st->to_act,
                    st->pot,
                    util[0],
                    util[1]);
        }
        return true;
    }
    return false;
}

static int hr_player_to_act(const void *s) { return ((const holdem_river_state_t *)s)->to_act; }

static int hr_num_actions(const void *s)
{
    const holdem_river_state_t *st = (const holdem_river_state_t *)s;
    if (st->to_call > 0.0)
    {
        /* facing bet: call, fold, and maybe raises (num_bet_sizes) if raises_left */
        return (st->raises_left > 0) ? (2 + st->num_bet_sizes) : 2; /* 0=call,1=fold,2.. raise sizes */
    }
    else
    {
        /* no bet: check + bet sizes */
        return 1 + st->num_bet_sizes; /* 0=check,1.. bet sizes */
    }
}

static int hr_trace_enabled = -1;

static int hr_should_trace(void)
{
    if (hr_trace_enabled == -1)
    {
        const char *env = getenv("HR_TRACE");
        hr_trace_enabled = (env && *env) ? 1 : 0;
    }
    return hr_trace_enabled;
}

static void hr_trace_state(const char *stage, int action, const holdem_river_state_t *st)
{
    if (!hr_should_trace())
        return;
    fprintf(stderr,
            "[hr] %s action=%d hist=0x%X to_act=%d to_call=%.6f raises_left=%d "
            "raise_cap=%d num_bet_sizes=%d pot=%.6f\n",
            stage,
            action,
            st->hist,
            st->to_act,
            st->to_call,
            st->raises_left,
            st->raise_cap,
            st->num_bet_sizes,
            st->pot);
}

static void hr_apply_action(const void *s, int a, void *out)
{
    const holdem_river_state_t *st = (const holdem_river_state_t *)s;
    holdem_river_state_t *ns = (holdem_river_state_t *)out;
    *ns = *st;
    int trace = hr_should_trace();
    if (trace)
    {
        hr_trace_state("before", a, st);
    }

    if (ns->to_call > 0.0)
    {
        /* Facing bet: a=0 call, a=1 fold, a>=2 raise (if raises_left) */
        if (a == 0)
        {
            ns->pot += ns->to_call; /* caller adds to pot */
            ns->to_call = 0.0;
            ns->hist |= (1u << 4);       /* call completed */
            ns->to_act = 1 - ns->to_act; /* not used post-terminal */
            goto done;
        }
        else if (a == 1)
        {
            ns->hist |= (1u << 5); /* fold */
            goto done;
        }
        else if (a >= 2 && ns->raises_left > 0)
        {
            int idx = a - 2;
            if (idx < 0 || idx >= ns->num_bet_sizes)
                goto done;
            double raise_amt = ns->bet_fracs[idx] * ns->pot;
            ns->pot += ns->to_call + raise_amt;
            ns->to_call = raise_amt;
            ns->raises_left -= 1;
            ns->to_act = 1 - ns->to_act;
            goto done;
        }
        goto done;
    }
    else
    {
        /* Not facing bet: a=0 check, a>=1 bet sizes */
        if (a == 0)
        {
            ns->hist |= (1u << (ns->to_act));
            ns->to_act = 1 - ns->to_act;
            goto done;
        }
        else if (a >= 1)
        {
            int idx = a - 1;
            if (idx < 0 || idx >= ns->num_bet_sizes)
                goto done;
            ns->hist |= (1u << (2 + ns->to_act));
            double amt = ns->bet_fracs[idx] * ns->pot;
            ns->pot += amt; /* bettor adds bet */
            ns->to_act = 1 - ns->to_act;
            ns->to_call = amt;               /* opponent faces to_call */
            ns->raises_left = ns->raise_cap; /* allow multiple raises */
            goto done;
        }
        goto done;
    }

done:
    if (trace)
    {
        hr_trace_state("after", a, ns);
    }
    return;
}

static uint64_t hr_infoset_key(const void *s)
{
    const holdem_river_state_t *st = (const holdem_river_state_t *)s;
    /* Bucketed key controlled by bucket_mode */
    int p = st->to_act;
    /* Compact action state: hist (16 bits), to_call flag */
    uint64_t act = ((uint64_t)(st->hist & 0xFFFF) << 8) | ((st->to_call > 0.0) ? 1ULL : 0ULL);

    if (st->bucket_mode == 0)
    {
        /* Minimal: action-only + raises_left + player */
        uint64_t tf = ((uint64_t)(st->extra_feats & 0xFF) << 40);
        return tf | (act << 16) | (uint64_t)(st->raises_left & 0xF) | ((uint64_t)(p & 1) << 4);
    }

    /* Board class (public) */
    eval_t b_eval = pe_eval_5c(st->ctx, st->board);
    hand_class_t bcl = eval_get_hand_class(b_eval);
    int b_cls = ((int)bcl) & 0xF;

    if (st->bucket_mode == 1)
    {
        /* Board-only bucketing */
        uint64_t tf = ((uint64_t)(st->extra_feats & 0xFF) << 40);
        return ((uint64_t)b_cls << 56) | tf | (act << 16) | (uint64_t)(st->raises_left & 0xF) | ((uint64_t)(p & 1) << 4);
    }

    /* Private evaluation */
    mask_t hp = (p == 0) ? st->h0 : st->h1;
    eval_t p_eval = pe_eval_7c(st->ctx, hp | st->board);
    hand_class_t pcl = eval_get_hand_class(p_eval);
    int p_cls = ((int)pcl) & 0xF;

    if (st->bucket_mode == 3)
    {
        /* Board + private class + coarse strength bin within class (configurable) */
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
            base = 0;
            break;
        default:
            base = 0;
            break;
        }
        uint32_t off = (p_eval > base) ? (p_eval - base) : 0u; /* expected < 1,000,000 */
        if (off > 999999u)
            off = 999999u;
        uint32_t coarse = 0;
        if (st->bucket_thresh_count > 0)
        {
            /* threshold-defined bins: thresholds are increasing cut points in [0..999999] */
            unsigned char i;
            coarse = 0;
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
            unsigned char bins = st->bucket_bins ? (unsigned char)st->bucket_bins : 8;
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

    /* Default: board + private class */
    {
        uint64_t tf = ((uint64_t)(st->extra_feats & 0xFF) << 40);
        return ((uint64_t)b_cls << 56) | ((uint64_t)p_cls << 52) | tf | (act << 16) | (uint64_t)(st->raises_left & 0xF) | ((uint64_t)(p & 1) << 4);
    }
}

static size_t hr_state_size(void)
{
    return sizeof(holdem_river_state_t);
}

void hr_build_game(const EvalContext *ctx, mask_t h0, mask_t h1, mask_t board, cfr_game_t *out_game, holdem_river_state_t *out_state)
{
    memset(out_state, 0, sizeof(*out_state));
    memset(out_game, 0, sizeof(*out_game));
    /* FEAT-02: canonicalize suits so isomorphic boards share one subtree.
     * The recorded permutation maps canonical suit labels back to the
     * original suits at export time. */
    {
        mask_t canon = MASK_EMPTY;
        int perm[4];
        int n = pe_board_count_cards(h0 | h1 | board);
        if (pe_board_canonicalize(h0 | h1 | board, n, &canon, perm) == 0)
        {
            int label_of[4];
            for (int s = 0; s < 4; ++s)
                label_of[s] = -1;
            for (int l = 0; l < 4; ++l)
                if (perm[l] >= 0)
                    label_of[perm[l]] = l;
            out_state->h0 = MASK_EMPTY;
            out_state->h1 = MASK_EMPTY;
            out_state->board = MASK_EMPTY;
            for (int card = 0; card < MODERN_DECK_SIZE; ++card)
            {
                if (!mask_is_set(h0 | h1 | board, card))
                    continue;
                int rank = MODERN_GET_RANK(card);
                int suit = MODERN_GET_SUIT(card);
                if (label_of[suit] < 0)
                    continue;
                mask_t c = mask_set(MASK_EMPTY, MODERN_MAKE_CARD(rank, label_of[suit]));
                if (mask_is_set(h0, card))
                    out_state->h0 |= c;
                else if (mask_is_set(h1, card))
                    out_state->h1 |= c;
                else
                    out_state->board |= c;
            }
            for (int l = 0; l < 4; ++l)
                out_state->suit_perm[l] = perm[l];
        }
        else
        {
            out_state->h0 = h0;
            out_state->h1 = h1;
            out_state->board = board;
            for (int l = 0; l < 4; ++l)
                out_state->suit_perm[l] = -1;
        }
    }
    out_state->to_act = 0;
    out_state->pot = 1.0;
    out_state->to_call = 0.0;
    out_state->bet_half = 0.5;
    out_state->bet_pot = 1.0;
    out_state->raises_left = 0;
    out_state->raise_cap = 2;
    out_state->num_bet_sizes = 4;
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
    out_game->is_terminal = hr_is_terminal_wrapper;
    out_game->get_utility = hr_get_utility_wrapper;
    out_game->get_actions = hr_get_actions_wrapper;
    out_game->apply_action = hr_apply_action_wrapper;
    out_game->release_state = hr_release_state_wrapper;
    out_game->current_player = hr_current_player_wrapper;
    out_game->num_players = 2;
    out_game->state_size = sizeof(*out_state);
}
