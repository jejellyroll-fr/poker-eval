/*
 * Comprehensive unit tests for Stud Range Parser
 *
 * Tests cover:
 *  - Basic patterns: (AA)K, (AK)Q, AKQ triplet
 *  - Pair patterns with various kickers
 *  - Trips (rolled-up): (AA)A
 *  - Specific suits: (AsKh)Qd
 *  - Suited hole cards: (ss)A, (ss)Ks
 *  - Wildcard patterns: (xx)A, (AA)x
 *  - Range intervals: (AA)K-T
 *  - Plus notation: (AA)T+
 *  - Dead card filtering
 *  - Pattern detection: ARP_IsStudPattern()
 *  - pe_range API integration
 *  - Stud top percentage
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include <poker_eval/range/AdvancedRangeParser.h>
#include <poker_eval/range/StudRangeParser.h>
#include <poker_eval/range.h>
#include <poker_eval/deck/deck_std.h>

/* Test helper macros */
#define TEST_ASSERT(condition, message)       \
    do                                        \
    {                                         \
        if (!(condition))                     \
        {                                     \
            printf("FAIL: %s\n", message);    \
            return 0;                         \
        }                                     \
    } while (0)

#define TEST_ASSERT_EQ(actual, expected, message)                                 \
    do                                                                            \
    {                                                                             \
        size_t _a = (size_t)(actual), _e = (size_t)(expected);                    \
        if (_a != _e)                                                             \
        {                                                                         \
            printf("FAIL: %s (expected %zu, got %zu)\n", message, _e, _a);        \
            return 0;                                                             \
        }                                                                         \
    } while (0)

#define TEST_PASS(message)                \
    do                                    \
    {                                     \
        printf("PASS: %s\n", message);    \
        return 1;                         \
    } while (0)

static StdDeck_CardMask no_dead(void)
{
    StdDeck_CardMask d;
    StdDeck_CardMask_RESET(d);
    return d;
}

/* ============================================================================
 * Pattern Detection Tests
 * ============================================================================ */

static int test_is_stud_pattern(void)
{
    printf("\n--- Testing ARP_IsStudPattern ---\n");

    /* Parenthesized patterns */
    TEST_ASSERT(ARP_IsStudPattern("(AA)K", 5) == 1, "(AA)K is a stud pattern");
    TEST_ASSERT(ARP_IsStudPattern("(AK)Q", 5) == 1, "(AK)Q is a stud pattern");
    TEST_ASSERT(ARP_IsStudPattern("(ss)A", 5) == 1, "(ss)A is a stud pattern");
    TEST_ASSERT(ARP_IsStudPattern("(xx)K", 5) == 1, "(xx)K is a stud pattern");
    TEST_ASSERT(ARP_IsStudPattern("(AsKh)Qd", 9) == 1, "(AsKh)Qd is a stud pattern");

    /* Triplet patterns */
    TEST_ASSERT(ARP_IsStudPattern("AKQ", 3) == 1, "AKQ is a stud pattern");
    TEST_ASSERT(ARP_IsStudPattern("AAK", 3) == 1, "AAK is a stud pattern");
    TEST_ASSERT(ARP_IsStudPattern("222", 3) == 1, "222 is a stud pattern");

    /* NOT stud patterns */
    TEST_ASSERT(ARP_IsStudPattern("AA", 2) == 0, "AA is not a stud pattern (too short)");
    TEST_ASSERT(ARP_IsStudPattern("AKs", 3) == 0, "AKs is not a stud pattern ('s' is a suit)");
    TEST_ASSERT(ARP_IsStudPattern("(AA)", 4) == 0, "(AA) with no upcard is not a stud pattern");
    TEST_ASSERT(ARP_IsStudPattern("X", 1) == 0, "Single char is not a stud pattern");

    TEST_PASS("ARP_IsStudPattern detection");
}

/* ============================================================================
 * Basic Pattern Tests
 * ============================================================================ */

