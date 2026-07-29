/* test_aligned_tables.c -- Test dynamic table alignment
 *
 * Copyright (C) 2024
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/utils/aligned_tables_dynamic.h>

/* External original tables - already declared in deck_std.h */
// extern uint8_t straightTable[8192];
// extern uint32_t topFiveCardsTable[8192];
// extern uint8_t nBitsTable[8192];

/* Test alignment and performance */
static void test_alignment(void) {
    printf("=== Testing Table Alignment ===\n\n");
    
    /* Original tables */
    printf("Original tables:\n");
    printf("  straightTable:     %p (mod 64 = %lu)\n", 
           (void*)straightTable, (uintptr_t)straightTable % 64);
    printf("  topFiveCardsTable: %p (mod 64 = %lu)\n", 
           (void*)topFiveCardsTable, (uintptr_t)topFiveCardsTable % 64);
    printf("  nBitsTable:        %p (mod 64 = %lu)\n", 
           (void*)nBitsTable, (uintptr_t)nBitsTable % 64);
    
    /* Initialize aligned tables */
    printf("\nInitializing aligned tables...\n");
    if (init_dynamic_aligned_tables() != 0) {
        printf("Failed to initialize aligned tables!\n");
        return;
    }
    
    /* Get aligned tables */
    uint8_t *aligned_straight = get_dynamic_aligned_straightTable();
    uint32_t *aligned_topfive = get_dynamic_aligned_topFiveCardsTable();
    uint8_t *aligned_nbits = get_dynamic_aligned_nBitsTable();
    
    printf("\nAligned tables:\n");
    printf("  straightTable:     %p (mod 64 = %lu) %s\n", 
           (void*)aligned_straight, (uintptr_t)aligned_straight % 64,
           ((uintptr_t)aligned_straight % 64 == 0) ? "✓ ALIGNED" : "✗ NOT ALIGNED");
    printf("  topFiveCardsTable: %p (mod 64 = %lu) %s\n", 
           (void*)aligned_topfive, (uintptr_t)aligned_topfive % 64,
           ((uintptr_t)aligned_topfive % 64 == 0) ? "✓ ALIGNED" : "✗ NOT ALIGNED");
    printf("  nBitsTable:        %p (mod 64 = %lu) %s\n", 
           (void*)aligned_nbits, (uintptr_t)aligned_nbits % 64,
           ((uintptr_t)aligned_nbits % 64 == 0) ? "✓ ALIGNED" : "✗ NOT ALIGNED");
    
    /* Verify data integrity */
    printf("\nVerifying data integrity...\n");
    int errors = 0;
    
    for (int i = 0; i < 8192; i++) {
        if (aligned_straight[i] != straightTable[i]) {
            errors++;
            if (errors < 5) {
                printf("  Error at straightTable[%d]: %d != %d\n", 
                       i, aligned_straight[i], straightTable[i]);
            }
        }
        if (aligned_topfive[i] != topFiveCardsTable[i]) {
            errors++;
            if (errors < 5) {
                printf("  Error at topFiveCardsTable[%d]: %u != %u\n", 
                       i, aligned_topfive[i], topFiveCardsTable[i]);
            }
        }
        if (aligned_nbits[i] != nBitsTable[i]) {
            errors++;
            if (errors < 5) {
                printf("  Error at nBitsTable[%d]: %d != %d\n", 
                       i, aligned_nbits[i], nBitsTable[i]);
            }
        }
    }
    
    if (errors == 0) {
        printf("  ✓ All data verified correctly!\n");
    } else {
        printf("  ✗ Found %d errors in data!\n", errors);
    }
}

/* Benchmark cache performance */
static void benchmark_cache_performance(void) {
    printf("\n=== Cache Performance Benchmark ===\n\n");
    
    const int iterations = 10000000;
    const int stride = 64;  /* Access pattern to stress cache */
    clock_t start, end;
    double cpu_time_used;
    uint32_t sum = 0;
    
    /* Benchmark original tables */
    printf("Original tables (potentially misaligned):\n");
    
    start = clock();
    for (int iter = 0; iter < iterations; iter++) {
        int idx = (iter * stride) % 8192;
        sum += straightTable[idx];
        sum += topFiveCardsTable[idx];
        sum += nBitsTable[idx];
    }
    end = clock();
    cpu_time_used = ((double)(end - start)) / CLOCKS_PER_SEC;
    printf("  Time: %.3f seconds (sum=%u)\n", cpu_time_used, sum);
    
    /* Benchmark aligned tables */
    uint8_t *aligned_straight = get_dynamic_aligned_straightTable();
    uint32_t *aligned_topfive = get_dynamic_aligned_topFiveCardsTable();
    uint8_t *aligned_nbits = get_dynamic_aligned_nBitsTable();
    
    printf("\nAligned tables:\n");
    
    sum = 0;
    start = clock();
    for (int iter = 0; iter < iterations; iter++) {
        int idx = (iter * stride) % 8192;
        sum += aligned_straight[idx];
        sum += aligned_topfive[idx];
        sum += aligned_nbits[idx];
    }
    end = clock();
    cpu_time_used = ((double)(end - start)) / CLOCKS_PER_SEC;
    printf("  Time: %.3f seconds (sum=%u)\n", cpu_time_used, sum);
    
    /* Random access pattern */
    printf("\nRandom access pattern:\n");
    
    /* Initialize random indices */
    int *random_indices = malloc(iterations * sizeof(int));
    srand(42);
    for (int i = 0; i < iterations; i++) {
        random_indices[i] = rand() % 8192;
    }
    
    /* Original tables */
    sum = 0;
    start = clock();
    for (int iter = 0; iter < iterations; iter++) {
        int idx = random_indices[iter];
        sum += straightTable[idx];
        sum += topFiveCardsTable[idx];
        sum += nBitsTable[idx];
    }
    end = clock();
    cpu_time_used = ((double)(end - start)) / CLOCKS_PER_SEC;
    printf("  Original: %.3f seconds\n", cpu_time_used);
    
    /* Aligned tables */
    sum = 0;
    start = clock();
    for (int iter = 0; iter < iterations; iter++) {
        int idx = random_indices[iter];
        sum += aligned_straight[idx];
        sum += aligned_topfive[idx];
        sum += aligned_nbits[idx];
    }
    end = clock();
    cpu_time_used = ((double)(end - start)) / CLOCKS_PER_SEC;
    printf("  Aligned:  %.3f seconds\n", cpu_time_used);
    
    free(random_indices);
}

int main(void) {
    test_alignment();
    benchmark_cache_performance();
    
    /* Cleanup */
    cleanup_dynamic_aligned_tables();
    
    printf("\n=== Test Complete ===\n");
    return 0;
}
