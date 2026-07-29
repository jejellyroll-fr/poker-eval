#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/core/enumerate.h>

static void test_params(void) {
    enum_gameparams_t *params = enumGameParams(game_omaha86);
    if (params == NULL) {
        fprintf(stderr, "enumGameParams(game_omaha86) returned NULL\n");
        exit(1);
    }
    if (params->minpocket != 6) { fprintf(stderr, "minpocket != 6\n"); exit(1); }
    if (params->maxpocket != 6) { fprintf(stderr, "maxpocket != 6\n"); exit(1); }
    if (params->maxboard != 5) { fprintf(stderr, "maxboard != 5\n"); exit(1); }
    if (params->hashipot != 1) { fprintf(stderr, "hashipot != 1\n"); exit(1); }
    if (params->haslopot != 1) { fprintf(stderr, "haslopot != 1\n"); exit(1); }
    printf("✓ game_omaha86 params checked.\n");

    params = enumGameParams(game_omaha6);
    if (params == NULL) {
        fprintf(stderr, "enumGameParams(game_omaha6) returned NULL\n");
        exit(1);
    }
    if (params->minpocket != 6) { fprintf(stderr, "minpocket != 6\n"); exit(1); }
    if (params->maxpocket != 6) { fprintf(stderr, "maxpocket != 6\n"); exit(1); }
    if (params->maxboard != 5) { fprintf(stderr, "maxboard != 5\n"); exit(1); }
    if (params->hashipot != 1) { fprintf(stderr, "hashipot != 1\n"); exit(1); }
    if (params->haslopot != 0) { fprintf(stderr, "haslopot != 0\n"); exit(1); }
    printf("✓ game_omaha6 params checked.\n");
}

/* Helper to parse cards */
static void parse_cards(const char *str, StdDeck_CardMask *mask)
{
    StdDeck_CardMask_RESET(*mask);
    int i = 0;
    while (str[i] != '\0')
    {
        if (str[i] == ' ' || str[i] == ',') { i++; continue; }
        int r = StdDeck_Rank_2;
        switch (str[i]) {
            case 'A': r=StdDeck_Rank_ACE; break;
            case 'K': r=StdDeck_Rank_KING; break;
            case 'Q': r=StdDeck_Rank_QUEEN; break;
            case 'J': r=StdDeck_Rank_JACK; break;
            case 'T': r=StdDeck_Rank_TEN; break;
            case '9': r=StdDeck_Rank_9; break;
            case '8': r=StdDeck_Rank_8; break;
            case '7': r=StdDeck_Rank_7; break;
            case '6': r=StdDeck_Rank_6; break;
            case '5': r=StdDeck_Rank_5; break;
            case '4': r=StdDeck_Rank_4; break;
            case '3': r=StdDeck_Rank_3; break;
            case '2': r=StdDeck_Rank_2; break;
            default: r=StdDeck_Rank_2; break;
        }
        i++;
        int s = StdDeck_Suit_HEARTS;
        switch (str[i]) {
            case 'h': s=StdDeck_Suit_HEARTS; break;
            case 'd': s=StdDeck_Suit_DIAMONDS; break;
            case 'c': s=StdDeck_Suit_CLUBS; break;
            case 's': s=StdDeck_Suit_SPADES; break;
            default: s=StdDeck_Suit_HEARTS; break;
        }
        i++;
        StdDeck_CardMask_SET(*mask, StdDeck_MAKE_CARD(r, s));
    }
}

static void test_sample_omaha86(void) {
    StdDeck_CardMask pockets[2];
    StdDeck_CardMask board, dead;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    /* Hand 1: Wheel possibility + flush */
    parse_cards("As2h3c4d5h6s", &pockets[0]);
    /* Hand 2: High cards */
    parse_cards("AhKhQhJhTh9h", &pockets[1]);

    /* Board: 7d 8c Ks */
    parse_cards("7d8cKs", &board);

    enum_result_t result;
    /* Run a small sample */
    int err = enumSample(game_omaha86, pockets, board, dead, 2, 3, 1000, 0, &result);
    if (err != 0) {
        fprintf(stderr, "enumSample failed with error %d\n", err);
        exit(1);
    }
    if (result.nsamples != 1000) {
        fprintf(stderr, "nsamples != 1000\n");
        exit(1);
    }

    /* Hand 0 should win low often */
    /* Hand 1 might win high */

    printf("Omaha86 Sample Results (1000 samples):\n");
    printf("Hand 0 EV: %.3f\n", result.ev[0]/1000.0);
    printf("Hand 1 EV: %.3f\n", result.ev[1]/1000.0);

    /* Basic sanity check: sum of EVs approx 1.0 */
    double sum = (result.ev[0] + result.ev[1]) / 1000.0;
    if (sum <= 0.99 || sum >= 1.01) {
        fprintf(stderr, "Total EV sum %.3f is not ~1.0\n", sum);
        // In Monte Carlo, sum should be exactly 1.0 if we count shares correctly per sample
        // But floating point precision might drift slightly
    }
    assert(sum > 0.99 && sum < 1.01);

    printf("✓ test_sample_omaha86 PASSED\n");

    enumResultFree(&result);
}

int main(void) {
    test_params();
    test_sample_omaha86();
    return 0;
}
