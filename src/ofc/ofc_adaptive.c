/*
 * ofc_adaptive.c - Adaptive processing mode selection
 *
 * Intelligently chooses CPU, SIMD, or GPU based on:
 * - Hardware availability
 * - Problem size
 * - Performance characteristics
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poker_eval/ofc/ofc.h>
#include <poker_eval/ofc/ofc_simd.h>
#include <poker_eval/ofc/ofc_adaptive.h>

#if defined(HAVE_CUDA) || defined(HAVE_OPENCL)
#include <poker_eval/gpu/ofc_gpu.h>
#define GPU_AVAILABLE 1
#else
#define GPU_AVAILABLE 0
/* Forward declaration for when GPU is not available */
typedef void* ofc_gpu_context_t;
#endif

/* Performance thresholds */
#define SIMD_MIN_BATCH_SIZE 4
#define GPU_MIN_BATCH_SIZE 64
#define GPU_MIN_SIMULATIONS 10000

/* Cache hardware capabilities */
static struct {
    int simd_available;
    int gpu_available;
    ofc_simd_capability_t simd_caps;
    ofc_gpu_context_t *gpu_ctx;
    int initialized;
} g_adaptive_state = {0};

/* Initialize adaptive system */
int OFC_AdaptiveInit(void) {
    if (g_adaptive_state.initialized) {
        return 0;
    }

    /* Check SIMD */
    g_adaptive_state.simd_caps = OFC_DetectSIMDCapabilities();
    g_adaptive_state.simd_available = (g_adaptive_state.simd_caps != OFC_SIMD_NONE);

    /* Check GPU */
#if GPU_AVAILABLE
    if (OFC_GPU_IsAvailable(OFC_GPU_BACKEND_AUTO)) {
        g_adaptive_state.gpu_ctx = OFC_GPU_Init(-1, 8192, OFC_GPU_BACKEND_AUTO);
        g_adaptive_state.gpu_available = (g_adaptive_state.gpu_ctx != NULL);
    } else {
        g_adaptive_state.gpu_available = 0;
        g_adaptive_state.gpu_ctx = NULL;
    }
#else
    g_adaptive_state.gpu_available = 0;
    g_adaptive_state.gpu_ctx = NULL;
#endif

    g_adaptive_state.initialized = 1;
    return 0;
}

/* Cleanup adaptive system */
void OFC_AdaptiveCleanup(void) {
#if GPU_AVAILABLE
    if (g_adaptive_state.gpu_ctx) {
        OFC_GPU_Cleanup(g_adaptive_state.gpu_ctx);
        g_adaptive_state.gpu_ctx = NULL;
    }
#endif
    g_adaptive_state.initialized = 0;
}

/* Select best processing mode */
ofc_processing_mode_t OFC_SelectBestMode(
    int batch_size,
    int simulations_per_hand) {

    /* GPU is best for large batches with many simulations */
    if (g_adaptive_state.gpu_available &&
        batch_size >= GPU_MIN_BATCH_SIZE &&
        simulations_per_hand >= GPU_MIN_SIMULATIONS) {
        return OFC_PROCESS_GPU;
    }

    /* SIMD is best for medium batches */
    if (g_adaptive_state.simd_available &&
        batch_size >= SIMD_MIN_BATCH_SIZE) {
        return OFC_PROCESS_SIMD_AUTO;
    }

    /* Fall back to CPU */
    return OFC_PROCESS_CPU;
}

/* Adaptive foul risk calculation */
float OFC_CalculateFoulRiskAdaptive(
    const ofc_hand_t *partial_hand,
    int card,
    ofc_position_t position,
    int simulations) {

    if (!g_adaptive_state.initialized) {
        OFC_AdaptiveInit();
    }

    /* Single hand - choose based on simulation count */
#if GPU_AVAILABLE
    if (simulations >= GPU_MIN_SIMULATIONS && g_adaptive_state.gpu_available) {
        /* Use GPU for large simulation counts */
        ofc_gpu_batch_t batch;
        memset(&batch, 0, sizeof(batch));
        batch.partial_hands[0] = *partial_hand;
        batch.cards[0] = card;
        batch.positions[0] = position;
        batch.simulations_per_hand = simulations;
        batch.batch_size = 1;

        if (OFC_GPU_CalculateFoulRiskBatch(g_adaptive_state.gpu_ctx, &batch) == 0) {
            return batch.foul_risks[0];
        }
        /* Fall through to CPU on error */
    }
#endif

    /* Use SIMD or CPU */
    return OFC_CalculateFoulRiskSIMDSingle(partial_hand, card, position, simulations);
}

