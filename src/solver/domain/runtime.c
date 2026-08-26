/* pe_runtime.c - Runtime backend/SIMD capability discovery. */

#include <poker_eval/solver/pe_runtime.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

static int gpu_parity_gate_disabled(void)
{
    const char *value = getenv("PE_GPU_SKIP_PARITY");

    return value != NULL &&
           (strcmp(value, "1") == 0 || strcmp(value, "true") == 0 ||
            strcmp(value, "TRUE") == 0 || strcmp(value, "yes") == 0 ||
            strcmp(value, "YES") == 0);
}

static int validate_gpu_backend(const pe_compute_ops_t *gpu_ops)
{
    const pe_compute_ops_t *cpu_ops = pe_compute_cpu_ref_ops();
    const pe_storage_ops_t *storage_ops = pe_storage_ram_ops();
    pe_compute_config_t config;
    pe_compute_config_t cpu_config;
    pe_compute_config_t gpu_config;
    pe_terminal_batch_t terminal;
    pe_infoset_batch_t infosets;
    pe_strategy_batch_t cpu_strategy;
    pe_strategy_batch_t gpu_strategy;
    pe_update_t updates[2];
    pe_update_batch_t batch;
    uint32_t offsets[] = {0u, 3u};
    uint16_t action_counts[] = {3u};
    float regrets[] = {4.0f, -2.0f, 1.0f};
    float cpu_values[3] = {0.0f, 0.0f, 0.0f};
    float gpu_values[3] = {0.0f, 0.0f, 0.0f};
    uint8_t cards[8u * 7u];
    uint32_t cpu_terminal[8u] = {0u};
    uint32_t gpu_terminal[8u] = {0u};
    void *cpu = NULL;
    void *gpu = NULL;
    void *cpu_storage = NULL;
    void *gpu_storage = NULL;
    pe_infoset_id_t cpu_id;
    pe_infoset_id_t gpu_id;
    size_t i;
    int ok = 0;

    if (gpu_ops == NULL || cpu_ops == NULL || storage_ops == NULL ||
        gpu_ops->create == NULL || gpu_ops->strategy_batch == NULL ||
        gpu_ops->apply_update_batch == NULL ||
        gpu_ops->terminal_eval_batch == NULL)
        return 0;

    memset(&config, 0, sizeof(config));
    config.cpu_threads = 1;
    config.deterministic = 1;
    config.sample_batch_size = 8u;
    config.terminal_batch_size = 8u;
    config.update_batch_size = 2u;
    if (storage_ops->create(&cpu_storage, 1u) != 0 ||
        storage_ops->create(&gpu_storage, 1u) != 0)
        goto cleanup;
    cpu_id = storage_ops->resolve(cpu_storage, UINT64_C(0xFACE), 2u, 1u,
                                  PE_STREET_UNKNOWN);
    gpu_id = storage_ops->resolve(gpu_storage, UINT64_C(0xFACE), 2u, 1u,
                                  PE_STREET_UNKNOWN);
    if (cpu_id == PE_INFOSET_ID_INVALID || gpu_id == PE_INFOSET_ID_INVALID)
        goto cleanup;

    cpu_config = config;
    gpu_config = config;
    cpu_config.storage = storage_ops;
    cpu_config.storage_self = cpu_storage;
    gpu_config.storage = storage_ops;
    gpu_config.storage_self = gpu_storage;
    if (cpu_ops->create(&cpu, &cpu_config) != 0 || cpu == NULL ||
        gpu_ops->create(&gpu, &gpu_config) != 0 || gpu == NULL)
        goto cleanup;

    infosets.count = 1u;
    infosets.offsets = offsets;
    infosets.action_counts = action_counts;
    infosets.regrets = regrets;
    cpu_strategy.count = 1u;
    cpu_strategy.capacity = 3u;
    cpu_strategy.offsets = offsets;
    cpu_strategy.strategies = cpu_values;
    gpu_strategy = cpu_strategy;
    gpu_strategy.strategies = gpu_values;
    if (cpu_ops->strategy_batch(cpu, &infosets, &cpu_strategy) != 0 ||
        gpu_ops->strategy_batch(gpu, &infosets, &gpu_strategy) != 0)
        goto cleanup;
    for (i = 0u; i < 3u; ++i)
        if (fabsf(cpu_values[i] - gpu_values[i]) > 1.0e-5f)
            goto cleanup;

    for (i = 0u; i < 8u; ++i)
    {
        size_t card;
        for (card = 0u; card < 7u; ++card)
            cards[i * 7u + card] = (uint8_t)((i * 7u + card) % 52u);
    }
    terminal.game = game_holdem;
    terminal.cards = cards;
    terminal.hole = NULL;
    terminal.board = NULL;
    terminal.count = 8u;
    {
        pe_value_batch_t cpu_output = {cpu_terminal, 8u, 0u};
        pe_value_batch_t gpu_output = {gpu_terminal, 8u, 0u};
        if (cpu_ops->terminal_eval_batch(cpu, &terminal, &cpu_output) != 0 ||
            gpu_ops->terminal_eval_batch(gpu, &terminal, &gpu_output) != 0 ||
            cpu_output.count != gpu_output.count || cpu_output.count != 8u)
            goto cleanup;
        for (i = 0u; i < 8u; ++i)
            if (cpu_terminal[i] != gpu_terminal[i])
                goto cleanup;
    }

    memset(&batch, 0, sizeof(batch));
    updates[0].infoset = cpu_id;
    updates[0].action = 0u;
    updates[0].combo = 0u;
    updates[0].delta = 1.0;
    updates[0].average_delta = 0.5;
    updates[1] = updates[0];
    updates[1].action = 1u;
    updates[1].delta = -2.0;
    updates[1].average_delta = 0.25;
    batch.items = updates;
    batch.count = 2u;
    batch.capacity = 2u;
    if (cpu_ops->apply_update_batch(cpu, &batch) != 0 ||
        gpu_ops->apply_update_batch(gpu, &batch) != 0)
        goto cleanup;
    {
        size_t cpu_length = 0u;
        size_t gpu_length = 0u;
        const double *cpu_regrets = storage_ops->values_const(
            cpu_storage, cpu_id, PE_VALUES_REGRET, &cpu_length);
        const double *gpu_regrets = storage_ops->values_const(
            gpu_storage, gpu_id, PE_VALUES_REGRET, &gpu_length);
        const double *cpu_average = storage_ops->values_const(
            cpu_storage, cpu_id, PE_VALUES_AVERAGE, NULL);
        const double *gpu_average = storage_ops->values_const(
            gpu_storage, gpu_id, PE_VALUES_AVERAGE, NULL);
        if (cpu_regrets == NULL || gpu_regrets == NULL || cpu_average == NULL ||
            gpu_average == NULL || cpu_length < 2u || gpu_length < 2u)
            goto cleanup;
        for (i = 0u; i < 2u; ++i)
            if (fabs(cpu_regrets[i] - gpu_regrets[i]) > 1.0e-5 ||
                fabs(cpu_average[i] - gpu_average[i]) > 1.0e-5)
                goto cleanup;
    }
    ok = 1;

cleanup:
    if (cpu != NULL)
        cpu_ops->destroy(cpu);
    if (gpu != NULL)
        gpu_ops->destroy(gpu);
    if (cpu_storage != NULL)
        storage_ops->destroy(cpu_storage);
    if (gpu_storage != NULL)
        storage_ops->destroy(gpu_storage);
    return ok;
}

