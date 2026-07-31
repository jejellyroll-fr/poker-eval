#include <poker_eval/equity.h>
#include <poker_eval/range.h>
#include <poker_eval/deck/deck_std.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>

#define EPSILON 0.01

static void test_heads_up_exact_with_flop(void) {
    printf("Testing Heads-Up Exact on Flop (AA vs KK, board 2c7d9h)...\n");
    pe_range_t *r1 = NULL;
    pe_range_t *r2 = NULL;
    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);

    pe_status_t s1 = pe_range_parse(game_holdem, "AA", dead, NULL, &r1);
    pe_status_t s2 = pe_range_parse(game_holdem, "KK", dead, NULL, &r2);
    assert(s1 == PE_STATUS_OK && r1);
    assert(s2 == PE_STATUS_OK && r2);
    (void)s1; (void)s2;

    StdDeck_CardMask board;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_HEARTS));

    pe_equity_result_multi_t result;
    pe_equity_opts_t opts = {0};
    opts.is_monte_carlo = 0; // Exact

    pe_status_t status = pe_equity_range_vs_range(NULL, game_holdem, r1, r2, board, dead, &opts, &result);
    assert(status == PE_STATUS_OK);
    assert(result.num_players == 2);
    assert(result.exact == 1);
    (void)status;

    printf("AA equity: %.4f\n", result.results[0].equity);
    printf("KK equity: %.4f\n", result.results[1].equity);

    assert(fabs((result.results[0].equity + result.results[1].equity) - 1.0) < EPSILON);
    assert(result.results[0].equity > result.results[1].equity);

    pe_range_free(r1);
    pe_range_free(r2);
    printf("PASS\n");
}

static void test_heads_up_monte_carlo(void) {
    printf("Testing Heads-Up Preflop Monte Carlo (AKs vs QJs)...\n");
    pe_range_t *r1 = NULL;
    pe_range_t *r2 = NULL;
    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);

    pe_status_t s1 = pe_range_parse(game_holdem, "AKs", dead, NULL, &r1);
    pe_status_t s2 = pe_range_parse(game_holdem, "QJs", dead, NULL, &r2);
    assert(s1 == PE_STATUS_OK && r1);
    assert(s2 == PE_STATUS_OK && r2);
    (void)s1; (void)s2;

    StdDeck_CardMask board;
    StdDeck_CardMask_RESET(board);

    pe_equity_result_multi_t result;
    pe_equity_opts_t opts = {0};
    opts.is_monte_carlo = 1;
    opts.iterations = 30000;

    pe_status_t status = pe_equity_range_vs_range(NULL, game_holdem, r1, r2, board, dead, &opts, &result);
    assert(status == PE_STATUS_OK);
    assert(result.exact == 0);
    assert(result.samples > 0);
    (void)status;

    printf("AKs equity: %.4f\n", result.results[0].equity);
    printf("QJs equity: %.4f\n", result.results[1].equity);

    // AKs vs QJs is approx 64% vs 36%
    assert(fabs(result.results[0].equity - 0.64) < 0.02);

    pe_range_free(r1);
    pe_range_free(r2);
    printf("PASS\n");
}

static void test_multiway(void) {
    printf("Testing Multiway (AA vs KK vs QQ)...\n");
    pe_range_t *r1 = NULL;
    pe_range_t *r2 = NULL;
    pe_range_t *r3 = NULL;
    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);

    if (pe_range_parse(game_holdem, "AA", dead, NULL, &r1) != PE_STATUS_OK) assert(0);
    if (pe_range_parse(game_holdem, "KK", dead, NULL, &r2) != PE_STATUS_OK) assert(0);
    if (pe_range_parse(game_holdem, "QQ", dead, NULL, &r3) != PE_STATUS_OK) assert(0);

    const pe_range_t *ranges[] = {r1, r2, r3};

    StdDeck_CardMask board;
    StdDeck_CardMask_RESET(board);

    pe_equity_result_multi_t result;
    pe_equity_opts_t opts = {0};
    opts.is_monte_carlo = 1;
    opts.iterations = 30000;

    pe_status_t status = pe_equity_multiway(NULL, game_holdem, ranges, 3, board, dead, &opts, &result);
    assert(status == PE_STATUS_OK);
    (void)status;

    printf("AA: %.4f, KK: %.4f, QQ: %.4f\n", result.results[0].equity, result.results[1].equity, result.results[2].equity);

    // AA wins most
    assert(result.results[0].equity > result.results[1].equity);
    assert(result.results[1].equity > result.results[2].equity);

    pe_range_free(r1);
    pe_range_free(r2);
    pe_range_free(r3);
    printf("PASS\n");
}

