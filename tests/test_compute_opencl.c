/* GPU-04: the OpenCL adapter is linkable and remains behind GPU-05. */

#include <poker_eval/solver/pe_compute.h>

#include <stdio.h>

int main(void)
{
    const pe_compute_ops_t *ops = pe_compute_opencl_ops();
    pe_compute_config_t cfg = {0};
    void *backend = (void *)1;

    cfg.terminal_batch_size = 100000u;
    if (ops == NULL || ops->name == NULL || ops->create == NULL ||
        ops->terminal_eval_batch == NULL) {
        fprintf(stderr, "OpenCL compute port is incomplete\n");
        return 1;
    }
    if ((ops->capabilities(NULL) & PE_CAP_GPU_TERMINAL_EVAL) != 0u) {
        fprintf(stderr, "OpenCL capability bypassed the GPU-05 parity gate\n");
        return 1;
    }
    if (ops->create(&backend, &cfg) == 0) {
        ops->destroy(backend);
    } else if (backend != NULL) {
        fprintf(stderr, "OpenCL create failed but returned a backend\n");
        return 1;
    }
    puts("test_compute_opencl: conditional OpenCL port and gate passed");
    return 0;
}
