/* bench_arch_comparison.c - Compare performance of different architecture-specific builds */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <inttypes.h>
#if !defined(_WIN32)
#include <dlfcn.h>
#endif
#include <poker_eval/poker_eval.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/enumdefs.h>

/* Timer function */
static double get_time(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}

/* Function pointer types for dynamic loading */
typedef int (*StdDeck_StdRules_EVAL_N_t)(StdDeck_CardMask, int);
typedef int (*enumerate_all_t)(enum_game_t, StdDeck_CardMask*, StdDeck_CardMask, StdDeck_CardMask, int, int, int, int, enum_result_t*);

/* Benchmark structure */
typedef struct {
    const char* name;
    void* handle;
    StdDeck_StdRules_EVAL_N_t eval_n;
    enumerate_all_t enumerate_all;
    double eval_time;
    double enum_time;
} LibBenchmark;

/* Benchmark hand evaluation */
static void bench_hand_evaluation(LibBenchmark* bench, int iterations) {
    StdDeck_CardMask hand;
    int values[52];
    
    /* Initialize random cards */
    srand(42); // Fixed seed for reproducibility
    for (int i = 0; i < 52; i++) {
        values[i] = i;
    }
    
    /* Benchmark */
    uint64_t sum = 0;
    double start = get_time();
    
    for (int i = 0; i < iterations; i++) {
        /* Shuffle first 7 cards */
        for (int j = 0; j < 7; j++) {
            int k = j + (rand() % (52 - j));
            int temp = values[j];
            values[j] = values[k];
            values[k] = temp;
        }
        
        /* Create hand */
        StdDeck_CardMask_RESET(hand);
        for (int j = 0; j < 7; j++) {
            StdDeck_CardMask_SET(hand, values[j]);
        }
        
        /* Evaluate */
        if (bench->eval_n) {
            sum += bench->eval_n(hand, 7);
        } else {
            sum += StdDeck_StdRules_EVAL_N(hand, 7);
        }
    }
    
    bench->eval_time = get_time() - start;
    printf("  Hand evaluation: %.3f ms (%" PRIu64 " hands/sec, sum=%" PRIu64 ")\n", 
           bench->eval_time * 1000, 
           (uint64_t)(iterations / bench->eval_time),
           sum);
}

/* Benchmark enumeration */
static void bench_enumeration(LibBenchmark* bench) {
    StdDeck_CardMask dead, pockets[2], board;
    enum_result_t result;
    
    /* Setup AA vs KK */
    StdDeck_CardMask_RESET(dead);
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    
    /* AA */
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS));
    
    /* KK */
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));
    
    /* Add to dead cards */
    StdDeck_CardMask_OR(dead, dead, pockets[0]);
    StdDeck_CardMask_OR(dead, dead, pockets[1]);
    
    /* Initialize result */
    enumResultAlloc(&result, 2, enum_ordering_mode_hi);
    
    /* Benchmark */
    double start = get_time();
    
    if (bench->enumerate_all) {
        bench->enumerate_all(game_holdem, pockets, board, dead, 2, 0, 0, 0, &result);
    } else {
        enumExhaustive_dispatch(game_holdem, pockets, board, dead, 2, 0, 0, &result);
    }
    
    bench->enum_time = get_time() - start;
    printf("  AA vs KK enumeration: %.3f ms (%.0f hands/sec)\n", 
           bench->enum_time * 1000,
           (double)result.nsamples / bench->enum_time);
    printf("    Results: AA wins %.2f%%, KK wins %.2f%%, ties %.2f%%\n",
           100.0 * result.ev[0] / result.nsamples,
           100.0 * result.ev[1] / result.nsamples,
           100.0 * (result.nsamples - result.ev[0] - result.ev[1]) / result.nsamples);
    
    enumResultFree(&result);
}

