#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/games/rules_omaha5.h>
#include <poker_eval/deck/deck_std.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Helper to parse card string to mask */
static StdDeck_CardMask parse_hand(const char *str) {
    StdDeck_CardMask mask;
    int card;
    StdDeck_CardMask_RESET(mask);
    char *dup = strdup(str);
    char *token = strtok(dup, " ");
    while (token != NULL) {
        if (StdDeck_stringToCard(token, &card)) {
            StdDeck_CardMask_SET(mask, card);
        }
        token = strtok(NULL, " ");
    }
    free(dup);
    return mask;
}

static int run_test_omaha5(void) {
    printf("Testing Omaha 5 (PLO5)...\n");

    /* Case 1: Simple PLO5 High evaluation check */
    /* Board: As Ks Qs Js 2d */
    /* Hand 1: Ts 9s 2c 3c 4c (Straight Flush T-A) - uses 2 cards (Ts 9s) */
    /* Hand 2: Ah Kh 2h 3h 4h (Trip Aces? No, needs 2 from hand + 3 from board) */
    /* Let's verify enumeration */

    StdDeck_CardMask board = parse_hand("As Ks Qs Js 2d");
    StdDeck_CardMask pockets[2];
    pockets[0] = parse_hand("Ts 9s 2c 3c 4c");
    pockets[1] = parse_hand("Ah Ad 2h 3h 4h"); /* Trip Aces (As from board, Ah Ad from hand) */

    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);

    enum_result_t result;
    int err = enumExhaustive(game_omaha5, pockets, board, dead, 2, 5, 0, &result);
    if (err != 0) {
        printf("FAIL: enumExhaustive returned error %d\n", err);
        return 1;
    }

    /* Hand 1 should win 100% because Straight Flush > Set */
    if (result.nwinhi[0] != result.nsamples) {
        printf("FAIL: Hand 1 should win %d/%d samples (got %d)\n", result.nsamples, result.nsamples, result.nwinhi[0]);
        return 1;
    }

    printf("PASS: Simple PLO5 exhaustive\n");
    return 0;
}

int main(void) {
    if (run_test_omaha5() != 0) return 1;
    return 0;
}
