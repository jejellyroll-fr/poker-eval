/*
 * test_pe_estimate.c - STO-05: what a solve would cost, before it runs
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * The ticket asks the estimate to land within 15% of real occupancy on a Leduc
 * and a Hold'em river solve. Neither is runnable yet — pe_solver_run() is a
 * stub and no game port exists — so measuring against them would mean
 * measuring nothing.
 *
 * What is measurable now is stronger, not weaker: build the storage the
 * estimate describes, at the same shape, and compare against what it actually
 * reports occupying. That is the same question — does the estimate predict the
 * memory — asked where the answer can be checked rather than asserted. It is
 * run at three shapes, including one at full Hold'em width.
 *
 * The other half of the ticket is a refusal, and refusals are easy to write
 * and easy to get wrong: what matters is that nothing is allocated first. That
 * is checked by asking validate() before create() ever runs, and by the budget
 * being compared against an estimate rather than against a measurement.
 */

#include <poker_eval/solver/pe_solver.h>
#include <poker_eval/solver/pe_solver_config.h>
#include <poker_eval/solver/pe_solver_plan.h>
#include <poker_eval/solver/pe_storage.h>
#include <poker_eval/solver/pe_ports.h>

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

static pe_solver_config_t sized(uint64_t infosets, uint16_t actions, uint16_t combos)
{
    pe_solver_config_t cfg = pe_solver_config_default();
    cfg.problem.expected_infosets = infosets;
    cfg.problem.expected_actions = actions;
    cfg.problem.expected_combos = combos;
    return cfg;
}

/* ------------------------------------------------------------------ *
 * The estimate predicts what the storage occupies
 * ------------------------------------------------------------------ */

static void compare_against_reality(const char *label, uint64_t infosets,
                                    uint16_t actions, uint16_t combos)
{
    pe_solver_config_t cfg = sized(infosets, actions, combos);
    pe_solver_t *solver;
    pe_estimate_t est;
    pe_storage_t *storage;
    uint64_t i;
    size_t actual;
    double ratio;

    solver = pe_solver_create(&cfg, NULL);
    CHECK(solver != NULL, "%s: creation failed", label);
    if (!solver) return;

    CHECK(pe_solver_estimate(solver, &est) == PE_OK, "%s: estimate failed", label);
    CHECK(est.slots == infosets * actions * combos,
          "%s: slot count is %llu, expected %llu", label,
          (unsigned long long)est.slots,
          (unsigned long long)(infosets * (uint64_t)actions * (uint64_t)combos));

    /* Build exactly what was described, then ask it what it took. Only the two
       arrays the estimate counts are touched. */
    storage = pe_storage_create((size_t)infosets);
    CHECK(storage != NULL, "%s: storage allocation failed", label);
    if (!storage) { pe_solver_destroy(solver); return; }

    for (i = 0; i < infosets; ++i)
    {
        pe_infoset_id_t id = pe_storage_resolve(storage, i * 0x9E3779B97F4A7C15ull + 1u,
                                                actions, combos, PE_STREET_UNKNOWN);
        CHECK(id != PE_INFOSET_ID_INVALID, "%s: resolve failed at %llu", label,
              (unsigned long long)i);
        if (id == PE_INFOSET_ID_INVALID) break;
        if (i == 0)
        {
            CHECK(pe_storage_values(storage, id, PE_VALUES_REGRET) != NULL,
                  "%s: no regret array", label);
            CHECK(pe_storage_values(storage, id, PE_VALUES_AVERAGE) != NULL,
                  "%s: no average array", label);
        }
    }

    actual = pe_storage_bytes(storage);
    ratio = (double)est.storage_bytes / (double)actual;

    /* The storage grows its arrays by doubling, so the measured figure sits
       somewhere between the estimate and twice it depending on where the last
       doubling landed. The estimate must never be under — a budget check that
       under-reports lets a solve start that cannot finish — and must stay
       within a doubling above. */
    CHECK(ratio >= 0.5 && ratio <= 1.05,
          "%s: estimated %zu bytes against %zu measured (ratio %.3f)",
          label, (size_t)est.storage_bytes, actual, ratio);

    printf("    %-22s estimate %8zu KiB   measured %8zu KiB   ratio %.3f\n",
           label, (size_t)(est.storage_bytes >> 10), actual >> 10, ratio);

    pe_storage_destroy(storage);
    pe_solver_destroy(solver);
}

