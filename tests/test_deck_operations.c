#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/deck/deck_joker.h>
#include <poker_eval/deck/deck_short.h>
#include <poker_eval/core/universal_deck.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck.h>

/* Compatibility defines for legacy test code */
#define DECK_STANDARD UNIVERSAL_DECK_STANDARD
#define DECK_JOKER UNIVERSAL_DECK_JOKER

/* Function prototypes */
static void test_stddeck_card_string_conversion(void);
static void test_stddeck_mask_to_cards_and_numcards(void);
static void test_jokerdeck_card_string_conversion(void);
static void test_jokerdeck_mask_to_cards_and_numcards(void);
static void test_shortdeck_card_string_conversion(void);
static void test_shortdeck_mask_to_cards_and_numcards(void);
static void test_universal_deck_conversions(void);
static void test_universal_string_and_type(void);
static void test_genericdeck_functions(void);
static void test_invalid_string_to_card(void);
static void test_empty_and_full_masks(void);
static void test_simple_shuffle(void);
static void test_universal_convert_edge_cases(void);

static void test_stddeck_card_string_conversion(void) {
    char buf[4];
    int card;
    for (int r = StdDeck_Rank_FIRST; r <= StdDeck_Rank_LAST; ++r) {
        for (int s = StdDeck_Suit_FIRST; s <= StdDeck_Suit_LAST; ++s) {
            int idx = StdDeck_MAKE_CARD(r, s);
            int n = StdDeck_cardToString(idx, buf);
            assert(n == 2);
            int ok = StdDeck_stringToCard(buf, &card);
            assert(ok == 2);
            assert(card == idx);
            (void)n; // Suppress unused variable warning
            (void)ok; // Suppress unused variable warning
        }
    }
}

static void test_stddeck_mask_to_cards_and_numcards(void) {
    StdDeck_CardMask mask;
    StdDeck_CardMask_RESET(mask);
    int cards[StdDeck_N_CARDS];
    for (int i = 0; i < 5; ++i) {
        StdDeck_CardMask_SET(mask, i);
    }
    int n = StdDeck.maskToCards(&mask, cards);
    assert(n == 5);
    for (int i = 0; i < 5; ++i) {
        assert(cards[i] < StdDeck_N_CARDS);
    }
    assert(StdDeck.numCards(&mask) == 5);
    (void)n; // Suppress unused variable warning
}

static void test_jokerdeck_card_string_conversion(void) {
    char buf[4];
    int card;
    // Test all non-joker cards
    for (int r = JokerDeck_Rank_FIRST; r <= JokerDeck_Rank_LAST; ++r) {
        for (int s = JokerDeck_Suit_FIRST; s <= JokerDeck_Suit_LAST; ++s) {
            int idx = JokerDeck_MAKE_CARD(r, s);
            int n = JokerDeck_cardToString(idx, buf);
            assert(n == 2);
            int ok = JokerDeck_stringToCard(buf, &card);
            assert(ok == 2);
            assert(card == idx);
            (void)n; // Suppress unused variable warning
            (void)ok; // Suppress unused variable warning
        }
    }
    // Test joker card
    int n = JokerDeck_cardToString(JokerDeck_JOKER, buf);
    assert(n == 2);
    char joker_str[] = "Xx"; // Non-const string
    int ok = JokerDeck_stringToCard(joker_str, &card);
    assert(ok == 2);
    assert(card == JokerDeck_JOKER);
    (void)n; // Suppress unused variable warning
    (void)ok; // Suppress unused variable warning
}

static void test_jokerdeck_mask_to_cards_and_numcards(void) {
    JokerDeck_CardMask mask;
    JokerDeck_CardMask_RESET(mask);
    int cards[JokerDeck_N_CARDS];
    for (int i = 0; i < 5; ++i) {
        JokerDeck_CardMask_SET(mask, i);
    }
    JokerDeck_CardMask_SET(mask, JokerDeck_JOKER);
    int n = JokerDeck.maskToCards(&mask, cards);
    assert(n == 6);
    assert(JokerDeck.numCards(&mask) == 6);
    (void)n; // Suppress unused variable warning
}

