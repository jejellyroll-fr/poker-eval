/*
 * deck_manila.c - Manila Poker deck implementation
 *
 * Manila deck: 32 cards (8, 9, T, J, Q, K, A in each suit)
 * No cards 2-7 are used.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <poker_eval/deck/deck_manila.h>
#include <poker_eval/deck/deck_std.h>

/* Card mask table for Manila deck */
ManilaDeck_CardMask ManilaDeck_cardMasksTable[ManilaDeck_N_CARDS];

/* Character representations */
char ManilaDeck_rankChars[ManilaDeck_Rank_LAST+1];
char ManilaDeck_suitChars[ManilaDeck_Suit_LAST+1];

/* Initialize Manila deck */
static int
ManilaDeck_cardToString_init(void) {
    int rank, suit, card;

    /* Initialize rank characters */
    ManilaDeck_rankChars[ManilaDeck_Rank_8]    = '8';
    ManilaDeck_rankChars[ManilaDeck_Rank_9]    = '9';
    ManilaDeck_rankChars[ManilaDeck_Rank_TEN]  = 'T';
    ManilaDeck_rankChars[ManilaDeck_Rank_JACK] = 'J';
    ManilaDeck_rankChars[ManilaDeck_Rank_QUEEN]= 'Q';
    ManilaDeck_rankChars[ManilaDeck_Rank_KING] = 'K';
    ManilaDeck_rankChars[ManilaDeck_Rank_ACE]  = 'A';

    /* Initialize suit characters */
    ManilaDeck_suitChars[ManilaDeck_Suit_HEARTS]   = 'h';
    ManilaDeck_suitChars[ManilaDeck_Suit_DIAMONDS] = 'd';
    ManilaDeck_suitChars[ManilaDeck_Suit_CLUBS]    = 'c';
    ManilaDeck_suitChars[ManilaDeck_Suit_SPADES]   = 's';

    /* Initialize card mask table - simplified approach */
    card = 0;
    for (suit = ManilaDeck_Suit_FIRST; suit <= ManilaDeck_Suit_LAST; suit++) {
        for (rank = ManilaDeck_Rank_FIRST; rank <= ManilaDeck_Rank_LAST; rank++) {
            ManilaDeck_CardMask_RESET(ManilaDeck_cardMasksTable[card]);
            /* For now, use a simple mapping - this will be improved later */
            /* The actual bit setting will be done using StdDeck mapping */
            int std_card = StdDeck_MAKE_CARD(rank, suit);
            ManilaDeck_cardMasksTable[card] = StdDeck_MASK(std_card);
            card++;
        }
    }

    return 0;
}

/* Convert card index to string representation */
int
ManilaDeck_cardToString(int cardIndex, char *outString) {
    static int initialized = 0;

    if (!initialized) {
        ManilaDeck_cardToString_init();
        initialized = 1;
    }

    if (cardIndex < 0 || cardIndex >= ManilaDeck_N_CARDS)
        return 1;

    int rank = ManilaDeck_RANK(cardIndex);
    int suit = ManilaDeck_SUIT(cardIndex);

    outString[0] = ManilaDeck_rankChars[rank];
    outString[1] = ManilaDeck_suitChars[suit];
    outString[2] = '\0';

    return 0;
}

/* Convert string to card index */
int
ManilaDeck_stringToCard(char *inString, int *outCard) {
    int rank = -1, suit = -1;
    char rankChar, suitChar;

    if (strlen(inString) != 2)
        return 1;

    rankChar = inString[0];
    suitChar = inString[1];

    /* Find rank */
    switch (rankChar) {
        case '8': rank = ManilaDeck_Rank_8; break;
        case '9': rank = ManilaDeck_Rank_9; break;
        case 'T': case 't': rank = ManilaDeck_Rank_TEN; break;
        case 'J': case 'j': rank = ManilaDeck_Rank_JACK; break;
        case 'Q': case 'q': rank = ManilaDeck_Rank_QUEEN; break;
        case 'K': case 'k': rank = ManilaDeck_Rank_KING; break;
        case 'A': case 'a': rank = ManilaDeck_Rank_ACE; break;
        default: return 1;
    }

    /* Find suit */
    switch (suitChar) {
        case 'h': case 'H': suit = ManilaDeck_Suit_HEARTS; break;
        case 'd': case 'D': suit = ManilaDeck_Suit_DIAMONDS; break;
        case 'c': case 'C': suit = ManilaDeck_Suit_CLUBS; break;
        case 's': case 'S': suit = ManilaDeck_Suit_SPADES; break;
        default: return 1;
    }

    *outCard = ManilaDeck_MAKE_CARD(rank, suit);
    return 0;
}

/* Convert card mask to array of card indices */
int
ManilaDeck_maskToCards(void *mask, int cards[]) {
    ManilaDeck_CardMask *cardMask = (ManilaDeck_CardMask *)mask;
    int i, count = 0;

    for (i = 0; i < ManilaDeck_N_CARDS; i++) {
        if (ManilaDeck_CardMask_CARD_IS_SET(*cardMask, i)) {
            cards[count++] = i;
        }
    }
    return count;
}

/* Count number of cards in mask */
int
ManilaDeck_NumCards(void *mask) {
    ManilaDeck_CardMask *cardMask = (ManilaDeck_CardMask *)mask;
    int count = 0, i;

    for (i = 0; i < ManilaDeck_N_CARDS; i++) {
        if (ManilaDeck_CardMask_CARD_IS_SET(*cardMask, i)) {
            count++;
        }
    }
    return count;
}

/* Manila deck structure */
Deck ManilaDeck = {
    ManilaDeck_N_CARDS,
    "ManilaDeck",
    ManilaDeck_cardToString,
    ManilaDeck_stringToCard,
    ManilaDeck_maskToCards,
    ManilaDeck_NumCards
};