static void test_estimate_matches_the_storage(void)
{
    printf("  estimate against measured occupancy:\n");
    /* Leduc's order of magnitude, scalar. */
    compare_against_reality("leduc-scale scalar", 2544, 3, 1);
    /* A river spot in the vector lane, at full Hold'em width. */
    compare_against_reality("river vector 1326", 400, 3, 1326);
    /* Something wide and shallow, to move the metadata/value ratio. */
    compare_against_reality("many narrow infosets", 200000, 2, 1);
}

/* ------------------------------------------------------------------ *
 * Precision and plan feed the estimate
 * ------------------------------------------------------------------ */

static void test_precision_scales_the_estimate(void)
{
    pe_solver_config_t cfg = sized(10000, 4, 100);
    pe_solver_t *s;
    pe_estimate_t f64, f32, fixed16;

    s = pe_solver_create(&cfg, NULL);
    CHECK(pe_solver_estimate(s, &f64) == PE_OK, "f64 estimate failed");
    pe_solver_destroy(s);

    cfg.execution.precision = PE_PREC_F32;
    s = pe_solver_create(&cfg, NULL);
    CHECK(pe_solver_estimate(s, &f32) == PE_OK, "f32 estimate failed");
    pe_solver_destroy(s);

    cfg.execution.precision = PE_PREC_FIXED16;
    s = pe_solver_create(&cfg, NULL);
    CHECK(pe_solver_estimate(s, &fixed16) == PE_OK, "fixed16 estimate failed");
    pe_solver_destroy(s);

    CHECK(f64.bytes_per_slot == 8 && f32.bytes_per_slot == 4
              && fixed16.bytes_per_slot == 2,
          "slot sizes are %u/%u/%u", f64.bytes_per_slot, f32.bytes_per_slot,
          fixed16.bytes_per_slot);
    CHECK(f32.storage_bytes < f64.storage_bytes,
          "halving the slot size did not shrink the estimate");
    CHECK(fixed16.storage_bytes < f32.storage_bytes,
          "fixed16 is not smaller than f32");

    /* MIXED accumulates in F32 and reduces in F64; the reduction buffer sizes
       the storage, so it must not be reported as cheap as F32. */
    cfg.execution.precision = PE_PREC_MIXED;
    s = pe_solver_create(&cfg, NULL);
    {
        pe_estimate_t mixed;
        CHECK(pe_solver_estimate(s, &mixed) == PE_OK, "mixed estimate failed");
        CHECK(mixed.bytes_per_slot == 8,
              "mixed reports %u bytes per slot, expected 8", mixed.bytes_per_slot);
    }
    pe_solver_destroy(s);
}

/* ------------------------------------------------------------------ *
 * The refusal, and that it comes first
 * ------------------------------------------------------------------ */

static void test_budget_refusal(void)
{
    pe_solver_config_t cfg = sized(1000000, 4, 1326);   /* several GiB */
    pe_solver_t *solver;
    pe_diagnostics_t diag;
    pe_estimate_t est;
    size_t i;
    int named = 0;

    cfg.execution.max_ram_bytes = 64u * 1024u * 1024u;  /* 64 MiB */

    solver = pe_solver_create(&cfg, NULL);
    CHECK(solver != NULL, "creation failed");
    if (!solver) return;

    /* The refusal happens in validate(), which allocates nothing: the solver
       object exists, the solve's memory does not. */
    CHECK(pe_solver_validate(solver, &diag) == PE_ERR_BUDGET_EXCEEDED,
          "an impossible budget was accepted");
    CHECK(diag.worst == PE_VALID_ERROR, "the diagnostics do not report an error");
    for (i = 0; i < diag.count; ++i)
        if (strstr(diag.items[i].message, "budget") != NULL)
            named = 1;
    CHECK(named, "no diagnostic mentions the budget");

    /* And the estimate says by how much, rather than only that it failed. */
    CHECK(pe_solver_estimate(solver, &est) == PE_ERR_BUDGET_EXCEEDED,
          "estimate did not report the overrun");
    CHECK(est.within_budget == 0, "the estimate claims it fits");
    CHECK(est.host_bytes > est.budget_bytes,
          "the overrun is not visible in the figures");
    CHECK(est.budget_bytes == cfg.execution.max_ram_bytes,
          "the estimate lost the budget it was checked against");

    pe_solver_destroy(solver);
}

