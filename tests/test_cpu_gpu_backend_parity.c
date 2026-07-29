/*
 * test_cpu_gpu_backend_parity.c
 *
 * Parity harness comparing CPU hand evaluations with CUDA and OpenCL
 * multi-game backends. This continues the work started for Hold'em by
 * adding scenario-driven sampling plus Hold'em Hi/Lo coverage so both
 * high-only and split-pot flows are exercised end-to-end.
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
#include <poker_eval/gpu/eval_gpu.h>
#include <poker_eval/games/eval_omaha.h>

#ifdef HAVE_OPENCL
#include "opencl/eval_opencl.h"
#endif

#define PARITY_VECTOR_FILE "tests/data/opencl_multi_game_vectors.json"
#define PARITY_DEFAULT_BATCH_SIZE 128
#define PARITY_BACKEND_CAPACITY 2

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
        printf("  ✓ %s\n", message); \
    } else { \
        g_results.tests_failed++; \
        printf("  ✗ %s\n", message); \
    } \
} while (0)

typedef struct {
    unsigned int seed;
    int samples;
    int players;
} scenario_params_t;

typedef void (*cpu_eval_hi_cb)(
    const gpu_game_config_t* config,
    StdDeck_CardMask board,
    StdDeck_CardMask hole,
    HandVal* hi_out);

typedef void (*cpu_eval_hilo_cb)(
    const gpu_game_config_t* config,
    StdDeck_CardMask board,
    StdDeck_CardMask hole,
    HandVal* hi_out,
    LowHandVal* lo_out);

static char* load_vector_blob(void) {
    static char* cached_blob = NULL;
    static int attempted = 0;
    if (attempted) {
        return cached_blob;
    }
    attempted = 1;

    const char* candidates[] = {
        PARITY_VECTOR_FILE,
        "../" PARITY_VECTOR_FILE,
        "../../" PARITY_VECTOR_FILE,
        NULL
    };

    for (int i = 0; candidates[i] != NULL; ++i) {
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

typedef int (*backend_eval_fn)(
    gpu_eval_context_t*,
    StdDeck_CardMask*,
    StdDeck_CardMask*,
    int,
    int,
    gpu_eval_result_hilo_t*);

typedef void (*backend_cleanup_fn)(gpu_eval_context_t*);

typedef struct {
    const char* name;
    int backend_type;
    int available;
    gpu_eval_context_t* ctx;
    backend_eval_fn eval_fn;
    backend_cleanup_fn cleanup_fn;
    gpu_eval_result_hilo_t result;
    int matches;
    int samples;
} backend_runner_t;

static void alloc_result_buffers(gpu_eval_result_hilo_t* result, int max_players) {
    memset(result, 0, sizeof(*result));
    result->hand_values_hi = (HandVal*)malloc((size_t)max_players * sizeof(HandVal));
    result->hand_values_lo = (HandVal*)malloc((size_t)max_players * sizeof(HandVal));
    result->lo_qualifies = (int*)malloc((size_t)max_players * sizeof(int));
}

static void free_result_buffers(gpu_eval_result_hilo_t* result) {
    free(result->hand_values_hi);
    free(result->hand_values_lo);
    free(result->lo_qualifies);
    memset(result, 0, sizeof(*result));
}

static StdDeck_CardMask random_card_mask(int n_cards, StdDeck_CardMask* dead) {
    StdDeck_CardMask result;
    StdDeck_CardMask_RESET(result);

    int added = 0;
    int guard = 0;
    while (added < n_cards) {
        if (++guard > 10000) {
            fprintf(stderr, "Random card generation guard triggered\n");
            exit(EXIT_FAILURE);
        }
        int card = rand() % 52;
        StdDeck_CardMask card_mask;
        StdDeck_CardMask_RESET(card_mask);
        StdDeck_CardMask_SET(card_mask, card);

        if (!StdDeck_CardMask_ANY_SET(*dead, card_mask) &&
            !StdDeck_CardMask_ANY_SET(result, card_mask)) {
            StdDeck_CardMask_OR(result, result, card_mask);
            StdDeck_CardMask_OR(*dead, *dead, card_mask);
            added++;
        }
    }

    return result;
}

/* CPU evaluation helpers */
static void cpu_eval_holdem_hi(
    const gpu_game_config_t* config,
    StdDeck_CardMask board,
    StdDeck_CardMask hole,
    HandVal* hi_out
) {
    StdDeck_CardMask combined;
    StdDeck_CardMask_OR(combined, board, hole);
    *hi_out = StdDeck_StdRules_EVAL_N(
        combined,
        config->num_board_cards + config->num_hole_cards);
}

