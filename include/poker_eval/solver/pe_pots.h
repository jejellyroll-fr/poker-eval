/* pe_pots.h - multiway side-pot construction and split distribution */

#ifndef POKER_EVAL_PE_POTS_H
#define POKER_EVAL_PE_POTS_H

#include <stdint.h>

#include <poker_eval/solver/pe_betting_state.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    double amount;
    uint8_t eligible_mask;
} pe_pot_slice_t;

/* Build contribution-level slices, including dead money already in pot. */
int pe_pot_slices_build(const pe_betting_state_t *state,
                        pe_pot_slice_t *out, uint8_t capacity,
                        uint8_t *out_count);

/* Divide each slice equally among its winning eligible players. */
int pe_pot_distribute(const pe_pot_slice_t *slices, uint8_t slice_count,
                      const uint8_t *winner_masks, uint8_t player_count,
                      double *out_awards);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_POTS_H */
