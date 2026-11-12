/*
 * pe_plo4_certification.h - heads-up PLO4 river exploitability certificate
 *
 * Issue #283, first tranche.  The module defines a small, fully enumerated
 * heads-up PLO4 river game and measures a fixed strategy against an
 * information-set-consistent best response for both players.  It does not
 * implement a new best response: the measurement goes through
 * pe_exploitability_vector(), so the numbers share the vector lane's
 * definition of a unilateral gap and of the policy value.
 *
 * Game contract
 * -------------
 * - Five-card board, two weighted four-card ranges.
 * - One fixed bet size.  Each decision offers check/bet when nothing is owed
 *   and fold/call when facing the bet.  The resulting tree is
 *
 *       P0: check -> P1: check        -> showdown
 *       P0: check -> P1: bet          -> P0: fold | call -> fold | showdown
 *       P0: bet   -> P1: fold | call  -> fold | showdown
 *
 *   which is the standard river value/bluff tree with a single sizing.
 * - No rake: the pot is returned exactly to its winners.
 * - The fixed strategy is a callback over four information sets:
 *   PE_PLO4_CERT_INFOSET_OPEN_P0 (P0 with nothing owed),
 *   PE_PLO4_CERT_INFOSET_FACING_P0 (P0 facing the bet),
 *   PE_PLO4_CERT_INFOSET_OPEN_P1 (P1 after a check),
 *   PE_PLO4_CERT_INFOSET_FACING_P1 (P1 facing the bet).
 *   The callback returns one probability vector per action, per combo, so an
 *   abstraction-expanded strategy is accepted as-is.
 *
 * Measurement
 * -----------
 * Private hands are drawn from the declared ranges: a hero combo weighs its
 * window weight and the opponent combo weighs its own, and the legal ordered
 * pairs are normalised by their total mass.  Terminal values are conditional
 * on the hero's own combo and net of that player's investment.  The reported
 * values are in chips per hand under that joint distribution, with the usual
 * normalized units alongside (bb/hand, mbb/hand, percent of the reference
 * pot).  For this two-player zero-sum game the raw NashConv is the sum of the
 * two unilateral gaps and the conventional exploitability is half of it.
 *
 * The CLI, the emitted schema and a certified example are documented in
 * docs/cfr/guides/plo4_river_certification.md.
 *
 * A result is labelled PE_PLO4_CERT_VERDICT_CERTIFIED only when the declared
 * tree was enumerated completely and both best responses converged.  Sampling
 * is never used here, so a sampled label exists only to keep an explicit
 * vocabulary for callers that add one later.  Non-convergence and incomplete
 * measurements are reported as NOT_CERTIFIED with their reason, never as an
 * exact number.
 */

#ifndef POKER_EVAL_PE_PLO4_CERTIFICATION_H
#define POKER_EVAL_PE_PLO4_CERTIFICATION_H

#include <stddef.h>
#include <stdint.h>

#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/solver/pe_best_response.h>
#include <poker_eval/solver/pe_omaha_deals.h>
#include <poker_eval/solver/pe_showdown.h>
#include <poker_eval/solver/pe_vector.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Information sets of the declared tree, and the two actions each offers. */
#define PE_PLO4_CERT_INFOSET_OPEN_P0 0u
#define PE_PLO4_CERT_INFOSET_FACING_P0 1u
#define PE_PLO4_CERT_INFOSET_OPEN_P1 2u
#define PE_PLO4_CERT_INFOSET_FACING_P1 3u
#define PE_PLO4_CERT_INFOSET_COUNT 4u
#define PE_PLO4_CERT_ACTION_COUNT 2u

typedef enum
{
    PE_PLO4_CERT_VERDICT_NOT_EVALUATED = 0,
    PE_PLO4_CERT_VERDICT_CERTIFIED,
    PE_PLO4_CERT_VERDICT_NOT_CERTIFIED
} pe_plo4_cert_verdict_t;

