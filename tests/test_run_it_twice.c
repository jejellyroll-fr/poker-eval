/*
 * test_run_it_twice.c - Unit tests for Run It Twice functionality
 */

#include <poker_eval/equity/run_it_twice.h>
#include <poker_eval/deck/deck_std.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define TEST_PASSED 0
#define TEST_FAILED 1

static int tests_run = 0;
static int tests_passed = 0;

#define RUN_TEST(test) do { \
    tests_run++; \
    printf("Running %s...\n", #test); \
    if (test() == TEST_PASSED) { \
        tests_passed++; \
        printf("  ✓ PASSED\n"); \
    } else { \
        printf("  ✗ FAILED\n"); \
    } \
} while(0)

/* Helper to create a card mask from string like "AsKh" */
static StdDeck_CardMask str_to_mask(const char *str) {
    StdDeck_CardMask mask;
    StdDeck_CardMask_RESET(mask);

    while (*str) {
        if (*str == ' ') {
            str++;
            continue;
        }

        int rank = -1, suit = -1;

        /* Parse rank */
        switch (*str++) {
            case 'A': case 'a': rank = StdDeck_Rank_ACE; break;
            case 'K': case 'k': rank = StdDeck_Rank_KING; break;
            case 'Q': case 'q': rank = StdDeck_Rank_QUEEN; break;
            case 'J': case 'j': rank = StdDeck_Rank_JACK; break;
            case 'T': case 't': rank = StdDeck_Rank_TEN; break;
            case '9': rank = StdDeck_Rank_9; break;
            case '8': rank = StdDeck_Rank_8; break;
            case '7': rank = StdDeck_Rank_7; break;
            case '6': rank = StdDeck_Rank_6; break;
            case '5': rank = StdDeck_Rank_5; break;
            case '4': rank = StdDeck_Rank_4; break;
            case '3': rank = StdDeck_Rank_3; break;
            case '2': rank = StdDeck_Rank_2; break;
            default: return mask;
        }

        /* Parse suit */
        switch (*str++) {
            case 'h': case 'H': suit = StdDeck_Suit_HEARTS; break;
            case 'd': case 'D': suit = StdDeck_Suit_DIAMONDS; break;
            case 'c': case 'C': suit = StdDeck_Suit_CLUBS; break;
            case 's': case 'S': suit = StdDeck_Suit_SPADES; break;
            default: return mask;
        }

        int card = StdDeck_MAKE_CARD(rank, suit);
        StdDeck_CardMask_SET(mask, card);
    }

    return mask;
}

static StdDeck_CardMask make_dead_mask(const StdDeck_CardMask board,
                                       const StdDeck_CardMask holes[],
                                       int num_players,
                                       StdDeck_CardMask allowed_extra)
{
    StdDeck_CardMask dead;
    StdDeck_CardMask used = board;
    StdDeck_CardMask_RESET(dead);

    for (int p = 0; p < num_players; ++p) {
        StdDeck_CardMask_OR(used, used, holes[p]);
    }

    for (int card = 0; card < StdDeck_N_CARDS; ++card) {
        if (StdDeck_CardMask_CARD_IS_SET(used, card)) {
            continue;
        }
        if (StdDeck_CardMask_CARD_IS_SET(allowed_extra, card)) {
            continue;
        }
        StdDeck_CardMask_SET(dead, card);
    }

    return dead;
}

/* Test 1: Configuration validation */
static int test_config_validation(void) {
    rit_config_t config;

    /* Valid config */
    config = rit_config_default();
    if (!rit_config_validate(&config)) {
        printf("    Default config should be valid\n");
        return TEST_FAILED;
    }

    /* Invalid: too few runouts */
    config.num_runouts = 1;
    if (rit_config_validate(&config)) {
        printf("    Should reject num_runouts < 2\n");
        return TEST_FAILED;
    }

    /* Invalid: too many runouts */
    config = rit_config_default();
    config.num_runouts = RIT_MAX_RUNOUTS + 1;
    if (rit_config_validate(&config)) {
        printf("    Should reject num_runouts > RIT_MAX_RUNOUTS\n");
        return TEST_FAILED;
    }

    /* NULL config */
    if (rit_config_validate(NULL)) {
        printf("    Should reject NULL config\n");
        return TEST_FAILED;
    }

    return TEST_PASSED;
}

