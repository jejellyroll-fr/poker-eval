/*
 * pe_traversal.h - Traversal lane port (architecture v3, VEC-02)
 *
 * The vector traversal owns the descent and the per-combo reach vectors. It
 * emits raw regret deltas into a batch; regret rules, averaging and discount
 * are deliberately outside this port.
 */

#ifndef POKER_EVAL_PE_TRAVERSAL_H
#define POKER_EVAL_PE_TRAVERSAL_H

#include <poker_eval/solver/pe_storage_port.h>
#include <poker_eval/solver/pe_game_rules.h>
#include <poker_eval/solver/pe_vector.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PE_TRAVERSAL_MAX_PLAYERS 8u

typedef struct pe_traversal_ctx_t pe_traversal_ctx_t;

typedef struct
{
    pe_infoset_id_t infoset;
    uint16_t action;
    uint16_t combo;
    double delta;
} pe_update_t;

typedef struct
{
    pe_update_t *items;
    size_t count;
    size_t capacity;
} pe_update_batch_t;

void pe_update_batch_clear(pe_update_batch_t *batch);
void pe_update_batch_destroy(pe_update_batch_t *batch);
int pe_update_batch_push(pe_update_batch_t *batch, pe_update_t update);

/*
 * The rules adapter used by the VEC-02 skeleton. States are borrowed and may
 * be static; apply_action returns another borrowed state. Later tickets will
 * replace this compact surface with pe_game_rules_t without changing the
 * traversal port.
 */
typedef struct
{
    const void *root;
    void *user;
    uint8_t player_count;
    uint16_t combo_count;

    int (*is_terminal)(const void *state, void *user);
    int (*acting_player)(const void *state, void *user);
    uint16_t (*action_count)(const void *state, void *user);
    uint64_t (*infoset_key)(const void *state, void *user);

    /* Fill one action's probability vector. NULL means uniform strategy. */
    int (*strategy)(const void *state, uint64_t infoset_key,
                    uint16_t action, pe_value_vec_t *out, void *user);
    const void *(*apply_action)(const void *state, uint16_t action, void *user);

    /* Optional terminal callback. `out_values` has player_count vectors. */
    int (*terminal_values)(const void *state,
                           const pe_reach_vec_t *reach,
                           pe_value_vec_t *out_values, uint8_t player_count,
                           void *user);
} pe_vector_game_t;

struct pe_traversal_ctx_t
{
    const pe_vector_game_t *game;
    pe_reach_vec_t reach[PE_TRAVERSAL_MAX_PLAYERS];
    size_t visited_nodes;
    size_t terminal_nodes;
    uint64_t iteration;
    int initialized;
};

typedef struct
{
    const char *name;
    uint64_t required_caps;
    int (*begin_iteration)(pe_traversal_ctx_t *ctx, uint64_t iteration);
    int (*run_iteration)(pe_traversal_ctx_t *ctx, pe_update_batch_t *out_batch);
    int (*end_iteration)(pe_traversal_ctx_t *ctx, uint64_t iteration);
} pe_traversal_ops_t;

typedef int (*pe_vector_chance_sample_fn)(const void *state,
                                          pe_rng_t *rng,
                                          pe_chance_sample_t *out,
                                          void *user);
typedef const void *(*pe_vector_apply_chance_fn)(const void *state,
                                                  int outcome,
                                                  void *user);

/** Context for one-vector-per-combo chance sampling (VEC-08). */
typedef struct
{
    const pe_vector_game_t *game;
    pe_vector_chance_sample_fn sample_chance;
    pe_vector_apply_chance_fn apply_chance;
    void *user;
    pe_rng_t rng;
    pe_reach_vec_t reach[PE_TRAVERSAL_MAX_PLAYERS];
    pe_value_vec_t values[PE_TRAVERSAL_MAX_PLAYERS];
    size_t visited_nodes;
    size_t terminal_nodes;
    size_t sampled_chance_nodes;
    double importance_ratio;
    int initialized;
} pe_chance_vector_ctx_t;

int pe_traversal_ctx_init(pe_traversal_ctx_t *ctx,
                          const pe_vector_game_t *game);
void pe_traversal_ctx_destroy(pe_traversal_ctx_t *ctx);

const pe_traversal_ops_t *pe_traversal_full_vector_ops(void);

int pe_chance_vector_ctx_init(pe_chance_vector_ctx_t *ctx,
                              const pe_vector_game_t *game,
                              pe_vector_chance_sample_fn sample_chance,
                              pe_vector_apply_chance_fn apply_chance,
                              uint64_t seed);
void pe_chance_vector_ctx_destroy(pe_chance_vector_ctx_t *ctx);
int pe_chance_vector_run(pe_chance_vector_ctx_t *ctx);
const pe_value_vec_t *pe_chance_vector_values(
    const pe_chance_vector_ctx_t *ctx, uint8_t player);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_TRAVERSAL_H */
