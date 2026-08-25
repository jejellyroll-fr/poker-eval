/* pe_runtime.c - Runtime backend/SIMD capability discovery. */

#include <poker_eval/solver/pe_runtime.h>

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

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
    out->simd = simd_detect_capability();
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
                      : "OpenMP is not compiled; adapter is single-worker");
    if (!out->openmp_available &&
        out->backends[PE_COMPUTE_CPU_PAR].runtime_available)
    {
        snprintf(out->backends[PE_COMPUTE_CPU_PAR].reason,
                 sizeof(out->backends[PE_COMPUTE_CPU_PAR].reason),
                 "adapter ready; OpenMP is not compiled, single-worker");
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
