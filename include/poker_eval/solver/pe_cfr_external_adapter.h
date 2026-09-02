/* pe_cfr_external_adapter.h - expose a legacy cfr_game_t through Lane B. */

#ifndef POKER_EVAL_PE_CFR_EXTERNAL_ADAPTER_H
#define POKER_EVAL_PE_CFR_EXTERNAL_ADAPTER_H

#include <poker_eval/engine/solvers/cfr/cfr_core.h>
#include <poker_eval/solver/pe_external_traversal.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The adapter borrows the legacy game.  The game and its game_data must stay
   alive until the external solver and any sampled BR measurement finish. */
typedef struct
{
    cfr_game_t *legacy;
    pe_external_game_t external;
} pe_cfr_external_adapter_t;

int pe_cfr_external_adapter_init(pe_cfr_external_adapter_t *out,
                                 cfr_game_t *legacy);

const pe_external_game_t *pe_cfr_external_adapter_game(
    const pe_cfr_external_adapter_t *adapter);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_CFR_EXTERNAL_ADAPTER_H */