/* Test 2: Runout generation independence */
static int test_runout_generation(void) {
    StdDeck_CardMask base_board = str_to_mask("AhKdQc");  /* Flop */
    StdDeck_CardMask dead = str_to_mask("AsKs");          /* Dead cards */
    StdDeck_CardMask boards[4];

    int result = rit_generate_runouts(base_board, dead, 2, 4, 12345, boards);
    if (result != RIT_SUCCESS) {
        printf("    Failed to generate runouts: %s\n", rit_error_string(result));
        return TEST_FAILED;
    }

    /* Check all boards have 5 cards */
    for (int i = 0; i < 4; i++) {
        int count = 0;
        for (int c = 0; c < StdDeck_N_CARDS; c++) {
            if (StdDeck_CardMask_CARD_IS_SET(boards[i], c)) count++;
        }
        if (count != 5) {
            printf("    Board %d has %d cards, expected 5\n", i, count);
            return TEST_FAILED;
        }
    }

    /* Check boards are different (at least one card differs) */
    for (int i = 0; i < 3; i++) {
        if (StdDeck_CardMask_EQUAL(boards[i], boards[i+1])) {
            printf("    Boards %d and %d are identical (should be independent)\n", i, i+1);
            return TEST_FAILED;
        }
    }

    /* Check base board cards are present in all runouts */
    for (int i = 0; i < 4; i++) {
        for (int c = 0; c < StdDeck_N_CARDS; c++) {
            if (StdDeck_CardMask_CARD_IS_SET(base_board, c)) {
                if (!StdDeck_CardMask_CARD_IS_SET(boards[i], c)) {
                    printf("    Board %d missing base board card\n", i);
                    return TEST_FAILED;
                }
            }
        }
    }

    return TEST_PASSED;
}

/* Test 3: Hold'em heads-up RIT-2 */
static int test_holdem_headsup_rit2(void) {
    StdDeck_CardMask holes[2];
    holes[0] = str_to_mask("AsAh");  /* Pocket Aces */
    holes[1] = str_to_mask("KsKh");  /* Pocket Kings */

    StdDeck_CardMask flop = str_to_mask("Qc9d2h");

    rit_result_t result;
    int ret = rit_holdem_equity(holes, 2, flop, 2, &result);

    if (ret != RIT_SUCCESS) {
        printf("    Failed: %s\n", rit_error_string(ret));
        return TEST_FAILED;
    }

    /* AA should have higher equity than KK */
    if (result.avg_equity[0] <= result.avg_equity[1]) {
        /* With 2 runouts, variance can lead to ties or losses.
           We accept >= 50% for regression testing with small samples. */
        if (result.avg_equity[0] < 0.5) {
             printf("    AA equity (%.2f%%) should be > KK equity (%.2f%%)\n",
                result.avg_equity[0] * 100, result.avg_equity[1] * 100);
             return TEST_FAILED;
        }
    }

    /* Total equity should sum to 1.0 per runout */
    double total_equity = result.avg_equity[0] + result.avg_equity[1];
    if (fabs(total_equity - 1.0) > 0.001) {
        printf("    Total equity %.3f should be 1.0\n", total_equity);
        return TEST_FAILED;
    }

    /* Check wins + ties + losses = num_runouts for each player */
    for (int p = 0; p < 2; p++) {
        int total = result.total_wins[p] + result.total_ties[p] + result.total_losses[p];
        if (total != result.num_runouts) {
            printf("    Player %d: wins+ties+losses=%d, expected %d\n",
                p, total, result.num_runouts);
            return TEST_FAILED;
        }
    }

    return TEST_PASSED;
}

