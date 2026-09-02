/* pe_variant_terminal.h - fixed-combo terminal bridge for supported variants */

#ifndef POKER_EVAL_PE_VARIANT_TERMINAL_H
#define POKER_EVAL_PE_VARIANT_TERMINAL_H

#include <stdint.h>

#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/solver/pe_variant.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PE_VARIANT_TERMINAL_MAX_PLAYERS 8u

/*
 * Evaluate one fixed private-hand joint outcome. The dispatch preserves the
 * existing game's high/low, short-deck, stud and draw rules while returning
 * normalized chip shares in the same [player] vector shape as the new solver.
 * `hands` and `board` use standard 0..51 card indices; short-deck callers must
 * simply omit ranks 2..5.
 */
int pe_variant_terminal_fixed(
    enum_game_t game,
    const mask_t *hands,
    uint8_t player_count,
    mask_t board,
    double pot,
    double *out_values);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_VARIANT_TERMINAL_H */
