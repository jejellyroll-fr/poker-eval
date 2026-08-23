/* Shared host-side preparation for GPU-06 regret updates. */

#ifndef POKER_EVAL_COMPUTE_GPU_UPDATES_H
#define POKER_EVAL_COMPUTE_GPU_UPDATES_H

#include <poker_eval/solver/pe_compute.h>

#include <stddef.h>
#include <stdint.h>

typedef struct
{
    pe_infoset_id_t id;
    uint16_t actions;
    uint16_t combos;
    size_t length;
    double *regrets;
    double *averages;
} pe_gpu_update_group_t;

typedef struct
{
    pe_gpu_update_group_t *groups;
    size_t group_count;
    size_t total_slots;

    pe_infoset_layout_t layout;
    pe_infoset_id_t *infosets;
    uint32_t *offsets;
    uint16_t *action_counts;
    uint16_t *combo_counts;

    float *regrets;
    float *averages;
    size_t count;
    uint32_t *slots;
    float *regret_deltas;
    float *average_deltas;
} pe_gpu_update_pack_t;

int pe_gpu_update_pack_build(const pe_compute_config_t *config,
                             const pe_update_batch_t *batch,
                             pe_gpu_update_pack_t *out);
int pe_gpu_update_pack_commit(pe_gpu_update_pack_t *pack);
void pe_gpu_update_pack_destroy(pe_gpu_update_pack_t *pack);

#endif /* POKER_EVAL_COMPUTE_GPU_UPDATES_H */
