#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <poker_eval/range/StudRangeParser.h>

void test_combine_ranges_union() {
    printf("Test: SRP_CombineRanges - Union...\n");
    
    srp_range_t range1, range2, result;
    
    // Crée range1: AA, KK
    int result1 = SRP_ParseRange("AA, KK", SRP_VARIANT_STUD, NULL, &range1);
    assert(result1 == 1);
    assert(range1.count == 2);
    
    // Crée range2: QQ, JJ
    int result2 = SRP_ParseRange("QQ, JJ", SRP_VARIANT_STUD, NULL, &range2);
    assert(result2 == 1);
    assert(range2.count == 2);
    
    // Combine avec union
    int combine_result = SRP_CombineRanges(&range1, &range2, SRP_OP_UNION, &result);
    assert(combine_result == 1);
    assert(result.count == 4); // AA, KK, QQ, JJ
    
    SRP_FreeRange(&range1);
    SRP_FreeRange(&range2);
    SRP_FreeRange(&result);
    
    printf("✓ Union test passed\n");
}

void test_combine_ranges_subtract() {
    printf("Test: SRP_CombineRanges - Subtract...\n");
    
    srp_range_t range1, range2, result;
    
    // Crée range1: AA-TT (5 paires)
    int result1 = SRP_ParseRange("AA-TT", SRP_VARIANT_STUD, NULL, &range1);
    assert(result1 == 1);
    assert(range1.count == 5);
    
    // Crée range2: AA, KK (2 paires)
    int result2 = SRP_ParseRange("AA, KK", SRP_VARIANT_STUD, NULL, &range2);
    assert(result2 == 1);
    assert(range2.count == 2);
    
    // Soustraction: AA-TT minus AA,KK = QQ,JJ,TT
    int combine_result = SRP_CombineRanges(&range1, &range2, SRP_OP_SUBTRACT, &result);
    assert(combine_result == 1);
    assert(result.count == 3); // QQ, JJ, TT
    
    SRP_FreeRange(&range1);
    SRP_FreeRange(&range2);
    SRP_FreeRange(&result);
    
    printf("✓ Subtract test passed\n");
}

void test_combine_ranges_with_duplicates() {
    printf("Test: SRP_CombineRanges - Union with duplicates...\n");
    
    srp_range_t range1, range2, result;
    
    // Crée range1: AA, KK, QQ
    int result1 = SRP_ParseRange("AA, KK, QQ", SRP_VARIANT_STUD, NULL, &range1);
    assert(result1 == 1);
    assert(range1.count == 3);
    
    // Crée range2: KK, QQ, JJ (avec doublons)
    int result2 = SRP_ParseRange("KK, QQ, JJ", SRP_VARIANT_STUD, NULL, &range2);
    assert(result2 == 1);
    assert(range2.count == 3);
    
    // Union devrait éliminer les doublons
    int combine_result = SRP_CombineRanges(&range1, &range2, SRP_OP_UNION, &result);
    assert(combine_result == 1);
    assert(result.count == 4); // AA, KK, QQ, JJ (pas de doublons)
    
    SRP_FreeRange(&range1);
    SRP_FreeRange(&range2);
    SRP_FreeRange(&result);
    
    printf("✓ Union with duplicates test passed\n");
}

void test_expand_hand_with_color_monotone() {
    printf("Test: SRP_ExpandHandWithColor - Monotone...\n");
    
    srp_range_t result;
    result.hands = NULL;
    result.count = 0;
    result.capacity = 0;
    
    int ranks[2] = {12, 12}; // AA
    int expand_result = SRP_ExpandHandWithColor(ranks, 2, SRP_COLOR_MONOTONE, NULL, &result);
    
    assert(expand_result == 1);
    assert(result.count == 4); // AAhh, AAcc, AAdd, AAss
    
    SRP_FreeRange(&result);
    
    printf("✓ Expand monotone test passed\n");
}

void test_expand_hand_with_color_rainbow() {
    printf("Test: SRP_ExpandHandWithColor - Rainbow...\n");
    
    srp_range_t result;
    result.hands = NULL;
    result.count = 0;
    result.capacity = 0;
    
    int ranks[3] = {12, 11, 10}; // AKQ
    int expand_result = SRP_ExpandHandWithColor(ranks, 3, SRP_COLOR_RAINBOW, NULL, &result);
    
    assert(expand_result == 1);
    assert(result.count == 24); // 4*3*2 = 24 combinaisons rainbow
    
    SRP_FreeRange(&result);
    
    printf("✓ Expand rainbow test passed\n");
}

void test_expand_hand_with_color_bicolor() {
    printf("Test: SRP_ExpandHandWithColor - Bicolor...\n");
    
    srp_range_t result;
    result.hands = NULL;
    result.count = 0;
    result.capacity = 0;
    
    int ranks[3] = {12, 11, 10}; // AKQ
    int expand_result = SRP_ExpandHandWithColor(ranks, 3, SRP_COLOR_BICOLOR, NULL, &result);
    
    assert(expand_result == 1);
    // Bicolor: 2 cartes d'une couleur, 1 d'une autre
    // 4 couleurs principales * 3 autres couleurs * 3 positions = 36
    assert(result.count == 36);
    
    SRP_FreeRange(&result);
    
    printf("✓ Expand bicolor test passed\n");
}

