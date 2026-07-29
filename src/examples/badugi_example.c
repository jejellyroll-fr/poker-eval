/*
 * Badugi, Badacey, and Badeucy Example Program
 *
 * This example demonstrates the evaluation of Badugi hands and its variants.
 * Badugi is a 4-card lowball game where all cards must be different suits and ranks.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <poker_eval/games/badugi_eval.h>
#include <poker_eval/games/rules_badugi.h>
#include <poker_eval/deck/deck_std.h>

/* Helper function to create a card from rank and suit strings */
static int make_card_from_string(const char *rank_str, const char *suit_str)
{
    int rank, suit;

    /* Parse rank */
    switch (rank_str[0])
    {
    case 'A':
    case 'a':
        rank = StdDeck_Rank_ACE;
        break;
    case '2':
        rank = StdDeck_Rank_2;
        break;
    case '3':
        rank = StdDeck_Rank_3;
        break;
    case '4':
        rank = StdDeck_Rank_4;
        break;
    case '5':
        rank = StdDeck_Rank_5;
        break;
    case '6':
        rank = StdDeck_Rank_6;
        break;
    case '7':
        rank = StdDeck_Rank_7;
        break;
    case '8':
        rank = StdDeck_Rank_8;
        break;
    case '9':
        rank = StdDeck_Rank_9;
        break;
    case 'T':
    case 't':
        rank = StdDeck_Rank_TEN;
        break;
    case 'J':
    case 'j':
        rank = StdDeck_Rank_JACK;
        break;
    case 'Q':
    case 'q':
        rank = StdDeck_Rank_QUEEN;
        break;
    case 'K':
    case 'k':
        rank = StdDeck_Rank_KING;
        break;
    default:
        return -1;
    }

    /* Parse suit */
    switch (suit_str[0])
    {
    case 'S':
    case 's':
        suit = StdDeck_Suit_SPADES;
        break;
    case 'H':
    case 'h':
        suit = StdDeck_Suit_HEARTS;
        break;
    case 'D':
    case 'd':
        suit = StdDeck_Suit_DIAMONDS;
        break;
    case 'C':
    case 'c':
        suit = StdDeck_Suit_CLUBS;
        break;
    default:
        return -1;
    }

    return StdDeck_MAKE_CARD(rank, suit);
}

/* Helper function to create a hand from string representation */
static StdDeck_CardMask create_hand_from_string(const char *hand_str)
{
    StdDeck_CardMask hand;
    char cards[5][3];
    int num_cards = 0;

    StdDeck_CardMask_RESET(hand);

    /* Parse hand string like "As 2h 3d 4c" */
    if (sscanf(hand_str, "%2s %2s %2s %2s %2s",
               cards[0], cards[1], cards[2], cards[3], cards[4]) >= 4)
    {
        num_cards = 5;
    }
    else if (sscanf(hand_str, "%2s %2s %2s %2s",
                    cards[0], cards[1], cards[2], cards[3]) == 4)
    {
        num_cards = 4;
    }

    for (int i = 0; i < num_cards; i++)
    {
        if (strlen(cards[i]) >= 2)
        {
            char rank_str[2] = {cards[i][0], '\0'};
            char suit_str[2] = {cards[i][1], '\0'};
            int card = make_card_from_string(rank_str, suit_str);
            if (card >= 0)
            {
                StdDeck_CardMask_SET(hand, card);
            }
        }
    }

    return hand;
}

/* Print a card in readable format */
static void print_card(int card)
{
    const char *ranks = "23456789TJQKA";
    const char *suits = "shdc";

    int rank = StdDeck_RANK(card);
    int suit = StdDeck_SUIT(card);

    printf("%c%c", ranks[rank], suits[suit]);
}

/* Print a hand in readable format */
static void print_hand(StdDeck_CardMask hand)
{
    int first = 1;
    for (int i = 0; i < StdDeck_N_CARDS; i++)
    {
        if (StdDeck_CardMask_CARD_IS_SET(hand, i))
        {
            if (!first)
                printf(" ");
            print_card(i);
            first = 0;
        }
    }
}

