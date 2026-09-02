/*
 * test_pe_analysis_model.c - the Studio's equity/ICM analysis, headless.
 *
 * The panel that uses this cannot be asserted on, so everything worth being
 * sure of lives here: the answers on cases with a known closed form, and the
 * refusals, because these inputs are typed by hand and a plausible-looking
 * wrong number is the failure mode that matters.
 */

#include "pe_analysis_model.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition, ...)                                      \
    do                                                             \
    {                                                              \
        if (!(condition))                                          \
        {                                                          \
            fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                          \
            fputc('\n', stderr);                                   \
            failures++;                                            \
        }                                                          \
    } while (0)

/* AA against KK on a blank board: the classic ~82/18, and exactly 4.5%
   of the time it is a chopped board. Wide tolerances -- this pins the
   plumbing and the orientation of the result, not the evaluator. */
static void test_known_preflop_matchup(void)
{
    pe_analysis_equity_request_t request;
    pe_analysis_equity_report_t report;

    memset(&request, 0, sizeof(request));
    request.game = game_holdem;
    request.ranges[0] = "AA";
    request.ranges[1] = "KK";
    request.player_count = 2;
    request.board = "";
    request.dead = "";
    CHECK(pe_analysis_equity(&request, &report) == 0,
          "AA vs KK was refused: %s", report.error);
    if (report.player_count != 2)
        return;
    CHECK(report.equity[0] > 0.79 && report.equity[0] < 0.85,
          "AA has %.4f equity against KK, expected about 0.82",
          report.equity[0]);
    CHECK(fabs(report.equity[0] + report.equity[1] - 1.0) < 1.0e-6,
          "equities sum to %.6f, not 1",
          report.equity[0] + report.equity[1]);
    CHECK(report.combos[0] == 6u && report.combos[1] == 6u,
          "AA/KK should be 6 combos each, got %zu/%zu",
          report.combos[0], report.combos[1]);
}

/* A hand already beaten with one card to come, where the answer is exact and
   independent of any sampling: 72o cannot beat AA on this board. */
static void test_drawing_dead_is_exact_zero(void)
{
    pe_analysis_equity_request_t request;
    pe_analysis_equity_report_t report;

    memset(&request, 0, sizeof(request));
    request.game = game_holdem;
    request.ranges[0] = "AcAd";
    request.ranges[1] = "7h2s";
    request.player_count = 2;
    request.board = "AhAs3d4c";
    request.dead = "";
    CHECK(pe_analysis_equity(&request, &report) == 0,
          "quad aces was refused: %s", report.error);
    if (report.player_count != 2)
        return;
    CHECK(report.equity[0] == 1.0 && report.equity[1] == 0.0,
          "drawing dead is %.6f / %.6f, expected 1 / 0",
          report.equity[0], report.equity[1]);
}

static void test_equity_refusals(void)
{
    pe_analysis_equity_request_t request;
    pe_analysis_equity_report_t report;

    memset(&request, 0, sizeof(request));
    request.game = game_holdem;
    request.ranges[0] = "AA";
    request.ranges[1] = "KK";
    request.player_count = 2;

    request.board = "AhKd";
    CHECK(pe_analysis_equity(&request, &report) != 0 &&
              report.error[0] != '\0',
          "a two-card board was accepted");

    request.board = "AhAh7s";
    CHECK(pe_analysis_equity(&request, &report) != 0,
          "a board with a repeated card was accepted");

    request.board = "AhXd7s";
    CHECK(pe_analysis_equity(&request, &report) != 0,
          "a board with a bad rank was accepted");

    request.board = "";
    request.ranges[1] = "";
    CHECK(pe_analysis_equity(&request, &report) != 0,
          "an empty range was accepted");

    /* Every AA combo is blocked by this board, so there is nothing to solve
       and saying "0% equity" would be a wrong answer, not an empty one. */
    request.ranges[1] = "KK";
    request.ranges[0] = "AA";
    request.board = "AhAsAd";
    CHECK(pe_analysis_equity(&request, &report) != 0,
          "a fully blocked range was accepted");

    /* Both ranges parse, but the only two hands share Ac. This must be
       rejected before the evaluator emits per-matchup failures. */
    request.board = "";
    request.ranges[0] = "AcAd";
    request.ranges[1] = "AcKd";
    CHECK(pe_analysis_equity(&request, &report) != 0 &&
              strstr(report.error, "compatible matchups") != NULL,
          "fully conflicting ranges were accepted: %s", report.error);
}

static void test_number_separators(void)
{
    double values[2];
    int count = 0;
    char error[PE_ANALYSIS_ERROR_MAX];

    CHECK(pe_analysis_parse_numbers("10, 20", values, 2, &count,
                                   error, sizeof(error)) == 0 && count == 2,
          "comma-separated numbers were not parsed");
    CHECK(pe_analysis_parse_numbers("10.5.5", values, 2, &count,
                                   error, sizeof(error)) != 0,
          "adjacent numeric tokens without a separator were accepted");
}

