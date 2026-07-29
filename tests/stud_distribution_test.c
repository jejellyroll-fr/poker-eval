#include <stdio.h>
#include <string.h>
#include <stdlib.h> // For qsort, calloc
#include <assert.h>
#include <stdbool.h>

#include <poker_eval/distributions/stud_distributions.h>
#include <poker_eval/deck/deck_std.h> // For StdDeck_CardMask, StdDeck.cardToString, STD_DECK_N_CARDS etc.

// StdDeck is already declared in deck_std.h

// --- Copied/Re-implemented Helper Functions ---

static void print_card_mask_ln(StdDeck_CardMask mask) {
    char card_str_buffer[4]; 
    bool first_card = true;
    for (int card_idx = 0; card_idx < StdDeck_N_CARDS; ++card_idx) {
        if (StdDeck_CardMask_CARD_IS_SET(mask, card_idx)) {
            if (!first_card) {
                printf(" ");
            }
            StdDeck.cardToString(card_idx, card_str_buffer);
            printf("%s", card_str_buffer);
            first_card = false;
        }
    }
    printf("\n");
}

static bool masks_are_equal(StdDeck_CardMask m1, StdDeck_CardMask m2) {
    return StdDeck_CardMask_EQUAL(m1, m2);
}

static void card_mask_to_string_static(StdDeck_CardMask mask, char* out_buffer, int buffer_size) {
    if (!out_buffer || buffer_size == 0) return;
    out_buffer[0] = '\0';
    int current_len = 0;
    char card_s[4]; 
    bool first = true;

    for (int i = 0; i < StdDeck_N_CARDS; i++) {
        if (StdDeck_CardMask_CARD_IS_SET(mask, i)) {
            StdDeck.cardToString(i, card_s);
            int card_s_len = (int)strlen(card_s);
            int space_len = first ? 0 : 1;

            if (current_len + card_s_len + space_len < buffer_size -1) {
                if (!first) {
                    strcat(out_buffer, " ");
                    current_len++;
                }
                strcat(out_buffer, card_s);
                current_len += card_s_len;
                first = false;
            } else {
                strcat(out_buffer, "..."); 
                break;
            }
        }
    }
}

static void string_to_card_mask(const char* cards_str, StdDeck_CardMask* mask) {
    StdDeck_CardMask_RESET(*mask);
    if (!cards_str || strlen(cards_str) == 0) {
        return;
    }
    char card_text_buffer[3]; 
    card_text_buffer[2] = '\0';
    for (size_t i = 0; i < strlen(cards_str); i += 2) {
        if (i + 1 < strlen(cards_str)) {
            card_text_buffer[0] = cards_str[i];
            card_text_buffer[1] = cards_str[i+1];
            int card_idx = -1;
            if (StdDeck.stringToCard(card_text_buffer, &card_idx)) {
                 StdDeck_CardMask_SET(*mask, card_idx);
            } else {
                fprintf(stderr, "Warning: Could not parse card string '%s' in string_to_card_mask.\n", card_text_buffer);
            }
        } else {
            fprintf(stderr, "Warning: Trailing character in card string '%s'.\n", cards_str);
            break; 
        }
    }
}

// Forward declaration
static void run_stud_instantiate_test(const char* test_name, 
                               const char* hand_text, 
                               int game_total_cards, 
                               const char* dead_cards_str, 
                               int expected_combo_count, 
                               bool check_hands_exact, 
                               int num_expected_hands_in_array,
                               StdDeck_CardMask* expected_hands_masks_arr);

// --- Test Runner for StudHand_Parse ---

