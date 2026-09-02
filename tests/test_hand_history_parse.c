#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/*
 * Regression tests for the hand-history importer parsing helpers.
 * The importer historically parsed multi-digit amounts as their last digit
 * ("bets $10" -> 0), kept the trailing ':' in hand ids and player names,
 * and its CLI smoke test asserted nothing, so each defect is pinned here.
 */
#include "../tools/hand_history_parse.h"

static void check_amount(const char *text, double expected)
{
    double amount = -1.0;
    assert(hh_parse_amount(text, &amount) == 1);
    assert(fabs(amount - expected) < 1e-9);
}

static void test_parse_amount(void)
{
    /* Multi-digit amounts must parse in full (was: last digit only). */
    check_amount("bets $10", 10.0);
    check_amount("bets $14", 14.0);
    check_amount("bets $60", 60.0);
    check_amount("raises $25 to $37", 37.0);
    check_amount(" raises $12 to $37", 37.0);
    /* Single-digit and decimal amounts keep working. */
    check_amount("bets $4", 4.0);
    check_amount("calls $2", 2.0);
    check_amount("posts small blind $0.50", 0.50);
    check_amount("raises $2.50 to $3.75", 3.75);
    /* The last amount wins when several appear. */
    check_amount("raises $2 to $3", 3.0);
    /* No digits at all. */
    {
        double amount = 0.0;
        assert(hh_parse_amount("checks", &amount) == 0);
        assert(hh_parse_amount("", &amount) == 0);
        assert(hh_parse_amount(NULL, &amount) == 0);
    }
}

static void test_extract_hand_id(void)
{
    char id[64];
    /* The trailing ':' separator is not part of the id (was kept). */
    hh_extract_hand_id(
        "PokerStars Hand #987654321: Hold'em No Limit ($0.50/$1.00 USD)", id,
        sizeof(id));
    assert(strcmp(id, "987654321") == 0);
    /* Whitespace-terminated ids still work. */
    hh_extract_hand_id("Hand #42 something", id, sizeof(id));
    assert(strcmp(id, "42") == 0);
    /* No '#' leaves an empty id. */
    hh_extract_hand_id("*** FLOP *** [Ah 7c 2d]", id, sizeof(id));
    assert(id[0] == '\0');
    /* Small buffers stay NUL-terminated. */
    {
        char tiny[4];
        hh_extract_hand_id("Hand #987654321:", tiny, sizeof(tiny));
        assert(strcmp(tiny, "987") == 0);
        hh_extract_hand_id("Hand #987654321:", tiny, 0);
    }
}

static void test_strip_trailing_colon(void)
{
    char player[64];
    /* Action-line prefixes lose their ':' (was kept, e.g. "PlayerB:"). */
    snprintf(player, sizeof(player), "%s", "PlayerB:");
    hh_strip_trailing_colon(player, sizeof(player));
    assert(strcmp(player, "PlayerB") == 0);
    /* Names without a colon are untouched. */
    snprintf(player, sizeof(player), "%s", "Hero");
    hh_strip_trailing_colon(player, sizeof(player));
    assert(strcmp(player, "Hero") == 0);
    /* Empty input stays empty. */
    player[0] = '\0';
    hh_strip_trailing_colon(player, sizeof(player));
    assert(player[0] == '\0');
}

int main(void)
{
    test_parse_amount();
    test_extract_hand_id();
    test_strip_trailing_colon();
    puts("hand history parse tests passed");
    return 0;
}
