/* pe_runtime.c - Runtime backend/SIMD capability discovery. */

#include <poker_eval/solver/pe_runtime.h>
#include <poker_eval/core/time_compat.h>

#include <math.h>
#include <stdarg.h>
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

static double runtime_backend_rate(const pe_runtime_backend_info_t *backend)
{
    double rate;

    if (!backend)
        return 0.0;
    rate = backend->terminal_elements_per_s;
    if (!(rate > 0.0) || !isfinite(rate))
        rate = backend->update_elements_per_s;
    if (!(rate > 0.0) || !isfinite(rate))
        rate = backend->strategy_elements_per_s;
    return rate > 0.0 && isfinite(rate) ? rate : 0.0;
}

static uint64_t runtime_clock_ns(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0u;
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) +
           (uint64_t)ts.tv_nsec;
}

static double measure_terminal_rate(const pe_compute_ops_t *ops, void *backend,
                                    size_t batch_size)
{
    pe_terminal_batch_t terminal;
    pe_value_batch_t output;
    uint8_t *cards;
    uint32_t *values;
    uint64_t start;
    uint64_t end;
    uint64_t elapsed;
    size_t index;
    size_t card;
    size_t repeat;
    const size_t repeats = 3u;
    const size_t card_count = 7u;

    if (ops == NULL || backend == NULL || ops->terminal_eval_batch == NULL ||
        batch_size == 0u || batch_size > SIZE_MAX / card_count)
        return 0.0;
    cards = (uint8_t *)malloc(batch_size * card_count);
    values = (uint32_t *)malloc(batch_size * sizeof(*values));
    if (cards == NULL || values == NULL)
    {
        free(cards);
        free(values);
        return 0.0;
    }
    for (index = 0u; index < batch_size; ++index)
        for (card = 0u; card < card_count; ++card)
            cards[index * card_count + card] =
                (uint8_t)((index * card_count + card) % 52u);

    terminal.game = game_holdem;
    terminal.cards = cards;
    terminal.hole = NULL;
    terminal.board = NULL;
    terminal.count = batch_size;
    output.values = values;
    output.capacity = batch_size;
    output.count = 0u;
    /* Warm up lazy device/context initialization before recording a rate. */
    if (ops->terminal_eval_batch(backend, &terminal, &output) != 0 ||
        output.count != batch_size)
    {
        free(values);
        free(cards);
        return 0.0;
    }
    start = runtime_clock_ns();
    if (start == 0u)
    {
        free(values);
        free(cards);
        return 0.0;
    }
    for (repeat = 0u; repeat < repeats; ++repeat)
    {
        output.count = 0u;
        if (ops->terminal_eval_batch(backend, &terminal, &output) != 0 ||
            output.count != batch_size)
        {
            free(values);
            free(cards);
            return 0.0;
        }
    }
    if (ops->sync != NULL && ops->sync(backend) != 0)
    {
        free(values);
        free(cards);
        return 0.0;
    }
    end = runtime_clock_ns();
    free(values);
    free(cards);
    if (end == 0u || end <= start)
        return 0.0;
    elapsed = end - start;
    if (elapsed == 0u)
        return 0.0;
    return ((double)batch_size * (double)repeats * 1.0e9) /
           (double)elapsed;
}

