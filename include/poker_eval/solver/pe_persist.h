/*
 * pe_persist.h - Backend-independent solver checkpoints (API-04)
 */

#ifndef POKER_EVAL_PE_PERSIST_H
#define POKER_EVAL_PE_PERSIST_H

#include <poker_eval/solver/pe_solver.h>
#include <poker_eval/solver/pe_solver_config.h>
#include <poker_eval/solver/pe_storage_port.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** File target for a checkpoint adapter. The path is borrowed. */
struct pe_persist_target_t {
    const char *path;
    uint64_t game_hash;
    uint64_t tree_hash;
};

/** File source for a checkpoint adapter. The path is borrowed. */
struct pe_persist_source_t {
    const char *path;
    uint64_t game_hash;
    uint64_t tree_hash;
};

#ifndef POKER_EVAL_PE_PERSIST_OPS_T_DEFINED
typedef struct pe_persist_ops_t pe_persist_ops_t;
#define POKER_EVAL_PE_PERSIST_OPS_T_DEFINED
#endif

/**
 * Persistence adapter contract. The storage pair is the live solver state;
 * adapters must not retain it after the call returns.
 */
struct pe_persist_ops_t {
    const char *name;
    int (*save)(void *self, const pe_persist_target_t *target,
                const pe_solver_config_t *config,
                const pe_storage_ops_t *storage, void *storage_self,
                uint64_t iteration);
    int (*load)(void *self, const pe_persist_source_t *source,
                const pe_solver_config_t *config,
                const pe_storage_ops_t *storage, void *storage_self,
                uint64_t *out_iteration);
};

/** The portable v2 checkpoint adapter. The returned table is static. */
const pe_persist_ops_t *pe_persist_checkpoint_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_PERSIST_H */
