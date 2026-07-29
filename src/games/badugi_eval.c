/*
 * Copyright (C) 2024
 *           Poker-eval contributors
 *
 * This program gives you software freedom; you can copy, convey,
 * propagate, redistribute and/or modify this program under the terms of
 * the GNU General Public License (GPL) as published by the Free Software
 * Foundation (FSF), either version 3 of the License, or (at your option)
 * any later version of the GPL published by the FSF.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program in a file in the toplevel directory called "GPLv3".
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <poker_eval/games/badugi_eval.h>
#include <poker_eval/games/rules_badugi.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/core/low_eval.h>

/*
 * Cursor arithmetic for the bounded appends below.
 *
 * snprintf reports what it *would* have written, so adding its return value to
 * the cursor walks past the end of the buffer as soon as the output is
 * truncated. room_left keeps the pointer in bounds and advance_cursor clamps.
 */
static size_t room_left(size_t size, int length) {
    if (length < 0 || (size_t)length >= size) return 0;
    return size - (size_t)length;
}

static int advance_cursor(int length, size_t size, int written) {
    size_t room = room_left(size, length);
    if (written < 0 || room == 0) return length;
    if ((size_t)written >= room) return (int)size - 1;   /* truncated */
    return length + written;
}

/*
 * Get the best Badugi hand from a set of cards
 * Returns the number of cards in the best Badugi and fills best_badugi with the cards
 */
int BadugiEval_GetBestBadugi(StdDeck_CardMask cards, int n_cards,
                             StdDeck_CardMask *best_badugi, int *badugi_size)
{
    int ranks_used[StdDeck_Rank_COUNT] = {0};
    int suits_used[StdDeck_Suit_COUNT] = {0};
    int best_count = 0;
    int i, rank, suit;

    StdDeck_CardMask_RESET(*best_badugi);

    /* Iterate through all cards and build the best Badugi */
    for (i = 0; i < StdDeck_N_CARDS && best_count < 4; i++)
    {
        if (StdDeck_CardMask_CARD_IS_SET(cards, i))
        {
            rank = StdDeck_RANK(i);
            suit = StdDeck_SUIT(i);

            /* If we haven't used this rank or suit, add it to our Badugi */
            if (!ranks_used[rank] && !suits_used[suit])
            {
                ranks_used[rank] = 1;
                suits_used[suit] = 1;
                StdDeck_CardMask_SET(*best_badugi, i);
                best_count++;
            }
        }
    }

    *badugi_size = best_count;
    return best_count;
}

/*
 * Remove duplicate ranks and suits from a hand, keeping the lowest rank for each suit
 */
int BadugiEval_RemoveDuplicates(StdDeck_CardMask cards, int n_cards,
                                StdDeck_CardMask *unique_cards)
{
    int lowest_rank_in_suit[StdDeck_Suit_COUNT];
    int lowest_card_in_suit[StdDeck_Suit_COUNT];
    int ranks_used[StdDeck_Rank_COUNT] = {0};
    int unique_count = 0;
    int i, rank, suit;

    /* Initialize */
    for (i = 0; i < StdDeck_Suit_COUNT; i++)
    {
        lowest_rank_in_suit[i] = StdDeck_Rank_KING + 1; /* Higher than any real rank */
        lowest_card_in_suit[i] = -1;
    }

    StdDeck_CardMask_RESET(*unique_cards);

    /* Find lowest rank in each suit */
    for (i = 0; i < StdDeck_N_CARDS; i++)
    {
        if (StdDeck_CardMask_CARD_IS_SET(cards, i))
        {
            rank = StdDeck_RANK(i);
            suit = StdDeck_SUIT(i);

            /* Ace is low in Badugi */
            int badugi_rank = (rank == StdDeck_Rank_ACE) ? -1 : rank;

            if (badugi_rank < lowest_rank_in_suit[suit])
            {
                lowest_rank_in_suit[suit] = badugi_rank;
                lowest_card_in_suit[suit] = i;
            }
        }
    }

    /* Add the lowest card from each suit, but only if rank is unique */
    for (suit = 0; suit < StdDeck_Suit_COUNT; suit++)
    {
        if (lowest_card_in_suit[suit] != -1)
        {
            rank = StdDeck_RANK(lowest_card_in_suit[suit]);
            if (!ranks_used[rank])
            {
                ranks_used[rank] = 1;
                StdDeck_CardMask_SET(*unique_cards, lowest_card_in_suit[suit]);
                unique_count++;
            }
        }
    }

    return unique_count;
}

