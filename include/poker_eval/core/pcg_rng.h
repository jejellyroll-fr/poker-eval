/*
 * pcg_rng.h - Per-thread PCG32 random number generator
 *
 * Replaces the global libc rand()/srand() through the sampling core.
 * Every thread owns an independent thread-local PCG32 stream, so
 * multithreaded sampling no longer contends on a shared global state
 * and streams can be seeded deterministically per thread/matchup.
 */
#ifndef PE_PCG_RNG_H
#define PE_PCG_RNG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* PCG32 state (32-bit output, XSH-RS scheme). */
typedef struct pe_rng_t {
    uint64_t state;
} pe_rng_t;

/* Seed a stream. Identical seeds reproduce identical output. */
void pe_rng_seed(pe_rng_t *rng, uint64_t seed);

/* Raw 32-bit output. */
uint32_t pe_rng_next(pe_rng_t *rng);

/* Unbiased integer in [0, bound) via rejection sampling (no modulo bias). */
uint32_t pe_rng_below(pe_rng_t *rng, uint32_t bound);

/* Uniform double in [0, 1). */
double pe_rng_uniform01(pe_rng_t *rng);

/* Bijective 64-bit mixing helper for deriving stream/thread keys. */
uint64_t pe_rng_mix(uint64_t value);

/* Derive a stream seed from a base seed and a key (e.g. thread or matchup
 * index). Same (base, key) pair always yields the same stream. */
uint64_t pe_rng_derive(uint64_t base, uint64_t key);

/* Global base seed from which per-thread streams are derived. */
void pe_rng_set_base_seed(uint64_t seed);
uint64_t pe_rng_base_seed(void);

/* Thread-local stream of the calling thread. */
pe_rng_t *pe_rng_current(void);
void pe_rng_seed_current(uint64_t seed);

#ifdef __cplusplus
}
#endif

#endif /* PE_PCG_RNG_H */