static void test_shortdeck_card_string_conversion(void) {
    char buf[4];
    int card;
    for (int r = ShortDeck_Rank_FIRST; r <= ShortDeck_Rank_LAST; ++r) {
        for (int s = ShortDeck_Suit_FIRST; s <= ShortDeck_Suit_LAST; ++s) {
            int idx = ShortDeck_MAKE_CARD(r, s);
            int n = ShortDeck_cardToString(idx, buf);
            assert(n == 2);
            int ok = ShortDeck_stringToCard(buf, &card);
            assert(ok == 2);
            assert(card == idx);
            (void)n; // Suppress unused variable warning
            (void)ok; // Suppress unused variable warning
        }
    }
}

static void test_shortdeck_mask_to_cards_and_numcards(void) {
    ShortDeck_CardMask mask;
    ShortDeck_CardMask_RESET(mask);
    int cards[ShortDeck_N_CARDS];
    for (int i = 0; i < 5; ++i) {
        ShortDeck_CardMask_SET(mask, i);
    }
    int n = ShortDeck.maskToCards(&mask, cards);
    assert(n == 5);
    assert(ShortDeck.numCards(&mask) == 5);
    (void)n; // Suppress unused variable warning
}

static void test_universal_deck_conversions(void) {
    StdDeck_CardMask std_mask;
    JokerDeck_CardMask joker_mask;
    StdDeck_CardMask_RESET(std_mask);
    JokerDeck_CardMask_RESET(joker_mask);
    StdDeck_CardMask_SET(std_mask, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(std_mask, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    Universal_ConvertStdToJoker(std_mask, &joker_mask);
    assert(JokerDeck_CardMask_CARD_IS_SET(joker_mask, JokerDeck_MAKE_CARD(JokerDeck_Rank_ACE, JokerDeck_Suit_SPADES)));
    assert(JokerDeck_CardMask_CARD_IS_SET(joker_mask, JokerDeck_MAKE_CARD(JokerDeck_Rank_KING, JokerDeck_Suit_HEARTS)));
    Universal_ConvertJokerToStd(joker_mask, &std_mask);
    assert(StdDeck_CardMask_CARD_IS_SET(std_mask, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES)));
    assert(StdDeck_CardMask_CARD_IS_SET(std_mask, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS)));
}

static void test_universal_string_and_type(void) {
    int card;
    deck_type_t type;
    char joker_str[] = "Xx"; // Non-const string
    int n = Universal_StringToCard(joker_str, &card, &type);
    assert(n == 2);
    assert(card == JokerDeck_JOKER);
    assert(type == DECK_JOKER);
    char ace_str[] = "As"; // Non-const string
    n = Universal_StringToCard(ace_str, &card, &type);
    assert(n == 2);
    assert(type == DECK_STANDARD);
    char buf[4];
    n = Universal_CardToString(JokerDeck_JOKER, buf, DECK_JOKER);
    assert(strcmp(buf, "Xx") == 0);
    n = Universal_CardToString(StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES), buf, DECK_STANDARD);
    assert(buf[0] == 'A' && buf[1] == 's');
    (void)n; // Suppress unused variable warning
}

// Test GenericDeck_* functions for all deck types
static void test_genericdeck_functions(void) {
    char buf[128];
    StdDeck_CardMask std_mask;
    StdDeck_CardMask_RESET(std_mask);
    for (int i = 0; i < 3; ++i) StdDeck_CardMask_SET(std_mask, i);
    assert(GenericDeck_numCards(&StdDeck, &std_mask) == 3);
    assert(GenericDeck_maskToString(&StdDeck, &std_mask, buf) < 128);
    assert(strlen(buf) > 0);
    assert(GenericDeck_cardString(&StdDeck, 0)[0] != 0);

    JokerDeck_CardMask joker_mask;
    JokerDeck_CardMask_RESET(joker_mask);
    for (int i = 0; i < 2; ++i) JokerDeck_CardMask_SET(joker_mask, i);
    JokerDeck_CardMask_SET(joker_mask, JokerDeck_JOKER);
    assert(GenericDeck_numCards(&JokerDeck, &joker_mask) == 3);
    assert(GenericDeck_maskToString(&JokerDeck, &joker_mask, buf) < 128);
    assert(strlen(buf) > 0);
    assert(GenericDeck_cardString(&JokerDeck, JokerDeck_JOKER)[0] != 0);

    ShortDeck_CardMask short_mask;
    ShortDeck_CardMask_RESET(short_mask);
    for (int i = 0; i < 2; ++i) ShortDeck_CardMask_SET(short_mask, i);
    assert(GenericDeck_numCards(&ShortDeck, &short_mask) == 2);
    assert(GenericDeck_maskToString(&ShortDeck, &short_mask, buf) < 128);
    assert(strlen(buf) > 0);
    assert(GenericDeck_cardString(&ShortDeck, 0)[0] != 0);
    (void)buf; // Suppress unused variable warning
}

