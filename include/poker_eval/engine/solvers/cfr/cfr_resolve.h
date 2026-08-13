/*
 * cfr_resolve.h - Subgame re-solving (FEAT-05)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * Re-solve a subtree of an already-solved (blueprint) game without touching the
 * rest of the tree, using value constraints at the subgame boundary.
 *
 * Two modes are provided:
 *
 *  1. Trunk-locked re-solve (pe_cfr_seed_resolve_storage). Every infoset that
 *     does not belong to the subgame is locked to its blueprint strategy with
 *     the FEAT-01 node-locking machinery, so a plain cfr_solve() on the full
 *     game trains the subgame only. Cheap, but unsound in the game-theoretic
 *     sense: the opponent is never allowed to avoid the subgame, so the refined
 *     strategy may be more exploitable than the blueprint.
 *
 *  2. CFR-D gadget re-solve (pe_cfr_resolve_subgame). A gadget is placed on top
 *     of the subgame root: in every boundary infoset the opponent chooses
 *     between entering the subgame ("follow") and taking a fixed payoff equal to
 *     its blueprint counterfactual value ("terminate"). The re-solver must
 *     therefore keep giving the opponent at least its blueprint value in every
 *     infoset, which is exactly the value constraint that makes re-solving
 *     sound (Burch, Johanson & Bowling, "Solving Imperfect Information Games
 *     Using Decomposition", AAAI 2014).
 *
 * The gadget is a decorator over cfr_game_t, so it works with any adapter
 * (multiway postflop, river adapters, hand-rolled games) without changes.
 */

#ifndef POKER_EVAL_CFR_RESOLVE_H
#define POKER_EVAL_CFR_RESOLVE_H

#include "cfr_core.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum number of boundary infosets a single re-solve may constrain. */
#define PE_CFR_RESOLVE_MAX_BOUNDARY 256

/* Return codes shared by the FEAT-05 entry points. */
enum
{
    PE_CFR_RESOLVE_OK = 0,
    PE_CFR_RESOLVE_EINVAL = -1,      /* invalid arguments */
    PE_CFR_RESOLVE_ENOMEM = -2,      /* allocation failure */
    PE_CFR_RESOLVE_UNSUPPORTED = -3, /* e.g. gadget re-solve with num_players > 2 */
    PE_CFR_RESOLVE_ETREE = -4        /* subgame root not reachable / malformed tree */
};

/*
 * A boundary constraint: one opponent infoset at the subgame root.
 *
 * `infoset` is the 64-bit infoset key as produced by the game (i.e. the value
 * cfr_game_t::get_infoset_key returns, or the raw state key when the game does
 * not provide that callback), so it indexes both the blueprint and the re-solve
 * storage.
 *
 * `reach` is the opponent's blueprint reach probability for this infoset; it
 * determines whether the boundary is included in the gadget's chance node.
 * Non-positive values are treated as 0 and such infosets are dropped from the
 * gadget.
 *
 * `cfv` is the blueprint counterfactual value of the infoset for the opponent:
 * the value the re-solve must keep matching. Fill it yourself when you have it
 * from a previous full solve, or let pe_cfr_blueprint_cfv() compute it.
 */
typedef struct pe_cfr_boundary_t
{
    uint64_t infoset;
    double reach;
    double cfv;
} pe_cfr_boundary_t;

/*
 * Description of the subgame to re-solve.
 *
 * `root_state_key` is the state key of the subgame root in the *inner* game.
 * `resolve_player` is the player whose strategy is being refined (0 or 1);
 * the other player is the one the gadget constrains.
 */
typedef struct pe_cfr_subgame_t
{
    uint64_t root_state_key;
    int resolve_player;
    const pe_cfr_boundary_t *boundary;
    size_t boundary_count;
} pe_cfr_subgame_t;

/*
 * Per-infoset outcome of a gadget re-solve.
 *
 * `margin` = blueprint CFV - achieved CFV for the opponent. A value >= 0 means
 * the constraint holds: the opponent gains nothing by entering the subgame, so
 * the refined strategy is safe. A negative margin means the constraint was
 * violated (usually not enough iterations) and is reported rather than hidden.
 */
typedef struct pe_cfr_resolve_margin_t
{
    uint64_t infoset;
    double blueprint_cfv;
    double resolved_cfv;
    double margin;
    double follow_freq; /* opponent's gadget probability of entering the subgame */
} pe_cfr_resolve_margin_t;

typedef struct pe_cfr_resolve_result_t
{
    int iterations;              /* iterations actually run */
    double exploitability;       /* exploitability of the gadget game */
    size_t boundary_count;       /* number of constrained infosets */
    size_t infosets_trained;     /* infosets in the re-solve storage */
    double worst_margin;         /* min over boundary infosets (>= 0 == safe) */
    double mean_margin;
    int constraints_satisfied;   /* 1 when worst_margin >= -tolerance */
    pe_cfr_resolve_margin_t margins[PE_CFR_RESOLVE_MAX_BOUNDARY];
} pe_cfr_resolve_result_t;

/* Tuning knobs for a gadget re-solve. Zero-initialise for defaults. */
typedef struct pe_cfr_resolve_config_t
{
    cfr_config_t cfr;         /* forwarded to cfr_solve (max_iterations etc.) */
    double margin_tolerance;  /* slack allowed on the value constraints
                               * (0 selects PE_CFR_RESOLVE_DEFAULT_TOLERANCE) */
    int lock_trunk;           /* also lock non-subgame infosets to the blueprint */
} pe_cfr_resolve_config_t;

