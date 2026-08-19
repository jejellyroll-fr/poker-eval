/*
 * test_paytable_ev.c - Unit tests for the paytable EV / house edge engine.
 *
 * Acceptance criteria (ISSUE-07):
 *   - 9/6 Jacks or Better paytable EV verifies to 0.995439 +/- 0.00001
 *   - House edge for 9/6 JoB evaluates to 0.004561 (0.4561%)
 *   - Payout std-dev (sigma) matches theoretical figure ~4.42
 */

#include <poker_eval/economics/paytable_ev.h>

#include <assert.h>
#include <math.h>
#include <stdio.h>

static void test_generic_math(void)
{
    /* Coin-flip style paytable: 50% win 2, 50% win 0. */
    double payout[2] = { 2.0, 0.0 };
    double prob[2] = { 0.5, 0.5 };

    pe_paytable_result_t res;
    int rc = pe_paytable_compute_ev(payout, prob, 2, &res);
    assert(rc == 0);
    (void)rc;
    assert(fabs(res.total_ev - 1.0) < 1e-9);
    assert(fabs(res.house_edge - 0.0) < 1e-9);
    /* Var = E[X^2] - E[X]^2 = 0.5*4 - 1 = 1.0 */
    assert(fabs(res.variance - 1.0) < 1e-9);
    assert(fabs(res.std_dev - 1.0) < 1e-9);

    /* Invalid inputs */
    assert(pe_paytable_compute_ev(NULL, prob, 2, &res) == -1);
    assert(pe_paytable_compute_ev(payout, prob, 0, &res) == -1);
    assert(pe_paytable_compute_ev(payout, prob, 17, &res) == -1);

    printf("OK generic math\n");
}

static void test_jacks_or_better_9_6(void)
{
    pe_paytable_result_t res;
    int rc = pe_paytable_get_game(PE_VIDEO_POKER_JACKS_OR_BETTER_9_6, &res);
    assert(rc == 0);
    (void)rc;

    /* Acceptance: EV = 0.995439 +/- 0.00001 */
    assert(fabs(res.total_ev - 0.995439) < 1e-5);

    /* House edge = 1 - 0.995439 = 0.004561 (0.4561%) */
    assert(fabs(res.house_edge - 0.004561) < 1e-5);

    /* Std dev of per-hand payout ~ 4.42 */
    assert(fabs(res.std_dev - 4.42) < 0.01);

    /* Sanity: probabilities sum to 1 */
    double psum = 0.0;
    for (int i = 0; i < res.num_rows; ++i)
        psum += res.rows[i].probability;
    assert(fabs(psum - 1.0) < 1e-6);
    (void)psum;

    printf("OK 9/6 Jacks or Better (EV=%.6f, HE=%.6f, sigma=%.4f)\n",
           res.total_ev, res.house_edge, res.std_dev);
}

static void test_deuces_wild_full_pay(void)
{
    pe_paytable_result_t res;
    int rc = pe_paytable_get_game(PE_VIDEO_POKER_DEUCES_WILD_FULL_PAY, &res);
    assert(rc == 0);
    (void)rc;

    /* Full Pay Deuces Wild EV = 1.007620 (player advantage). */
    assert(fabs(res.total_ev - 1.007620) < 1e-5);
    assert(res.house_edge < 0.0);

    printf("OK Full Pay Deuces Wild (EV=%.6f)\n", res.total_ev);
}

static void test_joker_poker_kings_or_better(void)
{
    pe_paytable_result_t res;
    int rc = pe_paytable_get_game(PE_VIDEO_POKER_JOKER_POKER_KINGS_OR_BETTER,
                                  &res);
    assert(rc == 0);
    (void)rc;

    /* Joker Poker - Kings or Better EV = 1.006463. */
    assert(fabs(res.total_ev - 1.006463) < 1e-5);

    printf("OK Joker Poker Kings or Better (EV=%.6f)\n", res.total_ev);
}

int main(void)
{
    test_generic_math();
    test_jacks_or_better_9_6();
    test_deuces_wild_full_pay();
    test_joker_poker_kings_or_better();

    printf("All paytable_ev tests passed!\n");
    return 0;
}