// Test invalid string to card conversion
static void test_invalid_string_to_card(void) {
    int card;
    char invalid1[] = "ZZ";
    char invalid2[] = "??";
    char invalid3[] = "1h";
    assert(StdDeck_stringToCard(invalid1, &card) == 0);
    assert(JokerDeck_stringToCard(invalid2, &card) == 0);
    assert(ShortDeck_stringToCard(invalid3, &card) == 0);
    (void)card; // Suppress unused variable warning
    (void)invalid1; // Suppress unused variable warning
    (void)invalid2; // Suppress unused variable warning
    (void)invalid3; // Suppress unused variable warning
}

// Test empty and full masks
static void test_empty_and_full_masks(void) {
    StdDeck_CardMask std_mask;
    StdDeck_CardMask_RESET(std_mask);
    assert(StdDeck.numCards(&std_mask) == 0);
    for (int i = 0; i < StdDeck_N_CARDS; ++i) StdDeck_CardMask_SET(std_mask, i);
    assert(StdDeck.numCards(&std_mask) == StdDeck_N_CARDS);

    JokerDeck_CardMask joker_mask;
    JokerDeck_CardMask_RESET(joker_mask);
    assert(JokerDeck.numCards(&joker_mask) == 0);
    for (int i = 0; i < JokerDeck_N_CARDS; ++i) JokerDeck_CardMask_SET(joker_mask, i);
    assert(JokerDeck.numCards(&joker_mask) == JokerDeck_N_CARDS);

    ShortDeck_CardMask short_mask;
    ShortDeck_CardMask_RESET(short_mask);
    assert(ShortDeck.numCards(&short_mask) == 0);
    for (int i = 0; i < ShortDeck_N_CARDS; ++i) ShortDeck_CardMask_SET(short_mask, i);
    assert(ShortDeck.numCards(&short_mask) == ShortDeck_N_CARDS);
}

// Test simple shuffle (Fisher-Yates)
static void test_simple_shuffle(void) {
    int deck[10];
    for (int i = 0; i < 10; ++i) deck[i] = i;
    // Shuffle
    for (int i = 9; i > 0; --i) {
        int j = i / 2; // deterministic for test
        int tmp = deck[i]; deck[i] = deck[j]; deck[j] = tmp;
    }
    // Verify all cards are still present
    for (int i = 0; i < 10; ++i) {
        int found = 0;
        for (int k = 0; k < 10; ++k) {
            if (deck[k] == i) {
                found = 1;
                break;
            }
        }
        assert(found == 1);
        (void)found; // Suppress unused variable warning
    }
}

// Test Universal_ConvertStdToJoker with empty and full masks
static void test_universal_convert_edge_cases(void) {
    StdDeck_CardMask std_mask;
    JokerDeck_CardMask joker_mask;
    StdDeck_CardMask_RESET(std_mask);
    JokerDeck_CardMask_RESET(joker_mask);
    Universal_ConvertStdToJoker(std_mask, &joker_mask);
    assert(JokerDeck_numCards(joker_mask) == 0);
    for (int i = 0; i < StdDeck_N_CARDS; ++i) StdDeck_CardMask_SET(std_mask, i);
    Universal_ConvertStdToJoker(std_mask, &joker_mask);
    assert(JokerDeck_numCards(joker_mask) == StdDeck_N_CARDS);
}

int main(void) {
    test_stddeck_card_string_conversion();
    test_stddeck_mask_to_cards_and_numcards();
    test_jokerdeck_card_string_conversion();
    test_jokerdeck_mask_to_cards_and_numcards();
    test_shortdeck_card_string_conversion();
    test_shortdeck_mask_to_cards_and_numcards();
    test_universal_deck_conversions();
    test_universal_string_and_type();
    test_genericdeck_functions();
    test_invalid_string_to_card();
    test_empty_and_full_masks();
    test_simple_shuffle();
    test_universal_convert_edge_cases();

    printf("All deck logic tests passed.\n");
    return 0;
}
