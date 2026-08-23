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
    pe_compute_config_t cfg = {
        .cpu_threads = 8,
        .deterministic = 1,
        .update_batch_size = 128u
    };
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
    pe_compute_config_t cfg = {.cpu_threads = -1, .deterministic = 1};
    void *backend = NULL;

    CHECK(ops->create(&backend, &cfg) == -1 && backend == NULL,
          "negative thread count should be refused");
}

static void test_update_batch_reaches_storage(void)
{
    const pe_compute_ops_t *ops = pe_compute_cpu_par_ops();
    const pe_storage_ops_t *storage_ops = pe_storage_ram_ops();
    pe_compute_config_t cfg = {1, 1, 0u, 0u, 16u, storage_ops, NULL};
    pe_update_batch_t batch = {0};
    void *storage = NULL;
    void *backend = NULL;
    pe_infoset_id_t id;
    const double *regrets;
    const double *average;
    size_t length;

    CHECK(storage_ops->create(&storage, 1u) == 0, "storage creation failed");
    if (!storage)
        return;
    cfg.storage_self = storage;
    id = storage_ops->resolve(storage, 0xCAFEu, 2u, 3u, PE_STREET_UNKNOWN);
    CHECK(id != PE_INFOSET_ID_INVALID, "storage infoset resolution failed");
    CHECK(ops->create(&backend, &cfg) == 0 && backend != NULL,
          "storage-backed cpu_par creation failed");
    if (!backend)
    {
        storage_ops->destroy(storage);
        return;
    }
    CHECK(pe_update_batch_push(&batch,
                               (pe_update_t){id, 1u, 2u, 2.5, 3.5}) == 0,
          "storage update push failed");
    CHECK(ops->apply_update_batch(backend, &batch) == 0,
          "storage-backed update failed");
    regrets = storage_ops->values_const(storage, id, PE_VALUES_REGRET, &length);
    CHECK(regrets != NULL && length == 6u && regrets[5u] == 2.5,
          "regret delta did not reach the expected slot");
    average = storage_ops->values_const(storage, id, PE_VALUES_AVERAGE, &length);
    CHECK(average != NULL && length == 6u && average[5u] == 3.5,
          "average delta did not reach the expected slot");

    pe_update_batch_destroy(&batch);
    ops->destroy(backend);
    storage_ops->destroy(storage);
}

int main(void)
{
    test_registration_and_capabilities();
    test_invalid_config_is_refused();
    test_update_batch_reaches_storage();
    if (failures != 0)
        return 1;
    puts("test_pe_compute_cpu_par: all tests passed");
    return 0;
}
