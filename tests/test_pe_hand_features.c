/*
 * test_pe_hand_features.c - PLO4/PLO5/PLO6 hand features and strategy
 * buckets (issue #238).
 *
 *  1. Fixtures worked out by hand, each with positive and negative
 *     assertions: dry, paired, monotone and connected boards; overpairs,
 *     underpairs, board-relative pairs, top two pair, sets and trips;
 *     straights, nut and non-nut flushes, a full house; wraps and a combo draw;
 *     a double flush draw; the two-plus-three rule for both a flush and a
 *     straight; paired and suited private structures for all three sizes.
 *  2. Random deals for PLO4, PLO5 and PLO6 on flops, turns and rivers,
 *     checked against brute force that shares no logic with the classifier:
 *     the made hand against the context evaluator (pe_eval_5c) over every
 *     two-plus-three hand; straight and flush draws by trying every unseen
 *     next card with the legacy evaluator; the nuts over every pair of unseen
 *     cards; backdoor flush draws over every pair of runout cards.
 *  3. Bucket keys: determinism, formatting, matching, dimension separation.
 *  4. Aggregation: combo counts, weights, weight shares, weighted
 *     frequencies, EV and EV delta, order independence, and the errors.
 */

#include <poker_eval/solver/pe_hand_features.h>
#include <poker_eval/core/eval_context.h>
#include <poker_eval/core/eval.h>
#include <poker_eval/core/cardmask_compat.h>
#include <poker_eval/games/rules_std.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(cond, ...)                                                   \
    do                                                                     \
    {                                                                      \
        if (!(cond))                                                       \
        {                                                                  \
            fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__);         \
            fprintf(stderr, __VA_ARGS__);                                  \
            fprintf(stderr, "\n");                                         \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

/* ---------------------------------------------------------------- *
 * Helpers
 * ---------------------------------------------------------------- */

static mask_t cards(const char *text)
{
    static const char ranks[] = "23456789TJQKA";
    static const char suits[] = "cdhs";
    mask_t m = MASK_EMPTY;
    while (*text)
    {
        if (*text == ' ')
        {
            ++text;
            continue;
        }
        const char *r = strchr(ranks, text[0]);
        const char *s = text[1] ? strchr(suits, text[1]) : NULL;
        if (!r || !s)
        {
            fprintf(stderr, "bad card text \"%s\"\n", text);
            exit(2);
        }
        int card = (int)(r - ranks) + 13 * (int)(s - suits);
        if (mask_is_set(m, card))
        {
            fprintf(stderr, "duplicate card in \"%s\"\n", text);
            exit(2);
        }
        m = mask_set(m, card);
        text += 2;
    }
    return m;
}

static void features(const char *board_text, const char *hole_text,
                     pe_hf_board_t *board, pe_hand_features_t *f)
{
    mask_t board_mask = cards(board_text);
    mask_t hole = cards(hole_text);
    if (mask_intersects(board_mask, hole))
    {
        fprintf(stderr, "fixture reuses a card: %s / %s\n", board_text,
                hole_text);
        exit(2);
    }
    if (pe_hf_board_prepare(board_mask, board) != PE_SOLVER_OK ||
        pe_hand_features_compute(board, hole, f) != PE_SOLVER_OK)
    {
        fprintf(stderr, "fixture refused: %s / %s\n", board_text, hole_text);
        exit(2);
    }
}

#define R(ch) ((int)(strchr("23456789TJQKA", (ch)) - "23456789TJQKA"))

/* ---------------------------------------------------------------- *
 * 1. Fixtures
 * ---------------------------------------------------------------- */

static void test_fixtures(void)
{
    pe_hf_board_t b;
    pe_hand_features_t f;

    printf("  fixtures\n");

    /* Dry rainbow flop, aces: an overpair with nothing else. */
    features("Ks7d2c", "AsAh8c9d", &b, &f);
    CHECK(f.made == PE_HF_MADE_PAIR && f.detail == PE_HF_DETAIL_OVERPAIR,
          "AA on K72: overpair (made %u detail %u)", f.made, f.detail);
    CHECK(!f.is_nuts, "AA on K72 is not the nuts (top set is)");
    CHECK(f.flush_draw == PE_HF_FLUSH_DRAW_NONE &&
              f.straight_draw == PE_HF_STRAIGHT_DRAW_NONE && !f.combo_draw,
          "AA89 rainbow on K72 draws to nothing");
    CHECK(f.paired_ranks == 1 && f.trips_ranks == 0, "AA89: one pair");
    CHECK(f.suit_shape[0] == 1 && f.suit_shape[3] == 1 && f.suited_groups == 0,
          "AsAh8c9d is rainbow");
    CHECK(f.rank_gaps == 4 && f.connectivity == PE_HF_CONNECT_LOW,
          "A98: four missing ranks in 8..A (gaps %u)", f.rank_gaps);
    CHECK(!(f.blockers & PE_HF_BLOCKS_TOP_CARD), "AA89 holds no king");
    /* One spade on the flop can still become three by the river, so the
       nut spade is a blocker (the backdoor case)... */
    CHECK(f.blockers & PE_HF_BLOCKS_NUT_FLUSH,
          "As on a one-spade flop blocks the backdoor nut flush");
    CHECK(!b.paired && !b.monotone && !b.flush_possible && b.max_suit == 1,
          "K72 rainbow texture");
    CHECK(b.nut_straight_top == -1, "no straight possible on K72");

    /* ...but not on the turn, with one card to come. */
    features("Ks7d2c5h", "AsAh8c9d", &b, &f);
    CHECK(!(f.blockers & PE_HF_BLOCKS_NUT_FLUSH),
          "one spade on the turn cannot become a flush: no blocker");

    /* Top set is the nuts on a dry board; a backdoor flush draw. */
    features("Ks7d2c", "KhKd5s6s", &b, &f);
    CHECK(f.made == PE_HF_MADE_TRIPS && f.detail == PE_HF_DETAIL_TOP_SET,
          "KK on K72: top set");
    CHECK(f.is_nuts, "top set is the nuts on K72 rainbow");
    CHECK(f.flush_draw == PE_HF_FLUSH_DRAW_BACKDOOR,
          "5s6s with Ks: backdoor flush draw (got %u)", f.flush_draw);
    CHECK(f.blockers & PE_HF_BLOCKS_TOP_CARD, "KK blocks the top card");
    CHECK(f.suit_shape[0] == 2 && f.suit_shape[1] == 1 &&
              f.suit_shape[2] == 1 && f.suit_shape[3] == 0 &&
              f.suited_groups == 1,
          "KhKd5s6s is 2-1-1");

    features("Ks7d2c", "7h7cQsJs", &b, &f);
    CHECK(f.detail == PE_HF_DETAIL_MIDDLE_SET && !f.is_nuts,
          "77 on K72: middle set, not the nuts");
    features("Ks7d2c", "2h2sQdJd", &b, &f);
    CHECK(f.detail == PE_HF_DETAIL_BOTTOM_SET, "22 on K72: bottom set");

    /* Pairs against the board. */
    features("Ks9d4c", "QhQd7c6s", &b, &f);
    CHECK(f.detail == PE_HF_DETAIL_UNDERPAIR, "QQ on K94: underpair");
    features("Ks9d4c", "KhJc6d5s", &b, &f);
    CHECK(f.detail == PE_HF_DETAIL_TOP_PAIR, "Kx on K94: top pair");
    features("Ks9d4c", "9hJc6d5s", &b, &f);
    CHECK(f.detail == PE_HF_DETAIL_SECOND_PAIR, "9x on K94: second pair");
    features("Ks9d4c", "4hJc7d8s", &b, &f);
    CHECK(f.detail == PE_HF_DETAIL_LOW_PAIR && f.made == PE_HF_MADE_PAIR,
          "4x on K94: low pair (made %u detail %u)", f.made, f.detail);
    features("Ks9d4c", "Kh9h5c5d", &b, &f);
    CHECK(f.made == PE_HF_MADE_TWO_PAIR && f.detail == PE_HF_DETAIL_TOP_TWO_PAIR,
          "K9 on K94: top two pair");

    /* Paired board. */
    /* The nuts are absolute: the best any two cards make, the hero's own
       included. Kings full is not the nuts on KK7 while KK in the hand is
       possible, even for the player who holds one of those kings. */
    features("KsKd7c", "Kh7hQc2d", &b, &f);
    CHECK(f.made == PE_HF_MADE_FULL_HOUSE && !f.is_nuts,
          "K7 on KK7: kings full, not the nuts (quad kings are)");
    CHECK((f.blockers & PE_HF_BLOCKS_BOARD_PAIR) &&
              (f.blockers & PE_HF_BLOCKS_TOP_CARD),
          "K7 on KK7 blocks the board pair and the top card");
    CHECK(b.paired, "KK7 is paired");
    features("KsKd7c", "KhKc7h2d", &b, &f);
    CHECK(f.made == PE_HF_MADE_QUADS && f.is_nuts, "KK on KK7: quads, the nuts");
    features("KsKd7c", "QhJh5c3d", &b, &f);
    CHECK(f.made == PE_HF_MADE_PAIR && f.detail == PE_HF_DETAIL_BOARD_PAIR,
          "QJ53 on KK7: only the board's pair");
    CHECK(!(f.blockers & PE_HF_BLOCKS_BOARD_PAIR), "QJ53 blocks no king");
    features("9s9d4c", "9hAcKd2s", &b, &f);
    CHECK(f.made == PE_HF_MADE_TRIPS && f.detail == PE_HF_DETAIL_TRIPS,
          "9x on 994: trips");

    /* Straights. JT on 987 is the nut straight, and the queen still
       upgrades it. */
    features("9s8d7c", "JhTc2d2s", &b, &f);
    CHECK(f.made == PE_HF_MADE_STRAIGHT && f.is_nuts, "JT on 987: nut straight");
    CHECK(f.redraw && f.straight_draw == PE_HF_STRAIGHT_DRAW_GUTSHOT &&
              f.straight_outs == 4,
          "JT on 987: a queen upgrades it (draw %u outs %u)", f.straight_draw,
          f.straight_outs);
    CHECK(f.blockers & PE_HF_BLOCKS_NUT_STRAIGHT, "JT blocks the nut straight");
    CHECK(b.nut_straight_top == R('J'), "987: the nut straight is jack-high");

    /* Two plus three: four to a straight on the board and one connecting
       rank in the hand is no straight and no draw. */
    features("5s6d7c8h", "KhKd2c2s", &b, &f);
    CHECK(f.made != PE_HF_MADE_STRAIGHT && f.made == PE_HF_MADE_PAIR,
          "KK22 on 5678 has no straight in Omaha (made %u)", f.made);
    CHECK(f.detail == PE_HF_DETAIL_OVERPAIR, "KK on 5678: overpair");
    CHECK(f.straight_draw == PE_HF_STRAIGHT_DRAW_NONE,
          "KK22 on 5678 draws to no straight");

    /* Wraps. JT76 on 982 is the 20-out wrap to the nuts. */
    features("9s8d2c", "JhTc7d6s", &b, &f);
    CHECK(f.made == PE_HF_MADE_HIGH_CARD, "JT76 on 982: no made hand");
    CHECK(f.straight_draw == PE_HF_STRAIGHT_DRAW_WRAP && f.straight_outs == 20,
          "JT76 on 982: 20-out wrap (draw %u outs %u)", f.straight_draw,
          f.straight_outs);
    CHECK(f.straight_draw_to_nuts, "the queen makes JT76 the nut straight");
    CHECK(f.flush_draw == PE_HF_FLUSH_DRAW_NONE && !f.combo_draw,
          "rainbow wrap: no flush draw, no combo");
    CHECK(f.rank_gaps == 2 && f.connectivity == PE_HF_CONNECT_MEDIUM &&
              f.longest_run == 2 && f.rank_components == 2,
          "JT76: two runs of two, two missing ranks");

    /* A wrap with the nut flush draw is a combo draw. */
    features("9s8s2c", "AsTs7d6h", &b, &f);
    CHECK(f.straight_draw == PE_HF_STRAIGHT_DRAW_WRAP && f.straight_outs == 17,
          "AT76 on 982: 17-out wrap (outs %u)", f.straight_outs);
    CHECK(f.flush_draw == PE_HF_FLUSH_DRAW_NUT && f.combo_draw,
          "AsTs on 9s8s: nut flush draw and a combo draw");
    CHECK(f.blockers & PE_HF_BLOCKS_NUT_FLUSH, "As blocks the nut flush");
    features("9s8s2c", "KsTs7d6h", &b, &f);
    CHECK(f.flush_draw == PE_HF_FLUSH_DRAW,
          "KsTs on 9s8s: a flush draw but not the nut one (got %u)",
          f.flush_draw);
    CHECK(!(f.blockers & PE_HF_BLOCKS_NUT_FLUSH),
          "KsTs does not block the nut flush");

    /* Monotone board: one spade in the hand is no flush in Omaha. */
    features("Ks9s4s", "AsQd7d2c", &b, &f);
    CHECK(f.made == PE_HF_MADE_HIGH_CARD, "As alone on Ks9s4s: no flush");
    CHECK(f.blockers & PE_HF_BLOCKS_NUT_FLUSH, "As blocks the nut flush");
    CHECK(b.monotone && b.flush_possible, "Ks9s4s is monotone");
    features("Ks9s4s", "AsJs5d5c", &b, &f);
    CHECK(f.made == PE_HF_MADE_FLUSH && f.nut_flush && !f.is_nuts,
          "AsJs: the nut-flush card, but AsQs is the better flush");
    features("Ks9s4s", "QsJs5d5c", &b, &f);
    CHECK(f.made == PE_HF_MADE_FLUSH && !f.nut_flush && !f.is_nuts,
          "QsJs: a flush without the nut card");
    features("Ks9s4s", "AsQs5d5c", &b, &f);
    CHECK(f.made == PE_HF_MADE_FLUSH && f.nut_flush && f.is_nuts,
          "AsQs: the nut flush");

    /* River: nothing is a draw any more. */
    features("Ks9d4c2h7s", "JhTc8d6s", &b, &f);
    CHECK(f.flush_draw == PE_HF_FLUSH_DRAW_NONE &&
              f.straight_draw == PE_HF_STRAIGHT_DRAW_NONE &&
              f.straight_outs == 0 && !f.redraw,
          "no draws on the river");

    /* No board: private structure only. */
    features("", "AsAhKsKh", &b, &f);
    CHECK(!f.evaluated && f.paired_ranks == 2 && f.broadway_count == 4 &&
              f.suit_shape[0] == 2 && f.suit_shape[1] == 2 &&
              f.suited_groups == 2,
          "AAKK double-suited, no board");

    /* PLO5: a double flush draw, shaped 2-2-1. */
    features("Ks9s4d8d", "AsQsAdJd3c", &b, &f);
    CHECK(f.hole_count == 5 && f.double_flush_draw &&
              f.flush_draw == PE_HF_FLUSH_DRAW_NUT,
          "AsQsAdJd on two two-tone suits: double nut flush draw");
    CHECK(f.suit_shape[0] == 2 && f.suit_shape[1] == 2 &&
              f.suit_shape[2] == 1 && f.suit_shape[3] == 0,
          "AsQsAdJd3c is 2-2-1");

    /* PLO6: six to the suit, the nut flush draw. */
    features("2s3s4d", "AsKsQsJsTs9s", &b, &f);
    CHECK(f.hole_count == 6 && f.suit_shape[0] == 6 && f.suited_groups == 1 &&
              f.flush_draw == PE_HF_FLUSH_DRAW_NUT && !f.double_flush_draw,
          "six spades on a two-spade flop: nut flush draw");
    CHECK(f.distinct_ranks == 6 && f.connectivity == PE_HF_CONNECT_RUNDOWN &&
              f.longest_run == 6 && f.broadway_count == 5,
          "9-A: a six-card rundown");

    /* PLO6 private pairs and trips. */
    features("", "7s7h7dKcQh2s", &b, &f);
    CHECK(f.trips_ranks == 1 && f.paired_ranks == 0, "777KQ2: trips");
    features("", "AsAhKsKhQdQc", &b, &f);
    CHECK(f.paired_ranks == 3 && f.trips_ranks == 0, "AAKKQQ: three pairs");

    /* Wheel connectivity: the ace counts low when that is tighter. */
    features("", "As2h3d5c", &b, &f);
    CHECK(f.rank_gaps == 1 && f.connectivity == PE_HF_CONNECT_HIGH &&
              f.longest_run == 3,
          "A235: one missing rank with the ace low (gaps %u)", f.rank_gaps);
}

/* ---------------------------------------------------------------- *
 * 2. Random deals against brute force
 * ---------------------------------------------------------------- */

static uint64_t rng_next(uint64_t *s)
{
    uint64_t x = *s;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *s = x;
    return x;
}

static int list_cards(mask_t m, int *out)
{
    int n = 0;
    for (int c = 0; c < 52; ++c)
        if (mask_is_set(m, c))
            out[n++] = c;
    return n;
}

static mask_t five_of(const int *h, int i, int j, const int *b, int a, int bb,
                      int c)
{
    mask_t m = MASK_EMPTY;
    m = mask_set(m, h[i]);
    m = mask_set(m, h[j]);
    m = mask_set(m, b[a]);
    m = mask_set(m, b[bb]);
    m = mask_set(m, b[c]);
    return m;
}

/* Best two-plus-three hand class and value by the context evaluator. */
static eval_t brute_best(const EvalContext *ctx, mask_t hole, mask_t board)
{
    int h[6], b[5];
    int nh = list_cards(hole, h), nb = list_cards(board, b);
    eval_t best = 0;
    for (int i = 0; i < nh; ++i)
        for (int j = i + 1; j < nh; ++j)
            for (int a = 0; a < nb; ++a)
                for (int bb = a + 1; bb < nb; ++bb)
                    for (int c = bb + 1; c < nb; ++c)
                    {
                        eval_t v = pe_eval_5c(ctx, five_of(h, i, j, b, a, bb, c));
                        if (v > best)
                            best = v;
                    }
    return best;
}

/* Highest straight top (legacy evaluator, straight or straight flush) any
   two-plus-three hand makes, or -1. */
static int brute_straight_top(mask_t hole, mask_t board)
{
    int h[6], b[5];
    int nh = list_cards(hole, h), nb = list_cards(board, b);
    int best = -1;
    for (int i = 0; i < nh; ++i)
        for (int j = i + 1; j < nh; ++j)
            for (int a = 0; a < nb; ++a)
                for (int bb = a + 1; bb < nb; ++bb)
                    for (int c = bb + 1; c < nb; ++c)
                    {
                        HandVal v = StdDeck_StdRules_EVAL_N(
                            mask_t_to_cardmask(five_of(h, i, j, b, a, bb, c)), 5);
                        int t = (int)HandVal_HANDTYPE(v);
                        if (t == StdRules_HandType_STRAIGHT ||
                            t == StdRules_HandType_STFLUSH)
                        {
                            int top = (int)HandVal_TOP_CARD(v);
                            if (top > best)
                                best = top;
                        }
                    }
    return best;
}

/* Whether some two-plus-three hand is a flush or straight flush. */
static int brute_has_flush(mask_t hole, mask_t board)
{
    int h[6], b[5];
    int nh = list_cards(hole, h), nb = list_cards(board, b);
    for (int i = 0; i < nh; ++i)
        for (int j = i + 1; j < nh; ++j)
        {
            if (h[i] / 13 != h[j] / 13)
                continue;
            for (int a = 0; a < nb; ++a)
                for (int bb = a + 1; bb < nb; ++bb)
                    for (int c = bb + 1; c < nb; ++c)
                        if (b[a] / 13 == h[i] / 13 && b[bb] / 13 == h[i] / 13 &&
                            b[c] / 13 == h[i] / 13)
                            return 1;
        }
    return 0;
}

/* Highest straight any two unseen cards make with this board. A straight
   only cares about ranks, so one unseen card of each rank is enough. */
static int brute_nut_straight_top(mask_t board)
{
    int card_of_rank[13];
    int best = -1;
    for (int r = 0; r < 13; ++r)
    {
        card_of_rank[r] = -1;
        for (int s = 0; s < 4 && card_of_rank[r] < 0; ++s)
            if (!mask_is_set(board, r + 13 * s))
                card_of_rank[r] = r + 13 * s;
    }
    for (int r1 = 0; r1 < 13; ++r1)
        for (int r2 = r1 + 1; r2 < 13; ++r2)
        {
            if (card_of_rank[r1] < 0 || card_of_rank[r2] < 0)
                continue;
            mask_t two = mask_set(mask_set(MASK_EMPTY, card_of_rank[r1]),
                                  card_of_rank[r2]);
            int top = brute_straight_top(two, board);
            if (top > best)
                best = top;
        }
    return best;
}

/* Best five of all the cards, ignoring the Omaha rule. */
static eval_t brute_unrestricted(const EvalContext *ctx, mask_t all)
{
    int c[11];
    int n = list_cards(all, c);
    eval_t best = 0;
    for (int a = 0; a < n; ++a)
        for (int b = a + 1; b < n; ++b)
            for (int d = b + 1; d < n; ++d)
                for (int e = d + 1; e < n; ++e)
                    for (int g = e + 1; g < n; ++g)
                    {
                        mask_t m = MASK_EMPTY;
                        m = mask_set(m, c[a]);
                        m = mask_set(m, c[b]);
                        m = mask_set(m, c[d]);
                        m = mask_set(m, c[e]);
                        m = mask_set(m, c[g]);
                        eval_t v = pe_eval_5c(ctx, m);
                        if (v > best)
                            best = v;
                    }
    return best;
}

static void test_random_deals(const EvalContext *ctx, int hole_n,
                              int board_n, int deals, uint64_t seed,
                              int deep)
{
    int made_bad = 0, nuts_bad = 0, straight_bad = 0, flush_bad = 0,
        backdoor_bad = 0, unrestricted_better = 0, draws_seen = 0;

    for (int d = 0; d < deals; ++d)
    {
        int deck[52];
        mask_t board = MASK_EMPTY, hole = MASK_EMPTY;
        pe_hf_board_t b;
        pe_hand_features_t f;

        for (int c = 0; c < 52; ++c)
            deck[c] = c;
        for (int c = 51; c > 0; --c)
        {
            int j = (int)(rng_next(&seed) % (uint64_t)(c + 1));
            int t = deck[c];
            deck[c] = deck[j];
            deck[j] = t;
        }
        for (int i = 0; i < board_n; ++i)
            board = mask_set(board, deck[i]);
        for (int i = 0; i < hole_n; ++i)
            hole = mask_set(hole, deck[board_n + i]);

        if (pe_hf_board_prepare(board, &b) != PE_SOLVER_OK ||
            pe_hand_features_compute(&b, hole, &f) != PE_SOLVER_OK)
        {
            CHECK(0, "random deal %d refused", d);
            continue;
        }

        /* Made hand against the other evaluator. */
        eval_t best = brute_best(ctx, hole, board);
        hand_class_t best_class = eval_get_hand_class(best);
        if ((int)best_class != (int)f.made)
            made_bad++;
        /* Unrestricted five-of-N would sometimes see more: count it, so the
           test shows the Omaha restriction is doing something. */
        if (board_n == 5)
        {
            eval_t any = brute_unrestricted(ctx, hole | board);
            if (eval_get_hand_class(any) > eval_get_hand_class(best))
                unrestricted_better++;
        }

        if (deep && d % 8 == 0)
        {
            /* The nuts: every pair of unseen cards. */
            eval_t nut = 0;
            for (int c1 = 0; c1 < 52; ++c1)
                for (int c2 = c1 + 1; c2 < 52; ++c2)
                {
                    if (mask_is_set(board, c1) || mask_is_set(board, c2))
                        continue;
                    eval_t v = brute_best(
                        ctx, mask_set(mask_set(MASK_EMPTY, c1), c2), board);
                    if (v > nut)
                        nut = v;
                }
            if ((best == nut) != (f.is_nuts != 0))
                nuts_bad++;
        }

        if (board_n < 5 && f.made <= PE_HF_MADE_STRAIGHT)
        {
            /* Straight outs: every unseen next card. */
            int current = brute_straight_top(hole, board);
            int outs = 0, ranks = 0, to_nuts = 0;
            int rank_seen[13] = {0};
            int flush_next = 0;
            for (int c = 0; c < 52; ++c)
            {
                if (mask_is_set(board, c) || mask_is_set(hole, c))
                    continue;
                mask_t next = mask_set(board, c);
                int top = brute_straight_top(hole, next);
                if (top > current)
                {
                    outs++;
                    if (!rank_seen[c % 13])
                    {
                        rank_seen[c % 13] = 1;
                        ranks++;
                        if (deep && !to_nuts &&
                            top == brute_nut_straight_top(next))
                            to_nuts = 1;
                    }
                }
                if (brute_has_flush(hole, next))
                    flush_next = 1;
            }
            int want_class = ranks == 0   ? PE_HF_STRAIGHT_DRAW_NONE
                             : ranks == 1 ? PE_HF_STRAIGHT_DRAW_GUTSHOT
                             : ranks == 2 ? PE_HF_STRAIGHT_DRAW_OPEN_ENDED
                                          : PE_HF_STRAIGHT_DRAW_WRAP;
            if (outs != f.straight_outs || want_class != f.straight_draw ||
                (deep && to_nuts != f.straight_draw_to_nuts))
            {
                if (straight_bad++ < 3)
                    fprintf(stderr,
                            "  straight mismatch: outs %d/%u class %d/%u "
                            "nuts %d/%u\n",
                            outs, f.straight_outs, want_class,
                            f.straight_draw, to_nuts, f.straight_draw_to_nuts);
            }
            if (ranks)
                draws_seen++;

            /* A flush draw: some next card completes a flush. */
            if ((f.flush_draw >= PE_HF_FLUSH_DRAW) != flush_next)
                flush_bad++;

            /* Backdoor: on the flop, no single card but some pair does. */
            if (deep && board_n == 3 && d % 4 == 0)
            {
                int two_cards = 0;
                for (int c1 = 0; c1 < 52 && !two_cards; ++c1)
                    for (int c2 = c1 + 1; c2 < 52 && !two_cards; ++c2)
                    {
                        if (mask_is_set(board | hole, c1) ||
                            mask_is_set(board | hole, c2))
                            continue;
                        if (brute_has_flush(hole, mask_set(mask_set(board, c1), c2)))
                            two_cards = 1;
                    }
                int want = flush_next ? 0 : two_cards;
                if ((f.flush_draw == PE_HF_FLUSH_DRAW_BACKDOOR) != want)
                    backdoor_bad++;
            }
        }
    }

    CHECK(made_bad == 0, "PLO%d on %d cards: %d made hands disagree with "
          "pe_eval_5c", hole_n, board_n, made_bad);
    CHECK(nuts_bad == 0, "PLO%d on %d cards: %d nut flags disagree",
          hole_n, board_n, nuts_bad);
    CHECK(straight_bad == 0, "PLO%d on %d cards: %d straight draws disagree",
          hole_n, board_n, straight_bad);
    CHECK(flush_bad == 0, "PLO%d on %d cards: %d flush draws disagree",
          hole_n, board_n, flush_bad);
    CHECK(backdoor_bad == 0, "PLO%d on %d cards: %d backdoor draws disagree",
          hole_n, board_n, backdoor_bad);
    if (board_n < 5)
        CHECK(draws_seen >= deals / 20, "PLO%d on %d cards: only %d straight "
              "draws, too few to test", hole_n, board_n, draws_seen);
    if (board_n == 5)
        CHECK(unrestricted_better > 0, "PLO%d river: unrestricted evaluation "
              "never differed, so the test cannot see the Omaha rule", hole_n);
    if (board_n < 5)
        printf("    PLO%d on %d board cards: %d deals, %d with a straight draw\n",
               hole_n, board_n, deals, draws_seen);
    else
        printf("    PLO%d on the river: %d deals, %d where five-of-N would "
               "overrate the hand\n",
               hole_n, deals, unrestricted_better);
}

/* ---------------------------------------------------------------- *
 * 3. Keys
 * ---------------------------------------------------------------- */

static void test_keys(void)
{
    pe_hf_board_t b;
    pe_hand_features_t f, g;
    char text[128];

    printf("  bucket keys\n");

    features("9s8s2c", "AsTs7d6h", &b, &f);
    uint64_t k = pe_hf_key(&f, PE_HF_DIM_MADE | PE_HF_DIM_FLUSH_DRAW);
    pe_hf_key_format(k, text, sizeof(text));
    CHECK(strcmp(text, "made=high_card,flush_draw=nut") == 0,
          "format: \"%s\"", text);
    CHECK(pe_hf_key_dims(k) == (PE_HF_DIM_MADE | PE_HF_DIM_FLUSH_DRAW),
          "the key records its dimensions");
    CHECK(pe_hf_key_matches(&f, k), "a hand matches its own key");

    features("9s8s2c", "KsTs7d6h", &b, &g);
    CHECK(!pe_hf_key_matches(&g, k),
          "a non-nut flush draw does not match flush_draw=nut");
    CHECK(pe_hf_key(&g, PE_HF_DIM_MADE) == pe_hf_key(&f, PE_HF_DIM_MADE),
          "both are high card: same made-hand key");
    CHECK(pe_hf_key(&g, PE_HF_DIM_MADE | PE_HF_DIM_STRAIGHT_DRAW) ==
              pe_hf_key(&f, PE_HF_DIM_MADE | PE_HF_DIM_STRAIGHT_DRAW),
          "both wrap: same made+straight key");

    /* The same values over different dimensions are different buckets. */
    features("Ks7d2c", "AsAh8c9d", &b, &g);
    CHECK(pe_hf_key(&g, PE_HF_DIM_NUTS) != pe_hf_key(&g, PE_HF_DIM_COMBO_DRAW),
          "nuts=no and combo_draw=no must not share a key");

    features("Ks9s4d8d", "AsQsAdJd3c", &b, &g);
    pe_hf_key_format(pe_hf_key(&g, PE_HF_DIM_SUITS | PE_HF_DIM_PAIRS), text,
                     sizeof(text));
    CHECK(strcmp(text, "suits=2-2-1,pairs=1p0t") == 0, "format: \"%s\"", text);

    pe_hf_key_format(pe_hf_key(&f, PE_HF_DIM_ALL), text, sizeof(text));
    CHECK(strstr(text, "straight_draw=wrap") && strstr(text, "combo_draw=yes") &&
              strstr(text, "blockers=nut_flush"),
          "full key: \"%s\"", text);

    /* snprintf semantics. */
    size_t need = pe_hf_key_format(k, NULL, 0);
    /* not "small": windows.h defines it as a macro */
    char tiny[8];
    size_t got = pe_hf_key_format(k, tiny, sizeof(tiny));
    CHECK(need == strlen("made=high_card,flush_draw=nut") && got == need &&
              strlen(tiny) == sizeof(tiny) - 1,
          "format returns the full length and truncates (need %zu)", need);

    /* Deterministic: recomputing gives the same key. */
    pe_hand_features_t again;
    pe_hand_features_compute(&b, cards("AsQsAdJd3c"), &again);
    CHECK(pe_hf_key(&again, PE_HF_DIM_ALL) == pe_hf_key(&g, PE_HF_DIM_ALL),
          "same hand, same key");
}

/* ---------------------------------------------------------------- *
 * 4. Aggregation
 * ---------------------------------------------------------------- */

static void test_aggregation(void)
{
    pe_hf_board_t b;
    printf("  strategy aggregation\n");
    pe_hf_board_prepare(cards("Ks7d2c"), &b);

    /* Three overpairs, two top sets, one bottom set. Actions: check, bet
       half, bet pot. */
    static const double f_aa[3] = {0.2, 0.3, 0.5};
    static const double f_qq[3] = {0.6, 0.4, 0.0};
    static const double f_set[3] = {0.1, 0.1, 0.8};
    static const double e_aa[3] = {1.0, 1.5, 2.0};
    static const double e_set[3] = {4.0, 5.0, 6.0};
    pe_strategy_row_t rows[] = {
        {0, 1.0, f_aa, e_aa},   /* overpair */
        {0, 3.0, f_qq, NULL},   /* overpair (QQ < K: underpair) */
        {0, 2.0, f_aa, NULL},   /* overpair */
        {0, 1.0, f_set, e_set}, /* top set */
        {0, 1.0, f_set, e_set}, /* top set */
        {0, 2.0, f_qq, NULL},   /* bottom set */
    };
    rows[0].hand = cards("AsAh8c9d");
    rows[1].hand = cards("QsQh8c9d");
    rows[2].hand = cards("AdAc5h6h");
    rows[3].hand = cards("KhKd5s6s");
    rows[4].hand = cards("KhKc9s8s");
    rows[5].hand = cards("2h2sQdJd");

    pe_strategy_bucket_t out[8];
    size_t n = 0;
    pe_solver_status_t st = pe_strategy_bucket_aggregate(
        &b, rows, 6, 3, PE_HF_DIM_DETAIL, out, 8, &n);
    CHECK(st == PE_SOLVER_OK && n == 4, "four buckets (status %d, n %zu)",
          (int)st, n);

    size_t combos = 0;
    double share = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        combos += out[i].combos;
        share += out[i].weight_share;
        if (i)
            CHECK(out[i - 1].key < out[i].key, "buckets sorted by key");
    }
    CHECK(combos == 6, "every combo counted once (%zu)", combos);
    CHECK(fabs(share - 1.0) < 1e-12, "weight shares sum to 1 (%f)", share);

    for (size_t i = 0; i < n; ++i)
    {
        char text[64];
        pe_hf_key_format(out[i].key, text, sizeof(text));
        if (strcmp(text, "detail=overpair") == 0)
        {
            /* AA x2: weights 1 and 2, same frequencies; EV from row 0 only. */
            CHECK(out[i].combos == 2 && fabs(out[i].weight - 3.0) < 1e-12,
                  "overpair: 2 combos, weight 3");
            CHECK(fabs(out[i].weight_share - 3.0 / 10.0) < 1e-12,
                  "overpair share 30%%");
            CHECK(fabs(out[i].freq[2] - 0.5) < 1e-12, "overpair bets pot 50%%");
            CHECK(out[i].ev_combos == 1 && fabs(out[i].ev_weight - 1.0) < 1e-12 &&
                      fabs(out[i].ev[2] - 2.0) < 1e-12,
                  "overpair EV from the one row that has it");
            CHECK(fabs(out[i].ev_delta[0] + 1.0) < 1e-12 &&
                      fabs(out[i].ev_delta[2]) < 1e-12,
                  "overpair EV delta: check -1, pot 0");
        }
        else if (strcmp(text, "detail=underpair") == 0)
        {
            CHECK(out[i].combos == 1 && fabs(out[i].freq[0] - 0.6) < 1e-12 &&
                      out[i].ev_combos == 0 && out[i].ev[0] == 0.0,
                  "underpair: one combo, no EV");
        }
        else if (strcmp(text, "detail=top_set") == 0)
        {
            CHECK(out[i].combos == 2 && fabs(out[i].freq[2] - 0.8) < 1e-12 &&
                      fabs(out[i].ev[1] - 5.0) < 1e-12 &&
                      fabs(out[i].ev_delta[1] + 1.0) < 1e-12,
                  "top set: 2 combos, pot 80%%, EV 5 and delta -1 for half");
        }
        else if (strcmp(text, "detail=bottom_set") == 0)
        {
            CHECK(out[i].combos == 1, "bottom set: one combo");
        }
        else
        {
            CHECK(0, "unexpected bucket %s", text);
        }
    }

    /* Mixed weights inside a bucket are weight-averaged. */
    {
        pe_strategy_row_t two[2] = {rows[0], rows[1]};
        pe_strategy_bucket_t one[2];
        size_t m = 0;
        pe_strategy_bucket_aggregate(&b, two, 2, 3, PE_HF_DIM_MADE, one, 2, &m);
        /* both are pairs: (1 * 0.2 + 3 * 0.6) / 4 = 0.5 */
        CHECK(m == 1 && fabs(one[0].freq[0] - 0.5) < 1e-12,
              "weighted frequency (%f)", m ? one[0].freq[0] : -1.0);
    }

    /* Row order does not change the result. */
    {
        pe_strategy_row_t rev[6];
        pe_strategy_bucket_t out2[8];
        size_t n2 = 0;
        for (int i = 0; i < 6; ++i)
            rev[i] = rows[5 - i];
        pe_strategy_bucket_aggregate(&b, rev, 6, 3, PE_HF_DIM_DETAIL, out2, 8,
                                     &n2);
        int same = n2 == n;
        for (size_t i = 0; same && i < n; ++i)
        {
            same = out2[i].key == out[i].key && out2[i].combos == out[i].combos &&
                   fabs(out2[i].weight - out[i].weight) < 1e-12;
            for (int a = 0; a < 3 && same; ++a)
                same = fabs(out2[i].freq[a] - out[i].freq[a]) < 1e-12 &&
                       fabs(out2[i].ev[a] - out[i].ev[a]) < 1e-12;
        }
        CHECK(same, "reversed rows give the same buckets");
    }

    /* Errors. */
    CHECK(pe_strategy_bucket_aggregate(&b, rows, 6, 3, PE_HF_DIM_DETAIL, out, 3,
                                       &n) == PE_SOLVER_ERR_BUDGET_EXCEEDED,
          "three buckets of room for four");
    CHECK(pe_strategy_bucket_aggregate(&b, rows, 6, 0, PE_HF_DIM_DETAIL, out, 8,
                                       &n) == PE_SOLVER_ERR_INVALID_CONFIG,
          "zero actions refused");
    {
        pe_strategy_row_t bad = rows[0];
        bad.weight = -1.0;
        CHECK(pe_strategy_bucket_aggregate(&b, &bad, 1, 3, 0, out, 8, &n) ==
                  PE_SOLVER_ERR_INVALID_CONFIG,
              "negative weight refused");
        bad = rows[0];
        bad.hand = cards("KsAh8c9d"); /* Ks is on the board */
        CHECK(pe_strategy_bucket_aggregate(&b, &bad, 1, 3, 0, out, 8, &n) ==
                  PE_SOLVER_ERR_INVALID_CONFIG,
              "a hand using a board card refused");
        bad = rows[0];
        bad.hand = cards("AhKh");
        CHECK(pe_strategy_bucket_aggregate(&b, &bad, 1, 3, 0, out, 8, &n) ==
                  PE_SOLVER_ERR_INVALID_CONFIG,
              "a two-card hand refused");
    }
}

