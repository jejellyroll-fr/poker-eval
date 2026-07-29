/*
 * test_c_api_best_five.c - pe_hand_result_t.cards must hold the best 5 cards
 *
 * Regression tests for the stable C API: the reported cards have to match the
 * reported hand_value, including when the best combination is not the first
 * five cards of the input.
 */

#include <poker_eval_api.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

static void fail(const char* test, const char* detail) {
    printf("FAIL: %s - %s\n", test, detail);
    failures++;
}

static void pass(const char* test) {
    printf("PASS: %s\n", test);
}

/* Render a result's cards as "AhKhQhJhTh" for diagnostics. */
static void cards_to_string(const pe_hand_result_t* result, char* out, size_t size) {
    out[0] = '\0';
    for (int i = 0; i < 5; i++) {
        char card[3];
        if (pe_card_to_string(result->cards[i], card, sizeof(card)) != PE_OK) {
            snprintf(out, size, "<invalid>");
            return;
        }
        strncat(out, card, size - strlen(out) - 1);
    }
}

/* The five reported cards, re-evaluated on their own, must reproduce hand_value. */
static void check_self_consistent(pe_handle_t h, const char* test,
                                  const pe_hand_result_t* result) {
    pe_hand_result_t five;
    if (pe_evaluate_hand(h, result->cards, 5, &five) != PE_OK) {
        fail(test, "re-evaluating the reported cards failed");
        return;
    }

    if (five.hand_value != result->hand_value) {
        char rendered[16];
        char detail[160];
        cards_to_string(result, rendered, sizeof(rendered));
        snprintf(detail, sizeof(detail),
                 "reported cards %s evaluate to %u, expected %u",
                 rendered, five.hand_value, result->hand_value);
        fail(test, detail);
        return;
    }

    pass(test);
}

/* The reported cards must be exactly the expected ones, order-insensitive. */
static void check_cards_are(const char* test, const pe_hand_result_t* result,
                            const char* expected_str) {
    uint8_t expected[5];
    if (pe_parse_board(expected_str, expected, 5) != 5) {
        fail(test, "test setup: could not parse expected cards");
        return;
    }

    for (int i = 0; i < 5; i++) {
        int found = 0;
        for (int j = 0; j < 5; j++) {
            if (result->cards[j] == expected[i]) { found = 1; break; }
        }
        if (!found) {
            char rendered[16];
            char detail[160];
            cards_to_string(result, rendered, sizeof(rendered));
            snprintf(detail, sizeof(detail), "got %s, expected %s",
                     rendered, expected_str);
            fail(test, detail);
            return;
        }
    }

    pass(test);
}

/* Seven cards whose best hand excludes both hole cards (board plays). */
static void test_seven_cards_board_plays(pe_handle_t h) {
    pe_hand_result_t result;
    if (pe_evaluate_holdem(h, "2c3d", "AhKhQhJhTh", &result) != PE_OK) {
        fail("seven_cards_board_plays", "evaluation failed");
        return;
    }

    check_cards_are("seven_cards_board_plays/cards", &result, "AhKhQhJhTh");
    check_self_consistent(h, "seven_cards_board_plays/consistency", &result);
}

/* Seven cards whose best hand uses one hole card and excludes an early board card. */
static void test_seven_cards_river_makes_hand(pe_handle_t h) {
    pe_hand_result_t result;
    /* Ac2c + 7h8s9dKdAd: the ace paired on the river, so the best hand is
     * AcAd with K/9/8 kickers - neither the first five cards (Ac2c7h8s9d)
     * nor any prefix of the input. */
    if (pe_evaluate_holdem(h, "Ac2c", "7h8s9dKdAd", &result) != PE_OK) {
        fail("seven_cards_river_makes_hand", "evaluation failed");
        return;
    }

    check_cards_are("seven_cards_river_makes_hand/cards", &result, "AcAdKd9d8s");
    check_self_consistent(h, "seven_cards_river_makes_hand/consistency", &result);
}

/* Six cards whose best hand excludes the very first card. */
static void test_six_cards_first_excluded(pe_handle_t h) {
    uint8_t cards[6];
    if (pe_parse_board("2h AhKhQhJhTh", cards, 6) != 6) {
        fail("six_cards_first_excluded", "test setup: parse failed");
        return;
    }

    pe_hand_result_t result;
    if (pe_evaluate_hand(NULL, cards, 6, &result) == PE_OK) {
        fail("six_cards_first_excluded", "NULL handle should be rejected");
        return;
    }
    if (pe_evaluate_hand(h, cards, 6, &result) != PE_OK) {
        fail("six_cards_first_excluded", "evaluation failed");
        return;
    }

    check_cards_are("six_cards_first_excluded/cards", &result, "AhKhQhJhTh");
    check_self_consistent(h, "six_cards_first_excluded/consistency", &result);
}

/* Five cards must be reported unchanged. */
static void test_five_cards_identity(pe_handle_t h) {
    uint8_t cards[5];
    if (pe_parse_board("AhKhQhJhTh", cards, 5) != 5) {
        fail("five_cards_identity", "test setup: parse failed");
        return;
    }

    pe_hand_result_t result;
    if (pe_evaluate_hand(h, cards, 5, &result) != PE_OK) {
        fail("five_cards_identity", "evaluation failed");
        return;
    }

    if (memcmp(result.cards, cards, 5) != 0) {
        fail("five_cards_identity", "five-card input was not echoed back");
        return;
    }

    pass("five_cards_identity");
}

/* Duplicate cards produce an undefined mask and must be rejected. */
static void test_duplicate_rejected(pe_handle_t h) {
    uint8_t cards[6];
    if (pe_parse_board("AhKhQhJhTh", cards, 5) != 5) {
        fail("duplicate_rejected", "test setup: parse failed");
        return;
    }
    cards[5] = cards[0];

    pe_hand_result_t result;
    if (pe_evaluate_hand(h, cards, 6, &result) == PE_OK) {
        fail("duplicate_rejected", "duplicate card was accepted");
        return;
    }

    pass("duplicate_rejected");
}

int main(void) {
    pe_config_t config;
    pe_get_default_config(&config);

    pe_handle_t h = pe_init(&config);
    if (!h) {
        printf("FAIL: pe_init returned NULL\n");
        return 1;
    }

    printf("=== C API best-five-cards tests ===\n");
    test_seven_cards_board_plays(h);
    test_seven_cards_river_makes_hand(h);
    test_six_cards_first_excluded(h);
    test_five_cards_identity(h);
    test_duplicate_rejected(h);

    pe_free(h);

    if (failures > 0) {
        printf("\n%d failure(s)\n", failures);
        return 1;
    }

    printf("\nAll tests passed\n");
    return 0;
}