/* On a paired board with a flush possible, the classes a range falls into are
   checkable by hand. AA on Ah7h2h: every combo holding Ah is a flush; the
   rest are trip aces. */
static void test_breakdown_classes(void)
{
    pe_analysis_breakdown_t report;

    CHECK(pe_analysis_breakdown(game_holdem, "AA", "Ah7h2h", "", &report) == 0,
          "breakdown was refused: %s", report.error);
    /* AA has 6 combos; the board takes Ah, leaving 3 (AsAd, AsAc, AdAc). */
    CHECK(report.live_combos == 3u,
          "%zu live combos, expected 3", report.live_combos);
    CHECK(report.blocked_combos == 3u,
          "%zu blocked combos, expected 3", report.blocked_combos);
    CHECK(fabs(report.share[PE_HAND_CLASS_TRIPS] - 1.0) < 1.0e-9,
          "AA on Ah7h2h should be all trips, trips share is %.4f",
          report.share[PE_HAND_CLASS_TRIPS]);

    /* A hand that makes the flush, and one that makes a straight. */
    CHECK(pe_analysis_breakdown(game_holdem, "KhQh", "Ah7h2h", "",
                                &report) == 0,
          "flush breakdown was refused: %s", report.error);
    CHECK(fabs(report.share[PE_HAND_CLASS_FLUSH] - 1.0) < 1.0e-9,
          "KhQh on a three-heart board should be a flush, got %.4f",
          report.share[PE_HAND_CLASS_FLUSH]);

    CHECK(pe_analysis_breakdown(game_holdem, "5c4c", "6h7d8s", "",
                                &report) == 0,
          "straight breakdown was refused: %s", report.error);
    CHECK(fabs(report.share[PE_HAND_CLASS_STRAIGHT] - 1.0) < 1.0e-9,
          "5c4c on 6h7d8s should be a straight, got %.4f",
          report.share[PE_HAND_CLASS_STRAIGHT]);

    /* Shares are a distribution over the live combos. */
    CHECK(pe_analysis_breakdown(game_holdem, "22+,A2s+", "Ah7h2h", "",
                                &report) == 0,
          "wide breakdown was refused: %s", report.error);
    {
        double total = 0.0;
        int i;
        for (i = 0; i < PE_HAND_CLASS_COUNT; ++i)
            total += report.share[i];
        CHECK(fabs(total - 1.0) < 1.0e-9,
              "shares sum to %.6f, not 1", total);
    }
}

static void test_breakdown_refusals(void)
{
    pe_analysis_breakdown_t report;

    CHECK(pe_analysis_breakdown(game_holdem, "AA", "", "", &report) != 0,
          "a preflop breakdown was accepted");
    CHECK(pe_analysis_breakdown(game_holdem, "AA", "AhKd", "", &report) != 0,
          "a two-card board was accepted");
    CHECK(pe_analysis_breakdown(game_omaha, "AA", "Ah7h2h", "", &report) != 0,
          "Omaha was accepted by the Hold'em classifier");
}

/*
 * Two ICM facts that hold for any correct implementation and are checkable
 * without reproducing the recursion: equity sums to one, and with all stacks
 * equal every player has the same equity regardless of the ladder.
 */
static void test_icm_invariants(void)
{
    pe_analysis_icm_request_t request;
    pe_analysis_icm_report_t report;
    double total = 0.0;
    int i;

    memset(&request, 0, sizeof(request));
    request.stacks = "5000, 3000, 2000";
    request.payouts = "500, 300, 200";
    CHECK(pe_analysis_icm(&request, &report) == 0,
          "ICM was refused: %s", report.error);
    if (report.player_count != 3)
        return;
    for (i = 0; i < report.player_count; ++i)
        total += report.equity[i];
    CHECK(fabs(total - 1.0) < 1.0e-9, "ICM equity sums to %.9f, not 1", total);
    CHECK(fabs(report.prize_pool - 1000.0) < 1.0e-9,
          "prize pool is %.2f, expected 1000", report.prize_pool);
    /* The chip leader owns half the chips but must be worth less than half
       the pool: that compression is the entire point of ICM. */
    CHECK(report.chip_share[0] > report.equity[0],
          "the chip leader's ICM equity (%.4f) is not below the chip share "
          "(%.4f)", report.equity[0], report.chip_share[0]);
    CHECK(report.equity[0] > report.equity[1] &&
              report.equity[1] > report.equity[2],
          "ICM equity is not ordered by stack");

    request.stacks = "1000, 1000, 1000";
    CHECK(pe_analysis_icm(&request, &report) == 0,
          "equal stacks were refused: %s", report.error);
    CHECK(fabs(report.equity[0] - report.equity[1]) < 1.0e-9 &&
              fabs(report.equity[1] - report.equity[2]) < 1.0e-9,
          "equal stacks gave unequal equity");
    CHECK(fabs(report.ev[0] - report.prize_pool / 3.0) < 1.0e-6,
          "equal stacks: EV is %.4f, expected %.4f",
          report.ev[0], report.prize_pool / 3.0);

    memset(&request, 0, sizeof(request));
    request.stacks = "5000, 3000, 2000";
    request.payouts = "500, 300, 200";
    request.mode = PE_ANALYSIS_TOURNAMENT_CHIP_EV;
    CHECK(pe_analysis_icm(&request, &report) == 0,
          "ChipEV was refused: %s", report.error);
    CHECK(fabs(report.equity[0] - 0.5) < 1.0e-9 &&
              fabs(report.ev[0] - 500.0) < 1.0e-9,
          "ChipEV did not preserve raw stack share");

    memset(&request, 0, sizeof(request));
    request.stacks = "5000, 3000, 2000";
    request.payouts = "500, 300, 200";
    request.mode = PE_ANALYSIS_TOURNAMENT_FGS;
    request.fgs_depth = 1;
    request.fgs_pot = "100";
    request.fgs_win_probabilities = "50, 30, 20";
    CHECK(pe_analysis_icm(&request, &report) == 0,
          "FGS was refused: %s", report.error);
    CHECK(report.fgs_leaf_count == 3u,
          "FGS produced %zu leaves instead of 3", report.fgs_leaf_count);
    total = 0.0;
    for (i = 0; i < report.player_count; ++i)
        total += report.equity[i];
    CHECK(fabs(total - 1.0) < 1.0e-9,
          "FGS equity sums to %.9f, not 1", total);
}

