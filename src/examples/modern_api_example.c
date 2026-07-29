/*
 * Example demonstrating the modern poker-eval C API
 *
 * This example shows how to use the new clean API with:
 * - Proper error handling
 * - Opaque structures
 * - Modern function naming
 */

#include <stdio.h>
#include <stdlib.h>
#include <poker_eval/core/poker_eval_modern.h>

static void print_error_and_exit(const char *operation, poker_eval_error_t error)
{
    fprintf(stderr, "Error in %s: %s\n", operation, poker_eval_error_string(error));
    exit(1);
}

int main(void)
{
    poker_eval_error_t err;

    printf("=== Modern Poker Eval API Example ===\n\n");

    // Get version information
    int major, minor, patch;
    err = poker_eval_get_version(&major, &minor, &patch);
    if (err != POKER_EVAL_SUCCESS)
    {
        print_error_and_exit("getting version", err);
    }
    printf("Library version: %d.%d.%d\n\n", major, minor, patch);

    // Create evaluation context
    poker_eval_context_t *context = NULL;
    err = poker_eval_context_create(POKER_DECK_STANDARD, &context);
    if (err != POKER_EVAL_SUCCESS)
    {
        print_error_and_exit("creating context", err);
    }
    printf("Created evaluation context for standard deck\n");

    // Enable caching for better performance
    err = poker_eval_context_set_caching(context, true);
    if (err != POKER_EVAL_SUCCESS)
    {
        print_error_and_exit("enabling caching", err);
    }
    printf("Enabled evaluation caching\n\n");

    // Create hands
    poker_eval_hand_t *hand1 = NULL;
    poker_eval_hand_t *hand2 = NULL;

    err = poker_eval_hand_create(&hand1);
    if (err != POKER_EVAL_SUCCESS)
    {
        print_error_and_exit("creating hand1", err);
    }

    err = poker_eval_hand_create(&hand2);
    if (err != POKER_EVAL_SUCCESS)
    {
        print_error_and_exit("creating hand2", err);
    }

    // Build first hand: As Ah (pocket aces)
    printf("Building Hand 1: As Ah\n");
    err = poker_eval_hand_add_card_string(hand1, "As");
    if (err != POKER_EVAL_SUCCESS)
    {
        print_error_and_exit("adding As to hand1", err);
    }

    err = poker_eval_hand_add_card_string(hand1, "Ah");
    if (err != POKER_EVAL_SUCCESS)
    {
        print_error_and_exit("adding Ah to hand1", err);
    }

    // Build second hand: Kc Kd (pocket kings)
    printf("Building Hand 2: Kc Kd\n");
    err = poker_eval_hand_add_card_string(hand2, "Kc");
    if (err != POKER_EVAL_SUCCESS)
    {
        print_error_and_exit("adding Kc to hand2", err);
    }

    err = poker_eval_hand_add_card_string(hand2, "Kd");
    if (err != POKER_EVAL_SUCCESS)
    {
        print_error_and_exit("adding Kd to hand2", err);
    }

    // Add community cards to both hands
    printf("Adding community cards: Ac 2h 3s 4d 5c\n");
    const char *community[] = {"Ac", "2h", "3s", "4d", "5c"};

    for (int i = 0; i < 5; i++)
    {
        err = poker_eval_hand_add_card_string(hand1, community[i]);
        if (err != POKER_EVAL_SUCCESS)
        {
            print_error_and_exit("adding community card to hand1", err);
        }

        err = poker_eval_hand_add_card_string(hand2, community[i]);
        if (err != POKER_EVAL_SUCCESS)
        {
            print_error_and_exit("adding community card to hand2", err);
        }
    }

    // Verify hand counts
    size_t count1, count2;
    err = poker_eval_hand_get_count(hand1, &count1);
    if (err != POKER_EVAL_SUCCESS)
    {
        print_error_and_exit("getting hand1 count", err);
    }

    err = poker_eval_hand_get_count(hand2, &count2);
    if (err != POKER_EVAL_SUCCESS)
    {
        print_error_and_exit("getting hand2 count", err);
    }

    printf("Hand 1 has %zu cards, Hand 2 has %zu cards\n\n", count1, count2);

    // Create evaluation results
    poker_eval_result_t *result1 = NULL;
    poker_eval_result_t *result2 = NULL;

    err = poker_eval_result_create(&result1);
    if (err != POKER_EVAL_SUCCESS)
    {
        print_error_and_exit("creating result1", err);
    }

    err = poker_eval_result_create(&result2);
    if (err != POKER_EVAL_SUCCESS)
    {
        print_error_and_exit("creating result2", err);
    }

    // Evaluate hands
    printf("Evaluating hands...\n");
    err = poker_eval_hand_evaluate(context, hand1, result1);
    if (err != POKER_EVAL_SUCCESS)
    {
        print_error_and_exit("evaluating hand1", err);
    }

    err = poker_eval_hand_evaluate(context, hand2, result2);
    if (err != POKER_EVAL_SUCCESS)
    {
        print_error_and_exit("evaluating hand2", err);
    }

    // Get hand types and descriptions
    poker_hand_type_t type1, type2;
    err = poker_eval_result_get_hand_type(result1, &type1);
    if (err != POKER_EVAL_SUCCESS)
    {
        print_error_and_exit("getting hand1 type", err);
    }

    err = poker_eval_result_get_hand_type(result2, &type2);
    if (err != POKER_EVAL_SUCCESS)
    {
        print_error_and_exit("getting hand2 type", err);
    }

    char desc1[64], desc2[64];
    err = poker_eval_result_get_description(result1, desc1, sizeof(desc1));
    if (err != POKER_EVAL_SUCCESS)
    {
        print_error_and_exit("getting hand1 description", err);
    }

    err = poker_eval_result_get_description(result2, desc2, sizeof(desc2));
    if (err != POKER_EVAL_SUCCESS)
    {
        print_error_and_exit("getting hand2 description", err);
    }

    printf("Hand 1: %s\n", desc1);
    printf("Hand 2: %s\n", desc2);

    // Compare hands
    int comparison;
    err = poker_eval_result_compare(result1, result2, &comparison);
    if (err != POKER_EVAL_SUCCESS)
    {
        print_error_and_exit("comparing results", err);
    }

    printf("\nComparison result: ");
    if (comparison > 0)
    {
        printf("Hand 1 wins!\n");
    }
    else if (comparison < 0)
    {
        printf("Hand 2 wins!\n");
    }
    else
    {
        printf("It's a tie!\n");
    }

    // Get raw values for advanced users
    uint32_t value1, value2;
    err = poker_eval_result_get_value(result1, &value1);
    if (err != POKER_EVAL_SUCCESS)
    {
        print_error_and_exit("getting hand1 value", err);
    }

    err = poker_eval_result_get_value(result2, &value2);
    if (err != POKER_EVAL_SUCCESS)
    {
        print_error_and_exit("getting hand2 value", err);
    }

    printf("Raw values: Hand 1 = %u, Hand 2 = %u\n", value1, value2);

    // Test error handling - try to add duplicate card
    printf("\nTesting error handling - adding duplicate card...\n");
    err = poker_eval_hand_add_card_string(hand1, "As");
    if (err == POKER_EVAL_ERROR_DUPLICATE_CARD)
    {
        printf("Correctly detected duplicate card: %s\n", poker_eval_error_string(err));
    }
    else
    {
        printf("Unexpected result when adding duplicate card\n");
    }

    // Test card checking
    bool contains_as, contains_2c;
    err = poker_eval_hand_contains_card(hand1, POKER_RANK_ACE, POKER_SUIT_SPADES, &contains_as);
    if (err != POKER_EVAL_SUCCESS)
    {
        print_error_and_exit("checking for As", err);
    }

    err = poker_eval_hand_contains_card(hand1, POKER_RANK_TWO, POKER_SUIT_CLUBS, &contains_2c);
    if (err != POKER_EVAL_SUCCESS)
    {
        print_error_and_exit("checking for 2c", err);
    }

    printf("Hand 1 contains As: %s\n", contains_as ? "yes" : "no");
    printf("Hand 1 contains 2c: %s\n", contains_2c ? "yes" : "no");

    // Cleanup
    printf("\nCleaning up...\n");
    poker_eval_result_destroy(result1);
    poker_eval_result_destroy(result2);
    poker_eval_hand_destroy(hand1);
    poker_eval_hand_destroy(hand2);
    poker_eval_context_destroy(context);

    printf("Example completed successfully!\n");
    return 0;
}
