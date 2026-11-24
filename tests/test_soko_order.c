/*
 * test_soko_order.c - total-order oracle for the Sökö hand value
 *
 * `pe_soko_describe_hand()` returns both a semantic key (category rank and the
 * tie-break ranks) and the comparable `value` the equity engine uses.  The
 * exhaustive oracle in test_soko_oracle.c pins the category counts and
 * test_soko_game.c pins a handful of comparisons; this file checks the other
 * half of the contract:
 *
 *   - `value` is order-isomorphic to the key over thousands of random hands,
 *     and distinct keys never share a value;
 *   - the four-card tie-break ranks are recomputed here from the raw cards,
 *     independently of the ranking code, so a wrong suited set or a wrong
 *     four-straight high card cannot hide behind a consistent packing.
 */

#include "unity.h"
#include <stdlib.h> /* qsort */
#include <string.h>

#include <poker_eval/deck/deck_std.h>
#include <poker_eval/games/soko.h>

void setUp(void) {}
void tearDown(void) {}

#define POOL_SIZE 3000

static unsigned long long rng_state = 0x243F6A8885A308D3ULL;

static unsigned int next_random(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (unsigned int)(rng_state >> 32);
}

static StdDeck_CardMask random_hand(void) {
    StdDeck_CardMask hand;

    StdDeck_CardMask_RESET(hand);
    while (StdDeck_numCards(hand) < 5)
        StdDeck_CardMask_SET(hand, (int)(next_random() % 52u));
    return hand;
}

/* Lexicographic comparison of the semantic key: category first, then the
 * tie-break ranks in significance order. */
static int compare_keys(const pe_soko_hand_description_t *a,
                        const pe_soko_hand_description_t *b) {
    int i, n;

    if (a->category_rank != b->category_rank)
        return a->category_rank < b->category_rank ? -1 : 1;
    n = a->num_tiebreak_ranks < b->num_tiebreak_ranks
            ? a->num_tiebreak_ranks
            : b->num_tiebreak_ranks;
    for (i = 0; i < n; ++i) {
        if (a->tiebreak_ranks[i] != b->tiebreak_ranks[i])
            return a->tiebreak_ranks[i] < b->tiebreak_ranks[i] ? -1 : 1;
    }
    if (a->num_tiebreak_ranks != b->num_tiebreak_ranks)
        return a->num_tiebreak_ranks < b->num_tiebreak_ranks ? -1 : 1;
    return 0;
}

static int sort_by_key(const void *pa, const void *pb) {
    return compare_keys((const pe_soko_hand_description_t *)pa,
                        (const pe_soko_hand_description_t *)pb);
}

/* `value` must sort exactly like the key, and distinct keys must be distinct
 * values: the HandVal has room for the category and every tie-break rank. */
static void test_value_is_order_isomorphic_to_the_key(void) {
    static pe_soko_hand_description_t descs[POOL_SIZE];
    int i;
    int four_flush = 0;
    int four_straight = 0;

    for (i = 0; i < POOL_SIZE; ++i) {
        TEST_ASSERT_EQUAL_INT(PE_STATUS_OK,
                              pe_soko_describe_hand(random_hand(), &descs[i]));
        if (descs[i].category == PE_CAT_FOUR_CARD_FLUSH)
            four_flush++;
        if (descs[i].category == PE_CAT_FOUR_CARD_STRAIGHT)
            four_straight++;
    }
    TEST_ASSERT_TRUE_MESSAGE(four_flush >= 20,
                             "random pool lacks four-card flushes");
    TEST_ASSERT_TRUE_MESSAGE(four_straight >= 20,
                             "random pool lacks four-card straights");

    qsort(descs, POOL_SIZE, sizeof(descs[0]), sort_by_key);
    for (i = 1; i < POOL_SIZE; ++i) {
        int cmp = compare_keys(&descs[i - 1], &descs[i]);

        TEST_ASSERT_TRUE_MESSAGE(cmp <= 0, "pool is not sorted by key");
        if (cmp == 0) {
            /* Same key (e.g. the same four suited ranks in another suit):
             * the off-suit card must not change the value. */
            TEST_ASSERT_EQUAL_UINT(descs[i - 1].value, descs[i].value);
        } else {
            TEST_ASSERT_TRUE_MESSAGE(descs[i - 1].value < descs[i].value,
                                     "distinct keys share or invert a value");
        }
    }
}

