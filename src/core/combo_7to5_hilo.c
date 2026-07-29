/*
 * combo_7to5_hilo.c - Optimized 7→5 Hi/Lo combination generator
 *
 * Eliminates double evaluation by computing Hi and Lo in single pass.
 * Uses the existing combo_7to5 pattern table for maximum performance.
 */

#include <poker_eval/core/combo_7to5_hilo.h>
#include <poker_eval/core/combo_7to5.h>
#include <poker_eval/core/canonical_5card.h>
#include <stdlib.h>
#include <string.h>

combo_7to5_hilo_t *combo_7to5_hilo_create(mask_t seven_cards, bool track_best)
{
    if (mask_popcount(seven_cards) != 7)
    {
        return NULL;
    }

    combo_7to5_hilo_t *gen = malloc(sizeof(combo_7to5_hilo_t));
    if (!gen)
    {
        return NULL;
    }

    gen->seven_cards = seven_cards;
    gen->current_combination = 0;
    gen->exhausted = false;
    gen->track_best = track_best;

    /* Initialize best result */
    memset(&gen->best_result, 0, sizeof(hilo_result_t));
    gen->best_result.hi_value = EVAL_INVALID;
    gen->best_result.lo_value = EVAL_INVALID;
    gen->best_result.has_low = false;

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

bool evaluate_hilo_single_pass(mask_t five_cards, hilo_result_t *result)
{
    if (!result || mask_popcount(five_cards) != 5)
    {
        return false;
    }

    /* Evaluate high hand using canonical evaluator */
    result->hi_value = evaluate_5card_canonical(five_cards);
    result->hi_class = eval_get_hand_class(result->hi_value);

    /* For low evaluation, we need to check 8-or-better qualification */
    /* Simple implementation: check if we have 5 cards ≤ 8 with no pairs */

    int ranks[5];
    int rank_count = 0;
    bool low_eligible = true;

    /* Extract ranks and check low eligibility */
    for (int card = 0; card < 52; card++)
    {
        if (mask_is_set(five_cards, card))
        {
            int rank = card % 13; /* 0=2, 1=3, ..., 12=A */

            /* Convert to low ranking: A=1, 2=2, 3=3, 4=4, 5=5, 6=6, 7=7, 8=8 */
            int low_rank;
            if (rank == 12)
            { /* Ace */
                low_rank = 1;
            }
            else if (rank <= 6)
            { /* 2-8 */
                low_rank = rank + 2;
            }
            else
            { /* 9, T, J, Q, K */
                low_eligible = false;
                break;
            }

            /* Check for duplicates */
            for (int i = 0; i < rank_count; i++)
            {
                if (ranks[i] == low_rank)
                {
                    low_eligible = false;
                    break;
                }
            }

            if (!low_eligible)
                break;
            ranks[rank_count++] = low_rank;
        }
    }

    if (low_eligible && rank_count == 5)
    {
        /* Sort ranks for low evaluation */
        for (int i = 0; i < 4; i++)
        {
            for (int j = i + 1; j < 5; j++)
            {
                if (ranks[i] > ranks[j])
                {
                    int temp = ranks[i];
                    ranks[i] = ranks[j];
                    ranks[j] = temp;
                }
            }
        }

        /* Calculate low value (lower is better) */
        result->lo_value = ranks[4] * 10000 + ranks[3] * 1000 + ranks[2] * 100 + ranks[1] * 10 + ranks[0];
        result->has_low = true;
    }
    else
    {
        result->lo_value = EVAL_INVALID;
        result->has_low = false;
    }

    return true;
}

bool combo_7to5_hilo_next(combo_7to5_hilo_t *gen, mask_t *five_combo, hilo_result_t *result)
{
    if (!gen || !five_combo || !result || gen->exhausted)
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

    /* Evaluate Hi/Lo in single pass */
    if (!evaluate_hilo_single_pass(*five_combo, result))
    {
        gen->current_combination++;
        return false;
    }

    /* Update best result if tracking */
    if (gen->track_best)
    {
        bool is_better = false;

        /* First result or better high hand */
        if (gen->best_result.hi_value == EVAL_INVALID ||
            result->hi_value > gen->best_result.hi_value)
        {
            is_better = true;
        }
        /* Same high hand but better low */
        else if (result->hi_value == gen->best_result.hi_value)
        {
            if (result->has_low && !gen->best_result.has_low)
            {
                is_better = true;
            }
            else if (result->has_low && gen->best_result.has_low &&
                     result->lo_value < gen->best_result.lo_value)
            {
                is_better = true;
            }
        }

        if (is_better)
        {
            gen->best_result = *result;
        }
    }

    gen->current_combination++;
    return true;
}

bool combo_7to5_hilo_get_best(const combo_7to5_hilo_t *gen, mask_t *best_combo, hilo_result_t *best_result)
{
    if (!gen || !best_result || !gen->track_best || gen->best_result.hi_value == EVAL_INVALID)
    {
        return false;
    }

    *best_result = gen->best_result;
    /* Note: best_combo would need to be stored separately if needed */
    if (best_combo)
    {
        *best_combo = MASK_EMPTY; /* Not tracked in this implementation */
    }

    return true;
}

bool combo_7to5_hilo_reset(combo_7to5_hilo_t *gen)
{
    if (!gen)
        return false;

    gen->current_combination = 0;
    gen->exhausted = false;

    if (gen->track_best)
    {
        memset(&gen->best_result, 0, sizeof(hilo_result_t));
        gen->best_result.hi_value = EVAL_INVALID;
        gen->best_result.lo_value = EVAL_INVALID;
        gen->best_result.has_low = false;
    }

    return true;
}

void combo_7to5_hilo_destroy(combo_7to5_hilo_t *gen)
{
    if (gen)
    {
        free(gen);
    }
}
