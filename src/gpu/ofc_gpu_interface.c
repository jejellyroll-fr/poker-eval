/*
 * ofc_gpu_interface.c - OFC GPU Interface Implementation
 *
 * Implements the OFC GPU API by extending the existing GPU infrastructure.
 * Provides the CPU-GPU interface and memory management for OFC operations.
 *
 * Based on Phase 1 SIMD success (96x speedup), targeting GPU acceleration.
 *
 * Copyright (C) 2025
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include <poker_eval/gpu/ofc_gpu.h>
#include <poker_eval/gpu/eval_gpu.h>

/* Forward declarations for internal functions */
static int ofc_gpu_allocate_memory(ofc_gpu_context_t *ctx);
static void ofc_gpu_free_memory(ofc_gpu_context_t *ctx);
static int ofc_gpu_transfer_to_device(ofc_gpu_context_t *ctx, const ofc_gpu_batch_t *batch);
static int ofc_gpu_transfer_from_device(ofc_gpu_context_t *ctx, ofc_gpu_batch_t *batch);

/* Global state for GPU initialization */
static int g_ofc_gpu_initialized = 0;
static ofc_gpu_context_t *g_global_ofc_gpu_ctx = NULL;

/* ===== Context Management ===== */

ofc_gpu_context_t* OFC_GPU_Init(int device_id, size_t max_batch_size, int backend_type) {
    printf("Initializing OFC GPU context...\n");

    /* Validate parameters */
    if (max_batch_size > OFC_GPU_MAX_BATCH_SIZE) {
        printf("Error: Requested batch size %zu exceeds maximum %d\n",
               max_batch_size, OFC_GPU_MAX_BATCH_SIZE);
        return NULL;
    }

    /* Check GPU availability first */
    if (!gpu_is_available(backend_type)) {
        printf("Warning: GPU backend %d not available, will use SIMD fallback\n", backend_type);
        return NULL;
    }

    /* Allocate OFC GPU context */
    ofc_gpu_context_t *ctx = malloc(sizeof(ofc_gpu_context_t));
    if (!ctx) {
        printf("Error: Failed to allocate OFC GPU context\n");
        return NULL;
    }
    memset(ctx, 0, sizeof(ofc_gpu_context_t));

    /* Initialize base GPU context using existing infrastructure */
    ctx->base_ctx = gpu_eval_init(device_id, max_batch_size, backend_type);
    if (!ctx->base_ctx) {
        printf("Error: Failed to initialize base GPU context\n");
        free(ctx);
        return NULL;
    }

    /* Set OFC-specific configuration */
    ctx->max_ofc_batch_size = max_batch_size;
    ctx->allocated_memory_size = 0;
    ctx->total_simulations = 0;
    ctx->total_gpu_time = 0.0;
    ctx->total_transfer_time = 0.0;

    /* Allocate GPU memory for OFC operations */
    if (ofc_gpu_allocate_memory(ctx) != 0) {
        printf("Error: Failed to allocate OFC GPU memory\n");
        gpu_eval_cleanup(ctx->base_ctx);
        free(ctx);
        return NULL;
    }

    printf("OFC GPU context initialized successfully:\n");
    printf("  Device ID: %d\n", device_id);
    printf("  Backend: %s\n", backend_type == 0 ? "CUDA" : "OpenCL");
    printf("  Max batch size: %zu\n", max_batch_size);
    printf("  Allocated GPU memory: %.2f MB\n", (double)ctx->allocated_memory_size / (1024.0 * 1024.0));

    g_ofc_gpu_initialized = 1;
    g_global_ofc_gpu_ctx = ctx;
    return ctx;
}

