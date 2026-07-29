/*
 * combo_7to5.c - Specialized 7→5 combination generator implementation
 *
 * Highly optimized for Texas Hold'em 7-card evaluation.
 * Pre-computes all 21 combinations for maximum performance.
 */

#include <poker_eval/core/combo_7to5.h>
#include <stdlib.h>
#include <string.h>

/* All 21 combinations of choosing 5 positions from 7 (precomputed) */
const combo_7to5_pattern_t combo_patterns[21] = {
    {{0, 1, 2, 3, 4}}, {{0, 1, 2, 3, 5}}, {{0, 1, 2, 3, 6}}, {{0, 1, 2, 4, 5}}, {{0, 1, 2, 4, 6}}, {{0, 1, 2, 5, 6}}, {{0, 1, 3, 4, 5}}, {{0, 1, 3, 4, 6}}, {{0, 1, 3, 5, 6}}, {{0, 1, 4, 5, 6}}, {{0, 2, 3, 4, 5}}, {{0, 2, 3, 4, 6}}, {{0, 2, 3, 5, 6}}, {{0, 2, 4, 5, 6}}, {{0, 3, 4, 5, 6}}, {{1, 2, 3, 4, 5}}, {{1, 2, 3, 4, 6}}, {{1, 2, 3, 5, 6}}, {{1, 2, 4, 5, 6}}, {{1, 3, 4, 5, 6}}, {{2, 3, 4, 5, 6}}};

combo_7to5_t *combo_7to5_create(mask_t seven_cards)
{
    if (mask_popcount(seven_cards) != 7)
    {
        return NULL;
    }

    combo_7to5_t *gen = malloc(sizeof(combo_7to5_t));
    if (!gen)
    {
        return NULL;
    }

    gen->seven_cards = seven_cards;
    gen->current_combination = 0;
    gen->exhausted = false;

    /* Convert mask to ordered list of cards for fast access */
    int card_index = 0;
    for (int card = 0; card < 52 && card_index < 7; card++)
    {
        if (mask_is_set(seven_cards, card))
        {
            gen->card_list[card_index++] = card;
        }
    }

    return gen;
}

bool combo_7to5_next(combo_7to5_t *gen, mask_t *five_combo)
{
    if (!gen || !five_combo || gen->exhausted)
    {
        return false;
    }

    if (gen->current_combination >= 21)
    {
        gen->exhausted = true;
        return false;
    }

    /* Build the 5-card combination using precomputed pattern */
    const combo_7to5_pattern_t *pattern = &combo_patterns[gen->current_combination];
    *five_combo = MASK_EMPTY;

    for (int i = 0; i < 5; i++)
    {
        int card = gen->card_list[pattern->positions[i]];
        *five_combo = mask_set(*five_combo, card);
    }

    gen->current_combination++;
    return true;
}

bool combo_7to5_reset(combo_7to5_t *gen)
{
    if (!gen)
        return false;

    gen->current_combination = 0;
    gen->exhausted = false;
    return true;
}

void combo_7to5_destroy(combo_7to5_t *gen)
{
    if (gen)
    {
        free(gen);
    }
}

int combo_7to5_current_index(const combo_7to5_t *gen)
{
    if (!gen || gen->exhausted)
    {
        return -1;
    }
    return gen->current_combination - 1; /* -1 because we increment after generating */
}