static void run_stud_parse_test(const char* test_name, 
                         const char* hand_text, 
                         int game_total_cards, 
                         bool expect_success, 
                         int expected_pattern_cards, 
                         int expected_known_cards, 
                         int expected_wildcards_in_pattern, 
                         int expected_down_cards, 
                         int expected_up_cards, 
                         StudCardPattern* expected_first_few_patterns, 
                         int num_first_few_to_check) {

    printf("Test Parse: %s (Hand: \"%s\", GameCards: %d)\n", test_name, hand_text, game_total_cards);
    StudHandQuery query;
    int parse_result = StudHand_Parse(hand_text, game_total_cards, &query);
    (void)parse_result; // Suppress unused warning

    assert((parse_result == 1) == expect_success);

    if (expect_success) {
        assert(query.game_total_cards == game_total_cards);
        assert(query.num_pattern_cards == expected_pattern_cards);
        assert(query.num_known_cards == expected_known_cards);
        assert(query.num_wildcards_in_pattern == expected_wildcards_in_pattern);
        assert(query.num_down_cards_specified == expected_down_cards);
        assert(query.num_up_cards_specified == expected_up_cards);

        for (int i = 0; i < num_first_few_to_check; ++i) {
            assert(query.patterns[i].rank == expected_first_few_patterns[i].rank);
            assert(query.patterns[i].suit == expected_first_few_patterns[i].suit);
            assert(query.patterns[i].is_up_card == expected_first_few_patterns[i].is_up_card);
        }
        
        printf("  PASS. Patterns: %d, Known: %d, Wild: %d, Down: %d, Up: %d\n", 
               query.num_pattern_cards, query.num_known_cards, query.num_wildcards_in_pattern,
               query.num_down_cards_specified, query.num_up_cards_specified);
    } else {
        printf("  PASS (expected failure).\n");
    }
    printf("---\n");
}


