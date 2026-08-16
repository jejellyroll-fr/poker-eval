/* omaha_river_adapter.h
 * Omaha Hold’em (High-only) — HU River adapter pour CFR
 *
 * Expose :
 *   - l’état de jeu omaha_river_state_t
 *   - la fabrique omaha_build_game() pour créer un cfr_game_t
 *
 * © poker-eval
 */

#ifndef POKER_EVAL_OMAHA_RIVER_ADAPTER_H
#define POKER_EVAL_OMAHA_RIVER_ADAPTER_H

#include <poker_eval/core/eval_context.h>
#include <poker_eval/engine/solvers/cfr/cfr_core.h>
#include <poker_eval/engine/solvers/cfr/hand_clustering.h>
#include <poker_eval/engine/solvers/cfr/strength_bucketing.h>
#include <poker_eval/engine/solvers/cfr/board_texture.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* --- Paramètres bornes --------------------------------------------------- */

#ifndef OMAHA_MAX_BET_SIZES
#define OMAHA_MAX_BET_SIZES 8
#endif

#ifndef OMAHA_MAX_BUCKET_THRESH
#define OMAHA_MAX_BUCKET_THRESH 16
#endif

    /* --- État de jeu Omaha River --------------------------------------------- */

    typedef struct omaha_river_state_s
    {
        /* Cartes privées et board */
        mask_t h0, h1;
        mask_t board;

        /* Historique & tour de jeu */
        uint32_t hist; /* bits d’historique */
        int to_act;    /* joueur actif (0/1) */

        /* Pot & mises */
        double pot;
        double to_call;

        /* Relances */
        int raise_cap;
        int raises_left;

        /* Tailles de mise */
        int n_bet_sizes;
        double bet_fracs[OMAHA_MAX_BET_SIZES];

        /* Contexte d’évaluation */
        const EvalContext *ctx;

        /* Bucketing river */
        unsigned char bucket_mode; /* 0: none, 1: board, 2: board+player, 3: coarse,
                                    * 4: k-means clusters (FEAT-04),
                                    * 5: strength buckets EHS/EHS2 (FEAT-13),
                                    * 6: board-texture merging (FEAT-13),
                                    * 7: strength buckets + texture merging (FEAT-13) */
        unsigned char bucket_bins; /* nombre de classes */
        unsigned char bucket_thresh_count;
        uint32_t bucket_thresh[OMAHA_MAX_BUCKET_THRESH];
        int suit_perm[4]; /* suit canonicalization mapping (label -> original suit) */

        /* FEAT-04: abstraction apprise par clustering (bucket_mode == 4).
         * Table entraînée hors solve et partagée entre deals ; l'état ne la
         * possède pas et ne la libère pas. NULL => repli sur le mode 3. */
        pe_bucket_table_t *bucket_table;

        /* FEAT-13 (#190): abstraction multi-street force + texture.
         * bucket_mode == 5 : strength buckets EHS/EHS2 (pe_strength_table_t),
         *   entraînée/chargée par deal et partagée entre deals (l'état ne la
         *   possède pas) ; NULL => repli sur le mode 3.
         * bucket_mode == 6 : fusion texture — pe_board_texture_id() du board
         *   est XORE dans la clé d'infoset pour merger les boards
         *   textures-indistinctes au niveau texture_level. */
        pe_strength_table_t *strength_table;
        int texture_level; /* pe_texture_filter_level_t, 0 = disabled */
    } omaha_river_state_t;

    /* --- Fabrique ------------------------------------------------------------ */

    /*
     * Construit un jeu Omaha HU River adapté au CFR.
     *
     * ctx     : contexte d’évaluation
     * h0,h1   : mains privées des deux joueurs
     * board   : board complet (5 cartes)
     * out_game: (OUT) CFR game avec pointeurs virtuels configurés
     * out_state: (OUT) état initialisé (à conserver vivant)
     */
    void omaha_build_game(const EvalContext *ctx,
                          mask_t h0,
                          mask_t h1,
                          mask_t board,
                          cfr_game_t *out_game,
                          omaha_river_state_t *out_state);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* POKER_EVAL_OMAHA_RIVER_ADAPTER_H */