/*
 * Count unique suits in a hand
 */
int BadugiEval_CountUniqueSuits(StdDeck_CardMask cards, int n_cards)
{
    int suits_seen[StdDeck_Suit_COUNT] = {0};
    int unique_suits = 0;
    int i, suit;

    for (i = 0; i < StdDeck_N_CARDS; i++)
    {
        if (StdDeck_CardMask_CARD_IS_SET(cards, i))
        {
            suit = StdDeck_SUIT(i);
            if (!suits_seen[suit])
            {
                suits_seen[suit] = 1;
                unique_suits++;
            }
        }
    }

    return unique_suits;
}

/*
 * Count unique ranks in a hand
 */
int BadugiEval_CountUniqueRanks(StdDeck_CardMask cards, int n_cards)
{
    int ranks_seen[StdDeck_Rank_COUNT] = {0};
    int unique_ranks = 0;
    int i, rank;

    for (i = 0; i < StdDeck_N_CARDS; i++)
    {
        if (StdDeck_CardMask_CARD_IS_SET(cards, i))
        {
            rank = StdDeck_RANK(i);
            if (!ranks_seen[rank])
            {
                ranks_seen[rank] = 1;
                unique_ranks++;
            }
        }
    }

    return unique_ranks;
}

/*
 * Main Badugi evaluation function
 */
BadugiHandVal StdDeck_BadugiRules_EVAL_N(StdDeck_CardMask cards, int n_cards)
{
    StdDeck_CardMask unique_cards;
    int unique_count;
    int card_ranks[4];
    int i, j;
    BadugiHandVal retval;

    /* Get the best possible Badugi from the cards */
    unique_count = BadugiEval_RemoveDuplicates(cards, n_cards, &unique_cards);

    /* Collect ranks and sort them (Ace low) */
    j = 0;
    for (i = 0; i < StdDeck_N_CARDS && j < unique_count; i++)
    {
        if (StdDeck_CardMask_CARD_IS_SET(unique_cards, i))
        {
            int rank = StdDeck_RANK(i);
            card_ranks[j++] = (rank == StdDeck_Rank_ACE) ? -1 : rank;
        }
    }

    /* Sort ranks (bubble sort for simplicity) */
    for (i = 0; i < unique_count - 1; i++)
    {
        for (j = i + 1; j < unique_count; j++)
        {
            if (card_ranks[i] > card_ranks[j])
            {
                int temp = card_ranks[i];
                card_ranks[i] = card_ranks[j];
                card_ranks[j] = temp;
            }
        }
    }

    /* Convert back to standard ranks and invert for hi-only enumeration */
    for (i = 0; i < unique_count; i++)
    {
        if (card_ranks[i] == -1)
        {
            card_ranks[i] = StdDeck_Rank_ACE;
        }
        /* Invert rank values: better Badugi cards (lower) become higher values */
        card_ranks[i] = (StdDeck_Rank_KING - card_ranks[i] + StdDeck_Rank_ACE + 1) % (StdDeck_Rank_ACE + 1);
    }

    /* Build hand value */
    switch (unique_count)
    {
    case 4:
        retval = BadugiHandVal_HANDTYPE_VALUE(BadugiRules_HandType_BADUGI);
        retval |= BadugiHandVal_FIRST_CARD_VALUE(card_ranks[0]);
        retval |= BadugiHandVal_SECOND_CARD_VALUE(card_ranks[1]);
        retval |= BadugiHandVal_THIRD_CARD_VALUE(card_ranks[2]);
        retval |= BadugiHandVal_FOURTH_CARD_VALUE(card_ranks[3]);
        break;

    case 3:
        retval = BadugiHandVal_HANDTYPE_VALUE(BadugiRules_HandType_THREE);
        retval |= BadugiHandVal_FIRST_CARD_VALUE(card_ranks[0]);
        retval |= BadugiHandVal_SECOND_CARD_VALUE(card_ranks[1]);
        retval |= BadugiHandVal_THIRD_CARD_VALUE(card_ranks[2]);
        break;

    case 2:
        retval = BadugiHandVal_HANDTYPE_VALUE(BadugiRules_HandType_TWO);
        retval |= BadugiHandVal_FIRST_CARD_VALUE(card_ranks[0]);
        retval |= BadugiHandVal_SECOND_CARD_VALUE(card_ranks[1]);
        break;

    case 1:
        retval = BadugiHandVal_HANDTYPE_VALUE(BadugiRules_HandType_ONE);
        retval |= BadugiHandVal_FIRST_CARD_VALUE(card_ranks[0]);
        break;

    default:
        retval = BadugiHandVal_NOTHING;
        break;
    }

    return retval;
}

