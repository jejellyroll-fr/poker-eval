/*
 * pe_br_sampling.h - confidence-guided Monte Carlo decisions (issue #257)
 *
 * A best-response decision picks the best of several actions whose values
 * are only known through noisy samples. Spending the same number of samples
 * on every decision wastes most of them: an action worth far more than the
 * others is clear after a handful, while a near tie needs many. This module
 * samples until the decision is resolved:
 *
 *   1. every action gets min_samples draws;
 *   2. at each look, an action whose upper confidence bound falls below the
 *      leader's lower bound is eliminated and sampled no more;
 *   3. the decision stops when only the leader survives (separated), when
 *      the leader's lower bound is within the tolerance of every survivor's
 *      upper bound (the gap cannot matter by more than the tolerance), or
 *      when every survivor has max_samples draws (unresolved, the hard cap);
 *   4. otherwise each survivor gets check_interval more draws.
 *
 * The reported value is not the leader's running mean. Stopping when the
 * decision looks resolved favours stopping when the leader's mean happens to
 * be high, so that mean is biased upward (optional stopping), on top of the
 * selection bias of reporting a maximum. Once the decision is made, the
 * chosen action is estimated again from min_samples fresh draws that played
 * no part in choosing it; their mean is an unbiased estimate of the chosen
 * action's value.
 *
 * Looking again and again at a fixed-sample confidence interval overstates
 * confidence. The half-widths here are sequentially valid by a union bound:
 * the budget fixes the number of looks L and the actions A, and every
 * interval is built at level 1 - (1 - confidence) / (L * A), so all of them
 * hold together, at every look, with probability at least `confidence`
 * (each interval being the usual normal-approximation one). That is
 * conservative next to a confidence sequence such as empirical Bernstein,
 * which needs a known value range the callers here do not have; the bound
 * lives in one function (see pe_br_sequential_z) so it can be replaced.
 *
 * Everything is deterministic given the samples, and the draws are made in a
 * fixed order, so a seeded sampler gives a reproducible decision.
 */

#ifndef POKER_EVAL_PE_BR_SAMPLING_H
#define POKER_EVAL_PE_BR_SAMPLING_H

#include <poker_eval/solver/pe_online_stats.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Matches PE_EXTERNAL_MAX_ACTIONS, the sampled best response's own limit. */
#define PE_BR_SAMPLING_MAX_ACTIONS 64u
#define PE_BR_SAMPLING_HISTOGRAM 8u

/**
 * Budget and stopping rule. max_samples == 0 turns the adaptive evaluation
 * off (the caller's historical behaviour); any other value turns it on, and
 * the remaining zero fields take their defaults.
 */
typedef struct pe_br_sampling_config_t {
    uint32_t min_samples;       /* per action before the first look; 0 = 4, raised to 2 */
    uint32_t max_samples;       /* per action to decide, hard cap; 0 = off.
                                   A decision makes at most
                                   actions * max_samples + min_samples draws:
                                   the last min_samples estimate the chosen
                                   action (see above). */
    uint32_t check_interval;    /* draws per survivor between looks; 0 = 4 */
    double confidence;          /* of the whole decision, in (0, 1); 0 = 0.95 */
    double absolute_tolerance;  /* a gap this small does not matter; 0 = none */
    double relative_tolerance;  /* ... as a fraction of |leader mean|; 0 = none */
} pe_br_sampling_config_t;

/** How one decision ended. */
typedef enum {
    PE_BR_DECISION_SEPARATED = 0, /* the leader's interval cleared all others */
    PE_BR_DECISION_TOLERANCE,     /* the remaining gap is within tolerance */
    PE_BR_DECISION_MAX_BUDGET,    /* unresolved at max_samples */
    PE_BR_DECISION_SINGLE         /* one action: nothing to decide */
} pe_br_decision_end_t;

