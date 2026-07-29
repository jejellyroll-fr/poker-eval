/* Triple draw rules implementation */

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/games/triple_draw.h>
#include <poker_eval/games/eval_triple_draw.h>

LowHandVal TripleDraw_27_EVAL_N(StdDeck_CardMask cards, int n_cards)
{
    return TripleDraw_27_EVAL_INLINE(cards, n_cards);
}

LowHandVal TripleDraw_A5_EVAL_N(StdDeck_CardMask cards, int n_cards)
{
    return TripleDraw_A5_EVAL_INLINE(cards, n_cards);
}

void TripleDraw_InitHandState(TripleDrawHandState *state)
{
    if (!state)
        return;
    StdDeck_CardMask_RESET(state->initial_hand);
    StdDeck_CardMask_RESET(state->current_hand);
    state->draw_round = 0;
    state->total_cards = 0;
    for (int i = 0; i < 3; i++)
        state->cards_discarded[i] = 0;
}

int TripleDraw_AddCard(TripleDrawHandState *state, int card)
{
    if (!state || card < 0 || card >= StdDeck_N_CARDS)
        return 0;
    if (state->total_cards >= 5)
        return 0;
    if (StdDeck_CardMask_CARD_IS_SET(state->current_hand, card))
        return 0;
    StdDeck_CardMask_SET(state->current_hand, card);
    state->total_cards++;
    if (state->total_cards == 1)
        StdDeck_CardMask_SET(state->initial_hand, card);
    return 1;
}

int TripleDraw_RemoveCard(TripleDrawHandState *state, int card)
{
    if (!state || card < 0 || card >= StdDeck_N_CARDS)
        return 0;
    if (!StdDeck_CardMask_CARD_IS_SET(state->current_hand, card))
        return 0;
    StdDeck_CardMask_UNSET(state->current_hand, card);
    state->total_cards--;
    if (state->draw_round >= 0 && state->draw_round < 3)
        state->cards_discarded[state->draw_round]++;
    return 1;
}

int TripleDraw_GetHandSize(const TripleDrawHandState *state)
{
    return state ? state->total_cards : 0;
}
