/*
 * nn_policy.c - Neural Network Policy Implementation
 *
 * Lightweight CPU-optimized neural network for fast poker policy inference.
 */

#include <poker_eval/engine/policy/nn_policy.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

static int nn_policy_monotonic_time(struct timespec *ts)
{
#ifdef _WIN32
    LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    if (!ts || !QueryPerformanceFrequency(&frequency) ||
        !QueryPerformanceCounter(&counter))
    {
        return -1;
    }
    ts->tv_sec = (time_t)(counter.QuadPart / frequency.QuadPart);
    ts->tv_nsec = (long)((counter.QuadPart % frequency.QuadPart) *
                         1000000000LL / frequency.QuadPart);
    return 0;
#else
    return clock_gettime(CLOCK_MONOTONIC, ts);
#endif
}

#ifdef __AVX2__
#include <immintrin.h>
#define HAS_SIMD 1
#else
#define HAS_SIMD 0
#endif

/* ===== Configuration ===== */

nn_policy_config_t nn_policy_holdem_config(void)
{
    nn_policy_config_t config = {0};

    /* Architecture for Hold'em */
    config.input_size = 52 + 5 + 8; /* 52 card indicators + 5 board + 8 action features */
    config.hidden_size = 128;
    config.output_size = 8; /* Typical action space: fold/call/bet_sizes */
    config.num_layers = 2;

    config.hidden_activation = NN_ACTIVATION_RELU;
    config.output_activation = NN_ACTIVATION_SOFTMAX;

    config.use_simd = HAS_SIMD;
    config.batch_size = 1;

    return config;
}

nn_policy_config_t nn_policy_ofc_config(void)
{
    nn_policy_config_t config = {0};

    /* Architecture for OFC */
    config.input_size = 52 + 13 + 16; /* 52 cards + 13 placements + 16 board state */
    config.hidden_size = 256;
    config.output_size = 13; /* 13 possible placements */
    config.num_layers = 2;

    config.hidden_activation = NN_ACTIVATION_RELU;
    config.output_activation = NN_ACTIVATION_SOFTMAX;

    config.use_simd = HAS_SIMD;
    config.batch_size = 1;

    return config;
}

/* ===== Feature Vector ===== */

nn_feature_vector_t *nn_feature_vector_create(int size)
{
    nn_feature_vector_t *vec = (nn_feature_vector_t *)malloc(sizeof(nn_feature_vector_t));
    if (!vec)
        return NULL;

    vec->values = (float *)calloc(size, sizeof(float));
    if (!vec->values)
    {
        free(vec);
        return NULL;
    }

    vec->size = size;
    return vec;
}

void nn_feature_vector_free(nn_feature_vector_t *vec)
{
    if (!vec)
        return;
    free(vec->values);
    free(vec);
}

/* Placeholder feature extraction (to be implemented per game) */
int nn_extract_features_holdem(const void *state, nn_feature_vector_t *out)
{
    /* TODO: Extract features from Hold'em game state */
    /* Features: hole cards (1-hot), board cards, pot size, stack sizes, position, etc. */
    if (!state || !out)
        return -1;

    /* For now, zero-initialize */
    memset(out->values, 0, out->size * sizeof(float));

    return 0;
}

int nn_extract_features_ofc(const void *state, nn_feature_vector_t *out)
{
    /* TODO: Extract features from OFC game state */
    /* Features: current placements, remaining cards, fantasy eligibility, etc. */
    if (!state || !out)
        return -1;

    memset(out->values, 0, out->size * sizeof(float));

    return 0;
}

/* ===== Activation Functions ===== */

static inline float relu(float x)
{
    return x > 0.0f ? x : 0.0f;
}

static inline float sigmoid(float x)
{
    return 1.0f / (1.0f + expf(-x));
}

static inline float tanh_activation(float x)
{
    return tanhf(x);
}

