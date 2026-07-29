#include <poker_eval/games/mixed_game.h>
#include <poker_eval/engine/game_engine.h>
#include <poker_eval/core/eval_context.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

static const char *game_name(game_type_t game)
{
    static const char *const names[] = {
        "Hold'em",
        "Omaha",
        "Omaha Hi/Lo",
        "PLO5",
        "PLO6",
        "Seven Card Stud",
        "Stud Hi/Lo",
        "Razz",
        "Five Card Draw",
        "Deuce to Seven",
        "Badugi",
        "Open Face Chinese",
        "Mixed HORSE"
    };
    if (game >= 0 && game < (int)(sizeof(names) / sizeof(names[0])))
        return names[game];
    return "Unknown";
}

static int test_sequential_rotation(void)
{
    mixed_variant_t variants[] = {
        {.game = GAME_TEXAS_HOLDEM, .hands_per_round = 1},
        {.game = GAME_OMAHA_HILO, .hands_per_round = 1},
    };

    mixed_game_t *mixed = mixed_game_create(variants, 2, MIXED_ROTATION_SEQUENTIAL);
    if (!mixed)
        return 1;

    const mixed_variant_t *current = mixed_game_current(mixed);
    if (!current || current->game != GAME_TEXAS_HOLDEM)
        return 2;

    mixed_game_advance(mixed, NULL, -1);
    current = mixed_game_current(mixed);
    if (!current || current->game != GAME_OMAHA_HILO)
        return 3;

    mixed_game_advance(mixed, NULL, -1);
    current = mixed_game_current(mixed);
    if (!current || current->game != GAME_TEXAS_HOLDEM)
        return 4;

    mixed_game_destroy(mixed);
    return 0;
}

static int test_horse_rotation(void)
{
    /* HORSE: Hold'em, Omaha Hi/Lo, Razz, Stud, Stud Hi/Lo */
    mixed_variant_t horse[] = {
        {.game = GAME_TEXAS_HOLDEM, .hands_per_round = 1},
        {.game = GAME_OMAHA_HILO, .hands_per_round = 1},
        {.game = GAME_RAZZ, .hands_per_round = 1},
        {.game = GAME_SEVEN_STUD, .hands_per_round = 1},
        {.game = GAME_SEVEN_STUD_HILO, .hands_per_round = 1},
    };

    mixed_game_t *mixed = mixed_game_create(horse, 5, MIXED_ROTATION_SEQUENTIAL);
    if (!mixed)
        return 20;

    /* Verify rotation through all 5 games */
    const game_type_t expected[] = {
        GAME_TEXAS_HOLDEM, GAME_OMAHA_HILO, GAME_RAZZ,
        GAME_SEVEN_STUD, GAME_SEVEN_STUD_HILO
    };

    for (int i = 0; i < 5; i++)
    {
        const mixed_variant_t *current = mixed_game_current(mixed);
        if (!current || current->game != expected[i])
            return 21 + i;
        mixed_game_advance(mixed, NULL, -1);
    }

    /* Should wrap back to Hold'em */
    const mixed_variant_t *current = mixed_game_current(mixed);
    if (!current || current->game != GAME_TEXAS_HOLDEM)
        return 26;

    mixed_game_destroy(mixed);
    return 0;
}

static int test_8game_rotation(void)
{
    /* 8-game rotation */
    mixed_variant_t eight_game[] = {
        {.game = GAME_DEUCE_TO_SEVEN, .hands_per_round = 2},
        {.game = GAME_TEXAS_HOLDEM, .hands_per_round = 2},
        {.game = GAME_OMAHA_HILO, .hands_per_round = 2},
        {.game = GAME_RAZZ, .hands_per_round = 2},
        {.game = GAME_SEVEN_STUD, .hands_per_round = 2},
        {.game = GAME_SEVEN_STUD_HILO, .hands_per_round = 2},
        {.game = GAME_TEXAS_HOLDEM, .hands_per_round = 2},  /* NL Hold'em */
        {.game = GAME_OMAHA, .hands_per_round = 2},         /* PLO */
    };

    mixed_game_t *mixed = mixed_game_create(eight_game, 8, MIXED_ROTATION_SEQUENTIAL);
    if (!mixed)
        return 30;

    /* First game should be 2-7 Triple Draw */
    const mixed_variant_t *current = mixed_game_current(mixed);
    if (!current || current->game != GAME_DEUCE_TO_SEVEN)
        return 31;

    /* Advance 2 hands to move to next game */
    mixed_game_advance(mixed, NULL, -1);
    mixed_game_advance(mixed, NULL, -1);

    current = mixed_game_current(mixed);
    if (!current || current->game != GAME_TEXAS_HOLDEM)
        return 32;

    /* Advance through all 8 games (16 total hands) */
    for (int i = 0; i < 14; i++)
    {
        mixed_game_advance(mixed, NULL, -1);
    }

    /* Should be back at 2-7 Triple Draw */
    current = mixed_game_current(mixed);
    if (!current || current->game != GAME_DEUCE_TO_SEVEN)
        return 33;

    mixed_game_destroy(mixed);
    return 0;
}

