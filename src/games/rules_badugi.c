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
#include <stdarg.h>
#include <string.h>
#include <poker_eval/games/rules_badugi.h>
#include <poker_eval/games/badugi_eval.h>
#include <poker_eval/deck/deck_std.h>

/*
 * Append to outString, never past size, and report how many bytes were really
 * written. snprintf returns what it *would* have written, so accumulating its
 * return value directly walks the cursor past the buffer on truncation.
 */
static int append_bounded(char *outString, size_t size, int length,
                          const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 4, 5)))
#endif
    ;

static int append_bounded(char *outString, size_t size, int length,
                          const char *fmt, ...) {
    va_list args;
    int written;

    if (length < 0 || (size_t)length >= size) return 0;

    va_start(args, fmt);
    written = vsnprintf(outString + length, size - (size_t)length, fmt, args);
    va_end(args);

    if (written < 0) return 0;
    if ((size_t)written >= size - (size_t)length) {
        return (int)(size - (size_t)length) - 1;   /* truncated */
    }
    return written;
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
        append_bounded(outString, size, 0, "%s", "Invalid");
        return 7;
    }



    length += append_bounded(outString, size, length, "%s", BadugiRules_handTypeNames[handType]);

    if (handType == BadugiRules_HandType_BADUGI)
    {
        length += append_bounded(outString, size, length, " (%s%s%s%s)",
                          rank_to_string_ace_low(HandVal_TOP_CARD(handval)),
                          rank_to_string_ace_low(HandVal_SECOND_CARD(handval)),
                          rank_to_string_ace_low(HandVal_THIRD_CARD(handval)),
                          rank_to_string_ace_low(HandVal_FOURTH_CARD(handval)));
    }
    else if (handType == BadugiRules_HandType_THREE)
    {
        length += append_bounded(outString, size, length, " (%s%s%s)",
                          rank_to_string_ace_low(HandVal_TOP_CARD(handval)),
                          rank_to_string_ace_low(HandVal_SECOND_CARD(handval)),
                          rank_to_string_ace_low(HandVal_THIRD_CARD(handval)));
    }
    else if (handType == BadugiRules_HandType_TWO)
    {
        length += append_bounded(outString, size, length, " (%s%s)",
                          rank_to_string_ace_low(HandVal_TOP_CARD(handval)),
                          rank_to_string_ace_low(HandVal_SECOND_CARD(handval)));
    }
    else if (handType == BadugiRules_HandType_ONE)
    {
        length += append_bounded(outString, size, length, " (%s)",
                          rank_to_string_ace_low(HandVal_TOP_CARD(handval)));
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
        append_bounded(outString, size, 0, "%s", "Invalid");
        return 7;
    }

    if (handType <= BadaceyRules_HandType_ONE)
    {
        /* Badugi portion */
        return BadugiRules_HandVal_toString(handval, outString);
    }
    else
    {
        /* A-5 lowball portion */
        length += append_bounded(outString, size, length, "%s", BadaceyRules_handTypeNames[handType]);
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
        append_bounded(outString, size, 0, "%s", "Invalid");
        return 7;
    }

    if (handType <= BadeucyRules_HandType_ONE)
    {
        /* Badugi portion */
        return BadugiRules_HandVal_toString(handval, outString);
    }
    else
    {
        /* 2-7 lowball portion */
        length += append_bounded(outString, size, length, "%s", BadeucyRules_handTypeNames[handType]);
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
