/* pe_pruning.h - Regret-based pruning port (RBP-01). */

#ifndef POKER_EVAL_PE_PRUNING_H
#define POKER_EVAL_PE_PRUNING_H

#include <poker_eval/solver/pe_storage_port.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    pe_infoset_id_t infoset;
    uint16_t action_count;
    const double *cumulative_regrets;
    uint8_t *pruned;
} pe_pruning_span_t;

typedef struct
{
    /* An action is eligible when its cumulative regret is <= this value. */
    double regret_threshold;
    /* Number of future evaluations skipped before the action is rechecked. */
    uint32_t revisit_interval;
} pe_pruning_config_t;

typedef struct
{
    const char *name;
    int (*create)(void **self, const pe_pruning_config_t *config);
    void (*destroy)(void *self);
    int (*begin_iteration)(void *self, uint64_t iteration);
    int (*evaluate)(void *self, const pe_pruning_span_t *span);
    int (*is_pruned)(void *self, pe_infoset_id_t infoset,
                     uint16_t action, int *out_pruned);
    int (*end_iteration)(void *self, uint64_t iteration);
} pe_pruning_ops_t;

/** The deterministic regret-based pruning adapter. */
const pe_pruning_ops_t *pe_pruning_rbp_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_PRUNING_H */
