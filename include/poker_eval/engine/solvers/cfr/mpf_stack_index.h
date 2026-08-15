#ifndef POKER_EVAL_MPF_STACK_INDEX_H
#define POKER_EVAL_MPF_STACK_INDEX_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Sparse state indexer for multiway asymmetrical stacks (FEAT-10, #146).
 *
 * Instead of paying the worst-case MPF_MAX_PLAYERS stride for every node, an
 * mpf_stack_index_t maps the *canonicalized active stack configuration* of a
 * state to a dense 32-bit cfg_id. Equivalent configs reached via different
 * action orders share one id, so memory grows with the number of *distinct*
 * committed-stack configurations rather than the combinatorial worst case.
 *
 * Ids are assigned deterministically: the canonical config key is hashed and
 * ids are handed out in ascending order of (hash, then lexicographic config),
 * so the same game reproduces the same id sequence across runs. This keeps the
 * index safe to embed in serialized blueprints (.pe_sol, FEAT-01).
 */

#define MPF_STACK_INDEX_MAX_PLAYERS 7

typedef struct
{
    /* Number of logical players (1..MPF_STACK_INDEX_MAX_PLAYERS). */
    int num_players;
    /* Per-player committed round contribution (already-invested-this-round). */
    double round_contrib[MPF_STACK_INDEX_MAX_PLAYERS];
    /* Remaining stack behind each player (post round_contrib). */
    double remaining[MPF_STACK_INDEX_MAX_PLAYERS];
    /* Active mask: bit i set when player i is still in the hand. */
    uint32_t active_mask;
} mpf_stack_config_t;

typedef struct mpf_stack_index_t mpf_stack_index_t;

/* Create an empty index. cap_hint is the initial bucket count (rounded up to a
 * power of two, minimum 16). Returns NULL only on allocation failure. */
mpf_stack_index_t *mpf_stack_index_create(size_t cap_hint);

/* Free the index and all internal storage. Safe to call with NULL. */
void mpf_stack_index_destroy(mpf_stack_index_t *idx);

/* Insert/lookup a configuration. On first sighting of a config it is assigned
 * the next deterministic cfg_id (>= 1; 0 is reserved as "unindexed"). The
 * assigned/looked-up id is written to *out_id. Returns 0 on success, -1 on
 * allocation failure (the id is left untouched in that case). */
int mpf_stack_index_put(mpf_stack_index_t *idx,
                        const mpf_stack_config_t *cfg,
                        uint32_t *out_id);

/* Lookup only; does not insert. Returns 1 and writes *out_id when present,
 * returns 0 when the config is unknown. */
int mpf_stack_index_get(const mpf_stack_index_t *idx,
                        const mpf_stack_config_t *cfg,
                        uint32_t *out_id);

/* Number of distinct configs discovered so far (== highest assigned id). */
size_t mpf_stack_index_count(const mpf_stack_index_t *idx);

/* Current bucket capacity (for memory/diagnostic reporting). */
size_t mpf_stack_index_capacity(const mpf_stack_index_t *idx);

/* Build a canonical config from parallel arrays. active[i] is treated as a
 * truthy int; only active players contribute to the canonical key. This is the
 * single place that normalizes a state into the sparse index key. */
void mpf_stack_config_from_arrays(mpf_stack_config_t *out,
                                  int num_players,
                                  const double *round_contrib,
                                  const double *remaining,
                                  const int *active);

/* Stable 64-bit hash of a stack configuration (the same value the sparse
 * index keys on). Exposed so callers can fold the stack structure into a
 * larger key without going through a handle/id. Two configs that differ
 * only by inactive players (or by identical per-player stacks) hash equal. */
uint64_t mpf_stack_config_hash(const mpf_stack_config_t *cfg);

/*
 * Compact reach-weight mapping (FEAT-10, #146).
 *
 * Reach probabilities are stored contiguously per (cfg_id, player) for active
 * players only, replacing the fixed MPF_MAX_PLAYERS stride that every node
 * previously paid for. Indexing is bounds-checked against the allocated slot
 * count so deep recursive traversals cannot go out of bounds.
 */
typedef struct mpf_reach_map_t mpf_reach_map_t;

/* Create a reach map sized for `config_count` configs and `num_players`
 * players. Returns NULL only on allocation failure. */
mpf_reach_map_t *mpf_reach_map_create(size_t config_count, int num_players);

/* Free the map. Safe to call with NULL. */
void mpf_reach_map_destroy(mpf_reach_map_t *map);

/* Store `weight` for (cfg_id, player). Returns 0 on success, -1 if the
 * (cfg_id, player) pair is out of the allocated bounds (caller error). */
int mpf_reach_map_set(mpf_reach_map_t *map,
                      uint32_t cfg_id,
                      int player,
                      double weight);

/* Read the weight for (cfg_id, player). Out-of-bounds access returns 0.0 and
 * leaves *ok = 0; otherwise *ok = 1. */
double mpf_reach_map_get(const mpf_reach_map_t *map,
                         uint32_t cfg_id,
                         int player,
                         int *ok);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_MPF_STACK_INDEX_H */
