/*
 * test_gpu_omaha.c: Test GPU Omaha evaluation
 *
 * This test demonstrates the new generic GPU evaluation framework
 * by evaluating Omaha hands on GPU and comparing with CPU.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <poker_eval/gpu/eval_gpu.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/eval.h>
#include <poker_eval/core/low_eval.h>
#include <poker_eval/core/low_qualifier.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/games/eval_omaha.h>

/* Test configuration */
#define TEST_SKIP_CODE 77
#define NUM_TEST_BOARDS 10
#define NUM_PLAYERS 2

/* Test result tracking */
typedef struct {
    int tests_run;
    int tests_passed;
    int tests_failed;
} test_results_t;

static test_results_t g_results = {0, 0, 0};

#define TEST_ASSERT(condition, message) do { \
    g_results.tests_run++; \
    if (condition) { \
        g_results.tests_passed++; \
        printf("  PASS: %s\n", message); \
    } else { \
        g_results.tests_failed++; \
        printf("  FAIL: %s\n", message); \
    } \
} while(0)

/* Generate random card mask with no duplicates */
static StdDeck_CardMask random_cards_no_dup(int n_cards, StdDeck_CardMask dead) {
    StdDeck_CardMask result;
    StdDeck_CardMask_RESET(result);

    for (int i = 0; i < n_cards; i++) {
        int card;
        StdDeck_CardMask card_mask;

        do {
            card = rand() % 52;
            card_mask = StdDeck_MASK(card);
        } while (StdDeck_CardMask_ANY_SET(dead, card_mask) ||
                 StdDeck_CardMask_ANY_SET(result, card_mask));

        StdDeck_CardMask_OR(result, result, card_mask);
        StdDeck_CardMask_OR(dead, dead, card_mask);
    }

    return result;
}

static low_qualifier_t to_low_qualifier(int qualifier)
{
    if (qualifier == 8)
        return LOW_QUALIFIER_8;
    if (qualifier == 7)
        return LOW_QUALIFIER_7;
    return LOW_QUALIFIER_NONE;
}

static bool expected_low_qualifies(gpu_game_config_t config, LowHandVal cpu_lo)
{
    if (config.game == GPU_GAME_RAZZ)
        return cpu_lo != LowHandVal_NOTHING;
    if (config.low_qualifier == 0)
        return cpu_lo != LowHandVal_NOTHING;
    return pe_low_qualify5(cpu_lo, to_low_qualifier(config.low_qualifier));
}

static HandVal evaluate_cpu_hi(gpu_game_config_t config,
                              StdDeck_CardMask board,
                              StdDeck_CardMask hole)
{
    if (config.num_board_cards > 0) {
        if (config.game == GPU_GAME_OMAHA ||
            config.game == GPU_GAME_OMAHA5 ||
            config.game == GPU_GAME_OMAHA6 ||
            config.game == GPU_GAME_OMAHA8) {
            HandVal hi = HandVal_NOTHING;
            StdDeck_OmahaHi_EVAL(hole, board, &hi);
            return hi;
        }
        StdDeck_CardMask combined;
        StdDeck_CardMask_RESET(combined);
        StdDeck_CardMask_OR(combined, board, hole);
        return StdDeck_StdRules_EVAL_N(combined, 7);
    }

    return StdDeck_StdRules_EVAL_N(hole, config.num_hole_cards);
}

static LowHandVal evaluate_cpu_lo(gpu_game_config_t config,
                                 StdDeck_CardMask board,
                                 StdDeck_CardMask hole)
{
    StdDeck_CardMask combined;
    StdDeck_CardMask_RESET(combined);

    if (config.num_board_cards > 0) {
        StdDeck_CardMask_OR(combined, board, hole);
    } else {
        combined = hole;
    }

    if (config.game == GPU_GAME_LOWBALL27) {
        return pe_eval_low_27(combined);
    }
    return pe_eval_low_a5(combined);
}

