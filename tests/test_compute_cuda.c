/* GPU-03: the CUDA adapter is linkable and gated in a no-CUDA build. */

#include <poker_eval/solver/pe_compute.h>

#include <stdio.h>

int main(void)
{
    const pe_compute_ops_t *ops = pe_compute_cuda_ops();
    pe_compute_config_t cfg = {0};
    void *backend = (void *)1;

    cfg.terminal_batch_size = 100000u;
    if (ops == NULL || ops->name == NULL || ops->create == NULL ||
        ops->terminal_eval_batch == NULL) {
        fprintf(stderr, "CUDA compute port is incomplete\n");
        return 1;
    }
    if ((ops->capabilities(NULL) & PE_CAP_GPU_TERMINAL_EVAL) != 0u) {
        fprintf(stderr, "CUDA capability bypassed the GPU-05 parity gate\n");
        return 1;
    }
    if (ops->create(&backend, &cfg) == 0 || backend != NULL) {
        ops->destroy(backend);
        fprintf(stderr, "CUDA backend unexpectedly created without a gate\n");
        return 1;
    }
    puts("test_compute_cuda: conditional CUDA port and gate passed");
    return 0;
}
