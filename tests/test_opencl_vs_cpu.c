/*
 * test_opencl_vs_cpu.c: Test OpenCL multi-game implementation vs CPU
 *
 * This test validates that the OpenCL port produces the same results
 * as the CPU implementation for various poker games.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/core/eval.h>
#include <poker_eval/core/low_eval.h>
#include <poker_eval/core/low_qualifier.h>
#include <poker_eval/games/eval_omaha.h>
#include <poker_eval/games/eval_low.h>
#include <poker_eval/games/eval_low27.h>
#include <poker_eval/gpu/eval_gpu.h>

#ifdef HAVE_OPENCL
#include "opencl/eval_opencl.h"
#endif

/* Test configuration */
#define TEST_SKIP_CODE 77
#define NUM_TEST_HANDS 100
#define MAX_DIFF_THRESHOLD 0.001
#define OPENCL_VECTOR_FILE "tests/data/opencl_multi_game_vectors.json"

typedef struct {
    int tests_run;
    int tests_passed;
    int tests_failed;
} test_results_t;

static test_results_t g_results = {0, 0, 0};

#define TEST_ASSERT(condition, message) do { \
    g_results.tests_run++; \
    if (condition) { \
        g_results.tests_passed++; \
        printf("  ✓ PASS: %s\n", message); \
    } else { \
        g_results.tests_failed++; \
        printf("  ✗ FAIL: %s\n", message); \
    } \
} while(0)

typedef struct {
    unsigned int seed;
    int samples;
    int players;
} scenario_params_t;

static char* load_vector_blob(void) {
    static char* cached_blob = NULL;
    static int attempted = 0;
    if (attempted) {
        return cached_blob;
    }
    attempted = 1;

    const char* candidates[] = {
        OPENCL_VECTOR_FILE,
        "../" OPENCL_VECTOR_FILE,
        "../../" OPENCL_VECTOR_FILE,
        NULL
    };

    for (int i = 0; candidates[i] != NULL; i++) {
        FILE* f = fopen(candidates[i], "rb");
        if (!f)
            continue;

        if (fseek(f, 0, SEEK_END) != 0) {
            fclose(f);
            continue;
        }
        long size = ftell(f);
        if (size <= 0) {
            fclose(f);
            continue;
        }
        if (fseek(f, 0, SEEK_SET) != 0) {
            fclose(f);
            continue;
        }

        char* buffer = (char*)malloc((size_t)size + 1);
        if (!buffer) {
            fclose(f);
            continue;
        }
        size_t read = fread(buffer, 1, (size_t)size, f);
        fclose(f);
        if (read != (size_t)size) {
            free(buffer);
            continue;
        }
        buffer[size] = '\0';
        cached_blob = buffer;
        break;
    }

    return cached_blob;
}

static int json_extract_int(const char* block, const char* key, int* value) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* pos = strstr(block, pattern);
    if (!pos)
        return 0;

    pos = strchr(pos, ':');
    if (!pos)
        return 0;
    pos++;

    while (*pos && !isdigit((unsigned char)*pos) && *pos != '-') {
        pos++;
    }
    if (!*pos)
        return 0;

    *value = (int)strtol(pos, NULL, 10);
    return 1;
}

static int load_vector_config(const char* scenario, scenario_params_t* params) {
    char* blob = load_vector_blob();
    if (!blob)
        return -1;

    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", scenario);
    char* start = strstr(blob, pattern);
    if (!start)
        return -1;

    char* section_start = strchr(start, '{');
    if (!section_start)
        return -1;

    int depth = 0;
    char* cursor = section_start;
    do {
        if (*cursor == '{')
            depth++;
        else if (*cursor == '}')
            depth--;
        cursor++;
    } while (*cursor && depth > 0);

    if (depth != 0)
        return -1;

    char saved = *cursor;
    *cursor = '\0';

    int seed_val = 0;
    int samples_val = 0;
    int players_val = 0;
    int ok = json_extract_int(section_start, "seed", &seed_val) &&
             json_extract_int(section_start, "samples", &samples_val) &&
             json_extract_int(section_start, "players", &players_val);

    *cursor = saved;

    if (!ok)
        return -1;

    params->seed = (unsigned int)seed_val;
    params->samples = samples_val;
    params->players = players_val;
    return 0;
}

static scenario_params_t scenario_params_or_default(const char* scenario,
                                                    unsigned int default_seed,
                                                    int default_samples,
                                                    int default_players) {
    scenario_params_t params = {default_seed, default_samples, default_players};
    if (load_vector_config(scenario, &params) != 0) {
        printf("  INFO: Using fallback defaults for scenario '%s'\n", scenario);
    }
    return params;
}

static low_qualifier_t qualifier_from_rank(int qualifier_rank) {
    if (qualifier_rank >= 8)
        return LOW_QUALIFIER_8;
    if (qualifier_rank == 7)
        return LOW_QUALIFIER_7;
    return LOW_QUALIFIER_NONE;
}

static StdDeck_CardMask random_card_mask(int n_cards, StdDeck_CardMask* dead);

