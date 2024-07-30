#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "HoldemAgnosticHand.h"
#include "Card.h"
#include "deck_std.h"
#include "poker_defs.h"

#ifdef StdDeck_maskString
#undef StdDeck_maskString
#endif

void StdDeck_maskString(StdDeck_CardMask mask, char *out) {
    int cardIndex = 0;
    int outIndex = 0;
    for (cardIndex = 0; cardIndex < 52; cardIndex++) {
        if (StdDeck_CardMask_CARD_IS_SET(mask, cardIndex)) {
            int rank = StdDeck_RANK(cardIndex);
            int suit = StdDeck_SUIT(cardIndex);
            out[outIndex++] = StdDeck_rankChars[rank];
            out[outIndex++] = StdDeck_suitChars[suit];
            out[outIndex++] = ' ';
        }
    }
    if (outIndex > 0) {
        out[outIndex - 1] = '\0';  // Replace last space with null terminator
    } else {
        out[0] = '\0';  // Empty string if no cards
    }
}

int HoldemAgnosticHand_Parse(const char* handText, const char* deadText) {
    StdDeck_CardMask deadCards;
    StdDeck_CardMask_RESET(deadCards);
        
    if (deadText && strlen(deadText)) {
        int suit, rank;
        StdDeck_CardMask hand;
        for(const char* pCard = deadText; *pCard != '\0'; pCard += 2) {
            rank = CharToRank(*pCard);
            suit = CharToSuit(*(pCard+1));
            hand = StdDeck_MASK( StdDeck_MAKE_CARD(rank, suit) );
            StdDeck_CardMask_OR(deadCards, deadCards, hand);
        }
    }

    return HoldemAgnosticHand_Parse_StdDeck(handText, deadCards);
}

int HoldemAgnosticHand_Parse_StdDeck(const char* handText, StdDeck_CardMask deadCards) {
    if (strcmp(handText, "XxXx") == 0) {
        return 1;
    }
    
    if (HoldemAgnosticHand_IsSpecificHand(handText)) {
        return 1;
    }

    char *newstr = strdup(handText);
    const char *p = newstr;
    int suitOffsuit = 0;
    int seenCards = 0;

    while (*p != '\0' && seenCards < 2) {
        if (NULL != strchr("23456789TtJjQqKkAaXx", *p)) {
            seenCards++;
            p++; while (*p == ' ') p++;
            if (NULL != strchr("23456789TtJjQqKkAaXx", *p)) {
                p++; while (*p == ' ') p++;
                if (NULL != strchr("SsOo", *p)) {
                    seenCards++;
                    suitOffsuit = 1;
                    p++; while (*p == ' ') p++;
                    if (*p == '+') {
                        p++; while (*p == ' ') p++;
                    }
                }
                else if (*p == '+') {
                    seenCards++;
                    p++; while (*p == ' ') p++;
                }
                else if (*p == '-') {
                    seenCards--;
                    p++; while (*p == ' ') p++;
                    if (NULL != strchr("23456789TtJjQqKkAa", *p)) {
                        p++; while (*p == ' ') p++; seenCards++;
                        if (NULL != strchr("23456789TtJjQqKkAa", *p)) {
                            seenCards++;
                            p++; while (*p == ' ') p++;
                            if (*p != '\0') {
                                if (NULL != strchr("SsOo", *p)) {
                                    if (suitOffsuit) {
                                        p++; while (*p == ' ') p++;
                                    }
                                    else {
                                        printf("%d: Unexpected %c, at %s %I64d\n", __LINE__, *p, p, (long long)(p-handText+1));
                                        goto error;
                                    }
                                }
                            }
                        }
                        else {
                            printf("%d: Unexpected %c, at %s %I64d\n", __LINE__, *p, p, (long long)(p-handText+1));
                            goto error;
                        }
                    }
                }
                else if (*p == '\0') {
                    continue;
                }
                else {
                    printf("%d: Unexpected %c, at %s %I64d\n", __LINE__, *p, p, (long long)(p-handText+1));
                    goto error;
                }
            }
            else {
                printf("%d: Unexpected %c, at %s %I64d\n", __LINE__, *p, p, (long long)(p-handText+1));
                goto error;
            }
        }
        else {
            goto error;
        }
    }
    
    if (seenCards != 2) {
        printf("Only saw %d cards in %s\n", seenCards, handText);
        goto error;
    }
    if (*p != '\0') {
        printf("Unexpected %s at end of %s\n", p, handText);
        goto error;
    }

    return 1;
error:
    if (newstr) free(newstr);
    return 0;
}

