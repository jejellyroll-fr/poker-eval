/*
 * test_paytable_ev.c - Unit tests for the paytable EV / house edge engine.
 *
 * Acceptance criteria (ISSUE-07):
 *   - 9/6 Jacks or Better paytable EV verifies to 0.995439 +/- 0.00001
 *   - House edge for 9/6 JoB evaluates to 0.004561 (0.4561%)
 *   - Payout std-dev (sigma) matches theoretical figure ~4.42
 *
 * NOTE: this file deliberately does not use assert(), because Release builds
 * define NDEBUG globally (top-level CMakeLists.txt) which would compile every
 * assertion out and turn the test into a no-op that always passes.
 */

#include <poker_eval/economics/paytable_ev.h>

#include <math.h>
#include <stdio.h>

static int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

static void test_generic_math(void)
{
    /* Coin-flip style paytable: 50% win 2, 50% win 0. */
    double payout[2] = { 2.0, 0.0 };
    double prob[2] = { 0.5, 0.5 };

    pe_paytable_result_t res;
    CHECK(pe_paytable_compute_ev(payout, prob, 2, &res) == 0);
    CHECK(fabs(res.total_ev - 1.0) < 1e-9);
    CHECK(fabs(res.house_edge - 0.0) < 1e-9);
    /* Var = E[X^2] - E[X]^2 = 0.5*4 - 1 = 1.0 */
    CHECK(fabs(res.variance - 1.0) < 1e-9);
    CHECK(fabs(res.std_dev - 1.0) < 1e-9);

    /* Per-row breakdown: ev_contribution = P_i * X_i */
    CHECK(res.num_rows == 2);
    CHECK(fabs(res.rows[0].ev_contribution - 1.0) < 1e-9);
    CHECK(fabs(res.rows[1].ev_contribution - 0.0) < 1e-9);

    /* out_result is optional. */
    CHECK(pe_paytable_compute_ev(payout, prob, 2, NULL) == 0);

    /* Invalid inputs */
    CHECK(pe_paytable_compute_ev(NULL, prob, 2, &res) == -1);
    CHECK(pe_paytable_compute_ev(payout, NULL, 2, &res) == -1);
    CHECK(pe_paytable_compute_ev(payout, prob, 0, &res) == -1);
    CHECK(pe_paytable_compute_ev(payout, prob, PE_PAYTABLE_MAX_ROWS + 1, &res)
          == -1);

    /* Probabilities that do not sum to 1 are rejected. */
    double bad_prob[2] = { 0.5, 0.2 };
    CHECK(pe_paytable_compute_ev(payout, bad_prob, 2, &res) == -1);

    /* Negative payouts are rejected. */
    double neg_payout[2] = { -1.0, 2.0 };
    CHECK(pe_paytable_compute_ev(neg_payout, prob, 2, &res) == -1);

    /* Non-finite entries are rejected (NaN compares false against every
     * bound, so this would otherwise produce a NaN EV). */
    double nan_prob[2] = { 0.5, 0.0 };
    nan_prob[1] = nan("");
    CHECK(pe_paytable_compute_ev(payout, nan_prob, 2, &res) == -1);

    double nan_payout[2] = { 0.0, 0.0 };
    nan_payout[0] = nan("");
    nan_payout[1] = 2.0;
    CHECK(pe_paytable_compute_ev(nan_payout, prob, 2, &res) == -1);

    /* Infinite entries are rejected too. */
    double inf_payout[2] = { HUGE_VAL, 2.0 };
    CHECK(pe_paytable_compute_ev(inf_payout, prob, 2, &res) == -1);

    double inf_prob[2] = { HUGE_VAL, 0.0 };
    CHECK(pe_paytable_compute_ev(payout, inf_prob, 2, &res) == -1);

    printf("OK generic math\n");
}

