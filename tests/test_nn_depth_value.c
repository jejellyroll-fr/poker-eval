#include <assert.h>
#include <math.h>
#include <stdio.h>

#include <poker_eval/engine/solvers/cfr/nn_depth_value.h>

static int features(cfr_game_t *game, uint64_t key, float *out, int count, void *user)
{
    (void)game; (void)key; (void)user;
    for (int i = 0; i < count; ++i) out[i] = 0.0f;
    out[0] = 1.0f;
    return 0;
}

int main(void)
{
    nn_policy_config_t config = {2, 2, 2, 1, NN_ACTIVATION_RELU, NN_ACTIVATION_RELU, 0, 1};
    nn_policy_t *policy = nn_policy_create(&config);
    pe_nn_depth_value_t adapter;
    double out[2] = {0.0, 0.0};
    assert(policy != NULL);
    /* hidden = [1, 0] for feature [1, 0]; output = [1, 0]. */
    policy->weights[0][0] = 1.0f; policy->weights[0][1] = 0.0f;
    policy->weights[0][2] = 0.0f; policy->weights[0][3] = 0.0f;
    policy->weights[1][0] = 1.0f; policy->weights[1][1] = 0.0f;
    policy->weights[1][2] = 0.0f; policy->weights[1][3] = 0.0f;
    for (int i = 0; i < 2; ++i) { policy->biases[0][i] = 0.0f; policy->biases[1][i] = 0.0f; }
    adapter.policy = policy; adapter.features = features; adapter.user_data = NULL;
    pe_nn_depth_value_callback(NULL, 7, 2, out, &adapter);
    assert(fabs(out[0] - 1.0) < 1e-6);
    assert(fabs(out[1]) < 1e-6);
    nn_policy_free(policy);
    puts("Embedded NN depth-value tests passed");
    return 0;
}
