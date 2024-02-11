#include <stdio.h>

#include "poker_defs.h"
#include "deck_short.h"
#include "rules_short.h"

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

// Function to convert HandVal to a string representation
int ShortRules_HandVal_toString(HandVal handval, char *outString) {
  char *p = outString;
  int htype = HandVal_HANDTYPE(handval);

  // Format the hand type at the beginning of the output string
  p += sprintf(outString, "%s (", ShortRules_handTypeNames[htype]);

  // Append the significant cards to the output string based on the hand type
  if (ShortRules_nSigCards[htype] >= 1)
    p += sprintf(p, "%c", ShortDeck_rankChars[HandVal_TOP_CARD(handval)]);
  if (ShortRules_nSigCards[htype] >= 2)
    p += sprintf(p, " %c", ShortDeck_rankChars[HandVal_SECOND_CARD(handval)]);
  if (ShortRules_nSigCards[htype] >= 3)
    p += sprintf(p, " %c", ShortDeck_rankChars[HandVal_THIRD_CARD(handval)]);
  if (ShortRules_nSigCards[htype] >= 4)
    p += sprintf(p, " %c", ShortDeck_rankChars[HandVal_FOURTH_CARD(handval)]);
  if (ShortRules_nSigCards[htype] >= 5)
    p += sprintf(p, " %c", ShortDeck_rankChars[HandVal_FIFTH_CARD(handval)]);

  // Append closing parenthesis to the output string
  p += sprintf(p, ")");

  // Calculate and return the length of the output string
  return p - outString;
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