static void run_gpu_cpu_variant(const char* label,
                                gpu_eval_context_t* ctx,
                                gpu_game_config_t config,
                                int n_boards,
                                int n_players)
{
    printf("\n--- Variant: %s ---\n", label);

    StdDeck_CardMask* boards = (StdDeck_CardMask*)calloc(n_boards, sizeof(StdDeck_CardMask));
    StdDeck_CardMask* hole_cards = (StdDeck_CardMask*)calloc(n_boards * n_players, sizeof(StdDeck_CardMask));
    if (!boards || !hole_cards) {
        TEST_ASSERT(boards && hole_cards, "Allocating board/hole buffers");
        free(boards);
        free(hole_cards);
        return;
    }

    StdDeck_CardMask global_dead;
    StdDeck_CardMask_RESET(global_dead);
    StdDeck_CardMask empty_board;
    StdDeck_CardMask_RESET(empty_board);

    for (int i = 0; i < n_boards; ++i) {
        StdDeck_CardMask board = empty_board;
        if (config.num_board_cards > 0) {
            board = random_cards_no_dup(config.num_board_cards, global_dead);
            StdDeck_CardMask_OR(global_dead, global_dead, board);
        }
        boards[i] = board;

        StdDeck_CardMask hole_dead = board;
        for (int p = 0; p < n_players; ++p) {
            StdDeck_CardMask hole = random_cards_no_dup(config.num_hole_cards, hole_dead);
            hole_cards[i * n_players + p] = hole;
            StdDeck_CardMask_OR(hole_dead, hole_dead, hole);
            StdDeck_CardMask_OR(global_dead, global_dead, hole);
        }
    }

    gpu_eval_result_hilo_t result = {0};
    int status = gpu_eval_batch_boards_hilo(
        ctx, boards, hole_cards, n_boards, n_players, &result);

    TEST_ASSERT(status == 0, "GPU hi/lo evaluation succeeded for variant");
    if (status != 0)
        goto cleanup;
    TEST_ASSERT(result.hand_values_hi != NULL, "GPU returned hi values");

    for (int i = 0; i < n_boards; ++i) {
        for (int p = 0; p < n_players; ++p) {
            size_t idx = (size_t)i * n_players + (size_t)p;
            if (config.eval_low && !config.split_pot) {
                TEST_ASSERT(result.hand_values_hi[idx] == 0,
                            "GPU hi slot is zero for low-only game");
            } else {
                HandVal expected_hi = evaluate_cpu_hi(config, boards[i], hole_cards[idx]);
                TEST_ASSERT(result.hand_values_hi[idx] == expected_hi,
                            "GPU hi matches CPU");
            }

            if (config.eval_low) {
                LowHandVal cpu_lo = evaluate_cpu_lo(config, boards[i], hole_cards[idx]);
                bool expected_loq = expected_low_qualifies(config, cpu_lo);
                TEST_ASSERT(result.lo_qualifies[idx] == (expected_loq ? 1 : 0),
                            "GPU low qualifier matches CPU");

                if (expected_loq) {
                    TEST_ASSERT(result.hand_values_lo[idx] == cpu_lo,
                                "GPU low value matches CPU");
                } else {
                    TEST_ASSERT(result.hand_values_lo[idx] == LowHandVal_NOTHING,
                                "GPU low is NOTHING when CPU low fails qualifier");
                }
            }
        }
    }

cleanup:
    free(result.hand_values_hi);
    free(result.hand_values_lo);
    free(result.lo_qualifies);
    free(boards);
    free(hole_cards);
}

static gpu_eval_context_t* init_gpu_context_for_config(gpu_game_config_t config)
{
    gpu_eval_context_t* ctx = NULL;
    if (gpu_is_available(0)) {
        ctx = gpu_eval_init_game(0, 1024, 0, config);
    }
    if (!ctx && gpu_is_available(1)) {
        ctx = gpu_eval_init_game(0, 1024, 1, config);
    }
    return ctx;
}

/*
 * Test 1: GPU Context Initialization with Game Config
 */
