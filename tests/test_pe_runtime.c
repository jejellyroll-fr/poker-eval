#include <poker_eval/solver/pe_runtime.h>
#include <poker_eval/solver/pe_solver_plan.h>

#include <stdio.h>
#include <string.h>

int main(void)
{
    pe_runtime_capabilities_t runtime;
    char status[256];
    if (pe_runtime_probe(&runtime) != 0)
        return 1;
    if (runtime.logical_cpus == 0u || runtime.simd < SIMD_NONE ||
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
    printf("runtime: cpus=%u openmp=%d simd=%s cpu_ref=%s\n",
           runtime.logical_cpus, runtime.openmp_available,
           pe_runtime_simd_name(runtime.simd), status);
    return 0;
}
