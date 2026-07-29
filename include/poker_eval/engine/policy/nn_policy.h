/* nn_policy.h - Public API for Neural Network Policy
 *
 * Defines configuration, feature vectors, policy creation/evaluation,
 * weight I/O, and stats for lightweight poker neural net inference.
 */

#ifndef NN_POLICY_H
#define NN_POLICY_H

#include <stddef.h>

/* ===== Activations ===== */
typedef enum
{
    NN_ACTIVATION_RELU = 0,
    NN_ACTIVATION_SIGMOID,
    NN_ACTIVATION_TANH,
    NN_ACTIVATION_SOFTMAX
} nn_activation_t;

/* ===== Config ===== */
typedef struct
{
    int input_size;
    int hidden_size;
    int output_size;
    int num_layers; /* hidden layers count */

    nn_activation_t hidden_activation;
    nn_activation_t output_activation;

    int use_simd; /* 1 if SIMD available */
    int batch_size;
} nn_policy_config_t;

/* ===== Feature Vector ===== */
typedef struct
{
    float *values;
    int size;
} nn_feature_vector_t;

nn_feature_vector_t *nn_feature_vector_create(int size);
void nn_feature_vector_free(nn_feature_vector_t *vec);

/* Feature extraction (per game) */
int nn_extract_features_holdem(const void *state, nn_feature_vector_t *out);
int nn_extract_features_ofc(const void *state, nn_feature_vector_t *out);

/* ===== Policy Object ===== */
typedef struct
{
    nn_policy_config_t config;
    int *layer_sizes;
    float **weights;
    float **biases;
    float **activations;
    unsigned long num_inferences;
    double total_inference_time_ms;
} nn_policy_t;

/* ===== Stats ===== */
typedef struct
{
    unsigned long num_inferences;
    double total_inference_time_ms;
    double avg_inference_time_us;
} nn_policy_stats_t;

/* ===== API ===== */
nn_policy_config_t nn_policy_holdem_config(void);
nn_policy_config_t nn_policy_ofc_config(void);

nn_policy_t *nn_policy_create(const nn_policy_config_t *config);
void nn_policy_free(nn_policy_t *policy);

int nn_policy_evaluate(
    nn_policy_t *policy,
    const nn_feature_vector_t *features,
    float *out_action_probs,
    int *out_best_action);

float nn_policy_estimate_value(
    nn_policy_t *policy,
    const nn_feature_vector_t *features);

int nn_policy_load_weights(nn_policy_t *policy, const char *filename);
int nn_policy_save_weights(const nn_policy_t *policy, const char *filename);

void nn_policy_get_stats(const nn_policy_t *policy, nn_policy_stats_t *stats);
void nn_policy_reset_stats(nn_policy_t *policy);
void nn_policy_print_summary(const nn_policy_t *policy);

#endif /* NN_POLICY_H */
