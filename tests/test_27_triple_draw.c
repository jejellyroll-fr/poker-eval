#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/games/triple_draw.h>
#include <poker_eval/games/eval_low.h>

/* Helper function to create a hand from card strings */
static int parse_card_safe(const char *str, int *card)
{
    char buf[16];
    strncpy(buf, str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    return StdDeck_stringToCard(buf, card);
}

static StdDeck_CardMask create_hand(const char *c1, const char *c2, const char *c3, const char *c4, const char *c5)
{
    StdDeck_CardMask hand;
    int card;

    StdDeck_CardMask_RESET(hand);

    if (parse_card_safe(c1, &card))
        StdDeck_CardMask_SET(hand, card);
    if (parse_card_safe(c2, &card))
        StdDeck_CardMask_SET(hand, card);
    if (parse_card_safe(c3, &card))
        StdDeck_CardMask_SET(hand, card);
    if (parse_card_safe(c4, &card))
        StdDeck_CardMask_SET(hand, card);
    if (parse_card_safe(c5, &card))
        StdDeck_CardMask_SET(hand, card);

    return hand;
}

/* Test that straights count against the player in 2-7 Triple Draw */
static void test_straights_count_against(void)
{
    printf("Testing straights in 2-7 Triple Draw...\n");

    /* 2-3-4-5-6 (straight) is a bad hand in 2-7 */
    StdDeck_CardMask straight = create_hand("2h", "3s", "4d", "5c", "6h");
    LowHandVal val_straight = TripleDraw_27_EVAL_N(straight, 5);

    /* 2-3-4-5-7 (no straight) is a good hand (The Wheel) */
    StdDeck_CardMask wheel = create_hand("2h", "3s", "4d", "5c", "7h");
    LowHandVal val_wheel = TripleDraw_27_EVAL_N(wheel, 5);

    /* 2-3-4-5-8 (no straight) is worse than wheel but better than straight */
    StdDeck_CardMask no_straight = create_hand("2h", "3s", "4d", "5c", "8h");
    LowHandVal val_no_straight = TripleDraw_27_EVAL_N(no_straight, 5);

    /* In 2-7, lower value is better. But straight makes it a high hand (bad).
       Usually eval returns large value for bad hands?
       Let's check ordering. */

    /* Wheel should be better (lower?) than 8-high */
    assert(val_wheel < val_no_straight);

    /* Straight counts as a high hand, so it should be much worse (higher value) than 8-high */
    assert(val_no_straight < val_straight);

    printf("✓ Straights count against passed\n");
    (void)val_wheel;
    (void)val_no_straight;
    (void)val_straight;
}

/* Test that flushes count against the player in 2-7 Triple Draw */
static void test_flushes_count_against(void)
{
    printf("Testing flushes in 2-7 Triple Draw...\n");

    /* 2-3-4-5-7 suited (flush) is bad */
    StdDeck_CardMask flush = create_hand("2s", "3s", "4s", "5s", "7s");
    LowHandVal val_flush = TripleDraw_27_EVAL_N(flush, 5);

    /* 2-3-4-5-7 offsuit is good */
    StdDeck_CardMask no_flush = create_hand("2s", "3d", "4c", "5h", "7s");
    LowHandVal val_no_flush = TripleDraw_27_EVAL_N(no_flush, 5);

    /* Flush should be worse (higher value) than no flush */
    assert(val_no_flush < val_flush);

    printf("✓ Flushes count against passed\n");
    (void)val_flush;
    (void)val_no_flush;
}

static void test_utility_functions(void)
{
    printf("Testing utility functions...\n");
    int card1, card2;
    char buf1[] = "7h";
    char buf2[] = "5s";

    StdDeck_stringToCard(buf1, &card1);
    StdDeck_stringToCard(buf2, &card2);

    assert(card1 != card2);
    printf("✓ Utility functions passed\n");
}

static void test_basic_enumeration(void)
{
    printf("Testing basic enumeration...\n");

    #ifdef game_27_triple_draw
    StdDeck_CardMask pockets[2];
    StdDeck_CardMask board, dead;
    enum_result_t result;
    int card;
    char buf[4];

    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    /* Player 1: 7-5-4-3-2 (Wheel) */
    StdDeck_CardMask_RESET(pockets[0]);
    strcpy(buf, "7h"); StdDeck_stringToCard(buf, &card); StdDeck_CardMask_SET(pockets[0], card);
    strcpy(buf, "5s"); StdDeck_stringToCard(buf, &card); StdDeck_CardMask_SET(pockets[0], card);
    strcpy(buf, "4d"); StdDeck_stringToCard(buf, &card); StdDeck_CardMask_SET(pockets[0], card);
    strcpy(buf, "3c"); StdDeck_stringToCard(buf, &card); StdDeck_CardMask_SET(pockets[0], card);
    strcpy(buf, "2h"); StdDeck_stringToCard(buf, &card); StdDeck_CardMask_SET(pockets[0], card);

    /* Player 2: A-K-Q-J-9 (High cards - very bad in 2-7) */
    StdDeck_CardMask_RESET(pockets[1]);
    strcpy(buf, "Ah"); StdDeck_stringToCard(buf, &card); StdDeck_CardMask_SET(pockets[1], card);
    strcpy(buf, "Ks"); StdDeck_stringToCard(buf, &card); StdDeck_CardMask_SET(pockets[1], card);
    strcpy(buf, "Qd"); StdDeck_stringToCard(buf, &card); StdDeck_CardMask_SET(pockets[1], card);
    strcpy(buf, "Jc"); StdDeck_stringToCard(buf, &card); StdDeck_CardMask_SET(pockets[1], card);
    strcpy(buf, "9h"); StdDeck_stringToCard(buf, &card); StdDeck_CardMask_SET(pockets[1], card);

    /* Mark dead cards */
    StdDeck_CardMask_OR(dead, dead, pockets[0]);
    StdDeck_CardMask_OR(dead, dead, pockets[1]);

    /* Run enumeration */
    int ret = enumSample(game_27_triple_draw, pockets, board, dead, 2, 0, 1000, 0, &result);

    assert(ret == 0);
    /* EV should be 1.0 for player 0 */
    assert(result.ev[0] > 990.0); /* 99% of samples */

    printf("✓ Basic enumeration passed\n");
    (void)ret;
    (void)board; /* In 2-7 draw, board is typically empty/unused in simple tests */
    #else
    printf("! Skipping enumeration test: game_27_triple_draw not defined\n");
    #endif
}

int main(void)
{
    test_straights_count_against();
    test_flushes_count_against();
    test_utility_functions();
    test_basic_enumeration();
    printf("All 2-7 Triple Draw tests passed!\n");
    return 0;
}