static void softmax(float *values, int size)
{
    /* Find max for numerical stability */
    float max_val = values[0];
    for (int i = 1; i < size; i++)
    {
        if (values[i] > max_val)
            max_val = values[i];
    }

    /* Compute exp and sum */
    float sum = 0.0f;
    for (int i = 0; i < size; i++)
    {
        values[i] = expf(values[i] - max_val);
        sum += values[i];
    }

    /* Normalize */
    if (sum > 1e-9f)
    {
        for (int i = 0; i < size; i++)
        {
            values[i] /= sum;
        }
    }
}

static void apply_activation(float *values, int size, nn_activation_t activation)
{
    switch (activation)
    {
    case NN_ACTIVATION_RELU:
        for (int i = 0; i < size; i++)
        {
            values[i] = relu(values[i]);
        }
        break;

    case NN_ACTIVATION_SIGMOID:
        for (int i = 0; i < size; i++)
        {
            values[i] = sigmoid(values[i]);
        }
        break;

    case NN_ACTIVATION_TANH:
        for (int i = 0; i < size; i++)
        {
            values[i] = tanh_activation(values[i]);
        }
        break;

    case NN_ACTIVATION_SOFTMAX:
        softmax(values, size);
        break;

    default:
        /* No activation */
        break;
    }
}

/* ===== Matrix Operations ===== */

/* Matrix-vector multiply: out = weights * input + bias */
static void matmul_scalar(
    const float *weights, /* [out_size x in_size] row-major */
    const float *input,   /* [in_size] */
    const float *bias,    /* [out_size] */
    float *output,        /* [out_size] */
    int in_size,
    int out_size)
{
    for (int i = 0; i < out_size; i++)
    {
        float sum = bias[i];

        for (int j = 0; j < in_size; j++)
        {
            sum += weights[i * in_size + j] * input[j];
        }

        output[i] = sum;
    }
}

#ifdef __AVX2__
/* SIMD-optimized matrix-vector multiply (AVX2) */
static void matmul_simd_avx2(
    const float *weights,
    const float *input,
    const float *bias,
    float *output,
    int in_size,
    int out_size)
{
    const int simd_width = 8; /* AVX2: 8 floats */

    for (int i = 0; i < out_size; i++)
    {
        __m256 sum_vec = _mm256_setzero_ps();

        /* Vectorized accumulation */
        int j = 0;
        for (; j + simd_width <= in_size; j += simd_width)
        {
            __m256 w = _mm256_loadu_ps(&weights[i * in_size + j]);
            __m256 x = _mm256_loadu_ps(&input[j]);
            /* AVX2 does not imply FMA support (and the AVX2 build is not
             * compiled with -mfma), so keep this path valid on AVX2-only
             * targets as well as under Clang's stricter intrinsic checks. */
            sum_vec = _mm256_add_ps(_mm256_mul_ps(w, x), sum_vec);
        }

        /* Horizontal sum */
        float temp[8];
        _mm256_storeu_ps(temp, sum_vec);
        float sum = temp[0] + temp[1] + temp[2] + temp[3] +
                    temp[4] + temp[5] + temp[6] + temp[7];

        /* Scalar remainder */
        for (; j < in_size; j++)
        {
            sum += weights[i * in_size + j] * input[j];
        }

        output[i] = sum + bias[i];
    }
}
#endif

static void matmul(
    const float *weights,
    const float *input,
    const float *bias,
    float *output,
    int in_size,
    int out_size,
    int use_simd)
{
#ifdef __AVX2__
    if (use_simd)
    {
        matmul_simd_avx2(weights, input, bias, output, in_size, out_size);
        return;
    }
#endif

    (void)use_simd; /* Suppress unused warning */
    matmul_scalar(weights, input, bias, output, in_size, out_size);
}

/* ===== Policy Network ===== */

