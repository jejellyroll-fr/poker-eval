/*
 * test_compute_cpu_ref.c - GPU-01: reference backend contract
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

static void test_reference_contract(void)
{
    const pe_compute_ops_t *ops = pe_compute_cpu_ref_ops();
    const pe_storage_ops_t *storage_ops = pe_storage_ram_ops();
    pe_compute_config_t cfg = {1, 1, 0u, 0u, 1u, storage_ops, NULL};
    void *storage = NULL;
    void *backend = NULL;
    pe_update_batch_t batch = {0};
    pe_infoset_id_t id;
    const double *regrets;
    const double *average;
    size_t length;
    uint64_t caps;

    CHECK(ops != NULL && ops->name != NULL, "cpu_ref did not register");
    if (ops == NULL)
        return;
    CHECK(ops->name[0] == 'c' && ops->name[4] == 'r',
          "unexpected cpu_ref name");
    caps = ops->capabilities(NULL);
    CHECK((caps & PE_CAP_DETERMINISTIC) != 0u,
          "cpu_ref must advertise deterministic execution");
    CHECK((caps & (PE_CAP_GPU_TERMINAL_EVAL | PE_CAP_GPU_VECTOR_SHOWDOWN |
                   PE_CAP_GPU_REGRET_UPDATE | PE_CAP_GPU_TRAVERSAL)) == 0u,
          "cpu_ref must not advertise GPU capabilities");
    CHECK((caps & PE_CAP_CPU_PARALLEL) == 0u,
          "cpu_ref must not advertise CPU parallelism");

    CHECK(storage_ops->create(&storage, 1u) == 0, "storage creation failed");
    if (storage == NULL)
        return;
    cfg.storage_self = storage;
    id = storage_ops->resolve(storage, 0xBEEF, 2u, 2u, PE_STREET_UNKNOWN);
    CHECK(id != PE_INFOSET_ID_INVALID, "infoset resolution failed");
    CHECK(ops->create(&backend, &cfg) == 0 && backend != NULL,
          "cpu_ref creation failed");
    if (backend != NULL) {
        CHECK(pe_update_batch_push(&batch,
                                   (pe_update_t){id, 1u, 1u, 1.5, 2.5}) == 0,
              "update push failed");
        CHECK(ops->apply_update_batch(backend, &batch) == 0,
              "reference update failed");
        regrets = storage_ops->values_const(storage, id, PE_VALUES_REGRET,
                                            &length);
        average = storage_ops->values_const(storage, id, PE_VALUES_AVERAGE,
                                            &length);
        CHECK(regrets != NULL && average != NULL && regrets[3] == 1.5 &&
                  average[3] == 2.5,
              "reference update did not reach storage");
        CHECK(ops->sync(backend) == 0, "reference sync failed");
        ops->destroy(backend);
    }
    pe_update_batch_destroy(&batch);
    storage_ops->destroy(storage);
}

static void test_invalid_parallel_config(void)
{
    const pe_compute_ops_t *ops = pe_compute_cpu_ref_ops();
    pe_compute_config_t cfg = {2, 1, 0u, 0u, 0u, NULL, NULL};
    void *backend = NULL;
    CHECK(ops->create(&backend, &cfg) == -1 && backend == NULL,
          "cpu_ref accepted more than one worker");
}

int main(void)
{
    test_reference_contract();
    test_invalid_parallel_config();
    if (failures != 0)
        return 1;
    puts("test_compute_cpu_ref: deterministic reference contract passed");
    return 0;
}
