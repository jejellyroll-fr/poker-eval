/*
 * flop_equity_demo.c - Demonstrate flop equity calculation
 *
 * Usage:
 *   ./flop_equity_demo AhKh "Qh Jh Th"
 */

#include <poker_eval/equity/flop_equity.h>
#include <poker_eval/equity/preflop_equity.h>
#include <poker_eval/deck/deck_std.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Helper to parse card string */
static int parse_card(const char *str, int *card)
{
    return StdDeck_stringToCard((char*)str, card);
}

static StdDeck_CardMask parse_cards(const char *str)
{
    StdDeck_CardMask mask;
    StdDeck_CardMask_RESET(mask);

    char buf[128];
    strncpy(buf, str, sizeof(buf)-1);
    buf[sizeof(buf)-1] = '\0';

    char *token = strtok(buf, " ");
    while (token) {
        int card;
        if (parse_card(token, &card)) {
            StdDeck_CardMask_SET(mask, card);
        }
        token = strtok(NULL, " ");
    }
    return mask;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        printf("Usage: %s <pocket> <flop>\n", argv[0]);
        printf("\nExamples:\n");
        printf("  %s \"Ah Kh\" \"Qh Jh Th\"\n", argv[0]);
        printf("  %s \"As Ks\" \"Kd 7h 2c\"\n", argv[0]);
        return 1;
    }

    const char *pocket_str = argv[1];
    const char *flop_str = argv[2];

    printf("=== Flop Equity Demo ===\n\n");
    printf("Pocket: %s\n", pocket_str);
    printf("Flop: %s\n\n", flop_str);

    StdDeck_CardMask pocket = parse_cards(pocket_str);
    StdDeck_CardMask flop = parse_cards(flop_str);

    /* Analyze flop texture */
    flop_analysis_t texture;
    analyze_flop_texture(flop, &texture);

    char texture_str[32];
    flop_texture_to_string(texture.texture, texture_str);

    printf("Flop Texture: %s (score: %d/100)\n", texture_str, texture.texture_score);
    printf("  Monotone: %s\n", texture.is_monotone ? "Yes" : "No");
    printf("  Paired: %s\n", texture.is_paired ? "Yes" : "No");
    printf("  Connected: %s\n", texture.is_connected ? "Yes" : "No");
    printf("\n");

    /* Calculate equity */
    flop_equity_input_t input = {
        .pocket = pocket,
        .flop = flop,
        .n_opponents = 1,
        .n_samples = 0
    };

    flop_equity_result_t result;
    flop_calc_equity(&input, &result);

    printf("=== Equity ===\n");
    printf("Overall equity: %.2f%%\n\n", result.equity * 100);

    /* Display simplified results based on current implementation */
    if (result.prob_flush > 0.0)
        printf("Current hand: Flush (%.1f%%)\n", result.prob_flush * 100);

    printf("=== Draws ===\n");
    printf("Flush draw: %.2f%%\n", result.prob_flush_draw * 100);
    printf("OESD: %.2f%%\n", result.prob_oesd * 100);
    printf("Gutshot: %.2f%%\n", result.prob_gutshot * 100);
    printf("\n");

    printf("=== Improvement Odds ===\n");
    printf("Turn: %.2f%%\n", result.turn_improvement * 100);
    printf("River: %.2f%%\n", result.river_improvement * 100);

    return 0;
}