/* Rank of every card of the suit that appears exactly four times. */
static int suited_ranks_desc(StdDeck_CardMask hand, int ranks[5]) {
    int counts[4] = {0, 0, 0, 0};
    int suit = -1;
    int card, i, n = 0;

    for (card = 0; card < StdDeck_N_CARDS; ++card) {
        if (StdDeck_CardMask_CARD_IS_SET(hand, card))
            counts[StdDeck_SUIT(card)]++;
    }
    for (i = 0; i < 4; ++i) {
        if (counts[i] == 4)
            suit = i;
    }
    if (suit < 0)
        return 0;
    for (card = 0; card < StdDeck_N_CARDS; ++card) {
        if (StdDeck_CardMask_CARD_IS_SET(hand, card) &&
            StdDeck_SUIT(card) == suit)
            ranks[n++] = StdDeck_RANK(card) + 2;
    }
    /* Insertion sort, descending: four entries at most. */
    for (i = 1; i < n; ++i) {
        int value = ranks[i];
        int j = i - 1;
        while (j >= 0 && ranks[j] < value) {
            ranks[j + 1] = ranks[j];
            --j;
        }
        ranks[j + 1] = value;
    }
    return n;
}

static void check_four_flush(const pe_soko_hand_description_t *desc,
                             StdDeck_CardMask hand) {
    int ranks[5];
    int n = suited_ranks_desc(hand, ranks);
    int i;

    TEST_ASSERT_EQUAL_INT_MESSAGE(4, n, "four-card flush without four suited cards");
    TEST_ASSERT_EQUAL_INT(4, desc->num_tiebreak_ranks);
    for (i = 0; i < 4; ++i)
        TEST_ASSERT_EQUAL_INT(ranks[i], desc->tiebreak_ranks[i]);
}

static int five_card_straight_high(const int present[15]) {
    int h;

    for (h = 14; h >= 5; --h) {
        if (present[h] && present[h - 1] && present[h - 2] &&
            present[h - 3] && present[h - 4])
            return h;
    }
    /* A-2-3-4-5, the wheel, is a five-high straight. */
    if (present[14] && present[2] && present[3] && present[4] && present[5])
        return 5;
    return 0;
}

static int four_card_straight_high(const int present[15]) {
    int h;

    for (h = 14; h >= 5; --h) {
        if (present[h] && present[h - 1] && present[h - 2] &&
            present[h - 3])
            return h;
    }
    /* A-2-3-4 plays as four-high; the ace is low inside the run. */
    if (present[14] && present[2] && present[3] && present[4])
        return 4;
    return 0;
}

static void check_four_straight(const pe_soko_hand_description_t *desc,
                                StdDeck_CardMask hand) {
    int present[15] = {0};
    int card;
    int high;

    for (card = 0; card < StdDeck_N_CARDS; ++card) {
        if (StdDeck_CardMask_CARD_IS_SET(hand, card))
            present[StdDeck_RANK(card) + 2] = 1;
    }
    /* A five-card straight would have been classified as a straight. */
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, five_card_straight_high(present),
                                  "straight classified as four-card straight");
    high = four_card_straight_high(present);
    TEST_ASSERT_TRUE_MESSAGE(high >= 4, "four-card straight without a four-run");
    TEST_ASSERT_EQUAL_INT(1, desc->num_tiebreak_ranks);
    TEST_ASSERT_EQUAL_INT(high, desc->tiebreak_ranks[0]);
}

/* The two Sökö-only tie-breaks are recomputed from the cards themselves. */
static void test_four_card_tiebreaks_come_from_the_cards(void) {
    int i;
    int flushes = 0;
    int straights = 0;

    for (i = 0; i < POOL_SIZE; ++i) {
        StdDeck_CardMask hand = random_hand();
        pe_soko_hand_description_t desc;

        TEST_ASSERT_EQUAL_INT(PE_STATUS_OK,
                              pe_soko_describe_hand(hand, &desc));
        if (desc.category == PE_CAT_FOUR_CARD_FLUSH) {
            check_four_flush(&desc, hand);
            flushes++;
        } else if (desc.category == PE_CAT_FOUR_CARD_STRAIGHT) {
            check_four_straight(&desc, hand);
            straights++;
        }
    }
    TEST_ASSERT_TRUE(flushes >= 20);
    TEST_ASSERT_TRUE(straights >= 20);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_value_is_order_isomorphic_to_the_key);
    RUN_TEST(test_four_card_tiebreaks_come_from_the_cards);
    return UNITY_END();
}
