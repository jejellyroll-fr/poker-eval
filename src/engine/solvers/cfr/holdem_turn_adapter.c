/*
 * holdem_turn_adapter.c - build a river game by sampling a river card from turn
 */

#include <poker_eval/engine/solvers/cfr/holdem_turn_adapter.h>
#include <poker_eval/core/eval_context.h>
#include <poker_eval/engine/solvers/cfr/holdem_river_adapter.h>
#include <stdlib.h>
#include <poker_eval/core/modern_cardmask.h>

static int sample_river_card(mask_t used, unsigned seed)
{
    srand(seed);
    for (int tries = 0; tries < 10000; ++tries)
    {
        int c = rand() % 52;
        if (!mask_is_set(used, c))
            return c;
    }
    return -1;
}

void ht_build_game_sampled_river(const EvalContext *ctx, mask_t h0, mask_t h1, mask_t board4, cfr_game_t *out_game, ht_state_t *out_state, unsigned seed)
{
    out_state->h0 = h0;
    out_state->h1 = h1;
    out_state->board4 = board4;
    mask_t used = h0 | h1 | board4;
    int rv = sample_river_card(used, seed);
    out_state->full_board = board4;
    if (rv >= 0)
        out_state->full_board = mask_set(out_state->full_board, rv);
    hr_build_game(ctx, h0, h1, out_state->full_board, out_game, &out_state->river_state);
    /* FEAT-13 (#192): remember the turn board so texture merging can also
     * collapse turn nodes reached from different turn runs. */
    out_state->river_state.turn_board = board4;
    /* Compute turn features on board4:
     *  bit0=pair_on_board4
     *  bit1=flush_draw_board4 (>=3 same suit)
     *  bit2=straight_draw_board4_any (4-to-straight in any 5-rank window)
     *  bit3=blk_flush (private)
     *  bit4=blk_straight (private)
     *  bit5=trips_on_board4
     *  bit6=quads_on_board4
     *  bit7=straight_draw_open_ended (subset of bit2)
     */
    unsigned char feats = 0;
    /* pair_on_board4 */
    for (int r = 0; r < 13; r++)
    {
        mask_t rm = mask_get_rank_mask(board4, r);
        int c = mask_popcount(rm);
        if (c >= 2)
        {
            feats |= 1u;
        }
        if (c == 3)
        {
            feats |= (1u << 5);
        }
        if (c >= 4)
        {
            feats |= (1u << 6);
        }
        if (feats & 1u && (feats & (1u << 5)) && (feats & (1u << 6)))
            break; /* early if all ranks processed */
    }
    /* flush_draw_board4 */
    for (int s = 0; s < 4; s++)
    {
        mask_t sm = mask_get_suit_mask(board4, s);
        if (mask_popcount(sm) >= 3)
        {
            feats |= (1u << 1);
            break;
        }
    }
    /* straight_draw_board4 */
    /* Build rank presence mask */
    unsigned rank_bits = 0;
    for (int r = 0; r < 13; r++)
    {
        mask_t rm = mask_get_rank_mask(board4, r);
        if (mask_popcount(rm) >= 1)
            rank_bits |= (1u << r);
    }
    /* straight draws: detect any (bit2) and open-ended (bit7) */
    for (int i = 0; i <= 8; i++)
    {
        unsigned w = (rank_bits >> i) & 0x1Fu; /* window of 5 ranks */
        int cnt = __builtin_popcount(w);
        if (cnt >= 4)
        {
            feats |= (1u << 2);
            /* open-ended patterns in a 5-bit window with 4 bits set are 11110 (0x1E) or 01111 (0x0F) */
            if (cnt == 4 && (w == 0x1E || w == 0x0F))
                feats |= (1u << 7);
            /* if multiple windows, once flagged we can stop */
            if ((feats & (1u << 2)) && (feats & (1u << 7)))
                break;
        }
    }
    /* Private blockers (union over both players) */
    /* Flush blocker: if board has flush draw (>=3 same suit) and any player holds that suit */
    if (feats & (1u << 1))
    {
        for (int s = 0; s < 4; s++)
        {
            mask_t sm = mask_get_suit_mask(board4, s);
            if (mask_popcount(sm) >= 3)
            {
                if (mask_popcount(mask_get_suit_mask(h0, s)) >= 1 || mask_popcount(mask_get_suit_mask(h1, s)) >= 1)
                {
                    feats |= (1u << 3);
                }
            }
        }
    }
    /* Straight blocker: if board is 3-to-straight in a window, and any player holds a rank in that window */
    unsigned rank_bits_h0 = 0, rank_bits_h1 = 0;
    for (int r = 0; r < 13; r++)
    {
        if (mask_popcount(mask_get_rank_mask(h0, r)) >= 1)
            rank_bits_h0 |= (1u << r);
        if (mask_popcount(mask_get_rank_mask(h1, r)) >= 1)
            rank_bits_h1 |= (1u << r);
    }
    for (int i = 0; i <= 8; i++)
    {
        unsigned w = (rank_bits >> i) & 0x1Fu; /* board window */
        int cnt = __builtin_popcount(w);
        if (cnt >= 3)
        {
            unsigned hw = (((rank_bits_h0 | rank_bits_h1) >> i) & 0x1Fu);
            if (hw)
            {
                feats |= (1u << 4);
                break;
            }
        }
    }
    out_state->river_state.extra_feats = feats;
}
