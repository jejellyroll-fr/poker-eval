#ifndef ENUMERATE_EEDC_H
#define ENUMERATE_EEDC_H

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/pokereval_export.h>

/*
 * Version optimisée EEDC (Efficient Exhaustive Deck Construction)
 * Génère les boards sans ANY_SET, à partir du deck restant.
 *
 * Optimisé pour : Hold'em, Omaha et variantes (cartes communes)
 * Non optimisé pour : Stud, Draw (utilise la version classique car
 *                     ces jeux nécessitent de gérer les permutations
 *                     de distribution des cartes individuelles)
 */
POKEREVAL_EXPORT int enumExhaustive_eedc(
    enum_game_t game,
    StdDeck_CardMask pockets[],
    StdDeck_CardMask board,
    StdDeck_CardMask dead,
    int npockets,
    int nboard,
    int orderflag,
    enum_result_t *result);

/* Optimized version for Omaha */
POKEREVAL_EXPORT int enumExhaustive_eedc_omaha_opt(
    enum_game_t game,
    StdDeck_CardMask pockets[],
    StdDeck_CardMask board,
    StdDeck_CardMask dead,
    int npockets,
    int nboard,
    int orderflag,
    enum_result_t *result);

#endif // ENUMERATE_EEDC_H