int HoldemAgnosticHand_Instantiate(const char* handText, const char* deadText, HandList* handList) {
    StdDeck_CardMask deadCards;
    StdDeck_CardMask_RESET(deadCards);
        
    if (deadText && strlen(deadText)) {
        int suit, rank;
        StdDeck_CardMask hand;
        for(const char* pCard = deadText; *pCard != '\0'; pCard += 2) {
            rank = CharToRank(*pCard);
            suit = CharToSuit(*(pCard+1));
            hand = StdDeck_MASK( StdDeck_MAKE_CARD(rank, suit) );
            StdDeck_CardMask_OR(deadCards, deadCards, hand);
        }
    }

    return HoldemAgnosticHand_Instantiate_StdDeck(handText, deadCards, handList);
}

int HoldemAgnosticHand_Instantiate_StdDeck(const char* handText, StdDeck_CardMask deadCards, HandList* handList) {
    if (strcmp(handText, "XxXx") == 0) {
        return HoldemAgnosticHand_InstantiateRandom(deadCards, handList);
    }

    int isPlus = (NULL != strchr(handText, '+'));
    int isSlice = (NULL != strchr(handText, '-'));
    int handRanks[2] = {0,0};
    int rankCeils[2] = {0,0};

    if (isSlice) {
        const char* index = strchr(handText, '-');

        char handCeil[4];
        char handFloor[4];
        strncpy(handCeil, handText, index - handText);
        strcpy(handFloor, index + 1);

        handRanks[0] = CharToRank(handFloor[0]);
        handRanks[1] = CharToRank(handFloor[1]);
        rankCeils[0] = CharToRank(handCeil[0]);
        rankCeils[1] = CharToRank(handCeil[1]);
    }
    else {
        handRanks[0] = CharToRank(handText[0]);
        handRanks[1] = CharToRank(handText[1]);
        rankCeils[0] = isPlus ? 12 : handRanks[0];  // Assuming Ace is 12
        rankCeils[1] = (NULL != strchr("Xx", handText[1])) ? 12 : 11; // Assuming King is 11
    }

    StdDeck_CardMask hand;
    int combos = 0;

    if (HoldemAgnosticHand_IsPair(handText)) {
        StdDeck_CardMask card1, card2;

        for (int rank = handRanks[0]; rank <= rankCeils[0]; rank++) {
            for(int suit1 = 0; suit1 <= 3; suit1++) {
                for (int suit2 = suit1 + 1; suit2 <= 3; suit2++) {
                    card1 = StdDeck_MASK( StdDeck_MAKE_CARD(rank, suit1) );
                    card2 = StdDeck_MASK( StdDeck_MAKE_CARD(rank, suit2) );
                    StdDeck_CardMask_OR(hand, card1, card2);
                    if (!StdDeck_CardMask_ANY_SET(deadCards, hand)) {
                        handList->hands[handList->count++] = hand;
                        combos++;
                    }
                }
            }
        }
    } else if (HoldemAgnosticHand_IsSuited(handText)) {
        StdDeck_CardMask card1, card2, hand;
        int rank0Increment = 1;
        if (handRanks[0] == 12)
            rank0Increment = 0;
        for (int rank0 = handRanks[0], rank1 = handRanks[1]; 
            rank0 <= rankCeils[0] && rank1 <= rankCeils[1];
            rank0 += rank0Increment, rank1++) {
            for(int suit = 0; suit <= 3; suit++) {
                if (rank0 == rank1)
                    continue;
                card1 = StdDeck_MASK( StdDeck_MAKE_CARD(rank0, suit) );
                card2 = StdDeck_MASK( StdDeck_MAKE_CARD(rank1, suit) );
                StdDeck_CardMask_OR(hand, card1, card2);
                if (!StdDeck_CardMask_ANY_SET(deadCards, hand)) {
                    handList->hands[handList->count++] = hand;
                    combos++;
                }
            }
        }
    } else if (HoldemAgnosticHand_IsOffSuit(handText)) {
        StdDeck_CardMask card1, card2, hand;

        int rank0Increment = 1;
        if (handRanks[0] == 12)
            rank0Increment = 0;

        for (int rank0 = handRanks[0], rank1 = handRanks[1]; 
            rank0 <= rankCeils[0] && rank1 <= rankCeils[1];
            rank0 += rank0Increment, rank1++) {
            for(int suit1 = 0; suit1 <= 3; suit1++) {
                for (int suit2 = 0; suit2 <= 3; suit2++) {
                    if (suit1 == suit2)
                        continue;

                    card1 = StdDeck_MASK( StdDeck_MAKE_CARD(rank0, suit1) );
                    card2 = StdDeck_MASK( StdDeck_MAKE_CARD(rank1, suit2) );
                    StdDeck_CardMask_OR(hand, card1, card2);
                    if (!StdDeck_CardMask_ANY_SET(deadCards, hand)) {
                        handList->hands[handList->count++] = hand;
                        combos++;
                    }
                }
            }
        }
    } else {
        StdDeck_CardMask card1, card2, hand;

        int rank0Increment = 1;
        if (handRanks[0] == 12)
            rank0Increment = 0;

        for (int rank0 = handRanks[0], rank1 = handRanks[1]; 
            rank0 <= rankCeils[0] && rank1 <= rankCeils[1];
            rank0 += rank0Increment, rank1++) {
            for(int suit1 = 0; suit1 <= 3; suit1++) {
                for (int suit2 = 0; suit2 <= 3; suit2++) {
                    if (rank0 == rank1 && suit1 == suit2)
                        continue;
                    card1 = StdDeck_MASK( StdDeck_MAKE_CARD(rank0, suit1) );
                    card2 = StdDeck_MASK( StdDeck_MAKE_CARD(rank1, suit2) );
                    StdDeck_CardMask_OR(hand, card1, card2);
                    if (!StdDeck_CardMask_ANY_SET(deadCards, hand)) {
                        handList->hands[handList->count++] = hand;
                        combos++;
                    }
                }
            }
        }
    }

    return combos;
}

