/*
 * omaha_river_adapter.c - Omaha (high-only) HU river adapter
 * Simplified version using 7-card evaluation
 */

#include <poker_eval/engine/solvers/cfr/omaha_river_adapter.h>
#include <poker_eval/engine/solvers/cfr/board_canonical.h>
#include <string.h>
#include <stdlib.h>

static bool omaha_is_terminal(const void *s, double util[2]);
static int omaha_num_actions(const void *s);
static bool omaha_apply_action(const void *s, int a, void *out);
static int omaha_player_to_act(const void *s);

static int omaha_is_terminal_wrapper(cfr_game_t* game, uint64_t state_key, void* user_data) {
    double util[2];
    return omaha_is_terminal((void*)state_key, util);
}

static double omaha_get_utility_wrapper(cfr_game_t* game, uint64_t state_key, int player, void* user_data) {
    double util[2];
    omaha_is_terminal((void*)state_key, util);
    return util[player];
}

static int omaha_get_actions_wrapper(cfr_game_t* game, uint64_t state_key, int* out_actions, int max_actions, void* user_data) {
    int n = omaha_num_actions((void*)state_key);
    for (int i = 0; i < n && i < max_actions; ++i) {
        out_actions[i] = i;
    }
    return n < max_actions ? n : max_actions;
}

static uint64_t omaha_apply_action_wrapper(cfr_game_t* game, uint64_t state_key, int action, void* user_data) {
    omaha_river_state_t* next_state = malloc(sizeof(omaha_river_state_t));
   omaha_apply_action((void*)state_key, action, next_state);
    return (uint64_t)next_state;
}

static void omaha_release_state_wrapper(cfr_game_t* game, uint64_t state_key, void* user_data) {
    (void)game;
    (void)user_data;
    free((void*)state_key);
}

static int omaha_current_player_wrapper(cfr_game_t *game, uint64_t state_key, void *user_data)
{
    return omaha_player_to_act((const void *)state_key);
}

static eval_t eval_omaha_best(const EvalContext *ctx, mask_t hole4, mask_t board5)
{
    return pe_eval_7c(ctx, hole4 | board5);
}