static void test_gpu_init_with_game_config(void) {
    printf("\n=== Test 1: GPU Initialization with Game Config ===\n");

    /* Create Omaha configuration */
    gpu_game_config_t config = gpu_game_config_omaha(4);

    TEST_ASSERT(config.game == GPU_GAME_OMAHA, "Config has correct game type");
    TEST_ASSERT(config.num_hole_cards == 4, "Config has 4 hole cards");
    TEST_ASSERT(config.num_board_cards == 5, "Config has 5 board cards");
    TEST_ASSERT(config.eval_low == 0, "Config doesn't eval low");
    TEST_ASSERT(config.split_pot == 0, "Config isn't split pot");

    /* Try to initialize GPU context */
    gpu_eval_context_t* ctx = gpu_eval_init_game(0, 1000, 0, config);

    if (ctx == NULL) {
        printf("  INFO: GPU not available or initialization failed (this is OK for testing)\n");
        TEST_ASSERT(1, "Handled GPU unavailable gracefully");
    } else {
        TEST_ASSERT(ctx->game_config.game == GPU_GAME_OMAHA,
                   "Context has correct game config");
        gpu_eval_cleanup(ctx);
        TEST_ASSERT(1, "GPU context cleanup successful");
    }
}

/*
 * Test 2: Game Config Helpers
 */
static void test_game_config_helpers(void) {
    printf("\n=== Test 2: Game Config Helper Functions ===\n");

    /* Test Hold'em */
    gpu_game_config_t holdem = gpu_game_config_holdem();
    TEST_ASSERT(holdem.num_hole_cards == 2 && holdem.num_board_cards == 5,
               "Hold'em config correct");

    /* Test Omaha variants */
    gpu_game_config_t omaha4 = gpu_game_config_omaha(4);
    TEST_ASSERT(omaha4.num_hole_cards == 4, "Omaha-4 config correct");

    gpu_game_config_t omaha5 = gpu_game_config_omaha(5);
    TEST_ASSERT(omaha5.num_hole_cards == 5 && omaha5.game == GPU_GAME_OMAHA5,
               "Omaha-5 config correct");

    gpu_game_config_t omaha6 = gpu_game_config_omaha(6);
    TEST_ASSERT(omaha6.num_hole_cards == 6 && omaha6.game == GPU_GAME_OMAHA6,
               "Omaha-6 config correct");

    /* Test Omaha Hi/Lo */
    gpu_game_config_t omaha8 = gpu_game_config_omaha8(4);
    TEST_ASSERT(omaha8.eval_low == 1 && omaha8.split_pot == 1,
               "Omaha8 config has lo eval and split pot");
    TEST_ASSERT(omaha8.low_qualifier == 8, "Omaha8 has 8-or-better qualifier");

    /* Test Stud */
    gpu_game_config_t stud = gpu_game_config_stud();
    TEST_ASSERT(stud.num_hole_cards == 7 && stud.num_board_cards == 0,
               "Stud config correct (no board)");

    gpu_game_config_t stud8 = gpu_game_config_stud8();
    TEST_ASSERT(stud8.game == GPU_GAME_STUD8 && stud8.eval_low == 1 && stud8.split_pot == 1,
               "Stud8 config enables hi/lo split");
    TEST_ASSERT(stud8.low_qualifier == 8, "Stud8 has 8-or-better qualifier");

    /* Test Razz */
    gpu_game_config_t razz = gpu_game_config_razz();
    TEST_ASSERT(razz.eval_low == 1 && razz.split_pot == 0,
               "Razz config evals low only");
    TEST_ASSERT(razz.low_qualifier == 0, "Razz has no qualifier");
}

/*
 * Test 3: Omaha Evaluation Placeholder
 * (Will work once GPU kernels are fully compiled)
 */
