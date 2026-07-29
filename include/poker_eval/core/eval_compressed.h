/* eval_compressed.h - Evaluation functions using compressed tables
 *
 * Drop-in replacement for eval.h that uses compressed tables
 * for better cache performance.
 */

#ifndef __EVAL_COMPRESSED_H__
#define __EVAL_COMPRESSED_H__

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/utils/compressed_tables.h>
#include <poker_eval/deck/deck_std.h>

/* External tables */
extern uint8 nBitsTable[8192];
extern uint8 topCardTable[8192];
extern uint32 topBitTable[8192];

/* Macro versions using compressed tables */
#define COMPRESSED_STRAIGHT_VALUE(ranks) get_straight_value(ranks)
#define COMPRESSED_TOPFIVE_VALUE(ranks) get_topfive_value(ranks)

/* Compressed version of StdDeck_StdRules_EVAL_N */
static inline HandVal
StdDeck_StdRules_EVAL_N_COMPRESSED(StdDeck_CardMask cards, int n_cards)
{
    HandVal retval;
    uint32 ranks, four_mask, three_mask, two_mask,
        n_dups, n_ranks, is_st_or_fl = 0, sc, sd, sh, ss;

    sc = StdDeck_CardMask_CLUBS(cards);
    sd = StdDeck_CardMask_DIAMONDS(cards);
    sh = StdDeck_CardMask_HEARTS(cards);
    ss = StdDeck_CardMask_SPADES(cards);

    ranks = sc | sd | sh | ss;
    n_ranks = nBitsTable[ranks];
    n_dups = n_cards - n_ranks;

    /* Check for straight, flush, or straight flush, and return if we can
       determine immediately that this is the best possible hand */
    if (n_ranks >= 5)
    {
        if (nBitsTable[ss] >= 5)
        {
            if (COMPRESSED_STRAIGHT_VALUE(ss))
                return HandVal_HANDTYPE_VALUE(StdRules_HandType_STFLUSH) + (HandVal)(COMPRESSED_STRAIGHT_VALUE(ss) << StdRules_HandVal_SHIFT);
            else
                retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_FLUSH) + COMPRESSED_TOPFIVE_VALUE(ss);
            is_st_or_fl = 1;
        }
        else if (nBitsTable[sh] >= 5)
        {
            if (COMPRESSED_STRAIGHT_VALUE(sh))
                return HandVal_HANDTYPE_VALUE(StdRules_HandType_STFLUSH) + (HandVal)(COMPRESSED_STRAIGHT_VALUE(sh) << StdRules_HandVal_SHIFT);
            else
                retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_FLUSH) + COMPRESSED_TOPFIVE_VALUE(sh);
            is_st_or_fl = 1;
        }
        else if (nBitsTable[sd] >= 5)
        {
            if (COMPRESSED_STRAIGHT_VALUE(sd))
                return HandVal_HANDTYPE_VALUE(StdRules_HandType_STFLUSH) + (HandVal)(COMPRESSED_STRAIGHT_VALUE(sd) << StdRules_HandVal_SHIFT);
            else
                retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_FLUSH) + COMPRESSED_TOPFIVE_VALUE(sd);
            is_st_or_fl = 1;
        }
        else if (nBitsTable[sc] >= 5)
        {
            if (COMPRESSED_STRAIGHT_VALUE(sc))
                return HandVal_HANDTYPE_VALUE(StdRules_HandType_STFLUSH) + (HandVal)(COMPRESSED_STRAIGHT_VALUE(sc) << StdRules_HandVal_SHIFT);
            else
                retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_FLUSH) + COMPRESSED_TOPFIVE_VALUE(sc);
            is_st_or_fl = 1;
        }
        else
        {
            int st_value = COMPRESSED_STRAIGHT_VALUE(ranks);
            if (st_value)
            {
                retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_STRAIGHT) + (HandVal)(st_value << StdRules_HandVal_SHIFT);
                is_st_or_fl = 1;
            }
        }
    }

    /* By the time we're here, either:
       1) there's no five-card hand possible (n_ranks < 5), or
       2) there's a flush or straight, but we know that there are enough
          duplicates to make a full house / quads possible.  */
    switch (n_dups)
    {
    case 0:
        /* It's a no-pair hand */
        return HandVal_HANDTYPE_VALUE(StdRules_HandType_NOPAIR) + COMPRESSED_TOPFIVE_VALUE(ranks);
        break;

    case 1:
        /* It's a one-pair hand */
        two_mask = ranks ^ (sc ^ sd ^ sh ^ ss);
        retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_ONEPAIR) + (HandVal)(topCardTable[two_mask] << StdRules_HandVal_SHIFT);
        ranks ^= two_mask;
        retval += COMPRESSED_TOPFIVE_VALUE(ranks) >> 8;
        return retval;
        break;

    case 2:
        /* Either two pair or trips */
        two_mask = ranks ^ (sc ^ sd ^ sh ^ ss);
        if (two_mask)
        {
            /* It's two pair */
            retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_TWOPAIR) + (COMPRESSED_TOPFIVE_VALUE(two_mask) & StdRules_HandVal_TOP_TWOCARDS_MASK);
            ranks ^= two_mask;
            retval += (HandVal)(topCardTable[ranks] << StdRules_HandVal_THIRD_CARD_SHIFT);
            return retval;
        }
        else
        {
            /* It's trips */
            three_mask = ((sc & sd) | (sh & ss)) & ((sc & sh) | (sd & ss));
            retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_TRIPS) + (HandVal)(topCardTable[three_mask] << StdRules_HandVal_SHIFT);
            ranks ^= three_mask;
            retval += COMPRESSED_TOPFIVE_VALUE(ranks) >> 8;
            return retval;
        }
        break;

    default:
        /* Possible quads, fullhouse, straight or flush, or two pair */
        four_mask = sh & sd & sc & ss;
        if (four_mask)
        {
            /* Quads */
            retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_QUADS) + (HandVal)(topCardTable[four_mask] << StdRules_HandVal_SHIFT);
            ranks ^= four_mask;
            retval += (HandVal)(topCardTable[ranks] << StdRules_HandVal_KICKER_SHIFT);
            return retval;
        }

        /* Technically, three_mask as defined below is really the set of
           bits which are set in three or four of the suits.  But since
           we've already eliminated quads, this is OK. */
        three_mask = ((sc & sd) | (sh & ss)) & ((sc & sh) | (sd & ss));
        if (is_st_or_fl)
        {
            if (three_mask)
            {
                /* Fullhouse */
                retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_FULLHOUSE);
                retval += (HandVal)(topCardTable[three_mask] << StdRules_HandVal_SHIFT);
                two_mask = (ranks ^ three_mask) & (ranks ^ (three_mask & (three_mask - 1)));
                retval += (HandVal)(topCardTable[two_mask] << StdRules_HandVal_THIRD_CARD_SHIFT);
                return retval;
            }
            else
            {
                /* Flush or straight */
                return retval;
            }
        }
        else
        {
            if (three_mask)
            {
                /* Fullhouse */
                retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_FULLHOUSE);
                retval += (HandVal)(topCardTable[three_mask] << StdRules_HandVal_SHIFT);
                two_mask = (ranks ^ three_mask) & (ranks ^ (three_mask & (three_mask - 1)));
                retval += (HandVal)(topCardTable[two_mask] << StdRules_HandVal_THIRD_CARD_SHIFT);
                return retval;
            }
            else
            {
                /* Two pair */
                two_mask = ranks ^ (sc ^ sd ^ sh ^ ss);
                retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_TWOPAIR);
                retval += COMPRESSED_TOPFIVE_VALUE(two_mask) & StdRules_HandVal_TOP_TWOCARDS_MASK;
                ranks ^= two_mask;
                retval += (HandVal)(topCardTable[ranks] << StdRules_HandVal_THIRD_CARD_SHIFT);
                return retval;
            }
        }
        break;
    }

    /* Should never reach here */
    return retval;
}

#endif /* __EVAL_COMPRESSED_H__ */
