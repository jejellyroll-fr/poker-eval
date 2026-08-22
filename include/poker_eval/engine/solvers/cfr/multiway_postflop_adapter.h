#ifndef POKER_EVAL_MULTIWAY_POSTFLOP_ADAPTER_H
#define POKER_EVAL_MULTIWAY_POSTFLOP_ADAPTER_H

#include "cfr_core.h"
#include <poker_eval/core/eval_context.h>
#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/economics/rake.h>
#include <poker_eval/solver/pe_range.h>

#if !defined(_WIN32)
#include <poker_eval/core/pthread_compat.h>
#endif

#ifndef MPF_TREE_ACTION_MAX
#define MPF_TREE_ACTION_MAX 16
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define MPF_MAX_PLAYERS 7
#define MPF_MAX_BET_SIZES 6

struct mpf_tree_def_t;
struct mpf_state_s;

/* One possible joint deal of the private hands, with its probability. */
typedef struct mpf_private_deal_t
{
    mask_t hole[MPF_MAX_PLAYERS];
    double weight;
} mpf_private_deal_t;
struct mpf_perf_stats_pool_t;

#if !defined(_WIN32)
#define MPF_NODE_CACHE_SLOTS 8
#endif

enum
{
    MPF_ACTION_FOLD = 0,
    MPF_ACTION_CALL = 1,
    MPF_ACTION_RAISE_BASE = 2,
    /* Distinct, non-colliding action id reserved for a pot-limit effective
     * all-in candidate (auto-jam when the remaining stack is <= threshold * pot).
     * Sits past the raise band so it never aliases a raise index. */
    MPF_ACTION_ALL_IN = MPF_ACTION_RAISE_BASE + MPF_MAX_BET_SIZES
};

typedef enum
{
    MPF_STREET_PREFLOP = 0,
    MPF_STREET_FLOP,
    MPF_STREET_TURN,
    MPF_STREET_RIVER,
    MPF_STREET_SHOWDOWN
} mpf_street_t;

typedef enum
{
    MPF_RULE_HOLDEM = 0,
    MPF_RULE_PLO4,
    MPF_RULE_PLO5,
    MPF_RULE_PLO6,
    MPF_RULE_SHORTDECK,
} mpf_rule_t;

typedef struct
{
    uint64_t apply_action_calls;
    uint64_t state_clone_ops;
    uint64_t state_heap_allocs;
    uint64_t state_cache_hits;
    uint64_t state_cache_misses;
    uint64_t street_transitions;
    uint64_t round_resets;
    uint64_t board_updates;
    uint64_t utility_computations;
    uint64_t tree_snapshot_applies;
} mpf_perf_stats_t;

typedef struct
{
    int defined;
    int has_invested;
    int has_round;
    int has_active;
    int has_pot;
    int has_to_call;
    int has_current_bet;
    int has_raises;
    int has_to_act;
    double invested[MPF_MAX_PLAYERS];
    double round_contrib[MPF_MAX_PLAYERS];
    int active[MPF_MAX_PLAYERS];
    double pot;
    double to_call;
    double current_bet;
    int raises_made;
    int to_act;
} mpf_preflop_cfg_t;

