#ifndef __DECK_SHORT_H__
#define __DECK_SHORT_H__

#include "pokereval_export.h"
#include "poker_defs.h"




#define ShortDeck_N_CARDS 36
#define ShortDeck_MASK(index)  (ShortDeck_cardMasksTable[index])

// Define ranks for short deck
#define ShortDeck_Rank_6      StdDeck_Rank_6
#define ShortDeck_Rank_7      StdDeck_Rank_7
#define ShortDeck_Rank_8      StdDeck_Rank_8
#define ShortDeck_Rank_9      StdDeck_Rank_9
#define ShortDeck_Rank_TEN    StdDeck_Rank_TEN
#define ShortDeck_Rank_JACK   StdDeck_Rank_JACK
#define ShortDeck_Rank_QUEEN  StdDeck_Rank_QUEEN
#define ShortDeck_Rank_KING   StdDeck_Rank_KING
#define ShortDeck_Rank_ACE    StdDeck_Rank_ACE
#define ShortDeck_Rank_FIRST  ShortDeck_Rank_6
#define ShortDeck_Rank_LAST   ShortDeck_Rank_ACE
#define ShortDeck_Rank_COUNT  9
#define ShortDeck_N_RANKMASKS (1 << ShortDeck_Rank_COUNT)

// Define functions to access ranks and suits based on card index
#define ShortDeck_RANK(index)  (ShortDeck_Rank_FIRST \
                                + ((index) % ShortDeck_Rank_COUNT))
#define ShortDeck_SUIT(index)  ((index) / ShortDeck_Rank_COUNT)
#define ShortDeck_MAKE_CARD(rank, suit) ((suit * ShortDeck_Rank_COUNT) \
                                         + ( rank - ShortDeck_Rank_FIRST ))


// Define suits for short deck
#define ShortDeck_Suit_HEARTS   StdDeck_Suit_HEARTS
#define ShortDeck_Suit_DIAMONDS StdDeck_Suit_DIAMONDS
#define ShortDeck_Suit_CLUBS    StdDeck_Suit_CLUBS
#define ShortDeck_Suit_SPADES   StdDeck_Suit_SPADES
#define ShortDeck_Suit_FIRST    ShortDeck_Suit_HEARTS
#define ShortDeck_Suit_LAST     ShortDeck_Suit_SPADES
#define ShortDeck_Suit_COUNT    4

// Define type for rank masks
typedef StdDeck_RankMask ShortDeck_RankMask;

// Define type for card mask
#define ShortDeck_CardMask          StdDeck_CardMask
#define ShortDeck_CardMask_SPADES   StdDeck_CardMask_SPADES
#define ShortDeck_CardMask_CLUBS    StdDeck_CardMask_CLUBS
#define ShortDeck_CardMask_DIAMONDS StdDeck_CardMask_DIAMONDS
#define ShortDeck_CardMask_HEARTS   StdDeck_CardMask_HEARTS


// Define operations on card masks
#define ShortDeck_CardMask_NOT         StdDeck_CardMask_NOT
#define ShortDeck_CardMask_OR          StdDeck_CardMask_OR
#define ShortDeck_CardMask_AND         StdDeck_CardMask_AND
#define ShortDeck_CardMask_XOR         StdDeck_CardMask_XOR
#define ShortDeck_CardMask_ANY_SET     StdDeck_CardMask_ANY_SET
#define ShortDeck_CardMask_RESET       StdDeck_CardMask_RESET
#define ShortDeck_CardMask_UNSET       StdDeck_CardMask_UNSET
#define ShortDeck_CardMask_IS_EMPTY    StdDeck_CardMask_IS_EMPTY

#ifdef USE_INT64                                                          
#define ShortDeck_CardMask_CARD_IS_SET(mask, index)                       \
  (( (mask).cards_n & (ShortDeck_MASK(index).cards_n)) != 0 )                 
#else                                                                   
#define ShortDeck_CardMask_CARD_IS_SET(mask, index)                       \
  ((( (mask).cards_nn.n1 & (ShortDeck_MASK(index).cards_nn.n1)) != 0 )    \
   || (( (mask).cards_nn.n2 & (ShortDeck_MASK(index).cards_nn.n2)) != 0 ))   
#endif

#define ShortDeck_CardMask_SET(mask, index)     \
do {                                            \
  ShortDeck_CardMask _t1 = ShortDeck_MASK(index);         \
  ShortDeck_CardMask_OR((mask), (mask), _t1);             \
} while (0)