static void set_backend(pe_runtime_backend_info_t *info,
                        pe_compute_kind_t kind, const char *name,
                        int compiled, int available, int validated,
                        uint64_t capabilities, int devices,
                        const char *reason)
{
    if (!info)
        return;
    memset(info, 0, sizeof(*info));
    info->kind = kind;
    info->compiled = compiled;
    info->runtime_available = available;
    info->validated = validated;
    info->device_count = devices;
    info->capabilities = capabilities;
    snprintf(info->name, sizeof(info->name), "%s", name ? name : "unknown");
    snprintf(info->reason, sizeof(info->reason), "%s", reason ? reason : "");
}

static void probe_adapter(pe_runtime_backend_info_t *info,
                          pe_compute_kind_t kind,
                          const pe_compute_ops_t *ops,
                          int compiled, int deterministic,
                          const char *unavailable_reason)
{
    pe_compute_config_t config;
    void *backend = NULL;
    int created;
    uint64_t capabilities = 0u;
    const char *name = ops && ops->name ? ops->name : "unknown";

    memset(&config, 0, sizeof(config));
    config.cpu_threads = 1;
    config.deterministic = deterministic;
    config.sample_batch_size = 1u;
    config.terminal_batch_size = 1u;
    config.update_batch_size = 1u;
    created = ops && ops->create ? ops->create(&backend, &config) : -1;
    if (created == 0 && backend != NULL)
    {
        if ((kind == PE_COMPUTE_CUDA || kind == PE_COMPUTE_OPENCL) &&
            !validate_gpu_backend(ops))
        {
            if (ops->destroy)
                ops->destroy(backend);
            set_backend(info, kind, name, compiled, 0, 0, 0u, 0,
                        "device/context available; CPU/GPU parity validation failed");
            return;
        }
        if (kind == PE_COMPUTE_CUDA || kind == PE_COMPUTE_OPENCL)
        {
            if (gpu_parity_gate_disabled())
            {
                if (ops->destroy)
                    ops->destroy(backend);
                set_backend(info, kind, name, compiled, 0, 1, 0u, 0,
                            "GPU parity passed; gate disabled by PE_GPU_SKIP_PARITY");
                return;
            }
            /* Runtime probing is the production opener.  The validation
             * above exercises strategy, update and terminal paths against
             * cpu_ref before either GPU capability becomes visible. */
            pe_gpu_terminal_eval_gate_open();
            pe_gpu_regret_update_gate_open();
        }
        capabilities = ops->capabilities ? ops->capabilities(backend) : 0u;
        if (ops->destroy)
            ops->destroy(backend);
        /* A GPU context can be created even when the adapter is still
         * behind the solver's parity gate.  Do not advertise that as a
         * usable solver backend: the frontends must refuse it instead of
         * silently producing a CPU result. */
        if ((kind == PE_COMPUTE_CUDA || kind == PE_COMPUTE_OPENCL) &&
            capabilities == 0u)
        {
            set_backend(info, kind, name, compiled, 1, 0, capabilities, 1,
                        "device/context available; GPU parity gate is closed");
        }
        else
        {
            set_backend(info, kind, name, compiled, 1, 1, capabilities, 1,
                        "ready");
        }
    }
    else
    {
        if (backend && ops && ops->destroy)
            ops->destroy(backend);
        set_backend(info, kind, name, compiled, 0, 0, 0u, 0,
                    unavailable_reason);
    }
}

