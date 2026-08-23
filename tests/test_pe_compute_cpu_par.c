/*
 * test_pe_compute_cpu_par.c - PAR-03 CPU-parallel backend registration
 */

#include <poker_eval/solver/pe_compute.h>

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

static void test_registration_and_capabilities(void)
{
    const pe_compute_ops_t *ops = pe_compute_cpu_par_ops();
    pe_compute_config_t cfg = {8, 1, 0u, 0u, 128u};
    pe_update_batch_t batch = {0};
    void *backend = NULL;

    CHECK(ops != NULL && ops->name != NULL, "cpu_par did not register");
    if (!ops)
        return;
    CHECK(ops->name[0] == 'c', "unexpected backend name");
    CHECK((ops->capabilities(NULL) &
           (PE_CAP_CPU_PARALLEL | PE_CAP_BATCH_UPDATES |
            PE_CAP_DETERMINISTIC)) ==
              (PE_CAP_CPU_PARALLEL | PE_CAP_BATCH_UPDATES |
               PE_CAP_DETERMINISTIC),
          "cpu_par capabilities are incomplete");
    CHECK((ops->capabilities(NULL) & PE_CAP_GPU_TRAVERSAL) == 0u,
          "cpu_par must not advertise GPU traversal");
    CHECK(ops->create(&backend, &cfg) == 0 && backend != NULL,
          "cpu_par creation failed");
    if (!backend)
        return;
    CHECK(ops->apply_update_batch(backend, &batch) == 0,
          "empty update batch should be accepted");
    CHECK(ops->sync(backend) == 0, "cpu_par sync failed");
    ops->destroy(backend);
}

static void test_invalid_config_is_refused(void)
{
    const pe_compute_ops_t *ops = pe_compute_cpu_par_ops();
    pe_compute_config_t cfg = {-1, 1, 0u, 0u, 0u};
    void *backend = NULL;

    CHECK(ops->create(&backend, &cfg) == -1 && backend == NULL,
          "negative thread count should be refused");
}

int main(void)
{
    test_registration_and_capabilities();
    test_invalid_config_is_refused();
    if (failures != 0)
        return 1;
    puts("test_pe_compute_cpu_par: all tests passed");
    return 0;
}
