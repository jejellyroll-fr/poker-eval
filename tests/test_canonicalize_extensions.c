#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <poker_eval/equity/canonicalize.h>
#include <poker_eval/deck/deck_std.h>

static void test_canonical_key_generic(void) {
    /* Test 5 cards (board) */
    StdDeck_CardMask board;
    StdDeck_CardMask_RESET(board);
    /* As Ks Qs Js Ts -> Royal Flush Spades */
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_SPADES));

    char key5[100];
    if (pe_canonical_key(board, 5, key5, sizeof(key5)) != 0) {
        fprintf(stderr, "pe_canonical_key failed for board\n");
        assert(0);
    }
    /* Should be "AaKaQaJaTa" or similar (suit 'a') */
    printf("5-card Royal: %s\n", key5);
    assert(strcmp(key5, "AaKaQaJaTa") == 0);

    /* Test 6 cards (PLO6 pocket) */
    /* As Ah Ks Kh Qs Qh */
    StdDeck_CardMask pocket6;
    StdDeck_CardMask_RESET(pocket6);
    StdDeck_CardMask_SET(pocket6, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pocket6, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pocket6, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pocket6, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pocket6, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pocket6, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS));

    char key6[100];
    if (pe_canonical_key(pocket6, 6, key6, sizeof(key6)) != 0) {
        fprintf(stderr, "pe_canonical_key failed for pocket6\n");
        assert(0);
    }
    /* Sorted ranks: A A K K Q Q. Suits: S H S H S H. */
    /* First suit (S) becomes 'a'. Second suit (H) becomes 'b'. */
    /* Aa Ab Ka Kb Qa Qb */
    printf("6-card AA KK QQ: %s\n", key6);
    assert(strcmp(key6, "AaAbKaKbQaQb") == 0);
}

int main(void) {
    test_canonical_key_generic();
    printf("All tests passed!\n");
    return 0;
}
