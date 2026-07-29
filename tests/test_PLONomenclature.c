#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <poker_eval/distributions/plo_nomenclature.h>
#include <poker_eval/deck/deck_std.h>

// Helper function to count set bits (needed for tests)
static int count_set_bits(StdDeck_CardMask mask) {
    int count = 0;
    for (int i = 0; i < StdDeck_N_CARDS; ++i) {
        if (StdDeck_CardMask_CARD_IS_SET(mask, i)) {
            count++;
        }
    }
    return count;
}

// Macro pour les tests avec affichage
#define TEST_ASSERT(expr, msg) \
    if (!(expr)) { \
        printf("[FAIL] %s\n", msg); \
        return 1; \
    } else { \
        printf("[PASS] %s\n", msg); \
    }

// Test du parsing de mains spécifiques
static int test_parse_specific_hands(void) {
    printf("\n=== Test Parse Specific Hands ===\n");
    PLOHand hand;
    
    // Test 1: Main spécifique valide
    TEST_ASSERT(PLO_ParseHand("AsKdQhJc", &hand) == 1, "Parse main spécifique AsKdQhJc");
    TEST_ASSERT(hand.category != PLO_CAT_INVALID, "Catégorie valide pour AsKdQhJc");
    TEST_ASSERT(count_set_bits(hand.cards) == 4, "4 cartes dans la main");
    
    // Test 2: Main invalide (carte dupliquée)
    TEST_ASSERT(PLO_ParseHand("AsAsQhJc", &hand) == 0, "Rejet main avec carte dupliquée");
    
    return 0;
}

// Test du parsing de patterns avec placeholders
static int test_parse_patterns(void) {
    printf("\n=== Test Parse Patterns ===\n");
    PLOHand hand;
    
    // Test 1: AAxxds
    TEST_ASSERT(PLO_ParseHand("AAxxds", &hand) == 1, "Parse pattern AAxxds");
    TEST_ASSERT(hand.category == PLO_CAT_AA_DS, "Catégorie AA double-suited");
    TEST_ASSERT(hand.suitedness == PLO_SUIT_DOUBLE, "Suitedness double");
    TEST_ASSERT(strcmp(hand.notation, "AAxxds") == 0, "Notation conservée");
    
    // Test 2: JT98r
    TEST_ASSERT(PLO_ParseHand("JT98r", &hand) == 1, "Parse pattern JT98r");
    TEST_ASSERT(hand.suitedness == PLO_SUIT_RAINBOW, "Suitedness rainbow");
    TEST_ASSERT(hand.connectivity == PLO_CONN_RUNDOWN, "Connectivity rundown");
    
    // Test 3: KKxxss
    TEST_ASSERT(PLO_ParseHand("KKxxss", &hand) == 1, "Parse pattern KKxxss");
    TEST_ASSERT(hand.suitedness == PLO_SUIT_SINGLE, "Suitedness single");
    TEST_ASSERT(hand.pair_count >= 1, "Au moins une paire");
    
    // Test 4: xxxxds (toutes cartes placeholders)
    TEST_ASSERT(PLO_ParseHand("xxxxds", &hand) == 1, "Parse pattern xxxxds");
    TEST_ASSERT(hand.suitedness == PLO_SUIT_DOUBLE, "Suitedness double pour xxxxds");
    
    // Test 5: Pattern impossible (AAAAr - 4 As rainbow impossible)
    TEST_ASSERT(PLO_ParseHand("AAAAr", &hand) == 0, "Rejet pattern impossible AAAAr");
    
    return 0;
}

// Test de la catégorisation
static int test_categorization(void) {
    printf("\n=== Test Categorization ===\n");
    PLOHand hand;
    
    // Test catégories Aces
    TEST_ASSERT(PLO_ParseHand("AAKQds", &hand) == 1 && hand.category == PLO_CAT_AA_DS, 
                "AAKQds -> AA double-suited");
    TEST_ASSERT(PLO_ParseHand("AAKQss", &hand) == 1 && hand.category == PLO_CAT_AA_SS, 
                "AAKQss -> AA single-suited");
    TEST_ASSERT(PLO_ParseHand("AAKQr", &hand) == 1 && hand.category == PLO_CAT_AA_RB, 
                "AAKQr -> AA rainbow");
    
    // Test catégories Trips
    TEST_ASSERT(PLO_ParseHand("KKKQr", &hand) == 1 && hand.category == PLO_CAT_TRIPS_RB, 
                "KKKQr -> Trips rainbow");
    
    // Test catégories Broadway
    TEST_ASSERT(PLO_ParseHand("AKQJds", &hand) == 1 && hand.category == PLO_CAT_BROADWAY_DS, 
                "AKQJds -> Broadway double-suited");
    TEST_ASSERT(PLO_ParseHand("KQJTr", &hand) == 1 && hand.category == PLO_CAT_BROADWAY_RB, 
                "KQJTr -> Broadway rainbow");
    
    return 0;
}

