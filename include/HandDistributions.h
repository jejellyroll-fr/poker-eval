#ifndef HAND_DISTRIBUTIONS_H
#define HAND_DISTRIBUTIONS_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

// Only define TRACE and ASSERT if NDEBUG is not defined
#ifndef NDEBUG
#define TRACE(...) do { printf(__VA_ARGS__); } while (0)
#define ASSERT assert
#else
#define TRACE(...)
#define ASSERT(x) ((void)0)
#endif

// Ensure this type is not redefined
#ifndef STDDECK_CARDMASK
#define STDDECK_CARDMASK
typedef struct {
    uint64_t cards_n;
} StdDeck_CardMask;
#endif

// Define macros for card operations if they are not already defined
#ifndef STDDECK_CARDMASK_RESET
#define STDDECK_CARDMASK_RESET(mask) (mask.cards_n = 0)
#endif

#ifndef STDDECK_CARDMASK_OR
#define STDDECK_CARDMASK_OR(dst, src1, src2) (dst.cards_n = src1.cards_n | src2.cards_n)
#endif

#ifndef STDDECK_CARDMASK_ANY_SET
#define STDDECK_CARDMASK_ANY_SET(mask, test) (mask.cards_n & test.cards_n)
#endif

#ifndef STDDECK_CARDMASK_EQUAL
#define STDDECK_CARDMASK_EQUAL(a, b) (a.cards_n == b.cards_n)
#endif

// Function declarations
void StdDeck_stringToCard(const char* str, int* index);
StdDeck_CardMask StdDeck_MASK(int index);

// Ensure these macros are not redefined
#ifndef STDDECK_MAKE_CARD
#define StdDeck_MAKE_CARD(rank, suit) ((rank) * 4 + (suit))
#endif

#ifndef STDDECK_RANKS
#define StdDeck_Rank_2 0
#define StdDeck_Rank_3 1
#define StdDeck_Rank_4 2
#define StdDeck_Rank_5 3
#define StdDeck_Rank_6 4
#define StdDeck_Rank_7 5
#define StdDeck_Rank_8 6
#define StdDeck_Rank_9 7
#define StdDeck_Rank_TEN 8
#define StdDeck_Rank_JACK 9
#define StdDeck_Rank_QUEEN 10
#define StdDeck_Rank_KING 11
#define StdDeck_Rank_ACE 12
#endif

#ifndef STDDECK_SUITS
#define StdDeck_Suit_CLUBS 0
#define StdDeck_Suit_DIAMONDS 1
#define StdDeck_Suit_HEARTS 2
#define StdDeck_Suit_SPADES 3
#endif

#endif // HAND_DISTRIBUTIONS_H
