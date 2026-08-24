/* pe_solver_cluster.h - deterministic sharding primitives for distributed runs */

#ifndef POKER_EVAL_PE_SOLVER_CLUSTER_H
#define POKER_EVAL_PE_SOLVER_CLUSTER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t shard_id;
    uint32_t shard_count;
} pe_solver_shard_t;

/* Stable FNV-1a-compatible partitioning. The result is independent of the
 * process, pointer layout and insertion order, so workers can route the same
 * infoset to the same owner across machines and checkpoint restores. */
uint32_t pe_solver_shard_for_key(uint64_t infoset_key, uint32_t shard_count);

int pe_solver_shard_valid(pe_solver_shard_t shard);
int pe_solver_shard_owns(pe_solver_shard_t shard, uint64_t infoset_key);

/* Split a half-open dense-id range as evenly as possible. This is useful for
 * deterministic manifest generation; it does not perform network transport or
 * merge mutable solver state. */
int pe_solver_shard_range(pe_solver_shard_t shard, uint64_t total,
                          uint64_t *out_begin, uint64_t *out_end);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_SOLVER_CLUSTER_H */
