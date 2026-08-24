#include <poker_eval/engine/solvers/cfr/nn_depth_value.h>

#include <string.h>

void pe_nn_depth_value_callback(cfr_game_t *game,
                                uint64_t state_key,
                                int num_players,
                                double *out_utilities,
                                void *user_data)
{
    pe_nn_depth_value_t *adapter = user_data;
    nn_feature_vector_t features;
    float *values;
    float probabilities[32];
    int output_count;
    if (!out_utilities || num_players <= 0)
        return;
    memset(out_utilities, 0, (size_t)num_players * sizeof(*out_utilities));
    if (!adapter || !adapter->policy || !adapter->features)
        return;
    output_count = adapter->policy->config.output_size;
    if (output_count <= 0 || output_count > (int)(sizeof(probabilities) / sizeof(probabilities[0])))
        return;
    values = adapter->policy->activations[0];
    features.values = values;
    features.size = adapter->policy->config.input_size;
    if (adapter->features(game, state_key, values, features.size, adapter->user_data) != 0 ||
        nn_policy_evaluate(adapter->policy, &features, probabilities, NULL) != 0)
        return;
    for (int player = 0; player < num_players && player < output_count; ++player)
        out_utilities[player] = (double)probabilities[player];
}
