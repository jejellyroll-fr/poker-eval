/*
 * Copyright (C) 2024
 * Version finale pour Triple Draw - utilise la logique correcte
 */

#ifndef __EVAL_TRIPLE_DRAW_H__
#define __EVAL_TRIPLE_DRAW_H__

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/games/eval_low27.h>
#include <poker_eval/games/eval_low.h>

/* Version finale et correcte des fonctions d'évaluation Triple Draw */

/* 2-7 Triple Draw evaluation - version correcte */
static inline LowHandVal
TripleDraw_27_EVAL_INLINE(StdDeck_CardMask cards, int n_cards)
{
    if (n_cards != 5)
    {
        return LowHandVal_NOTHING;
    }

    /* Pour 2-7, on utilise exactement la même logique que INNER_LOOP_LOWBALL27
     * StdDeck_Lowball27_EVAL_N retourne un HandVal, on le cast en LowHandVal
     * La logique de comparaison dans INNER_LOOP gère correctement les valeurs
     */
    HandVal hval = StdDeck_Lowball27_EVAL_N(cards, n_cards);
    return (LowHandVal)hval;
}

/* A-5 Triple Draw evaluation - version correcte */
static inline LowHandVal
TripleDraw_A5_EVAL_INLINE(StdDeck_CardMask cards, int n_cards)
{
    if (n_cards != 5)
    {
        return LowHandVal_NOTHING;
    }

    /* Pour A-5, on utilise directement l'évaluation A-5 lowball
     * StdDeck_Lowball_EVAL retourne déjà un LowHandVal correct
     */
    return StdDeck_Lowball_EVAL(cards, n_cards);
}

/* Utility function to check if a hand is valid for Triple Draw */
static inline int
TripleDraw_IsValidHand(StdDeck_CardMask cards, int n_cards)
{
    return (n_cards >= 0 && n_cards <= 5 &&
            StdDeck_numCards(cards) == n_cards);
}

/* Get the number of cards that need to be dealt to complete a hand */
static inline int
TripleDraw_GetCardsNeeded(StdDeck_CardMask cards)
{
    int current_cards = StdDeck_numCards(cards);
    return (current_cards < 5) ? (5 - current_cards) : 0;
}

#endif /* __EVAL_TRIPLE_DRAW_H__ */
