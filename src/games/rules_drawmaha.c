/*
 * Drawmaha rules (split pot: Omaha Hi + 5-card draw)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <poker_eval/games/rules_drawmaha.h>
#include <poker_eval/games/eval_omaha.h>
#include <poker_eval/core/eval.h>
#include <poker_eval/deck/deck_std.h>

int Drawmaha_InitDrawState(DrawmahaDrawState *state, StdDeck_CardMask initial_hand)
{
    if (state == NULL) return 1;
    if (StdDeck_numCards(initial_hand) != 5) return 1;
    state->original_hand = initial_hand;
    state->current_hand = initial_hand;
    state->cards_drawn = 0;
    state->draw_mask = 0;
    return 0;
}

int Drawmaha_ExecuteDraw(DrawmahaDrawState *state,
                         int discard_mask,
                         StdDeck_CardMask replacement_cards)
{
    if (state == NULL) return 1;
    if (!Drawmaha_IsValidDrawMask(discard_mask)) return 1;
    int cards_to_draw = Drawmaha_CountDrawCards(discard_mask);
    if (StdDeck_numCards(replacement_cards) != cards_to_draw) return 1;

    StdDeck_CardMask new_hand = state->original_hand;
    for (int i = 0; i < 5; i++) {
        if (discard_mask & (1 << i)) {
            int card_count = 0;
            for (int c = 0; c < StdDeck_N_CARDS; c++) {
                if (StdDeck_CardMask_CARD_IS_SET(state->original_hand, c)) {
                    if (card_count == i) {
                        StdDeck_CardMask_UNSET(new_hand, c);
                        break;
                    }
                    card_count++;
                }
            }
        }
    }
    StdDeck_CardMask_OR(new_hand, new_hand, replacement_cards);
    if (StdDeck_numCards(new_hand) != 5) return 1;
    state->current_hand = new_hand;
    state->cards_drawn = cards_to_draw;
    state->draw_mask = discard_mask;
    return 0;
}

int Drawmaha_FindOptimalDraw(StdDeck_CardMask hand,
                             StdDeck_CardMask board,
                             StdDeck_CardMask dead_cards,
                             int *optimal_discard_mask)
{
    if (optimal_discard_mask == NULL) return 1;
    double best_equity = -1.0;
    int best_mask = 0;
    for (int mask = 0; mask < 32; mask++) {
        if (!Drawmaha_IsValidDrawMask(mask)) continue;
        double equity = 0.5; /* Placeholder */
        if (equity > best_equity) { best_equity = equity; best_mask = mask; }
    }
    *optimal_discard_mask = best_mask;
    return 0;
}

int Drawmaha_EvaluateOptimal(StdDeck_CardMask hand,
                             StdDeck_CardMask board,
                             StdDeck_CardMask dead_cards,
                             HandVal *hival,
                             LowHandVal *loval)
{
    if (hival == NULL) return 1;
    return Drawmaha_EvaluateHand(hand, board, hival, loval);
}

int Drawmaha_EvaluateHand(StdDeck_CardMask hand,
                          StdDeck_CardMask board,
                          HandVal *hival,
                          LowHandVal *loval)
{
    if (hival == NULL) return 1;
    if (StdDeck_numCards(hand) != 5) return 1;
    if (StdDeck_numCards(board) < 3) {
        *hival = HandVal_NOTHING;
        if (loval) *loval = LowHandVal_NOTHING;
        return 0;
    }
    HandVal omaha_hi; LowHandVal dummy;
    int r = StdDeck_OmahaHiLow8_EVAL(hand, board, &omaha_hi, &dummy);
    if (r != 0) return r;
    HandVal draw_hand = StdDeck_StdRules_EVAL_N(hand, 5);
    *hival = omaha_hi;
    if (loval) *loval = (LowHandVal)draw_hand;
    return 0;
}

int Drawmaha_CalculateDrawEquity(StdDeck_CardMask hand,
                                 StdDeck_CardMask board,
                                 StdDeck_CardMask dead_cards,
                                 double *draw_equities)
{
    if (!draw_equities) return 1;
    for (int i = 0; i < 32; i++) draw_equities[i] = 0.5;
    return 0;
}

int Drawmaha_IsValidDrawMask(int draw_mask)
{
    if (draw_mask < 0 || draw_mask > 31) return 0;
    int count = Drawmaha_CountDrawCards(draw_mask);
    return (count >= 0 && count <= 5);
}

int Drawmaha_CountDrawCards(int draw_mask)
{
    int count = 0;
    for (int i = 0; i < 5; i++) if (draw_mask & (1 << i)) count++;
    return count;
}

int Drawmaha_DrawMaskToIndices(int draw_mask, int *indices)
{
    if (!indices) return 1;
    int count = 0;
    for (int i = 0; i < 5; i++) if (draw_mask & (1 << i)) indices[count++] = i;
    return count;
}

int Drawmaha_GenerateAllDrawMasks(int *draw_masks, int max_masks)
{
    if (!draw_masks || max_masks < 32) return -1;
    int count = 0;
    for (int mask = 0; mask < 32; mask++) if (Drawmaha_IsValidDrawMask(mask)) draw_masks[count++] = mask;
    return count;
}