/* CPU feature detection */
static void print_cpu_features(void) {
    printf("\n=== CPU Features ===\n");
    
#if defined(__x86_64__) || defined(_M_X64)
    #if defined(__GNUC__) || defined(__clang__)
        __builtin_cpu_init();
        
        printf("SSE2:    %s\n", __builtin_cpu_supports("sse2") ? "Yes" : "No");
        printf("SSE4.1:  %s\n", __builtin_cpu_supports("sse4.1") ? "Yes" : "No");
        printf("AVX:     %s\n", __builtin_cpu_supports("avx") ? "Yes" : "No");
        printf("AVX2:    %s\n", __builtin_cpu_supports("avx2") ? "Yes" : "No");
        printf("FMA:     %s\n", __builtin_cpu_supports("fma") ? "Yes" : "No");
        printf("AVX512F: %s\n", __builtin_cpu_supports("avx512f") ? "Yes" : "No");
    #else
        printf("CPU feature detection not available on this compiler\n");
    #endif
#else
    printf("Not an x86-64 architecture\n");
#endif
}

int main(void) {
    printf("=== Architecture-Specific Library Comparison ===\n");
    
    /* Print CPU features */
    print_cpu_features();
    
    /* Libraries to test */
    LibBenchmark benchmarks[] = {
        {"Native (static link)", NULL, NULL, NULL, 0, 0},
        {"Generic x86-64", NULL, NULL, NULL, 0, 0},
        {"AVX2 optimized", NULL, NULL, NULL, 0, 0},
        {"AVX512 optimized", NULL, NULL, NULL, 0, 0}
    };
    
    /* Try to load dynamic libraries */
    printf("\n=== Loading Libraries ===\n");
#if defined(_WIN32)
    printf("Dynamic library loading not supported on Windows; using static benchmark only.\n");
#else
    const char* lib_paths[] = {
        NULL,  // Static link
        "./libpoker_lib_generic.so",
        "./libpoker_lib_avx2.so",
        "./libpoker_lib_avx512.so"
    };
    for (int i = 1; i < 4; i++) {
        benchmarks[i].handle = dlopen(lib_paths[i], RTLD_NOW);
        if (benchmarks[i].handle) {
            printf("%s: Loaded successfully\n", benchmarks[i].name);
            
            /* Try to get function pointers */
            void *eval_n_ptr = dlsym(benchmarks[i].handle, "StdDeck_StdRules_EVAL_N");
            void *enumerate_all_ptr = dlsym(benchmarks[i].handle, "enumerate_all");
            
            /* Use union to safely convert void* to function pointer */
            union { void *ptr; StdDeck_StdRules_EVAL_N_t func; } eval_n_conv = { .ptr = eval_n_ptr };
            union { void *ptr; enumerate_all_t func; } enumerate_all_conv = { .ptr = enumerate_all_ptr };
            
            benchmarks[i].eval_n = eval_n_conv.func;
            benchmarks[i].enumerate_all = enumerate_all_conv.func;
        } else {
            printf("%s: Not found (%s)\n", benchmarks[i].name, dlerror());
        }
    }
#endif
    
    /* Run benchmarks */
    printf("\n=== Performance Benchmarks ===\n");
    int eval_iterations = 10000000;
    
    for (int i = 0; i < 4; i++) {
        if (i == 0 || benchmarks[i].handle) {
            printf("\n%s:\n", benchmarks[i].name);
            bench_hand_evaluation(&benchmarks[i], eval_iterations);
            bench_enumeration(&benchmarks[i]);
        }
    }
    
    /* Compare results */
    printf("\n=== Performance Summary ===\n");
    printf("%-20s %10s %10s\n", "Library", "Eval Time", "Enum Time");
    printf("%-20s %10s %10s\n", "-------", "---------", "---------");
    
    double base_eval = benchmarks[0].eval_time;
    double base_enum = benchmarks[0].enum_time;
    
    for (int i = 0; i < 4; i++) {
        if (i == 0 || benchmarks[i].handle) {
            printf("%-20s %9.3fms %9.3fms", 
                   benchmarks[i].name, 
                   benchmarks[i].eval_time * 1000,
                   benchmarks[i].enum_time * 1000);
            
            if (i > 0) {
                printf(" (%.2fx, %.2fx)",
                       base_eval / benchmarks[i].eval_time,
                       base_enum / benchmarks[i].enum_time);
            }
            printf("\n");
        }
    }
    
    /* Cleanup */
#if !defined(_WIN32)
    for (int i = 1; i < 4; i++) {
        if (benchmarks[i].handle) {
            dlclose(benchmarks[i].handle);
        }
    }
#endif
    
    return 0;
}
