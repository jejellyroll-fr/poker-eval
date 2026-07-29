/* stud8_river_adapter.h
 * Header pour l’adaptateur Seven-Card Stud Hi/Lo 8-or-better (river HU)
 */

#ifndef STUD8_RIVER_ADAPTER_H
#define STUD8_RIVER_ADAPTER_H

#include <stdint.h>
#include <stdbool.h>
#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/core/eval.h>
#include <poker_eval/core/eval_context.h>
#include <poker_eval/engine/solvers/cfr/cfr_core.h>

/* bornes raisonnables */
#define S8_MAX_BET_SIZES 8
#define S8_MAX_BUCKET_THRESH 16

/* état du jeu Stud8 river */
typedef struct
{
    /* cartes privées complètes (7-card mask) */
    mask_t seven0;
    mask_t seven1;

    /* pot et mises */
    double pot;
    double to_call;

    /* historique codé en bits (check/bet/fold/call) */
    uint32_t hist;

    /* joueur à agir (0 ou 1) */
    int to_act;

    /* gestion des relances */
    int raises_left;
    int raise_cap;

    /* tailles de mise autorisées (fractions du pot) */
    int n_bet_sizes;
    double bet_fracs[S8_MAX_BET_SIZES];

    /* contexte d’évaluation (hi) */
    const EvalContext *ctx;

    /* paramètres de bucketing */
    int bucket_mode;           /* 0 = none, 3 = coarse eval buckets */
    unsigned char bucket_bins; /* nb de bins si auto */
    unsigned char bucket_thresh_count;
    unsigned int bucket_thresh[S8_MAX_BUCKET_THRESH];
} s8_state_t;

/* Fabrique un jeu CFR pour Stud8 river HU */
void s8_build_game(const EvalContext *ctx,
                   mask_t seven0,
                   mask_t seven1,
                   cfr_game_t *out_game,
                   s8_state_t *out_state);

#endif /* STUD8_RIVER_ADAPTER_H */
