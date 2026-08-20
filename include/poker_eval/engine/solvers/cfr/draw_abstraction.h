/*
 * draw_abstraction.h - deterministic hand buckets for draw-game CFR
 */
#ifndef POKER_EVAL_DRAW_ABSTRACTION_H
#define POKER_EVAL_DRAW_ABSTRACTION_H

#include <stdint.h>
#include <poker_eval/core/modern_cardmask.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum pe_draw_variant_e {
    PE_DRAW_BADUGI = 0,
    PE_DRAW_TRIPLE_DRAW_27 = 1
} pe_draw_variant_t;

typedef struct pe_draw_features_s {
    uint8_t cards_kept;
    uint8_t cards_discarded;
    uint8_t distinct_ranks;
    uint8_t distinct_suits;
    uint8_t paired_ranks;
    uint8_t low_cards;
    uint16_t rank_sum;
} pe_draw_features_t;

/* Extract structural features from a kept draw hand. Returns 0 on success. */
int pe_draw_features(pe_draw_variant_t variant,
                     mask_t hand,
                     mask_t discard,
                     pe_draw_features_t *out);

/*
 * Return a stable, compact infoset bucket. Equal keys mean that the two
 * hands have the same draw count and the same coarse rank/suit structure.
 * The key intentionally does not encode card identities.
 * Returns 0 for invalid arguments; valid keys are non-zero.
 */
uint32_t pe_draw_abstraction_key(pe_draw_variant_t variant,
                                 mask_t hand,
                                 mask_t discard);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_DRAW_ABSTRACTION_H */
