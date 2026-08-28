/*
 * work_scheduler.c - deterministic distributed work allocation (DIST-03)
 */

#include <poker_eval/solver/pe_work_scheduler.h>

#include <float.h>
#include <math.h>
#include <stddef.h>

static int finite_double(double value)
{
    return value <= DBL_MAX && value >= -DBL_MAX;
}

static double backend_rate(const pe_runtime_backend_info_t *backend)
{
    if (backend->update_elements_per_s > 0.0 &&
        finite_double(backend->update_elements_per_s))
        return backend->update_elements_per_s;
    if (backend->strategy_elements_per_s > 0.0 &&
        finite_double(backend->strategy_elements_per_s))
        return backend->strategy_elements_per_s;
    if (backend->terminal_elements_per_s > 0.0 &&
        finite_double(backend->terminal_elements_per_s))
        return backend->terminal_elements_per_s;
    return 0.0;
}

int pe_work_schedule(const pe_runtime_capabilities_t *runtime,
                     size_t unit_count,
                     pe_work_allocation_t *out,
                     size_t capacity)
{
    size_t i;
    size_t usable = 0u;
    size_t assigned = 0u;
    long double total_rate = 0.0L;
    int have_rate = 0;
    double rates[PE_COMPUTE_COUNT];
    size_t counts[PE_COMPUTE_COUNT];
    long double fractions[PE_COMPUTE_COUNT];

    if (!runtime || (!out && capacity != 0u))
        return -1;

    if (unit_count == 0u)
        return 0;

    for (i = 0u; i < PE_COMPUTE_COUNT; ++i) {
        rates[i] = 0.0;
        counts[i] = 0u;
        fractions[i] = 0.0L;
        if (i == (size_t)PE_COMPUTE_AUTO)
            continue;
        if (!runtime->backends[i].compiled ||
            !runtime->backends[i].runtime_available ||
            !runtime->backends[i].validated)
            continue;
        rates[i] = backend_rate(&runtime->backends[i]);
        if (rates[i] > 0.0) {
            have_rate = 1;
            total_rate += (long double)rates[i];
        }
        ++usable;
    }

    if (usable > capacity)
        return -1;
    if (usable == 0u)
        return 0;

    if (!have_rate) {
        total_rate = (long double)usable;
        for (i = 0u; i < PE_COMPUTE_COUNT; ++i) {
            if (runtime->backends[i].compiled &&
                runtime->backends[i].runtime_available &&
                runtime->backends[i].validated &&
                i != (size_t)PE_COMPUTE_AUTO)
                rates[i] = 1.0;
        }
    }

    for (i = 0u; i < PE_COMPUTE_COUNT; ++i) {
        long double ideal;
        long double floored;
        size_t base;
        if (rates[i] <= 0.0)
            continue;
        ideal = ((long double)unit_count * (long double)rates[i]) /
                total_rate;
        floored = floorl(ideal);
        base = (size_t)floored;
        if (base > unit_count)
            base = unit_count;
        counts[i] = base;
        fractions[i] = ideal - (long double)base;
        assigned += base;
    }

    while (assigned < unit_count) {
        size_t best = PE_COMPUTE_COUNT;
        for (i = 0u; i < PE_COMPUTE_COUNT; ++i) {
            if (rates[i] <= 0.0)
                continue;
            if (best == (size_t)PE_COMPUTE_COUNT ||
                fractions[i] > fractions[best])
                best = i;
        }
        if (best == (size_t)PE_COMPUTE_COUNT)
            return -1;
        ++counts[best];
        fractions[best] = -1.0L;
        ++assigned;
    }

    usable = 0u;
    for (i = 0u; i < PE_COMPUTE_COUNT; ++i) {
        if (rates[i] <= 0.0)
            continue;
        out[usable].backend = (pe_compute_kind_t)i;
        out[usable].unit_count = counts[i];
        out[usable].units_per_s = rates[i];
        ++usable;
    }
    return (int)usable;
}

int pe_work_assign(const pe_work_allocation_t *allocations,
                   size_t allocation_count,
                   size_t total_units,
                   pe_work_assignment_t *out,
                   size_t capacity)
{
    size_t i;
    size_t first = 0u;
    size_t positive = 0u;
    size_t seen = 0u;
    unsigned int seen_backends = 0u;

    if ((!allocations && allocation_count != 0u) ||
        (!out && capacity != 0u))
        return -1;
    for (i = 0u; i < allocation_count; ++i) {
        unsigned int bit;
        if (allocations[i].backend <= PE_COMPUTE_AUTO ||
            allocations[i].backend >= PE_COMPUTE_COUNT)
            return -1;
        bit = 1u << (unsigned int)allocations[i].backend;
        if ((seen_backends & bit) != 0u)
            return -1;
        seen_backends |= bit;
        if (allocations[i].unit_count > total_units - seen)
            return -1;
        seen += allocations[i].unit_count;
        if (allocations[i].unit_count != 0u)
            ++positive;
    }
    if (seen != total_units || positive > capacity)
        return -1;

    positive = 0u;
    for (i = 0u; i < allocation_count; ++i) {
        if (allocations[i].unit_count == 0u)
            continue;
        out[positive].backend = allocations[i].backend;
        out[positive].first_unit = first;
        out[positive].unit_count = allocations[i].unit_count;
        first += allocations[i].unit_count;
        ++positive;
    }
    return (int)positive;
}

int pe_work_schedule_assignments(const pe_runtime_capabilities_t *runtime,
                                 size_t total_units,
                                 pe_work_assignment_t *out,
                                 size_t capacity)
{
    pe_work_allocation_t allocations[PE_COMPUTE_COUNT];
    int allocation_count;

    if (total_units == 0u)
        return 0;
    allocation_count = pe_work_schedule(runtime, total_units, allocations,
                                         PE_COMPUTE_COUNT);
    if (allocation_count < 0)
        return -1;
    return pe_work_assign(allocations, (size_t)allocation_count, total_units,
                          out, capacity);
}
