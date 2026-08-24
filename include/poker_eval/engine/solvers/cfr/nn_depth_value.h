/* Adapter for using the embedded CPU MLP as a CFR depth value callback. */
#ifndef POKER_EVAL_NN_DEPTH_VALUE_H
#define POKER_EVAL_NN_DEPTH_VALUE_H

#include <poker_eval/engine/policy/nn_policy.h>
#include <poker_eval/engine/solvers/cfr/cfr_core.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*pe_nn_state_features_fn)(cfr_game_t *game,
                                       uint64_t state_key,
                                       float *out_features,
                                       int feature_count,
                                       void *user_data);

typedef struct {
    nn_policy_t *policy;
    pe_nn_state_features_fn features;
    void *user_data;
} pe_nn_depth_value_t;

/* The policy output is interpreted as one bounded utility per player. For a
 * multi-player policy, output_size should be at least num_players. This is an
 * inference bridge only: the repository does not ship trained poker weights. */
void pe_nn_depth_value_callback(cfr_game_t *game,
                                uint64_t state_key,
                                int num_players,
                                double *out_utilities,
                                void *user_data);

#ifdef __cplusplus
}
#endif

#endif