/* Demonstrate Badugi evaluation */
static void demo_badugi(void)
{
    printf("=== Badugi Evaluation Demo ===\n\n");

    const char *test_hands[] = {
        "As 2h 3d 4c", /* Perfect Badugi */
        "As 2h 3d Kc", /* High Badugi */
        "As 2h 3d 3c", /* Three-card (duplicate rank) */
        "As 2h 2d 4c", /* Three-card (duplicate rank) */
        "As Ah 3d 4c", /* Three-card (duplicate rank) */
        "As 2s 3d 4c", /* Three-card (duplicate suit) */
        "As 2h 3h 4h", /* Two-card (multiple duplicate suits) */
        "As As Ad Ac", /* One-card (all same rank) */
        "As 2s 3s 4s"  /* One-card (all same suit) */
    };

    int num_hands = sizeof(test_hands) / sizeof(test_hands[0]);

    for (int i = 0; i < num_hands; i++)
    {
        StdDeck_CardMask hand = create_hand_from_string(test_hands[i]);
        BadugiHandVal val = StdDeck_BadugiRules_EVAL_4(hand);

        printf("Hand: %-12s -> ", test_hands[i]);
        print_hand(hand);
        printf(" -> ");

        char result[80];
        BadugiHandVal_toString(val, result);
        printf("%s\n", result);
    }
}

/* Demonstrate hand comparison */
static void demo_comparison(void)
{
    printf("\n=== Badugi Hand Comparison Demo ===\n\n");

    const char *hands[][2] = {
        {"As 2h 3d 4c", "As 2h 3d 5c"}, /* A234 vs A235 */
        {"As 2h 3d 4c", "2s 3h 4d 5c"}, /* A234 vs 2345 */
        {"Ks Qh Jd Tc", "As 2h 3d"},    /* Four-card vs Three-card */
        {"As 2h 3d", "As 2h"},          /* Three-card vs Two-card */
        {"As 2h", "As"},                /* Two-card vs One-card */
    };

    int num_comparisons = sizeof(hands) / sizeof(hands[0]);

    for (int i = 0; i < num_comparisons; i++)
    {
        StdDeck_CardMask hand1 = create_hand_from_string(hands[i][0]);
        StdDeck_CardMask hand2 = create_hand_from_string(hands[i][1]);

        BadugiHandVal val1 = StdDeck_BadugiRules_EVAL_N(hand1, 5);
        BadugiHandVal val2 = StdDeck_BadugiRules_EVAL_N(hand2, 5);

        printf("%-12s vs %-12s -> ", hands[i][0], hands[i][1]);

        if (val1 < val2)
        {
            printf("%s wins\n", hands[i][0]);
        }
        else if (val1 > val2)
        {
            printf("%s wins\n", hands[i][1]);
        }
        else
        {
            printf("Tie\n");
        }
    }
}

/* Demonstrate Badacey evaluation */
static void demo_badacey(void)
{
    printf("\n=== Badacey Evaluation Demo ===\n\n");
    printf("Badacey combines Badugi with A-5 lowball.\n");
    printf("Any Badugi (even 1-card) beats any A-5 lowball hand.\n\n");

    const char *test_hands[] = {
        "As 2h 3d 4c 5s", /* Badugi possible */
        "As 2h 3d 4c Kh", /* Badugi possible */
        "As 2s 3s 4s 5s", /* No Badugi - all same suit, A-5 lowball */
        "2s 3s 4s 5s 6s", /* No Badugi - all same suit, A-5 lowball */
        "As 2h 3d 4c 4s"  /* Badugi possible (3-card) */
    };

    int num_hands = sizeof(test_hands) / sizeof(test_hands[0]);

    for (int i = 0; i < num_hands; i++)
    {
        StdDeck_CardMask hand = create_hand_from_string(test_hands[i]);
        HandVal val = StdDeck_BadaceyRules_EVAL_5(hand);
        int handType = HandVal_HANDTYPE(val);

        printf("Hand: %-15s -> ", test_hands[i]);

        if (handType <= BadaceyRules_HandType_ONE)
        {
            printf("Badugi (%s)\n", BadaceyRules_handTypeNames[handType]);
        }
        else
        {
            printf("A-5 Lowball\n");
        }
    }
}