static void cpu_eval_holdem_hilo(
    const gpu_game_config_t* config,
    StdDeck_CardMask board,
    StdDeck_CardMask hole,
    HandVal* hi_out,
    LowHandVal* lo_out
) {
    StdDeck_CardMask hand;
    StdDeck_CardMask_OR(hand, board, hole);
    *hi_out = StdDeck_StdRules_EVAL_N(
        hand,
        config->num_board_cards + config->num_hole_cards);
    *lo_out = pe_eval_low_a5(hand);
}

static void cpu_eval_omaha_hi(
    const gpu_game_config_t* config,
    StdDeck_CardMask board,
    StdDeck_CardMask hole,
    HandVal* hi_out
) {
    (void)config;
    HandVal hi = 0;
    StdDeck_OmahaHi_EVAL(hole, board, &hi);
    *hi_out = hi;
}

static void cpu_eval_omaha_hilo(
    const gpu_game_config_t* config,
    StdDeck_CardMask board,
    StdDeck_CardMask hole,
    HandVal* hi_out,
    LowHandVal* lo_out
) {
    (void)config;
    HandVal hi = 0;
    LowHandVal lo = 0;
    StdDeck_OmahaHiLow8_EVAL(hole, board, &hi, &lo);
    *hi_out = hi;
    *lo_out = lo;
}

static int setup_backends(gpu_game_config_t config,
                          int max_players,
                          backend_runner_t* backends,
                          int backend_capacity) {
    int idx = 0;
#ifdef HAVE_CUDA
    if (idx < backend_capacity) {
        backend_runner_t* cuda_backend = &backends[idx++];
        memset(cuda_backend, 0, sizeof(*cuda_backend));
        cuda_backend->name = "CUDA";
        cuda_backend->backend_type = 0;
        cuda_backend->eval_fn = gpu_eval_batch_boards_hilo;
        cuda_backend->cleanup_fn = gpu_eval_cleanup;
        cuda_backend->available = gpu_is_available(0);
        if (cuda_backend->available) {
            cuda_backend->ctx = gpu_eval_init_game(
                0, PARITY_DEFAULT_BATCH_SIZE, 0, config);
            if (!cuda_backend->ctx) {
                printf("  WARN: Failed to initialize CUDA backend context\n");
                cuda_backend->available = 0;
            } else {
                alloc_result_buffers(&cuda_backend->result, max_players);
            }
        } else {
            printf("  INFO: CUDA backend not available\n");
        }
    }
#endif

#ifdef HAVE_OPENCL
    if (idx < backend_capacity) {
        backend_runner_t* opencl_backend = &backends[idx++];
        memset(opencl_backend, 0, sizeof(*opencl_backend));
        opencl_backend->name = "OpenCL";
        opencl_backend->backend_type = 1;
        opencl_backend->eval_fn = opencl_gpu_eval_batch_boards_hilo;
        opencl_backend->cleanup_fn = opencl_gpu_eval_cleanup;
        opencl_backend->available = opencl_gpu_is_available(1);
        if (opencl_backend->available) {
            opencl_backend->ctx = opencl_gpu_eval_init_game(
                0, PARITY_DEFAULT_BATCH_SIZE, 1, config);
            if (!opencl_backend->ctx) {
                printf("  WARN: Failed to initialize OpenCL backend context\n");
                opencl_backend->available = 0;
            } else {
                alloc_result_buffers(&opencl_backend->result, max_players);
            }
        } else {
            printf("  INFO: OpenCL backend not available\n");
        }
    }
#endif

    return idx;
}

static void teardown_backends(backend_runner_t* backends, int backend_count) {
    for (int i = 0; i < backend_count; ++i) {
        if (!backends[i].available)
            continue;
        free_result_buffers(&backends[i].result);
        if (backends[i].cleanup_fn && backends[i].ctx) {
            backends[i].cleanup_fn(backends[i].ctx);
        }
        backends[i].ctx = NULL;
    }
}

static int available_backend_count(backend_runner_t* backends, int backend_count) {
    int count = 0;
    for (int i = 0; i < backend_count; ++i) {
        if (backends[i].available)
            count++;
    }
    return count;
}

static void report_backend_results(backend_runner_t* backends, int backend_count) {
    for (int i = 0; i < backend_count; ++i) {
        backend_runner_t* backend = &backends[i];
        if (!backend->name)
            continue;

        if (!backend->available) {
            char skipped_msg[128];
            snprintf(skipped_msg, sizeof(skipped_msg),
                     "%s backend skipped (not available)", backend->name);
            TEST_ASSERT(1, skipped_msg);
            continue;
        }

        char result_msg[160];
        snprintf(result_msg, sizeof(result_msg),
                 "%s backend parity %d/%d samples",
                 backend->name,
                 backend->matches,
                 backend->samples);
        TEST_ASSERT(backend->matches == backend->samples, result_msg);
    }
}

