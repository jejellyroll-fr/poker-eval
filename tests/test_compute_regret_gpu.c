/*
 * test_compute_regret_gpu.c - GPU-02/GPU-03: the regret-only device backends.
 *
 * HIP and Metal carry the ragged strategy and regret-update kernels but no
 * batched terminal evaluator, so the GPU-05 terminal parity test cannot cover
 * them. This one compares what they do implement against cpu_ref, on a batch
 * with the shapes that have historically been where device kernels differ:
 * ragged spans with padding beyond the live actions, an all-negative infoset
 * that must fall back to a uniform strategy, and duplicate slots in one update
 * batch.
 *
 * A backend that cannot be created is skipped, not failed: this test runs on
 * machines with neither device.
 */

#include <poker_eval/solver/pe_compute.h>
#include <poker_eval/solver/pe_storage_port.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;
static int exercised;

#define CHECK(condition, ...)                                      \
    do                                                             \
    {                                                              \
        if (!(condition))                                          \
        {                                                          \
            fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                          \
            fputc('\n', stderr);                                   \
            failures++;                                            \
        }                                                          \
    } while (0)

/* Three infosets: two live actions with one padding slot, three live actions,
   and two actions whose regrets are both negative. */
static const uint32_t k_offsets[] = {0u, 3u, 6u, 8u};
static const uint16_t k_actions[] = {2u, 3u, 2u};
static const float k_regrets[] = {
    4.0f, 1.0f, 7.0f,      /* the 7.0f is padding and must be ignored     */
    3.0f, -1.0f, 1.0f,
    -2.0f, -5.0f           /* all negative: uniform strategy expected     */
};
#define K_VALUES 8u

static int strategy_parity(const pe_compute_ops_t *gpu_ops, void *gpu,
                           const pe_compute_ops_t *cpu_ops, void *cpu,
                           const char *name)
{
    float cpu_out[K_VALUES];
    float gpu_out[K_VALUES];
    pe_infoset_batch_t in;
    pe_strategy_batch_t cpu_batch;
    pe_strategy_batch_t gpu_batch;
    size_t i;

    memset(cpu_out, 0, sizeof(cpu_out));
    memset(gpu_out, 0, sizeof(gpu_out));
    in.count = 3u;
    in.offsets = k_offsets;
    in.action_counts = k_actions;
    in.regrets = k_regrets;
    cpu_batch.count = 0u;
    cpu_batch.capacity = K_VALUES;
    cpu_batch.offsets = k_offsets;
    cpu_batch.strategies = cpu_out;
    gpu_batch = cpu_batch;
    gpu_batch.strategies = gpu_out;

    if (cpu_ops->strategy_batch(cpu, &in, &cpu_batch) != 0)
    {
        CHECK(0, "%s: cpu_ref refused the strategy batch", name);
        return -1;
    }
    if (gpu_ops->strategy_batch(gpu, &in, &gpu_batch) != 0)
    {
        CHECK(0, "%s: strategy batch was refused", name);
        return -1;
    }
    for (i = 0u; i < K_VALUES; ++i)
        CHECK(fabsf(cpu_out[i] - gpu_out[i]) <= 1.0e-6f,
              "%s: strategy %zu is %f, cpu_ref says %f",
              name, i, (double)gpu_out[i], (double)cpu_out[i]);
    /* Padding must be cleared, not left holding the input regret. */
    CHECK(gpu_out[2] == 0.0f, "%s: padding slot was not cleared (%f)",
          name, (double)gpu_out[2]);
    /* An all-negative infoset is uniform over its live actions. */
    CHECK(fabsf(gpu_out[6] - 0.5f) <= 1.0e-6f &&
              fabsf(gpu_out[7] - 0.5f) <= 1.0e-6f,
          "%s: all-negative infoset is not uniform (%f, %f)",
          name, (double)gpu_out[6], (double)gpu_out[7]);
    return 0;
}