static void alloc_result_buffers(gpu_eval_result_hilo_t* result, int n_players) {
    result->hand_values_hi = (HandVal*)malloc((size_t)n_players * sizeof(HandVal));
    result->hand_values_lo = (HandVal*)malloc((size_t)n_players * sizeof(HandVal));
    result->lo_qualifies = (int*)malloc((size_t)n_players * sizeof(int));
    result->pot_fractions_hi = NULL;
    result->pot_fractions_lo = NULL;
    result->batch_size = 0;
}

static void free_result_buffers(gpu_eval_result_hilo_t* result) {
    free(result->hand_values_hi);
    free(result->hand_values_lo);
    free(result->lo_qualifies);
    result->hand_values_hi = NULL;
    result->hand_values_lo = NULL;
    result->lo_qualifies = NULL;
}

#ifdef HAVE_OPENCL
static void dump_card_mask(const char* label, StdDeck_CardMask mask) {
    printf("      %s: ", label);
    StdDeck_printMask(mask);
    printf("\n");
}

static void log_omaha_hilo_mismatch(
    int hand_idx,
    int player,
    StdDeck_CardMask board,
    StdDeck_CardMask hole,
    HandVal cpu_hi,
    HandVal gpu_hi,
    LowHandVal cpu_lo,
    LowHandVal gpu_lo,
    int cpu_qual,
    int gpu_qual
) {
    printf("    [DBG] Omaha8 mismatch hand %d player %d\n", hand_idx, player);
    dump_card_mask("Board", board);
    dump_card_mask("Hole ", hole);
    printf("      CPU HI=%08x GPU HI=%08x\n", cpu_hi, gpu_hi);
    printf("      CPU LO=%08x GPU LO=%08x\n", cpu_lo, gpu_lo);
    printf("      CPU qual=%d GPU qual=%d\n", cpu_qual, gpu_qual);
}

static void log_board_hilo_mismatch(
    int hand_idx,
    int player,
    StdDeck_CardMask board,
    StdDeck_CardMask hole,
    HandVal cpu_hi,
    HandVal gpu_hi,
    LowHandVal cpu_lo,
    LowHandVal gpu_lo,
    int cpu_qual,
    int gpu_qual
) {
    printf("    [DBG] Board-game hi/lo mismatch hand %d player %d\n", hand_idx, player);
    dump_card_mask("Board", board);
    dump_card_mask("Hole ", hole);
    printf("      CPU HI=%08x GPU HI=%08x\n", cpu_hi, gpu_hi);
    printf("      CPU LO=%08x GPU LO=%08x\n", cpu_lo, gpu_lo);
    printf("      CPU qual=%d GPU qual=%d\n", cpu_qual, gpu_qual);
}
#endif

#ifdef HAVE_OPENCL
static void run_omaha_hi_case(int num_hole_cards, const char* scenario_key,
                              unsigned int default_seed) {
    gpu_game_config_t config = gpu_game_config_omaha(num_hole_cards);
    scenario_params_t params = scenario_params_or_default(
        scenario_key, default_seed, 10, 2);
    srand(params.seed);

    printf("  -- Omaha-%d (hi) %d samples, %d players\n",
           num_hole_cards, params.samples, params.players);

    gpu_eval_context_t* ctx = opencl_gpu_eval_init_game(0, 100, 1, config);
    if (!ctx) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Could not init OpenCL for Omaha-%d", num_hole_cards);
        TEST_ASSERT(0, msg);
        return;
    }

    int matches = 0;
    int total = params.samples;
    StdDeck_CardMask* holes = (StdDeck_CardMask*)malloc((size_t)params.players * sizeof(StdDeck_CardMask));
    HandVal* cpu_vals = (HandVal*)malloc((size_t)params.players * sizeof(HandVal));
    gpu_eval_result_hilo_t result;
    alloc_result_buffers(&result, params.players);

    for (int i = 0; i < total; ++i) {
        StdDeck_CardMask dead;
        StdDeck_CardMask_RESET(dead);

        StdDeck_CardMask board = random_card_mask(config.num_board_cards, &dead);
        for (int p = 0; p < params.players; ++p) {
            holes[p] = random_card_mask(config.num_hole_cards, &dead);
            HandVal hi = 0;
            StdDeck_OmahaHi_EVAL(holes[p], board, &hi);
            cpu_vals[p] = hi;
        }

        StdDeck_CardMask boards_arr[1] = {board};
        int status = opencl_gpu_eval_batch_boards_hilo(
            ctx, boards_arr, holes, 1, params.players, &result);

        if (status == 0) {
            int iteration_match = 1;
            for (int p = 0; p < params.players; ++p) {
                if (cpu_vals[p] != result.hand_values_hi[p]) {
                    iteration_match = 0;
                    printf("    Omaha-%d hand %d player %d: CPU=%08x GPU=%08x\n",
                           num_hole_cards, i, p,
                           cpu_vals[p], result.hand_values_hi[p]);
                }
            }
            if (iteration_match)
                matches++;
        }
    }

    free(holes);
    free(cpu_vals);
    free_result_buffers(&result);
    opencl_gpu_eval_cleanup(ctx);

    char msg[160];
    snprintf(msg, sizeof(msg), "Omaha-%d CPU vs OpenCL: %d/%d hands match",
             num_hole_cards, matches, total);
    TEST_ASSERT(matches == total, msg);
}

