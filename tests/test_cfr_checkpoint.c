#include <poker_eval/engine/solvers/cfr/cfr_core.h>

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

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

struct stats_ctx
{
    uint64_t key;
    double mean;
    int seen;
};

static void stats_cb(uint64_t key,
                     int n_actions,
                     const double *regret,
                     const double *avg,
                     double ev_sum,
                     double ev_sq_sum,
                     uint64_t sample_count,
                     void *user)
{
    (void)n_actions;
    (void)regret;
    (void)avg;
    struct stats_ctx *ctx = (struct stats_ctx *)user;
    if (ctx->key == key)
    {
        ctx->mean = (sample_count > 0) ? (ev_sum / (double)sample_count) : 0.0;
        ctx->seen += 1;
    }
}

static int make_checkpoint_path(char *buffer, size_t len)
{
    if (!buffer || len == 0)
        return -1;
#ifdef _WIN32
    char tmp_path[MAX_PATH];
    DWORD path_len = GetTempPathA((DWORD)sizeof(tmp_path), tmp_path);
    if (path_len == 0 || path_len > sizeof(tmp_path))
        return -1;
    if (GetTempFileNameA(tmp_path, "cfr", 0, buffer) == 0)
        return -1;
    FILE *f = fopen(buffer, "wb");
    if (!f)
        return -1;
    fclose(f);
    return 0;
#else
    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir || !*tmpdir)
        tmpdir = "/tmp";
    int written = snprintf(buffer, len, "%s/cfr_checkpoint_testXXXXXX", tmpdir);
    if (written < 0 || (size_t)written >= len)
        return -1;
    int fd = mkstemp(buffer);
    if (fd < 0)
        return -1;
    close(fd);
    return 0;
#endif
}

int main(void)
{
    char path[512];
    if (make_checkpoint_path(path, sizeof(path)) != 0)
    {
        fprintf(stderr, "Failed to generate checkpoint path: %s\n",
                strerror(errno));
        return 1;
    }

    cfr_storage_t *storage = cfr_storage_create();
    ASSERT_TRUE(storage != NULL, "storage allocation");

    const uint64_t KEY_A = 0x1234ull;
    const uint64_t KEY_B = 0x4567ull;

    double regret_a[2] = {1.0, -0.5};
    double avg_a[2] = {0.2, 0.8};
    cfr_storage_update_regret(storage, KEY_A, 2, regret_a, 1.0);
    cfr_storage_update_avg(storage, KEY_A, 2, avg_a, 10.0);
    cfr_storage_accumulate_ev(storage, KEY_A, 0.5);
    cfr_storage_accumulate_ev(storage, KEY_A, 1.5);

    double regret_b[3] = {0.1, 0.2, 0.3};
    double avg_b[3] = {0.3, 0.3, 0.4};
    cfr_storage_update_regret(storage, KEY_B, 3, regret_b, 1.0);
    cfr_storage_update_avg(storage, KEY_B, 3, avg_b, 5.0);

    ASSERT_TRUE(cfr_storage_save_checkpoint(storage, path, 42) == 0,
                "save checkpoint");

    cfr_storage_destroy(storage);

    cfr_storage_t *resume = cfr_storage_create();
    ASSERT_TRUE(resume != NULL, "resume allocation");

    uint64_t iteration = 0;
    ASSERT_TRUE(cfr_storage_load_checkpoint(resume, path, &iteration) == 0,
                "load checkpoint");
    ASSERT_TRUE(iteration == 42, "iteration roundtrip");

    ASSERT_TRUE(cfr_storage_count_infosets(resume) == 2, "entry count");

    double strat_a[2];
    cfr_storage_get_strategy(resume, KEY_A, 2, strat_a);
    ASSERT_NEAR(strat_a[0], 1.0, 1e-12, "regret strategy action 0");
    ASSERT_NEAR(strat_a[1], 0.0, 1e-12, "regret strategy action 1");

    double strat_b[3];
    cfr_storage_get_strategy(resume, KEY_B, 3, strat_b);
    ASSERT_NEAR(strat_b[0], regret_b[0] / 0.6, 1e-12, "regret strategy action 0");
    ASSERT_NEAR(strat_b[1], regret_b[1] / 0.6, 1e-12, "regret strategy action 1");
    ASSERT_NEAR(strat_b[2], regret_b[2] / 0.6, 1e-12, "regret strategy action 2");

    double avg_out_a[2];
    cfr_storage_get_avg_strategy(resume, KEY_A, 2, avg_out_a);
    ASSERT_NEAR(avg_out_a[0], avg_a[0], 1e-12, "avg strategy action 0");
    ASSERT_NEAR(avg_out_a[1], avg_a[1], 1e-12, "avg strategy action 1");

    double avg_out_b[3];
    cfr_storage_get_avg_strategy(resume, KEY_B, 3, avg_out_b);
    ASSERT_NEAR(avg_out_b[0], avg_b[0], 1e-12, "avg strategy B action 0");
    ASSERT_NEAR(avg_out_b[1], avg_b[1], 1e-12, "avg strategy B action 1");
    ASSERT_NEAR(avg_out_b[2], avg_b[2], 1e-12, "avg strategy B action 2");

    struct stats_ctx ctx = {KEY_A, 0.0, 0};
    cfr_storage_iterate_stats(resume, stats_cb, &ctx);
    ASSERT_TRUE(ctx.seen == 1, "stats iterate hit");
    ASSERT_NEAR(ctx.mean, 1.0, 1e-12, "EV roundtrip");

    cfr_storage_destroy(resume);
    remove(path);

    printf("CFR checkpoint save/load test passed.\n");
    return 0;
}
