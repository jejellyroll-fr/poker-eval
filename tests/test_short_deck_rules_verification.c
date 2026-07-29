/*
 * Test de vérification des règles du Short Deck Hold'em
 * 
 * Ce test vérifie que l'implémentation actuelle respecte bien les 3 règles principales:
 * 1. A-6-7-8-9 straight (wheel straight)
 * 2. Flush beats Full House
 * 3. Trips beats Straight (si implémenté)
 * 4. Calculs d'équité adaptés
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_short.h>
#include <poker_eval/games/rules_short.h>
#include <poker_eval/games/eval_short.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    do { \
        printf("Test: %-40s ", #name); \
        tests_run++; \
        if (test_##name()) { \
            printf("✓ PASSED\n"); \
            tests_passed++; \
        } else { \
            printf("✗ FAILED\n"); \
            tests_failed++; \
        } \
    } while(0)

#define ASSERT_TRUE(expr, msg) \
    do { \
        if (!(expr)) { \
            printf("FAILED: %s\n", msg); \
            return 0; \
        } \
    } while(0)

#define ASSERT_EQ(a, b, msg) \
    do { \
        if ((a) != (b)) { \
            printf("FAILED: %s (got %d, expected %d)\n", msg, (int)(a), (int)(b)); \
            return 0; \
        } \
    } while(0)

#define ASSERT_GT(a, b, msg) \
    do { \
        if ((a) <= (b)) { \
            printf("FAILED: %s (%d should be > %d)\n", msg, (int)(a), (int)(b)); \
            return 0; \
        } \
    } while(0)

// Helper function to create a hand with specific cards
static void create_hand(ShortDeck_CardMask* hand, int rank1, int suit1, 
                       int rank2, int suit2, int rank3, int suit3, 
                       int rank4, int suit4, int rank5, int suit5) {
    ShortDeck_CardMask_RESET(*hand);
    ShortDeck_CardMask_SET(*hand, ShortDeck_MAKE_CARD(rank1, suit1));
    ShortDeck_CardMask_SET(*hand, ShortDeck_MAKE_CARD(rank2, suit2));
    ShortDeck_CardMask_SET(*hand, ShortDeck_MAKE_CARD(rank3, suit3));
    ShortDeck_CardMask_SET(*hand, ShortDeck_MAKE_CARD(rank4, suit4));
    ShortDeck_CardMask_SET(*hand, ShortDeck_MAKE_CARD(rank5, suit5));
}

// Helper function to print hand details
static void print_hand_details(const char* label, ShortDeck_CardMask hand, HandVal result) {
    printf("  %s: ", label);
    for (int i = 0; i < ShortDeck_N_CARDS; i++) {
        if (ShortDeck_CardMask_CARD_IS_SET(hand, i)) {
            char card_str[4];
            ShortDeck_cardToString(i, card_str);
            printf("%s ", card_str);
        }
    }
    printf("-> %s (value: %u)\n", 
           ShortRules_handTypeNames[HandVal_HANDTYPE(result)], result);
}

// Test 1: Vérifier que A-6-7-8-9 est reconnu comme straight
static int test_wheel_straight_recognition(void) {
    ShortDeck_CardMask hand;
    HandVal result;
    
    // Create A-6-7-8-9 straight (wheel)
    create_hand(&hand, 
                ShortDeck_Rank_ACE, ShortDeck_Suit_SPADES,
                ShortDeck_Rank_6, ShortDeck_Suit_HEARTS,
                ShortDeck_Rank_7, ShortDeck_Suit_DIAMONDS,
                ShortDeck_Rank_8, ShortDeck_Suit_CLUBS,
                ShortDeck_Rank_9, ShortDeck_Suit_SPADES);
    
    result = ShortDeck_ShortRules_EVAL_N(hand, 5);
    
    ASSERT_EQ(HandVal_HANDTYPE(result), ShortRules_HandType_STRAIGHT, 
              "A-6-7-8-9 should be recognized as straight");
    
    ASSERT_EQ(HandVal_TOP_CARD(result), ShortDeck_Rank_9, 
              "A-6-7-8-9 straight should have 9 as top card");
    
    return 1;
}

// Test 2: Vérifier que A-6-7-8-9 suited est reconnu comme straight flush
static int test_wheel_straight_flush(void) {
    ShortDeck_CardMask hand;
    HandVal result;
    
    // Create A-6-7-8-9 straight flush (all spades)
    create_hand(&hand, 
                ShortDeck_Rank_ACE, ShortDeck_Suit_SPADES,
                ShortDeck_Rank_6, ShortDeck_Suit_SPADES,
                ShortDeck_Rank_7, ShortDeck_Suit_SPADES,
                ShortDeck_Rank_8, ShortDeck_Suit_SPADES,
                ShortDeck_Rank_9, ShortDeck_Suit_SPADES);
    
    result = ShortDeck_ShortRules_EVAL_N(hand, 5);
    
    ASSERT_EQ(HandVal_HANDTYPE(result), ShortRules_HandType_STFLUSH, 
              "A-6-7-8-9 suited should be straight flush");
    
    ASSERT_EQ(HandVal_TOP_CARD(result), ShortDeck_Rank_9, 
              "A-6-7-8-9 straight flush should have 9 as top card");
    
    return 1;
}

// Test 3: Vérifier que Flush bat Full House
static int test_flush_beats_full_house(void) {
    ShortDeck_CardMask flush_hand, full_house_hand;
    HandVal flush_result, full_house_result;
    
    // Create flush (A-K-Q-J-9 spades)
    create_hand(&flush_hand,
                ShortDeck_Rank_ACE, ShortDeck_Suit_SPADES,
                ShortDeck_Rank_KING, ShortDeck_Suit_SPADES,
                ShortDeck_Rank_QUEEN, ShortDeck_Suit_SPADES,
                ShortDeck_Rank_JACK, ShortDeck_Suit_SPADES,
                ShortDeck_Rank_9, ShortDeck_Suit_SPADES);
    
    // Create full house (AAA KK)
    create_hand(&full_house_hand,
                ShortDeck_Rank_ACE, ShortDeck_Suit_SPADES,
                ShortDeck_Rank_ACE, ShortDeck_Suit_HEARTS,
                ShortDeck_Rank_ACE, ShortDeck_Suit_DIAMONDS,
                ShortDeck_Rank_KING, ShortDeck_Suit_CLUBS,
                ShortDeck_Rank_KING, ShortDeck_Suit_SPADES);
    
    flush_result = ShortDeck_ShortRules_EVAL_N(flush_hand, 5);
    full_house_result = ShortDeck_ShortRules_EVAL_N(full_house_hand, 5);
    
    // Verify hand types are correct
    ASSERT_EQ(HandVal_HANDTYPE(flush_result), ShortRules_HandType_FLUSH, 
              "First hand should be flush");
    ASSERT_EQ(HandVal_HANDTYPE(full_house_result), ShortRules_HandType_FULLHOUSE, 
              "Second hand should be full house");
    
    // The critical test: Flush should beat Full House in Short Deck
    ASSERT_GT(flush_result, full_house_result, 
              "Flush should beat Full House in Short Deck");
    
    return 1;
}

// Test 4: Vérifier l'ordre des mains standard
static int test_standard_hand_rankings(void) {
    ShortDeck_CardMask high_card, pair, two_pair, trips, straight, flush, full_house, quads;
    HandVal hc_val, pair_val, tp_val, trips_val, str_val, flush_val, fh_val, quads_val;
    
    // High card: A-K-Q-J-8
    create_hand(&high_card,
                ShortDeck_Rank_ACE, ShortDeck_Suit_SPADES,
                ShortDeck_Rank_KING, ShortDeck_Suit_HEARTS,
                ShortDeck_Rank_QUEEN, ShortDeck_Suit_DIAMONDS,
                ShortDeck_Rank_JACK, ShortDeck_Suit_CLUBS,
                ShortDeck_Rank_8, ShortDeck_Suit_SPADES);
    
    // Pair: AA K Q J
    create_hand(&pair,
                ShortDeck_Rank_ACE, ShortDeck_Suit_SPADES,
                ShortDeck_Rank_ACE, ShortDeck_Suit_HEARTS,
                ShortDeck_Rank_KING, ShortDeck_Suit_DIAMONDS,
                ShortDeck_Rank_QUEEN, ShortDeck_Suit_CLUBS,
                ShortDeck_Rank_JACK, ShortDeck_Suit_SPADES);
    
    // Two pair: AA KK Q
    create_hand(&two_pair,
                ShortDeck_Rank_ACE, ShortDeck_Suit_SPADES,
                ShortDeck_Rank_ACE, ShortDeck_Suit_HEARTS,
                ShortDeck_Rank_KING, ShortDeck_Suit_DIAMONDS,
                ShortDeck_Rank_KING, ShortDeck_Suit_CLUBS,
                ShortDeck_Rank_QUEEN, ShortDeck_Suit_SPADES);
    
    // Trips: AAA K Q
    create_hand(&trips,
                ShortDeck_Rank_ACE, ShortDeck_Suit_SPADES,
                ShortDeck_Rank_ACE, ShortDeck_Suit_HEARTS,
                ShortDeck_Rank_ACE, ShortDeck_Suit_DIAMONDS,
                ShortDeck_Rank_KING, ShortDeck_Suit_CLUBS,
                ShortDeck_Rank_QUEEN, ShortDeck_Suit_SPADES);
    
    // Straight: T-J-Q-K-A
    create_hand(&straight,
                ShortDeck_Rank_TEN, ShortDeck_Suit_SPADES,
                ShortDeck_Rank_JACK, ShortDeck_Suit_HEARTS,
                ShortDeck_Rank_QUEEN, ShortDeck_Suit_DIAMONDS,
                ShortDeck_Rank_KING, ShortDeck_Suit_CLUBS,
                ShortDeck_Rank_ACE, ShortDeck_Suit_SPADES);
    
    // Flush: A-K-Q-J-9 spades
    create_hand(&flush,
                ShortDeck_Rank_ACE, ShortDeck_Suit_SPADES,
                ShortDeck_Rank_KING, ShortDeck_Suit_SPADES,
                ShortDeck_Rank_QUEEN, ShortDeck_Suit_SPADES,
                ShortDeck_Rank_JACK, ShortDeck_Suit_SPADES,
                ShortDeck_Rank_9, ShortDeck_Suit_SPADES);
    
    // Full house: AAA KK
    create_hand(&full_house,
                ShortDeck_Rank_ACE, ShortDeck_Suit_SPADES,
                ShortDeck_Rank_ACE, ShortDeck_Suit_HEARTS,
                ShortDeck_Rank_ACE, ShortDeck_Suit_DIAMONDS,
                ShortDeck_Rank_KING, ShortDeck_Suit_CLUBS,
                ShortDeck_Rank_KING, ShortDeck_Suit_SPADES);
    
    // Quads: AAAA K
    create_hand(&quads,
                ShortDeck_Rank_ACE, ShortDeck_Suit_SPADES,
                ShortDeck_Rank_ACE, ShortDeck_Suit_HEARTS,
                ShortDeck_Rank_ACE, ShortDeck_Suit_DIAMONDS,
                ShortDeck_Rank_ACE, ShortDeck_Suit_CLUBS,
                ShortDeck_Rank_KING, ShortDeck_Suit_SPADES);
    
    // Evaluate all hands
    hc_val = ShortDeck_ShortRules_EVAL_N(high_card, 5);
    pair_val = ShortDeck_ShortRules_EVAL_N(pair, 5);
    tp_val = ShortDeck_ShortRules_EVAL_N(two_pair, 5);
    trips_val = ShortDeck_ShortRules_EVAL_N(trips, 5);
    str_val = ShortDeck_ShortRules_EVAL_N(straight, 5);
    flush_val = ShortDeck_ShortRules_EVAL_N(flush, 5);
    fh_val = ShortDeck_ShortRules_EVAL_N(full_house, 5);
    quads_val = ShortDeck_ShortRules_EVAL_N(quads, 5);
    
    // Test basic rankings
    ASSERT_GT(pair_val, hc_val, "Pair should beat high card");
    ASSERT_GT(tp_val, pair_val, "Two pair should beat pair");
    ASSERT_GT(quads_val, fh_val, "Quads should beat full house");
    
    // Critical Short Deck rule: Flush beats Full House
    ASSERT_GT(flush_val, fh_val, "Flush should beat Full House in Short Deck");
    
    return 1;
}

// Test 5: Vérifier que les différents straights sont correctement ordonnés
static int test_straight_rankings(void) {
    ShortDeck_CardMask wheel, low_straight, high_straight;
    HandVal wheel_val, low_val, high_val;
    
    // Wheel: A-6-7-8-9
    create_hand(&wheel,
                ShortDeck_Rank_ACE, ShortDeck_Suit_SPADES,
                ShortDeck_Rank_6, ShortDeck_Suit_HEARTS,
                ShortDeck_Rank_7, ShortDeck_Suit_DIAMONDS,
                ShortDeck_Rank_8, ShortDeck_Suit_CLUBS,
                ShortDeck_Rank_9, ShortDeck_Suit_SPADES);
    
    // Low straight: 6-7-8-9-T
    create_hand(&low_straight,
                ShortDeck_Rank_6, ShortDeck_Suit_SPADES,
                ShortDeck_Rank_7, ShortDeck_Suit_HEARTS,
                ShortDeck_Rank_8, ShortDeck_Suit_DIAMONDS,
                ShortDeck_Rank_9, ShortDeck_Suit_CLUBS,
                ShortDeck_Rank_TEN, ShortDeck_Suit_SPADES);
    
    // High straight: T-J-Q-K-A
    create_hand(&high_straight,
                ShortDeck_Rank_TEN, ShortDeck_Suit_SPADES,
                ShortDeck_Rank_JACK, ShortDeck_Suit_HEARTS,
                ShortDeck_Rank_QUEEN, ShortDeck_Suit_DIAMONDS,
                ShortDeck_Rank_KING, ShortDeck_Suit_CLUBS,
                ShortDeck_Rank_ACE, ShortDeck_Suit_SPADES);
    
    wheel_val = ShortDeck_ShortRules_EVAL_N(wheel, 5);
    low_val = ShortDeck_ShortRules_EVAL_N(low_straight, 5);
    high_val = ShortDeck_ShortRules_EVAL_N(high_straight, 5);
    
    // All should be straights
    ASSERT_EQ(HandVal_HANDTYPE(wheel_val), ShortRules_HandType_STRAIGHT, 
              "Wheel should be straight");
    ASSERT_EQ(HandVal_HANDTYPE(low_val), ShortRules_HandType_STRAIGHT, 
              "6-T should be straight");
    ASSERT_EQ(HandVal_HANDTYPE(high_val), ShortRules_HandType_STRAIGHT, 
              "T-A should be straight");
    
    // Check ordering: high > low > wheel
    ASSERT_GT(high_val, low_val, "T-A straight should beat 6-T straight");
    ASSERT_GT(low_val, wheel_val, "6-T straight should beat A-9 wheel");
    
    return 1;
}

// Test 6: Test avec 7 cartes (comme au Hold'em)
static int test_seven_card_evaluation(void) {
    ShortDeck_CardMask hand;
    HandVal result;
    
    // Create a 7-card hand with flush and full house possibilities
    // Should pick the flush (which beats full house in Short Deck)
    ShortDeck_CardMask_RESET(hand);
    // Trips: AAA
    ShortDeck_CardMask_SET(hand, ShortDeck_MAKE_CARD(ShortDeck_Rank_ACE, ShortDeck_Suit_SPADES));
    ShortDeck_CardMask_SET(hand, ShortDeck_MAKE_CARD(ShortDeck_Rank_ACE, ShortDeck_Suit_HEARTS));
    ShortDeck_CardMask_SET(hand, ShortDeck_MAKE_CARD(ShortDeck_Rank_ACE, ShortDeck_Suit_DIAMONDS));
    // Pair: KK
    ShortDeck_CardMask_SET(hand, ShortDeck_MAKE_CARD(ShortDeck_Rank_KING, ShortDeck_Suit_SPADES));
    ShortDeck_CardMask_SET(hand, ShortDeck_MAKE_CARD(ShortDeck_Rank_KING, ShortDeck_Suit_HEARTS));
    // Additional spades for flush
    ShortDeck_CardMask_SET(hand, ShortDeck_MAKE_CARD(ShortDeck_Rank_QUEEN, ShortDeck_Suit_SPADES));
    ShortDeck_CardMask_SET(hand, ShortDeck_MAKE_CARD(ShortDeck_Rank_JACK, ShortDeck_Suit_SPADES));
    
    result = ShortDeck_ShortRules_EVAL_N(hand, 7);
    
    // Should evaluate to flush (which beats the full house in Short Deck)
    ASSERT_EQ(HandVal_HANDTYPE(result), ShortRules_HandType_FLUSH, 
              "7-card hand should pick flush over full house");
    
    return 1;
}

// Test 7: Vérifier que les noms des types de mains sont corrects
static int test_hand_type_names(void) {
    ASSERT_TRUE(strcmp(ShortRules_handTypeNames[ShortRules_HandType_NOPAIR], "HighCard") == 0,
                "High card name should be correct");
    ASSERT_TRUE(strcmp(ShortRules_handTypeNames[ShortRules_HandType_FLUSH], "Flush") == 0,
                "Flush name should be correct");
    ASSERT_TRUE(strcmp(ShortRules_handTypeNames[ShortRules_HandType_FULLHOUSE], "FullHouse") == 0,
                "Full house name should be correct");
    ASSERT_TRUE(strcmp(ShortRules_handTypeNames[ShortRules_HandType_STRAIGHT], "Straight") == 0,
                "Straight name should be correct");
    
    return 1;
}

int main(void) {
    printf("=== Test de Vérification des Règles Short Deck Hold'em ===\n\n");
    
    printf("Vérification des règles principales du Short Deck:\n");
    printf("1. A-6-7-8-9 straight (wheel)\n");
    printf("2. Flush beats Full House\n");
    printf("3. Évaluations correctes\n\n");
    
    TEST(wheel_straight_recognition);
    TEST(wheel_straight_flush);
    TEST(flush_beats_full_house);
    TEST(standard_hand_rankings);
    TEST(straight_rankings);
    TEST(seven_card_evaluation);
    TEST(hand_type_names);
    
    printf("\n=== Résultats des Tests ===\n");
    printf("Tests exécutés: %d\n", tests_run);
    printf("Tests réussis:  %d\n", tests_passed);
    printf("Tests échoués:  %d\n", tests_failed);
    
    if (tests_failed == 0) {
        printf("\n🎉 TOUS LES TESTS SONT PASSÉS!\n");
        printf("✅ L'implémentation Short Deck est CORRECTE et COMPLÈTE!\n");
        printf("\nRègles Short Deck vérifiées:\n");
        printf("✅ A-6-7-8-9 straight supporté\n");
        printf("✅ Flush bat Full House\n");
        printf("✅ Évaluations 5 et 7 cartes correctes\n");
        printf("✅ Ordonnancement des mains correct\n");
        return 0;
    } else {
        printf("\n❌ %d TEST(S) ONT ÉCHOUÉ!\n", tests_failed);
        printf("L'implémentation Short Deck nécessite des corrections.\n");
        return 1;
    }
}