static void test_icm_refusals(void)
{
    pe_analysis_icm_request_t request;
    pe_analysis_icm_report_t report;

    memset(&request, 0, sizeof(request));
    request.stacks = "1000";
    request.payouts = "100";
    CHECK(pe_analysis_icm(&request, &report) != 0,
          "a one-player tournament was accepted");

    request.stacks = "1000, 1000";
    request.payouts = "100, 200, 300";
    CHECK(pe_analysis_icm(&request, &report) != 0,
          "more payouts than players was accepted");

    request.payouts = "100, 200";
    CHECK(pe_analysis_icm(&request, &report) != 0,
          "an increasing payout ladder was accepted");

    request.stacks = "1000, 0";
    request.payouts = "100";
    CHECK(pe_analysis_icm(&request, &report) != 0,
          "a zero stack was accepted");

    request.stacks = "1000, abc";
    CHECK(pe_analysis_icm(&request, &report) != 0,
          "a non-numeric stack was accepted");

    request.stacks = "1000, -50";
    CHECK(pe_analysis_icm(&request, &report) != 0,
          "a negative stack was accepted");
}

static void test_icm_decision(void)
{
    pe_analysis_icm_decision_request_t request;
    pe_analysis_icm_decision_report_t report;

    memset(&request, 0, sizeof(request));
    request.stacks = "5000, 3000, 2000";
    request.payouts = "500, 300, 200";
    request.hero_index = 0;
    request.opponent_index = 1;
    request.win_probability = 0.60;
    request.chips_at_risk = 500.0;
    request.chips_to_win = 500.0;
    CHECK(pe_analysis_icm_decision(&request, &report) == 0,
          "ICM decision was refused: %s", report.error);
    CHECK(fabs(report.effective_win - 500.0) < 1.0e-9 &&
              fabs(report.effective_loss - 500.0) < 1.0e-9,
          "ICM decision did not clamp/transfer the requested chips");
    CHECK(fabs(report.decision_ev -
               (0.60 * report.win_ev + 0.40 * report.lose_ev)) < 1.0e-9,
          "ICM decision EV was not probability weighted");

    /* An all-in outcome legitimately busts either side.  The decision helper
     * must compact zero-stack players before calling the positive-stack ICM
     * core rather than rejecting the whole decision. */
    request.stacks = "100, 100";
    request.payouts = "150, 50";
    request.hero_index = 0;
    request.opponent_index = 1;
    request.win_probability = 0.5;
    request.chips_at_risk = 100.0;
    request.chips_to_win = 100.0;
    CHECK(pe_analysis_icm_decision(&request, &report) == 0,
          "all-in ICM decision was refused: %s", report.error);
    CHECK(fabs(report.win_ev - 150.0) < 1.0e-9 &&
              fabs(report.lose_ev) < 1.0e-9,
          "busted ICM outcome was not evaluated correctly: win %.17g, lose %.17g",
          report.win_ev, report.lose_ev);
}

int main(void)
{
    test_known_preflop_matchup();
    test_drawing_dead_is_exact_zero();
    test_equity_refusals();
    test_number_separators();
    test_breakdown_classes();
    test_breakdown_refusals();
    test_icm_invariants();
    test_icm_refusals();
    test_icm_decision();
    if (failures != 0)
        return 1;
    puts("test_pe_analysis_model: all tests passed");
    return 0;
}
