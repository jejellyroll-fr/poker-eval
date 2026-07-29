#ifndef __OFC_ADAPTIVE_H__
#define __OFC_ADAPTIVE_H__

#include <poker_eval/ofc/ofc.h>
#include <poker_eval/ofc/ofc_simd.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize/cleanup adaptive system */
int OFC_AdaptiveInit(void);
void OFC_AdaptiveCleanup(void);

/* Select best processing mode */
ofc_processing_mode_t OFC_SelectBestMode(int batch_size, int simulations_per_hand);

/* Adaptive calculations */
float OFC_CalculateFoulRiskAdaptive(
    const ofc_hand_t *partial_hand,
    int card,
    ofc_position_t position,
    int simulations);

int OFC_CalculateMultipleFoulRisksAdaptive(
    const ofc_hand_t *partial_hands,
    const int *cards,
    const ofc_position_t *positions,
    int num_hands,
    int simulations_per_hand,
    float *foul_risks);

/* Query capabilities */
void OFC_GetAdaptiveCapabilities(
    int *cpu_available,
    int *simd_available,
    int *gpu_available);

void OFC_PrintAdaptiveInfo(void);

#ifdef __cplusplus
}
#endif

#endif /* __OFC_ADAPTIVE_H__ */