// Test de la correspondance de patterns
static int test_pattern_matching(void) {
    printf("\n=== Test Pattern Matching ===\n");
    PLOHand hand;
    
    // Test 1: AAKQds matches AAxxds
    PLO_ParseHand("AAKQds", &hand);
    TEST_ASSERT(PLO_MatchesPattern(&hand, "AAxxds") == 1, "AAKQds matches AAxxds");
    TEST_ASSERT(PLO_MatchesPattern(&hand, "AAxxss") == 0, "AAKQds doesn't match AAxxss");
    TEST_ASSERT(PLO_MatchesPattern(&hand, "KKxxds") == 0, "AAKQds doesn't match KKxxds");
    
    // Test 2: JT98r matches patterns
    PLO_ParseHand("JT98r", &hand);
    TEST_ASSERT(PLO_MatchesPattern(&hand, "JT98r") == 1, "JT98r matches JT98r");
    TEST_ASSERT(PLO_MatchesPattern(&hand, "xxxxr") == 1, "JT98r matches xxxxr");
    TEST_ASSERT(PLO_MatchesPattern(&hand, "JTxxr") == 1, "JT98r matches JTxxr");
    TEST_ASSERT(PLO_MatchesPattern(&hand, "JT98ds") == 0, "JT98r doesn't match JT98ds");
    
    // Test 3: Specific hand matches patterns
    PLO_ParseHand("AsAhKsKh", &hand);  // This is actually double-suited
    TEST_ASSERT(PLO_MatchesPattern(&hand, "AAKKds") == 1, "AsAhKsKh matches AAKKds");
    TEST_ASSERT(PLO_MatchesPattern(&hand, "AAxxds") == 1, "AsAhKsKh matches AAxxds");

    // Invalid lengths must be rejected before reading pattern ranks or suffix.
    TEST_ASSERT(PLO_MatchesPattern(&hand, "") == 0, "Empty pattern is rejected");
    TEST_ASSERT(PLO_MatchesPattern(&hand, "AAK") == 0, "Short pattern is rejected");
    TEST_ASSERT(PLO_MatchesPattern(&hand, "AAKKdss") == 0, "Long pattern is rejected");
    
    return 0;
}

// Test de la connectivité
static int test_connectivity(void) {
    printf("\n=== Test Connectivity ===\n");
    PLOHand hand;
    
    // Rundown
    TEST_ASSERT(PLO_ParseHand("JT98r", &hand) == 1 && hand.connectivity == PLO_CONN_RUNDOWN, 
                "JT98 -> Rundown");
    TEST_ASSERT(PLO_ParseHand("5432r", &hand) == 1 && hand.connectivity == PLO_CONN_RUNDOWN, 
                "5432 -> Rundown");
    
    // 1-gap
    TEST_ASSERT(PLO_ParseHand("JT97r", &hand) == 1 && hand.connectivity == PLO_CONN_1GAP, 
                "JT97 -> 1-gap");
    
    // Wheel
    TEST_ASSERT(PLO_ParseHand("A234r", &hand) == 1 && hand.connectivity == PLO_CONN_RUNDOWN, 
                "A234 -> Rundown (wheel)");
    
    return 0;
}

// Test des fonctions utilitaires
static int test_utilities(void) {
    printf("\n=== Test Utilities ===\n");
    
    // Test category names
    TEST_ASSERT(strcmp(PLO_CategoryName(PLO_CAT_AA_DS), "Aces Double-Suited") == 0, 
                "Category name for AA DS");
    TEST_ASSERT(strcmp(PLO_CategoryName(PLO_CAT_BROADWAY_SS), "3+ Broadway Single-Suited") == 0, 
                "Category name for Broadway SS");
    
    // Test suitedness suffix
    TEST_ASSERT(strcmp(PLO_SuitednessSuffix(PLO_SUIT_DOUBLE), "ds") == 0, "Suffix for double-suited");
    TEST_ASSERT(strcmp(PLO_SuitednessSuffix(PLO_SUIT_SINGLE), "ss") == 0, "Suffix for single-suited");
    TEST_ASSERT(strcmp(PLO_SuitednessSuffix(PLO_SUIT_RAINBOW), "r") == 0, "Suffix for rainbow");
    
    // Test category percentages
    float pct = PLO_CategoryPercentage(PLO_CAT_AA_DS);
    TEST_ASSERT(pct > 0.0f && pct < 1.0f, "AA DS percentage in valid range");
    
    return 0;
}

int main(void) {
    int failed = 0;
    
    printf("==== PLO Nomenclature Tests ====\n");
    
    failed |= test_parse_specific_hands();
    failed |= test_parse_patterns();
    failed |= test_categorization();
    failed |= test_pattern_matching();
    failed |= test_connectivity();
    failed |= test_utilities();
    
    printf("\n==== Summary ====\n");
    if (!failed) {
        printf("[SUCCESS] All tests passed!\n");
    } else {
        printf("[FAILURE] Some tests failed!\n");
    }
    
    return failed;
}
