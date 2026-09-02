/*
 * pe_game_rules.h - variant port: chance sampling (architecture v3, CHN-03)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * CHN-01/CHN-02 gave every chance node a name and dealt the flop as one
 * combination. This is the sampling half of the same surface: a game that
 * cannot enumerate its chance space (a deep game, or a wide preflop tree)
 * can still be traversed by direct sampling, provided each drawn outcome
 * carries the importance ratio that re-weights its contribution back onto the
 * uniform (or reference) probability scale.
 *
 * The sampler is part of the game-rules port: enumeration and sampling are two
 * answers to the same question — "what can happen next, and with what weight?"
 * The full port (betting limits, terminal vectors, infoset identity) arrives
 * with the ticket that owns that surface; what belongs here is exactly the
 * chance protocol, because that is what a sampled traversal consumes.
 */

#ifndef POKER_EVAL_PE_GAME_RULES_H
#define POKER_EVAL_PE_GAME_RULES_H

#include <poker_eval/core/pcg_rng.h>
#include <poker_eval/solver/pe_chance.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ *
 * Chance sampling
 * ------------------------------------------------------------------ */

/**
 * One drawn chance outcome.
 *
 * `outcome` is the same numbering the enumerated surface uses, so a caller can
 * apply either path to the same transition. `importance_ratio` is the weight a
 * sampled traversal must multiply a child's contribution by so that the
 * expectation matches an exhaustive sweep: it is p(reference) / p(actual).
 * For a uniform deal (a flop combination, or an unbunched board card) the
 * ratio is 1.0.
 */
typedef struct pe_chance_sample_t
{
    int outcome;             /* outcome index, as get_chance_outcomes() counts */
    double importance_ratio; /* p_reference / p_actual, 1.0 when uniform */
} pe_chance_sample_t;

/**
 * Game-rules chance sampler: choose one outcome from the state's deal.
 *
 * The `state` is opaque because the port does not own the state type; the
 * adapter that implements the sampler casts it back to its own state. Returns
 * zero on success, nonzero when the state is not a chance node of a kind this
 * sampler understands (or the arguments are NULL).
 */
typedef int (*pe_chance_sample_fn)(const void *state, pe_rng_t *rng,
                                   pe_chance_sample_t *out);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_GAME_RULES_H */
