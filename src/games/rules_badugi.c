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
#include <poker_eval/games/rules_badugi.h>
#include <poker_eval/games/badugi_eval.h>
#include <poker_eval/deck/deck_std.h>

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

/* Badugi hand type names */
const char *BadugiRules_handTypeNames[BadugiRules_HandType_COUNT] = {
    "Badugi",
    "Three-card",
    "Two-card",
    "One-card"};

const char *BadugiRules_handTypeNamesPadded[BadugiRules_HandType_COUNT] = {
    "Badugi    ",
    "Three-card",
    "Two-card  ",
    "One-card  "};

/* Badacey hand type names */
const char *BadaceyRules_handTypeNames[BadaceyRules_HandType_COUNT] = {
    "Badugi",
    "Three-card",
    "Two-card",
    "One-card",
    "A-5 Low"};

const char *BadaceyRules_handTypeNamesPadded[BadaceyRules_HandType_COUNT] = {
    "Badugi    ",
    "Three-card",
    "Two-card  ",
    "One-card  ",
    "A-5 Low   "};

/* Badeucy hand type names */
const char *BadeucyRules_handTypeNames[BadeucyRules_HandType_COUNT] = {
    "Badugi",
    "Three-card",
    "Two-card",
    "One-card",
    "2-7 Low"};

const char *BadeucyRules_handTypeNamesPadded[BadeucyRules_HandType_COUNT] = {
    "Badugi    ",
    "Three-card",
    "Two-card  ",
    "One-card  ",
    "2-7 Low   "};

/* Number of significant cards for each hand type */
int BadugiRules_nSigCards[BadugiRules_HandType_COUNT] = {
    4, /* Badugi */
    3, /* Three-card */
    2, /* Two-card */
    1  /* One-card */
};

int BadaceyRules_nSigCards[BadaceyRules_HandType_COUNT] = {
    4, /* Badugi */
    3, /* Three-card */
    2, /* Two-card */
    1, /* One-card */
    5  /* A-5 Low */
};

int BadeucyRules_nSigCards[BadeucyRules_HandType_COUNT] = {
    4, /* Badugi */
    3, /* Three-card */
    2, /* Two-card */
    1, /* One-card */
    5  /* 2-7 Low */
};

/* Convert rank to string (Ace low for Badugi) */
static const char *rank_to_string_ace_low(int rank)
{
    switch (rank)
    {
    case StdDeck_Rank_ACE:
        return "A";
    case StdDeck_Rank_2:
        return "2";
    case StdDeck_Rank_3:
        return "3";
    case StdDeck_Rank_4:
        return "4";
    case StdDeck_Rank_5:
        return "5";
    case StdDeck_Rank_6:
        return "6";
    case StdDeck_Rank_7:
        return "7";
    case StdDeck_Rank_8:
        return "8";
    case StdDeck_Rank_9:
        return "9";
    case StdDeck_Rank_TEN:
        return "T";
    case StdDeck_Rank_JACK:
        return "J";
    case StdDeck_Rank_QUEEN:
        return "Q";
    case StdDeck_Rank_KING:
        return "K";
    default:
        return "?";
    }
}

/* Badugi hand value to string */
int BadugiRules_HandVal_toString_n(HandVal handval, char *outString, size_t size)
{
    int handType = HandVal_HANDTYPE(handval);
    int length = 0;

    if (handType >= BadugiRules_HandType_COUNT)
    {
        /* Report what was really written: the caller's buffer may be
         * shorter than "Invalid". */
        return advance_cursor(0, size,
                              snprintf(outString, size, "%s", "Invalid"));
    }



    length = advance_cursor(length, size,
        snprintf(outString + length, room_left(size, length), "%s", BadugiRules_handTypeNames[handType]));

    if (handType == BadugiRules_HandType_BADUGI)
    {
        length = advance_cursor(length, size,
        snprintf(outString + length, room_left(size, length), " (%s%s%s%s)",
                          rank_to_string_ace_low(HandVal_TOP_CARD(handval)),
                          rank_to_string_ace_low(HandVal_SECOND_CARD(handval)),
                          rank_to_string_ace_low(HandVal_THIRD_CARD(handval)),
                          rank_to_string_ace_low(HandVal_FOURTH_CARD(handval))));
    }
    else if (handType == BadugiRules_HandType_THREE)
    {
        length = advance_cursor(length, size,
        snprintf(outString + length, room_left(size, length), " (%s%s%s)",
                          rank_to_string_ace_low(HandVal_TOP_CARD(handval)),
                          rank_to_string_ace_low(HandVal_SECOND_CARD(handval)),
                          rank_to_string_ace_low(HandVal_THIRD_CARD(handval))));
    }
    else if (handType == BadugiRules_HandType_TWO)
    {
        length = advance_cursor(length, size,
        snprintf(outString + length, room_left(size, length), " (%s%s)",
                          rank_to_string_ace_low(HandVal_TOP_CARD(handval)),
                          rank_to_string_ace_low(HandVal_SECOND_CARD(handval))));
    }
    else if (handType == BadugiRules_HandType_ONE)
    {
        length = advance_cursor(length, size,
        snprintf(outString + length, room_left(size, length), " (%s)",
                          rank_to_string_ace_low(HandVal_TOP_CARD(handval))));
    }

    return length;
}