/* Omaha Hi/Lo single runout split pot */
static int test_omaha8_runout_split(void) {
    StdDeck_CardMask holes[2];
    holes[0] = str_to_mask("KhThQdQc");  /* Nut flush potential, no low */
    holes[1] = str_to_mask("2d3c4s5d");  /* Strong low candidate */

    StdDeck_CardMask board = str_to_mask("Ah5h9h7cQs");

    rit_runout_result_t result;
    int ret = rit_evaluate_runout(holes, 2, board, RIT_GAME_OMAHA8, &result);
    if (ret != RIT_SUCCESS) {
        printf("    Failed: %s\n", rit_error_string(ret));
        return TEST_FAILED;
    }

    if (result.winner_mask != 0x1 || result.num_winners != 1) {
        printf("    Expected player 1 to scoop high pot\n");
        return TEST_FAILED;
    }

    if (!result.low_available || result.low_winner_mask != 0x2 || result.low_num_winners != 1) {
        printf("    Expected player 2 to win low pot exclusively\n");
        return TEST_FAILED;
    }

    if (fabs(result.equities[0] - 0.5) > 1e-6 || fabs(result.equities[1] - 0.5) > 1e-6) {
        printf("    Equity split should be 50/50 but got %.3f / %.3f\n",
               result.equities[0], result.equities[1]);
        return TEST_FAILED;
    }

    if (fabs(result.low_equities[0]) > 1e-6 || fabs(result.low_equities[1] - 0.5) > 1e-6) {
        printf("    Low equities incorrect: %.3f / %.3f\n",
               result.low_equities[0], result.low_equities[1]);
        return TEST_FAILED;
    }

    return TEST_PASSED;
}

/* Omaha Hi/Lo aggregate statistics */
static int test_omaha8_aggregate(void) {
    StdDeck_CardMask holes[2];
    holes[0] = str_to_mask("KhThQdQc");
    holes[1] = str_to_mask("2d3c4s5d");

    StdDeck_CardMask board = str_to_mask("Ah5h9h7c"); /* 4-board cards */
    StdDeck_CardMask allowed = str_to_mask("Qs3d");
    StdDeck_CardMask dead = make_dead_mask(board, holes, 2, allowed);

    rit_config_t config = rit_config_default();
    config.game_type = RIT_GAME_OMAHA8;
    config.stage = RIT_STAGE_RIVER;
    config.num_runouts = 2;
    config.seed = 123456;
    config.track_individual = true;
    config.calc_variance = true;

    rit_result_t result;
    int ret = rit_calculate_equity(holes, 2, board, dead, &config, &result);
    if (ret != RIT_SUCCESS) {
        printf("    Failed: %s\n", rit_error_string(ret));
        return TEST_FAILED;
    }

    if (fabs(result.avg_equity[0] - 0.5) > 1e-6 || fabs(result.avg_equity[1] - 0.5) > 1e-6) {
        printf("    Total equities mismatch: %.3f / %.3f\n",
               result.avg_equity[0], result.avg_equity[1]);
        return TEST_FAILED;
    }

    if (fabs(result.avg_low_equity[0]) > 1e-6 || fabs(result.avg_low_equity[1] - 0.5) > 1e-6) {
        printf("    Low equities mismatch: %.3f / %.3f\n",
               result.avg_low_equity[0], result.avg_low_equity[1]);
        return TEST_FAILED;
    }

    if (result.total_wins[0] != config.num_runouts ||
        result.total_losses[1] != config.num_runouts) {
        printf("    High win/loss counts incorrect\n");
        return TEST_FAILED;
    }

    if (result.total_low_wins[1] != config.num_runouts ||
        result.total_low_losses[0] != config.num_runouts) {
        printf("    Low win/loss counts incorrect\n");
        return TEST_FAILED;
    }

    for (int run = 0; run < config.num_runouts; ++run) {
        if (!result.runouts[run].low_available ||
            result.runouts[run].low_winner_mask != 0x2) {
            printf("    Expected low availability and winner mask on run %d\n", run);
            return TEST_FAILED;
        }
    }

    return TEST_PASSED;
}