int main(void) {
    // StdDeck_Initialize();
    printf("Running StudHand_Parse tests...\n\n");

    // Test 1: Specific 7-card hand
    // Ranks: A=12, K=11, Q=10, J=9, T=8, 9=7
    // Suits: c=2, d=1, h=0, s=3
    StudCardPattern pats1[] = {
        {12, StdDeck_Suit_SPADES,   false}, // As (in hole)
        {11, StdDeck_Suit_DIAMONDS, false}, // Kd (in hole)
        {12, StdDeck_Suit_CLUBS,    false}, // Ac (in hole)
        {10, StdDeck_Suit_HEARTS,   true},  // Qh (up)
        {9,  StdDeck_Suit_HEARTS,   true},  // Jh (up)
        {8,  StdDeck_Suit_HEARTS,   true},  // Th (up)
        {7,  StdDeck_Suit_SPADES,   true}   // 9s (up)
    };
    run_stud_parse_test("Specific 7-card", "(AsKdAc)QhJhTh9s", 7, true, 7, 7, 0, 3, 4, pats1, 7);

    // Test 2: 3rd street specific (AA)K for 7-card game
    StudCardPattern pats2[] = {
        {12, StdDeck_Suit_SPADES,   false}, // As (in hole)
        {12, StdDeck_Suit_DIAMONDS, false}, // Ad (in hole)
        {11, StdDeck_Suit_CLUBS,    true}   // Kc (up)
    };
    run_stud_parse_test("3rd Street (AA)K", "(AsAd)Kc", 7, true, 3, 3, 0, 2, 1, pats2, 3);

    // Test 3: Partial with wildcards (AhKh)QhJx x for 7-card game
    // "Jx" = Rank J, Suit Wildcard. "x" = Full Wildcard.
    // Known: Ah, Kh, Qh (specific), Jx (rank known, suit wildcard) = 4 known
    // Wild: x (full wildcard) = 1 wild
    // Total patterns: 5
    StudCardPattern pats3[] = {
        {12, StdDeck_Suit_HEARTS,   false}, // Ah
        {11, StdDeck_Suit_HEARTS,   false}, // Kh
        {10, StdDeck_Suit_HEARTS,   true},  // Qh
        {9,  WILDCARD_CARD_VAL,     true},  // Jx (Rank J, Suit Wildcard)
        {WILDCARD_CARD_VAL, WILDCARD_CARD_VAL, true}   // x (Full Wildcard Up)
    };
    run_stud_parse_test("Partial (AhKh)QhJx x", "(AhKh)QhJx x", 7, true, 5, 4, 1, 2, 3, pats3, 5);

    // Test 4: Invalid - unmatched parenthesis
    run_stud_parse_test("Unmatched Paren", "(AsKdQc", 7, false, 0,0,0,0,0, NULL, 0);

    // Test 5: All up cards, specific
    StudCardPattern pats5[] = {
        {12, StdDeck_Suit_SPADES,   true}, // As (up)
        {11, StdDeck_Suit_DIAMONDS, true}, // Kd (up)
        {10, StdDeck_Suit_CLUBS,    true}  // Qc (up)
    };
    run_stud_parse_test("All Up Specific 3-card", "AsKdQc", 3, true, 3, 3, 0, 0, 3, pats5, 3);
    
    printf("\nStudHand_Parse tests completed.\n");

    // Placeholder for StudHand_Instantiate tests
    printf("\nRunning StudHand_Instantiate tests...\n\n");

    // Test S1: Specific 3-card hand, 3-card game, No Dead
    StdDeck_CardMask expected_s1[1];
    StdDeck_CardMask_RESET(expected_s1[0]);
    StdDeck_CardMask_SET(expected_s1[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(expected_s1[0], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(expected_s1[0], StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS));
    run_stud_instantiate_test("Instantiate (AsKdQc) gc=3 NoDead", "(AsKdQc)", 3, NULL, 1, true, 1, expected_s1);

    // Test S2: Specific 3-card hand, 3-card game, "As" Dead
    run_stud_instantiate_test("Instantiate (AsKdQc) gc=3 As Dead", "(AsKdQc)", 3, "As", 0, false, 0, NULL);

    // Test S3: Partial pattern (AsKd) for 3-card game, No Dead
    run_stud_instantiate_test("Instantiate (AsKd) gc=3 NoDead", "(AsKd)", 3, NULL, 50, false, 0, NULL);

    // Test S4: Pattern (AsKd)x for 3-card game, No Dead (explicit wildcard)
    run_stud_instantiate_test("Instantiate (AsKd)x gc=3 NoDead", "(AsKd)x", 3, NULL, 50, false, 0, NULL);

    // Test S5: Specific 7-card hand, 7-card game, No Dead
    StdDeck_CardMask expected_s5[1];
    StdDeck_CardMask_RESET(expected_s5[0]);
    StdDeck_CardMask_SET(expected_s5[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));   // (As
    StdDeck_CardMask_SET(expected_s5[0], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS)); // Kd
    StdDeck_CardMask_SET(expected_s5[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_CLUBS));    // Ac)
    StdDeck_CardMask_SET(expected_s5[0], StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS)); // Qh
    StdDeck_CardMask_SET(expected_s5[0], StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_HEARTS));  // Jh
    StdDeck_CardMask_SET(expected_s5[0], StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_HEARTS));   // Th
    StdDeck_CardMask_SET(expected_s5[0], StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_SPADES));  // 9s
    run_stud_instantiate_test("Instantiate (AsKdAc)QhJhTh9s gc=7 NoDead", "(AsKdAc)QhJhTh9s", 7, NULL, 1, true, 1, expected_s5);
    
    // Test S6: (AsAd)Kcxxxx for 7-card game, No Dead - Expect MAX_STUD_COMBOS due to large number of combos C(49,4) = 211876
    run_stud_instantiate_test("Instantiate (AsAd)Kcxxxx gc=7 NoDead", "(AsAd)Kcxxxx", 7, NULL, MAX_STUD_COMBOS, false, 0, NULL);

    // Test S7: (xxx)xxxx for 7-card game, No Dead - Expect MAX_STUD_COMBOS due to C(52,7)
    run_stud_instantiate_test("Instantiate (xxx)xxxx gc=7 NoDead", "(xxx)xxxx", 7, NULL, MAX_STUD_COMBOS, false, 0, NULL);


    printf("\nStudHand_Instantiate tests completed.\n");
    return 0;
}