/** One decision's outcome. */
typedef struct pe_br_decision_t {
    uint16_t best;              /* chosen action */
    double value;               /* its mean over fresh draws (unbiased) */
    double selection_value;     /* its running mean when chosen (biased up);
                                   equal to value for a single action, where
                                   nothing was selected */
    double gap;                 /* leader mean minus runner-up mean (0 when single) */
    double gap_half_width;      /* leader + runner-up half-widths at the end */
    double best_stderr;         /* leader's standard error at the end, and */
    double runner_stderr;       /* the runner-up's (issue #271): not scaled
                                   by the confidence z (0 when single) */
    uint64_t samples;           /* draws over all actions, estimate included */
    uint16_t eliminated;        /* actions dropped before the end */
    pe_br_decision_end_t end;
} pe_br_decision_t;

/** Totals over many decisions, for telemetry. The four end reasons below
    (separated, tolerance_stops, max_budget_hits, single_action) sum to
    `decisions`, so a reader can tell a decision that had nothing to decide
    from one that stopped early. */
typedef struct pe_br_sampling_stats_t {
    uint64_t decisions;
    uint64_t samples;           /* action draws */
    uint64_t separated;
    uint64_t tolerance_stops;
    uint64_t max_budget_hits;   /* unresolved at the cap */
    uint64_t single_action;     /* nothing to decide: one legal action */
    uint64_t eliminated_actions;
    uint64_t terminal_evaluations; /* filled by the best-response evaluator */
    /* Decisions by draws per action: bucket b holds those with at most
       min_samples * 2^b, the last one everything above. */
    uint64_t histogram[PE_BR_SAMPLING_HISTOGRAM];
    double gap_sum;             /* for the mean final gap */
    double gap_half_width_sum;  /* ... and its mean uncertainty */
    /* The running means the decisions were made on. Their mean sits above
       the chosen actions' true values (that is what the fresh re-estimate
       corrects), so it is the diagnostic for the bias, not an estimate. */
    double selection_value_sum;
} pe_br_sampling_stats_t;

/** Draw one sample of action `action`'s value. Return a finite value, or a
    non-finite one to abort the decision. */
typedef double (*pe_br_sample_fn)(void *user, uint16_t action);

/** Whether the adaptive evaluation is on for this config. */
static inline int pe_br_sampling_enabled(const pe_br_sampling_config_t *config)
{
    return config != NULL && config->max_samples != 0u;
}

/**
 * Fill the defaults into a copy. A config with max_samples == 0 resolves to
 * itself (off). @return 0, or -1 for a confidence outside (0, 1) or so close
 * to 1 that its union-bound level rounds to 1, a negative or non-finite
 * tolerance, min above max, or max above 1 << 20.
 */
int pe_br_sampling_resolve(const pe_br_sampling_config_t *in,
                           pe_br_sampling_config_t *out);

/**
 * The z of every interval of a decision with `actions` actions under a
 * resolved config: the union bound over its looks and actions (see above).
 */
double pe_br_sequential_z(const pe_br_sampling_config_t *resolved,
                          uint16_t actions);

/**
 * Decide the best of `actions` actions with confidence-guided stopping.
 * `config` must be resolved. Draws are requested in a fixed order (by round,
 * then by action), so a seeded sampler reproduces the decision. `stats` may
 * be NULL; when set, the decision is added to it.
 * @return 0, or -1 for bad arguments or a non-finite sample.
 */
int pe_br_resolve_decision(uint16_t actions, pe_br_sample_fn sample,
                           void *user, const pe_br_sampling_config_t *config,
                           pe_br_decision_t *out,
                           pe_br_sampling_stats_t *stats);

/**
 * Set one field from text: "min-samples", "max-samples", "check-interval"
 * (unsigned integers), "confidence", "absolute-tolerance",
 * "relative-tolerance" (numbers). @return 0, or -1.
 */
int pe_br_sampling_parse_option(pe_br_sampling_config_t *config,
                                const char *key, const char *value);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_BR_SAMPLING_H */