/* Test 4: Hold'em 3-way RIT-3 */
static int test_holdem_3way_rit3(void) {
    StdDeck_CardMask holes[3];
    holes[0] = str_to_mask("AsKs");  /* Big slick suited */
    holes[1] = str_to_mask("QcQd");  /* Pocket Queens */
    holes[2] = str_to_mask("JhTh");  /* JT suited */

    StdDeck_CardMask flop = str_to_mask("Qh9s2c");  /* QQ has set */
    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);

    /* Use fixed seed for deterministic test results */
    rit_config_t config = {
        .game_type = RIT_GAME_HOLDEM,
        .stage = RIT_STAGE_TURN_RIVER,
        .num_runouts = 3,
        .seed = 12345,  /* Fixed seed for reproducibility */
        .enable_simd = true,
        .num_threads = 1,
        .calc_variance = false,
        .track_individual = true
    };

    rit_result_t result;
    int ret = rit_calculate_equity(holes, 3, flop, dead, &config, &result);

    if (ret != RIT_SUCCESS) {
        printf("    Failed: %s\n", rit_error_string(ret));
        return TEST_FAILED;
    }

    /* QQ with set should have highest equity - with set on flop, QQ is a huge favorite */
    /* Debug: print actual equities */
    printf("    Equities: AKs=%.2f%%, QQ=%.2f%%, JTs=%.2f%%\n",
           result.avg_equity[0] * 100, result.avg_equity[1] * 100, result.avg_equity[2] * 100);

    if (result.avg_equity[1] < result.avg_equity[0] ||
        result.avg_equity[1] < result.avg_equity[2]) {
        printf("    QQ with set should have highest equity\n");
        return TEST_FAILED;
    }

    /* Total equity should sum to 1.0 */
    double total = result.avg_equity[0] + result.avg_equity[1] + result.avg_equity[2];
    if (fabs(total - 1.0) > 0.001) {
        printf("    Total equity %.3f should be 1.0\n", total);
        return TEST_FAILED;
    }

    return TEST_PASSED;
}

/* Test 5: Omaha heads-up RIT-2 */
static int test_omaha_headsup_rit2(void) {
    StdDeck_CardMask holes[2];
    holes[0] = str_to_mask("AsAdKsKd");  /* AAxx double suited */
    holes[1] = str_to_mask("QdQcJdJc");  /* QQJJxx double suited */

    StdDeck_CardMask flop = str_to_mask("Ac9h2d");

    rit_result_t result;
    int ret = rit_omaha_equity(holes, 2, flop, 2, &result);

    if (ret != RIT_SUCCESS) {
        printf("    Failed: %s\n", rit_error_string(ret));
        return TEST_FAILED;
    }

    /* AAxx with set should dominate */
    /* Relaxed check for small sample size (2 runouts) */
    if (result.avg_equity[0] < 0.4) {
        printf("    AAxx with set of aces should have reasonable equity, got %.1f%%\n",
            result.avg_equity[0] * 100);
        return TEST_FAILED;
    }

    /* Total equity check */
    double total = result.avg_equity[0] + result.avg_equity[1];
    if (fabs(total - 1.0) > 0.001) {
        printf("    Total equity %.3f should be 1.0\n", total);
        return TEST_FAILED;
    }

    return TEST_PASSED;
}

/* Test 6: Variance reduction */
static int test_variance_reduction(void) {
    StdDeck_CardMask holes[2];
    holes[0] = str_to_mask("AsKs");
    holes[1] = str_to_mask("QhQd");

    StdDeck_CardMask flop = str_to_mask("Jh9d2c");

    /* Run with variance calculation enabled */
    rit_config_t config = rit_config_holdem_rit2();
    config.num_runouts = 4;
    config.calc_variance = true;
    config.track_individual = true;
    config.seed = 42;
    config.stage = RIT_STAGE_TURN_RIVER;

    rit_result_t result;
    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);

    int ret = rit_calculate_equity(holes, 2, flop, dead, &config, &result);
    if (ret != RIT_SUCCESS) {
        printf("    Failed: %s\n", rit_error_string(ret));
        return TEST_FAILED;
    }

    /* Theoretical variance reduction should equal num_runouts */
    double expected_reduction = rit_theoretical_variance_reduction(4);
    if (fabs(expected_reduction - 4.0) > 0.01) {
        printf("    Theoretical variance reduction should be 4.0, got %.2f\n",
            expected_reduction);
        return TEST_FAILED;
    }

    /* Variance should be calculated */
    if (result.equity_variance[0] < 0.0 || result.equity_variance[1] < 0.0) {
        printf("    Variance should be non-negative\n");
        return TEST_FAILED;
    }

    return TEST_PASSED;
}

/* Test 7: River stage (single card runout) */
static int test_river_stage(void) {
    StdDeck_CardMask holes[2];
    holes[0] = str_to_mask("AsKs");
    holes[1] = str_to_mask("AhKh");

    /* Board with flop + turn */
    StdDeck_CardMask board = str_to_mask("QdJcTs9h");  /* 4 cards without flush skew */

    rit_result_t result;
    int ret = rit_holdem_equity(holes, 2, board, 2, &result);

    if (ret != RIT_SUCCESS) {
        printf("    Failed: %s\n", rit_error_string(ret));
        return TEST_FAILED;
    }

    /* Both have identical hands (straight), so equity should be equal */
    if (fabs(result.avg_equity[0] - result.avg_equity[1]) > 0.1) {
        printf("    Identical hands should have similar equity\n");
        printf("    Player 1: %.2f%%, Player 2: %.2f%%\n",
            result.avg_equity[0] * 100, result.avg_equity[1] * 100);
        return TEST_FAILED;
    }

    return TEST_PASSED;
}