nn_policy_t *nn_policy_create(const nn_policy_config_t *config)
{
    if (!config)
        return NULL;

    nn_policy_t *policy = (nn_policy_t *)calloc(1, sizeof(nn_policy_t));
    if (!policy)
        return NULL;

    policy->config = *config;

    /* Determine layer sizes */
    int num_layers = config->num_layers + 1; /* +1 for output layer */
    policy->layer_sizes = (int *)malloc((num_layers + 1) * sizeof(int));
    if (!policy->layer_sizes) {
        nn_policy_free(policy);
        return NULL;
    }

    policy->layer_sizes[0] = config->input_size;
    for (int i = 1; i <= config->num_layers; i++)
    {
        policy->layer_sizes[i] = config->hidden_size;
    }
    policy->layer_sizes[num_layers] = config->output_size;

    /* Allocate weights and biases */
    policy->weights = (float **)malloc(num_layers * sizeof(float *));
    policy->biases = (float **)malloc(num_layers * sizeof(float *));
    policy->activations = (float **)malloc((num_layers + 1) * sizeof(float *));
    if (!policy->weights || !policy->biases || !policy->activations) {
        nn_policy_free(policy);
        return NULL;
    }

    for (int i = 0; i < num_layers; i++)
    {
        int in_size = policy->layer_sizes[i];
        int out_size = policy->layer_sizes[i + 1];

        /* Allocate weights (out_size x in_size) */
        policy->weights[i] = (float *)malloc(out_size * in_size * sizeof(float));
        policy->biases[i] = (float *)calloc(out_size, sizeof(float));
        if (!policy->weights[i] || !policy->biases[i]) {
            nn_policy_free(policy);
            return NULL;
        }

        /* Xavier initialization */
        float scale = sqrtf(2.0f / (float)(in_size + out_size));
        for (int j = 0; j < out_size * in_size; j++)
        {
            int rand_val = rand();
            policy->weights[i][j] = ((float)rand_val / (float)RAND_MAX - 0.5f) * 2.0f * scale;
        }

        /* Allocate activation buffers */
        policy->activations[i] = (float *)malloc(in_size * sizeof(float));
        if (!policy->activations[i]) {
            nn_policy_free(policy);
            return NULL;
        }
    }

    /* Output activation buffer */
    policy->activations[num_layers] = (float *)malloc(config->output_size * sizeof(float));
    if (!policy->activations[num_layers]) {
        nn_policy_free(policy);
        return NULL;
    }

    return policy;
}

void nn_policy_free(nn_policy_t *policy)
{
    if (!policy)
        return;

    int num_layers = policy->config.num_layers + 1;

    for (int i = 0; i < num_layers; i++)
    {
        free(policy->weights[i]);
        free(policy->biases[i]);
        free(policy->activations[i]);
    }

    free(policy->activations[num_layers]);
    free(policy->weights);
    free(policy->biases);
    free(policy->activations);
    free(policy->layer_sizes);
    free(policy);
}

/* ===== Inference ===== */

int nn_policy_evaluate(
    nn_policy_t *policy,
    const nn_feature_vector_t *features,
    float *out_action_probs,
    int *out_best_action)
{
    if (!policy || !features || !out_action_probs)
        return -1;

    struct timespec start, end;
    nn_policy_monotonic_time(&start);

    int num_layers = policy->config.num_layers + 1;

    /* Copy input features */
    memcpy(policy->activations[0], features->values, features->size * sizeof(float));

    /* Forward pass */
    for (int i = 0; i < num_layers; i++)
    {
        int in_size = policy->layer_sizes[i];
        int out_size = policy->layer_sizes[i + 1];

        /* Matrix multiply */
        matmul(
            policy->weights[i],
            policy->activations[i],
            policy->biases[i],
            policy->activations[i + 1],
            in_size,
            out_size,
            policy->config.use_simd);

        /* Activation */
        nn_activation_t activation = (i < num_layers - 1)
                                         ? policy->config.hidden_activation
                                         : policy->config.output_activation;

        apply_activation(policy->activations[i + 1], out_size, activation);
    }

    /* Copy output */
    memcpy(out_action_probs, policy->activations[num_layers], policy->config.output_size * sizeof(float));

    /* Find best action */
    if (out_best_action)
    {
        int best_idx = 0;
        float best_prob = out_action_probs[0];

        for (int i = 1; i < policy->config.output_size; i++)
        {
            if (out_action_probs[i] > best_prob)
            {
                best_prob = out_action_probs[i];
                best_idx = i;
            }
        }

        *out_best_action = best_idx;
    }

    /* Update stats */
    nn_policy_monotonic_time(&end);
    double elapsed_ms = (double)(end.tv_sec - start.tv_sec) * 1000.0 +
                        (double)(end.tv_nsec - start.tv_nsec) / 1e6;

    policy->num_inferences++;
    policy->total_inference_time_ms += elapsed_ms;

    return 0;
}

