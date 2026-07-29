/*
 * rules_manila.c - Manila Poker rules implementation
 *
 * Manila specific rules:
 * - Flush beats Full House
 * - Only high straights (no A-2-3-4-5)
 * - Must use both hole cards + exactly 3 community cards
 */

#include <stdio.h>
#include <string.h>
#include <poker_eval/games/rules_manila.h>
#include <poker_eval/deck/deck_manila.h>

/* Manila hand type names */
static const char *manilaHandTypeNames[ManilaRules_HandType_COUNT] __attribute__((unused)) = {
    "NoPair",
    "OnePair",
    "TwoPair",
    "Trips",
    "Straight",
    "Flush",        /* Higher than Full House in Manila */
    "FullHouse",    /* Lower than Flush in Manila */
    "FourOfAKind",
    "StraightFlush"
};

/* Manila straight validation */
int
Manila_IsValidStraight(int ranks) {
    /* Manila valid straights (only high straights):
     * 8-9-T-J-Q (0x1F00)
     * 9-T-J-Q-K (0x3E00)
     * T-J-Q-K-A (0x7C00)
     */

    static const int manilaValidStraights[] = {
        0x1F00,  /* 8-9-T-J-Q */
        0x3E00,  /* 9-T-J-Q-K */
        0x7C00,  /* T-J-Q-K-A */
        0        /* terminator */
    };

    int i;
    for (i = 0; manilaValidStraights[i] != 0; i++) {
        if ((ranks & manilaValidStraights[i]) == manilaValidStraights[i]) {
            return manilaValidStraights[i];
        }
    }
    return 0;
}

/* Manila hand value to string */
int
ManilaRules_HandVal_toString(HandVal handval, char *outString) {
    int handType = HandVal_HANDTYPE(handval);
    int topCard = HandVal_TOP_CARD(handval);
    int secondCard = HandVal_SECOND_CARD(handval);

    if (handType < 0 || handType >= ManilaRules_HandType_COUNT) {
        strcpy(outString, "Invalid");
        return 1;
    }

    switch (handType) {
        case ManilaRules_HandType_STFLUSH:
            sprintf(outString, "StraightFlush (%c high)",
                    ManilaDeck_rankChars[topCard]);
            break;

        case ManilaRules_HandType_QUADS:
            sprintf(outString, "FourOfAKind (%cs) with %c kicker",
                    ManilaDeck_rankChars[topCard],
                    ManilaDeck_rankChars[secondCard]);
            break;

        case ManilaRules_HandType_FLUSH:
            sprintf(outString, "Flush (%c high)",
                    ManilaDeck_rankChars[topCard]);
            break;

        case ManilaRules_HandType_FULLHOUSE:
            sprintf(outString, "FullHouse (%cs full of %cs)",
                    ManilaDeck_rankChars[topCard],
                    ManilaDeck_rankChars[secondCard]);
            break;

        case ManilaRules_HandType_STRAIGHT:
            sprintf(outString, "Straight (%c high)",
                    ManilaDeck_rankChars[topCard]);
            break;

        case ManilaRules_HandType_TRIPS:
            sprintf(outString, "ThreeOfAKind (%cs) with %c kicker",
                    ManilaDeck_rankChars[topCard],
                    ManilaDeck_rankChars[secondCard]);
            break;

        case ManilaRules_HandType_TWOPAIR:
            sprintf(outString, "TwoPair (%cs and %cs)",
                    ManilaDeck_rankChars[topCard],
                    ManilaDeck_rankChars[secondCard]);
            break;

        case ManilaRules_HandType_ONEPAIR:
            sprintf(outString, "OnePair (%cs) with %c kicker",
                    ManilaDeck_rankChars[topCard],
                    ManilaDeck_rankChars[secondCard]);
            break;

        case ManilaRules_HandType_NOPAIR:
            sprintf(outString, "HighCard (%c)",
                    ManilaDeck_rankChars[topCard]);
            break;

        default:
            strcpy(outString, "Unknown");
            return 1;
    }

    return 0;
}

/* Print Manila hand value */
int
ManilaRules_HandVal_print(HandVal handval) {
    char buffer[128];
    if (ManilaRules_HandVal_toString(handval, buffer) == 0) {
        printf("%s", buffer);
        return 0;
    }
    return 1;
}

/* Evaluate Manila hand with modified hierarchy */
HandVal
Manila_EvaluateHand(StdDeck_CardMask cards, int num_cards) {
    /* This is a placeholder - the actual evaluation is done in eval_manila.h */
    /* For now, convert StdDeck_CardMask to ManilaDeck_CardMask and evaluate */

    ManilaDeck_CardMask manila_cards;
    ManilaDeck_CardMask_RESET(manila_cards);

    /* Copy relevant cards from standard deck to Manila deck */
    int i;
    for (i = 0; i < 52; i++) {
        if (StdDeck_CardMask_CARD_IS_SET(cards, i)) {
            int rank = StdDeck_RANK(i);
            int suit = StdDeck_SUIT(i);

            /* Only include cards 8 and above */
            if (rank >= ManilaDeck_Rank_8 && rank <= ManilaDeck_Rank_ACE) {
                int manila_card = ManilaDeck_MAKE_CARD(rank, suit);
                ManilaDeck_CardMask_SET(manila_cards, manila_card);
            }
        }
    }

    /* Use the Manila evaluation function */
    /* This would call Manila_StdRules_EVAL_N from eval_manila.h */
    return HandVal_NOTHING; /* Placeholder */
}