int pe_runtime_probe(pe_runtime_capabilities_t *out)
{
    uint32_t cpus = 1u;
    if (!out)
        return -1;
    memset(out, 0, sizeof(*out));

#if defined(_WIN32)
    {
        SYSTEM_INFO system_info;
        GetSystemInfo(&system_info);
        if (system_info.dwNumberOfProcessors > 0u)
            cpus = (uint32_t)system_info.dwNumberOfProcessors;
    }
#else
    {
        long value = sysconf(_SC_NPROCESSORS_ONLN);
        if (value > 0)
            cpus = (uint32_t)value;
    }
#endif
    out->logical_cpus = cpus;
    out->simd_machine = simd_detect_capability();
    out->simd_compiled = simd_compiled_capability();
    out->simd = simd_runtime_capability();
#if defined(_OPENMP)
    out->openmp_available = 1;
#else
    out->openmp_available = 0;
#endif

    probe_adapter(&out->backends[PE_COMPUTE_CPU_REF], PE_COMPUTE_CPU_REF,
                  pe_compute_cpu_ref_ops(), 1, 1,
                  "reference adapter could not be created");
    probe_adapter(&out->backends[PE_COMPUTE_CPU_PAR], PE_COMPUTE_CPU_PAR,
                  pe_compute_cpu_par_ops(), 1, 1,
                  out->openmp_available
                      ? "parallel adapter could not be created"
                      : "OpenMP is not compiled; parallel adapter is unavailable");
    if (!out->openmp_available &&
        out->backends[PE_COMPUTE_CPU_PAR].runtime_available)
    {
        /* cpu_par is a parallel backend contract, not merely a second name
         * for the scalar adapter.  The adapter can be instantiated without
         * OpenMP, but advertising that instance as available makes the UI
         * offer a backend that cannot deliver parallel execution. */
        out->backends[PE_COMPUTE_CPU_PAR].runtime_available = 0;
        out->backends[PE_COMPUTE_CPU_PAR].validated = 0;
        out->backends[PE_COMPUTE_CPU_PAR].device_count = 0;
        snprintf(out->backends[PE_COMPUTE_CPU_PAR].reason,
                 sizeof(out->backends[PE_COMPUTE_CPU_PAR].reason),
                 "OpenMP is not compiled; CPU_PAR is unavailable (parallel adapter requires OpenMP)");
    }

#if defined(PE_RUNTIME_CUDA_COMPILED)
    probe_adapter(&out->backends[PE_COMPUTE_CUDA], PE_COMPUTE_CUDA,
                  pe_compute_cuda_ops(), 1, 0,
                  "CUDA adapter compiled but no usable device/context");
#else
    set_backend(&out->backends[PE_COMPUTE_CUDA], PE_COMPUTE_CUDA,
                "cuda", 0, 0, 0, 0u, 0,
                "CUDA adapter is not compiled in this build");
#endif

#if defined(PE_RUNTIME_OPENCL_COMPILED)
    probe_adapter(&out->backends[PE_COMPUTE_OPENCL], PE_COMPUTE_OPENCL,
                  pe_compute_opencl_ops(), 1, 0,
                  "OpenCL adapter compiled but no usable device/context");
#else
    set_backend(&out->backends[PE_COMPUTE_OPENCL], PE_COMPUTE_OPENCL,
                "opencl", 0, 0, 0, 0u, 0,
                "OpenCL adapter is not compiled in this build");
#endif

    set_backend(&out->backends[PE_COMPUTE_AUTO], PE_COMPUTE_AUTO, "auto", 1,
                1, 1, out->backends[PE_COMPUTE_CPU_REF].capabilities, 0,
                "resolved by the solver plan");
    return 0;
}

