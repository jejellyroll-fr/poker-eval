/* razz_river_adapter.h
 * Razz (A-5 lowball) — HU River adapter pour CFR
 *
 * Expose :
 *   - l’état de jeu razz_state_t
 *   - la fabrique razz_build_game() pour configurer un cfr_game_t
 *
 * © poker-eval
 */
#ifndef POKER_EVAL_RAZZ_RIVER_ADAPTER_H
#define POKER_EVAL_RAZZ_RIVER_ADAPTER_H

#include <poker_eval/core/eval_context.h>
#include <poker_eval/engine/solvers/cfr/cfr_core.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* --- Paramètres bornes --------------------------------------------------- */
#ifndef RAZZ_MAX_BET_SIZES
#define RAZZ_MAX_BET_SIZES 8
#endif

#ifndef RAZZ_MAX_BUCKET_THRESH
#define RAZZ_MAX_BUCKET_THRESH 16
#endif

    /* --- État de jeu Razz River ---------------------------------------------- */
    typedef struct razz_state_s
    {
        /* Contexte & cartes (7 cartes complètes par joueur) */
        const EvalContext *ctx;
        mask_t seven0; /* main complète joueur 0 */
        mask_t seven1; /* main complète joueur 1 */

        /* Tour de jeu & historique (bits) */
        int to_act;    /* joueur actif (0/1) */
        uint32_t hist; /* encodage check/bet/call/fold */

        /* Pot & mises */
        double pot;
        double to_call;

        /* Relances */
        int raise_cap;
        int raises_left;

        /* Tailles de mise (fractions du pot) */
        int n_bet_sizes;
        double bet_fracs[RAZZ_MAX_BET_SIZES];

        /* Bucketing river */
        unsigned char bucket_mode; /* 0:none, 1:board, 2:board+player, 3:coarse */
        unsigned char bucket_bins; /* nb de classes si pas de thresholds */
        unsigned char bucket_thresh_count;
        uint32_t bucket_thresh[RAZZ_MAX_BUCKET_THRESH];

    } razz_state_t;

    /* --- Fabrique ------------------------------------------------------------ */
    /*
     * Construit un jeu Razz HU River adapté au CFR.
     *
     * ctx        : contexte d’évaluation
     * seven0/1   : 7 cartes complètes des joueurs
     * out_game   : (OUT) CFR game avec vtables configurées
     * out_state  : (OUT) état initialisé (doit rester vivant)
     */
    void razz_build_game(const EvalContext *ctx,
                         mask_t seven0,
                         mask_t seven1,
                         cfr_game_t *out_game,
                         razz_state_t *out_state);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* POKER_EVAL_RAZZ_RIVER_ADAPTER_H */