/* Demonstrate Badeucy evaluation */
static void demo_badeucy(void)
{
    printf("\n=== Badeucy Evaluation Demo ===\n\n");
    printf("Badeucy combines Badugi with 2-7 lowball.\n");
    printf("Any Badugi (even 1-card) beats any 2-7 lowball hand.\n\n");

    const char *test_hands[] = {
        "As 2h 3d 4c 5s", /* Badugi possible */
        "As 2h 3d 4c Kh", /* Badugi possible */
        "2s 3s 4s 5s 7s", /* No Badugi - all same suit, 2-7 lowball */
        "2s 3s 4s 6s 7s", /* No Badugi - all same suit, 2-7 lowball */
        "As 2h 3d 4c 4s"  /* Badugi possible (3-card) */
    };

    int num_hands = sizeof(test_hands) / sizeof(test_hands[0]);

    for (int i = 0; i < num_hands; i++)
    {
        StdDeck_CardMask hand = create_hand_from_string(test_hands[i]);
        HandVal val = StdDeck_BadeucyRules_EVAL_5(hand);
        int handType = HandVal_HANDTYPE(val);

        printf("Hand: %-15s -> ", test_hands[i]);

        if (handType <= BadeucyRules_HandType_ONE)
        {
            printf("Badugi (%s)\n", BadeucyRules_handTypeNames[handType]);
        }
        else
        {
            printf("2-7 Lowball\n");
        }
    }
}

/* Interactive mode */
static void interactive_mode(void)
{
    printf("\n=== Interactive Badugi Evaluator ===\n\n");
    printf("Enter hands in format: As 2h 3d 4c (or As 2h 3d 4c 5s for 5 cards)\n");
    printf("Type 'quit' to exit\n\n");

    char input[100];
    while (1)
    {
        printf("Enter hand: ");
        if (!fgets(input, sizeof(input), stdin))
            break;

        /* Remove newline */
        input[strcspn(input, "\n")] = 0;

        if (strcmp(input, "quit") == 0)
            break;

        StdDeck_CardMask hand = create_hand_from_string(input);

        /* Count cards */
        int num_cards = 0;
        for (int i = 0; i < StdDeck_N_CARDS; i++)
        {
            if (StdDeck_CardMask_CARD_IS_SET(hand, i))
            {
                num_cards++;
            }
        }

        if (num_cards < 4)
        {
            printf("Error: Need at least 4 cards\n");
            continue;
        }

        printf("\nEvaluating: ");
        print_hand(hand);
        printf(" (%d cards)\n", num_cards);

        /* Badugi evaluation */
        BadugiHandVal badugi_val = StdDeck_BadugiRules_EVAL_N(hand, num_cards);
        char badugi_str[80];
        BadugiHandVal_toString(badugi_val, badugi_str);
        printf("Badugi:  %s\n", badugi_str);

        if (num_cards == 5)
        {
            /* Badacey evaluation */
            HandVal badacey_val = StdDeck_BadaceyRules_EVAL_5(hand);
            int badacey_type = HandVal_HANDTYPE(badacey_val);
            printf("Badacey: ");
            if (badacey_type <= BadaceyRules_HandType_ONE)
            {
                printf("Badugi (%s)\n", BadaceyRules_handTypeNames[badacey_type]);
            }
            else
            {
                printf("A-5 Lowball\n");
            }

            /* Badeucy evaluation */
            HandVal badeucy_val = StdDeck_BadeucyRules_EVAL_5(hand);
            int badeucy_type = HandVal_HANDTYPE(badeucy_val);
            printf("Badeucy: ");
            if (badeucy_type <= BadeucyRules_HandType_ONE)
            {
                printf("Badugi (%s)\n", BadeucyRules_handTypeNames[badeucy_type]);
            }
            else
            {
                printf("2-7 Lowball\n");
            }
        }

        printf("\n");
    }
}

int main(int argc, char *argv[])
{
    printf("Badugi, Badacey, and Badeucy Evaluation Example\n");
    printf("===============================================\n\n");

    printf("Badugi Rules:\n");
    printf("- 4-card lowball game\n");
    printf("- All cards must be different suits\n");
    printf("- All cards must be different ranks\n");
    printf("- Aces are low\n");
    printf("- Lower ranks are better\n");
    printf("- More cards beats fewer cards\n\n");

    demo_badugi();
    demo_comparison();
    demo_badacey();
    demo_badeucy();

    if (argc > 1 && strcmp(argv[1], "-i") == 0)
    {
        interactive_mode();
    }
    else
    {
        printf("\nRun with -i for interactive mode\n");
    }

    return 0;
}
