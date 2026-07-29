/* Pineapple rules implementation */

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/games/rules_pineapple.h>
#include <poker_eval/core/eval.h>

HandVal Pineapple_EVAL(StdDeck_CardMask pocket, StdDeck_CardMask board)
{
    int pocket_cards[3];
    int num_pocket = 0;
    HandVal best_hand = HandVal_NOTHING;
    for (int i = 0; i < StdDeck_N_CARDS && num_pocket < 3; i++)
        if (StdDeck_CardMask_CARD_IS_SET(pocket, i))
            pocket_cards[num_pocket++] = i;
    if (num_pocket != 3)
        return HandVal_NOTHING;
    for (int discard = 0; discard < 3; discard++)
    {
        StdDeck_CardMask two_card_hand;
        StdDeck_CardMask full_hand;
        StdDeck_CardMask_RESET(two_card_hand);
        for (int i = 0; i < 3; i++)
            if (i != discard)
                StdDeck_CardMask_SET(two_card_hand, pocket_cards[i]);
        StdDeck_CardMask_OR(full_hand, two_card_hand, board);
        HandVal hv = StdDeck_StdRules_EVAL_N(full_hand, 7);
        if (hv > best_hand)
            best_hand = hv;
    }
    return best_hand;
}

StdDeck_CardMask Pineapple_FindBestDiscard(StdDeck_CardMask pocket, StdDeck_CardMask board)
{
    int pocket_cards[3];
    int num_pocket = 0;
    HandVal best_hand = HandVal_NOTHING;
    StdDeck_CardMask best_two;
    StdDeck_CardMask_RESET(best_two);
    for (int i = 0; i < StdDeck_N_CARDS && num_pocket < 3; i++)
        if (StdDeck_CardMask_CARD_IS_SET(pocket, i))
            pocket_cards[num_pocket++] = i;
    if (num_pocket != 3)
        return best_two;
    for (int discard = 0; discard < 3; discard++)
    {
        StdDeck_CardMask two;
        StdDeck_CardMask full;
        StdDeck_CardMask_RESET(two);
        for (int i = 0; i < 3; i++)
            if (i != discard)
                StdDeck_CardMask_SET(two, pocket_cards[i]);
        StdDeck_CardMask_OR(full, two, board);
        HandVal hv = StdDeck_StdRules_EVAL_N(full, 7);
        if (hv > best_hand)
        {
            best_hand = hv;
            best_two = two;
        }
    }
    return best_two;
}
