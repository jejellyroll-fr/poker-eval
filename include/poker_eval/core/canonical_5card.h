/*
 * canonical_5card.h - Fast canonical 5-card poker hand evaluator
 *
 * Provides optimized 5-card hand evaluation using:
 * - Direct classification without LUT for maximum flexibility
 * - Canonical hand strength calculation
 * - Integration with modern mask operations
 * - Support for different deck types
 */

#ifndef CANONICAL_5CARD_H
#define CANONICAL_5CARD_H

#include <stdint.h>
#include <stdbool.h>
#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/core/eval_context.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Use hand classification from eval_context.h */

/**
 * Classify a 5-card hand into its poker hand type
 * @param hand 5-card mask (must have exactly 5 cards set)
 * @return hand classification
 */
hand_class_t classify_5card_hand(mask_t hand);

/**
 * Fast canonical 5-card evaluation
 * @param hand 5-card mask (must have exactly 5 cards set)
 * @return evaluation value (higher = stronger)
 */
uint32_t evaluate_5card_canonical(mask_t hand);

/**
 * Create canonical 5-card lookup table
 * @param deck_type Type of deck for evaluation
 * @param lut_size Output: size of created LUT
 * @return allocated LUT or NULL on failure
 */
uint32_t* create_canonical_5c_lut(eval_deck_type_t deck_type, size_t* lut_size);

/**
 * Canonical 5-card evaluator with optional LUT
 * @param hand 5-card mask
 * @param lut Optional precomputed lookup table
 * @param lut_size Size of LUT
 * @return evaluation value or EVAL_INVALID
 */
uint32_t canonical_5card_eval(mask_t hand, const uint32_t* lut, size_t lut_size);

#ifdef __cplusplus
}
#endif

#endif /* CANONICAL_5CARD_H */
