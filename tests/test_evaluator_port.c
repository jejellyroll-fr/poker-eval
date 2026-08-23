/*
 * test_evaluator_port.c - GPU-02: evaluator port boundary
 */

#include <poker_eval/solver/pe_evaluator.h>

#include <stdio.h>

static int failures;

#define CHECK(condition, ...)                                      \
    do                                                             \
    {                                                              \
        if (!(condition))                                          \
        {                                                          \
            fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                         \
            fputc('\n', stderr);                                  \
            failures++;                                            \
        }                                                          \
    } while (0)

static void test_cpu_port(void)
{
    const pe_evaluator_ops_t *ops = pe_evaluator_cpu_ops();
    pe_evaluator_request_t request = {0};
    pe_evaluator_result_t result;
    void *backend = NULL;

    CHECK(ops != NULL && ops->name != NULL, "CPU evaluator did not register");
    if (ops == NULL)
        return;
    CHECK(ops->create(&backend) == 0 && backend != NULL,
          "CPU evaluator creation failed");
    if (backend == NULL)
        return;
    CHECK((ops->capabilities(backend) & PE_CAP_GPU_TERMINAL_EVAL) == 0u,
          "CPU evaluator advertised a GPU capability");
    CHECK(ops->evaluate(backend, &request, &result) ==
              PE_EVALUATOR_ERR_INVALID_REQUEST,
          "invalid request was not rejected at the port");
    CHECK(ops->sync(backend) == 0, "CPU evaluator sync failed");
    ops->destroy(backend);
}

static void test_gpu_boundary(void)
{
    const pe_evaluator_ops_t *ops = pe_evaluator_gpu_ops();
    void *backend = (void *)1;

    CHECK(ops != NULL && ops->capabilities(NULL) == 0u,
          "GPU placeholder advertised capabilities");
    CHECK(ops->create(&backend) == -1 && backend == NULL,
          "GPU placeholder unexpectedly created a backend");
}

int main(void)
{
    test_cpu_port();
    test_gpu_boundary();
    if (failures != 0)
        return 1;
    puts("test_evaluator_port: CPU/GPU evaluator boundary passed");
    return 0;
}