static void run_omaha_hilo_case(int num_hole_cards, const char* scenario_key,
                                unsigned int default_seed) {
    gpu_game_config_t config = gpu_game_config_omaha8(num_hole_cards);
    scenario_params_t params = scenario_params_or_default(
        scenario_key, default_seed, 8, 2);
    srand(params.seed);

    low_qualifier_t qualifier = qualifier_from_rank(config.low_qualifier);

    printf("  -- Omaha8-%d (hi/lo) %d samples, %d players\n",
           num_hole_cards, params.samples, params.players);

    gpu_eval_context_t* ctx = opencl_gpu_eval_init_game(0, 100, 1, config);
    if (!ctx) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Could not init OpenCL for Omaha8-%d", num_hole_cards);
        TEST_ASSERT(0, msg);
        return;
    }

    int matches = 0;
    int total = params.samples;
    StdDeck_CardMask* holes = (StdDeck_CardMask*)malloc((size_t)params.players * sizeof(StdDeck_CardMask));
    HandVal* cpu_hi = (HandVal*)malloc((size_t)params.players * sizeof(HandVal));
    LowHandVal* cpu_lo = (LowHandVal*)malloc((size_t)params.players * sizeof(LowHandVal));
    int* cpu_lo_qual = (int*)malloc((size_t)params.players * sizeof(int));
    gpu_eval_result_hilo_t result;
    alloc_result_buffers(&result, params.players);

    for (int i = 0; i < total; ++i) {
        StdDeck_CardMask dead;
        StdDeck_CardMask_RESET(dead);

        StdDeck_CardMask board = random_card_mask(config.num_board_cards, &dead);
        for (int p = 0; p < params.players; ++p) {
            holes[p] = random_card_mask(config.num_hole_cards, &dead);
            HandVal hi = 0;
            LowHandVal lo = LowHandVal_NOTHING;
            StdDeck_OmahaHiLow8_EVAL(holes[p], board, &hi, &lo);
            cpu_hi[p] = hi;
            cpu_lo[p] = lo;
            cpu_lo_qual[p] = pe_low_qualify5(lo, qualifier) ? 1 : 0;
        }

        StdDeck_CardMask boards_arr[1] = {board};
        int status = opencl_gpu_eval_batch_boards_hilo(
            ctx, boards_arr, holes, 1, params.players, &result);

        if (status == 0) {
            int iteration_match = 1;
            for (int p = 0; p < params.players; ++p) {
                LowHandVal gpu_lo = (LowHandVal)result.hand_values_lo[p];
                if (cpu_hi[p] != result.hand_values_hi[p] ||
                    cpu_lo[p] != gpu_lo ||
                    cpu_lo_qual[p] != result.lo_qualifies[p]) {
                    iteration_match = 0;
                    printf("    Omaha8-%d hand %d player %d: HI CPU=%08x GPU=%08x | LO CPU=%08x GPU=%08x (qual CPU=%d GPU=%d)\n",
                           num_hole_cards, i, p,
                           cpu_hi[p], result.hand_values_hi[p],
                           cpu_lo[p], gpu_lo,
                           cpu_lo_qual[p], result.lo_qualifies[p]);
                    log_omaha_hilo_mismatch(i, p, board, holes[p],
                                            cpu_hi[p], result.hand_values_hi[p],
                                            cpu_lo[p], gpu_lo,
                                            cpu_lo_qual[p], result.lo_qualifies[p]);
                }
            }
            if (iteration_match)
                matches++;
        }
    }

    free(holes);
    free(cpu_hi);
    free(cpu_lo);
    free(cpu_lo_qual);
    free_result_buffers(&result);
    opencl_gpu_eval_cleanup(ctx);

    char msg[160];
    snprintf(msg, sizeof(msg), "Omaha8-%d CPU vs OpenCL: %d/%d hands match",
             num_hole_cards, matches, total);
    TEST_ASSERT(matches == total, msg);
}
#endif /* HAVE_OPENCL */

/* Random card generation */
static StdDeck_CardMask random_card_mask(int n_cards, StdDeck_CardMask* dead) {
    StdDeck_CardMask result;
    StdDeck_CardMask_RESET(result);

    int added = 0;
    int iterations = 0;
    while (added < n_cards) {
        iterations++;
        if (iterations > 10000) {
            printf("ERROR: Infinite loop in random_card_mask! added=%d, n_cards=%d\n", added, n_cards);
            exit(1);
        }
        int card = rand() % 52;
        StdDeck_CardMask card_mask;
        StdDeck_CardMask_RESET(card_mask);
        StdDeck_CardMask_SET(card_mask, card);

        /* Check if card is already used */
        if (!StdDeck_CardMask_ANY_SET(*dead, card_mask) &&
            !StdDeck_CardMask_ANY_SET(result, card_mask)) {
            StdDeck_CardMask_OR(result, result, card_mask);
            StdDeck_CardMask_OR(*dead, *dead, card_mask);
            added++;
        }
    }

    return result;
}

