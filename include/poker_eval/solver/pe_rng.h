/*
 * pe_rng.h - Reproducible stream derivation for the solver (v3, CTR-05)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * The solver does not define its own generator. poker_eval/core/pcg_rng.h
 * already provides PCG32 with explicit state, unbiased bounded draws and a
 * bijective mixing helper; duplicating it would mean two places to fix a bias.
 * What the solver adds is the part that was missing: deriving an independent
 * stream from a coordinate in the solve.
 *
 * The rule that makes a parallel run reproducible
 * -----------------------------------------------
 * A stream is derived from the **root seed**, never from a parent generator's
 * current state. Deriving from a live generator would make the numbers a
 * thread receives depend on how far other threads had advanced when it asked —
 * so the same solve, same seed, same thread count would produce different
 * results depending on scheduling. Deriving from the seed and a coordinate
 * makes the stream a pure function of (seed, thread, iteration, player,
 * sample), which is what PAR-04's bit-identical parity gate needs.
 *
 * There is no shared state anywhere in this port: no global, no thread-local,
 * no lazy initialisation. Two solves in one process cannot interfere.
 */

#ifndef POKER_EVAL_PE_RNG_H
#define POKER_EVAL_PE_RNG_H

#include <poker_eval/core/pcg_rng.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The solver's root stream for a seed.
 *
 * Used for anything not tied to a thread or an iteration — shuffling a deck
 * once at setup, for instance. Per-iteration work should take a derived stream
 * instead, so that adding a thread does not change what any other thread draws.
 */
pe_rng_t pe_solver_rng_root(uint64_t seed);

/**
 * An independent stream for one coordinate of the solve.
 *
 * The result is a pure function of its arguments: the same tuple always yields
 * the same sequence, on any machine, whatever the rest of the solve is doing.
 * Distinct tuples yield unrelated sequences — including tuples that differ only
 * by which field carries a value, so (1,0,0,0) and (0,1,0,0) do not collide.
 *
 * @param seed       Root seed, from pe_solver_config_t::seed.
 * @param thread_id  Worker index, 0 for single-threaded execution.
 * @param iteration  Solve iteration.
 * @param player     Player the traversal is updating.
 * @param sample     Sample index within the iteration, 0 when there is one.
 */
pe_rng_t pe_solver_rng_stream(uint64_t seed,
                              uint32_t thread_id,
                              uint64_t iteration,
                              uint32_t player,
                              uint64_t sample);

/**
 * The stream key for a coordinate, without seeding a generator.
 *
 * Exposed for tests and diagnostics: comparing keys is how one checks that two
 * coordinates really do get different streams.
 */
uint64_t pe_solver_rng_key(uint32_t thread_id,
                           uint64_t iteration,
                           uint32_t player,
                           uint64_t sample);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_RNG_H */