// --- Implementation of StudHand_Instantiate Test Runner ---
static void run_stud_instantiate_test(const char* test_name, 
                               const char* hand_text, 
                               int game_total_cards, 
                               const char* dead_cards_str, 
                               int expected_combo_count, 
                               bool check_hands_exact, 
                               int num_expected_hands_in_array, // Should match expected_combo_count if check_hands_exact is true
                               StdDeck_CardMask* expected_hands_masks_arr) {

    printf("Test Instantiate: %s (Hand: \"%s\", GameCards: %d, Dead: \"%s\")\n", 
           test_name, hand_text, game_total_cards, dead_cards_str ? dead_cards_str : "None");

    StudHandQuery query;
    int parse_status = StudHand_Parse(hand_text, game_total_cards, &query);
    
    if (!parse_status) {
        if (expected_combo_count == 0) { // Or some other indicator for expected parse failure
            printf("  PASS: Expected parse failure and 0 combos, and parse failed.\n");
            printf("---\n");
            return;
        } else {
            printf("  FAIL: StudHand_Parse failed for hand string: %s\n", hand_text);
            assert(false); // Force test failure
            return;
        }
    }
    assert(parse_status == 1); // Should pass if not handled above

    StdDeck_CardMask dead_mask;
    StdDeck_CardMask_RESET(dead_mask);
    if (dead_cards_str) {
        string_to_card_mask(dead_cards_str, &dead_mask);
    }

    StudHandList generated_hands_list;
    generated_hands_list.count = 0; // Initialize count
    int actual_combo_count = StudHand_Instantiate(&query, dead_mask, &generated_hands_list);

    // If StudHand_Instantiate returns -1 (error), but we expected combos, it's a fail.
    // If it returns -1 and we expected 0 (e.g. conflict), it might be ok, but current tests use 0.
    if (actual_combo_count < 0 && expected_combo_count > 0) {
         printf("  FAIL: Instantiation returned error %d, expected %d combos.\n", actual_combo_count, expected_combo_count);
         assert(false);
    }
    if (actual_combo_count < 0 && expected_combo_count == 0) { // If error led to 0 combos, and 0 expected.
        printf("  PASS: Instantiation returned error %d, and 0 combos expected.\n", actual_combo_count);
        printf("---\n");
        return;
    }


    assert(actual_combo_count == expected_combo_count);

    if (check_hands_exact) {
        assert(actual_combo_count == num_expected_hands_in_array);
        
        bool* expected_found_flags = (bool*)calloc(num_expected_hands_in_array, sizeof(bool));
        if (!expected_found_flags && num_expected_hands_in_array > 0) { // calloc can return NULL
             printf("  FAIL: Could not allocate memory for found flags array.\n");
             assert(false); 
             return; // Should not happen in normal test environment
        }

        bool all_generated_found_in_expected = true;
        for (int i = 0; i < actual_combo_count; ++i) {
            bool current_gen_hand_found = false;
            for (int j = 0; j < num_expected_hands_in_array; ++j) {
                if (masks_are_equal(generated_hands_list.hands[i], expected_hands_masks_arr[j])) {
                    if (expected_found_flags[j]) {
                        // This expected hand was already matched. Implies generated list has duplicates,
                        // or expected list has duplicates that this simple flag system doesn't uniquely track.
                        // For now, this is not treated as an error for this test structure.
                    }
                    expected_found_flags[j] = true;
                    current_gen_hand_found = true;
                    break; 
                }
            }
            if (!current_gen_hand_found) {
                all_generated_found_in_expected = false;
                char buf[100];
                card_mask_to_string_static(generated_hands_list.hands[i], buf, sizeof(buf));
                printf("  FAIL: Generated hand [%s] not found in expected list.\n", buf);
            }
        }
        (void)all_generated_found_in_expected; // Suppress unused warning
        assert(all_generated_found_in_expected);

        bool all_expected_were_generated = true;
        for(int i=0; i<num_expected_hands_in_array; ++i) {
            if(!expected_found_flags[i]) {
                all_expected_were_generated = false;
                char buf[100];
                card_mask_to_string_static(expected_hands_masks_arr[i], buf, sizeof(buf));
                printf("  FAIL: Expected hand [%s] was not generated.\n", buf);
            }
        }
        (void)all_expected_were_generated; // Suppress unused warning
        assert(all_expected_were_generated);
        free(expected_found_flags);
    }

    printf("  PASS. Expected combos: %d, Got: %d.\n", expected_combo_count, actual_combo_count);
    if (actual_combo_count > 0 && actual_combo_count <= 5 && check_hands_exact) { // Print some for exact checks if few
        printf("  Generated hands (up to 5):\n");
        for (int i = 0; i < actual_combo_count && i < 5; ++i) {
            char buf[100];
            card_mask_to_string_static(generated_hands_list.hands[i], buf, sizeof(buf));
            printf("    - [%s]\n", buf);
        }
    }
    printf("---\n");
}
