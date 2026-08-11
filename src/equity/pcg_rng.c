/*
 * pcg_rng.c - Per-thread PCG32 random number generator
 *
 * PCG XSH-RS scheme (O'Neill). Same constants as the ISMCTS solver's
 * private pcg_next(), shared here so the whole sampling core draws from
 * one unbiased, thread-local implementation.
 */
#include <poker_eval/core/pcg_rng.h>

#if defined(_MSC_VER)
#define PE_THREAD_LOCAL __declspec(thread)
#else
#define PE_THREAD_LOCAL __thread
#endif

#define PE_PCG_MULTIPLIER 6364136223846793005ULL
#define PE_PCG_INCREMENT  1442695040888963407ULL

static PE_THREAD_LOCAL pe_rng_t g_tls_rng;
static uint64_t g_base_seed = 0x1234567890ABCDEFULL;

uint64_t pe_rng_mix(uint64_t value)
{
    value += 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31);
}

uint64_t pe_rng_derive(uint64_t base, uint64_t key)
{
    return pe_rng_mix(base ^ pe_rng_mix(key));
}

void pe_rng_seed(pe_rng_t *rng, uint64_t seed)
{
    uint64_t s = pe_rng_mix(seed);
    rng->state = s ? s : 1ULL;
}

uint32_t pe_rng_next(pe_rng_t *rng)
{
    uint64_t oldstate = rng->state;
    rng->state = oldstate * PE_PCG_MULTIPLIER + PE_PCG_INCREMENT;
    uint32_t xorshifted = (uint32_t)(((oldstate >> 18u) ^ oldstate) >> 27u);
    uint32_t rot = (uint32_t)(oldstate >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((~rot + 1u) & 31u));
}

uint32_t pe_rng_below(pe_rng_t *rng, uint32_t bound)
{
    if (bound <= 1)
        return 0;
    /* Rejection threshold: results in [threshold, 2^32) map uniformly
     * onto [0, bound) with no modulo bias. */
    uint32_t threshold = (uint32_t)(0u - bound) % bound;
    for (;;) {
        uint32_t r = pe_rng_next(rng);
        if (r >= threshold)
            return r % bound;
    }
}

double pe_rng_uniform01(pe_rng_t *rng)
{
    return (double)(pe_rng_next(rng) >> 8) * (1.0 / 16777216.0);
}

void pe_rng_set_base_seed(uint64_t seed)
{
    g_base_seed = seed;
    pe_rng_seed_current(seed);
}

uint64_t pe_rng_base_seed(void)
{
    return g_base_seed;
}

pe_rng_t *pe_rng_current(void)
{
    if (g_tls_rng.state == 0)
        pe_rng_seed(&g_tls_rng, g_base_seed);
    return &g_tls_rng;
}

void pe_rng_seed_current(uint64_t seed)
{
    pe_rng_seed(pe_rng_current(), seed);
}
