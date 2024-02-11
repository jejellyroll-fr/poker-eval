#include <stdio.h>

#include "poker_defs.h"
#include "deck_short.h"
#include "mktable.h"

// Define a comment string for the card mask table
#define ACM_COMMENT_STRING \
  "ShortDeck_cardMasks[].  Maps card indices (0..35) to CardMasks.  \n"       \
  "The output mask has only one bit set, the bit corresponding to the card\n" \
  "identified by the index."

// Define the filename for the generated table
#define ACM_FILENAME "t_shortdeckcardmasks"

// Function to generate the card mask table for Short Deck
static void doCardMaskTable(void) {
  ShortDeck_CardMask c;
  int i;

  // Begin generating the table with MakeTable_begin function
  MakeTable_begin("ShortDeck_cardMasksTable", ACM_FILENAME, "ShortDeck_CardMask", ShortDeck_N_CARDS);
  MakeTable_comment(ACM_COMMENT_STRING); // Add the comment string to the table

  // Include additional header file required for ShortDeck_CardMask data type
  MakeTable_extraCode("#include \"deck_short.h\"");

  // Loop through each card index (0 to 35) to generate the card masks
  for (i = 0; i < ShortDeck_N_CARDS; i++) {
    int suit = ShortDeck_SUIT(i);
    int rank = ShortDeck_RANK(i);

    ShortDeck_CardMask_RESET(c); // Reset the card mask to all zero bits

    // Set the bit corresponding to the card index based on its suit and rank
    if (suit == ShortDeck_Suit_HEARTS)
      c.cards.hearts = (1 << rank);
    else if (suit == ShortDeck_Suit_DIAMONDS)
      c.cards.diamonds = (1 << rank);
    else if (suit == ShortDeck_Suit_CLUBS)
      c.cards.clubs = (1 << rank);
    else if (suit == ShortDeck_Suit_SPADES)
      c.cards.spades = (1 << rank);

#ifdef USE_INT64
    MakeTable_outputUInt64(c.cards_n); // Output the card mask value (UInt64)
#else
    {
      char buf[80];
      // Output the card mask value (two 32-bit integers) as a string in curly braces format
      sprintf(buf, " { { 0x%08x, 0x%08x } } ", c.cards_nn.n1, c.cards_nn.n2);
      MakeTable_outputString(buf);
    };
#endif
  };

  MakeTable_end(); // Finish generating the table
}

// Main function
int main(int argc, char **argv) {
  doCardMaskTable(); // Generate the card mask table for Short Deck

  return 0;
}
