#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/games/triple_draw.h>
#include <poker_eval/games/eval_low.h> /* For pe_eval_low_a5 logic via triple_draw */

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

static void test_basic_a5_hands(void)
{
    printf("Testing basic A-5 Triple Draw hands...\n");

    /* Best hand in A-5: A-2-3-4-5 (The Wheel) */
    /* Straights and Flushes DO NOT count against the player in A-5 Lowball */
    StdDeck_CardMask wheel = create_hand("Ah", "2s", "3d", "4c", "5h");
    LowHandVal val_wheel = TripleDraw_A5_EVAL_N(wheel, 5);

    /* Second best: A-2-3-4-6 */
    StdDeck_CardMask six_low = create_hand("Ah", "2s", "3d", "4c", "6h");
    LowHandVal val_six = TripleDraw_A5_EVAL_N(six_low, 5);

    /* Worse: A-2-3-4-K */
    StdDeck_CardMask king_low = create_hand("Ah", "2s", "3d", "4c", "Kh");
    LowHandVal val_king = TripleDraw_A5_EVAL_N(king_low, 5);

    /* Check ordering: Lower value is better */
    assert(val_wheel < val_six);
    assert(val_six < val_king);

    /* Check Straights: 5-4-3-2-A is a straight, but in A-5 lowball it ignores straights */
    /* So it should still be the best hand (value same as unsuited wheel) */
    /* Actually, wheel IS a straight. In 2-7 it's bad. In A-5 it's good. */

    /* Check Flushes: A-2-3-4-5 suited */
    StdDeck_CardMask suited_wheel = create_hand("As", "2s", "3s", "4s", "5s");
    LowHandVal val_suited_wheel = TripleDraw_A5_EVAL_N(suited_wheel, 5);

    /* Suited wheel should have same value as offsuit wheel because flushes are ignored */
    assert(val_suited_wheel == val_wheel);

    printf("✓ A-5 basic hands and rules passed\n");
    (void)val_wheel;
    (void)val_six;
    (void)val_king;
    (void)val_suited_wheel;
}

int main(void)
{
    test_basic_a5_hands();
    printf("All A-5 Triple Draw tests passed!\n");
    return 0;
}
