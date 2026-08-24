/*
 * config.c - Default solver configuration (architecture v3, CTR-03)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * One function, and it is worth being deliberate about: the default decides
 * what a caller who configures nothing actually runs. The choice here is the
 * reference path — exhaustive scalar traversal, vanilla regret, F64, one
 * thread — because a wrong-but-fast default is far more expensive than a
 * slow-but-correct one. Anything faster is opted into.
 */

#include <poker_eval/solver/pe_solver_config.h>
#include <poker_eval/solver/pe_ports.h>

#include <string.h>

pe_solver_config_t pe_solver_config_default(void)
{
    pe_solver_config_t cfg;

    /* Zero the whole object first, padding included, so two calls are
       byte-identical and a field added later without being set here is zero
       rather than whatever was on the stack. */
    memset(&cfg, 0, sizeof(cfg));

    /* Then every field explicitly. Zeroing is not the default configuration:
       PE_TRAVERSAL_FULL_VECTOR and PE_COMPUTE_AUTO are the zero values, and
       neither is what the reference path means. */
    cfg.algorithm.preset    = PE_PRESET_CUSTOM;
    cfg.algorithm.traversal = PE_TRAVERSAL_FULL_SCALAR;
    cfg.algorithm.regret    = PE_REGRET_VANILLA;
    cfg.algorithm.policy    = PE_POLICY_REGRET_MATCHING;
    cfg.algorithm.averaging = PE_AVG_UNIFORM;
    cfg.algorithm.pruning   = PE_PRUNE_NONE;

    /* Canonical DCFR parameters (Brown & Sandholm). Carried even though
       PE_REGRET_VANILLA ignores them, so switching the regret axis alone
       yields the documented algorithm instead of zeros. */
    cfg.algorithm.dcfr_alpha = 1.5;
    cfg.algorithm.dcfr_beta  = 0.0;
    cfg.algorithm.dcfr_gamma = 2.0;

    /* Neutral temperature: exp(r/lambda) with lambda = 1. */
    cfg.algorithm.exponential_lambda = 1.0;

    cfg.algorithm.averaging_delay = 0;

    /* Standard outcome-sampling exploration. */
    cfg.algorithm.outcome_epsilon = 0.6;

    /* Every stage on the reference backend. Named explicitly rather than left
       at AUTO so the default plan is fully determined before the resolver
       runs — the reference path must not depend on what hardware is present. */
    cfg.execution.backend              = PE_COMPUTE_CPU_REF;
    cfg.execution.stages.traversal     = PE_COMPUTE_CPU_REF;
    cfg.execution.stages.update        = PE_COMPUTE_CPU_REF;
    cfg.execution.stages.terminal_eval = PE_COMPUTE_CPU_REF;

    cfg.execution.precision = PE_PREC_F64;

    cfg.execution.device_id   = -1;
    cfg.execution.cpu_threads = 1;

    /* The reference path promises reproducibility, so ask for it by default;
       a backend that cannot honour it is then refused rather than silently
       substituted. */
    cfg.execution.deterministic = 1;

    /* 0 lets the resolved plan size the batches. The reference backend uses a
       batch of one whatever this says. */
    cfg.execution.sample_batch_size   = 0;
    cfg.execution.terminal_batch_size = 0;
    cfg.execution.update_batch_size   = 0;

    /* No budget and no declared size: a caller who has not said how big the
       problem is gets an estimate that refuses rather than a confident zero. */
    cfg.execution.max_ram_bytes = 0;
    cfg.execution.big_blind = 1.0;
    cfg.problem.expected_infosets = 0;
    cfg.problem.expected_actions = 0;
    cfg.problem.expected_combos = 0;

    cfg.seed = 0;

    /* Small on purpose: enough to be meaningful on a toy game, small enough
       that a caller who forgets to set a stop condition gets a run that ends. */
    cfg.max_iterations = 1000;
    cfg.target_exploitability_mbb = 0.0;
    cfg.exploitability_interval = 0;

    return cfg;
}

pe_solver_deps_t pe_solver_deps_default(void)
{
    pe_solver_deps_t deps;

    /* Unlike the configuration, zero is meaningful here: every port left NULL
       asks the solver for that port's default adapter. This function exists so
       calling code can say so explicitly instead of relying on the reader
       knowing which of the two structures treats {0} as valid. */
    memset(&deps, 0, sizeof(deps));
    return deps;
}
