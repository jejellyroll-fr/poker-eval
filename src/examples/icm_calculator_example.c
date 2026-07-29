/*
 * ICM Calculator Example
 *
 * This example demonstrates how to use the ICM (Independent Chip Model) calculator
 * for tournament and sit-and-go scenarios.
 *
 * Copyright (C) 2024 Poker-Eval Project
 * Licensed under GPL v3 or later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poker_eval/utils/icm_calculator.h>

static void print_separator(const char *title)
{
    printf("\n");
    printf("=== %s ===\n", title);
    printf("\n");
}

static void demo_basic_icm_calculation(void)
{
    print_separator("Basic ICM Calculation - 4 Player SNG");

    // Create tournament structure
    icm_tournament_t tournament;
    icm_tournament_init(&tournament);

    // Add players with different chip stacks
    icm_tournament_add_player(&tournament, 3000, "Alice");
    icm_tournament_add_player(&tournament, 2500, "Bob");
    icm_tournament_add_player(&tournament, 2000, "Charlie");
    icm_tournament_add_player(&tournament, 1500, "David");

    // Set up payout structure (50%, 30%, 20% for top 3)
    icm_setup_standard_payouts(&tournament.payout_structure, 4, 3, 1000.0);

    // Print tournament state
    icm_print_tournament(&tournament);

    // Calculate ICM equity
    icm_result_t result;
    icm_error_t error = icm_calculate_equity(&tournament, &result);

    if (error == ICM_SUCCESS)
    {
        printf("ICM calculation completed in %.4f seconds\n", result.calculation_time);
        icm_print_results(&result, true);
    }
    else
    {
        printf("ICM calculation failed: %s\n", icm_error_string(error));
    }
}

static void demo_sng_payouts(void)
{
    print_separator("Standard SNG Payout Structures");

    icm_payout_structure_t payout;
    double prize_pool = 1000.0;

    // 6-max SNG
    if (icm_setup_sng_payouts(&payout, 6, prize_pool) == ICM_SUCCESS)
    {
        printf("6-max SNG Payouts (Total: $%.2f):\n", payout.total_prize_pool);
        for (int i = 0; i < payout.num_paid_positions; i++)
        {
            printf("  %d place: $%.2f (%.1f%%)\n", i + 1,
                   payout.payouts[i] * payout.total_prize_pool,
                   payout.payouts[i] * 100.0);
        }
        printf("\n");
    }

    // 9-max SNG
    if (icm_setup_sng_payouts(&payout, 9, prize_pool) == ICM_SUCCESS)
    {
        printf("9-max SNG Payouts (Total: $%.2f):\n", payout.total_prize_pool);
        for (int i = 0; i < payout.num_paid_positions; i++)
        {
            printf("  %d place: $%.2f (%.1f%%)\n", i + 1,
                   payout.payouts[i] * payout.total_prize_pool,
                   payout.payouts[i] * 100.0);
        }
        printf("\n");
    }
}

static void demo_monte_carlo_vs_exact(void)
{
    print_separator("Monte Carlo vs Exact Calculation Comparison");

    uint64_t stacks[] = {4000, 3000, 2000, 1000};
    int num_players = 4;

    icm_payout_structure_t payouts;
    icm_setup_standard_payouts(&payouts, num_players, 3, 1000.0);

    // Exact calculation
    icm_result_t exact_result;
    icm_error_t error = icm_calculate_exact(stacks, num_players, &payouts, &exact_result);

    if (error == ICM_SUCCESS)
    {
        printf("Exact ICM Calculation (%.4f seconds):\n", exact_result.calculation_time);
        for (int i = 0; i < num_players; i++)
        {
            printf("  Player %d: $%.2f\n", i + 1, exact_result.equity[i]);
        }
        printf("\n");
    }

    // Monte Carlo calculation
    icm_result_t mc_result;
    error = icm_calculate_monte_carlo(stacks, num_players, &payouts, 100000, &mc_result);

    if (error == ICM_SUCCESS)
    {
        printf("Monte Carlo ICM Calculation (100k iterations, %.4f seconds):\n", mc_result.calculation_time);
        for (int i = 0; i < num_players; i++)
        {
            printf("  Player %d: $%.2f", i + 1, mc_result.equity[i]);
            if (exact_result.is_valid)
            {
                double diff = mc_result.equity[i] - exact_result.equity[i];
                printf(" (diff: %+.2f)", diff);
            }
            printf("\n");
        }
        printf("\n");
    }

    // Approximation calculation
    icm_result_t approx_result;
    error = icm_calculate_approximation(stacks, num_players, &payouts, &approx_result);

    if (error == ICM_SUCCESS)
    {
        printf("Approximation ICM Calculation (%.4f seconds):\n", approx_result.calculation_time);
        for (int i = 0; i < num_players; i++)
        {
            printf("  Player %d: $%.2f", i + 1, approx_result.equity[i]);
            if (exact_result.is_valid)
            {
                double diff = approx_result.equity[i] - exact_result.equity[i];
                printf(" (diff: %+.2f)", diff);
            }
            printf("\n");
        }
        printf("\n");
    }
}

static void demo_chip_ev_calculation(void)
{
    print_separator("Chip EV Calculation for All-in Decision");

    uint64_t stacks[] = {2000, 2000, 3000, 3000};
    int num_players = 4;
    int player_index = 0; // First player making decision

    icm_payout_structure_t payouts;
    icm_setup_sng_payouts(&payouts, 9, 1000.0); // Use 9-max payout structure

    printf("Current stacks: ");
    for (int i = 0; i < num_players; i++)
    {
        printf("%llu ", (unsigned long long)stacks[i]);
    }
    printf("\n");

    // Scenario: Player 0 is considering an all-in call
    uint64_t chips_at_risk = 2000; // All chips
    uint64_t chips_to_win = 2000;  // Opponent's stack

    printf("Decision: Call all-in for %llu chips to win %llu chips\n",
           (unsigned long long)chips_at_risk, (unsigned long long)chips_to_win);

    // Calculate EV for different win probabilities
    double win_probabilities[] = {0.3, 0.4, 0.5, 0.6, 0.7};
    int num_scenarios = sizeof(win_probabilities) / sizeof(win_probabilities[0]);

    printf("\nChip EV Analysis:\n");
    printf("Win Prob | Chip EV  | Decision\n");
    printf("---------|----------|----------\n");

    for (int i = 0; i < num_scenarios; i++)
    {
        double chip_ev = icm_calculate_chip_ev(stacks, num_players, player_index,
                                               win_probabilities[i], chips_at_risk,
                                               chips_to_win, &payouts);

        printf("  %4.1f%% | %+7.2f | %s\n",
               win_probabilities[i] * 100.0, chip_ev,
               chip_ev > 0 ? "CALL" : "FOLD");
    }
    printf("\n");
}

static void demo_bubble_factor(void)
{
    print_separator("Bubble Factor Analysis");

    icm_tournament_t tournament;
    icm_tournament_init(&tournament);

    // Bubble situation: 4 players, 3 get paid
    icm_tournament_add_player(&tournament, 5000, "BigStack");
    icm_tournament_add_player(&tournament, 2000, "MediumStack");
    icm_tournament_add_player(&tournament, 1500, "ShortStack");
    icm_tournament_add_player(&tournament, 500, "BubbleBoy");

    icm_setup_sng_payouts(&tournament.payout_structure, 9, 1000.0);

    icm_print_tournament(&tournament);

    // Normal ICM
    icm_result_t normal_result;
    icm_calculate_equity(&tournament, &normal_result);

    printf("Normal ICM Equity:\n");
    for (int i = 0; i < normal_result.num_players; i++)
    {
        printf("  %s: $%.2f\n", tournament.players[i].name, normal_result.equity[i]);
    }
    printf("\n");

    // ICM with bubble factor (more risk averse)
    icm_result_t bubble_result;
    icm_calculate_with_bubble_factor(&tournament, 1.5, &bubble_result);

    printf("ICM with Bubble Factor 1.5 (more risk averse):\n");
    for (int i = 0; i < bubble_result.num_players; i++)
    {
        printf("  %s: $%.2f (change: %+.2f)\n",
               tournament.players[i].name, bubble_result.equity[i],
               bubble_result.equity[i] - normal_result.equity[i]);
    }
    printf("\n");
}

static void demo_finish_probabilities(void)
{
    print_separator("Finish Probability Analysis");

    uint64_t stacks[] = {4000, 3000, 2000, 1000};
    int num_players = 4;

    printf("Chip stacks: ");
    for (int i = 0; i < num_players; i++)
    {
        printf("%llu ", (unsigned long long)stacks[i]);
    }
    printf("\n\n");

    printf("Finish Probabilities:\n");
    printf("Player | 1st    | 2nd    | 3rd    | 4th\n");
    printf("-------|--------|--------|--------|--------\n");

    for (int player = 0; player < num_players; player++)
    {
        printf("   %d   |", player + 1);
        for (int position = 1; position <= num_players; position++)
        {
            double prob = icm_finish_probability(stacks, num_players, player, position);
            printf(" %5.1f%% |", prob * 100.0);
        }
        printf("\n");
    }
    printf("\n");
}

int main(void)
{
    printf("ICM Calculator Example\n");
    printf("======================\n");

    // Run all demonstrations
    demo_basic_icm_calculation();
    demo_sng_payouts();
    demo_monte_carlo_vs_exact();
    demo_chip_ev_calculation();
    demo_bubble_factor();
    demo_finish_probabilities();

    printf("ICM Calculator demonstration completed.\n");
    printf("This calculator can be used for:\n");
    printf("- Tournament equity calculations\n");
    printf("- Sit-and-go analysis\n");
    printf("- All-in decision making\n");
    printf("- Bubble play optimization\n");
    printf("- Future scenario planning\n");

    return 0;
}
