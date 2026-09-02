/*
 * rng.c - Reproducible stream derivation (architecture v3, CTR-05)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * Three functions and no state. Everything here is a pure function of its
 * arguments: no global, no thread-local, no cached table. That is not
 * austerity for its own sake — it is the property that lets PAR-04 demand a
 * bit-identical result from 1, 2, 4 and 8 threads.
 *
 * The generator itself comes from poker_eval/core/pcg_rng.h.
 */

#include <poker_eval/solver/pe_rng.h>

uint64_t pe_solver_rng_key(uint32_t thread_id,
                           uint64_t iteration,
                           uint32_t player,
                           uint64_t sample)
{
    /* Mixing between each field rather than folding them together in one step.
       A plain XOR or sum would make the coordinates commutative, so (1,0,0,0)
       and (0,1,0,0) would collide and two threads would silently share a
       stream. pe_rng_mix is bijective, so chaining it keeps the fields
       distinguishable and order-dependent. */
    uint64_t key = pe_rng_mix((uint64_t)thread_id);
    key = pe_rng_mix(key ^ iteration);
    key = pe_rng_mix(key ^ (uint64_t)player);
    key = pe_rng_mix(key ^ sample);
    return key;
}

pe_rng_t pe_solver_rng_root(uint64_t seed)
{
    pe_rng_t rng;
    pe_rng_seed(&rng, seed);
    return rng;
}

pe_rng_t pe_solver_rng_stream(uint64_t seed,
                              uint32_t thread_id,
                              uint64_t iteration,
                              uint32_t player,
                              uint64_t sample)
{
    pe_rng_t rng;

    /* From the seed, never from a live generator: the stream a worker receives
       must not depend on how far any other worker has advanced. */
    pe_rng_seed(&rng, pe_rng_derive(seed, pe_solver_rng_key(thread_id, iteration,
                                                            player, sample)));
    return rng;
}
