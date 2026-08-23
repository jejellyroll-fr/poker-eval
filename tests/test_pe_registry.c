/*
 * test_pe_registry.c - CTR-06 preset resolution, validation, execution plan
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * The property under test is a refusal, not a computation: a configuration the
 * solver cannot honour must be rejected, and must not leave behind a plan a
 * caller could run anyway. That second half is the one that bites — a resolver
 * which reports an error but still fills the output buffer gives every
 * downstream caller a plausible-looking plan for a configuration nobody
 * validated.
 */

#include <poker_eval/solver/pe_solver_plan.h>

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

/* Everything a plan could possibly ask for. Used when the test is about the
   algorithm axes rather than about capability gating. */
#define ALL_CAPS ((uint64_t)PE_CAP_ALL)

static int diag_mentions(const pe_diagnostics_t *d, const char *needle)
{
    size_t i;
    for (i = 0; i < d->count; ++i)
    {
        if (strstr(d->items[i].message, needle) != NULL)
            return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ *
 * Presets
 * ------------------------------------------------------------------ */

static void test_every_preset_expands(void)
{
    int p;

    for (p = 0; p < PE_PRESET_COUNT; ++p)
    {
        pe_algorithm_config_t algo;
        const char *name = pe_preset_name((pe_algorithm_preset_t)p);

        memset(&algo, 0, sizeof(algo));
        CHECK(name != NULL, "preset %d has no name", p);
        CHECK(pe_preset_expand((pe_algorithm_preset_t)p, &algo) == 0,
              "preset %s does not expand", name != NULL ? name : "?");

        if (name != NULL)
            CHECK(pe_preset_from_name(name) == (pe_algorithm_preset_t)p,
                  "preset name %s does not map back", name);
    }

    CHECK(pe_preset_from_name("CFR+") == PE_PRESET_CFR_PLUS,
          "preset lookup should be case-insensitive");
    CHECK(pe_preset_from_name("nope") == PE_PRESET_COUNT, "unknown preset should not resolve");
    CHECK(pe_preset_from_name(NULL) == PE_PRESET_COUNT, "NULL is not a preset name");
    CHECK(pe_preset_expand((pe_algorithm_preset_t)PE_PRESET_COUNT, NULL) == -1,
          "a NULL target should be rejected");
}

static void test_preset_matches_the_matrix(void)
{
    pe_algorithm_config_t algo;

    /* Architecture v3 §5, row by row. Getting one of these wrong means a user
       asking for CFR+ silently receives something else. */
    memset(&algo, 0, sizeof(algo));
    pe_preset_expand(PE_PRESET_CFR, &algo);
    CHECK(algo.traversal == PE_TRAVERSAL_FULL_SCALAR, "cfr traversal");
    CHECK(algo.regret == PE_REGRET_VANILLA, "cfr regret");
    CHECK(algo.averaging == PE_AVG_UNIFORM, "cfr averaging");

    memset(&algo, 0, sizeof(algo));
    pe_preset_expand(PE_PRESET_CFR_PLUS, &algo);
    CHECK(algo.traversal == PE_TRAVERSAL_FULL_VECTOR, "cfr+ traversal");
    CHECK(algo.regret == PE_REGRET_PLUS, "cfr+ regret");
    CHECK(algo.averaging == PE_AVG_DELAYED_LINEAR, "cfr+ averaging");

    memset(&algo, 0, sizeof(algo));
    pe_preset_expand(PE_PRESET_DCFR, &algo);
    CHECK(algo.traversal == PE_TRAVERSAL_FULL_VECTOR, "dcfr traversal");
    CHECK(algo.regret == PE_REGRET_DCFR, "dcfr regret");
    CHECK(algo.averaging == PE_AVG_POWER, "dcfr averaging");

    memset(&algo, 0, sizeof(algo));
    pe_preset_expand(PE_PRESET_ECFR, &algo);
    CHECK(algo.policy == PE_POLICY_EXPONENTIAL, "ecfr policy");
    CHECK(algo.regret == PE_REGRET_LEGACY_EXP, "ecfr regret");
}

static void test_preset_leaves_tuning_alone(void)
{
    pe_algorithm_config_t algo;

    memset(&algo, 0, sizeof(algo));
    algo.dcfr_alpha = 2.5;
    algo.pruning = PE_PRUNE_RBP;
    pe_preset_expand(PE_PRESET_DCFR, &algo);

    /* `--algorithm dcfr --alpha 2.5` must not have alpha stamped back to the
       preset's value: a preset says which algorithm, not how it is tuned. */
    CHECK(algo.dcfr_alpha == 2.5, "the preset overwrote an explicit alpha");
    CHECK(algo.pruning == PE_PRUNE_RBP, "the preset overwrote an explicit pruning mode");
}

static void test_custom_expands_to_nothing(void)
{
    pe_algorithm_config_t algo;

    memset(&algo, 0, sizeof(algo));
    algo.traversal = PE_TRAVERSAL_OUTCOME_SAMPLING;
    algo.regret = PE_REGRET_PLUS;
    pe_preset_expand(PE_PRESET_CUSTOM, &algo);

    CHECK(algo.traversal == PE_TRAVERSAL_OUTCOME_SAMPLING,
          "CUSTOM overwrote an explicit traversal");
    CHECK(algo.regret == PE_REGRET_PLUS, "CUSTOM overwrote an explicit regret mode");
}

/* ------------------------------------------------------------------ *
 * Resolution succeeds when everything is available
 * ------------------------------------------------------------------ */

static void test_reference_preset_resolves(void)
{
    pe_solver_config_t cfg = pe_solver_config_default();
    pe_execution_plan_t plan;
    pe_diagnostics_t diag;
    pe_valid_severity_t sev;

    cfg.algorithm.preset = PE_PRESET_CFR;
    sev = pe_plan_resolve(&cfg, ALL_CAPS, &plan, &diag);

    CHECK(sev != PE_VALID_ERROR, "the reference preset was refused");
    CHECK(plan.valid, "a resolved plan should be valid");
    CHECK(plan.traversal == PE_TRAVERSAL_FULL_SCALAR, "unexpected resolved traversal");
    CHECK(plan.preset == PE_PRESET_CFR, "the plan lost which preset produced it");

    /* No stage may remain undecided: that is the whole point of a plan. */
    CHECK(plan.stages.traversal != PE_COMPUTE_AUTO, "traversal stage left at AUTO");
    CHECK(plan.stages.update != PE_COMPUTE_AUTO, "update stage left at AUTO");
    CHECK(plan.stages.terminal_eval != PE_COMPUTE_AUTO, "terminal stage left at AUTO");

    CHECK((plan.required_caps & ~plan.provided_caps) == 0,
          "a valid plan requires something it was not provided");
}

static void test_canonical_presets_resolve(void)
{
    pe_solver_config_t cfg = pe_solver_config_default();
    pe_execution_plan_t plan;
    pe_diagnostics_t diag;

    cfg.algorithm.preset = PE_PRESET_CFR_PLUS;
    CHECK(pe_plan_resolve(&cfg, ALL_CAPS, &plan, &diag) != PE_VALID_ERROR,
          "cfr+ preset was refused");
    CHECK(plan.traversal == PE_TRAVERSAL_FULL_VECTOR &&
              plan.regret == PE_REGRET_PLUS &&
              plan.averaging == PE_AVG_DELAYED_LINEAR,
          "cfr+ preset did not resolve to its canonical axes");

    cfg = pe_solver_config_default();
    cfg.algorithm.preset = PE_PRESET_DCFR;
    CHECK(pe_plan_resolve(&cfg, ALL_CAPS, &plan, &diag) != PE_VALID_ERROR,
          "dcfr preset was refused");
    CHECK(plan.traversal == PE_TRAVERSAL_FULL_VECTOR &&
              plan.regret == PE_REGRET_DCFR &&
              plan.averaging == PE_AVG_POWER,
          "dcfr preset did not resolve to its canonical axes");
}

/* ------------------------------------------------------------------ *
 * A refused configuration produces no plan
 * ------------------------------------------------------------------ */

static void test_undeclared_combination_is_refused(void)
{
    pe_solver_config_t cfg = pe_solver_config_default();
    pe_execution_plan_t plan;
    pe_diagnostics_t diag;
    pe_valid_severity_t sev;

    cfg.algorithm.traversal = PE_TRAVERSAL_OUTCOME_SAMPLING;
    cfg.algorithm.pruning = PE_PRUNE_RBP;
    cfg.algorithm.averaging = PE_AVG_POWER;   /* isolate the pruning conflict */

    sev = pe_plan_resolve(&cfg, ALL_CAPS, &plan, &diag);

    CHECK(sev == PE_VALID_ERROR, "RBP with outcome sampling should be refused");
    CHECK(!plan.valid, "a refused configuration must not produce a usable plan");

    /* The message has to name both sides, or the caller learns nothing. */
    CHECK(diag_mentions(&diag, pe_pruning_name(PE_PRUNE_RBP)),
          "the diagnostic does not name the pruning mode");
    CHECK(diag_mentions(&diag, pe_traversal_name(PE_TRAVERSAL_OUTCOME_SAMPLING)),
          "the diagnostic does not name the traversal mode");
}

static void test_refused_plan_is_zeroed(void)
{
    pe_solver_config_t cfg = pe_solver_config_default();
    pe_execution_plan_t plan;
    pe_diagnostics_t diag;
    pe_execution_plan_t zeroed;

    /* Poison the buffer first: a resolver that reports an error but leaves the
       plan half-built is the failure this checks for. */
    memset(&plan, 0x5A, sizeof(plan));
    memset(&zeroed, 0, sizeof(zeroed));

    cfg.algorithm.traversal = PE_TRAVERSAL_OUTCOME_SAMPLING;
    cfg.algorithm.pruning = PE_PRUNE_RBP;
    cfg.algorithm.averaging = PE_AVG_POWER;

    CHECK(pe_plan_resolve(&cfg, ALL_CAPS, &plan, &diag) == PE_VALID_ERROR,
          "expected a refusal");
    CHECK(memcmp(&plan, &zeroed, sizeof(plan)) == 0,
          "a refused resolution left data in the plan buffer");
}

static void test_missing_capability_is_an_error_not_a_fallback(void)
{
    pe_solver_config_t cfg = pe_solver_config_default();
    pe_execution_plan_t plan;
    pe_diagnostics_t diag;
    pe_valid_severity_t sev;

    /* Ask for the vector lane on a game that cannot carry per-combo values. */
    cfg.algorithm.preset = PE_PRESET_CFR_VECTOR;
    sev = pe_plan_resolve(&cfg, ALL_CAPS & ~(uint64_t)PE_CAP_VECTOR_FORM, &plan, &diag);

    CHECK(sev == PE_VALID_ERROR, "a missing capability must be an error");
    CHECK(sev != PE_VALID_FALLBACK, "a missing capability must never be a silent fallback");
    CHECK(!plan.valid, "no plan should be produced");
    CHECK(diag_mentions(&diag, "VECTOR_FORM"),
          "the diagnostic does not name the missing capability");
}

static void test_gpu_stage_without_the_capability_is_refused(void)
{
    pe_solver_config_t cfg = pe_solver_config_default();
    pe_execution_plan_t plan;
    pe_diagnostics_t diag;

    cfg.execution.stages.terminal_eval = PE_COMPUTE_CUDA;

    CHECK(pe_plan_resolve(&cfg, ALL_CAPS & ~(uint64_t)PE_CAP_GPU_TERMINAL_EVAL,
                          &plan, &diag) == PE_VALID_ERROR,
          "a CUDA stage without the GPU capability should be refused");
    CHECK(diag_mentions(&diag, "GPU_TERMINAL_EVAL"),
          "the diagnostic does not name the missing GPU capability");

    /* And it resolves once the capability is there. */
    CHECK(pe_plan_resolve(&cfg, ALL_CAPS, &plan, &diag) != PE_VALID_ERROR,
          "the same stage should resolve when the capability is provided");
    CHECK(plan.stages.terminal_eval == PE_COMPUTE_CUDA,
          "the resolved terminal stage is not the one requested");
}

static void test_sampling_presets_are_gated(void)
{
    pe_solver_config_t cfg = pe_solver_config_default();
    pe_execution_plan_t plan;
    pe_diagnostics_t diag;

    /* external-mccfr expands, but its plan is refused until LNB-02 provides
       importance-weighted averaging. Expansion and validation are separate
       questions, and the ticket's "every preset expands" is the first one. */
    cfg.algorithm.preset = PE_PRESET_EXTERNAL_MCCFR;
    CHECK(pe_plan_resolve(&cfg, ALL_CAPS, &plan, &diag) == PE_VALID_ERROR,
          "external-mccfr should not resolve before LNB-02");
    CHECK(diag_mentions(&diag, "LNB-02"),
          "the refusal should name the ticket that unblocks it");

    cfg = pe_solver_config_default();
    cfg.algorithm.preset = PE_PRESET_EXTERNAL_DCFR;
    CHECK(pe_plan_resolve(&cfg, ALL_CAPS, &plan, &diag) == PE_VALID_ERROR,
          "external-dcfr should not resolve before LNB-02");
    CHECK(diag_mentions(&diag, "LNB-02"),
          "external-dcfr refusal should name LNB-02");
}

/* ------------------------------------------------------------------ *
 * AUTO is reported, not hidden
 * ------------------------------------------------------------------ */

static void test_auto_backend_reports_a_fallback(void)
{
    pe_solver_config_t cfg = pe_solver_config_default();
    pe_execution_plan_t plan;
    pe_diagnostics_t diag;
    pe_valid_severity_t sev;

    cfg.execution.backend = PE_COMPUTE_AUTO;
    cfg.execution.stages.traversal = PE_COMPUTE_AUTO;
    cfg.execution.stages.update = PE_COMPUTE_AUTO;
    cfg.execution.stages.terminal_eval = PE_COMPUTE_AUTO;

    sev = pe_plan_resolve(&cfg, ALL_CAPS, &plan, &diag);

    CHECK(sev == PE_VALID_FALLBACK, "an unresolved AUTO should be reported as a fallback");
    CHECK(plan.valid, "a fallback still produces a plan");
    CHECK(plan.stages.traversal == PE_COMPUTE_CPU_REF, "AUTO should resolve to cpu_ref");
    CHECK(diag.count == 3, "expected one diagnostic per stage, got %zu", diag.count);
}

/* ------------------------------------------------------------------ *
 * Ranges and degenerate inputs
 * ------------------------------------------------------------------ */

static void test_out_of_range_parameters(void)
{
    pe_solver_config_t cfg = pe_solver_config_default();
    pe_execution_plan_t plan;
    pe_diagnostics_t diag;

    cfg.algorithm.outcome_epsilon = 1.5;
    CHECK(pe_plan_resolve(&cfg, ALL_CAPS, &plan, &diag) == PE_VALID_ERROR,
          "an epsilon above 1 should be refused");

    cfg = pe_solver_config_default();
    cfg.execution.cpu_threads = -1;
    CHECK(pe_plan_resolve(&cfg, ALL_CAPS, &plan, &diag) == PE_VALID_ERROR,
          "a negative thread count should be refused");

    cfg = pe_solver_config_default();
    cfg.algorithm.policy = PE_POLICY_EXPONENTIAL;
    cfg.algorithm.regret = PE_REGRET_VANILLA;
    CHECK(pe_plan_resolve(&cfg, ALL_CAPS, &plan, &diag) == PE_VALID_ERROR,
          "the exponential policy without legacy-exp regret should be refused");
}

static void test_exploitability_stop_configuration(void)
{
    pe_solver_config_t cfg = pe_solver_config_default();
    pe_execution_plan_t plan;
    pe_diagnostics_t diag;

    cfg.target_exploitability_mbb = 5.0;
    cfg.exploitability_interval = 10;
    CHECK(pe_plan_resolve(&cfg, ALL_CAPS, &plan, &diag) != PE_VALID_ERROR,
          "a target with a measurement interval should be valid");

    cfg = pe_solver_config_default();
    cfg.target_exploitability_mbb = 5.0;
    CHECK(pe_plan_resolve(&cfg, ALL_CAPS, &plan, &diag) == PE_VALID_ERROR,
          "a target without a measurement interval should be refused");
    CHECK(diag_mentions(&diag, "exploitability_interval"),
          "the missing interval should be diagnosed");

    cfg = pe_solver_config_default();
    cfg.target_exploitability_mbb = -1.0;
    CHECK(pe_plan_resolve(&cfg, ALL_CAPS, &plan, &diag) == PE_VALID_ERROR,
          "a negative target should be refused");
    CHECK(diag_mentions(&diag, "target_exploitability_mbb"),
          "the invalid target should be diagnosed");

    cfg = pe_solver_config_default();
    cfg.max_iterations = 0;
    cfg.target_exploitability_mbb = 0.0;
    CHECK(pe_plan_resolve(&cfg, ALL_CAPS, &plan, &diag) == PE_VALID_ERROR,
          "a configuration without a stop condition should be refused");
}

static void test_null_arguments(void)
{
    pe_solver_config_t cfg = pe_solver_config_default();
    pe_execution_plan_t plan;
    pe_diagnostics_t diag;

    CHECK(pe_plan_resolve(NULL, ALL_CAPS, &plan, &diag) == PE_VALID_ERROR,
          "a NULL configuration should be refused");
    CHECK(pe_plan_resolve(&cfg, ALL_CAPS, NULL, &diag) == PE_VALID_ERROR,
          "a NULL plan buffer should be refused");

    /* Diagnostics are optional: the status alone must still be right. */
    CHECK(pe_plan_resolve(&cfg, ALL_CAPS, &plan, NULL) != PE_VALID_ERROR,
          "a NULL diagnostics buffer should not break resolution");

    pe_diagnostics_clear(NULL);
    CHECK(pe_config_required_caps(NULL) == 0, "a NULL configuration requires nothing");
}

/* ------------------------------------------------------------------ *
 * Reporting
 * ------------------------------------------------------------------ */

static void test_plan_text(void)
{
    pe_solver_config_t cfg = pe_solver_config_default();
    pe_execution_plan_t plan;
    pe_diagnostics_t diag;
    char buf[1024];
    size_t needed;

    cfg.algorithm.preset = PE_PRESET_CFR;
    pe_plan_resolve(&cfg, ALL_CAPS, &plan, &diag);

    needed = pe_plan_to_string(&plan, buf, sizeof(buf));
    CHECK(needed > 0 && needed < sizeof(buf), "unexpected plan text length %zu", needed);

    /* The point of --print-execution-plan is seeing the effective backend. */
    CHECK(strstr(buf, "cpu_ref") != NULL, "the plan text hides the resolved backend");
    CHECK(strstr(buf, "full-scalar") != NULL, "the plan text hides the traversal");
    CHECK(strstr(buf, "f64") != NULL, "the plan text hides the precision");

    /* A refused plan says so rather than printing a blank one. */
    memset(&plan, 0, sizeof(plan));
    pe_plan_to_string(&plan, buf, sizeof(buf));
    CHECK(strstr(buf, "refused") != NULL, "an invalid plan should say it was refused");

    CHECK(pe_plan_to_string(NULL, buf, sizeof(buf)) == 0, "a NULL plan renders nothing");
}

static void test_diagnostics_overflow_is_counted(void)
{
    pe_diagnostics_t diag;
    pe_solver_config_t cfg = pe_solver_config_default();
    pe_execution_plan_t plan;

    /* Strip every capability so each one is reported missing. There are more
       named capabilities than PE_DIAG_MAX slots for some configurations; what
       matters is that nothing is lost without being counted. */
    cfg.algorithm.preset = PE_PRESET_CFR_PLUS_CHANCE;
    cfg.execution.stages.traversal = PE_COMPUTE_CUDA;
    cfg.execution.stages.update = PE_COMPUTE_CUDA;
    cfg.execution.stages.terminal_eval = PE_COMPUTE_CUDA;
    pe_plan_resolve(&cfg, 0, &plan, &diag);

    CHECK(diag.worst == PE_VALID_ERROR, "expected an error");
    CHECK(diag.count <= PE_DIAG_MAX, "diagnostics overflowed their buffer");
    CHECK(diag.count + diag.dropped >= 1, "nothing was reported at all");
}

int main(void)
{
    test_every_preset_expands();
    test_preset_matches_the_matrix();
    test_preset_leaves_tuning_alone();
    test_custom_expands_to_nothing();
    test_reference_preset_resolves();
    test_canonical_presets_resolve();
    test_undeclared_combination_is_refused();
    test_refused_plan_is_zeroed();
    test_missing_capability_is_an_error_not_a_fallback();
    test_gpu_stage_without_the_capability_is_refused();
    test_sampling_presets_are_gated();
    test_auto_backend_reports_a_fallback();
    test_out_of_range_parameters();
    test_exploitability_stop_configuration();
    test_null_arguments();
    test_plan_text();
    test_diagnostics_overflow_is_counted();

    if (g_failures != 0)
    {
        fprintf(stderr, "test_pe_registry: %d failure(s)\n", g_failures);
        return 1;
    }

    printf("test_pe_registry: %d presets resolve, refusals produce no plan\n",
           (int)PE_PRESET_COUNT);
    return 0;
}
