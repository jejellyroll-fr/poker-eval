/* bench_compressed_tables.c - Benchmark compressed vs original tables */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <inttypes.h>
#include <poker_eval/poker_eval.h>
#include <poker_eval/utils/compressed_tables.h>
#include <poker_eval/utils/aligned_tables.h>

/* External tables are declared in deck_std.h */

/* Timer function */
static double get_time(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}

/* Benchmark straight table access */
static void bench_straight_table(int iterations) {
    printf("\n=== Straight Table Benchmark ===\n");
    
    /* Generate random indices */
    srand((unsigned int)time(NULL));
    int* indices = malloc(iterations * sizeof(int));
    for (int i = 0; i < iterations; i++) {
        indices[i] = rand() % 8192;
    }
    
    /* Get aligned table */
    const uint8_t* aligned_straight = get_aligned_straightTable();
    
    /* Benchmark original table */
    uint64_t sum1 = 0;
    double t1 = get_time();
    for (int i = 0; i < iterations; i++) {
        sum1 += straightTable[indices[i]];
    }
    t1 = get_time() - t1;
    
    /* Benchmark aligned table */
    uint64_t sum2 = 0;
    double t2 = get_time();
    for (int i = 0; i < iterations; i++) {
        sum2 += aligned_straight[indices[i]];
    }
    t2 = get_time() - t2;
    
    /* Benchmark compressed table */
    uint64_t sum3 = 0;
    double t3 = get_time();
    for (int i = 0; i < iterations; i++) {
        sum3 += get_straight_value((uint16_t)indices[i]);
    }
    t3 = get_time() - t3;
    
    printf("Original table:   %.3f ms (sum=%" PRIu64 ")\n", t1 * 1000, sum1);
    printf("Aligned table:    %.3f ms (sum=%" PRIu64 ") - Speedup: %.2fx\n", t2 * 1000, sum2, t1/t2);
    printf("Compressed table: %.3f ms (sum=%" PRIu64 ") - Speedup: %.2fx\n", t3 * 1000, sum3, t1/t3);
    printf("Results match: %s\n", (sum1 == sum2 && sum2 == sum3) ? "YES" : "NO");
    
    free(indices);
}

/* Benchmark top five cards table access */
static void bench_topfive_table(int iterations) {
    printf("\n=== Top Five Cards Table Benchmark ===\n");
    
    /* Generate random indices */
    int* indices = malloc(iterations * sizeof(int));
    for (int i = 0; i < iterations; i++) {
        indices[i] = rand() % 8192;
    }
    
    /* Benchmark original table */
    uint64_t sum1 = 0;
    double t1 = get_time();
    for (int i = 0; i < iterations; i++) {
        sum1 += topFiveCardsTable[indices[i]];
    }
    t1 = get_time() - t1;
    
    /* Benchmark compressed table */
    uint64_t sum2 = 0;
    double t2 = get_time();
    for (int i = 0; i < iterations; i++) {
        sum2 += get_topfive_value((uint16_t)indices[i]);
    }
    t2 = get_time() - t2;
    
    printf("Original table: %.3f ms (sum=%" PRIu64 ")\n", t1 * 1000, sum1);
    printf("Compressed table: %.3f ms (sum=%" PRIu64 ")\n", t2 * 1000, sum2);
    printf("Speedup: %.2fx\n", t1/t2);
    printf("Results match: %s\n", sum1 == sum2 ? "YES" : "NO");
    
    free(indices);
}

/* Benchmark cache-friendly sequential access */
static void bench_sequential_access(int iterations) {
    printf("\n=== Sequential Access Benchmark ===\n");
    
    /* Original tables */
    volatile uint64_t sum1 = 0;
    double t1 = get_time();
    for (int iter = 0; iter < iterations; iter++) {
        for (int i = 0; i < 8192; i++) {
            sum1 += straightTable[i] + topFiveCardsTable[i];
        }
    }
    t1 = get_time() - t1;
    
    /* Compressed tables */
    volatile uint64_t sum2 = 0;
    double t2 = get_time();
    for (int iter = 0; iter < iterations; iter++) {
        for (int i = 0; i < 8192; i++) {
            sum2 += get_straight_value((uint16_t)i) + get_topfive_value((uint16_t)i);
        }
    }
    t2 = get_time() - t2;
    
    printf("Original tables: %.3f ms (sum=%" PRIu64 ")\n", t1 * 1000, sum1);
    printf("Compressed tables: %.3f ms (sum=%" PRIu64 ")\n", t2 * 1000, sum2);
    printf("Speedup: %.2fx\n", t1/t2);
}

/* Verify correctness */
static void verify_correctness(void) {
    printf("\n=== Correctness Verification ===\n");
    
    int errors = 0;
    
    /* Verify straight table */
    for (int i = 0; i < 8192; i++) {
        uint8_t orig = straightTable[i];
        uint8_t comp = get_straight_value((uint16_t)i);
        if (orig != comp) {
            printf("Error at straight[%d]: %d != %d\n", i, orig, comp);
            errors++;
            if (errors > 10) break;
        }
    }
    
    /* Verify top five cards table */
    for (int i = 0; i < 8192; i++) {
        uint32_t orig = topFiveCardsTable[i];
        uint32_t comp = get_topfive_value((uint16_t)i);
        if (orig != comp) {
            printf("Error at topfive[%d]: %u != %u\n", i, orig, comp);
            errors++;
            if (errors > 10) break;
        }
    }
    
    printf("Total errors: %d\n", errors);
    if (errors == 0) {
        printf("All values match correctly!\n");
    }
}

int main(void) {
    printf("=== Compressed & Aligned Tables Benchmark ===\n");
    
    /* Initialize tables */
    printf("\nInitializing aligned tables...\n");
    init_aligned_tables();
    
    printf("Initializing compressed tables...\n");
    init_compressed_tables();
    
    /* Get compression stats */
    size_t original_size, compressed_size;
    get_compression_stats(&original_size, &compressed_size);
    printf("\nMemory usage:\n");
    printf("Original size: %zu KB\n", original_size / 1024);
    printf("Aligned size: %zu KB (same data, 64-byte aligned)\n", original_size / 1024);
    printf("Compressed size: %zu KB\n", compressed_size / 1024);
    if (compressed_size > 0) {
        printf("Compression ratio: %.2fx\n", (double)original_size / (double)compressed_size);
    }
    
    /* Verify correctness first */
    verify_correctness();
    
    /* Run benchmarks */
    int iterations = 10000000;
    bench_straight_table(iterations);
    bench_topfive_table(iterations);
    bench_sequential_access(100);
    
    /* Cleanup */
    cleanup_compressed_tables();
    
    return 0;
}
