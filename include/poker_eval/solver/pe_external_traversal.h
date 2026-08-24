/* pe_external_traversal.h - External Sampling MCCFR (LNB-01). */

#ifndef POKER_EVAL_PE_EXTERNAL_TRAVERSAL_H
#define POKER_EVAL_PE_EXTERNAL_TRAVERSAL_H

#include <poker_eval/solver/pe_batch.h>
#include <poker_eval/solver/pe_capabilities.h>
#include <poker_eval/solver/pe_game_rules.h>
#include <poker_eval/solver/pe_storage_port.h>
#include <poker_eval/solver/pe_traversal.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PE_EXTERNAL_MAX_ACTIONS 64u

typedef struct pe_external_game_t
{
    const void *root;
    void *user;
    uint8_t player_count;

    int (*is_terminal)(const void *state, void *user);
    int (*acting_player)(const void *state, void *user);
    uint16_t (*action_count)(const void *state, void *user);
    uint64_t (*infoset_key)(const void *state, void *user);
    const void *(*apply_action)(const void *state, uint16_t action, void *user);

    /* Return the current behavioral probability of one action. NULL means
       uniform over the actions returned by action_count(). */
    double (*action_probability)(const void *state, uint64_t infoset_key,
                                 uint16_t action, void *user);
    double (*terminal_value)(const void *state, int player, void *user);

    /* Optional sampled chance layer. A non-zero sampler return means that the
       state is not a chance node and normal player callbacks are used. */
    pe_chance_sample_fn sample_chance;
    /* Context-aware form for adapters that own a sampler (for example the
       correlated Hold'em/PLO private-deal sampler). Takes precedence over the
       legacy callback when both are set. */
    int (*sample_chance_with_user)(const void *state, pe_rng_t *rng,
                                   pe_chance_sample_t *out, void *user);
    /* Optional direct sampler. It returns the already-created child state,
       avoiding an unbounded outcome index for a large private-deal space. It
       is called only when acting_player(state, user) is negative. */
    const void *(*sample_chance_child)(const void *state, pe_rng_t *rng,
                                       pe_chance_sample_t *out, void *user);
    const void *(*apply_chance)(const void *state, int outcome, void *user);
} pe_external_game_t;

typedef struct
{
    const pe_external_game_t *game;
    const pe_storage_ops_t *storage_ops;
    void *storage;
    pe_rng_t rng;
    int updating_player;
    uint64_t iteration;
    size_t visited_nodes;
    size_t terminal_nodes;
    size_t sampled_chance_nodes;
    int initialized;
} pe_external_sampling_ctx_t;

int pe_external_sampling_ctx_init(pe_external_sampling_ctx_t *ctx,
                                  const pe_external_game_t *game,
                                  const pe_storage_ops_t *storage_ops,
                                  void *storage,
                                  int updating_player,
                                  uint64_t seed);
void pe_external_sampling_ctx_destroy(pe_external_sampling_ctx_t *ctx);

/* Run one external-sampling iteration for ctx->updating_player. */
int pe_external_sampling_run(pe_external_sampling_ctx_t *ctx,
                             pe_update_batch_t *out_batch);

static inline uint64_t pe_external_sampling_required_caps(void)
{
    return PE_CAP_DIRECT_CHANCE_SAMPLING;
}

#ifdef __cplusplus
}
#endif

#endif /* PE_EXTERNAL_TRAVERSAL_H */
