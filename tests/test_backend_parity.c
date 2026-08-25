/*
 * test_backend_parity.c - PAR-04: cpu_par thread-count parity
 */

#include <poker_eval/solver/pe_compute.h>

#include <stdio.h>
#include <string.h>

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

static int prepare_storage(const pe_storage_ops_t *storage_ops, void *storage)
{
    unsigned infoset;

    for (infoset = 0u; infoset < 32u; ++infoset)
    {
        pe_infoset_id_t id = storage_ops->resolve(
            storage, 0x1000u + infoset, 4u, 2u, PE_STREET_UNKNOWN);
        if (id == PE_INFOSET_ID_INVALID)
            return -1;
    }
    return 0;
}

static int make_batch(pe_update_batch_t *batch,
                      const pe_storage_ops_t *storage_ops,
                      void *storage)
{
    unsigned infoset;
    unsigned action;
    unsigned combo;

    if (prepare_storage(storage_ops, storage) != 0)
        return -1;

    for (infoset = 0u; infoset < 32u; ++infoset)
    {
        pe_infoset_id_t id = storage_ops->find(storage, 0x1000u + infoset);
        if (id == PE_INFOSET_ID_INVALID)
            return -1;

        for (action = 0u; action < 4u; ++action)
            for (combo = 0u; combo < 2u; ++combo)
            {
                /* Every slot occurs once: PAR-02 has already reduced the
                   batch, so the storage loop has no competing writes. */
                double delta = (double)(infoset + 1u) * 0.125 +
                               (double)action * 0.03125 +
                               (double)combo * 0.0078125;
                double average = (double)(action + 1u) * 0.25 +
                                 (double)infoset * 0.015625 +
                                 (double)combo * 0.00390625;
                if (pe_update_batch_push(batch,
                                         (pe_update_t){id, (uint16_t)action,
                                                       (uint16_t)combo, delta,
                                                       average}) != 0)
                    return -1;
            }
    }
    return 0;
}

static int storage_matches(const pe_storage_ops_t *ops,
                           const void *left, const void *right)
{
    unsigned infoset;

    for (infoset = 0u; infoset < 32u; ++infoset)
    {
        pe_infoset_id_t left_id = ops->find(left, 0x1000u + infoset);
        pe_infoset_id_t right_id = ops->find(right, 0x1000u + infoset);
        size_t left_len = 0u;
        size_t right_len = 0u;
        const double *left_regret;
        const double *right_regret;
        const double *left_average;
        const double *right_average;

        if (left_id == PE_INFOSET_ID_INVALID ||
            right_id == PE_INFOSET_ID_INVALID)
            return 0;
        left_regret = ops->values_const(left, left_id, PE_VALUES_REGRET,
                                        &left_len);
        right_regret = ops->values_const(right, right_id, PE_VALUES_REGRET,
                                          &right_len);
        if (!left_regret || !right_regret || left_len != right_len ||
            memcmp(left_regret, right_regret,
                   left_len * sizeof(*left_regret)) != 0)
            return 0;

        left_average = ops->values_const(left, left_id, PE_VALUES_AVERAGE,
                                         &left_len);
        right_average = ops->values_const(right, right_id, PE_VALUES_AVERAGE,
                                           &right_len);
        if (!left_average || !right_average || left_len != right_len ||
            memcmp(left_average, right_average,
                   left_len * sizeof(*left_average)) != 0)
            return 0;
    }
    return 1;
}

static void test_thread_count_parity(void)
{
    const pe_compute_ops_t *compute = pe_compute_cpu_par_ops();
    const pe_storage_ops_t *storage_ops = pe_storage_ram_ops();
    void *reference_storage = NULL;
    void *reference_backend = NULL;
    pe_update_batch_t batch = {0};
    unsigned thread_counts[] = {1u, 2u, 4u, 8u};
    size_t i;

    CHECK(storage_ops->create(&reference_storage, 32u) == 0,
          "reference storage creation failed");
    if (!reference_storage)
        return;
    CHECK(make_batch(&batch, storage_ops, reference_storage) == 0,
          "parity batch creation failed");
    {
        pe_compute_config_t reference_cfg = {
            .cpu_threads = 1,
            .deterministic = 1,
            .update_batch_size = batch.count,
            .storage = storage_ops,
            .storage_self = reference_storage
        };
        CHECK(compute->create(&reference_backend, &reference_cfg) == 0 &&
                  reference_backend != NULL,
              "reference backend creation failed");
        if (reference_backend)
            CHECK(compute->apply_update_batch(reference_backend, &batch) == 0,
                  "reference update failed");
    }

    for (i = 0u; i < sizeof(thread_counts) / sizeof(thread_counts[0]); ++i)
    {
        void *storage = NULL;
        void *backend = NULL;
        pe_compute_config_t cfg = {
            .cpu_threads = (int)thread_counts[i],
            .deterministic = 1,
            .update_batch_size = batch.count,
            .storage = storage_ops
        };

        CHECK(storage_ops->create(&storage, 32u) == 0,
              "storage creation failed for %u workers", thread_counts[i]);
        if (!storage)
            continue;
        CHECK(prepare_storage(storage_ops, storage) == 0,
              "storage shape preparation failed for %u workers",
              thread_counts[i]);
        cfg.storage_self = storage;
        CHECK(compute->create(&backend, &cfg) == 0 && backend != NULL,
              "backend creation failed for %u workers", thread_counts[i]);
        if (backend)
        {
            CHECK(compute->apply_update_batch(backend, &batch) == 0,
                  "update failed for %u workers", thread_counts[i]);
            CHECK(storage_matches(storage_ops, reference_storage, storage),
                  "storage differs from reference at %u workers",
                  thread_counts[i]);
            compute->destroy(backend);
        }
        storage_ops->destroy(storage);
    }

    pe_update_batch_destroy(&batch);
    if (reference_backend)
        compute->destroy(reference_backend);
    storage_ops->destroy(reference_storage);
}

int main(void)
{
#ifndef _OPENMP
    puts("test_backend_parity: skipped (OpenMP unavailable)");
    return 0;
#else
    test_thread_count_parity();
    if (failures != 0)
        return 1;
    puts("test_backend_parity: cpu_par is bit-identical across worker counts");
    return 0;
#endif
}
