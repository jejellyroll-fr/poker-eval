#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/games/rules_std.h>
#include <poker_eval/games/eval_low.h>

// Structure de test simple
typedef struct
{
    const char *desc;
    const char *cards[7];
    int n_cards;
    int expected_hand_type; // StdRules_HandType_*
} TestCase;

/* Function prototypes */
static StdDeck_CardMask create_mask(const char *cards[], int n);
static const char *hand_type_str(int t);

// Helper pour créer un mask à partir d'une liste de strings
static StdDeck_CardMask create_mask(const char *cards[], int n)
{
    StdDeck_CardMask mask;
    StdDeck_CardMask_RESET(mask);
    for (int i = 0; i < n; ++i)
    {
        if (cards[i])
        {
            int card;
            char card_str[4];
            strcpy(card_str, cards[i]); // Copy to non-const string
            if (StdDeck_stringToCard(card_str, &card) == 2)
                StdDeck_CardMask_SET(mask, card);
        }
    }
    return mask;
}

static const char *hand_type_str(int t)
{
    switch (t)
    {
    case StdRules_HandType_NOPAIR:
        return "NoPair";
    case StdRules_HandType_ONEPAIR:
        return "OnePair";
    case StdRules_HandType_TWOPAIR:
        return "TwoPair";
    case StdRules_HandType_TRIPS:
        return "Trips";
    case StdRules_HandType_FULLHOUSE:
        return "FullHouse";
    case StdRules_HandType_QUADS:
        return "Quads";
    default:
        return "Unknown";
    }
}

TestCase tests[] = {
    // NoPair
    {"NoPair 5 cartes", {"2c", "3d", "4h", "5s", "7c", NULL, NULL}, 5, StdRules_HandType_NOPAIR},
    {"NoPair 6 cartes", {"2c", "3d", "4h", "5s", "7c", "8d", NULL}, 6, StdRules_HandType_NOPAIR},
    {"NoPair 7 cartes", {"2c", "3d", "4h", "5s", "7c", "8d", "9h"}, 7, StdRules_HandType_NOPAIR},
    // OnePair
    {"OnePair 5 cartes", {"2c", "2d", "4h", "5s", "7c", NULL, NULL}, 5, StdRules_HandType_ONEPAIR},
    {"OnePair 6 cartes", {"2c", "2d", "4h", "5s", "7c", "8d", NULL}, 6, StdRules_HandType_ONEPAIR},
    {"OnePair 7 cartes", {"2c", "2d", "4h", "5s", "7c", "8d", "9h"}, 7, StdRules_HandType_ONEPAIR},
    // TwoPair
    {"TwoPair 5 cartes", {"2c", "2d", "4h", "4s", "7c", NULL, NULL}, 5, StdRules_HandType_TWOPAIR},
    {"TwoPair 6 cartes (3 paires)", {"2c", "2d", "4h", "4s", "7c", "7d", NULL}, 6, StdRules_HandType_TWOPAIR},
    {"TwoPair 7 cartes (2 trips)", {"2c", "2d", "2h", "4s", "4c", "4d", "Kh"}, 7, StdRules_HandType_TWOPAIR},
    // Trips
    {"Trips 5 cartes", {"2c", "2d", "2h", "5s", "7c", NULL, NULL}, 5, StdRules_HandType_TRIPS},
    {"Trips 6 cartes", {"2c", "2d", "2h", "5s", "7c", "8d", NULL}, 6, StdRules_HandType_ONEPAIR},
    {"Trips 7 cartes", {"2c", "2d", "2h", "5s", "7c", "8d", "9h"}, 7, StdRules_HandType_ONEPAIR},
    // FullHouse
    {"FullHouse 5 cartes", {"2c", "2d", "2h", "4s", "4c", NULL, NULL}, 5, StdRules_HandType_FULLHOUSE},
    {"FullHouse 6 cartes", {"2c", "2d", "2h", "4s", "4c", "8d", NULL}, 6, StdRules_HandType_TWOPAIR},
    {"FullHouse 7 cartes", {"2c", "2d", "2h", "4s", "4c", "8d", "9h"}, 7, StdRules_HandType_ONEPAIR},
    // Quads
    {"Quads 5 cartes", {"2c", "2d", "2h", "2s", "7c", NULL, NULL}, 5, StdRules_HandType_QUADS},
    {"Quads 6 cartes", {"2c", "2d", "2h", "2s", "7c", "8d", NULL}, 6, StdRules_HandType_TRIPS},
    {"Quads 7 cartes", {"2c", "2d", "2h", "2s", "7c", "8d", "9h"}, 7, StdRules_HandType_ONEPAIR},
    // Cas mixtes complexes
    {"FullHouse + pair (KKK773)", {"Kh", "Kd", "Kc", "7s", "7h", "3d", NULL}, 6, StdRules_HandType_TWOPAIR},
    {"Trips + two pair (777KK3)", {"7h", "7d", "7c", "Kh", "Kd", "3s", NULL}, 6, StdRules_HandType_TWOPAIR},
    {"Quads + pair + single (4444KK2)", {"4h", "4d", "4c", "4s", "Kh", "Kd", "2c"}, 7, StdRules_HandType_TWOPAIR},
    {"Quads + trips (4444KKK)", {"4h", "4d", "4c", "4s", "Kh", "Kd", "Kc"}, 7, StdRules_HandType_FULLHOUSE},
    {"Two trips (KKKJJJ)", {"Kh", "Kd", "Kc", "Jh", "Jd", "Jc", NULL}, 6, StdRules_HandType_FULLHOUSE},
};

int main(void)
{
    int n = sizeof(tests) / sizeof(tests[0]);
    int pass = 0;
    for (int i = 0; i < n; ++i)
    {
        StdDeck_CardMask mask = create_mask(tests[i].cards, tests[i].n_cards);
        LowHandVal val = StdDeck_Lowball_EVAL(mask, tests[i].n_cards);
        int hand_type = LowHandVal_HANDTYPE(val);
        printf("Test: %-40s | Got: %-9s | Expected: %-9s | %s\n",
               tests[i].desc,
               hand_type_str(hand_type),
               hand_type_str(tests[i].expected_hand_type),
               (hand_type == tests[i].expected_hand_type) ? "PASS" : "FAIL");
        assert(hand_type == tests[i].expected_hand_type);
        pass += (hand_type == tests[i].expected_hand_type);
    }
    printf("\n%d/%d tests passed.\n", pass, n);
    return 0;
}
