/*
 * pe_holdem_chance.h - exact fixed-hole Hold'em river chance adapter
 *
 * This adapter is intentionally scoped to fixed private hands. It proves the
 * public-card chance protocol first; range/private-card chance comes after the
 * combo and blocker contract is in place.
 */

#ifndef POKER_EVAL_PE_HOLDEM_CHANCE_H
#define POKER_EVAL_PE_HOLDEM_CHANCE_H

#include <stddef.h>

#include <poker_eval/core/eval_context.h>
#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/solver/pe_betting_state.h>
#include <poker_eval/solver/pe_traversal.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    mask_t board;
    int is_chance;
    pe_betting_state_t betting;
} pe_holdem_chance_state_t;

typedef struct
{
    pe_vector_game_t vector;
    const EvalContext *context;
    mask_t hole[2];
    pe_holdem_chance_state_t root;
    pe_holdem_chance_state_t **children;
    size_t child_count;
    size_t child_capacity;
} pe_holdem_chance_game_t;

/* Build an exact river-deal vector game from a four-card board and fixed holes. */
int pe_holdem_chance_game_init(
    pe_holdem_chance_game_t *out,
    const EvalContext *context,
    mask_t board,
    mask_t player0_hole,
    mask_t player1_hole,
    double pot,
    double invested_each);

void pe_holdem_chance_game_destroy(pe_holdem_chance_game_t *game);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_HOLDEM_CHANCE_H */