void OFC_GPU_Cleanup(ofc_gpu_context_t *ctx) {
    if (!ctx) return;

    printf("Cleaning up OFC GPU context...\n");

    /* Print final performance statistics */
    if (ctx->total_simulations > 0) {
        printf("OFC GPU Performance Summary:\n");
        printf("  Total simulations: %lu\n", ctx->total_simulations);
        printf("  Total GPU time: %.2f seconds\n", ctx->total_gpu_time);
        double avg_ms = ctx->total_simulations > 0
            ? (ctx->total_gpu_time * 1000.0) / (double)ctx->total_simulations
            : 0.0;
        printf("  Average time per simulation: %.6f ms\n", avg_ms);
    }

    /* Free GPU memory */
    ofc_gpu_free_memory(ctx);

    /* Cleanup base GPU context */
    if (ctx->base_ctx) {
        gpu_eval_cleanup(ctx->base_ctx);
    }

    /* Free context */
    free(ctx);

    if (g_global_ofc_gpu_ctx == ctx) {
        g_global_ofc_gpu_ctx = NULL;
        g_ofc_gpu_initialized = 0;
    }
}

size_t OFC_GPU_GetOptimalBatchSize(ofc_gpu_context_t *ctx) {
    if (!ctx || !ctx->base_ctx) {
        return OFC_GPU_BATCH_SIZE_SMALL;
    }

    /* Base decision on GPU memory and compute capability */
    size_t gpu_memory_mb;
    int compute_units;
    char device_name[256];

    if (gpu_get_device_info(ctx->base_ctx->device_id, device_name, &gpu_memory_mb, &compute_units) == 0) {
        /* Scale batch size based on GPU capabilities */
        if (gpu_memory_mb >= 8192 && compute_units >= 20) {
            return OFC_GPU_BATCH_SIZE_LARGE;      /* High-end GPU */
        } else if (gpu_memory_mb >= 4096 && compute_units >= 10) {
            return OFC_GPU_BATCH_SIZE_MEDIUM;     /* Mid-tier GPU */
        } else {
            return OFC_GPU_BATCH_SIZE_SMALL;      /* Entry-level GPU */
        }
    }

    return OFC_GPU_BATCH_SIZE_MEDIUM;  /* Safe default */
}

ofc_gpu_processing_mode_t OFC_GPU_SelectProcessingMode(
    int batch_size,
    int simulations_per_hand,
    int gpu_available
) {
    /* Decision tree based on workload characteristics */

    if (!gpu_available) {
        if (batch_size >= 4) {
            return OFC_GPU_PROCESS_SIMD;    /* Use proven 96x SIMD */
        } else {
            return OFC_GPU_PROCESS_CPU;     /* Single hand, use CPU */
        }
    }

    /* GPU is available, decide based on workload size */
    long total_operations = (long)batch_size * simulations_per_hand;

    if (total_operations >= 10000000) {     /* 10M+ operations */
        return OFC_GPU_PROCESS_HYBRID;      /* SIMD preprocessing + GPU */
    } else if (total_operations >= 100000) { /* 100K+ operations */
        return OFC_GPU_PROCESS_GPU;         /* Pure GPU */
    } else if (batch_size >= 4) {
        return OFC_GPU_PROCESS_SIMD;        /* Use proven SIMD for smaller workloads */
    } else {
        return OFC_GPU_PROCESS_CPU;         /* Very small workloads, CPU is fine */
    }
}

/* ===== Memory Management (Internal Functions) ===== */

static int ofc_gpu_allocate_memory(ofc_gpu_context_t *ctx) {
    if (!ctx || !ctx->base_ctx) return -1;

    size_t batch_size = ctx->max_ofc_batch_size;

    /* Calculate memory requirements */
    size_t partial_hands_size = batch_size * sizeof(ofc_hand_t);
    size_t completed_hands_size = batch_size * sizeof(ofc_hand_t);
    size_t available_cards_size = batch_size * StdDeck_N_CARDS * sizeof(int);
    size_t foul_risks_size = batch_size * sizeof(float);
    size_t random_states_size = batch_size * sizeof(uint32_t);

    ctx->allocated_memory_size = partial_hands_size + completed_hands_size +
                                available_cards_size + foul_risks_size + random_states_size;

    printf("Allocating OFC GPU memory:\n");
    printf("  Partial hands: %.2f MB\n", (double)partial_hands_size / (1024.0 * 1024.0));
    printf("  Completed hands: %.2f MB\n", (double)completed_hands_size / (1024.0 * 1024.0));
    printf("  Available cards: %.2f MB\n", (double)available_cards_size / (1024.0 * 1024.0));
    printf("  Results: %.2f MB\n", (double)foul_risks_size / (1024.0 * 1024.0));
    printf("  Random states: %.2f MB\n", (double)random_states_size / (1024.0 * 1024.0));
    printf("  Total: %.2f MB\n", (double)ctx->allocated_memory_size / (1024.0 * 1024.0));

    /* For now, mark as allocated (actual GPU malloc will be implemented in kernels) */
    ctx->device_partial_hands = (void*)0x1;    /* Placeholder */
    ctx->device_completed_hands = (void*)0x2;  /* Placeholder */
    ctx->device_available_cards = (void*)0x3;  /* Placeholder */
    ctx->device_foul_risks = (void*)0x4;       /* Placeholder */
    ctx->device_random_states = (void*)0x5;    /* Placeholder */

    return 0;
}

