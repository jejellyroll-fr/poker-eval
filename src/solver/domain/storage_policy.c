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
    /* Case-insensitive like the other solver enum parsers, so the CLI
     * accepts the case variants that precision and backend accept. */
    static const char *const names[] = { "full", "compact", "recompute-deep" };
    static const pe_storage_policy_t values[] = {
        PE_STORAGE_FULL, PE_STORAGE_COMPACT, PE_STORAGE_RECOMPUTE_DEEP
    };
    size_t length;
    size_t i;
    size_t j;

    if (!name || !*name)
        return PE_STORAGE_POLICY_COUNT;
    length = strnlen(name, 128u);
    if (length >= 128u)
        return PE_STORAGE_POLICY_COUNT;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
    {
        for (j = 0; j < length; ++j)
        {
            char lower = (char)((name[j] >= 'A' && name[j] <= 'Z')
                                    ? name[j] - 'A' + 'a' : name[j]);
            char ref = (char)((names[i][j] >= 'A' && names[i][j] <= 'Z')
                                  ? names[i][j] - 'A' + 'a' : names[i][j]);
            if (names[i][j] == '\0' || lower != ref)
                break;
        }
        if (j == length && names[i][length] == '\0')
            return values[i];
    }
    return PE_STORAGE_POLICY_COUNT;
}
