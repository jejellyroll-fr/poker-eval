/*
 * work_scheduler.c - deterministic distributed work allocation (DIST-03)
 */

#include <poker_eval/solver/pe_work_scheduler.h>

#include <math.h>
#include <stddef.h>

static double backend_rate(const pe_runtime_backend_info_t *backend)
{
    if (backend->update_elements_per_s > 0.0 &&
        isfinite(backend->update_elements_per_s))
        return backend->update_elements_per_s;
    if (backend->strategy_elements_per_s > 0.0 &&
        isfinite(backend->strategy_elements_per_s))
        return backend->strategy_elements_per_s;
    if (backend->terminal_elements_per_s > 0.0 &&
        isfinite(backend->terminal_elements_per_s))
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
