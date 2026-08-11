#ifndef POKER_EVAL_MULTIWAY_POSTFLOP_ADAPTER_H
#define POKER_EVAL_MULTIWAY_POSTFLOP_ADAPTER_H

#include "cfr_core.h"
#include <poker_eval/core/eval_context.h>
#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/economics/rake.h>

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
struct mpf_perf_stats_pool_t;

#if !defined(_WIN32)
#define MPF_NODE_CACHE_SLOTS 8
#endif

enum
{
    MPF_ACTION_FOLD = 0,
    MPF_ACTION_CALL = 1,
    MPF_ACTION_RAISE_BASE = 2
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

    /* Terminal rake applied to pots won at showdown and on folds.
     * percentage <= 0.0 disables rake; no_flop_no_drop skips rake when
     * the pot never saw a flop. */
    rake_config_t rake;

    mpf_preflop_cfg_t preflop;

    struct mpf_tree_def_t *tree;
    int tree_enforced;
    mpf_perf_stats_t *perf_stats;
    struct mpf_perf_stats_pool_t *perf_pool;
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
    /* Copied from mpf_config_t at build time; propagates to cloned states */
    rake_config_t rake;
    double base_bet_sizes[MPF_MAX_BET_SIZES];
    int base_bet_size_count;
    int base_enable_pot_sizing;

    double utilities[MPF_MAX_PLAYERS];
    int util_ready;

    struct mpf_tree_def_t *tree;
    int tree_enabled;
    int tree_node_idx;
    mpf_perf_stats_t *perf_stats;
    struct mpf_state_s *action_cache[MPF_TREE_ACTION_MAX];
    int heap_owned;
    cfr_storage_t *lock_storage; /* set via mpf_apply_locked_strategies */
} mpf_state_t;

int mpf_build_game(const mpf_config_t *cfg, cfr_game_t *out_game, mpf_state_t *out_state);

/*
 * Wire the tree's locked strategies (is_locked / locked_strategy fields on
 * player nodes) into a CFR storage for a solve rooted at `root_state`.
 * The root node lock is applied immediately; the remaining nodes are locked
 * lazily on first entry during the solve (their state keys are only known
 * then). Returns the number of locks applied immediately.
 */
int mpf_apply_locked_strategies(mpf_state_t *root_state, cfr_storage_t *storage);
void mpf_perf_stats_reset(mpf_perf_stats_t *stats);
void mpf_state_cleanup(mpf_state_t *state);
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
