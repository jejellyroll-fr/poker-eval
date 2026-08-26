#include <poker_eval/solver/pe_runtime.h>
#include <poker_eval/solver/pe_solver_plan.h>

#include <stdio.h>
#include <string.h>

int main(void)
{
    pe_runtime_capabilities_t runtime;
    pe_runtime_capabilities_t synthetic;
    pe_runtime_capabilities_t wire;
    pe_runtime_capabilities_t decoded;
    char status[256];
    char descriptor[PE_RUNTIME_DESCRIPTOR_MAX];
    size_t descriptor_length;
    if (pe_runtime_probe(&runtime) != 0)
        return 1;
    if (runtime.logical_cpus == 0u || runtime.simd_machine < SIMD_NONE ||
        runtime.simd_machine > SIMD_NEON || runtime.simd_compiled < SIMD_NONE ||
        runtime.simd_compiled > SIMD_NEON || runtime.simd < SIMD_NONE ||
        runtime.simd > SIMD_NEON)
        return 2;
    if (!runtime.backends[PE_COMPUTE_CPU_REF].runtime_available ||
        !runtime.backends[PE_COMPUTE_CPU_REF].validated)
        return 3;
    if (pe_runtime_backend_status(&runtime.backends[PE_COMPUTE_CPU_REF],
                                  status, sizeof(status)) < 0 ||
        strstr(status, "available") == NULL)
        return 4;
    if (pe_runtime_simd_name(runtime.simd) == NULL ||
        pe_runtime_simd_name(runtime.simd)[0] == '\0')
        return 5;
    wire = runtime;
    wire.backends[PE_COMPUTE_CPU_REF].capabilities |= UINT64_C(1) << 63;
    wire.backends[PE_COMPUTE_CPU_REF].strategy_elements_per_s = 123.5;
    wire.backends[PE_COMPUTE_CPU_REF].update_elements_per_s = 456.25;
    wire.backends[PE_COMPUTE_CPU_REF].terminal_elements_per_s = 789.75;
    descriptor_length = pe_runtime_descriptor_to_string(
        &wire, descriptor, sizeof(descriptor));
    if (descriptor_length == 0u ||
        descriptor_length != pe_runtime_descriptor_to_string(&wire, NULL, 0u) ||
        pe_runtime_descriptor_from_string(descriptor, &decoded) != 0 ||
        decoded.backends[PE_COMPUTE_CPU_REF].capabilities !=
            wire.backends[PE_COMPUTE_CPU_REF].capabilities ||
        memcmp(&decoded.backends[PE_COMPUTE_CPU_REF].strategy_elements_per_s,
               &wire.backends[PE_COMPUTE_CPU_REF].strategy_elements_per_s,
               sizeof(double)) != 0 ||
        memcmp(&decoded.backends[PE_COMPUTE_CPU_REF].update_elements_per_s,
               &wire.backends[PE_COMPUTE_CPU_REF].update_elements_per_s,
               sizeof(double)) != 0 ||
        memcmp(&decoded.backends[PE_COMPUTE_CPU_REF].terminal_elements_per_s,
               &wire.backends[PE_COMPUTE_CPU_REF].terminal_elements_per_s,
               sizeof(double)) != 0)
        return 11;
    if (!runtime.openmp_available)
    {
        if (runtime.backends[PE_COMPUTE_CPU_PAR].runtime_available ||
            runtime.backends[PE_COMPUTE_CPU_PAR].validated)
            return 9;
        if (pe_runtime_backend_status(&runtime.backends[PE_COMPUTE_CPU_PAR],
                                      status, sizeof(status)) < 0 ||
            strstr(status, "OpenMP") == NULL)
            return 10;
    }
    memset(&synthetic, 0, sizeof(synthetic));
    synthetic.openmp_available = 1;
    synthetic.backends[PE_COMPUTE_CPU_PAR].runtime_available = 1;
    synthetic.backends[PE_COMPUTE_CPU_PAR].validated = 1;
    synthetic.backends[PE_COMPUTE_CPU_REF].runtime_available = 1;
    synthetic.backends[PE_COMPUTE_CPU_REF].validated = 1;
    if (pe_runtime_recommended_backend(&synthetic) != PE_COMPUTE_CPU_PAR)
        return 6;
    synthetic.openmp_available = 0;
    if (pe_runtime_recommended_backend(&synthetic) != PE_COMPUTE_CPU_REF)
        return 7;
    memset(&synthetic, 0, sizeof(synthetic));
    if (pe_runtime_recommended_backend(&synthetic) != PE_COMPUTE_AUTO)
        return 8;
    synthetic.backends[PE_COMPUTE_CPU_REF].kind = PE_COMPUTE_CPU_REF;
    synthetic.backends[PE_COMPUTE_CPU_REF].runtime_available = 1;
    synthetic.backends[PE_COMPUTE_CPU_REF].validated = 1;
    synthetic.backends[PE_COMPUTE_CPU_REF].terminal_elements_per_s = 10.0;
    synthetic.backends[PE_COMPUTE_CUDA].kind = PE_COMPUTE_CUDA;
    synthetic.backends[PE_COMPUTE_CUDA].compiled = 1;
    synthetic.backends[PE_COMPUTE_CUDA].runtime_available = 1;
    synthetic.backends[PE_COMPUTE_CUDA].validated = 1;
    synthetic.backends[PE_COMPUTE_CUDA].capabilities =
        PE_CAP_GPU_TERMINAL_EVAL;
    synthetic.backends[PE_COMPUTE_CUDA].terminal_elements_per_s = 100.0;
    if (pe_runtime_recommended_backend(&synthetic) != PE_COMPUTE_CUDA)
        return 12;
    printf("runtime: cpus=%u openmp=%d simd_machine=%s simd_compiled=%s simd=%s\n",
           runtime.logical_cpus, runtime.openmp_available,
           pe_runtime_simd_name(runtime.simd_machine),
           pe_runtime_simd_name(runtime.simd_compiled),
           pe_runtime_simd_name(runtime.simd));
    for (pe_compute_kind_t kind = PE_COMPUTE_AUTO;
         kind < PE_COMPUTE_COUNT; ++kind)
    {
        if (pe_runtime_backend_status(&runtime.backends[kind],
                                      status, sizeof(status)) >= 0)
            printf("backend=%s %s\n",
                   pe_compute_kind_name(kind), status);
    }
    return 0;
}