typedef struct
{
    const EvalContext *ctx;
    mpf_rule_t rules;

    int num_players;
    int button_index;
    mpf_street_t start_street;

    mask_t hole[MPF_MAX_PLAYERS];
    int hole_specified[MPF_MAX_PLAYERS];

    /*
     * Private range per player (RNG-02). Borrowed: the caller owns it and must
     * keep it alive for the life of the game.
     *
     * A range and a fixed hole say the same thing when the range holds one
     * combo, and that is the only case supported so far — mpf_build_game
     * materialises it into hole[p]. A range with several combos is refused
     * rather than collapsed onto its first entry: solving one hand while the
     * caller asked for a range would produce numbers that look like a range
     * solve. RNG-03 adds the root private chance that makes them meaningful.
     *
     * The range must have been through pe_solver_range_prepare().
     */
    const pe_range_t *range[MPF_MAX_PLAYERS];
    int board_cards[5];
    int board_card_count;

    double stacks[MPF_MAX_PLAYERS];
    double sb;
    double bb;
    double ante;

    double bet_sizes_common[MPF_MAX_BET_SIZES];
    int bet_size_count_common;
    int raise_cap;

    int enable_pot_sizing; /* 0 = montants absolus, 1 = fractions du pot */
    rake_config_t rake;

    mpf_preflop_cfg_t preflop;

    /* FEAT-06: pot-limit effective all-in + dynamic STPR rules */
    int is_pot_limit;                 /* 0 (or -1) = auto-detect from rules (PLO4/5/6), 1 = forced on */
    double committal_threshold_percent; /* auto-jam when remaining stack <= threshold% * pot */

    struct mpf_tree_def_t *tree;
    int tree_enforced;
    mpf_perf_stats_t *perf_stats;
    struct mpf_perf_stats_pool_t *perf_pool;
    int enable_chance_nodes; /* deal turn/river runouts via chance instead of a fixed board */

    /* FEAT-14 (#150): folded-range card bunching estimator. When
     * enable_card_bunching is set, turn/river chance deals are weighted by
     * the probability that each unseen card survived the folded players'
     * hands (i.e. it is NOT statistically depleted from the stub). Only
     * players who folded preflop with an *unknown* hole (hole mask not fully
     * specified) are modeled: fully-specified holes are already removed
     * deterministically by the unused-card enumeration. folded_range_prob[p]
     * must hold, for each card index 0..51, the probability that card was in
     * player p's initial hand, conditioned on the action that made them fold
     * (e.g. derived from player p's preflop opening range distribution). */
    int enable_card_bunching;
    int folded_range_provided[MPF_MAX_PLAYERS];
    double folded_range_prob[MPF_MAX_PLAYERS][52];
} mpf_config_t;

typedef struct mpf_state_s
{
    const EvalContext *ctx;
    mpf_rule_t rules;

    int num_players;
    mpf_street_t street;

    mask_t board_mask;
    mask_t hole[MPF_MAX_PLAYERS];

    int board_cards[5];
    int board_revealed;
    int total_hole_cards;

    double stacks[MPF_MAX_PLAYERS];
    double invested[MPF_MAX_PLAYERS];
    double round_contrib[MPF_MAX_PLAYERS];
    int active[MPF_MAX_PLAYERS];
    int acted_this_round[MPF_MAX_PLAYERS];

    double pot;
    double to_call;
    double current_bet;
    int raises_made;

    int button_index;
    int sb_index;
    int bb_index;
    int first_to_act;
    int to_act;

    double sb;
    double bb;
    double ante;

    double bet_sizes[MPF_MAX_BET_SIZES];
    int bet_size_count;
    int raise_cap;
    int enable_pot_sizing;
    rake_config_t rake;
    double base_bet_sizes[MPF_MAX_BET_SIZES];
    int base_bet_size_count;
    int base_enable_pot_sizing;

    /* FEAT-06: pot-limit effective all-in + dynamic STPR rules */
    int is_pot_limit;                 /* 0 (or -1) = auto-detect from rules (PLO4/5/6), 1 = forced on */
    double committal_threshold_percent; /* auto-jam when remaining stack <= threshold% * pot */
    double stpr;                      /* dynamic stack-to-pot ratio at this node (current_stack / pot) */

    double utilities[MPF_MAX_PLAYERS];
    int util_ready;

    struct mpf_tree_def_t *tree;
    int tree_enabled;
    int tree_node_idx;
    mpf_perf_stats_t *perf_stats;
    struct mpf_state_s *action_cache[MPF_TREE_ACTION_MAX];
    int heap_owned;

    /* FEAT-10 (#146): sparse index of distinct committed-stack configurations.
       Owned by the game root; shared (read-only) by all derived states so the
       id namespace is stable across the whole traversal. */
    struct mpf_stack_index_t *stack_index;
    /* Cached sparse config id for this state (0 until resolved by
       mpf_state_resolve_cfg_id). Folded into the infoset key so asymmetric
       stacks become distinct-but-deduplicated infosets. */
    uint32_t stack_cfg_id;
    /* Set only on the root state: it owns (and must free) stack_index. */
    int owns_stack_index;

    /*
     * Root private chance (RNG-03).
     *
     * One entry per joint deal that is actually possible: the cartesian
     * product of the players' ranges, minus every combination where two
     * players hold the same card or a card already on the board. Weights are
     * the product of the per-player weights, renormalised over what survives.
     *
     * Owned by the root state. `private_children` caches the dealt states the
     * same way chance_children does for board cards, but sized by the number
     * of deals rather than by 52.
     */
    struct mpf_private_deal_t *private_deals;
    struct mpf_state_s **private_children;
    int private_deal_count;
    /* Set on the root when there is a deal to make; cleared by apply_chance. */
    int private_pending;

    /* FEAT-03: real chance nodes */
    int keyed_mode;            /* use infoset keys instead of raw state pointers */
    struct mpf_state_s *key_map_owner; /* game instance owning keyed states */
    int enable_chance_nodes;   /* deal turn/river runouts via chance */
    int chance_pending;        /* next street transition deals a card (chance state) */
    int chance_children_count; /* number of dealt children so far */
    struct mpf_state_s *chance_children[52]; /* cached per-outcome children */
    cfr_storage_t *lock_storage;

    /* FEAT-14 (#150): folded-range card bunching (copied from mpf_config_t).
       Survival probabilities for turn/river chance deals are computed from
       the folded players' per-card range marginals; see
       mpf_bunching_compute_survival(). */
    int enable_card_bunching;
    int folded_range_provided[MPF_MAX_PLAYERS];
    double folded_range_prob[MPF_MAX_PLAYERS][52];
} mpf_state_t;