static void run_board_game_hi(
    const char* label,
    const char* scenario_key,
    gpu_game_config_t config,
    unsigned int default_seed,
    int default_samples,
    int default_players,
    cpu_eval_hi_cb cpu_eval
) {
    printf("\n=== %s ===\n", label);

    scenario_params_t params = scenario_params_or_default(
        scenario_key, default_seed, default_samples, default_players);
    srand(params.seed);

    backend_runner_t backends[PARITY_BACKEND_CAPACITY];
    memset(backends, 0, sizeof(backends));
    int backend_count = setup_backends(config, params.players, backends,
                                       PARITY_BACKEND_CAPACITY);

    if (backend_count == 0 || available_backend_count(backends, backend_count) == 0) {
        char skip_msg[160];
        snprintf(skip_msg, sizeof(skip_msg),
                 "%s skipped (no GPU backend)", label);
        printf("  SKIP: No GPU backend available for this test\n");
        TEST_ASSERT(1, skip_msg);
        teardown_backends(backends, backend_count);
        return;
    }

    printf("  Scenario: %d samples, %d players (seed %u)\n",
           params.samples, params.players, params.seed);

    StdDeck_CardMask* holes = (StdDeck_CardMask*)malloc(
        (size_t)params.players * sizeof(StdDeck_CardMask));
    HandVal* cpu_vals = (HandVal*)malloc(
        (size_t)params.players * sizeof(HandVal));
    StdDeck_CardMask boards[1];

    for (int sample = 0; sample < params.samples; ++sample) {
        StdDeck_CardMask dead;
        StdDeck_CardMask_RESET(dead);

        boards[0] = random_card_mask(config.num_board_cards, &dead);

        for (int p = 0; p < params.players; ++p) {
            holes[p] = random_card_mask(config.num_hole_cards, &dead);
            cpu_eval(&config, boards[0], holes[p], &cpu_vals[p]);
        }

        for (int b = 0; b < backend_count; ++b) {
            backend_runner_t* backend = &backends[b];
            if (!backend->available)
                continue;

            backend->samples++;
            int status = backend->eval_fn(
                backend->ctx,
                boards,
                holes,
                1,
                params.players,
                &backend->result);

            if (status != 0) {
                printf("  ERROR: %s evaluation failed for sample %d (status=%d)\n",
                       backend->name, sample, status);
                continue;
            }

            int iteration_match = 1;
            for (int p = 0; p < params.players; ++p) {
                if (cpu_vals[p] != backend->result.hand_values_hi[p]) {
                    iteration_match = 0;
                    printf("    Mismatch [%s] sample %d player %d: CPU=%08x GPU=%08x\n",
                           backend->name,
                           sample,
                           p,
                           cpu_vals[p],
                           backend->result.hand_values_hi[p]);
                }
            }

            if (iteration_match) {
                backend->matches++;
            }
        }
    }

    report_backend_results(backends, backend_count);

    free(holes);
    free(cpu_vals);
    teardown_backends(backends, backend_count);
}