// Define the table for card masks in short deck
extern POKEREVAL_EXPORT ShortDeck_CardMask ShortDeck_cardMasksTable[ShortDeck_N_CARDS];

// Define characters for ranks and suits
extern POKEREVAL_EXPORT char ShortDeck_rankChars[ShortDeck_Rank_LAST+1];
extern POKEREVAL_EXPORT char ShortDeck_suitChars[ShortDeck_Suit_LAST+1];

// Functions to convert card index to string and vice versa
extern POKEREVAL_EXPORT int ShortDeck_cardToString(int cardIndex, char *outString);
extern POKEREVAL_EXPORT int ShortDeck_stringToCard(char *inString, int *outCard);

extern int ShortDeck_maskToCards(void *mask, int cards[]);
extern int ShortDeck_NumCards(void *mask);


// Macros to access deck-related functions
#define ShortDeck_cardString(i) GenericDeck_cardString(&ShortDeck, (i))
#define ShortDeck_printCard(i)  GenericDeck_printCard(&ShortDeck, (i))
#define ShortDeck_printMask(m)  GenericDeck_printMask(&ShortDeck, ((void *) &(m)))
#define ShortDeck_maskString(m) GenericDeck_maskString(&ShortDeck, ((void *) &(m)))
#define ShortDeck_numCards(m) GenericDeck_numCards(&ShortDeck, ((void *) &(m)))
#define ShortDeck_maskToString(m, s) GenericDeck_maskToString(&ShortDeck, ((void *) &(m)), (s))

// Define the ShortDeck instance
extern POKEREVAL_EXPORT Deck ShortDeck;

#endif

#ifdef DECK_SHORT

#if defined(Deck_N_CARDS)
#include "deck_undef.h"
#endif

// Define the number of cards in the short deck
#define Deck_N_CARDS      ShortDeck_N_CARDS
#define Deck_MASK         ShortDeck_MASK
#define Deck_RANK         ShortDeck_RANK
#define Deck_SUIT         ShortDeck_SUIT

// Define ranks and rank count for short deck
#define Rank_6            ShortDeck_Rank_6
#define Rank_7            ShortDeck_Rank_7 
#define Rank_8            ShortDeck_Rank_8 
#define Rank_9            ShortDeck_Rank_9 
#define Rank_TEN          ShortDeck_Rank_TEN
#define Rank_JACK         ShortDeck_Rank_JACK
#define Rank_QUEEN        ShortDeck_Rank_QUEEN
#define Rank_KING         ShortDeck_Rank_KING
#define Rank_ACE          ShortDeck_Rank_ACE
#define Rank_FIRST        ShortDeck_Rank_FIRST 
#define Rank_COUNT        ShortDeck_Rank_COUNT

// Define suits and suit count for short deck
#define Suit_HEARTS       ShortDeck_Suit_HEARTS
#define Suit_DIAMONDS     ShortDeck_Suit_DIAMONDS
#define Suit_CLUBS        ShortDeck_Suit_CLUBS
#define Suit_SPADES       ShortDeck_Suit_SPADES
#define Suit_FIRST        ShortDeck_Suit_FIRST
#define Suit_COUNT        ShortDeck_Suit_COUNT

#define CardMask               ShortDeck_CardMask 
#define CardMask_NOT           ShortDeck_CardMask_NOT
#define CardMask_OR            ShortDeck_CardMask_OR
#define CardMask_XOR           ShortDeck_CardMask_XOR
#define CardMask_AND           ShortDeck_CardMask_AND
#define CardMask_SET           ShortDeck_CardMask_SET
#define CardMask_CARD_IS_SET   ShortDeck_CardMask_CARD_IS_SET
#define CardMask_ANY_SET       ShortDeck_CardMask_ANY_SET
#define CardMask_RESET         ShortDeck_CardMask_RESET
#define CardMask_UNSET         ShortDeck_CardMask_UNSET

#define CardMask_SPADES        ShortDeck_CardMask_SPADES
#define CardMask_HEARTS        ShortDeck_CardMask_HEARTS
#define CardMask_CLUBS         ShortDeck_CardMask_CLUBS
#define CardMask_DIAMONDS      ShortDeck_CardMask_DIAMONDS

// Define the current deck as ShortDeck
#define CurDeck ShortDeck

#endif /* DECK_SHORT */