/*
 * 4-card Badugi evaluation
 */
BadugiHandVal StdDeck_BadugiRules_EVAL_4(StdDeck_CardMask cards)
{
    return StdDeck_BadugiRules_EVAL_N(cards, 4);
}

/*
 * 5-card Badugi evaluation (best 4 cards)
 */
BadugiHandVal StdDeck_BadugiRules_EVAL_5(StdDeck_CardMask cards)
{
    return StdDeck_BadugiRules_EVAL_N(cards, 5);
}

/* Convert between the distinct BadugiHandVal and HandVal bit layouts. */
static HandVal badugi_to_handval(BadugiHandVal badugi_val, int badugi_type)
{
    HandVal retval = HandVal_HANDTYPE_VALUE(badugi_type);

    retval |= HandVal_TOP_CARD_VALUE(BadugiHandVal_FIRST_CARD(badugi_val));
    if (badugi_type >= BadugiRules_HandType_TWO)
        retval |= HandVal_SECOND_CARD_VALUE(BadugiHandVal_SECOND_CARD(badugi_val));
    if (badugi_type >= BadugiRules_HandType_THREE)
        retval |= HandVal_THIRD_CARD_VALUE(BadugiHandVal_THIRD_CARD(badugi_val));
    if (badugi_type >= BadugiRules_HandType_BADUGI)
        retval |= HandVal_FOURTH_CARD_VALUE(BadugiHandVal_FOURTH_CARD(badugi_val));

    return retval;
}

/*
 * Badacey evaluation (Badugi + A-5 lowball)
 */
HandVal StdDeck_BadaceyRules_EVAL_N(StdDeck_CardMask cards, int n_cards)
{
    BadugiHandVal badugi_val = StdDeck_BadugiRules_EVAL_N(cards, n_cards);
    int badugi_type = BadugiHandVal_HANDTYPE(badugi_val);

    /* If we have any Badugi (even 1-card), use it */
    if (badugi_type >= BadugiRules_HandType_ONE &&
        badugi_type <= BadugiRules_HandType_BADUGI)
    {
        return badugi_to_handval(badugi_val, badugi_type);
    }

    /* No Badugi possible, evaluate as A-5 lowball */
    LowHandVal low_val = pe_eval_low_a5(cards);
    if (low_val != LowHandVal_NOTHING)
    {
        return HandVal_HANDTYPE_VALUE(BadaceyRules_HandType_A5_LOW) | LowHandVal_CARDS(low_val);
    }

    return HandVal_NOTHING;
}

/*
 * Badeucy evaluation (Badugi + 2-7 lowball)
 */
