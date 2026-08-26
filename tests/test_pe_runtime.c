#include <poker_eval/solver/pe_runtime.h>
#include <poker_eval/solver/pe_solver_plan.h>

#include <stdio.h>
#include <string.h>

int main(void)
{
    pe_runtime_capabilities_t runtime;
    pe_runtime_capabilities_t synthetic;
    char status[256];
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
    printf("runtime: cpus=%u openmp=%d simd_machine=%s simd_compiled=%s simd=%s cpu_ref=%s\n",
           runtime.logical_cpus, runtime.openmp_available,
           pe_runtime_simd_name(runtime.simd_machine),
           pe_runtime_simd_name(runtime.simd_compiled),
           pe_runtime_simd_name(runtime.simd), status);
    return 0;
}
