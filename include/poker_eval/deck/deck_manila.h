#ifndef __DECK_MANILA_H__
#define __DECK_MANILA_H__

#include <poker_eval/core/pokereval_export.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_std.h>

/* Manila Poker deck: 32 cards (8, 9, T, J, Q, K, A in each suit) */

#define ManilaDeck_N_CARDS 32
#define ManilaDeck_N_RANKS 8
#define ManilaDeck_N_SUITS 4
#define ManilaDeck_MASK(index)  (ManilaDeck_cardMasksTable[index])

/* Define ranks for Manila deck (no cards 2-7) */
#define ManilaDeck_Rank_8      StdDeck_Rank_8
#define ManilaDeck_Rank_9      StdDeck_Rank_9
#define ManilaDeck_Rank_TEN    StdDeck_Rank_TEN
#define ManilaDeck_Rank_JACK   StdDeck_Rank_JACK
#define ManilaDeck_Rank_QUEEN  StdDeck_Rank_QUEEN
#define ManilaDeck_Rank_KING   StdDeck_Rank_KING
#define ManilaDeck_Rank_ACE    StdDeck_Rank_ACE
#define ManilaDeck_Rank_FIRST  ManilaDeck_Rank_8
#define ManilaDeck_Rank_LAST   ManilaDeck_Rank_ACE
#define ManilaDeck_Rank_COUNT  8
#define ManilaDeck_N_RANKMASKS (1 << ManilaDeck_Rank_COUNT)

/* Define functions to access ranks and suits based on card index */
#define ManilaDeck_RANK(index)  (ManilaDeck_Rank_FIRST + ((index) % ManilaDeck_Rank_COUNT))
#define ManilaDeck_SUIT(index)  ((index) / ManilaDeck_Rank_COUNT)
#define ManilaDeck_MAKE_CARD(rank, suit) ((suit * ManilaDeck_Rank_COUNT) + (rank - ManilaDeck_Rank_FIRST))

/* Define suits for Manila deck */
#define ManilaDeck_Suit_HEARTS   StdDeck_Suit_HEARTS
#define ManilaDeck_Suit_DIAMONDS StdDeck_Suit_DIAMONDS
#define ManilaDeck_Suit_CLUBS    StdDeck_Suit_CLUBS
#define ManilaDeck_Suit_SPADES   StdDeck_Suit_SPADES
#define ManilaDeck_Suit_FIRST    ManilaDeck_Suit_HEARTS
#define ManilaDeck_Suit_LAST     ManilaDeck_Suit_SPADES
#define ManilaDeck_Suit_COUNT    4

/* Define type for rank masks */
typedef uint32 ManilaDeck_RankMask;

/* Define type for card mask */
#define ManilaDeck_CardMask          StdDeck_CardMask
#define ManilaDeck_CardMask_SPADES   StdDeck_CardMask_SPADES
#define ManilaDeck_CardMask_CLUBS    StdDeck_CardMask_CLUBS
#define ManilaDeck_CardMask_DIAMONDS StdDeck_CardMask_DIAMONDS
#define ManilaDeck_CardMask_HEARTS   StdDeck_CardMask_HEARTS

/* Card mask operations */
#define ManilaDeck_CardMask_RESET(m)               StdDeck_CardMask_RESET(m)
#define ManilaDeck_CardMask_SET(m, c)              StdDeck_CardMask_SET(m, c)
#define ManilaDeck_CardMask_UNSET(m, c)            StdDeck_CardMask_UNSET(m, c)
#define ManilaDeck_CardMask_CARD_IS_SET(m, c)      StdDeck_CardMask_CARD_IS_SET(m, c)
#define ManilaDeck_CardMask_OR(result, op1, op2)   StdDeck_CardMask_OR(result, op1, op2)
#define ManilaDeck_CardMask_AND(result, op1, op2)  StdDeck_CardMask_AND(result, op1, op2)
#define ManilaDeck_CardMask_XOR(result, op1, op2)  StdDeck_CardMask_XOR(result, op1, op2)

/* External declarations */
extern POKEREVAL_EXPORT ManilaDeck_CardMask ManilaDeck_cardMasksTable[ManilaDeck_N_CARDS];
/* Declared without a size: the definition includes the terminating NUL, so a
 * [<rank>_LAST + 1] bound made the array one byte too small for its own
 * initializer. Apple clang and GCC 15 reject that; no caller uses sizeof. */
extern POKEREVAL_EXPORT char ManilaDeck_rankChars[];
extern POKEREVAL_EXPORT char ManilaDeck_suitChars[];

/* Function declarations */
extern POKEREVAL_EXPORT int ManilaDeck_cardToString(int cardIndex, char *outString);
extern POKEREVAL_EXPORT int ManilaDeck_stringToCard(char *inString, int *outCard);
extern POKEREVAL_EXPORT int ManilaDeck_maskToCards(void *mask, int cards[]);
extern POKEREVAL_EXPORT int ManilaDeck_NumCards(void *mask);

#endif /* __DECK_MANILA_H__ */
