#include <poker_eval/engine/solvers/cfr/cfr_core.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT_TRUE(cond, msg)                         \
    do                                                 \
    {                                                  \
        if (!(cond))                                   \
        {                                              \
            fprintf(stderr, "Assertion failed: %s\n",  \
                    msg);                              \
            return 1;                                  \
        }                                              \
    } while (0)

#define ASSERT_NEAR(val, expected, tol, msg)                              \
    do                                                                    \
    {                                                                     \
        if (fabs((val) - (expected)) > (tol))                             \
        {                                                                 \
            fprintf(stderr, "Assertion failed: %s (got %.12f, want %.12f)\n", \
            msg, (double)(val), (double)(expected));                      \
            return 1;                                                     \
        }                                                                 \
    } while (0)

int main(void)
{
    cfr_storage_t *storage = cfr_storage_create();
    ASSERT_TRUE(storage != NULL, "storage allocation");

    const uint64_t KEY = 0xabcdull;

    /* Create entry with 2 actions. */
    double regret2[2] = {1.0, 0.0};
    double avg2[2] = {0.5, 0.5};
    cfr_storage_update_regret(storage, KEY, 2, regret2, 1.0);
    cfr_storage_update_avg(storage, KEY, 2, avg2, 4.0);
    ASSERT_TRUE(cfr_storage_count_infosets(storage) == 1, "one infoset");

    /* Make sure a second pass with the same size keeps the old data. */
    double regret2b[2] = {2.0, 1.0};
    cfr_storage_update_regret(storage, KEY, 2, regret2b, 1.0);
    double strat_same[2];
    cfr_storage_get_strategy(storage, KEY, 2, strat_same);
    ASSERT_NEAR(strat_same[0], 3.0 / 4.0, 1e-12, "same-size strategy a0");
    ASSERT_NEAR(strat_same[1], 1.0 / 4.0, 1e-12, "same-size strategy a1");

    /* Grow the same entry from 2 to 4 actions (exercises the
       realloc path inside get_entry). */
    double regret4[4] = {0.0, 0.0, 1.0, 1.0};
    /* Update with discount 1.0: existing actions keep their prior
       regret, new actions get regret4. */
    cfr_storage_update_regret(storage, KEY, 4, regret4, 1.0);
    double avg4[4];
    cfr_storage_get_avg_strategy(storage, KEY, 4, avg4);
    /* avg accumulated once with weight 4 at size 2 (0.5,0.5) then
       nothing at size 4, so only the first two actions are set. */
    ASSERT_NEAR(avg4[0], 0.5, 1e-12, "avg a0 after grow");
    ASSERT_NEAR(avg4[1], 0.5, 1e-12, "avg a1 after grow");
    ASSERT_NEAR(avg4[2], 0.0, 1e-12, "avg a2 after grow");
    ASSERT_NEAR(avg4[3], 0.0, 1e-12, "avg a3 after grow");

    /* Regret strategy: actions 0,1 keep 3.0,1.0; actions 2,3 are 1.0. */
    double s4[4];
    cfr_storage_get_strategy(storage, KEY, 4, s4);
    ASSERT_NEAR(s4[0], 0.5, 1e-12, "strategy a0 after grow");
    ASSERT_NEAR(s4[1], 1.0 / 6.0, 1e-12, "strategy a1 after grow");
    ASSERT_NEAR(s4[2], 1.0 / 6.0, 1e-12, "strategy a2 after grow");
    ASSERT_NEAR(s4[3], 1.0 / 6.0, 1e-12, "strategy a3 after grow");

    /* Shrink is also legal; update with 2 actions again. */
    double regret_small[2] = {0.0, 3.0};
    cfr_storage_update_regret(storage, KEY, 2, regret_small, 1.0);
    double s2[2];
    cfr_storage_get_strategy(storage, KEY, 2, s2);
    ASSERT_NEAR(s2[0], 3.0 / 7.0, 1e-12, "strategy a0 after shrink");
    ASSERT_NEAR(s2[1], 4.0 / 7.0, 1e-12, "strategy a1 after shrink");

    double ev_sum = 0.0;
    double ev_sq_sum = 0.0;
    uint64_t count = 0;
    if (cfr_storage_get_ev_stats(storage, KEY, &ev_sum, &ev_sq_sum, &count) == 0)
    {
        /* accumulate_ev creates its own minimal entry; not required here. */
        (void)count;
    }

    cfr_storage_destroy(storage);
    printf("CFR storage entry growth test passed.\n");
    return 0;
}