static size_t calibrate_terminal_min_batch(const pe_compute_ops_t *gpu_ops,
                                           void *gpu_backend,
                                           const pe_compute_config_t *gpu_config)
{
    static const size_t batch_sizes[] = {1u, 8u, 32u, 128u, 256u};
    const pe_compute_ops_t *cpu_ops = pe_compute_cpu_ref_ops();
    pe_compute_config_t cpu_config;
    void *cpu_backend = NULL;
    size_t i;
    size_t threshold = 0u;

    if (gpu_ops == NULL || gpu_backend == NULL || gpu_config == NULL ||
        cpu_ops == NULL || cpu_ops->create == NULL ||
        gpu_ops->terminal_eval_batch == NULL)
        return 0u;
    cpu_config = *gpu_config;
    cpu_config.cpu_threads = 1;
    cpu_config.deterministic = 1;
    if (cpu_ops->create(&cpu_backend, &cpu_config) != 0 || cpu_backend == NULL)
        return 0u;
    for (i = 0u; i < sizeof(batch_sizes) / sizeof(batch_sizes[0]); ++i)
    {
        double cpu_rate = measure_terminal_rate(cpu_ops, cpu_backend,
                                                batch_sizes[i]);
        double gpu_rate = measure_terminal_rate(gpu_ops, gpu_backend,
                                                batch_sizes[i]);
        /* Keep a small margin so timer noise does not make AUTO oscillate
         * around the crossover point from one probe to the next. */
        if (cpu_rate > 0.0 && gpu_rate > cpu_rate * 1.05)
        {
            threshold = batch_sizes[i];
            break;
        }
    }
    if (cpu_ops->destroy)
        cpu_ops->destroy(cpu_backend);
    return threshold;
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
    double terminal_rate = 0.0;
    size_t terminal_min_batch_size = 0u;
    const char *name = ops && ops->name ? ops->name : "unknown";

    memset(&config, 0, sizeof(config));
    config.cpu_threads = 1;
    config.deterministic = deterministic;
    config.sample_batch_size = 1u;
    config.terminal_batch_size = 256u;
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
        terminal_rate = measure_terminal_rate(ops, backend,
                                              config.terminal_batch_size);
        if ((kind == PE_COMPUTE_CUDA || kind == PE_COMPUTE_OPENCL) &&
            (capabilities & PE_CAP_GPU_TERMINAL_EVAL))
            terminal_min_batch_size = calibrate_terminal_min_batch(
                ops, backend, &config);
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
            info->terminal_elements_per_s = terminal_rate;
            info->terminal_min_batch_size = terminal_min_batch_size;
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

pe_compute_kind_t pe_runtime_recommended_backend_for_batch(
    const pe_runtime_capabilities_t *runtime, size_t terminal_batch_size)
{
    const pe_runtime_backend_info_t *parallel;
    const pe_runtime_backend_info_t *reference;
    pe_compute_kind_t best;
    double best_rate;
    size_t i;

    if (!runtime)
        return PE_COMPUTE_AUTO;

    parallel = &runtime->backends[PE_COMPUTE_CPU_PAR];
    if (runtime->openmp_available && parallel->runtime_available &&
        parallel->validated)
    {
        best = PE_COMPUTE_CPU_PAR;
        best_rate = runtime_backend_rate(parallel);
    }
    else
    {
        best = PE_COMPUTE_AUTO;
        best_rate = 0.0;
    }

    reference = &runtime->backends[PE_COMPUTE_CPU_REF];
    if (reference->runtime_available && reference->validated &&
        best == PE_COMPUTE_AUTO)
    {
        best = PE_COMPUTE_CPU_REF;
        best_rate = runtime_backend_rate(reference);
    }

    /* GPU selection is permitted only after parity validation and only when
     * an advertised measured rate beats the best CPU candidate. */
    for (i = 1u; i < PE_COMPUTE_COUNT; ++i)
    {
        const pe_runtime_backend_info_t *candidate = &runtime->backends[i];
        double rate;
        if ((candidate->kind != PE_COMPUTE_CUDA &&
             candidate->kind != PE_COMPUTE_OPENCL) ||
            !candidate->compiled || !candidate->runtime_available ||
            !candidate->validated ||
            !(candidate->capabilities & PE_CAP_GPU_TERMINAL_EVAL) ||
            (terminal_batch_size != 0u &&
             candidate->terminal_min_batch_size != 0u &&
             terminal_batch_size < candidate->terminal_min_batch_size))
            continue;
        rate = runtime_backend_rate(candidate);
        if (best != PE_COMPUTE_AUTO && best_rate > 0.0 && rate > best_rate)
        {
            best = candidate->kind;
            best_rate = rate;
        }
    }

    return best;
}

pe_compute_kind_t pe_runtime_recommended_backend(
    const pe_runtime_capabilities_t *runtime)
{
    return pe_runtime_recommended_backend_for_batch(runtime, 0u);
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
                       "%s: %s%s%s (strategy=%.3f/s update=%.3f/s terminal=%.3f/s terminal_min_batch=%zu; %s)",
                       backend->name,
                       backend->runtime_available ? "available" : "unavailable",
                       backend->validated ? ", validated" : "",
                       backend->compiled ? "" : ", not compiled",
                       backend->strategy_elements_per_s,
                       backend->update_elements_per_s,
                       backend->terminal_elements_per_s,
                       backend->terminal_min_batch_size,
                       backend->reason);
    return written < 0 ? -1 : written;
}

#if defined(__GNUC__) || defined(__clang__)
#define PE_RUNTIME_PRINTF_LIKE(a, b) __attribute__((format(printf, a, b)))
#else
#define PE_RUNTIME_PRINTF_LIKE(a, b)
#endif

