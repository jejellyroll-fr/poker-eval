/* pe_outcome_traversal.h - Outcome Sampling MCCFR (OUT-01). */

#ifndef POKER_EVAL_PE_OUTCOME_TRAVERSAL_H
#define POKER_EVAL_PE_OUTCOME_TRAVERSAL_H

#include <poker_eval/solver/pe_external_traversal.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    const pe_external_game_t *game;
    const pe_storage_ops_t *storage_ops;
    void *storage;
    pe_rng_t rng;
    int updating_player;
    double epsilon;
    uint64_t iteration;
    size_t visited_nodes;
    size_t terminal_nodes;
    size_t sampled_chance_nodes;
    size_t sampled_action_nodes;
    int initialized;
} pe_outcome_sampling_ctx_t;

int pe_outcome_sampling_ctx_init(pe_outcome_sampling_ctx_t *ctx,
                                 const pe_external_game_t *game,
                                 const pe_storage_ops_t *storage_ops,
                                 void *storage, int updating_player,
                                 double epsilon, uint64_t seed);
void pe_outcome_sampling_ctx_destroy(pe_outcome_sampling_ctx_t *ctx);
int pe_outcome_sampling_run(pe_outcome_sampling_ctx_t *ctx,
                            pe_update_batch_t *out_batch);

static inline uint64_t pe_outcome_sampling_required_caps(void)
{
    return PE_CAP_DIRECT_CHANCE_SAMPLING;
}

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_OUTCOME_TRAVERSAL_H */
