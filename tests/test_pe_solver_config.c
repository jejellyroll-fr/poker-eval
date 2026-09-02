/*
 * test_pe_solver_config.c - CTR-03 configuration axes and defaults
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * Three things are pinned here.
 *
 * 1. The default configuration, field by field. It is the reference path, and
 *    a silent drift towards a faster-but-unvalidated default is exactly the
 *    failure this suite exists to prevent.
 * 2. The enumerator values. They cross the public ABI and are recorded in a
 *    checkpoint, so reordering an axis has to be a deliberate act, not an
 *    editing accident.
 * 3. That a zero-initialised configuration is NOT the default. The axes are
 *    ordered for reading rather than to make `{0}` meaningful, which is a trap
 *    worth having a test stand on.
 */

#include <poker_eval/solver/pe_solver_config.h>

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

#define CHECK_EQ_INT(actual, expected)                         \
    CHECK((long long)(actual) == (long long)(expected),        \
          #actual " is %lld, expected %lld",                   \
          (long long)(actual), (long long)(expected))

/*
 * Exact equality is the intent for these: the values are constants copied
 * verbatim by pe_solver_config_default(), never the result of arithmetic. A
 * tolerance here would let a default drift from 1.5 to 1.4999 unnoticed, which
 * is precisely what this test exists to catch.
 */
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wfloat-equal"
#endif

