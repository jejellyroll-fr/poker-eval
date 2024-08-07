///////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2009 James Devlin
//
// DISCLAIMER OF WARRANTY
//
// This source code is provided "as is" and without warranties as to performance
// or merchantability. The author and/or distributors of this source code may 
// have made statements about this source code. Any such statements do not 
// constitute warranties and shall not be relied on by the user in deciding 
// whether to use this source code.
//
// This source code is provided without any express or implied warranties 
// whatsoever. Because of the diversity of conditions and hardware under which
// this source code may be used, no warranty of fitness for a particular purpose
// is offered. The user is advised to test the source code thoroughly before 
// relying on it. The user must assume the entire risk of using the source code.
// 
//       rewrite in c by jejellyroll
//
///////////////////////////////////////////////////////////////////////////////

#ifndef CARD_H
#define CARD_H

enum Rank {
    UnknownRank = -1,
    Two = 0,
    Three,
    Four,
    Five,
    Six,
    Seven,
    Eight,
    Nine,
    Ten,
    Jack,
    Queen,
    King,
    Ace = 12
};

enum Suit {
    UnknownSuit = -1,
    Hearts,
    Diamonds,
    Clubs,
    Spades
};

typedef struct {
    const char* rankChars;
    const char* suitChars;
} Card;

void Card_init(Card* card);
void Card_destroy(Card* card);
int CharToRank(const char c);
int CharToSuit(const char s);
char RankToChar(int rank);
char SuitToChar(int suit);

#endif // CARD_H
