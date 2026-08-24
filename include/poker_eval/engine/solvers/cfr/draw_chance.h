/* Draw-game chance adapter: replacement outcomes for PE_CHANCE_DRAW_N. */
#ifndef POKER_EVAL_DRAW_CHANCE_H
#define POKER_EVAL_DRAW_CHANCE_H

#include <poker_eval/engine/solvers/cfr/draw_abstraction.h>
#include <poker_eval/solver/pe_chance.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    pe_draw_variant_t variant;
    mask_t hand;
    mask_t discard;
    unsigned draw_count;
} pe_draw_chance_t;

int pe_draw_chance_init(pe_draw_chance_t *chance,
                        pe_draw_variant_t variant,
                        mask_t hand,
                        mask_t discard);

pe_chance_kind_t pe_draw_chance_kind(const pe_draw_chance_t *chance);
uint64_t pe_draw_chance_outcome_count(const pe_draw_chance_t *chance);

/* Return the replacement-card mask for an outcome index. */
int pe_draw_chance_outcome(const pe_draw_chance_t *chance,
                           uint64_t outcome,
                           mask_t *replacement);

/* Apply one replacement outcome and return the new private hand. */
int pe_draw_chance_apply(const pe_draw_chance_t *chance,
                         uint64_t outcome,
                         mask_t *new_hand);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_DRAW_CHANCE_H */