static int test_pair_with_kicker(void)
{
    printf("\n--- Testing Pair with Kicker ---\n");

    StdDeck_CardMask dead = no_dead();
    arp_range_t range;
    memset(&range, 0, sizeof(range));

    /* (AA)K: 6 AA combos * 4 kings = 24 */
    TEST_ASSERT(ARP_ParseStudPattern("(AA)K", dead, &range) == 1, "Parse (AA)K");
    TEST_ASSERT_EQ(range.count, 24, "(AA)K should have 24 combos");
    ARP_FreeRange(&range);

    /* (KK)A: 6 KK combos * 4 aces = 24 */
    memset(&range, 0, sizeof(range));
    TEST_ASSERT(ARP_ParseStudPattern("(KK)A", dead, &range) == 1, "Parse (KK)A");
    TEST_ASSERT_EQ(range.count, 24, "(KK)A should have 24 combos");
    ARP_FreeRange(&range);

    /* (AK)Q: A and K in hole (unordered), Q door.
     * Hole: A has 4 suits, K has 4 suits. Unordered pairs where card1 < card2.
     * Since A(rank=12) and K(rank=11): card indices for A are 12,25,38,51
     * and K are 11,24,37,50. The pairs with c1<c2 that have one A and one K:
     * K cards (11,24,37,50) < A cards (12,25,38,51).
     * So 4*4 = 16 valid ordered pairs, but c1<c2 constraint:
     * (11,12), (11,25), (11,38), (11,51), (24,25), (24,38), (24,51),
     * (37,38), (37,51), (50,51) = 10? No, all K indices < all A indices
     * within same suit block might not hold. Let's just check:
     * StdDeck_MAKE_CARD(rank, suit) = suit*13+rank
     * K = rank 11: cards 11, 24, 37, 50
     * A = rank 12: cards 12, 25, 38, 51
     * All 16 combos have K_idx < A_idx, so c1<c2 gives 16 pairs.
     * 16 hole combos * 4 Q doors = 64 */
    memset(&range, 0, sizeof(range));
    TEST_ASSERT(ARP_ParseStudPattern("(AK)Q", dead, &range) == 1, "Parse (AK)Q");
    TEST_ASSERT_EQ(range.count, 64, "(AK)Q should have 64 combos");
    ARP_FreeRange(&range);

    TEST_PASS("Pair with kicker tests");
}

static int test_trips_rolled_up(void)
{
    printf("\n--- Testing Rolled-Up Trips ---\n");

    StdDeck_CardMask dead = no_dead();
    arp_range_t range;
    memset(&range, 0, sizeof(range));

    /* (AA)A: Hole pair AA (c1<c2), door A. 3 cards all aces.
     * C(4,3) = 4 unique combos. With c1<c2 for hole and c3!=c1,c2:
     * 6 hole pairs * 2 remaining aces = 12, but many are duplicate 3-card sets.
     * Actually each set of 3 aces appears: pick 2 of them as hole (c1<c2), then 1 as door.
     * 3 ways to assign door from 3 cards, but our canonical form is c1<c2 for hole only.
     * So each 3-ace combo appears 1 time (c3 is fixed as the remaining ace, NOT c1<c2<c3).
     * Wait: for a 3-ace set {a1,a2,a3} with a1<a2<a3:
     *   hole=(a1,a2), door=a3  -> c1=a1<c2=a2, c3=a3 (valid)
     *   hole=(a1,a3), door=a2  -> c1=a1<c2=a3, c3=a2 (valid, c3!=c1,c3!=c2)
     *   hole=(a2,a3), door=a1  -> c1=a2<c2=a3, c3=a1 (valid)
     * So each 3-ace set produces 3 hands, not 1.
     * C(4,3) = 4 sets, each 3 variants = 12 total.
     * But dedup in stud_add_hand_to_range checks card mask equality.
     * All 3 variants produce the SAME 3-card mask {a1,a2,a3}.
     * So dedup collapses them to 4 unique combos. */
    TEST_ASSERT(ARP_ParseStudPattern("(AA)A", dead, &range) == 1, "Parse (AA)A");
    TEST_ASSERT_EQ(range.count, 4, "(AA)A should have 4 combos (C(4,3))");
    ARP_FreeRange(&range);

    /* (KK)K */
    memset(&range, 0, sizeof(range));
    TEST_ASSERT(ARP_ParseStudPattern("(KK)K", dead, &range) == 1, "Parse (KK)K");
    TEST_ASSERT_EQ(range.count, 4, "(KK)K should have 4 combos");
    ARP_FreeRange(&range);

    TEST_PASS("Rolled-up trips tests");
}