/* Function prototypes */
static void test_holdem_opencl_vs_cpu(void);
static void test_holdem8_opencl_vs_cpu(void);
static void test_omaha_opencl_vs_cpu(void);
static void test_omaha8_opencl_vs_cpu(void);
static void test_stud_opencl_vs_cpu(void);
static void test_stud8_opencl_vs_cpu(void);
static void test_razz_opencl_vs_cpu(void);
static void test_lowball27_opencl_vs_cpu(void);
static void test_opencl_performance(void);

/* Hold'em CPU vs OpenCL */
static void test_holdem_opencl_vs_cpu(void) {
    printf("\n=== Hold'em CPU vs OpenCL ===\n");

#ifndef HAVE_OPENCL
    printf("  SKIP: OpenCL not available\n");
    return;
#else
    /* Check if OpenCL is available */
    if (!opencl_gpu_is_available(1)) {
        printf("  SKIP: No OpenCL device found\n");
        return;
    }

    /* Create game config for Hold'em */
    gpu_game_config_t config = gpu_game_config_holdem();
    scenario_params_t params = scenario_params_or_default("holdem", 1337, 12, 2);
    srand(params.seed);

    /* Initialize OpenCL */
    gpu_eval_context_t* ctx = opencl_gpu_eval_init_game(0, 100, 1, config);
    if (!ctx) {
        printf("  FAIL: Could not initialize OpenCL context\n");
        g_results.tests_failed++;
        return;
    }

    int matches = 0;
    int total = params.samples;
    StdDeck_CardMask* holes = (StdDeck_CardMask*)malloc((size_t)params.players * sizeof(StdDeck_CardMask));
    HandVal* cpu_vals = (HandVal*)malloc((size_t)params.players * sizeof(HandVal));
    gpu_eval_result_hilo_t result;
    alloc_result_buffers(&result, params.players);

    for (int i = 0; i < total; i++) {
        StdDeck_CardMask dead;
        StdDeck_CardMask_RESET(dead);

        StdDeck_CardMask board;
        StdDeck_CardMask_RESET(board);
        if (config.num_board_cards > 0) {
            board = random_card_mask(config.num_board_cards, &dead);
        }

        for (int p = 0; p < params.players; ++p) {
            holes[p] = random_card_mask(config.num_hole_cards, &dead);
            StdDeck_CardMask hand;
            StdDeck_CardMask_OR(hand, board, holes[p]);
            cpu_vals[p] = StdDeck_StdRules_EVAL_N(hand, config.num_board_cards + config.num_hole_cards);
        }

        StdDeck_CardMask boards_arr[1] = {board};
        int status = opencl_gpu_eval_batch_boards_hilo(ctx, boards_arr, holes, 1, params.players, &result);

        if (status == 0) {
            int iteration_match = 1;
            for (int p = 0; p < params.players; ++p) {
                if (cpu_vals[p] != result.hand_values_hi[p]) {
                    iteration_match = 0;
                    printf("    Hand %d player %d: CPU=%08x GPU=%08x\n",
                           i, p, cpu_vals[p], result.hand_values_hi[p]);
                }
            }
            if (iteration_match) {
                matches++;
            }
        }
    }

    free(holes);
    free(cpu_vals);
    free_result_buffers(&result);

    /* Cleanup */
    opencl_gpu_eval_cleanup(ctx);

    char msg[128];
    snprintf(msg, sizeof(msg), "Hold'em CPU vs OpenCL: %d/%d hands match", matches, total);
    TEST_ASSERT(matches == total, msg);
#endif
}

