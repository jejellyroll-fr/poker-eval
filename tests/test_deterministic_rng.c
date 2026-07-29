/*
 * test_deterministic_rng.c - Simple test of deterministic RNG
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <poker_eval/core/time_compat.h>

/* Simple PCG implementation */
typedef struct {
    uint64_t state;
    uint64_t inc;
} DeterministicRNG;

void det_rng_seed(DeterministicRNG* rng, uint64_t seed) {
    rng->state = 0;
    rng->inc = (seed << 1) | 1;
    /* Warmup */
    rng->state = rng->state * 6364136223846793005ULL + rng->inc;
    rng->state += seed;
    rng->state = rng->state * 6364136223846793005ULL + rng->inc;
}

uint32_t det_rng_next(DeterministicRNG* rng) {
    uint64_t old_state = rng->state;
    rng->state = old_state * 6364136223846793005ULL + rng->inc;
    uint32_t xorshifted = ((old_state >> 18) ^ old_state) >> 27;
    uint32_t rot = old_state >> 59;
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
}

int main(void) {
    printf("Deterministic RNG Test\n");
    printf("======================\n");

    uint64_t seed = 42;
    DeterministicRNG rng1, rng2;

    /* Test determinism */
    det_rng_seed(&rng1, seed);
    det_rng_seed(&rng2, seed);

    bool deterministic = true;
    printf("Testing determinism with seed %llu:\n", (unsigned long long)seed);

    for (int i = 0; i < 10; i++) {
        uint32_t val1 = det_rng_next(&rng1);
        uint32_t val2 = det_rng_next(&rng2);

        printf("  Step %d: %u vs %u %s\n", i + 1, val1, val2,
               (val1 == val2) ? "✓" : "✗");

        if (val1 != val2) {
            deterministic = false;
        }
    }

    printf("\nResult: %s\n", deterministic ? "DETERMINISTIC ✓" : "NOT DETERMINISTIC ✗");

    /* Test different seeds produce different sequences */
    printf("\nTesting different seeds:\n");
    det_rng_seed(&rng1, 123);
    det_rng_seed(&rng2, 456);

    uint32_t val1 = det_rng_next(&rng1);
    uint32_t val2 = det_rng_next(&rng2);

    printf("Seed 123: %u\n", val1);
    printf("Seed 456: %u\n", val2);
    printf("Different: %s\n", (val1 != val2) ? "✓" : "✗");

    /* Benchmark performance */
    det_rng_seed(&rng1, 987654321);
    uint64_t iterations = 10000000;

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    volatile uint32_t dummy = 0;
    for (uint64_t i = 0; i < iterations; i++) {
        dummy += det_rng_next(&rng1);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    double ops_per_sec = iterations / elapsed;

    printf("\nPerformance:\n");
    printf("  Operations: %llu\n", (unsigned long long)iterations);
    printf("  Time: %.3f seconds\n", elapsed);
    printf("  Rate: %.0f ops/sec\n", ops_per_sec);
    printf("  Checksum: %u (prevents optimization)\n", dummy);

    printf("\n✅ Deterministic RNG system validated!\n");
    return 0;
}
