/*
 * test_handval_tostring_bounds.c - the *_toString_n variants must respect size
 *
 * Guards the bounded formatting introduced with those variants: nothing is
 * written past the requested size, the result stays NUL-terminated, and the
 * returned length describes what was actually written rather than what the
 * format would have produced.
 */

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/games/rules_badugi.h>
#include <poker_eval/games/rules_manila.h>
#include <stdio.h>
#include <string.h>

#define GUARD 16
#define SLACK 64

static int failures = 0;

static void fail(const char *test, size_t size, const char *detail) {
    printf("FAIL: %s size=%zu - %s\n", test, size, detail);
    failures++;
}

/*
 * Call fn into a guarded buffer for every size from 1 to SLACK and check the
 * properties above. raw is filled with '#', so any stray byte shows up.
 *
 * returns_length distinguishes the two contracts in this family: the Badugi
 * variants return how many bytes they wrote, while ManilaRules returns a
 * status (0 on success). Only the former can be checked against the buffer.
 */
static void check_all_sizes(const char *test,
                            int (*fn)(HandVal, char *, size_t),
                            HandVal handval,
                            int returns_length) {
    for (size_t size = 1; size <= SLACK; size++) {
        char raw[GUARD + SLACK + GUARD];
        memset(raw, '#', sizeof(raw));

        char *buf = raw + GUARD;
        int len = fn(handval, buf, size);

        for (int i = 0; i < GUARD; i++) {
            if (raw[i] != '#') {
                fail(test, size, "wrote before the buffer");
                break;
            }
        }
        for (size_t i = size; i < SLACK; i++) {
            if (buf[i] != '#') {
                fail(test, size, "wrote past the requested size");
                break;
            }
        }
        if (strnlen(buf, size) >= size) {
            fail(test, size, "result is not NUL-terminated");
        }
        if (returns_length) {
            if (len < 0 || (size_t)len >= size) {
                char detail[64];
                snprintf(detail, sizeof(detail), "returned length %d is outside the buffer", len);
                fail(test, size, detail);
            } else if (buf[len] != '\0') {
                fail(test, size, "returned length does not point at the terminator");
            }
        }
    }
}

int main(void) {
    printf("=== bounded *_toString_n tests ===\n");

    /* A made hand and a value that falls in the "Invalid" branch: the short
     * literal path had a hardcoded return that ignored the buffer size. */
    const HandVal values[] = { 0x07040302, 0x00000000, 0x7fffffff };

    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        check_all_sizes("BadugiRules_HandVal_toString_n",
                        BadugiRules_HandVal_toString_n, values[i], 1);
        check_all_sizes("BadaceyRules_HandVal_toString_n",
                        BadaceyRules_HandVal_toString_n, values[i], 1);
        check_all_sizes("BadeucyRules_HandVal_toString_n",
                        BadeucyRules_HandVal_toString_n, values[i], 1);
        check_all_sizes("ManilaRules_HandVal_toString_n",
                        ManilaRules_HandVal_toString_n, values[i], 0);
    }

    if (failures > 0) {
        printf("\n%d failure(s)\n", failures);
        return 1;
    }

    printf("\nAll sizes respected\n");
    return 0;
}