#define PE_CFR_RESOLVE_DEFAULT_TOLERANCE 1e-3

/* --------------------------------------------------------------------------
 * Blueprint counterfactual values
 * -------------------------------------------------------------------------- */

/*
 * Walk `game` once under the blueprint average strategies held in `blueprint`
 * and record, for each infoset of `player`, its counterfactual value: the
 * expected utility of the infoset weighted by the *opponents'* reach only.
 *
 * The values are written back into `boundary[i].cfv` for every entry whose
 * `infoset` is found during the walk, and `boundary[i].reach` receives the
 * accumulated reach of the other players used to normalize the CFV. Entries
 * that are never reached keep a zero reach and a zero CFV.
 *
 * Returns PE_CFR_RESOLVE_OK, or a negative PE_CFR_RESOLVE_* code.
 */
int pe_cfr_blueprint_cfv(cfr_game_t *game,
                         cfr_storage_t *blueprint,
                         int player,
                         void *user_data,
                         pe_cfr_boundary_t *boundary,
                         size_t boundary_count);

/* --------------------------------------------------------------------------
 * Trunk-locked re-solve
 * -------------------------------------------------------------------------- */

/*
 * Copy `blueprint` into `resolve_storage` and lock every infoset that is not
 * part of the subgame rooted at `root_state_key`, so that a subsequent
 * cfr_solve() on the full game refines the subgame only.
 *
 * `out_locked` and `out_free` (both optional) receive the number of locked and
 * of trainable (subgame) infosets.
 *
 * Returns PE_CFR_RESOLVE_OK, or a negative PE_CFR_RESOLVE_* code.
 */
int pe_cfr_seed_resolve_storage(cfr_game_t *game,
                                cfr_storage_t *blueprint,
                                cfr_storage_t *resolve_storage,
                                uint64_t root_state_key,
                                void *user_data,
                                size_t *out_locked,
                                size_t *out_free);

/*
 * Collect the infoset keys reachable from `root_state_key` (inclusive).
 * Pass out_keys = NULL to only count them. `*out_count` always receives the
 * total number of distinct infosets in the subgame, even when it exceeds
 * `max_keys`.
 */
int pe_cfr_subgame_infosets(cfr_game_t *game,
                            uint64_t root_state_key,
                            void *user_data,
                            uint64_t *out_keys,
                            size_t max_keys,
                            size_t *out_count);

/* --------------------------------------------------------------------------
 * CFR-D gadget re-solve
 * -------------------------------------------------------------------------- */

/*
 * Re-solve `subgame` of `game` with the CFR-D gadget.
 *
 * `blueprint`  solved storage used for the boundary values and, when
 *              config->lock_trunk is set, for the locked trunk strategy.
 * `resolve_storage` receives the refined strategy. It is seeded from the
 *              blueprint, so the subgame infosets start from the blueprint
 *              rather than from scratch.
 * `out_result` optional; filled with per-infoset margins and diagnostics.
 *
 * Only 2-player games are supported: for num_players > 2 there is no single
 * opponent counterfactual value to constrain, and the call returns
 * PE_CFR_RESOLVE_UNSUPPORTED (use the trunk-locked mode instead).
 *
 * Returns PE_CFR_RESOLVE_OK, or a negative PE_CFR_RESOLVE_* code. A successful
 * return does not imply the value constraints hold: check
 * out_result->constraints_satisfied.
 */
int pe_cfr_resolve_subgame(cfr_game_t *game,
                           cfr_storage_t *blueprint,
                           cfr_storage_t *resolve_storage,
                           const pe_cfr_subgame_t *subgame,
                           const pe_cfr_resolve_config_t *config,
                           void *user_data,
                           pe_cfr_resolve_result_t *out_result);

/*
 * Build the gadget game without solving it, for callers that want to drive
 * cfr_solve() themselves (custom monitoring, metrics, checkpoints).
 *
 * `out_game` is filled with a cfr_game_t whose root is the gadget root; it
 * stays valid until pe_cfr_gadget_destroy(). The gadget keeps a pointer to
 * `game` and to the boundary array inside `subgame`, both of which must outlive
 * the handle.
 */
typedef struct pe_cfr_gadget_t pe_cfr_gadget_t;

int pe_cfr_gadget_create(cfr_game_t *game,
                         const pe_cfr_subgame_t *subgame,
                         void *user_data,
                         pe_cfr_gadget_t **out_gadget,
                         cfr_game_t *out_game);

void pe_cfr_gadget_destroy(pe_cfr_gadget_t *gadget);

/*
 * Read the opponent's gadget decision at a boundary infoset after (or during) a
 * solve: `out_follow` receives the average probability of entering the subgame.
 * Returns PE_CFR_RESOLVE_OK when the infoset belongs to the gadget.
 */
int pe_cfr_gadget_follow_frequency(const pe_cfr_gadget_t *gadget,
                                   cfr_storage_t *storage,
                                   uint64_t infoset,
                                   double *out_follow);

/* Print a re-solve result (debug helper). */
void pe_cfr_resolve_print(const pe_cfr_resolve_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_CFR_RESOLVE_H */