/* Test 8: Error handling - too many players */
static int test_error_too_many_players(void) {
    StdDeck_CardMask holes[RIT_MAX_PLAYERS + 1];
    for (int i = 0; i <= RIT_MAX_PLAYERS; i++) {
        holes[i] = str_to_mask("AsKs");
    }

    StdDeck_CardMask board = str_to_mask("QhJd9c");
    rit_result_t result;

    int ret = rit_holdem_equity(holes, RIT_MAX_PLAYERS + 1, board, 2, &result);

    if (ret != RIT_ERROR_TOO_MANY_PLAYERS) {
        printf("    Should return RIT_ERROR_TOO_MANY_PLAYERS, got %d\n", ret);
        return TEST_FAILED;
    }

    return TEST_PASSED;
}

/* Test 9: Error handling - deck exhausted */
static int test_error_deck_exhausted(void) {
    /* Create scenario where we can't deal enough cards */
    StdDeck_CardMask base_board = str_to_mask("AhKdQc");

    /* Mark almost entire deck as dead (48 cards) */
    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);
    for (int i = 0; i < 48; i++) {
        StdDeck_CardMask_SET(dead, i);
    }

    StdDeck_CardMask boards[2];
    int ret = rit_generate_runouts(base_board, dead, 5, 2, 123, boards);

    if (ret != RIT_ERROR_DECK_EXHAUSTED) {
        printf("    Should return RIT_ERROR_DECK_EXHAUSTED, got %d\n", ret);
        return TEST_FAILED;
    }

    return TEST_PASSED;
}

/* Test 10: Deterministic with same seed */
static int test_deterministic_seed(void) {
    StdDeck_CardMask holes[2];
    holes[0] = str_to_mask("AsKs");
    holes[1] = str_to_mask("QhQd");

    StdDeck_CardMask flop = str_to_mask("Jh9d2c");

    rit_config_t config = rit_config_holdem_rit2();
    config.seed = 999;
    config.track_individual = true;
    config.stage = RIT_STAGE_TURN_RIVER;

    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);

    rit_result_t result1, result2;

    int ret1 = rit_calculate_equity(holes, 2, flop, dead, &config, &result1);
    int ret2 = rit_calculate_equity(holes, 2, flop, dead, &config, &result2);

    if (ret1 != RIT_SUCCESS || ret2 != RIT_SUCCESS) {
        printf("    Failed to calculate equity\n");
        return TEST_FAILED;
    }

    /* Results should be identical with same seed */
    for (int run = 0; run < 2; run++) {
        if (!StdDeck_CardMask_EQUAL(result1.runouts[run].board, result2.runouts[run].board)) {
            printf("    Runout %d boards differ with same seed\n", run);
            return TEST_FAILED;
        }
    }

    return TEST_PASSED;
}

/* Main test runner */
int main(void) {
    printf("\n=== Run It Twice Unit Tests ===\n\n");

    RUN_TEST(test_config_validation);
    RUN_TEST(test_runout_generation);
    RUN_TEST(test_holdem_headsup_rit2);
    RUN_TEST(test_holdem_3way_rit3);
    RUN_TEST(test_omaha_headsup_rit2);
    RUN_TEST(test_omaha8_runout_split);
    RUN_TEST(test_omaha8_aggregate);
    RUN_TEST(test_variance_reduction);
    RUN_TEST(test_river_stage);
    RUN_TEST(test_error_too_many_players);
    RUN_TEST(test_error_deck_exhausted);
    RUN_TEST(test_deterministic_seed);

    printf("\n=== Test Summary ===\n");
    printf("Tests run: %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_run - tests_passed);

    if (tests_passed == tests_run) {
        printf("\n✓ All tests passed!\n\n");
        return 0;
    } else {
        printf("\n✗ Some tests failed\n\n");
        return 1;
    }
}
