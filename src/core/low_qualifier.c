#include <poker_eval/core/low_qualifier.h>
#include <poker_eval/deck/deck_std.h>

static int rotate_rank_for_low(int rank) {
    return (rank == StdDeck_Rank_ACE) ? 0 : (rank + 1);
}

bool pe_low_qualify5(LowHandVal hand_low, low_qualifier_t qualifier) {
    if (hand_low == LowHandVal_NOTHING)
        return false;

    int worst_rank = LowHandVal_TOP_CARD(hand_low);

    switch (qualifier) {
        case LOW_QUALIFIER_NONE:
            return true;
        case LOW_QUALIFIER_8:
            return worst_rank <= rotate_rank_for_low(StdDeck_Rank_8);
        case LOW_QUALIFIER_7:
            return worst_rank <= rotate_rank_for_low(StdDeck_Rank_7);
        default:
            return false;
    }
}