/* Hold'em Hi/Lo CPU vs OpenCL */
static void test_holdem8_opencl_vs_cpu(void) {
    printf("\n=== Hold'em Hi/Lo CPU vs OpenCL ===\n");

#ifndef HAVE_OPENCL
    printf("  SKIP: OpenCL not available\n");
    return;
#else
    if (!opencl_gpu_is_available(1)) {
        printf("  SKIP: No OpenCL device found\n");
        return;
    }

    gpu_game_config_t config = gpu_game_config_holdem8();
    low_qualifier_t qualifier = qualifier_from_rank(config.low_qualifier);
    scenario_params_t params = scenario_params_or_default("holdem8", 2025, 8, 3);
    srand(params.seed);

    gpu_eval_context_t* ctx = opencl_gpu_eval_init_game(0, 100, 1, config);
    if (!ctx) {
        printf("  FAIL: Could not initialize OpenCL context\n");
        g_results.tests_failed++;
        return;
    }

    int matches = 0;
    int total = params.samples;
    StdDeck_CardMask* holes = (StdDeck_CardMask*)malloc((size_t)params.players * sizeof(StdDeck_CardMask));
    HandVal* cpu_hi = (HandVal*)malloc((size_t)params.players * sizeof(HandVal));
    LowHandVal* cpu_lo = (LowHandVal*)malloc((size_t)params.players * sizeof(LowHandVal));
    int* cpu_lo_qual = (int*)malloc((size_t)params.players * sizeof(int));
    gpu_eval_result_hilo_t result;
    alloc_result_buffers(&result, params.players);

    for (int i = 0; i < total; ++i) {
        StdDeck_CardMask dead;
        StdDeck_CardMask_RESET(dead);

        StdDeck_CardMask board;
        StdDeck_CardMask_RESET(board);
        if (config.num_board_cards > 0) {
            board = random_card_mask(config.num_board_cards, &dead);
        }

        for (int p = 0; p < params.players; ++p) {
            holes[p] = random_card_mask(config.num_hole_cards, &dead);
            StdDeck_CardMask hand;
            StdDeck_CardMask_OR(hand, board, holes[p]);
            cpu_hi[p] = StdDeck_StdRules_EVAL_N(hand, config.num_board_cards + config.num_hole_cards);
            LowHandVal lo = pe_eval_low_a5(hand);
            cpu_lo[p] = lo;
            cpu_lo_qual[p] = pe_low_qualify5(lo, qualifier) ? 1 : 0;
        }

        StdDeck_CardMask boards_arr[1] = {board};
        int status = opencl_gpu_eval_batch_boards_hilo(ctx, boards_arr, holes, 1, params.players, &result);

        if (status == 0) {
            int iteration_match = 1;
            for (int p = 0; p < params.players; ++p) {
                LowHandVal gpu_lo = (LowHandVal)result.hand_values_lo[p];
                if (cpu_hi[p] != result.hand_values_hi[p] ||
                    cpu_lo[p] != gpu_lo ||
                    cpu_lo_qual[p] != result.lo_qualifies[p]) {
                    iteration_match = 0;
                    printf("    Hold'em8 hand %d player %d: HI CPU=%08x GPU=%08x | LO CPU=%08x GPU=%08x (qual CPU=%d GPU=%d)\n",
                           i, p, cpu_hi[p], result.hand_values_hi[p],
                           cpu_lo[p], gpu_lo, cpu_lo_qual[p], result.lo_qualifies[p]);
#ifdef HAVE_OPENCL
                    log_board_hilo_mismatch(i, p, board, holes[p],
                                            cpu_hi[p], result.hand_values_hi[p],
                                            cpu_lo[p], gpu_lo,
                                            cpu_lo_qual[p], result.lo_qualifies[p]);
#endif
                }
            }
            if (iteration_match) {
                matches++;
            }
        }
    }

    free(holes);
    free(cpu_hi);
    free(cpu_lo);
    free(cpu_lo_qual);
    free_result_buffers(&result);
    opencl_gpu_eval_cleanup(ctx);

    char msg[160];
    snprintf(msg, sizeof(msg), "Hold'em Hi/Lo CPU vs OpenCL: %d/%d hands match", matches, total);
    TEST_ASSERT(matches == total, msg);
#endif
}

/* Omaha CPU vs OpenCL */
static void test_omaha_opencl_vs_cpu(void) {
    printf("\n=== Omaha CPU vs OpenCL ===\n");

#ifndef HAVE_OPENCL
    printf("  SKIP: OpenCL not available\n");
    return;
#else
    if (!opencl_gpu_is_available(1)) {
        printf("  SKIP: No OpenCL device found\n");
        return;
    }
    run_omaha_hi_case(4, "omaha4", 4242);
    run_omaha_hi_case(5, "omaha5", 4343);
    run_omaha_hi_case(6, "omaha6", 4444);
#endif
}

/* Omaha Hi/Lo CPU vs OpenCL */
static void test_omaha8_opencl_vs_cpu(void) {
    printf("\n=== Omaha Hi/Lo CPU vs OpenCL ===\n");

#ifndef HAVE_OPENCL
    printf("  SKIP: OpenCL not available\n");
    return;
#else
    if (!opencl_gpu_is_available(1)) {
        printf("  SKIP: No OpenCL device found\n");
        return;
    }
    run_omaha_hilo_case(4, "omaha8_4", 5150);
    run_omaha_hilo_case(5, "omaha8_5", 5252);
    run_omaha_hilo_case(6, "omaha8_6", 5353);
#endif
}

/* Stud CPU vs OpenCL */
static void test_stud_opencl_vs_cpu(void) {
    printf("\n=== Stud CPU vs OpenCL ===\n");

#ifndef HAVE_OPENCL
    printf("  SKIP: OpenCL not available\n");
    return;
#else
    if (!opencl_gpu_is_available(1)) {
        printf("  SKIP: No OpenCL device found\n");
        return;
    }

    /* Create game config for Stud */
    gpu_game_config_t config = gpu_game_config_stud();
    scenario_params_t params = scenario_params_or_default("stud", 6060, 8, 2);
    srand(params.seed);

    /* Initialize OpenCL */
    gpu_eval_context_t* ctx = opencl_gpu_eval_init_game(0, 100, 1, config);
    if (!ctx) {
        printf("  FAIL: Could not initialize OpenCL context\n");
        g_results.tests_failed++;
        return;
    }

    int matches = 0;
    int total = params.samples;
    StdDeck_CardMask* holes = (StdDeck_CardMask*)malloc((size_t)params.players * sizeof(StdDeck_CardMask));
    HandVal* cpu_vals = (HandVal*)malloc((size_t)params.players * sizeof(HandVal));
    gpu_eval_result_hilo_t result;
    alloc_result_buffers(&result, params.players);

    for (int i = 0; i < total; i++) {
        StdDeck_CardMask dead;
        StdDeck_CardMask_RESET(dead);

        for (int p = 0; p < params.players; ++p) {
            holes[p] = random_card_mask(config.num_hole_cards, &dead);
            cpu_vals[p] = StdDeck_StdRules_EVAL_N(holes[p], config.num_hole_cards);
        }

        StdDeck_CardMask boards_arr[1];
        StdDeck_CardMask_RESET(boards_arr[0]);

        int status = opencl_gpu_eval_batch_boards_hilo(ctx, boards_arr, holes, 1, params.players, &result);

        if (status == 0) {
            int iteration_match = 1;
            for (int p = 0; p < params.players; ++p) {
                if (cpu_vals[p] != result.hand_values_hi[p]) {
                    iteration_match = 0;
                    printf("    Stud hand %d player %d: CPU=%08x GPU=%08x\n",
                           i, p, cpu_vals[p], result.hand_values_hi[p]);
                }
            }
            if (iteration_match) {
                matches++;
            }
        }
    }

    free(holes);
    free(cpu_vals);
    free_result_buffers(&result);

    /* Cleanup */
    opencl_gpu_eval_cleanup(ctx);

    char msg[128];
    snprintf(msg, sizeof(msg), "Stud CPU vs OpenCL: %d/%d hands match", matches, total);
    TEST_ASSERT(matches == total, msg);
#endif
}