static void runtime_descriptor_appendf(
    char *out, size_t capacity, size_t *position, const char *format, ...)
    PE_RUNTIME_PRINTF_LIKE(4, 5);

static void runtime_descriptor_appendf(char *out, size_t capacity,
                                       size_t *position, const char *format, ...)
{
    va_list args;
    int needed;

    va_start(args, format);
    needed = vsnprintf(NULL, 0u, format, args);
    va_end(args);
    if (needed <= 0)
        return;
    if (out != NULL && *position < capacity)
    {
        va_start(args, format);
        (void)vsnprintf(out + *position, capacity - *position, format, args);
        va_end(args);
    }
    *position += (size_t)needed;
}

size_t pe_runtime_descriptor_to_string(
    const pe_runtime_capabilities_t *runtime, char *out, size_t capacity)
{
    size_t position = 0u;
    size_t i;

    if (runtime == NULL || (out == NULL && capacity != 0u) ||
        runtime->logical_cpus == 0u || runtime->openmp_available < 0 ||
        runtime->openmp_available > 1 || runtime->simd_machine < SIMD_NONE ||
        runtime->simd_machine > SIMD_NEON || runtime->simd_compiled < SIMD_NONE ||
        runtime->simd_compiled > SIMD_NEON || runtime->simd < SIMD_NONE ||
        runtime->simd > SIMD_NEON)
        return 0u;

    runtime_descriptor_appendf(
        out, capacity, &position,
        "PE_RUNTIME_V%u;cpus=%u;openmp=%d;simd_machine=%d;simd_compiled=%d;simd=%d",
        PE_RUNTIME_DESCRIPTOR_VERSION, runtime->logical_cpus,
        runtime->openmp_available, (int)runtime->simd_machine,
        (int)runtime->simd_compiled, (int)runtime->simd);
    for (i = 0u; i < PE_COMPUTE_COUNT; ++i)
    {
        const pe_runtime_backend_info_t *backend = &runtime->backends[i];
        if (backend->kind != (pe_compute_kind_t)i ||
            backend->compiled < 0 || backend->compiled > 1 ||
            backend->runtime_available < 0 || backend->runtime_available > 1 ||
            backend->validated < 0 || backend->validated > 1 ||
            backend->device_count < 0 ||
            !isfinite(backend->strategy_elements_per_s) ||
            !isfinite(backend->update_elements_per_s) ||
            !isfinite(backend->terminal_elements_per_s) ||
            backend->strategy_elements_per_s < 0.0 ||
            backend->update_elements_per_s < 0.0 ||
            backend->terminal_elements_per_s < 0.0)
            return 0u;
        runtime_descriptor_appendf(
            out, capacity, &position,
            ";b%zu=%d,%d,%d,%d,0x%016llx,%a,%a,%a,%zu", i,
            backend->compiled, backend->runtime_available, backend->validated,
            backend->device_count,
            (unsigned long long)backend->capabilities,
            backend->strategy_elements_per_s,
            backend->update_elements_per_s,
            backend->terminal_elements_per_s,
            backend->terminal_min_batch_size);
    }
    if (out != NULL && capacity != 0u)
        out[position < capacity ? position : capacity - 1u] = '\0';
    return position;
}

static int runtime_descriptor_parse_int(const char *value, int *out)
{
    char extra;
    return value != NULL && out != NULL && sscanf(value, "%d%c", out, &extra) == 1;
}

static int runtime_descriptor_parse_uint(const char *value, unsigned *out)
{
    char extra;
    return value != NULL && out != NULL &&
           sscanf(value, "%u%c", out, &extra) == 1;
}