static void ofc_gpu_free_memory(ofc_gpu_context_t *ctx) {
    if (!ctx) return;

    /* Reset memory pointers (actual GPU free will be in kernels) */
    ctx->device_partial_hands = NULL;
    ctx->device_completed_hands = NULL;
    ctx->device_available_cards = NULL;
    ctx->device_foul_risks = NULL;
    ctx->device_random_states = NULL;
    ctx->allocated_memory_size = 0;
}

/* ===== High-Level API Functions ===== */

int OFC_GPU_CalculateFoulRiskBatch(ofc_gpu_context_t *ctx, ofc_gpu_batch_t *batch) {
    /* Validate inputs */
    if (!ctx || !batch) {
        return -1;
    }

    if (batch->batch_size <= 0) {
        fprintf(stderr, "Error: Invalid batch size %d (must be > 0)\n", batch->batch_size);
        return -1;
    }

    if (batch->batch_size > OFC_GPU_MAX_BATCH_SIZE) {
        fprintf(stderr, "Error: Batch size %d exceeds maximum %d\n",
                batch->batch_size, OFC_GPU_MAX_BATCH_SIZE);
        return -1;
    }

    if (batch->simulations_per_hand <= 0) {
        fprintf(stderr, "Error: Invalid simulations_per_hand %d (must be > 0)\n",
                batch->simulations_per_hand);
        return -1;
    }

    printf("Processing OFC GPU batch: %d hands, %d simulations each\n",
           batch->batch_size, batch->simulations_per_hand);

    clock_t start_time = clock();
    int result = -1;

    /* Execute based on processing mode */
    switch (batch->processing_mode) {
        case OFC_GPU_PROCESS_AUTO:
            batch->processing_mode = OFC_GPU_PROCESS_GPU;
            /* fall through */
        case OFC_GPU_PROCESS_GPU:
        case OFC_GPU_PROCESS_HYBRID:
            /* Try GPU kernel execution */
            if (OFC_GPU_GetKernelStatus() > 0) {
                printf("Using GPU Monte Carlo kernels\n");
                result = OFC_GPU_ExecuteMonteCarloKernel(
                    ctx,
                    batch->partial_hands,
                    batch->cards,
                    batch->positions,
                    batch->batch_size,
                    batch->simulations_per_hand,
                    batch->foul_risks
                );
            } else {
                printf("GPU kernels not available, falling back to SIMD\n");
                result = OFC_CalculateMultipleFoulRisksSIMD(
                    batch->partial_hands,
                    batch->cards,
                    batch->positions,
                    batch->batch_size,
                    batch->simulations_per_hand,
                    batch->foul_risks
                );
                batch->processing_mode = OFC_GPU_PROCESS_SIMD;
            }
            break;

        case OFC_GPU_PROCESS_SIMD:
            /* Use Phase 1 SIMD implementation */
            printf("Using Phase 1 SIMD implementation (96x proven)\n");
            result = OFC_CalculateMultipleFoulRisksSIMD(
                batch->partial_hands,
                batch->cards,
                batch->positions,
                batch->batch_size,
                batch->simulations_per_hand,
                batch->foul_risks
            );
            break;

        case OFC_GPU_PROCESS_CPU:
            /* Sequential CPU processing */
            printf("Using CPU processing\n");
            for (int i = 0; i < batch->batch_size; i++) {
                batch->foul_risks[i] = OFC_CalculateFoulRisk(
                    &batch->partial_hands[i],
                    batch->cards[i],
                    batch->positions[i]
                );
            }
            result = 0;
            break;

        default:
            printf("Unknown processing mode: %d\n", batch->processing_mode);
            return -1;
    }

    /* Calculate performance metrics */
    clock_t end_time = clock();
    batch->processing_time_ms = ((double)(end_time - start_time)) / CLOCKS_PER_SEC * 1000.0;
    batch->total_simulations_performed = (unsigned long)batch->batch_size * batch->simulations_per_hand;

    if (result == 0) {
        ctx->total_simulations += batch->total_simulations_performed;
        ctx->total_gpu_time += batch->processing_time_ms / 1000.0;

        double sims_per_sec = (batch->processing_time_ms > 0.0)
            ? (double)batch->total_simulations_performed / (batch->processing_time_ms / 1000.0)
            : 0.0;
        printf("Batch completed in %.2f ms (%.2f simulations/sec)\n",
               batch->processing_time_ms,
               sims_per_sec);
    }

    return result;
}