#define CHECK_EQ_DBL(actual, expected)                         \
    CHECK((actual) == (expected),                              \
          #actual " is %f, expected %f", (double)(actual), (double)(expected))

/* ------------------------------------------------------------------ *
 * The default configuration, field by field
 * ------------------------------------------------------------------ */

static void test_default_algorithm(void)
{
    pe_solver_config_t cfg = pe_solver_config_default();

    /* The combination the ticket specifies: the reference oracle. */
    CHECK_EQ_INT(cfg.algorithm.preset,    PE_PRESET_CUSTOM);
    CHECK_EQ_INT(cfg.algorithm.traversal, PE_TRAVERSAL_FULL_SCALAR);
    CHECK_EQ_INT(cfg.algorithm.regret,    PE_REGRET_VANILLA);
    CHECK_EQ_INT(cfg.algorithm.policy,    PE_POLICY_REGRET_MATCHING);
    CHECK_EQ_INT(cfg.algorithm.averaging, PE_AVG_UNIFORM);
    CHECK_EQ_INT(cfg.algorithm.pruning,   PE_PRUNE_NONE);

    /* Canonical DCFR parameters, carried even when unused. */
    CHECK_EQ_DBL(cfg.algorithm.dcfr_alpha, 1.5);
    CHECK_EQ_DBL(cfg.algorithm.dcfr_beta,  0.0);
    CHECK_EQ_DBL(cfg.algorithm.dcfr_gamma, 2.0);

    CHECK_EQ_DBL(cfg.algorithm.exponential_lambda, 1.0);
    CHECK_EQ_INT(cfg.algorithm.averaging_delay, 0);
    CHECK_EQ_DBL(cfg.algorithm.outcome_epsilon, 0.6);

    CHECK(cfg.algorithm.outcome_epsilon >= 0.0 && cfg.algorithm.outcome_epsilon <= 1.0,
          "outcome_epsilon must be a probability");
}

static void test_default_execution(void)
{
    pe_solver_config_t cfg = pe_solver_config_default();

    /* Every stage on the reference backend, named rather than left at AUTO:
       the reference path must not depend on the hardware present. */
    CHECK_EQ_INT(cfg.execution.backend,              PE_COMPUTE_CPU_REF);
    CHECK_EQ_INT(cfg.execution.stages.traversal,     PE_COMPUTE_CPU_REF);
    CHECK_EQ_INT(cfg.execution.stages.update,        PE_COMPUTE_CPU_REF);
    CHECK_EQ_INT(cfg.execution.stages.terminal_eval, PE_COMPUTE_CPU_REF);

    CHECK_EQ_INT(cfg.execution.precision, PE_PREC_F64);

    CHECK_EQ_INT(cfg.execution.device_id, -1);
    CHECK_EQ_INT(cfg.execution.cpu_threads, 1);
    CHECK_EQ_INT(cfg.execution.deterministic, 1);

    CHECK_EQ_INT(cfg.execution.sample_batch_size, 0);
    CHECK_EQ_INT(cfg.execution.terminal_batch_size, 0);
    CHECK_EQ_INT(cfg.execution.update_batch_size, 0);
}

static void test_default_top_level(void)
{
    pe_solver_config_t cfg = pe_solver_config_default();

    CHECK_EQ_INT(cfg.seed, 0);

    /* A default that never terminates would be a bad default. */
    CHECK(cfg.max_iterations > 0, "the default must carry an iteration limit");
    CHECK_EQ_INT(cfg.max_iterations, 1000);
    CHECK_EQ_INT(cfg.exploitability_interval, 0);
    CHECK_EQ_INT(cfg.br_samples, 0);
}

static void test_default_is_stable(void)
{
    pe_solver_config_t a = pe_solver_config_default();
    pe_solver_config_t b = pe_solver_config_default();

    /* Same bytes every call: no hidden state, nothing derived from the
       environment, and no uninitialised padding leaking into a checkpoint or a
       hash. This only holds because the implementation zeroes the object
       before filling it; drop that and this check is what notices. */
    CHECK(memcmp(&a, &b, sizeof(a)) == 0, "pe_solver_config_default is not pure");
}

/* ------------------------------------------------------------------ *
 * A zeroed configuration is not the default
 * ------------------------------------------------------------------ */

static void test_zero_init_is_not_the_default(void)
{
    pe_solver_config_t zeroed;
    pe_solver_config_t cfg = pe_solver_config_default();

    memset(&zeroed, 0, sizeof(zeroed));

    CHECK(memcmp(&zeroed, &cfg, sizeof(cfg)) != 0,
          "zero-init happens to equal the default; the header's warning is now wrong");

    /* And specifically on the two axes where it bites. */
    CHECK(zeroed.algorithm.traversal != cfg.algorithm.traversal,
          "a zeroed traversal should not be the reference traversal");
    CHECK(zeroed.execution.backend != cfg.execution.backend,
          "a zeroed backend should not be the reference backend");
}

/* ------------------------------------------------------------------ *
 * Enumerator values are ABI
 * ------------------------------------------------------------------ */

static void test_enum_values(void)
{
    /* Traversal: lane A, then the oracle, then lane B. */
    CHECK_EQ_INT(PE_TRAVERSAL_FULL_VECTOR,       0);
    CHECK_EQ_INT(PE_TRAVERSAL_CHANCE_VECTOR,     1);
    CHECK_EQ_INT(PE_TRAVERSAL_FULL_SCALAR,       2);
    CHECK_EQ_INT(PE_TRAVERSAL_EXTERNAL_SAMPLING, 3);
    CHECK_EQ_INT(PE_TRAVERSAL_OUTCOME_SAMPLING,  4);
    CHECK_EQ_INT(PE_TRAVERSAL_COUNT,             5);

    CHECK_EQ_INT(PE_REGRET_VANILLA,    0);
    CHECK_EQ_INT(PE_REGRET_PLUS,       1);
    CHECK_EQ_INT(PE_REGRET_DCFR,       2);
    CHECK_EQ_INT(PE_REGRET_LEGACY_EXP, 3);
    CHECK_EQ_INT(PE_REGRET_COUNT,      4);

    CHECK_EQ_INT(PE_POLICY_REGRET_MATCHING, 0);
    CHECK_EQ_INT(PE_POLICY_EXPONENTIAL,     1);
    CHECK_EQ_INT(PE_POLICY_COUNT,           2);

    CHECK_EQ_INT(PE_AVG_UNIFORM,        0);
    CHECK_EQ_INT(PE_AVG_LINEAR,         1);
    CHECK_EQ_INT(PE_AVG_POWER,          2);
    CHECK_EQ_INT(PE_AVG_DELAYED_LINEAR, 3);
    CHECK_EQ_INT(PE_AVG_IMPORTANCE,     4);
    CHECK_EQ_INT(PE_AVG_COUNT,          5);

    CHECK_EQ_INT(PE_PRUNE_NONE,  0);
    CHECK_EQ_INT(PE_PRUNE_RBP,   1);
    CHECK_EQ_INT(PE_PRUNE_COUNT, 2);

    /* F64 is zero: the reference precision is the one you get by accident. */
    CHECK_EQ_INT(PE_PREC_F64,     0);
    CHECK_EQ_INT(PE_PREC_F32,     1);
    CHECK_EQ_INT(PE_PREC_MIXED,   2);
    CHECK_EQ_INT(PE_PREC_FIXED16, 3);
    CHECK_EQ_INT(PE_PREC_COUNT,   4);

    CHECK_EQ_INT(PE_COMPUTE_AUTO,    0);
    CHECK_EQ_INT(PE_COMPUTE_CPU_REF, 1);
    CHECK_EQ_INT(PE_COMPUTE_CPU_PAR, 2);
    CHECK_EQ_INT(PE_COMPUTE_CUDA,    3);
    CHECK_EQ_INT(PE_COMPUTE_OPENCL,  4);
    /* Appended by GPU-02/GPU-03. The existing values above must never move:
       they cross the public ABI and are written into saved plans. */
    CHECK_EQ_INT(PE_COMPUTE_HIP,     5);
    CHECK_EQ_INT(PE_COMPUTE_METAL,   6);
    CHECK_EQ_INT(PE_COMPUTE_COUNT,   7);

    /* CUSTOM is zero so a hand-written configuration needs no preset. */
    CHECK_EQ_INT(PE_PRESET_CUSTOM, 0);
    CHECK_EQ_INT(PE_PRESET_CFR,    1);
    CHECK_EQ_INT(PE_PRESET_COUNT,  11);
}

/* ------------------------------------------------------------------ *
 * The axes really are independent
 * ------------------------------------------------------------------ */

static void test_axes_are_independent(void)
{
    pe_solver_config_t cfg = pe_solver_config_default();

    /* Moving one axis moves nothing else. This is the whole point of replacing
       the v2 booleans: no field may imply another. */
    cfg.algorithm.regret = PE_REGRET_DCFR;
    CHECK_EQ_INT(cfg.algorithm.traversal, PE_TRAVERSAL_FULL_SCALAR);
    CHECK_EQ_INT(cfg.algorithm.averaging, PE_AVG_UNIFORM);
    CHECK_EQ_INT(cfg.execution.precision, PE_PREC_F64);

    cfg.execution.stages.terminal_eval = PE_COMPUTE_CUDA;
    CHECK_EQ_INT(cfg.execution.stages.traversal, PE_COMPUTE_CPU_REF);
    CHECK_EQ_INT(cfg.execution.stages.update,    PE_COMPUTE_CPU_REF);
    CHECK_EQ_INT(cfg.execution.backend,          PE_COMPUTE_CPU_REF);
}

int main(void)
{
    test_default_algorithm();
    test_default_execution();
    test_default_top_level();
    test_default_is_stable();
    test_zero_init_is_not_the_default();
    test_enum_values();
    test_axes_are_independent();

    if (g_failures != 0)
    {
        fprintf(stderr, "test_pe_solver_config: %d failure(s)\n", g_failures);
        return 1;
    }

    printf("test_pe_solver_config: default configuration and axis values pinned\n");
    return 0;
}
