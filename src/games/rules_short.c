#include <stdio.h>

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/deck/deck_short.h>
#include <poker_eval/games/rules_short.h>

// Define names for each Short Deck (Short) hand type
const char *ShortRules_handTypeNames[ShortRules_HandType_LAST + 1] = {
  "HighCard",
  "OnePair",
  "TwoPair",
  "Straight",
  "Trips",
  "FullHouse",  
  "Flush",
  "Quads",
  "StraightFlush"
};

// Define padded names for each Short Deck (Short) hand type
const char *ShortRules_handTypeNamesPadded[ShortRules_HandType_LAST + 1] = {
  "HighCard     ",
  "OnePair      ",
  "TwoPair      ",
  "Straight     ",
  "Trips        ",
  "FullHouse    ",
  "Flush        ",
  "Quads        ",
  "StraightFlush"
};

// Define the number of significant cards for each Short Deck (Short) hand type (not sure)
int ShortRules_nSigCards[ShortRules_HandType_LAST + 1] = {
  5, // HighCard has 5 significant card
  4, // OnePair has 4 significant cards
  3, // TwoPair has 4 significant cards
  1, // Straight has 5 significant cards
  3, // Trips has 3 significant cards
  2, // FullHouse has 2 significant cards
  5, // Flush has 5 significant cards
  2, // Quads has 4 significant cards
  1, // StraightFlush has 5 significant cards
};

static inline void advance_cursor(char **cursor, size_t *remaining, int written) {
  if (*remaining == 0)
    return;

  if (written < 0) {
    *remaining = 0;
    return;
  }

  if ((size_t)written >= *remaining) {
    *cursor += *remaining;
    *remaining = 0;
    return;
  }

  *cursor += written;
  *remaining -= (size_t)written;
}

// Function to convert HandVal to a string representation
int ShortRules_HandVal_toString(HandVal handval, char *outString) {
  if (!outString)
    return 0;

  char *p = outString;
  const size_t BUFFER_SIZE = 80;
  size_t remaining = BUFFER_SIZE;
  int htype = HandVal_HANDTYPE(handval);

  // Format the hand type at the beginning of the output string
  int written = snprintf(p, remaining, "%s (", ShortRules_handTypeNames[htype]);
  advance_cursor(&p, &remaining, written);

  // Append the significant cards to the output string based on the hand type
  if (remaining > 0 && ShortRules_nSigCards[htype] >= 1)
    advance_cursor(&p, &remaining,
                   snprintf(p, remaining, "%c", ShortDeck_rankChars[HandVal_TOP_CARD(handval)]));
  if (remaining > 0 && ShortRules_nSigCards[htype] >= 2)
    advance_cursor(&p, &remaining,
                   snprintf(p, remaining, " %c", ShortDeck_rankChars[HandVal_SECOND_CARD(handval)]));
  if (remaining > 0 && ShortRules_nSigCards[htype] >= 3)
    advance_cursor(&p, &remaining,
                   snprintf(p, remaining, " %c", ShortDeck_rankChars[HandVal_THIRD_CARD(handval)]));
  if (remaining > 0 && ShortRules_nSigCards[htype] >= 4)
    advance_cursor(&p, &remaining,
                   snprintf(p, remaining, " %c", ShortDeck_rankChars[HandVal_FOURTH_CARD(handval)]));
  if (remaining > 0 && ShortRules_nSigCards[htype] >= 5)
    advance_cursor(&p, &remaining,
                   snprintf(p, remaining, " %c", ShortDeck_rankChars[HandVal_FIFTH_CARD(handval)]));

  // Append closing parenthesis to the output string
  if (remaining > 0)
    advance_cursor(&p, &remaining, snprintf(p, remaining, ")"));

  // Calculate and return the length of the output string
  return (int)(p - outString);
}

// Function to print the HandVal to stdout
int ShortRules_HandVal_print(HandVal handval) {
  char buf[80];
  int n;

  // Convert HandVal to a string and print it to stdout
  n = ShortRules_HandVal_toString(handval, buf);
  printf("%s", buf);

  // Return the number of characters printed
  return n;
}
