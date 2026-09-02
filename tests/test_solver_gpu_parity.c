/*
 * GPU-05: terminal batch parity gate.
 *
 * The test deliberately returns 77 when no CUDA/OpenCL device can be created;
 * CTest reports that as SKIP. A device run compares one million deterministic
 * Hold'em terminals against cpu_ref before a backend may be considered for the
 * future AUTO gate.
 */

#include <poker_eval/solver/pe_compute.h>
#include <poker_eval/solver/pe_solver_plan.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define PARITY_BATCH_SIZE 1000000u

static int run_strategy_parity(const pe_compute_ops_t *cpu_ops, void *cpu,
                               const pe_compute_ops_t *gpu_ops, void *gpu)
{
    const uint32_t offsets[] = {0u, 4u, 7u};
    const uint16_t actions[] = {3u, 2u};
    const float regrets[] = {4.0f, -2.0f, 1.0f, 99.0f,
                             -1.0f, -3.0f, 88.0f};
    float cpu_values[7] = {0};
    float gpu_values[7] = {0};
    pe_infoset_batch_t input = {2u, offsets, actions, regrets};
    pe_strategy_batch_t cpu_output = {0u, 7u, NULL, cpu_values};
    pe_strategy_batch_t gpu_output = {0u, 7u, NULL, gpu_values};
    size_t i;

    if (cpu_ops->strategy_batch(cpu, &input, &cpu_output) != 0 ||
        gpu_ops->strategy_batch(gpu, &input, &gpu_output) != 0 ||
        cpu_output.count != gpu_output.count)
        return -1;
    for (i = 0u; i < 7u; ++i)
        if (fabsf(cpu_values[i] - gpu_values[i]) > 1.0e-5f)
            return -1;
    return 0;
}

static int run_update_parity(const pe_compute_ops_t *gpu_ops)
{
    const pe_compute_ops_t *cpu_ops = pe_compute_cpu_ref_ops();
    const pe_storage_ops_t *storage_ops = pe_storage_ram_ops();
    pe_compute_config_t cpu_cfg = {1, 1, 0u, 1u, PARITY_BATCH_SIZE,
                                   storage_ops, NULL};
    pe_compute_config_t gpu_cfg = cpu_cfg;
    pe_update_batch_t batch = {0};
    void *cpu_storage = NULL;
    void *gpu_storage = NULL;
    void *cpu = NULL;
    void *gpu = NULL;
    pe_infoset_id_t cpu_id;
    pe_infoset_id_t gpu_id;
    size_t length;
    size_t i;
    int result = -1;

    if (storage_ops->create(&cpu_storage, 1u) != 0 ||
        storage_ops->create(&gpu_storage, 1u) != 0)
        goto cleanup;
    cpu_id = storage_ops->resolve(cpu_storage, 0xFACEu, 2u, 3u,
                                  PE_STREET_UNKNOWN);
    gpu_id = storage_ops->resolve(gpu_storage, 0xFACEu, 2u, 3u,
                                  PE_STREET_UNKNOWN);
    if (cpu_id == PE_INFOSET_ID_INVALID || gpu_id == PE_INFOSET_ID_INVALID)
        goto cleanup;
    cpu_cfg.storage_self = cpu_storage;
    gpu_cfg.storage_self = gpu_storage;
    if (cpu_ops->create(&cpu, &cpu_cfg) != 0 || cpu == NULL ||
        gpu_ops->create(&gpu, &gpu_cfg) != 0 || gpu == NULL)
        goto cleanup;
    batch.items = (pe_update_t *)calloc(PARITY_BATCH_SIZE, sizeof(*batch.items));
    if (batch.items == NULL)
        goto cleanup;
    batch.count = PARITY_BATCH_SIZE;
    batch.capacity = PARITY_BATCH_SIZE;
    for (i = 0u; i < PARITY_BATCH_SIZE; ++i)
    {
        batch.items[i].infoset = cpu_id;
        batch.items[i].action = (uint16_t)(i % 2u);
        batch.items[i].combo = (uint16_t)(i % 3u);
        batch.items[i].delta = 1.0;
        batch.items[i].average_delta = 0.5;
    }
    if (cpu_ops->apply_update_batch(cpu, &batch) != 0 ||
        gpu_ops->apply_update_batch(gpu, &batch) != 0)
        goto cleanup;
    {
        const double *cpu_regrets = storage_ops->values_const(
            cpu_storage, cpu_id, PE_VALUES_REGRET, &length);
        const double *gpu_regrets = storage_ops->values_const(
            gpu_storage, gpu_id, PE_VALUES_REGRET, &length);
        const double *cpu_average = storage_ops->values_const(
            cpu_storage, cpu_id, PE_VALUES_AVERAGE, &length);
        const double *gpu_average = storage_ops->values_const(
            gpu_storage, gpu_id, PE_VALUES_AVERAGE, &length);
        if (cpu_regrets == NULL || gpu_regrets == NULL || cpu_average == NULL ||
            gpu_average == NULL)
            goto cleanup;
        for (i = 0u; i < 6u; ++i)
            if (fabs(cpu_regrets[i] - gpu_regrets[i]) > 1.0e-4 ||
                fabs(cpu_average[i] - gpu_average[i]) > 1.0e-4)
                goto cleanup;
    }
    result = 0;

cleanup:
    pe_update_batch_destroy(&batch);
    if (cpu != NULL)
        cpu_ops->destroy(cpu);
    if (gpu != NULL)
        gpu_ops->destroy(gpu);
    if (cpu_storage != NULL)
        storage_ops->destroy(cpu_storage);
    if (gpu_storage != NULL)
        storage_ops->destroy(gpu_storage);
    return result;
}