static int test_specific_suits(void)
{
    printf("\n--- Testing Specific Suits ---\n");

    StdDeck_CardMask dead = no_dead();
    arp_range_t range;
    memset(&range, 0, sizeof(range));

    /* (AsKh)Qd: exactly 1 combo */
    TEST_ASSERT(ARP_ParseStudPattern("(AsKh)Qd", dead, &range) == 1, "Parse (AsKh)Qd");
    TEST_ASSERT_EQ(range.count, 1, "(AsKh)Qd should have 1 combo");
    ARP_FreeRange(&range);

    /* Verify the hand contains the right cards */
    memset(&range, 0, sizeof(range));
    TEST_ASSERT(ARP_ParseStudPattern("(AhKs)Qc", dead, &range) == 1, "Parse (AhKs)Qc");
    TEST_ASSERT_EQ(range.count, 1, "(AhKs)Qc should have 1 combo");

    /* Check the actual cards */
    StdDeck_CardMask expected;
    StdDeck_CardMask_RESET(expected);
    /* Ah = rank ACE(12), suit HEARTS(0) -> card 0*13+12 = 12 */
    /* Ks = rank KING(11), suit SPADES(3) -> card 3*13+11 = 50 */
    /* Qc = rank QUEEN(10), suit CLUBS(2) -> card 2*13+10 = 36 */
    StdDeck_CardMask_SET(expected, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(expected, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(expected, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS));
    TEST_ASSERT(StdDeck_CardMask_EQUAL(range.hands[0], expected),
                "(AhKs)Qc should contain Ah, Ks, Qc");
    ARP_FreeRange(&range);

    TEST_PASS("Specific suits tests");
}

static int test_triplet_format(void)
{
    printf("\n--- Testing Triplet Format ---\n");

    StdDeck_CardMask dead = no_dead();
    arp_range_t range1, range2;
    memset(&range1, 0, sizeof(range1));
    memset(&range2, 0, sizeof(range2));

    /* AKQ should be equivalent to (AK)Q */
    TEST_ASSERT(ARP_ParseStudPattern("AKQ", dead, &range1) == 1, "Parse AKQ");
    TEST_ASSERT(ARP_ParseStudPattern("(AK)Q", dead, &range2) == 1, "Parse (AK)Q");
    TEST_ASSERT_EQ(range1.count, range2.count,
                   "AKQ and (AK)Q should have same combo count");
    ARP_FreeRange(&range1);
    ARP_FreeRange(&range2);

    /* AAK triplet = (AA)K */
    memset(&range1, 0, sizeof(range1));
    memset(&range2, 0, sizeof(range2));
    TEST_ASSERT(ARP_ParseStudPattern("AAK", dead, &range1) == 1, "Parse AAK");
    TEST_ASSERT(ARP_ParseStudPattern("(AA)K", dead, &range2) == 1, "Parse (AA)K");
    TEST_ASSERT_EQ(range1.count, range2.count,
                   "AAK and (AA)K should have same combo count");
    ARP_FreeRange(&range1);
    ARP_FreeRange(&range2);

    TEST_PASS("Triplet format tests");
}

/* ============================================================================
 * Wildcard Tests
 * ============================================================================ */

static int test_wildcard_upcard(void)
{
    printf("\n--- Testing Wildcard Upcard ---\n");

    StdDeck_CardMask dead = no_dead();
    arp_range_t range;
    memset(&range, 0, sizeof(range));

    /* (AA)x: pair of aces, any door card.
     * 6 AA combos. For each, 50 remaining cards as door. But dedup applies
     * when door is also an ace (trips).
     * Without dedup collision:
     *   6 pairs * 50 door cards = 300 raw
     * But trips (AA)A produces dedup: each 3-ace set appears 3 times in raw,
     * collapsed to 1. C(4,3)=4 sets, so 3*4=12 raw -> 4 deduped.
     * Non-ace door: 6 pairs * 48 non-ace cards = 288 (no dedup issues).
     * Total: 288 + 4 = 292. */
    TEST_ASSERT(ARP_ParseStudPattern("(AA)x", dead, &range) == 1, "Parse (AA)x");
    TEST_ASSERT_EQ(range.count, 292, "(AA)x should have 292 combos");
    ARP_FreeRange(&range);

    /* (KK)x: same logic. 288 non-K + 4 trips = 292 */
    memset(&range, 0, sizeof(range));
    TEST_ASSERT(ARP_ParseStudPattern("(KK)x", dead, &range) == 1, "Parse (KK)x");
    TEST_ASSERT_EQ(range.count, 292, "(KK)x should have 292 combos");
    ARP_FreeRange(&range);

    TEST_PASS("Wildcard upcard tests");
}

static int test_wild_hole_cards(void)
{
    printf("\n--- Testing Wild Hole Cards ---\n");

    StdDeck_CardMask dead = no_dead();
    arp_range_t range;
    memset(&range, 0, sizeof(range));

    /* (xx)A: any two hole cards, ace door.
     * Hole: C(52,2) unordered pairs = 1326.
     * But hole can't contain the door ace. With 4 aces as door:
     * For each ace door card: C(51,2) - pairs containing that ace.
     * Wait, c1<c2 and c3 != c1,c2. So for door = specific ace:
     *   hole pairs from remaining 51 cards, c1<c2: C(51,2)=1275
     * But we iterate all 4 ace doors, some holes are counted multiple times.
     * Actually we iterate over all (c1,c2,c3) with c1<c2, c3!=c1,c2, r3=A.
     * Dedup is by 3-card mask. Each unique 3-card set with exactly one ace:
     *   4 aces * C(48,2) = 4*1128 = 4512? No: 48 non-ace cards, pick 2: C(48,2)=1128.
     *   For each pair of non-aces, 4 choices of ace door. But that's 3-card sets with
     *   exactly 1 ace. Also, 3-card sets with exactly 2 aces:
     *   C(4,2)*48 = 6*48 = 288 (2 aces + 1 non-ace, where the door ace is one of the 2).
     *   But in our generation, c1<c2 from all 52, c3 must be ace.
     *   If c1 or c2 is also an ace, we get 2-ace hands.
     *   And 3-ace hands: C(4,3)=4.
     * Total unique 3-card sets containing at least one ace:
     *   exactly 1 ace: C(4,1)*C(48,2) = 4*1128 = 4512
     *   exactly 2 aces: C(4,2)*C(48,1) = 6*48 = 288
     *   exactly 3 aces: C(4,3) = 4
     *   Total = 4804
     * But we need door to be ace. For a set with k aces, the door can be any of the k aces.
     * But dedup means each set is stored once regardless of which ace is door.
     * So total unique 3-card masks = 4512 + 288 + 4 = 4804. */
    TEST_ASSERT(ARP_ParseStudPattern("(xx)A", dead, &range) == 1, "Parse (xx)A");
    TEST_ASSERT_EQ(range.count, 4804, "(xx)A should have 4804 combos");
    ARP_FreeRange(&range);

    TEST_PASS("Wild hole cards tests");
}

static int test_suited_hole_cards(void)
{
    printf("\n--- Testing Suited Hole Cards ---\n");

    StdDeck_CardMask dead = no_dead();
    arp_range_t range;
    memset(&range, 0, sizeof(range));

    /* (ss)A: Suited hole cards (same suit), ace door (any suit).
     * For each suit: C(13,2)=78 hole pairs. 4 suits = 312 hole pairs.
     * Door: 4 aces, minus any ace already in hole.
     *
     * Case 1: Hole suit is same as one of the aces' suit.
     *   Hole pair doesn't contain ace: C(12,2)=66 pairs per suit.
     *   Door aces: 4 aces available. 66*4 = 264 per suit, * 4 suits = 1056.
     *   But dedup: if door ace is same suit as hole, the 3-card set could
     *   also appear from different assignment. With dedup it's fine since
     *   the card mask is unique.
     *
     * Actually, let me just compute systematically:
     * For each of 4 suits s, hole pairs are C(13,2)=78 unordered pairs from that suit.
     * For each hole pair, door is any ace not in the pair.
     *   If pair contains the ace of suit s: C(12,1)=12 such pairs (A+one other of suit s).
     *     Door: 3 remaining aces. 12*3 = 36 combos per suit.
     *   If pair doesn't contain ace of suit s: C(12,2)=66 such pairs.
     *     Door: 4 aces. 66*4 = 264 combos per suit.
     *   Total per suit: 36 + 264 = 300.
     * 4 suits: 1200 total.
     * But dedup: a 3-card set {As, Xs, Ys} where X,Y are suit s:
     *   This appears once from suit=s, hole=(X,Y), door=As. No other path.
     *   A 3-card set {As, Xh, Yh}: suit=h, hole=(Xh,Yh), door=As. Only one path.
     *   A 3-card set {As, Ah, Xs}: Can this appear?
     *     suit=s: hole=(As,Xs), door=Ah -> yes.
     *     suit=h: hole=(Ah,...) but need second heart. Xs is spade, so no.
     *   Hmm wait, {As, Ah, Xs}:
     *     From suit=s: c1<c2 both spades, c3=ace. c1,c2 from spades: (As,Xs) with As<Xs?
     *     As = card 51 (rank12,suit3), Xs depends on rank.
     *     If X=K: Ks=50, As=51. c1=50<c2=51. c3=Ah(card 12). Valid: 3-card={50,51,12}.
     *     From suit=h: c1<c2 both hearts. Need both hearts AND c3=ace.
     *     The set {As,Ah,Ks} from suit=h: c1,c2 must be hearts. Ah=12 is heart, Ks=50 is spade. NO.
     *   So no dedup collision here. 1200 is correct.
     * Actually wait, a set with 2 suited cards + 1 ace of different suit only comes
     * from one path. But a set with 2 aces of same suit plus another card:
     * E.g. {Ah, Kh, As}: From suit=h, hole=(Ah,Kh), door=As. Only one path. OK.
     * So 1200 unique combos. */
    TEST_ASSERT(ARP_ParseStudPattern("(ss)A", dead, &range) == 1, "Parse (ss)A");
    TEST_ASSERT_EQ(range.count, 1200, "(ss)A should have 1200 combos");
    ARP_FreeRange(&range);

    TEST_PASS("Suited hole cards tests");
}

static int test_three_flush_binding(void)
{
    printf("\n--- Testing Three-Flush Suit Binding ---\n");

    StdDeck_CardMask dead = no_dead();
    arp_range_t range;
    memset(&range, 0, sizeof(range));

    /* (ss)Ks: Suited hole cards AND door K is same suit as hole (three-flush).
     * For suit=spades: C(13,2)=78 hole pairs from spades.
     *   Door must be Ks (King of spades). But Ks might be in hole.
     *   If Ks is NOT in hole: C(12,2) where neither is K? No, C(12,2) where K excluded.
     *   Cards in spades: 2s,3s,...,As = 13 cards.
     *   Ks = card with rank KING. Pairs not containing Ks: C(12,2)=66.
     *   Pairs containing Ks: can't use Ks as door too. So excluded.
     *   Total for suit=spades: 66 combos.
     *
     * But wait, (ss)Ks means door is literally King-of-spades, and suit binding
     * means door suit must match hole suit.
     * The 's' after 'K' is parsed as suit=SPADES. And suit_binding=1.
     * suit_binding means c3's suit must equal s1 (hole suit).
     * So door must be spades AND rank=K. That's just Ks.
     * Hole must be suited AND same suit as door = spades.
     * Hole: 2 spade cards, neither is Ks: C(12,2)=66.
     *
     * For other suits: door must be Ks(spades) but hole suit must match door suit(spades).
     * So only spades hole cards qualify. Only 66 combos from spades suit.
     * Other suits: hole is hearts but binding requires s3=s1=hearts, but s3=spades. Skip.
     *
     * Total: 66. */
    TEST_ASSERT(ARP_ParseStudPattern("(ss)Ks", dead, &range) == 1, "Parse (ss)Ks");
    TEST_ASSERT_EQ(range.count, 66, "(ss)Ks should have 66 combos");
    ARP_FreeRange(&range);

    /* (ss)Ah: three-flush with Ace of Hearts as door.
     * Hole must be hearts (binding). Pairs of hearts not containing Ah: C(12,2)=66. */
    memset(&range, 0, sizeof(range));
    TEST_ASSERT(ARP_ParseStudPattern("(ss)Ah", dead, &range) == 1, "Parse (ss)Ah");
    TEST_ASSERT_EQ(range.count, 66, "(ss)Ah should have 66 combos");
    ARP_FreeRange(&range);

    TEST_PASS("Three-flush suit binding tests");
}

/* ============================================================================
 * Range Interval Tests
 * ============================================================================ */

static int test_range_interval(void)
{
    printf("\n--- Testing Range Intervals ---\n");

    StdDeck_CardMask dead = no_dead();
    arp_range_t range;
    memset(&range, 0, sizeof(range));

    /* (AA)K-T: (AA)K + (AA)Q + (AA)J + (AA)T
     * Each is 24 combos, so total = 4 * 24 = 96 */
    TEST_ASSERT(ARP_ParseStudPattern("(AA)K-T", dead, &range) == 1, "Parse (AA)K-T");
    TEST_ASSERT_EQ(range.count, 96, "(AA)K-T should have 96 combos");
    ARP_FreeRange(&range);

    /* (AA)A-K: (AA)A + (AA)K = 4 + 24 = 28 */
    memset(&range, 0, sizeof(range));
    TEST_ASSERT(ARP_ParseStudPattern("(AA)A-K", dead, &range) == 1, "Parse (AA)A-K");
    TEST_ASSERT_EQ(range.count, 28, "(AA)A-K should have 28 combos (4 trips + 24)");
    ARP_FreeRange(&range);

    /* Reversed range: (AA)T-K should be same as (AA)K-T */
    memset(&range, 0, sizeof(range));
    TEST_ASSERT(ARP_ParseStudPattern("(AA)T-K", dead, &range) == 1, "Parse (AA)T-K");
    TEST_ASSERT_EQ(range.count, 96, "(AA)T-K should have 96 combos (same as K-T)");
    ARP_FreeRange(&range);

    TEST_PASS("Range interval tests");
}

static int test_plus_notation(void)
{
    printf("\n--- Testing Plus Notation ---\n");

    StdDeck_CardMask dead = no_dead();
    arp_range_t range;
    memset(&range, 0, sizeof(range));

    /* (AA)T+: (AA)T, (AA)J, (AA)Q, (AA)K, (AA)A = 24+24+24+24+4 = 100 */
    TEST_ASSERT(ARP_ParseStudPattern("(AA)T+", dead, &range) == 1, "Parse (AA)T+");
    TEST_ASSERT_EQ(range.count, 100, "(AA)T+ should have 100 combos");
    ARP_FreeRange(&range);

    /* (AA)K+: (AA)K + (AA)A = 24 + 4 = 28 */
    memset(&range, 0, sizeof(range));
    TEST_ASSERT(ARP_ParseStudPattern("(AA)K+", dead, &range) == 1, "Parse (AA)K+");
    TEST_ASSERT_EQ(range.count, 28, "(AA)K+ should have 28 combos");
    ARP_FreeRange(&range);

    /* (AA)A+: just (AA)A = 4 */
    memset(&range, 0, sizeof(range));
    TEST_ASSERT(ARP_ParseStudPattern("(AA)A+", dead, &range) == 1, "Parse (AA)A+");
    TEST_ASSERT_EQ(range.count, 4, "(AA)A+ should have 4 combos");
    ARP_FreeRange(&range);

    TEST_PASS("Plus notation tests");
}

/* ============================================================================
 * Dead Card Tests
 * ============================================================================ */

static int test_dead_cards(void)
{
    printf("\n--- Testing Dead Card Filtering ---\n");

    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);

    /* Set Ah as dead */
    StdDeck_CardMask_SET(dead, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));

    arp_range_t range;
    memset(&range, 0, sizeof(range));

    /* (AA)K without dead: 24 combos. With Ah dead:
     * Hole AA pairs containing Ah: 3 (Ah+Ad, Ah+Ac, Ah+As) = 3 pairs.
     * These 3 pairs * 4 K doors = 12 combos removed.
     * Remaining: 24 - 12 = 12.
     * But also check door cards: K doors don't include Ah, so no extra removal.
     * Wait, door K can't be Ah since Ah is ace not king. So only hole filtering.
     * Actually: remaining AA pairs = C(3,2) = 3 (from Ad, Ac, As).
     * 3 * 4 king doors = 12. */
    TEST_ASSERT(ARP_ParseStudPattern("(AA)K", dead, &range) == 1, "Parse (AA)K with Ah dead");
    TEST_ASSERT_EQ(range.count, 12, "(AA)K with Ah dead should have 12 combos");
    ARP_FreeRange(&range);

    /* (AsKh)Qd with Ah dead: Ah is not in this hand, so should still have 1 combo */
    memset(&range, 0, sizeof(range));
    TEST_ASSERT(ARP_ParseStudPattern("(AsKh)Qd", dead, &range) == 1, "Parse (AsKh)Qd with Ah dead");
    TEST_ASSERT_EQ(range.count, 1, "(AsKh)Qd with Ah dead should still have 1 combo");
    ARP_FreeRange(&range);

    /* (AhKs)Qd with Ah dead: Ah IS in this hand, so should have 0 combos */
    memset(&range, 0, sizeof(range));
    TEST_ASSERT(ARP_ParseStudPattern("(AhKs)Qd", dead, &range) == 1, "Parse (AhKs)Qd with Ah dead");
    TEST_ASSERT_EQ(range.count, 0, "(AhKs)Qd with Ah dead should have 0 combos");
    ARP_FreeRange(&range);

    TEST_PASS("Dead card filtering tests");
}

