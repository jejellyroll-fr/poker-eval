#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h> // Should be already included by other headers, but good practice

#include <poker_eval/deck/deck_std.h>   // For StdDeck_Rank_X constants, MAKE_CARD
#include <poker_eval/deck/deck_joker.h> // For JokerDeck_CardMask, JokerDeck_JOKER, JokerDeck_stringToCard
#include <poker_eval/games/rules_joker.h>// For JokerRules_HandType_X constants
#include <poker_eval/core/handval.h>    // For HandVal, HandVal_HANDTYPE, HandVal_TOP_CARD etc.
#include <poker_eval/core/poker_defs.h> // For CardMask_SET, CardMask_RESET (via deck_joker.h includes)
#include <poker_eval/games/eval_joker.h> // For JokerDeck_JokerRules_EVAL_N

// Assumes StdDeck is initialized if any StdDeck functions are directly used
// For JokerDeck.stringToCard, it should be fine as it's part of JokerDeck struct.
// extern Deck JokerDeck; // Already declared in deck_joker.h // From lib/deck_joker.c
// extern const char *JokerRules_handTypeNames[]; // Already declared in rules_joker.h // From rules_joker.c


// Helper to convert a string of cards (e.g., "AsKsQsJsXx") to JokerDeck_CardMask
// Returns number of cards successfully parsed.
static int string_to_joker_mask(const char* hand_str, JokerDeck_CardMask* mask, int* num_cards_parsed) {
    JokerDeck_CardMask_RESET(*mask);
    *num_cards_parsed = 0;
    if (!hand_str) return 0;

    char card_text_buffer[3]; // For "As", "Kd", "Xx"
    card_text_buffer[2] = '\0';
    int cards_found = 0;

    for (size_t i = 0; i < strlen(hand_str); i += 2) {
        if (i + 1 < strlen(hand_str)) {
            card_text_buffer[0] = hand_str[i];
            card_text_buffer[1] = hand_str[i+1];
            
            int card_idx = -1;
            // Use JokerDeck.stringToCard which should point to JokerDeck_stringToCard
            // The first argument to stringToCard is usually the Deck pointer itself.
            // The stringToCard in deck_joker.c is JokerDeck_stringToCard(const char *s, int *card_p)
            // So, JokerDeck.stringToCard should be a function pointer of that type.
            if (JokerDeck.stringToCard((char*)card_text_buffer, &card_idx)) { // Cast away const for char* if needed by API
                 JokerDeck_CardMask_SET(*mask, card_idx);
                 cards_found++;
            } else {
                fprintf(stderr, "Warning: Could not parse card string '%s' in string_to_joker_mask for test.\n", card_text_buffer);
                return -1; // Indicate error
            }
        } else {
            fprintf(stderr, "Warning: Trailing character in card string '%s' for test.\n", hand_str);
            return -1; // Indicate error
        }
    }
    *num_cards_parsed = cards_found;
    return cards_found;
}

// Test runner for JokerDeck_JokerRules_EVAL_N
static void run_joker_eval_test(const char* test_name, 
                         const char* hand_str_with_joker, 
                         int expected_hand_type, 
                         int expected_top_card_rank_val,
                         int expected_second_card_rank_val, // Use -1 if not applicable or not checked
                         int expected_third_card_rank_val) { // Use -1 if not applicable or not checked
    printf("Test Eval: %s (Hand: \"%s\")\n", test_name, hand_str_with_joker);

    JokerDeck_CardMask hand_mask;
    int num_cards = 0;
    if (string_to_joker_mask(hand_str_with_joker, &hand_mask, &num_cards) < 0 || num_cards == 0) {
        printf("  FAIL: Could not parse hand string: %s\n", hand_str_with_joker);
        assert(false);
        return;
    }
    // Ensure num_cards is appropriate for the evaluation (e.g. 5 for typical 5-card joker poker)
    // The JokerDeck_JokerRules_EVAL_N function takes n_total_cards_in_hand.
    // string_to_joker_mask sets num_cards to the number of cards parsed from the string.
    // If the game implies a fixed number of cards (e.g. 5 card draw), num_cards should match that.
    // For these tests, we assume the hand_str_with_joker provides exactly the number of cards to evaluate.

    HandVal result_val = JokerDeck_JokerRules_EVAL_N(hand_mask, num_cards);
    int actual_hand_type = HandVal_HANDTYPE(result_val);
    int actual_top_card = HandVal_TOP_CARD(result_val);
    int actual_second_card = HandVal_SECOND_CARD(result_val);
    int actual_third_card = HandVal_THIRD_CARD(result_val);

    bool type_match = (actual_hand_type == expected_hand_type);
    bool top_card_match = (expected_top_card_rank_val == -1 || actual_top_card == expected_top_card_rank_val);
    bool second_card_match = (expected_second_card_rank_val == -1 || actual_second_card == expected_second_card_rank_val);
    bool third_card_match = (expected_third_card_rank_val == -1 || actual_third_card == expected_third_card_rank_val);

    if (type_match && top_card_match && second_card_match && third_card_match) {
        printf("  PASS. Type: %s, Top: %d, Second: %d, Third: %d\n", 
               JokerRules_handTypeNames[actual_hand_type], actual_top_card, actual_second_card, actual_third_card);
    } else {
        printf("  FAIL:\n");
        printf("    Expected Type: %s (%d), Got: %s (%d)\n", 
               JokerRules_handTypeNames[expected_hand_type], expected_hand_type, 
               JokerRules_handTypeNames[actual_hand_type], actual_hand_type);
        if (expected_top_card_rank_val != -1 && actual_top_card != expected_top_card_rank_val) {
            printf("    Expected Top Card: %d, Got: %d\n", expected_top_card_rank_val, actual_top_card);
        }
        if (expected_second_card_rank_val != -1 && actual_second_card != expected_second_card_rank_val) {
            printf("    Expected Second Card: %d, Got: %d\n", expected_second_card_rank_val, actual_second_card);
        }
        if (expected_third_card_rank_val != -1 && actual_third_card != expected_third_card_rank_val) {
             printf("    Expected Third Card: %d, Got: %d\n", expected_third_card_rank_val, actual_third_card);
        }
        assert(false); // Force test failure for summary
    }
    printf("---\n");
}


