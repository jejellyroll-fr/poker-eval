/*
 * test_canonical_regression.c - Regression corpus for canonical 5-card evaluator
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <poker_eval/core/canonical_5card.h>
#include <poker_eval/core/modern_cardmask.h>

static hand_class_t parse_class(const char* s) {
    if (!s) return HAND_MAX;
    if (strcmp(s, "HAND_HIGH_CARD") == 0) return HAND_HIGH_CARD;
    if (strcmp(s, "HAND_PAIR") == 0) return HAND_PAIR;
    if (strcmp(s, "HAND_TWO_PAIR") == 0) return HAND_TWO_PAIR;
    if (strcmp(s, "HAND_THREE_KIND") == 0) return HAND_THREE_KIND;
    if (strcmp(s, "HAND_STRAIGHT") == 0) return HAND_STRAIGHT;
    if (strcmp(s, "HAND_FLUSH") == 0) return HAND_FLUSH;
    if (strcmp(s, "HAND_FULL_HOUSE") == 0) return HAND_FULL_HOUSE;
    if (strcmp(s, "HAND_FOUR_KIND") == 0) return HAND_FOUR_KIND;
    if (strcmp(s, "HAND_STRAIGHT_FLUSH") == 0) return HAND_STRAIGHT_FLUSH;
    return HAND_MAX;
}

static int run_corpus(const char* path, int* out_checked) {
    FILE* f = fopen(path, "r");
    if (!f) return 0;
    char line[256];
    int checked = 0;
    while (fgets(line, sizeof(line), f)) {
        /* Skip comments/empty lines */
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\0') continue;

        /* Split on ';' */
        char* semi = strchr(p, ';');
        if (!semi) continue;
        *semi = '\0';
        char* cards = p;
        char* cls = semi + 1;

        /* Trim spaces */
        while (*cls == ' ' || *cls == '\t') cls++;
        /* Remove comments starting with # */
        char* comment = strchr(cls, '#');
        if (comment) *comment = '\0';
        /* Trim trailing spaces */
        char* end = cls + strlen(cls);
        while (end > cls && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t')) { end--; }
        *end = '\0';

        hand_class_t expected = parse_class(cls);
        assert(expected != HAND_MAX);

        mask_t hand = string_to_mask(cards);
        assert(mask_popcount(hand) == 5);

        uint32_t evalv = evaluate_5card_canonical(hand);
        hand_class_t got = eval_get_hand_class(evalv);

        if (got != expected) {
            char buf[128];
            mask_to_string(hand, buf, sizeof(buf));
            printf("Mismatch: %s -> got=%s expected=%s (eval=%u)\n",
                   buf, eval_hand_class_name(got), eval_hand_class_name(expected), evalv);
        }
        assert(got == expected);
        checked++;
    }
    fclose(f);
    if (out_checked) *out_checked = checked;
    return 1;
}

int main(void) {
    /* Try to open from build/tests/data/ first (CMake copies there) */
    int checked = 0;
    if (!run_corpus("data/canonical_regression.txt", &checked)) {
        /* Fallback to source tree */
        if (!run_corpus("../tests/data/canonical_regression.txt", &checked)) {
            fprintf(stderr, "Failed to open canonical_regression.txt\n");
            return 1;
        }
    }
    printf("Canonical regression corpus: %d cases validated.\n", checked);
    return 0;
}