static int test_dealer_choice(void)
{
    mixed_variant_t choices[] = {
        {.game = GAME_TEXAS_HOLDEM, .hands_per_round = 1},
        {.game = GAME_OMAHA, .hands_per_round = 1},
        {.game = GAME_RAZZ, .hands_per_round = 1},
    };

    mixed_game_t *mixed = mixed_game_create(choices, 3, MIXED_ROTATION_DEALER_CHOICE);
    if (!mixed)
        return 40;

    /* Dealer selects Omaha */
    if (!mixed_game_set_current_variant(mixed, 1))
        return 41;

    const mixed_variant_t *current = mixed_game_current(mixed);
    if (!current || current->game != GAME_OMAHA)
        return 42;

    /* Dealer selects Razz */
    if (!mixed_game_set_current_variant(mixed, 2))
        return 43;

    current = mixed_game_current(mixed);
    if (!current || current->game != GAME_RAZZ)
        return 44;

    /* Invalid index should fail */
    if (mixed_game_set_current_variant(mixed, 5))
        return 45;

    mixed_game_destroy(mixed);
    return 0;
}

static int test_reset(void)
{
    mixed_variant_t variants[] = {
        {.game = GAME_TEXAS_HOLDEM, .hands_per_round = 1},
        {.game = GAME_OMAHA, .hands_per_round = 1},
    };

    mixed_game_t *mixed = mixed_game_create(variants, 2, MIXED_ROTATION_SEQUENTIAL);
    if (!mixed)
        return 50;

    /* Advance to Omaha */
    mixed_game_advance(mixed, NULL, -1);

    const mixed_variant_t *current = mixed_game_current(mixed);
    if (!current || current->game != GAME_OMAHA)
        return 51;

    /* Reset should go back to Hold'em */
    mixed_game_reset(mixed);
    current = mixed_game_current(mixed);
    if (!current || current->game != GAME_TEXAS_HOLDEM)
        return 52;

    mixed_game_destroy(mixed);
    return 0;
}

static int test_stats_accumulation(void)
{
    mixed_variant_t variants[] = {
        {.game = GAME_TEXAS_HOLDEM, .hands_per_round = 1},
        {.game = GAME_RAZZ, .hands_per_round = 1},
    };

    mixed_game_t *mixed = mixed_game_create(variants, 2, MIXED_ROTATION_SEQUENTIAL);
    if (!mixed)
        return 10;

    HandResult results[2];
    memset(results, 0, sizeof(results));
    results[0].is_winner = true;
    results[0].winnings = 150;
    mixed_game_record_result(mixed, results, 2);

    mixed_game_advance(mixed, NULL, -1);

    mixed_game_report_t report;
    mixed_game_get_report(mixed, &report);
    if (report.total_hands_played != 1)
        return 11;
    if (report.stats[0].wins != 1 || report.stats[0].hands_played != 1)
        return 12;
    if (fabs(report.stats[0].total_winnings - 150.0) > 1e-6)
        return 13;

    mixed_game_destroy(mixed);
    return 0;
}

int main(void)
{
    int result;
    int passed = 0;
    int failed = 0;

    printf("Running Mixed Game tests...\n\n");

    /* Test 1: Sequential rotation */
    result = test_sequential_rotation();
    if (result != 0)
    {
        fprintf(stderr, "FAIL: Sequential rotation test (error %d)\n", result);
        failed++;
    }
    else
    {
        printf("PASS: Sequential rotation\n");
        passed++;
    }

    /* Test 2: HORSE rotation */
    result = test_horse_rotation();
    if (result != 0)
    {
        fprintf(stderr, "FAIL: HORSE rotation test (error %d)\n", result);
        failed++;
    }
    else
    {
        printf("PASS: HORSE rotation\n");
        passed++;
    }

    /* Test 3: 8-game rotation */
    result = test_8game_rotation();
    if (result != 0)
    {
        fprintf(stderr, "FAIL: 8-game rotation test (error %d)\n", result);
        failed++;
    }
    else
    {
        printf("PASS: 8-game rotation\n");
        passed++;
    }

    /* Test 4: Dealer's choice */
    result = test_dealer_choice();
    if (result != 0)
    {
        fprintf(stderr, "FAIL: Dealer's choice test (error %d)\n", result);
        failed++;
    }
    else
    {
        printf("PASS: Dealer's choice\n");
        passed++;
    }

    /* Test 5: Reset */
    result = test_reset();
    if (result != 0)
    {
        fprintf(stderr, "FAIL: Reset test (error %d)\n", result);
        failed++;
    }
    else
    {
        printf("PASS: Reset\n");
        passed++;
    }

    /* Test 6: Stats accumulation */
    result = test_stats_accumulation();
    if (result != 0)
    {
        fprintf(stderr, "FAIL: Stats accumulation test (error %d)\n", result);
        failed++;
    }
    else
    {
        printf("PASS: Stats accumulation\n");
        passed++;
    }

    printf("\n========================================\n");
    printf("Mixed Game Tests: %d passed, %d failed\n", passed, failed);
    printf("========================================\n");

    return failed > 0 ? 1 : 0;
}