void test_improved_top_percentage() {
    printf("Test: Improved SRP_GetTopPercentage...\n");
    
    srp_range_t result;
    
    // Test avec 10%
    int get_result = SRP_GetTopPercentage(0.1f, SRP_VARIANT_STUD, NULL, &result);
    assert(get_result == 1);
    assert(result.is_percentage == true);
    assert(result.percentage_used == 0.1f);
    assert(result.count > 0);
    
    printf("Top 10%% contains %zu hands\n", result.count);
    
    // Vérifie que les mains contiennent des paires premium (AA, KK, etc.)
    bool found_premium_pair = false;
    for (size_t i = 0; i < result.count; i++) {
        if (result.hands[i].card_count == 2 &&
            result.hands[i].cards[0] == result.hands[i].cards[1] &&
            result.hands[i].cards[0] >= 10) { // J, Q, K, A
            found_premium_pair = true;
            break;
        }
    }
    assert(found_premium_pair); // Devrait contenir des paires premium
    
    // Vérifie que les trips sont aussi présents (ajoutés après les paires)
    bool found_trips = false;
    for (size_t i = 0; i < result.count; i++) {
        if (result.hands[i].card_count == 3 &&
            result.hands[i].cards[0] == result.hands[i].cards[1] &&
            result.hands[i].cards[1] == result.hands[i].cards[2]) {
            found_trips = true;
            break;
        }
    }
    assert(found_trips); // Devrait contenir des trips
    
    SRP_FreeRange(&result);
    
    printf("✓ Improved top percentage test passed\n");
}

void test_complex_operations() {
    printf("Test: Complex operations...\n");
    
    srp_range_t premium_pairs, trips, combined, final_result;
    
    // Crée une range de paires premium
    int result1 = SRP_ParseRange("AA-QQ", SRP_VARIANT_STUD, NULL, &premium_pairs);
    assert(result1 == 1);
    
    // Crée une range de trips
    int result2 = SRP_ParseRange("AAA, KKK", SRP_VARIANT_STUD, NULL, &trips);
    assert(result2 == 1);
    
    // Combine les deux
    int combine1 = SRP_CombineRanges(&premium_pairs, &trips, SRP_OP_UNION, &combined);
    assert(combine1 == 1);
    
    // Soustrait une paire spécifique
    srp_range_t to_remove;
    int result3 = SRP_ParseRange("QQ", SRP_VARIANT_STUD, NULL, &to_remove);
    assert(result3 == 1);
    
    int combine2 = SRP_CombineRanges(&combined, &to_remove, SRP_OP_SUBTRACT, &final_result);
    assert(combine2 == 1);
    
    // Vérifie le résultat final
    assert(final_result.count == 4); // AA, KK, AAA, KKK (sans QQ)
    
    SRP_FreeRange(&premium_pairs);
    SRP_FreeRange(&trips);
    SRP_FreeRange(&combined);
    SRP_FreeRange(&to_remove);
    SRP_FreeRange(&final_result);
    
    printf("✓ Complex operations test passed\n");
}

void test_range_to_string_advanced() {
    printf("Test: Advanced SRP_RangeToString...\n");
    
    srp_range_t range1, range2, combined;
    
    // Crée des ranges avec contraintes de couleur
    int result1 = SRP_ParseRange("A23xxx, KQJxxy", SRP_VARIANT_STUD, NULL, &range1);
    assert(result1 == 1);
    
    int result2 = SRP_ParseRange("AA, KK", SRP_VARIANT_STUD, NULL, &range2);
    assert(result2 == 1);
    
    // Combine
    int combine_result = SRP_CombineRanges(&range1, &range2, SRP_OP_UNION, &combined);
    assert(combine_result == 1);
    
    // Convertit en string
    char buffer[512];
    int len = SRP_RangeToString(&combined, buffer, sizeof(buffer));
    assert(len > 0);
    
    printf("Combined range string: %s\n", buffer);
    
    // Vérifie que la string contient les éléments attendus
    assert(strstr(buffer, "A23xxx") != NULL);
    assert(strstr(buffer, "KQJxxy") != NULL);
    assert(strstr(buffer, "AA") != NULL);
    assert(strstr(buffer, "KK") != NULL);
    
    SRP_FreeRange(&range1);
    SRP_FreeRange(&range2);
    SRP_FreeRange(&combined);
    
    printf("✓ Advanced range to string test passed\n");
}

int main() {
    printf("=== StudRangeParser Advanced Features Tests ===\n\n");
    
    test_combine_ranges_union();
    test_combine_ranges_subtract();
    test_combine_ranges_with_duplicates();
    test_expand_hand_with_color_monotone();
    test_expand_hand_with_color_rainbow();
    test_expand_hand_with_color_bicolor();
    test_improved_top_percentage();
    test_complex_operations();
    test_range_to_string_advanced();
    
    printf("\n=== All advanced features tests passed! ===\n");
    return 0;
}