static int update_parity(const pe_compute_ops_t *gpu_ops, void *gpu,
                         const pe_compute_ops_t *cpu_ops, void *cpu,
                         const pe_storage_ops_t *storage_ops,
                         void *cpu_storage, void *gpu_storage,
                         pe_infoset_id_t cpu_id, pe_infoset_id_t gpu_id,
                         const char *name)
{
    /* Slot (0, 0) appears twice. Note where that is resolved: the host pack
       in compute_gpu_updates.c reduces duplicate slots before any dispatch,
       so the device kernel does not see them and its atomic add is defensive
       only. What this checks is therefore the composed path -- reduce, then
       apply -- which is what the solver actually runs. */
    pe_update_t updates[3];
    pe_update_batch_t batch;
    const double *cpu_regret;
    const double *gpu_regret;
    const double *cpu_average;
    const double *gpu_average;
    size_t length = 0u;
    size_t i;

    memset(&batch, 0, sizeof(batch));
    memset(updates, 0, sizeof(updates));
    updates[0].infoset = cpu_id;
    updates[0].action = 0u;
    updates[0].combo = 0u;
    updates[0].delta = 1.5;
    updates[0].average_delta = 0.25;
    updates[1].infoset = cpu_id;
    updates[1].action = 1u;
    updates[1].combo = 0u;
    updates[1].delta = -0.5;
    updates[1].average_delta = 0.5;
    updates[2] = updates[0];
    updates[2].delta = 2.0;
    updates[2].average_delta = 0.25;
    batch.items = updates;
    batch.count = 3u;
    batch.capacity = 3u;
    batch.iteration = 1u;

    if (cpu_ops->apply_update_batch(cpu, &batch) != 0)
    {
        CHECK(0, "%s: cpu_ref refused the update batch", name);
        return -1;
    }
    for (i = 0u; i < 3u; ++i)
        updates[i].infoset = gpu_id;
    if (gpu_ops->apply_update_batch(gpu, &batch) != 0)
    {
        CHECK(0, "%s: update batch was refused", name);
        return -1;
    }
    cpu_regret = storage_ops->values_const(cpu_storage, cpu_id,
                                           PE_VALUES_REGRET, &length);
    gpu_regret = storage_ops->values_const(gpu_storage, gpu_id,
                                           PE_VALUES_REGRET, NULL);
    cpu_average = storage_ops->values_const(cpu_storage, cpu_id,
                                            PE_VALUES_AVERAGE, NULL);
    gpu_average = storage_ops->values_const(gpu_storage, gpu_id,
                                            PE_VALUES_AVERAGE, NULL);
    if (cpu_regret == NULL || gpu_regret == NULL || cpu_average == NULL ||
        gpu_average == NULL || length < 2u)
    {
        CHECK(0, "%s: storage read failed", name);
        return -1;
    }
    for (i = 0u; i < 2u; ++i)
    {
        CHECK(fabs(cpu_regret[i] - gpu_regret[i]) <= 1.0e-5,
              "%s: regret %zu is %f, cpu_ref says %f",
              name, i, gpu_regret[i], cpu_regret[i]);
        CHECK(fabs(cpu_average[i] - gpu_average[i]) <= 1.0e-5,
              "%s: average %zu is %f, cpu_ref says %f",
              name, i, gpu_average[i], cpu_average[i]);
    }
    /* Stated absolutely as well as by parity, so that a change making both
       sides equally wrong still fails. */
    CHECK(fabs(gpu_regret[0] - 3.5) <= 1.0e-5,
          "%s: duplicate slots did not accumulate (%f, expected 3.5)",
          name, gpu_regret[0]);
    return 0;
}

static void run_backend(const pe_compute_ops_t *gpu_ops, const char *name)
{
    const pe_compute_ops_t *cpu_ops = pe_compute_cpu_ref_ops();
    const pe_storage_ops_t *storage_ops = pe_storage_ram_ops();
    pe_compute_config_t cfg;
    void *cpu = NULL;
    void *gpu = NULL;
    void *cpu_storage = NULL;
    void *gpu_storage = NULL;
    pe_infoset_id_t cpu_id;
    pe_infoset_id_t gpu_id;

    if (gpu_ops == NULL || gpu_ops->create == NULL)
        return;
    memset(&cfg, 0, sizeof(cfg));
    cfg.cpu_threads = 1;
    cfg.deterministic = 1;
    cfg.sample_batch_size = 1u;
    cfg.terminal_batch_size = 8u;
    cfg.update_batch_size = 4u;
    if (storage_ops->create(&cpu_storage, 1u) != 0 ||
        storage_ops->create(&gpu_storage, 1u) != 0)
        goto cleanup;
    cpu_id = storage_ops->resolve(cpu_storage, UINT64_C(0xBEEF), 2u, 1u,
                                  PE_STREET_UNKNOWN);
    gpu_id = storage_ops->resolve(gpu_storage, UINT64_C(0xBEEF), 2u, 1u,
                                  PE_STREET_UNKNOWN);
    if (cpu_id == PE_INFOSET_ID_INVALID || gpu_id == PE_INFOSET_ID_INVALID)
        goto cleanup;

    cfg.storage = storage_ops;
    cfg.storage_self = gpu_storage;
    if (gpu_ops->create(&gpu, &cfg) != 0 || gpu == NULL)
    {
        printf("test_compute_regret_gpu: %s unavailable, skipped\n", name);
        goto cleanup;
    }
    cfg.storage_self = cpu_storage;
    if (cpu_ops->create(&cpu, &cfg) != 0 || cpu == NULL)
    {
        CHECK(0, "%s: cpu_ref creation failed", name);
        goto cleanup;
    }
    exercised++;
    if (strategy_parity(gpu_ops, gpu, cpu_ops, cpu, name) == 0)
        (void)update_parity(gpu_ops, gpu, cpu_ops, cpu, storage_ops,
                            cpu_storage, gpu_storage, cpu_id, gpu_id, name);
    if (gpu_ops->sync != NULL)
        CHECK(gpu_ops->sync(gpu) == 0, "%s: sync failed", name);
    printf("test_compute_regret_gpu: %s matched cpu_ref\n", name);

cleanup:
    if (cpu != NULL)
        cpu_ops->destroy(cpu);
    if (gpu != NULL)
        gpu_ops->destroy(gpu);
    if (cpu_storage != NULL)
        storage_ops->destroy(cpu_storage);
    if (gpu_storage != NULL)
        storage_ops->destroy(gpu_storage);
}

int main(void)
{
    run_backend(pe_compute_hip_ops(), "hip");
    run_backend(pe_compute_metal_ops(), "metal");
    if (failures != 0)
        return 1;
    if (exercised == 0)
    {
        puts("test_compute_regret_gpu: no HIP or Metal device available");
        return 77; /* CTest SKIP */
    }
    puts("test_compute_regret_gpu: all regret backends matched cpu_ref");
    return 0;
}
