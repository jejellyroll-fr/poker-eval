/*
 * test_cfr_strategy_mode.c - EXT-01: strategy extraction is per storage
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * The mode used to live in a thread-local static, which meant two solves in
 * one process shared one ECFR temperature: whichever ran cfr_solve() last
 * decided how both extracted their strategies. Nothing reported it, and the
 * loser simply produced a different policy than it asked for.
 *
 * What this pins is that the setting now belongs to the storage — two of them,
 * configured differently, must disagree on the same regrets.
 */

#include <poker_eval/engine/solvers/cfr/cfr_core.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(cond, ...)                                       \
    do                                                         \
    {                                                          \
        if (!(cond))                                           \
        {                                                      \
            fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                      \
            fprintf(stderr, "\n");                             \
            g_failures++;                                      \
        }                                                      \
    } while (0)

/* Regrets chosen so regret matching and the exponential policy disagree
   visibly, and so two temperatures disagree with each other. */
static void seed_regrets(cfr_storage_t *s, uint64_t key)
{
    const double delta[3] = { 3.0, 1.0, 0.5 };
    cfr_storage_update_regret(s, key, 3, delta, 1.0);
}

static int same_strategy(const double *a, const double *b, int n, double eps)
{
    int i;
    for (i = 0; i < n; ++i)
        if (fabs(a[i] - b[i]) > eps)
            return 0;
    return 1;
}

static void test_two_storages_do_not_share_a_mode(void)
{
    cfr_storage_t *cold = cfr_storage_create();
    cfr_storage_t *hot = cfr_storage_create();
    double s_cold[3];
    double s_hot[3];

    CHECK(cold != NULL && hot != NULL, "storage creation failed");
    if (cold == NULL || hot == NULL)
        return;

    seed_regrets(cold, 1);
    seed_regrets(hot, 1);

    cfr_storage_set_strategy_mode_for(cold, 1, 0.25);
    cfr_storage_set_strategy_mode_for(hot, 1, 4.0);

    cfr_storage_get_strategy(cold, 1, 3, s_cold);
    cfr_storage_get_strategy(hot, 1, 3, s_hot);

    CHECK(!same_strategy(s_cold, s_hot, 3, 1e-9),
          "two temperatures produced the same policy: %.6f/%.6f/%.6f",
          s_cold[0], s_cold[1], s_cold[2]);

    /* A high temperature concentrates on the best action, a low one spreads. */
    CHECK(s_hot[0] > s_cold[0],
          "lambda 4.0 should concentrate more than lambda 0.25 (%.6f vs %.6f)",
          s_hot[0], s_cold[0]);

    cfr_storage_destroy(cold);
    cfr_storage_destroy(hot);
}

static void test_setting_one_storage_leaves_the_other_alone(void)
{
    cfr_storage_t *a = cfr_storage_create();
    cfr_storage_t *b = cfr_storage_create();
    double before[3];
    double after[3];

    CHECK(a != NULL && b != NULL, "storage creation failed");
    if (a == NULL || b == NULL)
        return;

    seed_regrets(a, 7);
    seed_regrets(b, 7);

    cfr_storage_get_strategy(a, 7, 3, before);

    /* The old global would have changed `a` too. */
    cfr_storage_set_strategy_mode_for(b, 1, 2.0);

    cfr_storage_get_strategy(a, 7, 3, after);
    CHECK(same_strategy(before, after, 3, 0.0),
          "configuring one storage changed another's policy");

    cfr_storage_destroy(a);
    cfr_storage_destroy(b);
}

static void test_defaults_and_clamping(void)
{
    cfr_storage_t *s = cfr_storage_create();
    double regret_matched[3];
    double defaulted[3];

    CHECK(s != NULL, "storage creation failed");
    if (s == NULL)
        return;

    seed_regrets(s, 3);

    /* A fresh storage extracts by regret matching: 3/(3+1+0.5) etc. */
    cfr_storage_get_strategy(s, 3, 3, defaulted);
    CHECK(fabs(defaulted[0] - 3.0 / 4.5) < 1e-12,
          "a fresh storage should regret-match, got %.17g", defaulted[0]);

    /* A non-positive temperature is clamped to the neutral 1.0 rather than
       producing exp(0 * r) — a constant, hence a silently uniform policy. */
    cfr_storage_set_strategy_mode_for(s, 1, 1.0);
    cfr_storage_get_strategy(s, 3, 3, regret_matched);
    cfr_storage_set_strategy_mode_for(s, 1, -5.0);
    cfr_storage_get_strategy(s, 3, 3, defaulted);
    CHECK(same_strategy(regret_matched, defaulted, 3, 0.0),
          "a non-positive lambda was not clamped to 1.0");

    /* Turning the mode back off restores regret matching. */
    cfr_storage_set_strategy_mode_for(s, 0, 1.0);
    cfr_storage_get_strategy(s, 3, 3, defaulted);
    CHECK(fabs(defaulted[0] - 3.0 / 4.5) < 1e-12,
          "disabling the mode did not restore regret matching");

    cfr_storage_set_strategy_mode_for(NULL, 1, 1.0); /* must not crash */

    cfr_storage_destroy(s);
}

int main(void)
{
    test_two_storages_do_not_share_a_mode();
    test_setting_one_storage_leaves_the_other_alone();
    test_defaults_and_clamping();

    if (g_failures != 0)
    {
        fprintf(stderr, "test_cfr_strategy_mode: %d failure(s)\n", g_failures);
        return 1;
    }

    printf("test_cfr_strategy_mode: strategy extraction is per storage\n");
    return 0;
}
