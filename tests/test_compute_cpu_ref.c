/*
 * test_compute_cpu_ref.c - GPU-01: reference backend contract
 */

#include <poker_eval/solver/pe_compute.h>

#include <math.h>
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
        regrets = storage_ops->values(storage, id, PE_VALUES_REGRET,
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

static void test_regret_and_average_modes(void)
{
    const pe_compute_ops_t *ops = pe_compute_cpu_ref_ops();
    const pe_storage_ops_t *storage_ops = pe_storage_ram_ops();
    pe_compute_config_t plus_cfg = {
        .cpu_threads = 1,
        .deterministic = 1,
        .storage = storage_ops,
        .regret_mode = PE_REGRET_PLUS,
        .averaging_mode = PE_AVG_UNIFORM
    };
    pe_compute_config_t dcfr_cfg = {
        .cpu_threads = 1,
        .deterministic = 1,
        .storage = storage_ops,
        .regret_mode = PE_REGRET_DCFR,
        .averaging_mode = PE_AVG_POWER,
        .dcfr_alpha = 1.5,
        .dcfr_beta = 0.0,
        .dcfr_gamma = 2.0
    };
    pe_update_batch_t batch = {0};
    void *storage = NULL;
    void *backend = NULL;
    pe_infoset_id_t id;
    double *regrets;
    const double *average;
    size_t length;

    CHECK(storage_ops->create(&storage, 1u) == 0 && storage,
          "mode test storage creation failed");
    if (!storage)
        return;
    plus_cfg.storage_self = storage;
    id = storage_ops->resolve(storage, 0xA11CEu, 2u, 1u, PE_STREET_UNKNOWN);
    CHECK(id != PE_INFOSET_ID_INVALID, "CFR+ infoset resolution failed");
    CHECK(ops->create(&backend, &plus_cfg) == 0 && backend,
          "CFR+ backend creation failed");
    if (backend) {
        batch.iteration = 1u;
        CHECK(pe_update_batch_push(&batch,
                                   (pe_update_t){id, 1u, 0u, -2.0, 1.0}) == 0,
              "CFR+ update push failed");
        CHECK(ops->apply_update_batch(backend, &batch) == 0,
              "CFR+ update failed");
        regrets = storage_ops->values(storage, id, PE_VALUES_REGRET,
                                      &length);
        average = storage_ops->values_const(storage, id, PE_VALUES_AVERAGE,
                                            &length);
        CHECK(regrets && average && regrets[1] == 0.0 && average[1] == 1.0,
              "CFR+ did not clamp regret or preserve average update");
        ops->destroy(backend);
    }
    pe_update_batch_clear(&batch);
    storage_ops->destroy(storage);
    storage = NULL;
    backend = NULL;

    CHECK(storage_ops->create(&storage, 1u) == 0 && storage,
          "DCFR storage creation failed");
    if (!storage) {
        pe_update_batch_destroy(&batch);
        return;
    }
    dcfr_cfg.storage_self = storage;
    id = storage_ops->resolve(storage, 0xDCFEu, 2u, 1u, PE_STREET_UNKNOWN);
    CHECK(id != PE_INFOSET_ID_INVALID, "DCFR infoset resolution failed");
    regrets = storage_ops->values(storage, id, PE_VALUES_REGRET, &length);
    CHECK(regrets != NULL && length == 2u, "DCFR regret storage unavailable");
    if (regrets)
        regrets[0] = 8.0;
    CHECK(ops->create(&backend, &dcfr_cfg) == 0 && backend,
          "DCFR backend creation failed");
    if (backend) {
        batch.iteration = 1u;
        CHECK(pe_update_batch_push(&batch,
                                   (pe_update_t){id, 0u, 0u, 0.0, 4.0}) == 0,
              "DCFR update push failed");
        CHECK(ops->apply_update_batch(backend, &batch) == 0,
              "DCFR update failed");
        regrets = storage_ops->values(storage, id, PE_VALUES_REGRET,
                                      &length);
        average = storage_ops->values_const(storage, id, PE_VALUES_AVERAGE,
                                            &length);
        CHECK(regrets && average && fabs(regrets[0] - 4.0) < 1e-12 &&
                  fabs(average[0] - 1.0) < 1e-12,
              "DCFR did not discount regret and power-weight average");
        ops->destroy(backend);
    }
    pe_update_batch_destroy(&batch);
    storage_ops->destroy(storage);
}

static void test_soa_update_reaches_storage(void)
{
    const pe_compute_ops_t *ops = pe_compute_cpu_ref_ops();
    const pe_storage_ops_t *storage_ops = pe_storage_ram_ops();
    pe_compute_config_t cfg = {
        .cpu_threads = 1,
        .deterministic = 1,
        .storage = storage_ops,
        .averaging_mode = PE_AVG_UNIFORM
    };
    pe_update_batch_t batch = {0};
    void *storage = NULL;
    void *backend = NULL;
    pe_infoset_id_t id;
    double *deltas;
    double *averages;
    const double *regrets;
    const double *average;
    size_t length;

    CHECK(storage_ops->create(&storage, 1u) == 0 && storage,
          "SoA storage creation failed");
    if (!storage)
        return;
    cfg.storage_self = storage;
    id = storage_ops->resolve(storage, 0x50Au, 2u, 3u, PE_STREET_UNKNOWN);
    CHECK(id != PE_INFOSET_ID_INVALID, "SoA infoset resolution failed");
    CHECK(pe_update_batch_soa_begin_group(
              &batch, id, 2u, 3u, &deltas, &averages) == 0,
          "SoA group creation failed");
    if (deltas == NULL || averages == NULL)
        goto cleanup;
    for (size_t i = 0u; i < 6u; ++i)
    {
        deltas[i] = (double)i + 0.5;
        averages[i] = (double)i + 10.5;
    }
    batch.iteration = 1u;
    CHECK(ops->create(&backend, &cfg) == 0 && backend,
          "SoA reference backend creation failed");
    if (!backend)
        goto cleanup;
    CHECK(ops->apply_update_batch(backend, &batch) == 0,
          "SoA reference update failed");
    regrets = storage_ops->values_const(storage, id, PE_VALUES_REGRET,
                                        &length);
    average = storage_ops->values_const(storage, id, PE_VALUES_AVERAGE,
                                        &length);
    CHECK(regrets != NULL && average != NULL && length == 6u,
          "SoA storage output is incomplete");
    if (regrets != NULL && average != NULL)
        for (size_t i = 0u; i < 6u; ++i)
            CHECK(fabs(regrets[i] - deltas[i]) < 1e-12 &&
                      fabs(average[i] - averages[i]) < 1e-12,
                  "SoA value %zu was not applied", i);

cleanup:
    if (backend)
        ops->destroy(backend);
    pe_update_batch_destroy(&batch);
    storage_ops->destroy(storage);
}

/*
 * A batch that cannot be applied must leave storage exactly as it was.
 * The failure is placed in the second group so the first has already been
 * computed when it happens: staging is what keeps the first group's values
 * out of storage. Without it the caller sees -1 over half-updated regrets,
 * and a checkpoint taken afterwards would persist them.
 */
static void test_failed_batch_leaves_storage_untouched(void)
{
    const pe_compute_ops_t *ops = pe_compute_cpu_ref_ops();
    const pe_storage_ops_t *storage_ops = pe_storage_ram_ops();
    pe_compute_config_t cfg = {
        .cpu_threads = 1,
        .deterministic = 1,
        .storage = storage_ops,
        .regret_mode = PE_REGRET_LEGACY_EXP,
        .averaging_mode = PE_AVG_UNIFORM
    };
    pe_update_batch_t batch = {0};
    void *storage = NULL;
    void *backend = NULL;
    pe_infoset_id_t good;
    pe_infoset_id_t bad;
    double *good_deltas;
    double *good_average;
    double *bad_deltas;
    double *bad_average;
    const double *stored;
    size_t length;
    size_t i;

    CHECK(storage_ops->create(&storage, 2u) == 0 && storage,
          "atomicity: storage creation failed");
    if (!storage)
        return;
    cfg.storage_self = storage;
    good = storage_ops->resolve(storage, 0xA11u, 2u, 2u, PE_STREET_UNKNOWN);
    bad = storage_ops->resolve(storage, 0xA12u, 2u, 2u, PE_STREET_UNKNOWN);
    CHECK(good != PE_INFOSET_ID_INVALID && bad != PE_INFOSET_ID_INVALID,
          "atomicity: infoset resolution failed");
    CHECK(pe_update_batch_soa_begin_group(
              &batch, good, 2u, 2u, &good_deltas, &good_average) == 0 &&
              pe_update_batch_soa_begin_group(
                  &batch, bad, 2u, 2u, &bad_deltas, &bad_average) == 0,
          "atomicity: group creation failed");
    if (good_deltas == NULL || bad_deltas == NULL)
        goto cleanup;
    for (i = 0u; i < 4u; ++i)
    {
        good_deltas[i] = 1.0 + (double)i;
        good_average[i] = 2.0 + (double)i;
        bad_deltas[i] = 1.0;
        bad_average[i] = 1.0;
    }
    /* One value the update rule cannot produce a finite result for. */
    bad_deltas[3] = INFINITY;
    batch.iteration = 1u;

    CHECK(ops->create(&backend, &cfg) == 0 && backend,
          "atomicity: backend creation failed");
    if (!backend)
        goto cleanup;
    CHECK(ops->apply_update_batch(backend, &batch) == -1,
          "atomicity: a non-finite update was accepted");

    stored = storage_ops->values_const(storage, good, PE_VALUES_REGRET,
                                       &length);
    CHECK(stored != NULL && length >= 4u, "atomicity: storage read failed");
    if (stored != NULL)
        for (i = 0u; i < 4u; ++i)
            CHECK(stored[i] == 0.0,
                  "atomicity: regret %zu was written by a refused batch (%f)",
                  i, stored[i]);
    stored = storage_ops->values_const(storage, good, PE_VALUES_AVERAGE,
                                       &length);
    if (stored != NULL)
        for (i = 0u; i < 4u; ++i)
            CHECK(stored[i] == 0.0,
                  "atomicity: average %zu was written by a refused batch (%f)",
                  i, stored[i]);

cleanup:
    if (backend)
        ops->destroy(backend);
    pe_update_batch_destroy(&batch);
    storage_ops->destroy(storage);
}

static void test_exponential_policy(void)
{
    const pe_compute_ops_t *ops = pe_compute_cpu_ref_ops();
    pe_compute_config_t cfg = {
        .cpu_threads = 1,
        .deterministic = 1,
        .policy_mode = PE_POLICY_EXPONENTIAL,
        .exponential_lambda = 1.0
    };
    const uint32_t offsets[] = {0u, 2u};
    const uint16_t actions[] = {2u};
    const float regrets[] = {1.0f, 0.0f};
    float strategies[2] = {0.0f, 0.0f};
    pe_infoset_batch_t input = {1u, offsets, actions, regrets};
    pe_strategy_batch_t output = {0u, 2u, NULL, strategies};
    void *backend = NULL;

    CHECK(ops->create(&backend, &cfg) == 0 && backend,
          "exponential policy backend creation failed");
    if (!backend)
        return;
    CHECK(ops->strategy_batch(backend, &input, &output) == 0,
          "exponential policy evaluation failed");
    CHECK(fabs((double)strategies[0] - 0.7310586) < 1e-5 &&
              fabs((double)strategies[1] - 0.2689414) < 1e-5,
          "exponential policy did not produce a stable softmax");
    ops->destroy(backend);
}

static void test_invalid_parallel_config(void)
{
    const pe_compute_ops_t *ops = pe_compute_cpu_ref_ops();
    pe_compute_config_t cfg = {2, 1, 0u, 0u, 0u, NULL, NULL};
    void *backend = NULL;
    CHECK(ops->create(&backend, &cfg) == -1 && backend == NULL,
          "cpu_ref accepted more than one worker");
}

static void test_terminal_batch(void)
{
    const pe_compute_ops_t *ops = pe_compute_cpu_ref_ops();
    pe_compute_config_t cfg = {1, 1, 0u, 2u, 0u, NULL, NULL};
    const uint8_t cards[] = {
        12u, 25u, 38u, 3u, 16u, 29u, 42u,
        11u, 24u, 37u, 2u, 15u, 28u, 41u
    };
    uint32_t values[2] = {0u, 0u};
    pe_terminal_batch_t input = {game_holdem, cards, NULL, NULL, 2u};
    pe_value_batch_t output = {values, 2u, 0u};
    void *backend = NULL;

    CHECK(ops->create(&backend, &cfg) == 0 && backend != NULL,
          "cpu_ref terminal backend creation failed");
    if (backend == NULL)
        return;
    CHECK(ops->terminal_eval_batch(backend, &input, &output) == 0,
          "cpu_ref terminal evaluation failed");
    CHECK(output.count == 2u && values[0] != 0u && values[1] != 0u,
          "cpu_ref terminal output is incomplete");
    ops->destroy(backend);
}

static void test_ragged_strategy_batch(void)
{
    const pe_compute_ops_t *ops = pe_compute_cpu_ref_ops();
    pe_compute_config_t cfg = {1, 1, 0u, 0u, 0u, NULL, NULL};
    const uint32_t offsets[] = {0u, 3u, 5u};
    const uint16_t actions[] = {2u, 1u};
    const float regrets[] = {2.0f, -1.0f, 9.0f, -4.0f, 8.0f};
    float strategies[5] = {-1.0f, -1.0f, -1.0f, -1.0f, -1.0f};
    pe_infoset_batch_t input = {2u, offsets, actions, regrets};
    pe_strategy_batch_t output = {0u, 5u, NULL, strategies};
    void *backend = NULL;

    CHECK(ops->create(&backend, &cfg) == 0 && backend != NULL,
          "cpu_ref strategy backend creation failed");
    if (backend == NULL)
        return;
    CHECK(ops->strategy_batch(backend, &input, &output) == 0,
          "cpu_ref ragged strategy batch failed");
    CHECK(output.count == 2u && output.offsets == offsets,
          "strategy batch metadata was not returned");
    CHECK(strategies[0] == 1.0f && strategies[1] == 0.0f &&
              strategies[2] == 0.0f && strategies[3] == 1.0f &&
              strategies[4] == 0.0f,
          "ragged regret matching produced an invalid strategy");
    ops->destroy(backend);
}

int main(void)
{
    test_reference_contract();
    test_regret_and_average_modes();
    test_soa_update_reaches_storage();
    test_failed_batch_leaves_storage_untouched();
    test_exponential_policy();
    test_invalid_parallel_config();
    test_terminal_batch();
    test_ragged_strategy_batch();
    if (failures != 0)
        return 1;
    puts("test_compute_cpu_ref: deterministic reference contract passed");
    return 0;
}
