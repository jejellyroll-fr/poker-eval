#include <poker_eval/economics/rake.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>

static void test_rake_calculation(void)
{
    rake_config_t config = {0.05, 5.0, 0.0, 0}; /* 5% rake, cap 5 */

    /* Small pot: 10.0 * 0.05 = 0.5 */
    double net = pe_apply_rake(10.0, &config);
    assert(fabs(net - 9.5) < 1e-6);

    /* Large pot: 200.0 * 0.05 = 10.0 -> capped at 5.0 */
    net = pe_apply_rake(200.0, &config);
    assert(fabs(net - 195.0) < 1e-6);

    printf("✓ Rake calculation passed\n");
    (void)net; /* Silence unused variable warning */
}

static void test_rake_distribution(void)
{
    rake_config_t config = {0.05, 5.0, 0.0, 0};
    double winnings[2];

    /* Pot 100.0 -> Rake 5.0 -> Net 95.0 -> Share 47.5 */
    double total_rake = pe_distribute_pot_with_rake(100.0, 2, winnings, &config);

    assert(fabs(total_rake - 5.0) < 1e-6);
    assert(fabs(winnings[0] - 47.5) < 1e-6);
    assert(fabs(winnings[1] - 47.5) < 1e-6);

    printf("✓ Rake distribution passed\n");
    (void)total_rake;
}

static void test_uncalled_bet_is_not_raked(void)
{
    rake_config_t config = {0.10, 100.0, 0.0, 0};
    /* 10 chips are called and 90 are returned to the bettor. */
    assert(fabs(pe_apply_rake_excluding_uncalled(100.0, 90.0, &config) - 99.0) < 1e-9);
    /* Invalid/excess unmatched amounts are safely clamped. */
    assert(fabs(pe_apply_rake_excluding_uncalled(100.0, 150.0, &config) - 100.0) < 1e-9);
}

int main(void)
{
    test_rake_calculation();
    test_rake_distribution();
    test_uncalled_bet_is_not_raked();
    printf("All Rake tests passed!\n");
    return 0;
}