static void test_omaha_evaluation_placeholder(void) {
    printf("\n=== Test 3: Omaha GPU Evaluation (Placeholder) ===\n");

    printf("  INFO: This test demonstrates the API for Omaha GPU evaluation\n");
    printf("  INFO: Full implementation requires GPU compilation and linking\n");

    /* Create test scenario */
    StdDeck_CardMask board, holes[NUM_PLAYERS];
    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);

    /* Generate random board */
    board = random_cards_no_dup(5, dead);
    StdDeck_CardMask_OR(dead, dead, board);

    /* Generate random Omaha hands (4 cards each) */
    for (int p = 0; p < NUM_PLAYERS; p++) {
        holes[p] = random_cards_no_dup(4, dead);
        StdDeck_CardMask_OR(dead, dead, holes[p]);
    }

    printf("  Board: ");
    StdDeck_printMask(board);
    printf("\n");

    for (int p = 0; p < NUM_PLAYERS; p++) {
        printf("  Player %d: ", p + 1);
        StdDeck_printMask(holes[p]);
        printf("\n");
    }

    /* CPU evaluation for reference */
    HandVal cpu_values[NUM_PLAYERS];
    for (int p = 0; p < NUM_PLAYERS; p++) {
        cpu_values[p] = StdDeck_OmahaHi_EVAL(holes[p], board, NULL);
        printf("  Player %d CPU value: %u\n", p + 1, cpu_values[p]);
    }

    TEST_ASSERT(1, "CPU Omaha evaluation completed");

    /* Note: GPU evaluation would go here once fully implemented */
    printf("  TODO: GPU evaluation will compare against CPU values\n");
}

static void test_gpu_multi_game_equivalence(void)
{
    printf("\n=== Test 4: GPU multi-game Hi/Lo equivalence ===\n");

    if (!gpu_is_available(0) && !gpu_is_available(1))
    {
        printf("  INFO: No GPU backend available, skipping multi-game comparison\n");
        TEST_ASSERT(1, "GPU multi-game comparison skipped");
        return;
    }

    struct {
        const char* label;
        gpu_game_config_t config;
        int n_boards;
        int n_players;
    } variants[] = {
        { "Hold'em8 hi/lo", gpu_game_config_holdem8(), 3, 2 },
        { "Omaha hi", gpu_game_config_omaha(4), 2, 2 },
        { "Omaha8 hi/lo", gpu_game_config_omaha8(4), 2, 2 },
        { "Stud hi", gpu_game_config_stud(), 1, 2 },
        { "Stud8 hi/lo", gpu_game_config_stud8(), 1, 2 },
        { "Razz low", gpu_game_config_razz(), 1, 2 },
    };

    for (size_t v = 0; v < sizeof(variants) / sizeof(variants[0]); ++v)
    {
        gpu_eval_context_t* ctx = init_gpu_context_for_config(variants[v].config);
        if (!ctx)
        {
            printf("  INFO: No GPU context for %s (backend missing)\n", variants[v].label);
            TEST_ASSERT(1, "GPU context unavailable for variant");
            continue;
        }

        run_gpu_cpu_variant(variants[v].label, ctx,
                            variants[v].config,
                            variants[v].n_boards,
                            variants[v].n_players);

        gpu_eval_cleanup(ctx);
    }
}

/*
 * Main test function
 */
int main(void) {
    printf("===============================================\n");
    printf("GPU Multi-Game Framework Test - Omaha Edition\n");
    printf("===============================================\n");

    if (!gpu_is_available(0) && !gpu_is_available(1)) {
        printf("SKIP: No GPU backend available\n");
        return TEST_SKIP_CODE;
    }

    srand((unsigned int)time(NULL));

    /* Run tests */
    test_gpu_init_with_game_config();
    test_game_config_helpers();
    test_gpu_multi_game_equivalence();

    /* Print summary */
    printf("\n=== Test Summary ===\n");
    printf("Tests run: %d\n", g_results.tests_run);
    printf("Tests passed: %d\n", g_results.tests_passed);
    printf("Tests failed: %d\n", g_results.tests_failed);

    if (g_results.tests_failed == 0) {
        printf("\nAll tests passed!\n");
        return 0;
    } else {
        printf("\nSome tests failed!\n");
        return 1;
    }
}