int pe_runtime_descriptor_from_string(
    const char *text, pe_runtime_capabilities_t *out)
{
    pe_runtime_capabilities_t parsed;
    unsigned seen = 0u;
    unsigned backend_seen = 0u;
    const char *cursor;

    if (text == NULL || out == NULL ||
        strncmp(text, "PE_RUNTIME_V1;", 14u) != 0)
        return -1;
    memset(&parsed, 0, sizeof(parsed));
    cursor = text + 14u;
    while (*cursor != '\0')
    {
        const char *end = strchr(cursor, ';');
        size_t length = end != NULL ? (size_t)(end - cursor) : strlen(cursor);
        char token[512];
        char *equals;
        const char *value;

        if (length == 0u || length >= sizeof(token))
            return -1;
        memcpy(token, cursor, length);
        token[length] = '\0';
        equals = strchr(token, '=');
        if (equals == NULL || equals == token)
            return -1;
        *equals = '\0';
        value = equals + 1;
        if (strcmp(token, "cpus") == 0)
        {
            if ((seen & (1u << 0)) != 0u ||
                !runtime_descriptor_parse_uint(value, &parsed.logical_cpus))
                return -1;
            seen |= 1u << 0;
        }
        else if (strcmp(token, "openmp") == 0)
        {
            int parsed_value;
            if ((seen & (1u << 1)) != 0u ||
                !runtime_descriptor_parse_int(value, &parsed_value) ||
                parsed_value < 0 || parsed_value > 1)
                return -1;
            parsed.openmp_available = parsed_value;
            seen |= 1u << 1;
        }
        else if (strcmp(token, "simd_machine") == 0 ||
                 strcmp(token, "simd_compiled") == 0 ||
                 strcmp(token, "simd") == 0)
        {
            int parsed_value;
            unsigned bit = strcmp(token, "simd_machine") == 0 ? 2u :
                           strcmp(token, "simd_compiled") == 0 ? 3u : 4u;
            if ((seen & (1u << bit)) != 0u ||
                !runtime_descriptor_parse_int(value, &parsed_value) ||
                parsed_value < SIMD_NONE || parsed_value > SIMD_NEON)
                return -1;
            if (bit == 2u)
                parsed.simd_machine = (simd_capability_t)parsed_value;
            else if (bit == 3u)
                parsed.simd_compiled = (simd_capability_t)parsed_value;
            else
                parsed.simd = (simd_capability_t)parsed_value;
            seen |= 1u << bit;
        }
        else if (token[0] == 'b')
        {
            char *index_end;
            unsigned long index = strtoul(token + 1, &index_end, 10);
            int compiled;
            int available;
            int validated;
            int devices;
            unsigned long long capabilities;
            double strategy_rate;
            double update_rate;
            double terminal_rate;
            size_t terminal_min_batch_size = 0u;
            char extra;
            int scanned;
            int old_scanned;

            if (index_end == token + 1 || *index_end != '\0')
                return -1;
            /* Unknown future backend slots are intentionally ignored. */
            if (index >= PE_COMPUTE_COUNT)
            {
                cursor = end != NULL ? end + 1 : cursor + length;
                continue;
            }
            scanned = sscanf(value, "%d,%d,%d,%d,%llx,%la,%la,%la,%zu%c",
                             &compiled, &available, &validated, &devices,
                             &capabilities, &strategy_rate, &update_rate,
                             &terminal_rate, &terminal_min_batch_size, &extra);
            old_scanned = sscanf(value, "%d,%d,%d,%d,%llx,%la,%la,%la%c",
                                 &compiled, &available, &validated, &devices,
                                 &capabilities, &strategy_rate, &update_rate,
                                 &terminal_rate, &extra);
            if ((backend_seen & (1u << index)) != 0u ||
                (scanned != 9 && old_scanned != 8) ||
                compiled < 0 || compiled > 1 || available < 0 || available > 1 ||
                validated < 0 || validated > 1 || devices < 0 ||
                !isfinite(strategy_rate) || !isfinite(update_rate) ||
                !isfinite(terminal_rate) || strategy_rate < 0.0 ||
                update_rate < 0.0 || terminal_rate < 0.0)
                return -1;
            parsed.backends[index].kind = (pe_compute_kind_t)index;
            parsed.backends[index].compiled = compiled;
            parsed.backends[index].runtime_available = available;
            parsed.backends[index].validated = validated;
            parsed.backends[index].device_count = devices;
            parsed.backends[index].capabilities = (uint64_t)capabilities;
            parsed.backends[index].strategy_elements_per_s = strategy_rate;
            parsed.backends[index].update_elements_per_s = update_rate;
            parsed.backends[index].terminal_elements_per_s = terminal_rate;
            parsed.backends[index].terminal_min_batch_size =
                scanned == 9 ? terminal_min_batch_size : 0u;
            snprintf(parsed.backends[index].name,
                     sizeof(parsed.backends[index].name), "%s",
                     pe_compute_kind_name((pe_compute_kind_t)index));
            backend_seen |= 1u << index;
        }
        cursor = end != NULL ? end + 1 : cursor + length;
    }
    if (parsed.logical_cpus == 0u || seen != 0x1Fu ||
        backend_seen != ((1u << PE_COMPUTE_COUNT) - 1u))
        return -1;
    *out = parsed;
    return 0;
}