/* ============================================================================
 * pe_range API Integration Tests
 * ============================================================================ */

static int test_pe_range_stud_parse(void)
{
    printf("\n--- Testing pe_range API for Stud ---\n");

    StdDeck_CardMask dead = no_dead();
    pe_range_t *range = NULL;

    /* Parse via unified API */
    pe_status_t st = pe_range_parse(game_7stud, "(AA)K", dead, NULL, &range);
    TEST_ASSERT(st == PE_STATUS_OK, "pe_range_parse (AA)K for game_7stud");
    TEST_ASSERT(range != NULL, "range should not be NULL");
    TEST_ASSERT_EQ(range->count, 24, "pe_range (AA)K should have 24 combos");
    pe_range_free(range);

    /* Parse triplet format via pe_range API */
    range = NULL;
    st = pe_range_parse(game_7stud, "AKQ", dead, NULL, &range);
    TEST_ASSERT(st == PE_STATUS_OK, "pe_range_parse AKQ for game_7stud");
    TEST_ASSERT(range != NULL, "range should not be NULL");
    TEST_ASSERT(range->count > 0, "AKQ should produce combos");
    pe_range_free(range);

    TEST_PASS("pe_range API Stud integration tests");
}

static int test_pe_range_stud_top_percent(void)
{
    printf("\n--- Testing pe_range_top_percent for Stud ---\n");

    StdDeck_CardMask dead = no_dead();
    pe_range_t *range = NULL;

    /* pe_range_top_percent uses 0.0-1.0 fraction (matching ARP convention).
     * Top 1% of Stud hands: 1% of 22100 = 221 target combos.
     * Should include trips (52 combos) and some high pair hands. */
    pe_status_t st = pe_range_top_percent(game_7stud, 0.01, dead, &range);
    TEST_ASSERT(st == PE_STATUS_OK, "pe_range_top_percent 1% for game_7stud");
    TEST_ASSERT(range != NULL, "range should not be NULL");
    printf("  1%% Stud: %zu combos\n", range->count);
    TEST_ASSERT(range->count > 50, "1% Stud should have >50 combos (includes trips + high pairs)");
    TEST_ASSERT(range->count < 500, "1% Stud should have <500 combos");
    pe_range_free(range);

    /* Top 5% = 0.05 fraction */
    range = NULL;
    st = pe_range_top_percent(game_7stud, 0.05, dead, &range);
    TEST_ASSERT(st == PE_STATUS_OK, "pe_range_top_percent 5% for game_7stud");
    TEST_ASSERT(range != NULL, "range should not be NULL");
    TEST_ASSERT(range->count > 200, "5% Stud should have >200 combos");
    printf("  5%% Stud: %zu combos\n", range->count);
    pe_range_free(range);

    TEST_PASS("pe_range_top_percent Stud tests");
}

/* ============================================================================
 * Main Test Runner
 * ============================================================================ */

int main(void)
{
    printf("Stud Range Parser Comprehensive Tests\n");
    printf("======================================\n");

    int passed = 0, failed = 0;

    #define RUN_TEST(fn) do { if (fn()) passed++; else failed++; } while(0)

    RUN_TEST(test_is_stud_pattern);
    RUN_TEST(test_pair_with_kicker);
    RUN_TEST(test_trips_rolled_up);
    RUN_TEST(test_specific_suits);
    RUN_TEST(test_triplet_format);
    RUN_TEST(test_wildcard_upcard);
    RUN_TEST(test_wild_hole_cards);
    RUN_TEST(test_suited_hole_cards);
    RUN_TEST(test_three_flush_binding);
    RUN_TEST(test_range_interval);
    RUN_TEST(test_plus_notation);
    RUN_TEST(test_dead_cards);
    RUN_TEST(test_pe_range_stud_parse);
    RUN_TEST(test_pe_range_stud_top_percent);

    #undef RUN_TEST

    printf("\n======================================\n");
    printf("Results: %d passed, %d failed\n", passed, failed);

    return (failed > 0) ? 1 : 0;
}
