/*
 * combo_7to5_hilo.h - Optimized 7→5 combination generator with Hi/Lo mutualisation
 *
 * Eliminates double evaluation for Hi/Lo split-pot games by evaluating
 * both high and low hands in a single pass for each 5-card combination.
 */

#ifndef COMBO_7TO5_HILO_H
#define COMBO_7TO5_HILO_H

#include <stdint.h>
#include <stdbool.h>
#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/core/eval_context.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Result structure for Hi/Lo evaluation */
typedef struct {
    eval_t hi_value;    /* High hand value */
    eval_t lo_value;    /* Low hand value (or EVAL_INVALID if no qualifying low) */
    hand_class_t hi_class;  /* High hand classification */
    bool has_low;       /* True if hand qualifies for low */
} hilo_result_t;

/* Enhanced 7→5 generator with Hi/Lo optimization */
typedef struct {
    mask_t seven_cards;          /* The 7 cards to choose from */
    int card_list[7];            /* Cards in list form */
    int current_combination;     /* Current combination index (0-20) */
    bool exhausted;              /* True when all combinations generated */

    /* Hi/Lo tracking for best hands */
    hilo_result_t best_result;   /* Best Hi/Lo combination found so far */
    bool track_best;             /* Whether to track best combinations */
} combo_7to5_hilo_t;

/**
 * Create a specialized 7→5 Hi/Lo combination generator
 * @param seven_cards Mask with exactly 7 cards set
 * @param track_best Whether to track the best Hi/Lo combination
 * @return Generator instance or NULL on error
 */
combo_7to5_hilo_t* combo_7to5_hilo_create(mask_t seven_cards, bool track_best);

/**
 * Get the next 5-card combination with Hi/Lo evaluation
 * @param gen Generator instance
 * @param five_combo Output: next 5-card combination
 * @param result Output: Hi/Lo evaluation result
 * @return true if combination generated, false if exhausted
 */
bool combo_7to5_hilo_next(combo_7to5_hilo_t* gen, mask_t* five_combo, hilo_result_t* result);

/**
 * Get the best Hi/Lo combination found so far
 * @param gen Generator instance
 * @param best_combo Output: best 5-card combination
 * @param best_result Output: best Hi/Lo result
 * @return true if valid result available
 */
bool combo_7to5_hilo_get_best(const combo_7to5_hilo_t* gen, mask_t* best_combo, hilo_result_t* best_result);

/**
 * Reset generator to start over
 * @param gen Generator instance
 * @return true on success
 */
bool combo_7to5_hilo_reset(combo_7to5_hilo_t* gen);

/**
 * Destroy generator and free memory
 * @param gen Generator instance
 */
void combo_7to5_hilo_destroy(combo_7to5_hilo_t* gen);

/**
 * Evaluate a 5-card hand for both Hi and Lo in a single pass
 * @param five_cards 5-card mask
 * @param result Output: Hi/Lo evaluation result
 * @return true on success
 */
bool evaluate_hilo_single_pass(mask_t five_cards, hilo_result_t* result);

#ifdef __cplusplus
}
#endif

#endif /* COMBO_7TO5_HILO_H */
