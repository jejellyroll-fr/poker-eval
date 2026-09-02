/* pe_runout_report.h - exact conditional Hold'em runout enumeration. */

#ifndef POKER_EVAL_PE_RUNOUT_REPORT_H
#define POKER_EVAL_PE_RUNOUT_REPORT_H

#include <stddef.h>

#include <poker_eval/core/modern_cardmask.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    mask_t board;
    double probability;
} pe_runout_report_row_t;

typedef int (*pe_runout_report_callback)(
    const pe_runout_report_row_t *row, void *user);

/*
 * Enumerate complete five-card boards conditional on `board` and `dead`.
 * `dead` may contain private cards and other unavailable cards, but may not
 * overlap `board`. The callback receives each final board exactly once and
 * its conditional probability; probabilities sum to one when at least one
 * legal runout exists. Starting from preflop, flop or turn is supported.
 */
int pe_holdem_runout_report_generate(
    mask_t board,
    mask_t dead,
    pe_runout_report_callback callback,
    void *user,
    size_t *out_count,
    double *out_probability_sum);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_RUNOUT_REPORT_H */
