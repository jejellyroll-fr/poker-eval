#ifndef POKER_EVAL_MPF_COMPACT_STORAGE_H
#define POKER_EVAL_MPF_COMPACT_STORAGE_H

#include <stddef.h>
#include <stdint.h>

#include <poker_eval/engine/solvers/cfr/cfr_core.h>
#include <poker_eval/engine/solvers/cfr/mpf_tree.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Compact binary storage format for solved game trees.
 *
 * The format stores the average (solved) strategy of a cfr_storage_t as a
 * quantized, fixed-point binary blob that is dramatically smaller than a JSON
 * or raw 64-bit float dump, and that can be memory-mapped and read back on
 * demand without copying the entire strategy array into the heap.
 *
 * File extensions:
 *   .pe_sol  - compact solved strategy (cfr_storage_t average strategy)
 *   .pe_tree - compact serialized tree definition (mpf_tree_def_t)
 */

/* ------------------------------------------------------------------ *
 * .pe_sol : solved strategy storage
 * ------------------------------------------------------------------ */

/**
 * Save the average (solved) strategy of a CFR storage to a compact binary
 * .pe_sol file.
 *
 * Strategy weights for every infoset are normalized to a probability
 * distribution and quantized into 16-bit fixed-point values (or a denser
 * variable-byte representation for sparse rows). This typically reduces
 * on-disk size by >= 4x relative to a JSON / raw float dump.
 *
 * @param storage   Solved CFR storage (must not be NULL)
 * @param path      Output .pe_sol path (must not be NULL)
 * @return 0 on success, -1 on error (errno is set on failure)
 */
int pe_cfr_save_storage(cfr_storage_t *storage, const char *path);

/**
 * Load a compact binary .pe_sol file into a CFR storage.
 *
 * @param storage   Storage to populate (must not be NULL and empty)
 * @param path      Input .pe_sol path (must not be NULL)
 * @return 0 on success, -1 on error (errno is set on failure)
 */
int pe_cfr_load_storage(cfr_storage_t *storage, const char *path);

/**
 * Opaque handle for a memory-mapped, read-only view of a .pe_sol file.
 *
 * Loading via mmap avoids copying the strategy arrays into the heap and lets
 * the OS page the data in on demand, drastically cutting the RAM needed to
 * inspect a solved tree.
 */
typedef struct pe_sol_mmap_t pe_sol_mmap_t;

/**
 * Open a .pe_sol file as a memory-mapped, read-only storage view.
 *
 * @param path      Input .pe_sol path (must not be NULL)
 * @param out_view  Receives the view handle (must not be NULL)
 * @return 0 on success, -1 on error (errno is set on failure)
 */
int pe_sol_open_mmap(const char *path, pe_sol_mmap_t **out_view);

/**
 * Number of infosets contained in a memory-mapped view.
 */
size_t pe_sol_mmap_infoset_count(const pe_sol_mmap_t *view);

/**
 * Resolve the normalized (double) strategy for a given infoset index within a
 * memory-mapped view.
 *
 * @param view      Mapped view (must not be NULL)
 * @param index     Infoset index in [0, pe_sol_mmap_infoset_count(view))
 * @param out_key   Receives the infoset key (may be NULL)
 * @param max_actions Maximum size of out_probs (must be >= action count)
 * @param out_probs Receives normalized probabilities (must not be NULL)
 * @param out_n     Receives the action count (may be NULL)
 * @return 0 on success, -1 on error (errno set: ERANGE if max_actions too small)
 */
int pe_sol_mmap_get_strategy(const pe_sol_mmap_t *view,
                             size_t index,
                             uint64_t *out_key,
                             int max_actions,
                             double *out_probs,
                             int *out_n);

/**
 * Close a memory-mapped view and release the mapping.
 */
void pe_sol_close_mmap(pe_sol_mmap_t *view);

/* ------------------------------------------------------------------ *
 * .pe_tree : compact tree definition storage
 * ------------------------------------------------------------------ */

/**
 * Serialize a tree definition to a compact binary .pe_tree file.
 *
 * @param tree  Tree definition to serialize (must not be NULL)
 * @param path  Output .pe_tree path (must not be NULL)
 * @return 0 on success, -1 on error (errno is set on failure)
 */
int pe_tree_save(const mpf_tree_def_t *tree, const char *path);

/**
 * Load a compact binary .pe_tree file into a tree definition.
 *
 * @param path  Input .pe_tree path (must not be NULL)
 * @return A newly allocated mpf_tree_def_t on success, NULL on error
 *         (errno is set on failure). Free with mpf_tree_free().
 */
mpf_tree_def_t *pe_tree_load(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_MPF_COMPACT_STORAGE_H */
