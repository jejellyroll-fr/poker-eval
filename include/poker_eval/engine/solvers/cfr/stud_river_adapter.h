/* stud_river_adapter.h
 * 7-Stud High — HU River adapter pour CFR
 *
 * Expose :
 *   - l’état de jeu stud_state_t
 *   - la fabrique stud_build_game() pour créer un cfr_game_t prêt à itérer
 *
 * © poker-eval
 */
#ifndef POKER_EVAL_STUD_RIVER_ADAPTER_H
#define POKER_EVAL_STUD_RIVER_ADAPTER_H

#include <poker_eval/core/eval_context.h>
#include <poker_eval/engine/solvers/cfr/cfr_core.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* --- Bornes configurables ------------------------------------------------- */
#ifndef STUD_MAX_BET_SIZES
#define STUD_MAX_BET_SIZES 8
#endif

#ifndef STUD_MAX_BUCKET_THRESH
#define STUD_MAX_BUCKET_THRESH 16
#endif

    /* --- État de jeu : 7-Stud HU River -------------------------------------- */
    /* Aligné avec stud_river_adapter.c */
    typedef struct stud_state_s
    {
        /* Évaluateur & cartes (7 cartes complètes par joueur) */
        const EvalContext *ctx;
        mask_t seven0; /* 7 cartes joueur 0 (bitmask) */
        mask_t seven1; /* 7 cartes joueur 1 (bitmask) */

        /* Tour de jeu & historique (bits) */
        int to_act;    /* joueur actif : 0/1 */
        uint32_t hist; /* encodage des actions (check/bet/call/fold/…) */

        /* Pot & montants en jeu */
        double pot;     /* taille actuelle du pot (unit = 1.0 au start) */
        double to_call; /* montant à payer par le joueur actif (0 si libre) */

        /* Politique de relances */
        int raise_cap;   /* cap de relances par séquence */
        int raises_left; /* relances restantes */

        /* Tailles de mise (fractions du pot) */
        int n_bet_sizes;                      /* nombre de tailles actives */
        double bet_fracs[STUD_MAX_BET_SIZES]; /* ex. {1/3, 1/2, 3/4, 1.0} */

        /* Bucketing river */
        unsigned char bucket_mode;                      /* 0: none, 1: board, 2: board+player, 3: coarse */
        unsigned char bucket_bins;                      /* nb de classes si pas de thresholds */
        uint16_t bucket_thresh_count;                   /* nb de seuils utilisés */
        uint32_t bucket_thresh[STUD_MAX_BUCKET_THRESH]; /* seuils ascendants */

    } stud_state_t;

    /* --- Fabrique : construit le jeu CFR ------------------------------------ */
    /*
     * Initialise out_state (params par défaut) et configure out_game (vtables,
     * taille d’état, état initial) pour itérations CFR.
     *
     * ctx        : contexte d’évaluation 7→5
     * seven0/1   : 7 cartes complètes (bitmasks) des deux joueurs
     * out_game   : (OUT) descripteur du jeu CFR (pointeurs de callbacks remplis)
     * out_state  : (OUT) état initial (doit rester vivant pendant l’usage)
     */
    void stud_build_game(const EvalContext *ctx,
                         mask_t seven0,
                         mask_t seven1,
                         cfr_game_t *out_game,
                         stud_state_t *out_state);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* POKER_EVAL_STUD_RIVER_ADAPTER_H */
