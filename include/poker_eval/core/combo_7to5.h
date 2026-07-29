/*
 * combo_7to5.h - Specialized 7→5 combination generator
 *
 * Optimized for the common case of Texas Hold'em 7-card evaluation
 * where we need all C(7,5) = 21 combinations. This generator is
 * much faster than the general-purpose combo generator for this
 * specific case.
 */

#ifndef COMBO_7TO5_H
#define COMBO_7TO5_H

#include <stdint.h>
#include <stdbool.h>
#include <poker_eval/core/modern_cardmask.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* Precomputed all 21 combinations of choosing 5 from 7 positions */
    /* Each entry represents which 5 positions to select from 7 */
    typedef struct
    {
        uint8_t positions[5]; /* Which 5 of the 7 cards to select */
    } combo_7to5_pattern_t;

    /* 7→5 specialized generator state */
    typedef struct
    {
        mask_t seven_cards;      /* The 7 cards to choose from */
        int card_list[7];        /* Cards in list form */
        int current_combination; /* Current combination index (0-20) */
        bool exhausted;          /* True when all combinations generated */
    } combo_7to5_t;

    /**
     * Create a specialized 7→5 combination generator
     * @param seven_cards Mask with exactly 7 cards set
     * @return Generator instance or NULL on error
     */
    combo_7to5_t *combo_7to5_create(mask_t seven_cards);

    /**
     * Get the next 5-card combination
     * @param gen Generator instance
     * @param five_combo Output: next 5-card combination
     * @return true if combination generated, false if exhausted
     */
    bool combo_7to5_next(combo_7to5_t *gen, mask_t *five_combo);

    /**
     * Reset generator to start over
     * @param gen Generator instance
     * @return true on success
     */
    bool combo_7to5_reset(combo_7to5_t *gen);

    /**
     * Destroy generator and free memory
     * @param gen Generator instance
     */
    void combo_7to5_destroy(combo_7to5_t *gen);

    /**
     * Get total number of combinations (always 21)
     * @return 21
     */
    static inline int combo_7to5_total_combinations(void)
    {
        return 21;
    }

    /**
     * Get current combination index
     * @param gen Generator instance
     * @return Current index (0-20) or -1 if exhausted
     */
    int combo_7to5_current_index(const combo_7to5_t *gen);

    /* Export combination patterns for use by optimized variants */
    extern const combo_7to5_pattern_t combo_patterns[21];

#ifdef __cplusplus
}
#endif

#endif /* COMBO_7TO5_H */