HandVal StdDeck_BadeucyRules_EVAL_N(StdDeck_CardMask cards, int n_cards)
{
    BadugiHandVal badugi_val = StdDeck_BadugiRules_EVAL_N(cards, n_cards);
    int badugi_type = BadugiHandVal_HANDTYPE(badugi_val);

    /* If we have any Badugi (even 1-card), use it */
    if (badugi_type >= BadugiRules_HandType_ONE &&
        badugi_type <= BadugiRules_HandType_BADUGI)
    {
        return badugi_to_handval(badugi_val, badugi_type);
    }

    /* No Badugi possible, evaluate as 2-7 lowball */
    /* For now, use regular lowball - 2-7 lowball would need separate implementation */
    LowHandVal low_val = pe_eval_low_a5(cards);
    if (low_val != LowHandVal_NOTHING)
    {
        return HandVal_HANDTYPE_VALUE(BadeucyRules_HandType_27_LOW) | LowHandVal_CARDS(low_val);
    }

    return HandVal_NOTHING;
}

/*
 * 5-card Badacey evaluation
 */
HandVal StdDeck_BadaceyRules_EVAL_5(StdDeck_CardMask cards)
{
    return StdDeck_BadaceyRules_EVAL_N(cards, 5);
}

/*
 * 5-card Badeucy evaluation
 */
HandVal StdDeck_BadeucyRules_EVAL_5(StdDeck_CardMask cards)
{
    return StdDeck_BadeucyRules_EVAL_N(cards, 5);
}

/*
 * Convert Badugi hand value to string
 */
int BadugiHandVal_toString_n(BadugiHandVal hv, char *outString, size_t size)
{
    int handType = BadugiHandVal_HANDTYPE(hv);
    int length = 0;

    if (hv == BadugiHandVal_NOTHING)
    {
        /* Report what was really written: the caller's buffer may be
         * shorter than "Nothing". */
        return advance_cursor(0, size,
                              snprintf(outString, size, "%s", "Nothing"));
    }



    switch (handType)
    {
    case BadugiRules_HandType_BADUGI:
        length = advance_cursor(length, size,
        snprintf(outString + length, room_left(size, length), "Badugi: %d%d%d%d",
                          BadugiHandVal_FIRST_CARD(hv),
                          BadugiHandVal_SECOND_CARD(hv),
                          BadugiHandVal_THIRD_CARD(hv),
                          BadugiHandVal_FOURTH_CARD(hv)));
        break;

    case BadugiRules_HandType_THREE:
        length = advance_cursor(length, size,
        snprintf(outString + length, room_left(size, length), "Three-card: %d%d%d",
                          BadugiHandVal_FIRST_CARD(hv),
                          BadugiHandVal_SECOND_CARD(hv),
                          BadugiHandVal_THIRD_CARD(hv)));
        break;

    case BadugiRules_HandType_TWO:
        length = advance_cursor(length, size,
        snprintf(outString + length, room_left(size, length), "Two-card: %d%d",
                          BadugiHandVal_FIRST_CARD(hv),
                          BadugiHandVal_SECOND_CARD(hv)));
        break;

    case BadugiRules_HandType_ONE:
        length = advance_cursor(length, size,
        snprintf(outString + length, room_left(size, length), "One-card: %d",
                          BadugiHandVal_FIRST_CARD(hv)));
        break;

    default:
        /* Report what was really written: the caller's buffer may be
         * shorter than "Invalid". */
        return advance_cursor(0, size,
                              snprintf(outString, size, "%s", "Invalid"));
    }

    return length;
}

/*
 * Print Badugi hand value
 */
int BadugiHandVal_print(BadugiHandVal hv)
{
    char buf[80];
    int length = BadugiHandVal_toString(hv, buf);
    printf("%s", buf);
    return length;
}

/*
 * Historical entry point: no length, so the caller must supply at least
 * POKER_EVAL_HANDVAL_STRING_MAX bytes. Kept so the exported API does not break.
 */
int BadugiHandVal_toString(BadugiHandVal hv, char *outString)
{
    return BadugiHandVal_toString_n(hv, outString, POKER_EVAL_HANDVAL_STRING_MAX);
}
