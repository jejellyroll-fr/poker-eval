#include <poker_eval/solver/pe_work_scheduler.h>

#include <assert.h>
#include <stddef.h>

static void mark_backend(pe_runtime_capabilities_t *runtime,
                         pe_compute_kind_t kind,
                         double update_rate)
{
    runtime->backends[kind].kind = kind;
    runtime->backends[kind].compiled = 1;
    runtime->backends[kind].runtime_available = 1;
    runtime->backends[kind].validated = 1;
    runtime->backends[kind].update_elements_per_s = update_rate;
}

static void test_weighted_largest_remainder(void)
{
    pe_runtime_capabilities_t runtime = {0};
    pe_work_allocation_t allocations[PE_COMPUTE_COUNT];
    int count;

    mark_backend(&runtime, PE_COMPUTE_CPU_REF, 1.0);
    mark_backend(&runtime, PE_COMPUTE_CPU_PAR, 3.0);
    mark_backend(&runtime, PE_COMPUTE_CUDA, 6.0);

    count = pe_work_schedule(&runtime, 10u, allocations,
                             PE_COMPUTE_COUNT);
    assert(count == 3);
    assert(allocations[0].backend == PE_COMPUTE_CPU_REF);
    assert(allocations[0].unit_count == 1u);
    assert(allocations[1].backend == PE_COMPUTE_CPU_PAR);
    assert(allocations[1].unit_count == 3u);
    assert(allocations[2].backend == PE_COMPUTE_CUDA);
    assert(allocations[2].unit_count == 6u);
}

static void test_fallback_and_ties(void)
{
    pe_runtime_capabilities_t runtime = {0};
    pe_work_allocation_t allocations[PE_COMPUTE_COUNT];
    int count;

    mark_backend(&runtime, PE_COMPUTE_CPU_REF, 0.0);
    runtime.backends[PE_COMPUTE_CPU_REF].strategy_elements_per_s = 2.0;
    mark_backend(&runtime, PE_COMPUTE_CPU_PAR, 0.0);
    runtime.backends[PE_COMPUTE_CPU_PAR].terminal_elements_per_s = 2.0;

    count = pe_work_schedule(&runtime, 3u, allocations,
                             PE_COMPUTE_COUNT);
    assert(count == 2);
    assert(allocations[0].unit_count == 2u);
    assert(allocations[1].unit_count == 1u);

    runtime.backends[PE_COMPUTE_CPU_REF].strategy_elements_per_s = 0.0;
    runtime.backends[PE_COMPUTE_CPU_PAR].terminal_elements_per_s = 0.0;
    count = pe_work_schedule(&runtime, 3u, allocations,
                             PE_COMPUTE_COUNT);
    assert(count == 2);
    assert(allocations[0].unit_count == 2u);
    assert(allocations[1].unit_count == 1u);
}

static void test_filters_and_arguments(void)
{
    pe_runtime_capabilities_t runtime = {0};
    pe_work_allocation_t allocations[PE_COMPUTE_COUNT];

    mark_backend(&runtime, PE_COMPUTE_CPU_REF, 1.0);
    runtime.backends[PE_COMPUTE_CPU_PAR].runtime_available = 1;
    assert(pe_work_schedule(&runtime, 4u, allocations, 0u) == -1);
    assert(pe_work_schedule(NULL, 4u, allocations, PE_COMPUTE_COUNT) == -1);
    assert(pe_work_schedule(&runtime, 0u, allocations, 0u) == 0);
    assert(pe_work_schedule(&runtime, 4u, NULL, 0u) == -1);
}

int main(void)
{
    test_weighted_largest_remainder();
    test_fallback_and_ties();
    test_filters_and_arguments();
    return 0;
}