static void test_contract(void)
{
    pe_hf_board_t b;
    pe_hand_features_t f;
    mask_t holes[2];
    size_t failed = 99;

    printf("  contract\n");
    CHECK(pe_hf_board_prepare(cards("AsKs"), &b) == PE_SOLVER_ERR_INVALID_CONFIG,
          "a two-card board is refused");
    CHECK(pe_hf_board_prepare(cards("AsKsQsJsTs9s"), &b) ==
              PE_SOLVER_ERR_INVALID_CONFIG,
          "a six-card board is refused");
    pe_hf_board_prepare(cards("Ks7d2c"), &b);
    CHECK(pe_hand_features_compute(&b, cards("AsAhAcAdKh2s3s"), &f) ==
              PE_SOLVER_ERR_INVALID_CONFIG,
          "seven hole cards refused");
    holes[0] = cards("AsAh8c9d");
    holes[1] = cards("Ks8c9d2h"); /* Ks on the board */
    pe_hand_features_t batch_out[2];
    CHECK(pe_hand_features_compute_batch(&b, holes, 2, batch_out, &failed) ==
                  PE_SOLVER_ERR_INVALID_CONFIG &&
              failed == 1,
          "the batch reports the failing index (%zu)", failed);
}

int main(void)
{
    EvalConfig config = eval_config_omaha();
    EvalContext *ctx = eval_context_create(&config);
    if (!ctx)
    {
        fprintf(stderr, "EvalContext create failed\n");
        return 1;
    }

    printf("test_pe_hand_features: PLO hand features and strategy buckets\n");
    test_fixtures();
    printf("  random deals against brute force\n");
    for (int hole = 4; hole <= 6; ++hole)
    {
        test_random_deals(ctx, hole, 3, 400, 0x3A1ull + (uint64_t)hole, 1);
        test_random_deals(ctx, hole, 4, 400, 0x4A1ull + (uint64_t)hole, 1);
        test_random_deals(ctx, hole, 5, 1500, 0x5A1ull + (uint64_t)hole, 0);
    }
    test_keys();
    test_aggregation();
    test_contract();

    eval_context_destroy(ctx);
    if (g_failures)
    {
        fprintf(stderr, "test_pe_hand_features: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("test_pe_hand_features: all checks passed\n");
    return 0;
}
