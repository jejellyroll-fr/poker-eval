/* sampling_policy.c - street-work answers for sampled traversals (ISS-232). */

#include <poker_eval/solver/pe_sampling_policy.h>

#include <string.h>

uint16_t pe_sampling_replicates_for(pe_sampling_policy_t policy,
                                    const uint16_t *street_replicates,
                                    int street)
{
    if (street < 0 || street >= PE_SAMPLING_STREET_COUNT)
        return 1u;
    if (policy == PE_SAMPLING_STREET_BALANCED && street_replicates)
    {
        uint16_t requested = street_replicates[street];
        /* Zero means "one", so a zero-initialised config stays valid. */
        return requested ? requested : 1u;
    }
    return 1u;
}

const char *pe_sampling_policy_name(pe_sampling_policy_t policy)
{
    const char *name = "unknown";
    switch (policy)
    {
    case PE_SAMPLING_STANDARD: name = "standard"; break;
    case PE_SAMPLING_STREET_BALANCED: name = "street-balanced"; break;
    case PE_SAMPLING_COUNT: break;
    default: break;
    }
    return name;
}

int pe_sampling_policy_parse(const char *name, pe_sampling_policy_t *out)
{
    if (!name || !out)
        return -1;
    if (strcmp(name, "standard") == 0)
    {
        *out = PE_SAMPLING_STANDARD;
        return 0;
    }
    if (strcmp(name, "street-balanced") == 0)
    {
        *out = PE_SAMPLING_STREET_BALANCED;
        return 0;
    }
    return -1;
}