/* Stud Hi/Lo CPU vs OpenCL */
static void test_stud8_opencl_vs_cpu(void) {
    printf("\n=== Stud Hi/Lo CPU vs OpenCL ===\n");

#ifndef HAVE_OPENCL
    printf("  SKIP: OpenCL not available\n");
    return;
#else
    if (!opencl_gpu_is_available(1)) {
        printf("  SKIP: No OpenCL device found\n");
        return;
    }

    gpu_game_config_t config = gpu_game_config_stud8();
    low_qualifier_t qualifier = qualifier_from_rank(config.low_qualifier);
    scenario_params_t params = scenario_params_or_default("stud8", 7777, 6, 2);
    srand(params.seed);

    gpu_eval_context_t* ctx = opencl_gpu_eval_init_game(0, 100, 1, config);
    if (!ctx) {
        printf("  FAIL: Could not initialize OpenCL context\n");
        g_results.tests_failed++;
        return;
    }

    int matches = 0;
    int total = params.samples;
    StdDeck_CardMask* holes = (StdDeck_CardMask*)malloc((size_t)params.players * sizeof(StdDeck_CardMask));
    HandVal* cpu_hi = (HandVal*)malloc((size_t)params.players * sizeof(HandVal));
    LowHandVal* cpu_lo = (LowHandVal*)malloc((size_t)params.players * sizeof(LowHandVal));
    int* cpu_lo_qual = (int*)malloc((size_t)params.players * sizeof(int));
    gpu_eval_result_hilo_t result;
    alloc_result_buffers(&result, params.players);

    for (int i = 0; i < total; ++i) {
        StdDeck_CardMask dead;
        StdDeck_CardMask_RESET(dead);

        for (int p = 0; p < params.players; ++p) {
            holes[p] = random_card_mask(config.num_hole_cards, &dead);
            cpu_hi[p] = StdDeck_StdRules_EVAL_N(holes[p], config.num_hole_cards);
            LowHandVal lo = pe_eval_low_a5(holes[p]);
            cpu_lo[p] = lo;
            cpu_lo_qual[p] = pe_low_qualify5(lo, qualifier) ? 1 : 0;
        }

        StdDeck_CardMask boards_arr[1];
        StdDeck_CardMask_RESET(boards_arr[0]);
        int status = opencl_gpu_eval_batch_boards_hilo(ctx, boards_arr, holes, 1, params.players, &result);

        if (status == 0) {
            int iteration_match = 1;
            for (int p = 0; p < params.players; ++p) {
                LowHandVal gpu_lo = (LowHandVal)result.hand_values_lo[p];
                if (cpu_hi[p] != result.hand_values_hi[p] ||
                    cpu_lo[p] != gpu_lo ||
                    cpu_lo_qual[p] != result.lo_qualifies[p]) {
                    iteration_match = 0;
                    printf("    Stud8 hand %d player %d: HI CPU=%08x GPU=%08x | LO CPU=%08x GPU=%08x (qual CPU=%d GPU=%d)\n",
                           i, p, cpu_hi[p], result.hand_values_hi[p],
                           cpu_lo[p], gpu_lo, cpu_lo_qual[p], result.lo_qualifies[p]);
#ifdef HAVE_OPENCL
                    StdDeck_CardMask empty_board;
                    StdDeck_CardMask_RESET(empty_board);
                    log_board_hilo_mismatch(i, p, empty_board, holes[p],
                                            cpu_hi[p], result.hand_values_hi[p],
                                            cpu_lo[p], gpu_lo,
                                            cpu_lo_qual[p], result.lo_qualifies[p]);
#endif
                }
            }
            if (iteration_match) {
                matches++;
            }
        }
    }

    free(holes);
    free(cpu_hi);
    free(cpu_lo);
    free(cpu_lo_qual);
    free_result_buffers(&result);
    opencl_gpu_eval_cleanup(ctx);

    char msg[160];
    snprintf(msg, sizeof(msg), "Stud Hi/Lo CPU vs OpenCL: %d/%d hands match", matches, total);
    TEST_ASSERT(matches == total, msg);
#endif
}

