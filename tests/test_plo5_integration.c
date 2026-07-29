#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/core/enumerate.h>

static void test_params(void) {
    enum_gameparams_t *params = enumGameParams(game_omaha85);
    if (params == NULL) {
        fprintf(stderr, "enumGameParams(game_omaha85) returned NULL\n");
        exit(1);
    }
    if (params->minpocket != 5) { fprintf(stderr, "minpocket != 5\n"); exit(1); }
    if (params->maxpocket != 5) { fprintf(stderr, "maxpocket != 5\n"); exit(1); }
    if (params->maxboard != 5) { fprintf(stderr, "maxboard != 5\n"); exit(1); }
    if (params->hashipot != 1) { fprintf(stderr, "hashipot != 1\n"); exit(1); }
    if (params->haslopot != 1) { fprintf(stderr, "haslopot != 1\n"); exit(1); }
    printf("✓ game_omaha85 params checked.\n");
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

static void test_sample_omaha85(void) {
    StdDeck_CardMask pockets[2];
    StdDeck_CardMask board, dead;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    /* Hand 0: Wheel possibility (A,5) + trash */
    /* Cards: Ad 5d Td Th Ts */
    parse_cards("Ad5dTdThTs", &pockets[0]);

    /* Hand 1: High straight/flush possibility + no low */
    /* Cards: Qc Jc 9c 8c 7c */
    parse_cards("QcJc9c8c7c", &pockets[1]);

    /* Board: 2c 3c 4c Kh Kd */
    /* Board provides 2,3,4.
       Hand 0 uses A,5 -> A-2-3-4-5 Low (Nut Low).
       Hand 1 has Flush (Qc Jc 9c + 2c 3c 4c -> wait, needs 3 from board... 2c,3c,4c are 3 cards.
       Hand 1 uses Qc, Jc + 2c,3c,4c = Flush Q-high.
    */
    parse_cards("2c3c4cKhKd", &board);

    enum_result_t result;
    /* Run exhaustive (single sample since board is full) */
    /* Note: enumExhaustive does not take niter argument */
    int err = enumExhaustive(game_omaha85, pockets, board, dead, 2, 5, 0, &result);
    if (err != 0) {
        fprintf(stderr, "enumExhaustive failed with error %d\n", err);
        exit(1);
    }

    printf("Omaha85 Fixed Results:\n");
    /* result.nsamples should be 1 */
    printf("Hand 0 EV: %.3f (Expected ~0.5 for Low)\n", result.ev[0]);
    printf("Hand 1 EV: %.3f (Expected ~0.5 for High)\n", result.ev[1]);

    /* Hand 0 should win Low (0.5 pot) */
    /* Hand 1 should win High (0.5 pot) */

    /* Verify split pot logic */
    /* EV sum should be ~1.0 */
    double sum = (result.ev[0] + result.ev[1]);
    if (sum <= 0.99 || sum >= 1.01) {
        fprintf(stderr, "Total EV sum %.3f is not ~1.0\n", sum);
    }
    assert(sum > 0.99 && sum < 1.01);

    /* Verify specific wins */
    /* nwinlo[0] should be 1, nwinhi[1] should be 1 */
    if (result.nwinlo[0] != 1) fprintf(stderr, "Hand 0 did not win Low\n");
    if (result.nwinhi[1] != 1) fprintf(stderr, "Hand 1 did not win High\n");

    printf("✓ test_sample_omaha85 PASSED\n");

    enumResultFree(&result);
}

int main(void) {
    test_params();
    test_sample_omaha85();
    return 0;
}
