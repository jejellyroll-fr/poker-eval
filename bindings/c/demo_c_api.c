/*
 * demo_c_api.c - Demonstration of the poker-eval C API
 *
 * Copyright (C) 2025 poker-eval contributors
 */

#include "poker_eval_api.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    int major, minor, patch;
    pe_get_version(&major, &minor, &patch);
    printf("Poker-Eval C API v%d.%d.%d\n", major, minor, patch);
    printf("========================================\n\n");

    /* Initialize library */
    pe_handle_t handle = pe_init(NULL);
    if (!handle) {
        fprintf(stderr, "Failed to initialize poker-eval\n");
        return 1;
    }

    /* 1. Card parsing */
    printf("1. Card Parsing\n");
    printf("---------------\n");

    int card = pe_parse_card("As");
    printf("   As = card #%d\n", card);

    card = pe_parse_card("Kh");
    printf("   Kh = card #%d\n", card);

    char card_str[4];
    pe_card_to_string(51, card_str, sizeof(card_str));
    printf("   Card 51 = %s\n\n", card_str);

    /* 2. Hand evaluation */
    printf("2. Hand Evaluation\n");
    printf("------------------\n");

    pe_hand_result_t hand_result;
    pe_error_t err = pe_evaluate_holdem(handle, "AhAs", "KhQhJhTh2d", &hand_result);
    if (err == PE_OK) {
        printf("   AhAs on KhQhJhTh2d:\n");
        printf("   Hand type: %s\n", hand_result.hand_name);
        printf("   Hand value: %u\n\n", hand_result.hand_value);
    }

    /* 3. Equity calculation */
    printf("3. Equity Calculation\n");
    printf("---------------------\n");

    pe_equity_result_t equity_result;

    /* AA vs KK preflop */
    err = pe_calculate_equity_holdem(handle, "AhAd", "KhKd", "", &equity_result);
    if (err == PE_OK) {
        printf("   AA vs KK preflop:\n");
        printf("   Equity: %.2f%%\n", equity_result.equity * 100.0);
        printf("   Win: %.2f%%, Tie: %.2f%%, Lose: %.2f%%\n",
               equity_result.win_pct * 100.0,
               equity_result.tie_pct * 100.0,
               equity_result.lose_pct * 100.0);
        printf("   Samples: %llu\n\n", (unsigned long long)equity_result.samples);
    }

    /* AKs vs QQ on flop */
    err = pe_calculate_equity_holdem(handle, "AhKh", "QcQd", "JhTh2s", &equity_result);
    if (err == PE_OK) {
        printf("   AKs vs QQ on JhTh2s:\n");
        printf("   Equity: %.2f%%\n", equity_result.equity * 100.0);
        printf("   Samples: %llu\n\n", (unsigned long long)equity_result.samples);
    }

    /* 4. ICM calculation */
    printf("4. ICM Calculation\n");
    printf("------------------\n");

    pe_icm_handle_t icm = pe_icm_create(handle);
    if (icm) {
        double stacks[] = {1000, 800, 500, 300};
        double payouts[] = {500, 300, 150, 50};
        pe_icm_player_result_t icm_results[4];

        err = pe_icm_calculate(icm, stacks, payouts, 4, icm_results);
        if (err == PE_OK) {
            printf("   4-player SNG ICM:\n");
            printf("   Stacks: 1000, 800, 500, 300\n");
            printf("   Payouts: $500, $300, $150, $50\n\n");

            for (int i = 0; i < 4; i++) {
                printf("   Player %d: Chip%% = %.1f%%, ICM = $%.2f\n",
                       i + 1,
                       icm_results[i].chip_ev * 100.0,
                       icm_results[i].icm_equity);
            }
        }

        pe_icm_free(icm);
    }

    printf("\n========================================\n");
    printf("Demo complete!\n");

    /* Cleanup */
    pe_free(handle);

    return 0;
}