/* Adaptive batch processing */
int OFC_CalculateMultipleFoulRisksAdaptive(
    const ofc_hand_t *partial_hands,
    const int *cards,
    const ofc_position_t *positions,
    int num_hands,
    int simulations_per_hand,
    float *foul_risks) {

    if (!g_adaptive_state.initialized) {
        OFC_AdaptiveInit();
    }

    ofc_processing_mode_t mode = OFC_SelectBestMode(num_hands, simulations_per_hand);

    switch (mode) {
#if GPU_AVAILABLE
        case OFC_PROCESS_GPU:
            /* Use GPU for large batches */
            {
                ofc_gpu_batch_t batch;
                memset(&batch, 0, sizeof(batch));

                /* Handle batches larger than GPU max */
                if (num_hands > OFC_GPU_MAX_BATCH_SIZE) {
                    /* Process in chunks */
                    int processed = 0;
                    while (processed < num_hands) {
                        int chunk_size = (num_hands - processed > OFC_GPU_MAX_BATCH_SIZE) ?
                                        OFC_GPU_MAX_BATCH_SIZE : (num_hands - processed);

                        for (int i = 0; i < chunk_size; i++) {
                            batch.partial_hands[i] = partial_hands[processed + i];
                            batch.cards[i] = cards[processed + i];
                            batch.positions[i] = positions[processed + i];
                        }
                        batch.batch_size = chunk_size;
                        batch.simulations_per_hand = simulations_per_hand;

                        if (OFC_GPU_CalculateFoulRiskBatch(g_adaptive_state.gpu_ctx, &batch) != 0) {
                            /* GPU failed, fall back to SIMD */
                            return OFC_CalculateMultipleFoulRisksSIMD(
                                partial_hands, cards, positions, num_hands,
                                simulations_per_hand, foul_risks);
                        }

                        memcpy(&foul_risks[processed], batch.foul_risks,
                               sizeof(float) * chunk_size);
                        processed += chunk_size;
                    }
                    return 0;
                } else {
                    /* Fits in single batch */
                    for (int i = 0; i < num_hands; i++) {
                        batch.partial_hands[i] = partial_hands[i];
                        batch.cards[i] = cards[i];
                        batch.positions[i] = positions[i];
                    }
                    batch.batch_size = num_hands;
                    batch.simulations_per_hand = simulations_per_hand;

                    if (OFC_GPU_CalculateFoulRiskBatch(g_adaptive_state.gpu_ctx, &batch) != 0) {
                        /* GPU failed, fall back to SIMD */
                        return OFC_CalculateMultipleFoulRisksSIMD(
                            partial_hands, cards, positions, num_hands,
                            simulations_per_hand, foul_risks);
                    }

                    memcpy(foul_risks, batch.foul_risks, sizeof(float) * num_hands);
                    return 0;
                }
            }
#else
        case OFC_PROCESS_GPU:
            /* GPU not available, fall through to SIMD */
            /* Fall through */
#endif

        case OFC_PROCESS_SIMD_AUTO:
        case OFC_PROCESS_SIMD_SSE2:
        case OFC_PROCESS_SIMD_AVX2:
        case OFC_PROCESS_SIMD_AVX512:
            /* Use SIMD for medium batches */
            return OFC_CalculateMultipleFoulRisksSIMD(
                partial_hands, cards, positions, num_hands,
                simulations_per_hand, foul_risks);

        case OFC_PROCESS_CPU:
        default:
            /* Use CPU for small batches */
            for (int i = 0; i < num_hands; i++) {
                foul_risks[i] = OFC_CalculateFoulRiskSIMDSingle(
                    &partial_hands[i], cards[i], positions[i], simulations_per_hand);
            }
            return 0;
    }
}

/* Get current capabilities */
void OFC_GetAdaptiveCapabilities(
    int *cpu_available,
    int *simd_available,
    int *gpu_available) {

    if (!g_adaptive_state.initialized) {
        OFC_AdaptiveInit();
    }

    if (cpu_available) *cpu_available = 1;  /* Always available */
    if (simd_available) *simd_available = g_adaptive_state.simd_available;
    if (gpu_available) *gpu_available = g_adaptive_state.gpu_available;
}

/* Print adaptive system info */
void OFC_PrintAdaptiveInfo(void) {
    if (!g_adaptive_state.initialized) {
        OFC_AdaptiveInit();
    }

    printf("OFC Adaptive Processing System:\n");
    printf("  CPU:  Available\n");
    printf("  SIMD: %s", g_adaptive_state.simd_available ? "Available" : "Not Available");
    if (g_adaptive_state.simd_available) {
        printf(" (%s)\n", OFC_GetSIMDCapabilityName(g_adaptive_state.simd_caps));
    } else {
        printf("\n");
    }
    printf("  GPU:  %s\n", g_adaptive_state.gpu_available ? "Available" : "Not Available");

    printf("\nSelection Thresholds:\n");
    printf("  SIMD: batch_size >= %d\n", SIMD_MIN_BATCH_SIZE);
    printf("  GPU:  batch_size >= %d AND simulations >= %d\n",
           GPU_MIN_BATCH_SIZE, GPU_MIN_SIMULATIONS);
}