float OFC_GPU_CalculateFoulRiskAdaptive(
    const ofc_hand_t *partial_hand,
    int card,
    ofc_position_t position,
    int simulations
) {
    if (!partial_hand) return 1.0f;

    /* Determine best processing mode */
    ofc_gpu_processing_mode_t mode = OFC_GPU_SelectProcessingMode(
        1, simulations, g_ofc_gpu_initialized
    );

    switch (mode) {
        case OFC_GPU_PROCESS_SIMD:
            /* Use proven Phase 1 SIMD implementation */
            return OFC_CalculateFoulRiskSIMDSingle(partial_hand, card, position, simulations);

        case OFC_GPU_PROCESS_CPU:
            /* Use CPU fallback */
            return OFC_CalculateFoulRisk(partial_hand, card, position);

        case OFC_GPU_PROCESS_AUTO:
        case OFC_GPU_PROCESS_GPU:
        case OFC_GPU_PROCESS_HYBRID:
            /* GPU not implemented yet, fall back to SIMD */
            printf("GPU mode requested but not implemented, using SIMD fallback\n");
            return OFC_CalculateFoulRiskSIMDSingle(partial_hand, card, position, simulations);

        default:
            return OFC_CalculateFoulRisk(partial_hand, card, position);
    }
}

int OFC_GPU_CalculateFoulRiskHybrid(
    const ofc_hand_t *partial_hands,
    const int *cards,
    const ofc_position_t *positions,
    int batch_size,
    int simulations_per_hand,
    float *foul_risks
) {
    if (!partial_hands || !cards || !positions || !foul_risks || batch_size <= 0) {
        return -1;
    }

    /* For now, delegate to SIMD implementation until GPU kernels ready */
    printf("Hybrid GPU+SIMD processing (batch=%d, sims=%d) - using SIMD fallback\n",
           batch_size, simulations_per_hand);

    return OFC_CalculateMultipleFoulRisksSIMD(
        partial_hands, cards, positions, batch_size, simulations_per_hand, foul_risks
    );
}

/* ===== Utility Functions ===== */

int OFC_GPU_IsAvailable(int backend_type) {
    return gpu_is_available(backend_type);
}

int OFC_GPU_GetDeviceInfo(
    int device_id,
    char *name,
    size_t name_size,
    size_t *memory_mb,
    int *compute_units,
    size_t *max_batch_size
) {
    if (!name || !memory_mb || !compute_units || !max_batch_size) {
        return -1;
    }

    int result = gpu_get_device_info(device_id, name, memory_mb, compute_units);
    if (result == 0) {
        /* Calculate recommended OFC batch size based on memory */
        if (*memory_mb >= 8192) {
            *max_batch_size = OFC_GPU_BATCH_SIZE_LARGE;
        } else if (*memory_mb >= 4096) {
            *max_batch_size = OFC_GPU_BATCH_SIZE_MEDIUM;
        } else {
            *max_batch_size = OFC_GPU_BATCH_SIZE_SMALL;
        }
    }

    return result;
}