int mpf_build_game(const mpf_config_t *cfg, cfr_game_t *out_game, mpf_state_t *out_state);
int mpf_apply_locked_strategies(mpf_state_t *root_state, cfr_storage_t *storage);
void mpf_perf_stats_reset(mpf_perf_stats_t *stats);
void mpf_state_cleanup(mpf_state_t *state);

/* FEAT-14 (#150): folded-range card bunching estimator.
 *
 * Computes, for every card index 0..51, the probability that the card
 * survived the folded players' hands — i.e. that it is NOT statistically
 * depleted from the stub deck — as
 *
 *     survival[c] = product over folded players p of (1 - cfg->folded_range_prob[p][c])
 *
 * over the players that are inactive in the preflop configuration AND have
 * folded_range_provided[p] set AND whose hole is not fully specified (a fully
 * specified hole is already removed deterministically). When the estimator is
 * disabled (or no player qualifies) every survival[c] is 1.0, which yields
 * uniform chance deals. The turn/river deal weight of a card is then
 * survival[card] / sum over unseen cards of survival[c], so the weights are
 * automatically normalized at every chance node.
 *
 * Returns 0 on success, -1 on a NULL config or NULL out_survival. */
int mpf_bunching_compute_survival(const mpf_config_t *cfg, double out_survival[52]);

/* FEAT-10 (#146): diagnostic accessors for the sparse stack-config index.
   Return the number of distinct committed-stack configurations discovered so
   far (== highest assigned cfg id) and the current bucket capacity. Both are
   safe to call with a NULL/zero-indexed state (return 0). */
size_t mpf_state_stack_index_count(const mpf_state_t *state);
size_t mpf_state_stack_index_capacity(const mpf_state_t *state);

/* Content-derived infoset key for a state (the same hash the solver uses to
   index storage, including the FEAT-10 sparse stack-config id). Exposed so
   tests/diagnostics can locate a state's storage entry without reaching into
   the adapter's static helpers. */
uint64_t mpf_state_infoset_key(const mpf_state_t *state);

struct mpf_perf_stats_pool_t *mpf_perf_stats_pool_create(int max_threads_hint);
void mpf_perf_stats_pool_destroy(struct mpf_perf_stats_pool_t *pool);
void mpf_perf_stats_pool_reset(struct mpf_perf_stats_pool_t *pool);
mpf_perf_stats_t *mpf_perf_stats_pool_acquire(struct mpf_perf_stats_pool_t *pool);
void mpf_perf_stats_pool_collect(struct mpf_perf_stats_pool_t *pool, mpf_perf_stats_t *out_total);
void mpf_state_cleanup_cached(mpf_state_t *state);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_MULTIWAY_POSTFLOP_ADAPTER_H */