pe_compute_kind_t pe_runtime_recommended_backend(
    const pe_runtime_capabilities_t *runtime)
{
    const pe_runtime_backend_info_t *parallel;
    const pe_runtime_backend_info_t *reference;

    if (!runtime)
        return PE_COMPUTE_AUTO;

    parallel = &runtime->backends[PE_COMPUTE_CPU_PAR];
    if (runtime->openmp_available && parallel->runtime_available &&
        parallel->validated)
        return PE_COMPUTE_CPU_PAR;

    reference = &runtime->backends[PE_COMPUTE_CPU_REF];
    if (reference->runtime_available && reference->validated)
        return PE_COMPUTE_CPU_REF;

    return PE_COMPUTE_AUTO;
}

const char *pe_runtime_simd_name(simd_capability_t capability)
{
    const char *name = simd_capability_name(capability);
    return name ? name : "unknown";
}

int pe_runtime_backend_status(const pe_runtime_backend_info_t *backend,
                              char *out, size_t capacity)
{
    int written;
    if (!backend || !out || capacity == 0u)
        return -1;
    written = snprintf(out, capacity,
                       "%s: %s%s%s (%s)",
                       backend->name,
                       backend->runtime_available ? "available" : "unavailable",
                       backend->validated ? ", validated" : "",
                       backend->compiled ? "" : ", not compiled",
                       backend->reason);
    return written < 0 ? -1 : written;
}