static int run_backend(const pe_compute_ops_t *ops,
                       void *backend,
                       const pe_terminal_batch_t *input,
                       uint32_t *values,
                       const uint32_t *reference)
{
    pe_value_batch_t output = {values, PARITY_BATCH_SIZE, 0u};
    size_t i;

    if (ops->terminal_eval_batch(backend, input, &output) != 0 ||
        output.count != PARITY_BATCH_SIZE)
        return -1;
    for (i = 0u; i < PARITY_BATCH_SIZE; ++i) {
        if (values[i] != reference[i])
            return -1;
    }
    return 0;
}

int main(void)
{
    const pe_compute_ops_t *cpu_ops = pe_compute_cpu_ref_ops();
    const pe_compute_ops_t *cuda_ops = pe_compute_cuda_ops();
    const pe_compute_ops_t *opencl_ops = pe_compute_opencl_ops();
    pe_compute_config_t cfg = {1, 1, 0u, PARITY_BATCH_SIZE, 0u, NULL, NULL};
    pe_terminal_batch_t input;
    void *cpu = NULL;
    void *cuda = NULL;
    void *opencl = NULL;
    uint8_t *cards;
    uint32_t *reference;
    uint32_t *candidate;
    size_t i;
    int cuda_ready;
    int opencl_ready;

    cuda_ready = cuda_ops->create(&cuda, &cfg) == 0;
    opencl_ready = opencl_ops->create(&opencl, &cfg) == 0;
    if (!cuda_ready && !opencl_ready) {
        puts("SKIP: no CUDA or OpenCL device available");
        return 77;
    }

    cards = (uint8_t *)malloc((size_t)PARITY_BATCH_SIZE * 7u);
    reference = (uint32_t *)malloc((size_t)PARITY_BATCH_SIZE * sizeof(*reference));
    candidate = (uint32_t *)malloc((size_t)PARITY_BATCH_SIZE * sizeof(*candidate));
    if (cards == NULL || reference == NULL || candidate == NULL) {
        fprintf(stderr, "parity allocation failed\n");
        free(cards);
        free(reference);
        free(candidate);
        if (cuda_ready) cuda_ops->destroy(cuda);
        if (opencl_ready) opencl_ops->destroy(opencl);
        return 1;
    }
    for (i = 0u; i < PARITY_BATCH_SIZE; ++i) {
        size_t card;
        for (card = 0u; card < 7u; ++card)
            cards[i * 7u + card] = (uint8_t)((i * 7u + card) % 52u);
    }
    input.game = game_holdem;
    input.cards = cards;
    input.hole = NULL;
    input.board = NULL;
    input.count = PARITY_BATCH_SIZE;

    if (cpu_ops->create(&cpu, &cfg) != 0 || cpu == NULL ||
        run_backend(cpu_ops, cpu, &input, reference, reference) != 0) {
        fprintf(stderr, "cpu_ref parity evaluation failed\n");
        if (cpu != NULL) cpu_ops->destroy(cpu);
        if (cuda_ready) cuda_ops->destroy(cuda);
        if (opencl_ready) opencl_ops->destroy(opencl);
        free(cards);
        free(reference);
        free(candidate);
        return 1;
    }
    if (cuda_ready && run_backend(cuda_ops, cuda, &input, candidate, reference) != 0) {
        fprintf(stderr, "CUDA terminal parity failed\n");
        cpu_ops->destroy(cpu);
        cuda_ops->destroy(cuda);
        if (opencl_ready) opencl_ops->destroy(opencl);
        free(cards);
        free(reference);
        free(candidate);
        return 1;
    }
    if (opencl_ready && run_backend(opencl_ops, opencl, &input, candidate, reference) != 0) {
        fprintf(stderr, "OpenCL terminal parity failed\n");
        cpu_ops->destroy(cpu);
        if (cuda_ready) cuda_ops->destroy(cuda);
        opencl_ops->destroy(opencl);
        free(cards);
        free(reference);
        free(candidate);
        return 1;
    }

    if (cuda_ready && (run_strategy_parity(cpu_ops, cpu, cuda_ops, cuda) != 0 ||
                       run_update_parity(cuda_ops) != 0)) {
        fprintf(stderr, "CUDA strategy/update parity failed\n");
        cpu_ops->destroy(cpu);
        cuda_ops->destroy(cuda);
        if (opencl_ready) opencl_ops->destroy(opencl);
        free(cards);
        free(reference);
        free(candidate);
        return 1;
    }
    if (opencl_ready &&
        (run_strategy_parity(cpu_ops, cpu, opencl_ops, opencl) != 0 ||
         run_update_parity(opencl_ops) != 0)) {
        fprintf(stderr, "OpenCL strategy/update parity failed\n");
        cpu_ops->destroy(cpu);
        if (cuda_ready) cuda_ops->destroy(cuda);
        opencl_ops->destroy(opencl);
        free(cards);
        free(reference);
        free(candidate);
        return 1;
    }

    pe_gpu_terminal_eval_gate_open();
    pe_gpu_regret_update_gate_open();
    if ((cuda_ready && (cuda_ops->capabilities(NULL) & PE_CAP_GPU_TERMINAL_EVAL) == 0u) ||
        (opencl_ready && (opencl_ops->capabilities(NULL) & PE_CAP_GPU_TERMINAL_EVAL) == 0u) ||
        (cuda_ready && (cuda_ops->capabilities(NULL) & PE_CAP_GPU_REGRET_UPDATE) == 0u) ||
        (opencl_ready && (opencl_ops->capabilities(NULL) & PE_CAP_GPU_REGRET_UPDATE) == 0u)) {
        fprintf(stderr, "GPU parity passed but the capability gate stayed closed\n");
        cpu_ops->destroy(cpu);
        if (cuda_ready) cuda_ops->destroy(cuda);
        if (opencl_ready) opencl_ops->destroy(opencl);
        free(cards);
        free(reference);
        free(candidate);
        return 1;
    }

    cpu_ops->destroy(cpu);
    if (cuda_ready) cuda_ops->destroy(cuda);
    if (opencl_ready) opencl_ops->destroy(opencl);
    free(cards);
    free(reference);
    free(candidate);
    puts("test_solver_gpu_parity: one million terminal and update values matched");
    return 0;
}
