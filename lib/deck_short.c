#include <stdio.h>
#include <ctype.h>
#include "poker_defs.h"
#include "deck_short.h"

char ShortDeck_rankChars[] = "%%%%6789TJQKA"; // Rank starts from 6 (6, 7, 8, 9, T, J, Q, K, A)
char ShortDeck_suitChars[] = "hdcs"; // h: hearts, d: diamonds, c: clubs, s: spades

// Convert a card index to its string representation (e.g., 7h for 7 of hearts)
int ShortDeck_cardToString(int cardIndex, char *outString) {
    int rankIndex = ShortDeck_RANK(cardIndex);
    int suitIndex = ShortDeck_SUIT(cardIndex);

    //printf("Rank Index: %d, Suit Index: %d\n", rankIndex, suitIndex); // Pour le débogage

    *outString++ = ShortDeck_rankChars[rankIndex];
    *outString++ = ShortDeck_suitChars[suitIndex];
    *outString = '\0';

    //printf("Output String: %s\n", outString - 2); // Affiche la chaîne de sortie construite

    return 2;
}


// Convert a string representation of a card to its index (e.g., "7h" to 7)
int ShortDeck_stringToCard(char *inString, int *cardIndex) {
  char *p;
  int rank, suit;

  p = inString;

  // Find the rank by checking the first character of the input string
  for (rank = ShortDeck_Rank_FIRST; rank <= ShortDeck_Rank_LAST; rank++) {
    if (ShortDeck_rankChars[rank] == toupper(*p)) // Compare with the uppercased rank character
      break;
  }
  if (rank > ShortDeck_Rank_LAST)
    goto noMatch; // No valid rank found, return failure

  ++p; // Move to the next character in the input string

  // Find the suit by checking the second character of the input string
  for (suit = ShortDeck_Suit_FIRST; suit <= ShortDeck_Suit_LAST; suit++) {
    if (ShortDeck_suitChars[suit] == tolower(*p)) // Compare with the lowercased suit character
      break;
  }
  if (suit > ShortDeck_Suit_LAST)
    goto noMatch; // No valid suit found, return failure

  *cardIndex = ShortDeck_MAKE_CARD(rank, suit); // Calculate the card index based on rank and suit
  return 2; // Return the number of characters read (rank + suit = 2)

noMatch:
  /* Didn't match anything, return failure */
  return 0;
}

// Convert a card mask to an array of card indices
int ShortDeck_maskToCards(void *cardMask, int cards[]) {
  int i, n = 0;
  ShortDeck_CardMask c = *((ShortDeck_CardMask *) cardMask);

  for (i = ShortDeck_N_CARDS - 1; i >= 0; i--) {
    if (ShortDeck_CardMask_CARD_IS_SET(c, i)) {
      cards[n++] = i; // Add the card index to the array
    }
  }

  return n; // Return the number of cards in the array
}

// Get the number of cards in a card mask
int ShortDeck_NumCards(void *cardMask) {
  ShortDeck_CardMask c = *((ShortDeck_CardMask *) cardMask);
  int i;
  int ncards = 0;
  for (i = 0; i < ShortDeck_N_CARDS; i++) {
    if (ShortDeck_CardMask_CARD_IS_SET(c, i)) {
      ncards++; // Increment the count for each set card
    }
  }
  return ncards; // Return the total number of set cards
}

// Create a Deck object representing the Short Deck NL Holdem with associated functions
Deck ShortDeck = {
  ShortDeck_N_CARDS,
  "ShortDeckNLHoldem",
  ShortDeck_cardToString,
  ShortDeck_stringToCard,
  ShortDeck_maskToCards,
  ShortDeck_NumCards
};