int HoldemAgnosticHand_InstantiateRandom(StdDeck_CardMask deadCards, HandList* handList) {
    StdDeck_CardMask curHand;
    DECK_ENUMERATE_2_CARDS_D(StdDeck, curHand, deadCards, handList->hands[handList->count++] = curHand; );
    return handList->count;
}

int HoldemAgnosticHand_IsPair(const char* handText) { 
    return (handText[0] == handText[1]);
}

int HoldemAgnosticHand_IsSuited(const char* handText) { 
    return (strlen(handText) >= 3 && handText[2] == 's');
}

int HoldemAgnosticHand_IsOffSuit(const char* handText) { 
    return (strlen(handText) >= 3 && handText[2] == 'o');
}

int HoldemAgnosticHand_IsInclusive(const char* handText) { 
    int textlen = strlen(handText);
    return !HoldemAgnosticHand_IsPair(handText) && ((textlen == 2) || (textlen == 3 && handText[2] == '+'));
}

int HoldemAgnosticHand_IsSpecificHand(const char* handText) {
    if (strlen(handText) == 4) {
        return (NULL != strchr("SsHhDdCc", handText[1]) && 
                NULL != strchr("SsHhDdCc", handText[3]) &&
                NULL != strchr("23456789TtJjQqKkAa", handText[0]) &&
                NULL != strchr("23456789TtJjQqKkAa", handText[2]));
    }

    return 0;
}