static void run_board_game_hilo(
    const char* label,
    const char* scenario_key,
    gpu_game_config_t config,
    unsigned int default_seed,
    int default_samples,
    int default_players,
    low_qualifier_t qualifier,
    cpu_eval_hilo_cb cpu_eval
) {
    printf("\n=== %s ===\n", label);

    scenario_params_t params = scenario_params_or_default(
        scenario_key, default_seed, default_samples, default_players);
    srand(params.seed);

    backend_runner_t backends[PARITY_BACKEND_CAPACITY];
    memset(backends, 0, sizeof(backends));
    int backend_count = setup_backends(config, params.players, backends,
                                       PARITY_BACKEND_CAPACITY);

    if (backend_count == 0 || available_backend_count(backends, backend_count) == 0) {
        char skip_msg[160];
        snprintf(skip_msg, sizeof(skip_msg),
                 "%s skipped (no GPU backend)", label);
        printf("  SKIP: No GPU backend available for this test\n");
        TEST_ASSERT(1, skip_msg);
        teardown_backends(backends, backend_count);
        return;
    }

    printf("  Scenario: %d samples, %d players (seed %u)\n",
           params.samples, params.players, params.seed);

    StdDeck_CardMask* holes = (StdDeck_CardMask*)malloc(
        (size_t)params.players * sizeof(StdDeck_CardMask));
    HandVal* cpu_hi = (HandVal*)malloc(
        (size_t)params.players * sizeof(HandVal));
    LowHandVal* cpu_lo = (LowHandVal*)malloc(
        (size_t)params.players * sizeof(LowHandVal));
    int* cpu_lo_qual = (int*)malloc((size_t)params.players * sizeof(int));

    StdDeck_CardMask boards[1];

    for (int sample = 0; sample < params.samples; ++sample) {
        StdDeck_CardMask dead;
        StdDeck_CardMask_RESET(dead);

        boards[0] = random_card_mask(config.num_board_cards, &dead);

        for (int p = 0; p < params.players; ++p) {
            holes[p] = random_card_mask(config.num_hole_cards, &dead);
            cpu_eval(&config, boards[0], holes[p], &cpu_hi[p], &cpu_lo[p]);
            if (qualifier == LOW_QUALIFIER_NONE) {
                cpu_lo_qual[p] = 1;
            } else {
                cpu_lo_qual[p] = pe_low_qualify5(cpu_lo[p], qualifier) ? 1 : 0;
            }
        }

        for (int b = 0; b < backend_count; ++b) {
            backend_runner_t* backend = &backends[b];
            if (!backend->available)
                continue;

            backend->samples++;
            int status = backend->eval_fn(
                backend->ctx,
                boards,
                holes,
                1,
                params.players,
                &backend->result);

            if (status != 0) {
                printf("  ERROR: %s evaluation failed for sample %d (status=%d)\n",
                       backend->name, sample, status);
                continue;
            }

            int iteration_match = 1;
            for (int p = 0; p < params.players; ++p) {
                LowHandVal gpu_lo = (LowHandVal)backend->result.hand_values_lo[p];
                int gpu_qual = backend->result.lo_qualifies[p];
                if (cpu_hi[p] != backend->result.hand_values_hi[p] ||
                    cpu_lo[p] != gpu_lo ||
                    cpu_lo_qual[p] != gpu_qual) {
                    iteration_match = 0;
                    printf("    Hi/Lo mismatch [%s] sample %d player %d:\n",
                           backend->name, sample, p);
                    printf("      HI CPU=%08x GPU=%08x\n",
                           cpu_hi[p], backend->result.hand_values_hi[p]);
                    printf("      LO CPU=%08x GPU=%08x | Qual CPU=%d GPU=%d\n",
                           cpu_lo[p], gpu_lo, cpu_lo_qual[p], gpu_qual);
                }
            }

            if (iteration_match) {
                backend->matches++;
            }
        }
    }

    report_backend_results(backends, backend_count);

    free(holes);
    free(cpu_hi);
    free(cpu_lo);
    free(cpu_lo_qual);
    teardown_backends(backends, backend_count);
}

static void test_holdem_parity(void) {
    run_board_game_hi(
        "Hold'em CPU vs CUDA/OpenCL",
        "holdem",
        gpu_game_config_holdem(),
        1337,
        32,
        4,
        cpu_eval_holdem_hi);
}

static void test_holdem8_parity(void) {
    gpu_game_config_t config = gpu_game_config_holdem8();
    run_board_game_hilo(
        "Hold'em Hi/Lo CPU vs CUDA/OpenCL",
        "holdem8",
        config,
        2025,
        24,
        3,
        qualifier_from_rank(config.low_qualifier),
        cpu_eval_holdem_hilo);
}

static void test_omaha_parity(void) {
    const struct {
        int hole_cards;
        const char* scenario_key;
        unsigned int default_seed;
    } cases[] = {
        {4, "omaha4", 4242},
        {5, "omaha5", 4343},
        {6, "omaha6", 4444},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char label[80];
        snprintf(label, sizeof(label),
                 "Omaha-%d CPU vs CUDA/OpenCL", cases[i].hole_cards);
        run_board_game_hi(
            label,
            cases[i].scenario_key,
            gpu_game_config_omaha(cases[i].hole_cards),
            cases[i].default_seed,
            10,
            2,
            cpu_eval_omaha_hi);
    }
}

static void test_omaha8_parity(void) {
    const struct {
        int hole_cards;
        const char* scenario_key;
        unsigned int default_seed;
    } cases[] = {
        {4, "omaha8_4", 5150},
        {5, "omaha8_5", 5252},
        {6, "omaha8_6", 5353},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char label[96];
        snprintf(label, sizeof(label),
                 "Omaha8-%d (Hi/Lo) CPU vs CUDA/OpenCL", cases[i].hole_cards);
        gpu_game_config_t config = gpu_game_config_omaha8(cases[i].hole_cards);
        run_board_game_hilo(
            label,
            cases[i].scenario_key,
            config,
            cases[i].default_seed,
            8,
            2,
            qualifier_from_rank(config.low_qualifier),
            cpu_eval_omaha_hilo);
    }
}


int main(void) {
    test_holdem_parity();
    test_holdem8_parity();
    test_omaha_parity();
    test_omaha8_parity();

    printf("\n=== Summary ===\n");
    printf("Tests run:    %d\n", g_results.tests_run);
    printf("Tests passed: %d\n", g_results.tests_passed);
    printf("Tests failed: %d\n", g_results.tests_failed);

    return (g_results.tests_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
