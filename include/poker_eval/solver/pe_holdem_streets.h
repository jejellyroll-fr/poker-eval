/* pe_holdem_streets.h - exact public-card transitions by Hold'em street */

#ifndef POKER_EVAL_PE_HOLDEM_STREETS_H
#define POKER_EVAL_PE_HOLDEM_STREETS_H

#include <stddef.h>
#include <stdint.h>

#include <poker_eval/core/modern_cardmask.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    PE_HOLDEM_PREFLOP = 0,
    PE_HOLDEM_FLOP = 1,
    PE_HOLDEM_TURN = 2,
    PE_HOLDEM_RIVER = 3,
    PE_HOLDEM_SHOWDOWN = 4
} pe_holdem_street_t;

typedef int (*pe_holdem_board_callback)(mask_t board,
                                         uint8_t added_cards,
                                         double weight,
                                         void *user);

/* Derive the street from a board containing 0, 3, 4 or 5 cards. */
int pe_holdem_street_from_board(mask_t board, pe_holdem_street_t *out_street);

/* Number of public cards dealt by the next chance node, or zero at showdown. */
uint8_t pe_holdem_next_public_count(pe_holdem_street_t street);

/* Number of exact outcomes after removing board/dead cards. */
size_t pe_holdem_public_outcome_count(mask_t board, mask_t dead_cards);

/* Enumerate one exact public-card transition. Callback weights are 1.0. */
int pe_holdem_public_chance_enumerate(
    mask_t board,
    mask_t dead_cards,
    pe_holdem_board_callback callback,
    void *user,
    size_t *out_count,
    double *out_weight_sum);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_HOLDEM_STREETS_H */
