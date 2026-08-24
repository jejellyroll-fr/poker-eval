/* cluster.c - deterministic distributed-solver partition primitives */

#include <poker_eval/solver/pe_solver_cluster.h>

uint32_t pe_solver_shard_for_key(uint64_t key, uint32_t shard_count)
{
    uint64_t x = key;
    if (shard_count == 0u)
        return UINT32_MAX;
    /* SplitMix64 finalizer avoids pathological low-bit distributions while
       preserving a cheap, platform-independent routing rule. */
    x ^= x >> 30;
    x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27;
    x *= UINT64_C(0x94d049bb133111eb);
    x ^= x >> 31;
    return (uint32_t)(x % (uint64_t)shard_count);
}

int pe_solver_shard_valid(pe_solver_shard_t shard)
{
    return shard.shard_count > 0u && shard.shard_id < shard.shard_count;
}

int pe_solver_shard_owns(pe_solver_shard_t shard, uint64_t key)
{
    return pe_solver_shard_valid(shard) &&
           pe_solver_shard_for_key(key, shard.shard_count) == shard.shard_id;
}

int pe_solver_shard_range(pe_solver_shard_t shard, uint64_t total,
                          uint64_t *out_begin, uint64_t *out_end)
{
    uint64_t base;
    uint64_t remainder;
    if (!pe_solver_shard_valid(shard) || !out_begin || !out_end)
        return -1;
    base = total / (uint64_t)shard.shard_count;
    remainder = total % (uint64_t)shard.shard_count;
    *out_begin = (uint64_t)shard.shard_id * base +
                 ((uint64_t)shard.shard_id < remainder
                      ? (uint64_t)shard.shard_id : remainder);
    *out_end = *out_begin + base +
               ((uint64_t)shard.shard_id < remainder ? 1u : 0u);
    return 0;
}