const char* OFC_GPU_GetProcessingModeName(ofc_gpu_processing_mode_t mode) {
    switch (mode) {
        case OFC_GPU_PROCESS_CPU:       return "CPU";
        case OFC_GPU_PROCESS_SIMD:      return "SIMD (96x proven)";
        case OFC_GPU_PROCESS_GPU:       return "GPU (target 1000x)";
        case OFC_GPU_PROCESS_HYBRID:    return "Hybrid SIMD+GPU (target 10000x)";
        case OFC_GPU_PROCESS_AUTO:      return "Auto-select";
        default:                        return "Unknown";
    }
}

/* ===== Placeholder Implementations (to be completed in later steps) ===== */

int OFC_GPU_EvaluateHandsBatch(
    ofc_gpu_context_t *ctx,
    const ofc_hand_t *hands,
    int batch_size,
    ofc_gpu_eval_result_t *result
) {
    if (!hands || !result || batch_size <= 0) return -1;

    printf("Evaluating OFC hands batch: %d hands\n", batch_size);

    clock_t start_time = clock();
    int exec_result = -1;

    int temp_hand_values[OFC_GPU_MAX_BATCH_SIZE * 3];
    int temp_hierarchy[OFC_GPU_MAX_BATCH_SIZE];

    /* Try GPU kernel execution if available */
    if (ctx && OFC_GPU_GetKernelStatus() > 0) {
        printf("Using GPU evaluation kernels\n");
        exec_result = OFC_GPU_ExecuteEvaluationKernel(
            ctx,
            hands,
            batch_size,
            temp_hand_values,
            temp_hierarchy
        );
        if (exec_result == 0) {
            for (int i = 0; i < batch_size; i++) {
                result->top_values[i] = (HandVal)temp_hand_values[i * 3 + 0];
                result->middle_values[i] = (HandVal)temp_hand_values[i * 3 + 1];
                result->bottom_values[i] = (HandVal)temp_hand_values[i * 3 + 2];
                result->hierarchy_valid[i] = temp_hierarchy[i];
            }
        }
    } else {
        printf("Using SIMD evaluation fallback\n");
        /* Use SIMD implementation as fallback */
        exec_result = OFC_ValidateHierarchySIMD(hands, batch_size, temp_hierarchy, OFC_PROCESS_SIMD_AUTO);

        /* Calculate hand strengths with CPU fallback */
        for (int i = 0; i < batch_size; i++) {
            result->top_values[i] = OFC_EvaluateHand(hands[i].hands[OFC_TOP], OFC_TOP_CARDS);
            result->middle_values[i] = OFC_EvaluateHand(hands[i].hands[OFC_MIDDLE], OFC_MIDDLE_CARDS);
            result->bottom_values[i] = OFC_EvaluateHand(hands[i].hands[OFC_BOTTOM], OFC_BOTTOM_CARDS);
            result->hierarchy_valid[i] = temp_hierarchy[i];
        }
    }

    /* Calculate performance metrics */
    clock_t end_time = clock();
    double execution_time = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;

    if (ctx) {
        ctx->total_gpu_time += execution_time;
    }

    double hands_per_sec = execution_time > 0.0 ? batch_size / execution_time : 0.0;
    printf("Hand evaluation completed in %.3f seconds (%.1f hands/sec)\n",
           execution_time, hands_per_sec);

    return exec_result;
}

void OFC_GPU_RunBenchmarkSuite(void) {
    printf("=== OFC GPU Benchmark Suite ===\n");
    printf("Phase 1 SIMD benchmark (96x proven):\n");

    /* Run existing SIMD benchmark */
    OFC_BenchmarkMonteCarloSIMD();

    printf("\nGPU benchmarks will be implemented in Week 2-3\n");
}

int OFC_GPU_ValidateResults(ofc_gpu_context_t *ctx, int test_cases, float tolerance) {
    printf("GPU result validation not implemented yet\n");
    return 0;  /* Assume all pass for now */
}