/* Razz CPU vs OpenCL */
static void test_razz_opencl_vs_cpu(void) {
    printf("\n=== Razz (A-5 Lowball) CPU vs OpenCL ===\n");

#ifndef HAVE_OPENCL
    printf("  SKIP: OpenCL not available\n");
    return;
#else
    if (!opencl_gpu_is_available(1)) {
        printf("  SKIP: No OpenCL device found\n");
        return;
    }

    gpu_game_config_t config = gpu_game_config_razz();
    scenario_params_t params = scenario_params_or_default("razz", 8800, 8, 2);
    srand(params.seed);

    gpu_eval_context_t* ctx = opencl_gpu_eval_init_game(0, 100, 1, config);
    if (!ctx) {
        printf("  FAIL: Could not initialize OpenCL context\n");
        g_results.tests_failed++;
        return;
    }

    int matches = 0;
    int total = params.samples;
    StdDeck_CardMask* holes = (StdDeck_CardMask*)malloc((size_t)params.players * sizeof(StdDeck_CardMask));
    LowHandVal* cpu_vals = (LowHandVal*)malloc((size_t)params.players * sizeof(LowHandVal));
    gpu_eval_result_hilo_t result;
    alloc_result_buffers(&result, params.players);

    for (int i = 0; i < total; ++i) {
        StdDeck_CardMask dead;
        StdDeck_CardMask_RESET(dead);

        for (int p = 0; p < params.players; ++p) {
            holes[p] = random_card_mask(config.num_hole_cards, &dead);
            cpu_vals[p] = pe_eval_low_a5(holes[p]);
        }

        StdDeck_CardMask boards_arr[1];
        StdDeck_CardMask_RESET(boards_arr[0]);
        int status = opencl_gpu_eval_batch_boards_hilo(ctx, boards_arr, holes, 1, params.players, &result);

        if (status == 0) {
            int iteration_match = 1;
            for (int p = 0; p < params.players; ++p) {
                LowHandVal gpu_val = (LowHandVal)result.hand_values_lo[p];
                if (cpu_vals[p] != gpu_val || result.lo_qualifies[p] != 1) {
                    iteration_match = 0;
                    printf("    Razz hand %d player %d: CPU=%08x GPU=%08x (qual GPU=%d)\n",
                           i, p, cpu_vals[p], gpu_val, result.lo_qualifies[p]);
                }
            }
            if (iteration_match) {
                matches++;
            }
        }
    }

    free(holes);
    free(cpu_vals);
    free_result_buffers(&result);
    opencl_gpu_eval_cleanup(ctx);

    char msg[128];
    snprintf(msg, sizeof(msg), "Razz CPU vs OpenCL: %d/%d hands match", matches, total);
    TEST_ASSERT(matches == total, msg);
#endif
}

/* 2-7 Lowball CPU vs OpenCL */
static void test_lowball27_opencl_vs_cpu(void) {
    printf("\n=== 2-7 Lowball CPU vs OpenCL ===\n");

#ifndef HAVE_OPENCL
    printf("  SKIP: OpenCL not available\n");
    return;
#else
    if (!opencl_gpu_is_available(1)) {
        printf("  SKIP: No OpenCL device found\n");
        return;
    }

    /* Create game config for 2-7 Lowball */
    gpu_game_config_t config = gpu_game_config_lowball27();
    scenario_params_t params = scenario_params_or_default("lowball27", 9900, 8, 2);
    srand(params.seed);

    /* Initialize OpenCL */
    gpu_eval_context_t* ctx = opencl_gpu_eval_init_game(0, 100, 1, config);
    if (!ctx) {
        printf("  FAIL: Could not initialize OpenCL context\n");
        g_results.tests_failed++;
        return;
    }

    int matches = 0;
    int total = params.samples;
    StdDeck_CardMask* holes = (StdDeck_CardMask*)malloc((size_t)params.players * sizeof(StdDeck_CardMask));
    HandVal* cpu_vals = (HandVal*)malloc((size_t)params.players * sizeof(HandVal));
    gpu_eval_result_hilo_t result;
    alloc_result_buffers(&result, params.players);

    for (int i = 0; i < total; i++) {
        StdDeck_CardMask dead;
        StdDeck_CardMask_RESET(dead);

        for (int p = 0; p < params.players; ++p) {
            holes[p] = random_card_mask(config.num_hole_cards, &dead);
            cpu_vals[p] = StdDeck_Lowball27_EVAL_N(holes[p], config.num_hole_cards);
        }

        StdDeck_CardMask boards_arr[1];
        StdDeck_CardMask_RESET(boards_arr[0]);

        int status = opencl_gpu_eval_batch_boards_hilo(ctx, boards_arr, holes, 1, params.players, &result);

        if (status == 0) {
            int iteration_match = 1;
            for (int p = 0; p < params.players; ++p) {
                HandVal gpu_val = result.hand_values_lo[p];
                if (cpu_vals[p] != gpu_val) {
                    iteration_match = 0;
                    printf("    2-7 hand %d player %d: CPU=%08x GPU=%08x\n",
                           i, p, cpu_vals[p], gpu_val);
                }
            }
            if (iteration_match) {
                matches++;
            }
        }
    }

    free(holes);
    free(cpu_vals);
    free_result_buffers(&result);
    opencl_gpu_eval_cleanup(ctx);

    char msg[128];
    snprintf(msg, sizeof(msg), "2-7 Lowball CPU vs OpenCL: %d/%d hands match", matches, total);
    TEST_ASSERT(matches == total, msg);
#endif
}