/* Print Badugi hand value */
int BadugiRules_HandVal_print(HandVal handval)
{
    char buf[80];
    int length = BadugiRules_HandVal_toString(handval, buf);
    printf("%s", buf);
    return length;
}

/* Badacey hand value to string */
int BadaceyRules_HandVal_toString_n(HandVal handval, char *outString, size_t size)
{
    int handType = HandVal_HANDTYPE(handval);
    int length = 0;

    if (handType >= BadaceyRules_HandType_COUNT)
    {
        /* Report what was really written: the caller's buffer may be
         * shorter than "Invalid". */
        return advance_cursor(0, size,
                              snprintf(outString, size, "%s", "Invalid"));
    }

    if (handType <= BadaceyRules_HandType_ONE)
    {
        /* Badugi portion */
        /* Delegate to the bounded variant: the wrapper would assume
         * POKER_EVAL_HANDVAL_STRING_MAX and ignore the caller's size. */
        return BadugiRules_HandVal_toString_n(handval, outString, size);
    }
    else
    {
        /* A-5 lowball portion */
        length = advance_cursor(length, size,
        snprintf(outString + length, room_left(size, length), "%s", BadaceyRules_handTypeNames[handType]));
        /* Add lowball hand details here if needed */
    }

    return length;
}

/* Print Badacey hand value */
int BadaceyRules_HandVal_print(HandVal handval)
{
    char buf[80];
    int length = BadaceyRules_HandVal_toString(handval, buf);
    printf("%s", buf);
    return length;
}

/* Badeucy hand value to string */
int BadeucyRules_HandVal_toString_n(HandVal handval, char *outString, size_t size)
{
    int handType = HandVal_HANDTYPE(handval);
    int length = 0;

    if (handType >= BadeucyRules_HandType_COUNT)
    {
        /* Report what was really written: the caller's buffer may be
         * shorter than "Invalid". */
        return advance_cursor(0, size,
                              snprintf(outString, size, "%s", "Invalid"));
    }

    if (handType <= BadeucyRules_HandType_ONE)
    {
        /* Badugi portion */
        /* Delegate to the bounded variant: the wrapper would assume
         * POKER_EVAL_HANDVAL_STRING_MAX and ignore the caller's size. */
        return BadugiRules_HandVal_toString_n(handval, outString, size);
    }
    else
    {
        /* 2-7 lowball portion */
        length = advance_cursor(length, size,
        snprintf(outString + length, room_left(size, length), "%s", BadeucyRules_handTypeNames[handType]));
        /* Add lowball hand details here if needed */
    }

    return length;
}

/* Print Badeucy hand value */
int BadeucyRules_HandVal_print(HandVal handval)
{
    char buf[80];
    int length = BadeucyRules_HandVal_toString(handval, buf);
    printf("%s", buf);
    return length;
}

/*
 * Historical entry point: no length, so the caller must supply at least
 * POKER_EVAL_HANDVAL_STRING_MAX bytes. Kept so the exported API does not break.
 */
int BadugiRules_HandVal_toString(HandVal handval, char *outString)
{
    return BadugiRules_HandVal_toString_n(handval, outString, POKER_EVAL_HANDVAL_STRING_MAX);
}

/*
 * Historical entry point: no length, so the caller must supply at least
 * POKER_EVAL_HANDVAL_STRING_MAX bytes. Kept so the exported API does not break.
 */
int BadaceyRules_HandVal_toString(HandVal handval, char *outString)
{
    return BadaceyRules_HandVal_toString_n(handval, outString, POKER_EVAL_HANDVAL_STRING_MAX);
}

/*
 * Historical entry point: no length, so the caller must supply at least
 * POKER_EVAL_HANDVAL_STRING_MAX bytes. Kept so the exported API does not break.
 */
int BadeucyRules_HandVal_toString(HandVal handval, char *outString)
{
    return BadeucyRules_HandVal_toString_n(handval, outString, POKER_EVAL_HANDVAL_STRING_MAX);
}