typedef enum
{
    PE_PLO4_CERT_REASON_NONE = 0,
    PE_PLO4_CERT_REASON_INVALID_GAME,
    PE_PLO4_CERT_REASON_NOT_CONVERGED,
    PE_PLO4_CERT_REASON_INCOMPLETE,
    PE_PLO4_CERT_REASON_SAMPLED
} pe_plo4_cert_reason_t;

/*
 * Fill `probabilities` with the probability of one action for every private
 * combo, in combo order.  `infoset` is one of the PE_PLO4_CERT_INFOSET_
 * constants.  The row over actions must sum to one for every combo; the
 * evaluator checks that before it measures anything.
 */
typedef int (*pe_plo4_cert_strategy_fn)(void *user, uint16_t infoset,
                                        uint16_t action,
                                        pe_value_vec_t *probabilities);

typedef struct
{
    /* Five-card river board. */
    mask_t board;
    /* Two ranges, player 0 first.  They may differ in length; the shorter one
     * is padded with zero-weight combos internally.  A combo with weight zero
     * is disabled but must still be a legal four-card hand off the board. */
    const pe_omaha_range_t *ranges;
    /* Chips already in the middle at the root. */
    double pot;
    /* Chips each player has already committed, included in `pot`. */
    double invested[2];
    /* Chips still behind. */
    double stacks[2];
    /* The single bet size, in chips, no larger than either remaining stack. */
    double bet_size;
    /* Unit used for the bb and mbb conversions. */
    double big_blind;
    pe_plo4_cert_strategy_fn strategy;
    void *strategy_user;
} pe_plo4_cert_spec_t;

typedef struct
{
    pe_plo4_cert_verdict_t verdict;
    pe_plo4_cert_reason_t reason;
    /* "exact" when the measurement enumerated the declared tree, else
     * "not_certified". */
    const char *mode;
    /* "nash" only for a certified two-player zero-sum measurement, else
     * "none". */
    const char *guarantee;
    /* Human-readable scope of the claim: the declared tree and ranges. */
    const char *game_scope;
    int converged;
    /* Showdown path the exact terminal took for the root range width. */
    pe_showdown_path_t showdown_path;
    /* Padded combo width and the number of live combos per player. */
    pe_combo_count_t combo_count;
    size_t live_combos[2];
    /* Total weight of the legal ordered pairs, sum over h,o of w0(h)w1(o). */
    double joint_mass;
    double pot_reference;
    double big_blind;
    /* Chips per hand under the declared joint range distribution. */
    double policy_value[2];
    double br_value[2];
    double br_gap[2];
    /* Sum of the two unilateral gaps. */
    double nashconv;
    /* nashconv / 2, the conventional heads-up zero-sum exploitability. */
    double exploitability;
    double exploitability_mbb_per_game;
    double exploitability_pct_of_pot;
    /* Best-response fixed-point iterations used for each player. */
    uint32_t br_iterations[2];
} pe_plo4_cert_result_t;

/*
 * Measure the fixed strategy against both best responses.
 *
 * `config` may be NULL for the vector lane defaults; its max_iterations is
 * the BR convergence budget.  Returns PE_SOLVER_OK when the measurement ran,
 * including when the result is labelled NOT_CERTIFIED.  Invalid games return
 * PE_SOLVER_ERR_INVALID_CONFIG; allocation and runtime failures return the
 * matching status and leave `out_result` zeroed.
 */
pe_solver_status_t pe_plo4_cert_evaluate(
    const pe_plo4_cert_spec_t *spec,
    const pe_best_response_vector_config_t *config,
    pe_plo4_cert_result_t *out_result);

const char *pe_plo4_cert_verdict_name(pe_plo4_cert_verdict_t verdict);
const char *pe_plo4_cert_reason_name(pe_plo4_cert_reason_t reason);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_PLO4_CERTIFICATION_H */