static void test_omaha_hilo(void) {
    printf("Testing Omaha Hi/Lo (A23K ds vs QJT9)...\n");
    pe_range_t *r1 = NULL;
    pe_range_t *r2 = NULL;
    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);

    // Exact hand
    if (pe_range_parse(game_omaha8, "As2s3hKh", dead, NULL, &r1) != PE_STATUS_OK) assert(0);
    if (pe_range_parse(game_omaha8, "QdJdTc9c", dead, NULL, &r2) != PE_STATUS_OK) assert(0);
    const pe_range_t *ranges[] = {r1, r2};

    StdDeck_CardMask board;
    StdDeck_CardMask_RESET(board);

    pe_equity_result_multi_t result;
    pe_equity_opts_t opts = {0};
    opts.is_monte_carlo = 1;
    opts.iterations = 20000;

    pe_status_t status = pe_equity_multiway(NULL, game_omaha8, ranges, 2, board, dead, &opts, &result);
    assert(status == PE_STATUS_OK);
    (void)status;

    printf("P1 Eq: %.4f (Hi: %.4f, Lo: %.4f, Scoop: %.4f)\n",
           result.results[0].equity,
           result.hilo_results[0].equity_hi,
           result.hilo_results[0].equity_lo,
           result.hilo_results[0].scoop_prob);

    /* The old check asserted equity == 0.5*hi + 0.5*lo, which assumes a
     * qualifying low always exists. In Omaha/8 it often does not, and the high
     * hand then takes the whole pot — with this matchup the low shares sum to
     * only ~0.58, so that model accounts for just ~0.79 of the pot and cannot
     * hold. Check the invariants that do hold instead. */
    double hi_sum = result.hilo_results[0].equity_hi + result.hilo_results[1].equity_hi;
    double lo_sum = result.hilo_results[0].equity_lo + result.hilo_results[1].equity_lo;
    double eq_sum = result.results[0].equity + result.results[1].equity;
    printf("Sums over players: equity=%.4f hi=%.4f lo=%.4f (low qualifies %.1f%% of the time)\n",
           eq_sum, hi_sum, lo_sum, lo_sum * 100.0);

    /* The pot is always fully distributed. */
    assert(fabs(eq_sum - 1.0) < 0.01);
    /* The high half is always awarded, so high shares sum to exactly one. */
    assert(fabs(hi_sum - 1.0) < 0.01);
    /* The low half is awarded only when a low qualifies, so its shares sum to
     * the frequency of that happening — under one, and not negligible here. */
    assert(lo_sum > 0.3 && lo_sum < 1.0);

    /* QdJdTc9c holds no card at or below eight, so it can never make a
     * qualifying low; As2s3hKh makes one whenever the board allows it. */
    assert(result.hilo_results[1].equity_lo == 0.0);
    assert(result.hilo_results[0].equity_lo > 0.3);

    pe_range_free(r1);
    pe_range_free(r2);
    printf("PASS\n");
}

int main(void) {
    test_heads_up_exact_with_flop();
    test_heads_up_monte_carlo();
    test_multiway();
    test_omaha_hilo();
    printf("All tests passed!\n");
    return 0;
}