int main(void) {
    // StdDeck_Initialize(); // Not strictly needed for JokerDeck tests as JokerDeck_stringToCard is specific.
    // JokerDeck_Build(); // This or similar might be needed if JokerDeck is not auto-initialized.
                         // For poker-eval, often these are globals initialized by library load or first use.

    printf("Running JokerDeck_JokerRules_EVAL_N tests (Fully Wild Logic)...\n");

    // Test Cases (Rank constants from deck_std.h are fine as JokerDeck aliases them)
    // Ranks: 2=0, ..., 9=7, T=8, J=9, Q=10, K=11, A=12

    // Five of a Kind (Quints)
    run_joker_eval_test("Quints Aces", "AsAhAdAcXx", JokerRules_HandType_QUINTS, StdDeck_Rank_ACE, -1, -1);
    run_joker_eval_test("Quints Kings", "KsKhKdKcXx", JokerRules_HandType_QUINTS, StdDeck_Rank_KING, -1, -1);

    // Straight Flush (Joker Completing)
    run_joker_eval_test("Royal Flush (Joker as Ten)", "AsKsQsJsXx", JokerRules_HandType_STFLUSH, StdDeck_Rank_ACE, -1, -1);
    run_joker_eval_test("Royal Flush (Joker as Ace)", "KsQsJsTsXx", JokerRules_HandType_STFLUSH, StdDeck_Rank_ACE, -1, -1);
    run_joker_eval_test("Five High SF (Joker as Five)", "Ah2h3h4hXx", JokerRules_HandType_STFLUSH, StdDeck_Rank_5, -1, -1); // A-5 SF is 5-high

    // Critical Test: Joker making better trips vs. becoming Ace for a pair
    // Hand: Ks Kd Qc Jc Xx (5 cards)
    // Optimal: Joker as Kc -> Trips Kings (K K K Q J)
    run_joker_eval_test("Joker makes Trips Kings (better than Pair Kings with Joker as Ace)", 
                         "KsKdQcJcXx", JokerRules_HandType_TRIPS, StdDeck_Rank_KING, StdDeck_Rank_QUEEN, StdDeck_Rank_JACK);

    // Critical Test: Joker making Full House (Pair + Joker makes Trips) vs. lesser hand
    // Hand: Ks Kd Qs Qh Xx (5 cards)
    // Optimal: Joker as Ks (or Qs) -> Full House (KsKsKsQhQs or QsQsQsKhKs). Best is KKKQQ.
    run_joker_eval_test("Joker makes KKKQQ Full House", 
                         "KsKdQsQhXx", JokerRules_HandType_FULLHOUSE, StdDeck_Rank_KING, StdDeck_Rank_QUEEN, -1);
    
    // Hand: As Ah Kc Xx Qd (5 cards) -> Joker as As -> Trips Aces (AAA KQ)
    run_joker_eval_test("Joker makes Trips Aces (AsAhXx KcQd)",
                         "AsAhKcXxQd", JokerRules_HandType_TRIPS, StdDeck_Rank_ACE, StdDeck_Rank_KING, StdDeck_Rank_QUEEN);


    printf("\nJoker evaluation tests completed.\n");
    return 0;
}
