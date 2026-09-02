/*
 * pe_betting_vector.h - bridge generic betting states to the vector lane
 */

#ifndef POKER_EVAL_PE_BETTING_VECTOR_H
#define POKER_EVAL_PE_BETTING_VECTOR_H

#include <stddef.h>
#include <stdint.h>

#include <poker_eval/solver/pe_betting_state.h>
#include <poker_eval/solver/pe_traversal.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint16_t (*action_count)(const pe_betting_state_t *state, void *user);
    pe_action_status_t (*action_at)(const pe_betting_state_t *state,
                                    uint16_t action, pe_action_t *out,
                                    void *user);
    uint64_t (*infoset_key)(const pe_betting_state_t *state, void *user);
    int (*strategy)(const pe_betting_state_t *state, uint64_t infoset_key,
                    uint16_t action, pe_value_vec_t *out, void *user);
    int (*terminal_values)(const pe_betting_state_t *state,
                           const pe_reach_vec_t *reach,
                           pe_value_vec_t *out_values, uint8_t player_count,
                           void *user);
    pe_vector_combo_compatible_fn combo_compatible;
} pe_betting_vector_ops_t;

typedef struct
{
    pe_vector_game_t vector;
    pe_betting_rules_t rules;
    pe_betting_state_t root;
    pe_betting_vector_ops_t ops;
    void *user;
    pe_betting_state_t **owned_states;
    size_t owned_count;
    size_t owned_capacity;
} pe_betting_vector_game_t;

/*
 * Initialize a one-street betting game. The callback table is borrowed only
 * during initialization and copied into the adapter. `user` is forwarded to
 * every callback in the table.
 */
pe_betting_status_t pe_betting_vector_game_init(
    pe_betting_vector_game_t *out,
    const pe_betting_rules_t *rules,
    const pe_betting_state_t *root,
    uint16_t combo_count,
    const pe_betting_vector_ops_t *ops,
    void *user);

/* Release states allocated while traversing the adapter. */
void pe_betting_vector_game_destroy(pe_betting_vector_game_t *game);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_BETTING_VECTOR_H */