static bool omaha_is_terminal(const void *s, double util[2])
{
    const omaha_river_state_t *st = (const omaha_river_state_t *)s;
    if (st->hist & (1u << 5))
    {
        int winner = 1 - st->to_act;
        util[winner] = st->pot;
        util[1 - winner] = -st->pot;
        return true;
    }
    bool check_check = ((st->hist & 0x3) == 0x3);
    bool bet_call = (((st->hist & (1u << 2)) && (st->hist & (1u << 4))) ||
                     ((st->hist & (1u << 3)) && (st->hist & (1u << 4))));
    if (check_check || bet_call)
    {
        eval_t v0 = eval_omaha_best(st->ctx, st->h0, st->board);
        eval_t v1 = eval_omaha_best(st->ctx, st->h1, st->board);
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

static int omaha_player_to_act(const void *s) { return ((const omaha_river_state_t *)s)->to_act; }

static int omaha_num_actions(const void *s)
{
    const omaha_river_state_t *st = (const omaha_river_state_t *)s;
    return (st->to_call > 0.0) ? ((st->raises_left > 0) ? (2 + st->n_bet_sizes) : 2) : (1 + st->n_bet_sizes);
}

static bool omaha_apply_action(const void *s, int a, void *out)
{
    const omaha_river_state_t *st = (const omaha_river_state_t *)s;
    omaha_river_state_t *ns = (omaha_river_state_t *)out;
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

static uint64_t omaha_infoset_key(const void *s)
{
    const omaha_river_state_t *st = (const omaha_river_state_t *)s;
    int p = st->to_act;
    uint64_t act = ((uint64_t)(st->hist & 0xFFFF) << 8) | ((st->to_call > 0.0) ? 1ULL : 0ULL);
    if (st->bucket_mode == 0)
        return (act << 16) | (uint64_t)(st->raises_left & 0xF) | ((uint64_t)(p & 1) << 4);
    eval_t b_eval = pe_eval_5c(st->ctx, st->board);
    hand_class_t bcl = eval_get_hand_class(b_eval);
    int b_cls = ((int)bcl) & 0xF;
    if (st->bucket_mode == 1)
        return ((uint64_t)b_cls << 56) | (act << 16) | (uint64_t)(st->raises_left & 0xF) | ((uint64_t)(p & 1) << 4);
    mask_t hp = (p == 0) ? st->h0 : st->h1;
    if (st->bucket_mode == 4 && st->bucket_table)
    {
        /* FEAT-04: learned abstraction. The bucket id replaces both the private
         * hand class and the coarse strength bin: it already encodes how the
         * hand performs against the range, which is what those two fields were
         * approximating. 8 bits at <<48 allow k up to PE_BUCKET_MAX_CLUSTERS. */
        int bucket = pe_bucket_table_assign_cached(st->bucket_table, st->ctx, hp, st->board);
        if (bucket >= 0)
        {
            return ((uint64_t)b_cls << 56) | ((uint64_t)(bucket & 0xFF) << 48) | (act << 16) |
                   (uint64_t)(st->raises_left & 0xF) | ((uint64_t)(p & 1) << 4);
        }
        /* Assignment failed (degenerate board/hand): fall through to the
         * strength-threshold abstraction rather than collapsing the tree. */
    }
    /* FEAT-13 (#190): strength buckets (EHS/EHS2 k-means). The bucket replaces
     * the private hand class and coarse strength bin, exactly like FEAT-04 but
     * with the 2-D EHS/EHS2 abstraction. 8 bits at <<48 allow k up to
     * PE_SBK_MAX_BUCKETS. */
    if (st->bucket_mode == 5 && st->strength_table)
    {
        int bucket = pe_strength_table_assign_cached(st->strength_table, st->ctx, hp, st->board);
        if (bucket >= 0)
        {
            return ((uint64_t)b_cls << 56) | ((uint64_t)(bucket & 0xFF) << 48) | (act << 16) |
                   (uint64_t)(st->raises_left & 0xF) | ((uint64_t)(p & 1) << 4);
        }
        /* Assignment failed: fall through to coarse strength-threshold. */
    }
    /* FEAT-13 (#190): board-texture merging. Texture-indistinguishable boards
     * (per the configured filter level) must share one infoset, so the texture
     * id REPLACES the board class in the key: the high bits carry the texture
     * id (split across the former b_cls and bucket-id fields) and the former
     * private-hand class field is zeroed. Two boards with the same texture id
     * therefore produce identical keys regardless of suits/ranks. */
    if (st->bucket_mode == 6 && st->texture_level > 0)
    {
        uint64_t tid = pe_board_texture_id(st->board, (pe_texture_filter_level_t)st->texture_level);
        uint64_t tex_hi = (tid >> 4) & 0xF;   /* goes where b_cls was (<<56) */
        uint64_t tex_lo = (tid & 0xF);        /* goes where bucket id was (<<48) */
        return ((tex_hi << 56) | (tex_lo << 48) | (act << 16) |
                (uint64_t)(st->raises_left & 0xF) | ((uint64_t)(p & 1) << 4));
    }
    /* FEAT-13 (#190): combined strength buckets + texture merging (mode 7).
     * Both abstractions are applied at once: the hand is bucketed by EHS/EHS2
     * (bits 48..55) and the board is merged by texture (tex_hi at <<56,
     * tex_lo at <<44). This is the compatible "Strength Buckets + Texture
     * Filter" pairing and gives the largest state-space reduction. */
    if (st->bucket_mode == 7 && st->strength_table && st->texture_level > 0)
    {
        int bucket = pe_strength_table_assign_cached(st->strength_table, st->ctx, hp, st->board);
        uint64_t tid = pe_board_texture_id(st->board, (pe_texture_filter_level_t)st->texture_level);
        uint64_t tex_hi = (tid >> 4) & 0xF;
        uint64_t tex_lo = (tid & 0xF);
        if (bucket >= 0)
        {
            return ((tex_hi << 56) | ((uint64_t)(bucket & 0xFF) << 48) | (tex_lo << 44) |
                    (act << 16) | (uint64_t)(st->raises_left & 0xF) | ((uint64_t)(p & 1) << 4));
        }
        /* Strength assignment failed: fall through to texture-only merge. */
    }
    eval_t p_eval = eval_omaha_best(st->ctx, hp, st->board);
    hand_class_t pcl = eval_get_hand_class(p_eval);
    int p_cls = ((int)pcl) & 0xF;
    if (st->bucket_mode >= 2)
    {
        if (st->bucket_mode == 3 || st->bucket_mode == 4)
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
            return ((uint64_t)b_cls << 56) | ((uint64_t)p_cls << 52) | ((uint64_t)coarse << 48) | (act << 16) | (uint64_t)(st->raises_left & 0xF) | ((uint64_t)(p & 1) << 4);
        }
        return ((uint64_t)b_cls << 56) | ((uint64_t)p_cls << 52) | (act << 16) | (uint64_t)(st->raises_left & 0xF) | ((uint64_t)(p & 1) << 4);
    }
    return (act << 16) | (uint64_t)(st->raises_left & 0xF) | ((uint64_t)(p & 1) << 4);
}

void omaha_build_game(const EvalContext *ctx, mask_t h0, mask_t h1, mask_t board, cfr_game_t *out_game, omaha_river_state_t *out_state)
{
    memset(out_state, 0, sizeof(*out_state));
    memset(out_game, 0, sizeof(*out_game));
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
    out_state->bucket_table = NULL;
    out_state->strength_table = NULL;
    out_state->texture_level = 0;
    out_game->initial_state = out_state;
    out_game->game_data = out_state;
    out_game->is_terminal = omaha_is_terminal_wrapper;
    out_game->get_utility = omaha_get_utility_wrapper;
    out_game->get_actions = omaha_get_actions_wrapper;
    out_game->apply_action = omaha_apply_action_wrapper;
    out_game->get_infoset_key = omaha_infoset_key;
    out_game->release_state = omaha_release_state_wrapper;
    out_game->current_player = omaha_current_player_wrapper;
    out_game->num_players = 2;
    out_game->state_size = sizeof(*out_state);
}