float nn_policy_estimate_value(nn_policy_t *policy, const nn_feature_vector_t *features)
{
    float action_probs[32]; /* Max action space */
    int best_action;

    if (nn_policy_evaluate(policy, features, action_probs, &best_action) != 0)
    {
        return 0.0f;
    }

    /* Simple heuristic: use max probability as value estimate */
    return action_probs[best_action];
}

/* ===== I/O ===== */

int nn_policy_load_weights(nn_policy_t *policy, const char *filename)
{
    if (!policy || !filename)
        return -1;

    FILE *f = fopen(filename, "rb");
    if (!f)
    {
        fprintf(stderr, "Failed to open %s for reading\n", filename);
        return -1;
    }

    int num_layers = policy->config.num_layers + 1;

    for (int i = 0; i < num_layers; i++)
    {
        int in_size = policy->layer_sizes[i];
        int out_size = policy->layer_sizes[i + 1];

        fread(policy->weights[i], sizeof(float), out_size * in_size, f);
        fread(policy->biases[i], sizeof(float), out_size, f);
    }

    fclose(f);

    printf("Loaded NN policy weights from %s\n", filename);

    return 0;
}

int nn_policy_save_weights(const nn_policy_t *policy, const char *filename)
{
    if (!policy || !filename)
        return -1;

    FILE *f = fopen(filename, "wb");
    if (!f)
    {
        fprintf(stderr, "Failed to open %s for writing\n", filename);
        return -1;
    }

    int num_layers = policy->config.num_layers + 1;

    for (int i = 0; i < num_layers; i++)
    {
        int in_size = policy->layer_sizes[i];
        int out_size = policy->layer_sizes[i + 1];

        fwrite(policy->weights[i], sizeof(float), out_size * in_size, f);
        fwrite(policy->biases[i], sizeof(float), out_size, f);
    }

    fclose(f);

    printf("Saved NN policy weights to %s\n", filename);

    return 0;
}

/* ===== Statistics ===== */

void nn_policy_get_stats(const nn_policy_t *policy, nn_policy_stats_t *stats)
{
    if (!policy || !stats)
        return;

    stats->num_inferences = policy->num_inferences;
    stats->total_inference_time_ms = policy->total_inference_time_ms;
    stats->avg_inference_time_us = (policy->num_inferences > 0)
                                       ? (policy->total_inference_time_ms * 1000.0 / (double)policy->num_inferences)
                                       : 0.0;
}

void nn_policy_reset_stats(nn_policy_t *policy)
{
    if (!policy)
        return;

    policy->num_inferences = 0;
    policy->total_inference_time_ms = 0.0;
}

void nn_policy_print_summary(const nn_policy_t *policy)
{
    if (!policy)
        return;

    printf("NN Policy Summary:\n");
    printf("  Architecture: %d → %d",
           policy->config.input_size,
           policy->config.hidden_size);

    for (int i = 1; i < policy->config.num_layers; i++)
    {
        printf(" → %d", policy->config.hidden_size);
    }

    printf(" → %d\n", policy->config.output_size);

    printf("  Activations: %s (hidden), %s (output)\n",
           policy->config.hidden_activation == NN_ACTIVATION_RELU ? "ReLU" : "Other",
           policy->config.output_activation == NN_ACTIVATION_SOFTMAX ? "Softmax" : "Other");

    printf("  SIMD: %s\n", policy->config.use_simd ? "Enabled" : "Disabled");

    nn_policy_stats_t stats;
    nn_policy_get_stats(policy, &stats);

    printf("  Inferences: %lu\n", stats.num_inferences);
    printf("  Avg inference time: %.2f μs\n", stats.avg_inference_time_us);
}
