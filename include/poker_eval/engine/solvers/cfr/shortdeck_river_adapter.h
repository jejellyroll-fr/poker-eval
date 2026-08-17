/* shortdeck_river_adapter.h
 * Adapter Short Deck Hold'em — HU River (CFR)
 *
 * Ce header expose :
 *  - l'état de jeu shortdeck_river (shortdeck_state_t)
 *  - la fabrique de jeu CFR : shortdeck_build_game(...)
 *
 * Hypothèses minimales alignées sur shortdeck_river_adapter.c :
 *   - mask_t : bitmask de cartes (64 bits)
 *   - eval_t : score d'évaluation (32 bits)
 *   - EvalContext : contexte opaque de l'évaluateur 7→5 existant
 *   - cfr_game_t : structure opaque du moteur CFR (définie côté solver)
 *
 * © poker-eval
 */

#ifndef POKER_EVAL_SHORTDECK_RIVER_ADAPTER_H
#define POKER_EVAL_SHORTDECK_RIVER_ADAPTER_H

#include <poker_eval/core/eval_context.h>
#include <poker_eval/engine/solvers/cfr/cfr_core.h>
#include <poker_eval/engine/solvers/cfr/strength_bucketing.h>
#include <poker_eval/engine/solvers/cfr/board_texture.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* --- Paramètres de bornage (adaptables si besoin) -------------------------- */

/* Nombre max de tailles de mise autorisées dans l’adapter (bet fractions) */
#ifndef SD_MAX_BET_SIZES
#define SD_MAX_BET_SIZES 8
#endif

/* Nombre max de seuils de "coarse" bucket (mode 3) pour affiner p_eval */
#ifndef SD_MAX_BUCKET_THRESH
#define SD_MAX_BUCKET_THRESH 16
#endif

    /* --- État de jeu : Short Deck HU River ------------------------------------ */
    /*
     * Champs exactly match l’implémentation .c :
     *  - hist : historique codé en bits (check/check, bet/call, fold…) utilisé
     *           pour détecter terminalité & reconstruire la clé d’infoset.
     *  - to_act : joueur au tour (0/1)
     *  - pot / to_call : montants (unit pot = 1.0 au start)
     *  - raise_cap / raises_left : cap de relances et compteur restant
     *  - n_bet_sizes + bet_fracs[] : tailles de mise relatives au pot
     *  - bucket_mode/bins/thresholds : schémas de bucketing (classes river)
     *  - ctx/h0/h1/board : contexte d’évaluation + cartes privées & board
     */
    typedef struct shortdeck_state_s
    {
        /* Évaluateur et cartes */
        const EvalContext *ctx;
        mask_t h0;    /* main privée joueur 0 */
        mask_t h1;    /* main privée joueur 1 */
        mask_t board; /* board (5 cartes) */

        /* Tour de jeu & historique encodé en bits */
        int to_act;    /* 0 ou 1 */
        uint32_t hist; /* bits d’historique (voir .c pour le codage exact) */

        /* Pot & montants en jeu */
        double pot;     /* taille actuelle du pot (unit = 1.0 au start) */
        double to_call; /* montant à payer par le joueur actif (0.0 si libre) */

        /* Politique de relances */
        int raise_cap;   /* nombre max de relances (cap) par street */
        int raises_left; /* relances restantes dans la séquence courante */

        /* Tailles de mise (fractions du pot) */
        int n_bet_sizes;                    /* nombre de tailles actives */
        double bet_fracs[SD_MAX_BET_SIZES]; /* ex : {1/3, 1/2, 3/4, 1.0} */

        /* Bucketing river (voir .c : modes 0..3) */
        int bucket_mode;                              /* 0: none, 1: board, 2: board+player, 3: coarse,
                                                       * 5: strength buckets EHS/EHS2 (FEAT-13),
                                                       * 6: board-texture merging (FEAT-13),
                                                       * 7: strength buckets + texture merging (FEAT-13) */
        uint8_t bucket_bins;                          /* nb de classes "coarse" (fallback si pas de thresholds) */
        uint16_t bucket_thresh_count;                 /* nb de seuils dans bucket_thresh[] */
        uint32_t bucket_thresh[SD_MAX_BUCKET_THRESH]; /* seuils ascendants sur l’offset intra-classe */

        /* FEAT-13 (#190): abstraction multi-street force + texture.
         * bucket_mode == 5 : strength buckets EHS/EHS2 (pe_strength_table_t),
         *   entraînée/chargée par deal et partagée entre deals (l'état ne la
         *   possède pas) ; NULL => repli sur le mode 2.
         * bucket_mode == 6 : fusion texture — pe_board_texture_id() du board
         *   est XORE dans la clé d'infoset pour merger les boards
         *   textures-indistinctes au niveau texture_level.
         * bucket_mode == 7 : combinaison des deux (strength + texture). */
        pe_strength_table_t *strength_table;
        int texture_level; /* pe_texture_filter_level_t, 0 = disabled */

    } shortdeck_state_t;

    /* --- Fabrique de jeu : construit un cfr_game_t prêt à itérer --------------- */
    /*
     * Initialise out_state avec les paramètres par défaut (cap, tailles de mise,
     * mode de bucket), et remplit out_game->vt (pointeurs is_terminal, num_actions,
     * apply_action, infoset_key) + state_size + initial_state.
     *
     * Paramètres :
     *   ctx    : contexte d’évaluation 7→5 (déjà initialisé dans ton core)
     *   h0,h1  : mains privées (bitmasks) des deux joueurs
     *   board  : board (5 cartes) en bitmask
     *   out_game  : (OUT) structure CFR du jeu construit
     *   out_state : (OUT) état initial (doit rester vivant tant que le jeu existe)
     */
    void shortdeck_build_game(const EvalContext *ctx,
                              mask_t h0,
                              mask_t h1,
                              mask_t board,
                              cfr_game_t *out_game,
                              shortdeck_state_t *out_state);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* POKER_EVAL_SHORTDECK_RIVER_ADAPTER_H */
