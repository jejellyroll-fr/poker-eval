#include "CardConverter.h"
#include "deck_std.h"
#include <string.h>

StdDeck_CardMask PokerEvalCards[53];

void InitPokerEvalCards(void) {
    PokerEvalCards[0] = (StdDeck_CardMask){0};
    PokerEvalCards[1] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_CLUBS));
    PokerEvalCards[2] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_CLUBS));
    PokerEvalCards[3] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_CLUBS));
    PokerEvalCards[4] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_CLUBS));
    PokerEvalCards[5] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_6, StdDeck_Suit_CLUBS));
    PokerEvalCards[6] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_CLUBS));
    PokerEvalCards[7] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_8, StdDeck_Suit_CLUBS));
    PokerEvalCards[8] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_CLUBS));
    PokerEvalCards[9] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_CLUBS));
    PokerEvalCards[10] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_CLUBS));
    PokerEvalCards[11] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS));
    PokerEvalCards[12] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_CLUBS));
    PokerEvalCards[13] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_CLUBS));

    PokerEvalCards[14] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_DIAMONDS));
    PokerEvalCards[15] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_DIAMONDS));
    PokerEvalCards[16] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_DIAMONDS));
    PokerEvalCards[17] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_DIAMONDS));
    PokerEvalCards[18] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_6, StdDeck_Suit_DIAMONDS));
    PokerEvalCards[19] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_DIAMONDS));
    PokerEvalCards[20] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_8, StdDeck_Suit_DIAMONDS));
    PokerEvalCards[21] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_DIAMONDS));
    PokerEvalCards[22] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_DIAMONDS));
    PokerEvalCards[23] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_DIAMONDS));
    PokerEvalCards[24] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS));
    PokerEvalCards[25] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS));
    PokerEvalCards[26] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS));

    PokerEvalCards[27] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_HEARTS));
    PokerEvalCards[28] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_HEARTS));
    PokerEvalCards[29] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_HEARTS));
    PokerEvalCards[30] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_HEARTS));
    PokerEvalCards[31] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_6, StdDeck_Suit_HEARTS));
    PokerEvalCards[32] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_HEARTS));
    PokerEvalCards[33] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_8, StdDeck_Suit_HEARTS));
    PokerEvalCards[34] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_HEARTS));
    PokerEvalCards[35] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_HEARTS));
    PokerEvalCards[36] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_HEARTS));
    PokerEvalCards[37] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS));
    PokerEvalCards[38] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    PokerEvalCards[39] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));

    PokerEvalCards[40] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_SPADES));
    PokerEvalCards[41] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_SPADES));
    PokerEvalCards[42] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_SPADES));
    PokerEvalCards[43] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_SPADES));
    PokerEvalCards[44] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_6, StdDeck_Suit_SPADES));
    PokerEvalCards[45] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_SPADES));
    PokerEvalCards[46] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_8, StdDeck_Suit_SPADES));
    PokerEvalCards[47] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_SPADES));
    PokerEvalCards[48] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_SPADES));
    PokerEvalCards[49] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_SPADES));
    PokerEvalCards[50] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES));
    PokerEvalCards[51] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));
    PokerEvalCards[52] = StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
}

StdDeck_CardMask TextToPokerEval(const char *inString) {
    StdDeck_CardMask finalMask;
    StdDeck_CardMask_RESET(finalMask);
    
    printf("Converting string: %s\n", inString);
    
    char cardStr[3] = {0};
    int i = 0;
    
    while (inString[i] != '\0' && inString[i+1] != '\0') {
        cardStr[0] = inString[i];
        cardStr[1] = inString[i+1];
        
        int cardIndex;
        int result = StdDeck_stringToCard(cardStr, &cardIndex);
        printf("StdDeck_stringToCard result: %d, cardIndex: %d\n", result, cardIndex);
        
        if (result == 2) {
            StdDeck_CardMask cardMask = PokerEvalCards[cardIndex + 1];
            StdDeck_CardMask_OR(finalMask, finalMask, cardMask);
            printf("Intermediate mask: %llu\n", finalMask.cards_n);
        } else {
            printf("Invalid card: %s\n", cardStr);
        }
        
        i += 2;
    }
    
    printf("Final card mask: %llu\n", finalMask.cards_n);
    return finalMask;
}

int TextToPokerEvalArray(const char *inString, StdDeck_CardMask *outArray) {
    int numCards = 0;
    char cardStr[3];
    const char *ptr = inString;

    while (*ptr != '\0' && *(ptr+1) != '\0' && numCards < 7) {
        strncpy(cardStr, ptr, 2);
        cardStr[2] = '\0';
        outArray[numCards] = TextToPokerEval(cardStr);
        numCards++;
        ptr += 2;
    }

    return numCards;
}

StdDeck_CardMask PokerTrackerToPokerEval(int trackerId) {
    if (trackerId >= 1 && trackerId <= 52) {
        return PokerEvalCards[trackerId];
    }
    return (StdDeck_CardMask){0};
}