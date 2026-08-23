/*
 * GPU-05: terminal batch parity gate.
 *
 * The test deliberately returns 77 when no CUDA/OpenCL device can be created;
 * CTest reports that as SKIP. A device run compares one million deterministic
 * Hold'em terminals against cpu_ref before a backend may be considered for the
 * future AUTO gate.
 */

#include <poker_eval/solver/pe_compute.h>

#include <stdio.h>
#include <stdlib.h>

#define PARITY_BATCH_SIZE 1000000u

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

    cpu_ops->destroy(cpu);
    if (cuda_ready) cuda_ops->destroy(cuda);
    if (opencl_ready) opencl_ops->destroy(opencl);
    free(cards);
    free(reference);
    free(candidate);
    puts("test_solver_gpu_parity: one million terminal values matched");
    return 0;
}
