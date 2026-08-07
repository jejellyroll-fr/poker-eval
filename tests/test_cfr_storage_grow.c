#include <poker_eval/engine/solvers/cfr/cfr_core.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

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
                    msg, (double)(val), (double)(expected));              \
            return 1;                                                     \
        }                                                                 \
    } while (0)

int main(void)
{
    const uint64_t NUM_ENTRIES = 200000ull;
#define N_ACTIONS 3

    cfr_storage_t *storage = cfr_storage_create();
    ASSERT_TRUE(storage != NULL, "storage allocation");

    static double delta[3] = {1.0, 2.0, 3.0};

    for (uint64_t k = 0; k < NUM_ENTRIES; ++k)
    {
        uint64_t key = k * 2654435761u + 1; /* distinct, well spread */
        cfr_storage_update_regret(storage, key, N_ACTIONS, delta, 1.0);
        cfr_storage_update_avg(storage, key, N_ACTIONS, delta, 2.0);
    }

    ASSERT_TRUE(cfr_storage_count_infosets(storage) == NUM_ENTRIES,
                "all 200k infosets stored");

    double strat[N_ACTIONS];
    double avg[N_ACTIONS];
    double regrets[N_ACTIONS];
    for (uint64_t k = 0; k < NUM_ENTRIES; ++k)
    {
        uint64_t key = k * 2654435761u + 1;
        cfr_storage_get_strategy(storage, key, N_ACTIONS, strat);
        cfr_storage_get_avg_strategy(storage, key, N_ACTIONS, avg);

        ASSERT_TRUE(cfr_storage_peek_avg_strategy(storage, key, N_ACTIONS, regrets) == 0,
                    "peek avg strategy");
        ASSERT_NEAR(regrets[0], 1.0 / 6.0, 1e-6, "peek avg p0");
        ASSERT_NEAR(regrets[1], 2.0 / 6.0, 1e-6, "peek avg p1");
        ASSERT_NEAR(regrets[2], 3.0 / 6.0, 1e-6, "peek avg p2");
        ASSERT_NEAR(strat[0], 1.0 / 6.0, 1e-6, "regret strategy p0");
        ASSERT_NEAR(strat[1], 2.0 / 6.0, 1e-6, "regret strategy p1");
        ASSERT_NEAR(strat[2], 3.0 / 6.0, 1e-6, "regret strategy p2");
        ASSERT_NEAR(avg[0], 1.0 / 6.0, 1e-6, "avg strategy p0");
        ASSERT_NEAR(avg[1], 2.0 / 6.0, 1e-6, "avg strategy p1");
        ASSERT_NEAR(avg[2], 3.0 / 6.0, 1e-6, "avg strategy p2");
    }

    cfr_storage_destroy(storage);

    printf("CFR storage growth test passed (%llu infosets inserted and read back).\n",
           (unsigned long long)NUM_ENTRIES);
    return 0;
}
