/* storage_policy.c - explicit storage tiers for large sampled solves
 * (ISS-235, phase 6). */

#include <poker_eval/solver/pe_storage_policy.h>

#include <string.h>

int pe_storage_policy_pressure_street(pe_storage_policy_t policy)
{
    switch (policy)
    {
    case PE_STORAGE_COMPACT:
        /* Derived state kept for preflop (0) and flop (1); turn and beyond
         * re-materialise on demand. Streets tagged unknown pass. */
        return 2;
    case PE_STORAGE_RECOMPUTE_DEEP:
        /* Derived state dropped everywhere. */
        return 0;
    case PE_STORAGE_FULL:
        /* Nothing is deep enough to drop. */
        return PE_POLICY_STREET_COUNT;
    case PE_STORAGE_POLICY_COUNT:
        break;
    default:
        break;
    }
    return PE_POLICY_STREET_COUNT;
}

const char *pe_storage_policy_name(pe_storage_policy_t policy)
{
    const char *name = "unknown";
    switch (policy)
    {
    case PE_STORAGE_FULL: name = "full"; break;
    case PE_STORAGE_COMPACT: name = "compact"; break;
    case PE_STORAGE_RECOMPUTE_DEEP: name = "recompute-deep"; break;
    case PE_STORAGE_POLICY_COUNT: break;
    default: break;
    }
    return name;
}

pe_storage_policy_t pe_storage_policy_from_name(const char *name)
{
    if (!name)
        return PE_STORAGE_POLICY_COUNT;
    if (strcmp(name, "full") == 0)
        return PE_STORAGE_FULL;
    if (strcmp(name, "compact") == 0)
        return PE_STORAGE_COMPACT;
    if (strcmp(name, "recompute-deep") == 0)
        return PE_STORAGE_RECOMPUTE_DEEP;
    return PE_STORAGE_POLICY_COUNT;
}