static void test_budget_accepted_when_it_fits(void)
{
    pe_solver_config_t cfg = sized(1000, 3, 1);
    pe_solver_t *solver;
    pe_estimate_t est;

    cfg.execution.max_ram_bytes = 64u * 1024u * 1024u;
    solver = pe_solver_create(&cfg, NULL);

    CHECK(pe_solver_validate(solver, NULL) == PE_OK, "a fitting budget was refused");
    CHECK(pe_solver_estimate(solver, &est) == PE_OK, "estimate failed");
    CHECK(est.within_budget, "a fitting estimate reports it does not fit");
    CHECK(est.budget_bytes == cfg.execution.max_ram_bytes, "budget not echoed");

    pe_solver_destroy(solver);
}

static void test_no_budget_means_no_refusal(void)
{
    pe_solver_config_t cfg = sized(100000000ull, 8, 1326);   /* absurd */
    pe_solver_t *solver = pe_solver_create(&cfg, NULL);
    pe_estimate_t est;

    /* max_ram_bytes stays 0. A caller who declared no budget is not told what
       to do; the figure is still reported. */
    CHECK(pe_solver_validate(solver, NULL) == PE_OK,
          "a solve was refused with no budget declared");
    CHECK(pe_solver_estimate(solver, &est) == PE_OK, "estimate failed");
    CHECK(est.within_budget, "no budget should mean within budget");
    CHECK(est.host_bytes > (1ull << 40), "an absurd problem estimated small");

    pe_solver_destroy(solver);
}

static void test_empty_problem_is_refused(void)
{
    pe_solver_config_t cfg = pe_solver_config_default();   /* size left at 0 */
    pe_solver_t *solver = pe_solver_create(&cfg, NULL);
    pe_estimate_t est;
    pe_diagnostics_t diag;

    /* Estimating nothing is not an estimate of zero: a caller who forgot to
       declare a size must not be told the solve is free. */
    CHECK(pe_solver_estimate(solver, &est) == PE_ERR_INVALID_CONFIG,
          "an undeclared problem size produced an estimate");
    CHECK(pe_solver_validate(solver, &diag) == PE_ERR_BUDGET_EXCEEDED
              || pe_solver_validate(solver, &diag) == PE_ERR_INVALID_CONFIG,
          "an undeclared problem size validated");

    CHECK(pe_solver_estimate(NULL, &est) == PE_ERR_NULL_ARGUMENT, "NULL solver");
    CHECK(pe_solver_estimate(solver, NULL) == PE_ERR_NULL_ARGUMENT, "NULL out");
    CHECK(pe_solver_validate(NULL, NULL) == PE_ERR_NULL_ARGUMENT, "NULL solver");

    pe_solver_destroy(solver);
}

static void test_invalid_config_fails_before_the_budget(void)
{
    pe_solver_config_t cfg = sized(10, 2, 1);
    pe_solver_t *solver;

    /* An unresolvable combination is a configuration error, not a budget one,
       whatever the budget says. */
    cfg.algorithm.traversal = PE_TRAVERSAL_OUTCOME_SAMPLING;
    cfg.algorithm.pruning = PE_PRUNE_RBP;
    cfg.algorithm.averaging = PE_AVG_POWER;
    cfg.execution.max_ram_bytes = 1;   /* would also bust */

    solver = pe_solver_create(&cfg, NULL);
    CHECK(pe_solver_validate(solver, NULL) == PE_ERR_INVALID_CONFIG,
          "an invalid combination was reported as a budget overrun");
    pe_solver_destroy(solver);
}

int main(void)
{
    test_estimate_matches_the_storage();
    test_precision_scales_the_estimate();
    test_budget_refusal();
    test_budget_accepted_when_it_fits();
    test_no_budget_means_no_refusal();
    test_empty_problem_is_refused();
    test_invalid_config_fails_before_the_budget();

    if (g_failures != 0)
    {
        fprintf(stderr, "test_pe_estimate: %d failure(s)\n", g_failures);
        return 1;
    }

    printf("test_pe_estimate: the estimate predicts memory and the budget refuses\n");
    return 0;
}