static void test_jacks_or_better_9_6(void)
{
    pe_paytable_result_t res;
    CHECK(pe_paytable_get_game(PE_VIDEO_POKER_JACKS_OR_BETTER_9_6, &res) == 0);

    /* Acceptance: EV = 0.995439 +/- 0.00001 */
    CHECK(fabs(res.total_ev - 0.995439) < 1e-5);

    /* House edge = 1 - 0.995439 = 0.004561 (0.4561%) */
    CHECK(fabs(res.house_edge - 0.004561) < 1e-5);

    /* Std dev of per-hand payout ~ 4.42 */
    CHECK(fabs(res.std_dev - 4.42) < 0.01);

    /* Sanity: probabilities sum to 1 */
    double psum = 0.0;
    for (int i = 0; i < res.num_rows; ++i)
        psum += res.rows[i].probability;
    CHECK(fabs(psum - 1.0) < 1e-6);

    /* Total EV is the sum of the per-category contributions. */
    double ev_sum = 0.0;
    for (int i = 0; i < res.num_rows; ++i)
        ev_sum += res.rows[i].ev_contribution;
    CHECK(fabs(ev_sum - res.total_ev) < 1e-9);

    /* Named games carry an identifier and per-category labels. */
    CHECK(res.num_rows == 10);
    CHECK(res.rows[0].category_name != NULL);
    CHECK(res.game_name[0] != '\0');

    printf("OK 9/6 Jacks or Better (EV=%.6f, HE=%.6f, sigma=%.4f)\n",
           res.total_ev, res.house_edge, res.std_dev);
}

static void test_deuces_wild_full_pay(void)
{
    pe_paytable_result_t res;
    CHECK(pe_paytable_get_game(PE_VIDEO_POKER_DEUCES_WILD_FULL_PAY, &res) == 0);

    /* Full Pay Deuces Wild EV = 1.007620 (player advantage). */
    CHECK(fabs(res.total_ev - 1.007620) < 1e-5);
    CHECK(res.house_edge < 0.0);
    CHECK(fabs(res.std_dev - 5.0831) < 0.01);

    printf("OK Full Pay Deuces Wild (EV=%.6f, sigma=%.4f)\n",
           res.total_ev, res.std_dev);
}

static void test_joker_poker_kings_or_better(void)
{
    pe_paytable_result_t res;
    CHECK(pe_paytable_get_game(PE_VIDEO_POKER_JOKER_POKER_KINGS_OR_BETTER,
                               &res) == 0);

    /* Joker Poker - Kings or Better EV = 1.006463. */
    CHECK(fabs(res.total_ev - 1.006463) < 1e-5);
    CHECK(fabs(res.std_dev - 5.1230) < 0.01);

    printf("OK Joker Poker Kings or Better (EV=%.6f, sigma=%.4f)\n",
           res.total_ev, res.std_dev);
}

static void test_registry_helpers(void)
{
    /* pe_paytable_game_ev agrees with pe_paytable_get_game for every game. */
    for (int g = 0; g < PE_VIDEO_POKER_COUNT; ++g) {
        pe_paytable_result_t res;
        double ev = 0.0;
        CHECK(pe_paytable_get_game((pe_video_poker_game_t)g, &res) == 0);
        CHECK(pe_paytable_game_ev((pe_video_poker_game_t)g, &ev) == 0);
        CHECK(fabs(ev - res.total_ev) < 1e-12);
    }

    /* Invalid game ids and NULL outputs are rejected. */
    pe_paytable_result_t res;
    double ev = 0.0;
    CHECK(pe_paytable_get_game(PE_VIDEO_POKER_COUNT, &res) == -1);
    CHECK(pe_paytable_get_game(PE_VIDEO_POKER_JACKS_OR_BETTER_9_6, NULL) == -1);
    CHECK(pe_paytable_game_ev(PE_VIDEO_POKER_COUNT, &ev) == -1);
    CHECK(pe_paytable_game_ev(PE_VIDEO_POKER_JACKS_OR_BETTER_9_6, NULL) == -1);

    /* pe_paytable_print must tolerate a NULL result. */
    pe_paytable_print(NULL);

    printf("OK registry helpers\n");
}

int main(void)
{
    test_generic_math();
    test_jacks_or_better_9_6();
    test_deuces_wild_full_pay();
    test_joker_poker_kings_or_better();
    test_registry_helpers();

    if (g_failures != 0) {
        printf("%d paytable_ev check(s) FAILED\n", g_failures);
        return 1;
    }

    printf("All paytable_ev tests passed!\n");
    return 0;
}
