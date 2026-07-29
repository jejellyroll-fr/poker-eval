/* omaha8_river_adapter.h
 * Omaha Hi/Lo 8-or-better — HU River adapter pour CFR
 *
 * Expose :
 *   - l’état de jeu o8_state_t
 *   - la fabrique o8_build_game() pour construire un cfr_game_t
 *
 * © poker-eval
 */

#ifndef POKER_EVAL_OMAHA8_RIVER_ADAPTER_H
#define POKER_EVAL_OMAHA8_RIVER_ADAPTER_H

#include <poker_eval/core/eval_context.h>
#include <poker_eval/engine/solvers/cfr/cfr_core.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* --- Paramètres bornes --------------------------------------------------- */

#ifndef O8_MAX_BET_SIZES
#define O8_MAX_BET_SIZES 8
#endif

#ifndef O8_MAX_BUCKET_THRESH
#define O8_MAX_BUCKET_THRESH 16
#endif

    /* --- État de jeu Omaha8 River -------------------------------------------- */

    typedef struct o8_state_s
    {
        /* Cartes privées et board */
        mask_t h0, h1;
        mask_t board;

        /* Historique & tour de jeu */
        uint32_t hist;
        int to_act;

        /* Pot & mises */
        double pot;
        double to_call;

        /* Relances */
        int raise_cap;
        int raises_left;

        /* Tailles de mise */
        int n_bet_sizes;
        double bet_fracs[O8_MAX_BET_SIZES];

        /* Contexte d’évaluation */
        const EvalContext *ctx;

        /* Bucketing */
        unsigned char bucket_mode;
        unsigned char bucket_bins;
        unsigned char bucket_thresh_count;
        uint32_t bucket_thresh[O8_MAX_BUCKET_THRESH];

        /* Features supplémentaires (flags, encodeurs) */
        uint32_t extra_feats;

    } o8_state_t;

    /* --- Fabrique ------------------------------------------------------------ */

    /*
     * Construit un jeu Omaha8 HU River adapté au CFR.
     *
     * ctx       : contexte d’évaluation
     * h0, h1    : mains privées
     * board     : board complet (5 cartes)
     * out_game  : (OUT) CFR game avec pointeurs virtuels configurés
     * out_state : (OUT) état initial (doit rester vivant)
     */
    void o8_build_game(const EvalContext *ctx,
                       mask_t h0,
                       mask_t h1,
                       mask_t board,
                       cfr_game_t *out_game,
                       o8_state_t *out_state);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* POKER_EVAL_OMAHA8_RIVER_ADAPTER_H */