/* Performance comparison */
static void test_opencl_performance(void) {
    printf("\n=== OpenCL Performance ===\n");

#ifndef HAVE_OPENCL
    printf("  SKIP: OpenCL not available\n");
    return;
#else
    if (!opencl_gpu_is_available(1)) {
        printf("  SKIP: No OpenCL device found\n");
        return;
    }

    /* Create game config for Hold'em */
    gpu_game_config_t config = gpu_game_config_holdem();

    /* Initialize OpenCL */
    gpu_eval_context_t* ctx = opencl_gpu_eval_init_game(0, 1000, 1, config);
    if (!ctx) {
        printf("  FAIL: Could not initialize OpenCL context\n");
        g_results.tests_failed++;
        return;
    }

    /* Prepare test data */
    int n_boards = 100;
    int n_players = 6;
    int total_hands = n_boards * n_players;

    StdDeck_CardMask* boards = (StdDeck_CardMask*)malloc(n_boards * sizeof(StdDeck_CardMask));
    StdDeck_CardMask* holes = (StdDeck_CardMask*)malloc(total_hands * sizeof(StdDeck_CardMask));

    /* Generate random hands */
    for (int i = 0; i < n_boards; i++) {
        StdDeck_CardMask dead;
        StdDeck_CardMask_RESET(dead);

        boards[i] = random_card_mask(5, &dead);

        for (int p = 0; p < n_players; p++) {
            holes[i * n_players + p] = random_card_mask(2, &dead);
        }
    }

    /* CPU benchmark */
    clock_t cpu_start = clock();
    for (int i = 0; i < n_boards; i++) {
        for (int p = 0; p < n_players; p++) {
            StdDeck_CardMask hand;
            StdDeck_CardMask_OR(hand, boards[i], holes[i * n_players + p]);
            HandVal val = StdDeck_StdRules_EVAL_N(hand, 7);
            (void)val; /* Suppress unused warning */
        }
    }
    clock_t cpu_end = clock();
    double cpu_time = ((double)(cpu_end - cpu_start)) / CLOCKS_PER_SEC * 1000.0;

    /* OpenCL benchmark */
    gpu_eval_result_hilo_t result;
    result.hand_values_hi = (HandVal*)malloc(total_hands * sizeof(HandVal));
    result.hand_values_lo = (HandVal*)malloc(total_hands * sizeof(HandVal));
    result.lo_qualifies = (int*)malloc(total_hands * sizeof(int));

    clock_t gpu_start = clock();
    int status = opencl_gpu_eval_batch_boards_hilo(ctx, boards, holes, n_boards, n_players, &result);
    clock_t gpu_end = clock();
    double gpu_time = ((double)(gpu_end - gpu_start)) / CLOCKS_PER_SEC * 1000.0;

    free(result.hand_values_hi);
    free(result.hand_values_lo);
    free(result.lo_qualifies);
    free(boards);
    free(holes);

    /* Cleanup */
    opencl_gpu_eval_cleanup(ctx);

    printf("  CPU Time:    %.2f ms (%d hands)\n", cpu_time, total_hands);
    printf("  OpenCL Time: %.2f ms (%d hands)\n", gpu_time, total_hands);
    printf("  Speedup:     %.2fx\n", cpu_time / gpu_time);

    char msg[128];
    snprintf(msg, sizeof(msg), "OpenCL completed successfully");
    TEST_ASSERT(status == 0, msg);
#endif
}

int main(void) {
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║         OpenCL Multi-Game vs CPU Validation Test              ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n");

#ifndef HAVE_OPENCL
    printf("SKIP: OpenCL not available\n");
    return TEST_SKIP_CODE;
#else
    if (!opencl_gpu_is_available(1)) {
        printf("SKIP: No OpenCL device found\n");
        return TEST_SKIP_CODE;
    }
#endif

    /* Seed random number generator */
    srand((unsigned int)time(NULL));

    /* Run tests */
    test_holdem_opencl_vs_cpu();
    test_holdem8_opencl_vs_cpu();
    test_omaha_opencl_vs_cpu();
    test_omaha8_opencl_vs_cpu();
    test_stud_opencl_vs_cpu();
    test_stud8_opencl_vs_cpu();
    test_razz_opencl_vs_cpu();
    test_lowball27_opencl_vs_cpu();
    test_opencl_performance();

    /* Print summary */
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║                        Test Summary                            ║\n");
    printf("╠════════════════════════════════════════════════════════════════╣\n");
    printf("║  Total Tests:    %3d                                           ║\n", g_results.tests_run);
    printf("║  Passed:         %3d                                           ║\n", g_results.tests_passed);
    printf("║  Failed:         %3d                                           ║\n", g_results.tests_failed);
    printf("╚════════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    return (g_results.tests_failed == 0) ? 0 : 1;
}
