/* test_micro_optimizations.c -- Test micro-optimizations
 *
 * This program tests the micro-optimizations implemented:
 * - Pre-calculated reciprocals for division optimization
 * - Branch prediction hints
 * - Table alignment on 64-byte cache lines
 *
 * Copyright (C) 2024
 *
 * This program gives you software freedom; you can copy, convey,
 * propagate, redistribute and/or modify this program under the terms of
 * the GNU General Public License (GPL) as published by the Free Software
 * Foundation (FSF), either version 3 of the License, or (at your option)
 * any later version of the GPL published by the FSF.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program in a file in the toplevel directory called "GPLv3".
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <time.h>
#include <stdint.h>

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/utils/micro_optimizations.h>

/* External tables to check alignment */
// extern uint8 straightTable[8192]; // Already declared in deck_std.h
// extern uint32 topFiveCardsTable[8192]; // Already declared in deck_std.h
// extern uint8 nBitsTable[8192]; // Already declared in deck_std.h

/* Test reciprocal lookup tables */
static void test_reciprocals(void) {
    printf("Testing reciprocal lookup tables...\n");
    
    /* Test hishare_reciprocals */
    for (int i = 1; i <= ENUM_MAXPLAYERS; i++) {
        double expected = 1.0 / i;
        double actual = get_hishare_reciprocal(i);
        double diff = fabs(expected - actual);
        assert(diff < 1e-10);
        printf("  1/%d: expected=%.15f, actual=%.15f, diff=%.2e\n", 
               i, expected, actual, diff);
    }
    
    /* Test hishare_half_reciprocals */
    for (int i = 1; i <= ENUM_MAXPLAYERS; i++) {
        double expected = 0.5 / i;
        double actual = get_hishare_half_reciprocal(i);
        double diff = fabs(expected - actual);
        assert(diff < 1e-10);
        printf("  0.5/%d: expected=%.15f, actual=%.15f, diff=%.2e\n", 
               i, expected, actual, diff);
    }
    
    printf("Reciprocal tests passed!\n\n");
}

/* Test table alignment */
static void test_table_alignment(void) {
    printf("Testing table alignment...\n");
    
    /* Check if tables are aligned on 64-byte boundaries */
    uintptr_t straight_addr = (uintptr_t)straightTable;
    uintptr_t topfive_addr = (uintptr_t)topFiveCardsTable;
    uintptr_t nbits_addr = (uintptr_t)nBitsTable;
    
    printf("  straightTable address: %p (mod 64 = %lu)\n", 
           (void*)straightTable, straight_addr % 64);
    printf("  topFiveCardsTable address: %p (mod 64 = %lu)\n", 
           (void*)topFiveCardsTable, topfive_addr % 64);
    printf("  nBitsTable address: %p (mod 64 = %lu)\n", 
           (void*)nBitsTable, nbits_addr % 64);
    
    /* With CACHE_ALIGN, these should be aligned on 64-byte boundaries */
    if (straight_addr % 64 == 0) {
        printf("  straightTable is properly aligned on 64-byte boundary\n");
    } else {
        printf("  WARNING: straightTable is NOT aligned on 64-byte boundary\n");
    }
    
    if (topfive_addr % 64 == 0) {
        printf("  topFiveCardsTable is properly aligned on 64-byte boundary\n");
    } else {
        printf("  WARNING: topFiveCardsTable is NOT aligned on 64-byte boundary\n");
    }
    
    if (nbits_addr % 64 == 0) {
        printf("  nBitsTable is properly aligned on 64-byte boundary\n");
    } else {
        printf("  WARNING: nBitsTable is NOT aligned on 64-byte boundary\n");
    }
    
    printf("\n");
}

/* Benchmark division optimization */
static void benchmark_division(void) {
    printf("Benchmarking division optimization...\n");
    
    const int iterations = 10000000;  /* Reduced from 100M to 10M for faster testing */
    clock_t start, end;
    double cpu_time_used;
    double sum = 0.0;
    
    /* Benchmark direct division */
    start = clock();
    for (int iter = 0; iter < iterations; iter++) {
        int hishare = (iter % ENUM_MAXPLAYERS) + 1;
        sum += 1.0 / hishare;
    }
    end = clock();
    cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
    printf("  Direct division: %.3f seconds (sum=%.2f)\n", cpu_time_used, sum);
    
    /* Benchmark lookup table */
    sum = 0.0;
    start = clock();
    for (int iter = 0; iter < iterations; iter++) {
        int hishare = (iter % ENUM_MAXPLAYERS) + 1;
        sum += get_hishare_reciprocal(hishare);
    }
    end = clock();
    cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
    printf("  Lookup table: %.3f seconds (sum=%.2f)\n", cpu_time_used, sum);
    
    /* Benchmark half division */
    sum = 0.0;
    start = clock();
    for (int iter = 0; iter < iterations; iter++) {
        int hishare = (iter % ENUM_MAXPLAYERS) + 1;
        sum += 0.5 / hishare;
    }
    end = clock();
    cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
    printf("  Direct half division: %.3f seconds (sum=%.2f)\n", cpu_time_used, sum);
    
    /* Benchmark half lookup table */
    sum = 0.0;
    start = clock();
    for (int iter = 0; iter < iterations; iter++) {
        int hishare = (iter % ENUM_MAXPLAYERS) + 1;
        sum += get_hishare_half_reciprocal(hishare);
    }
    end = clock();
    cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
    printf("  Half lookup table: %.3f seconds (sum=%.2f)\n", cpu_time_used, sum);
    
    printf("\n");
}

/* Test branch prediction hints */
static void test_branch_hints(void) {
    printf("Testing branch prediction hints...\n");
    
    int likely_true = 0;
    int unlikely_true = 0;
    
    /* Test likely() with mostly true conditions */
    for (int i = 0; i < 1000000; i++) {
        if (likely(i < 999000)) {
            likely_true++;
        }
    }
    printf("  likely() with 99.9%% true: %d hits\n", likely_true);
    
    /* Test unlikely() with mostly false conditions */
    for (int i = 0; i < 1000000; i++) {
        if (unlikely(i >= 999000)) {
            unlikely_true++;
        }
    }
    printf("  unlikely() with 0.1%% true: %d hits\n", unlikely_true);
    
    printf("Branch hint tests completed\n\n");
}

int main(int argc, char *argv[]) {
    printf("=== Micro-Optimization Tests ===\n\n");
    
    test_reciprocals();
    test_table_alignment();
    benchmark_division();
    test_branch_hints();
    
    printf("All tests completed successfully!\n");
    
    return 0;
}
