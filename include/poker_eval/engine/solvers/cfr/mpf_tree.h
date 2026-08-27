#ifndef POKER_EVAL_MPF_TREE_H
#define POKER_EVAL_MPF_TREE_H

#include <stddef.h>
#include <stdint.h>

#include <poker_eval/engine/solvers/cfr/multiway_postflop_adapter.h>

#if !defined(_WIN32)
#include <poker_eval/core/pthread_compat.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define MPF_TREE_ACTION_MAX 16

typedef enum
{
    MPF_TREE_NODE_PLAYER = 0,
    MPF_TREE_NODE_CHANCE,
    MPF_TREE_NODE_TERMINAL
} mpf_tree_node_type_t;

typedef enum
{
    MPF_TREE_ACTION_FOLD = 0,
    MPF_TREE_ACTION_CALL,
    MPF_TREE_ACTION_RAISE,
    MPF_TREE_ACTION_CHANCE,
    MPF_TREE_ACTION_TERMINAL
} mpf_tree_action_type_t;

typedef struct mpf_tree_bet_profile_t
{
    char *id;
    double *bet_sizes;
    int bet_size_count;
    int use_pot_sizing;
} mpf_tree_bet_profile_t;

typedef struct mpf_tree_range_combo_t
{
    char *hand;
    double weight;
} mpf_tree_range_combo_t;

/* Spot filter / action morphing rule (FEAT-07, #143). Parsed out of a
   range-profile combo string such as "$cb", "SPR>3", "POS=IP", "BET", "AUTO". */
typedef enum
{
    MPF_SPOT_NONE = 0,
    MPF_SPOT_CB,    /* $cb - use previous street aggressor's active range */
    MPF_SPOT_SPR_GT,/* SPR > value */
    MPF_SPOT_SPR_LT,/* SPR < value */
    MPF_SPOT_POS,   /* position == value */
    MPF_SPOT_BET,   /* BET action morph (raise) */
    MPF_SPOT_AUTO   /* AUTO bet-sizing morph (pot sizing) */
} mpf_tree_spot_kind_t;

typedef enum
{
    MPF_SPOT_POS_INVALID = 0,
    MPF_SPOT_POS_IP,
    MPF_SPOT_POS_OOP
} mpf_tree_spot_pos_t;

typedef struct mpf_tree_spot_rule_t
{
    mpf_tree_spot_kind_t kind;
    double value;            /* numeric operand for SPR thresholds */
    mpf_tree_spot_pos_t pos;/* position for POS= rules */
    int is_cb;              /* set for $cb */
    char *hand;             /* residual hand part of the combo (may be NULL) */
} mpf_tree_spot_rule_t;

typedef struct mpf_tree_range_profile_t
{
    char *id;
    int player; /* -1 si non spécifié */
    mpf_street_t street;
    int street_defined;
    mpf_tree_range_combo_t *combos;
    int combo_count;
    char **aliases;
    int alias_count;
    /* Spot filter / action morphing rules parsed from the combos (FEAT-07). */
    mpf_tree_spot_rule_t *spot_rules;
    int spot_rule_count;
    /* Set when a combo used $cb: the c-bet range is resolved from the range
       profile belonging to the previous street's aggressor. */
    int has_cb;
    char *cb_range_id; /* explicit target profile id for $cb, or NULL */
} mpf_tree_range_profile_t;

typedef struct mpf_tree_action_t
{
    mpf_tree_action_type_t type;
    int size_index;     /* index into node bet sizes for raises */
    double weight;      /* used for chance nodes */
    int next_index;     /* resolved index or -1 */
    char *next_id;      /* temporary before resolution */
    /* FEAT-12: per-action frequency lock. Values in [0,1] pin this action to a
       fixed target frequency; -1.0 means the action is not individually locked
       (its frequency is left free and absorbs the normalized residual). */
    double lock_freq;
} mpf_tree_action_t;

    /* FEAT-12: opponent model loaded from a .json profile (compatible style).
   Keys are node ids; each entry maps action labels (e.g. "RAISE_50",
   "RAISE_100", "CALL") or action indices to target frequencies. */
typedef struct mpf_tree_action_lock_t
{
    int action_index;   /* resolved action index within the node, or -1 */
    char *label;        /* action label as written in the model, or NULL */
    double freq;        /* target frequency in [0,1] */
} mpf_tree_action_lock_t;

typedef struct mpf_tree_node_lock_t
{
    char *node_id;                          /* target node id */
    mpf_tree_action_lock_t *actions;        /* per-action locks */
    int action_count;
} mpf_tree_node_lock_t;

typedef struct mpf_tree_opponent_model_t
{
    char *name;                 /* optional profile name */
    mpf_tree_node_lock_t *nodes;/* per-node lock specs */
    int node_count;
} mpf_tree_opponent_model_t;

typedef struct mpf_tree_snapshot_t
{
    int defined;
    int has_street;
    int has_num_players;
    int has_to_act;
    int has_first_to_act;
    int has_pot;
    int has_to_call;
    int has_current_bet;
    int has_raises_made;
    int has_board;
    int has_board_revealed;
    int has_stacks;
    int has_invested;
    int has_round_contrib;
    int has_active;
    int has_acted;
    int board_len;
    int stacks_len;
    int invested_len;
    int round_contrib_len;
    int active_len;
    int acted_len;
    mpf_street_t street;
    int num_players;
    int to_act;
    int first_to_act;
    double pot;
    double to_call;
    double current_bet;
    int raises_made;
    int board_revealed;
    int board_cards[5];
    double stacks[MPF_MAX_PLAYERS];
    double invested[MPF_MAX_PLAYERS];
    double round_contrib[MPF_MAX_PLAYERS];
    int active[MPF_MAX_PLAYERS];
    int acted[MPF_MAX_PLAYERS];
    int has_snapshot;
} mpf_tree_snapshot_t;

typedef struct mpf_tree_node_t
{
    char *id;
    mpf_tree_node_type_t type;
    mpf_street_t street;
    int acting_player; /* -1 for chance/terminal */
    double *bet_sizes; /* owning array */
    int bet_size_count;
    int use_pot_sizing;
    mpf_tree_action_t *actions;
    int action_count;
    char *bet_profile_id;
    char *range_profile_id;
    const mpf_tree_range_profile_t *range_profile;
    int is_locked;
    double *locked_strategy; /* owning array, NULL when not locked */
    int locked_strategy_count;
    mpf_tree_snapshot_t snapshot;
    int has_snapshot;
    int spot_rules_pass; /* FEAT-07: gating result after applying node SPR/pos */
    /* FEAT-07: when the node's range profile uses $cb, this points at the
       resolved c-bet range (previous street aggressor's active range). */
    const mpf_tree_range_profile_t *cb_range;
    mpf_state_t *state_cache;
    uint64_t state_key;
#if !defined(_WIN32)
    pthread_mutex_t cache_lock;
    struct
    {
        pthread_t owner;
        mpf_state_t *state;
        int lock_wired;
    } cache_slots[MPF_NODE_CACHE_SLOTS];
#endif
    int lock_wired; /* single-threaded / non-Windows fallback wire flag */
} mpf_tree_node_t;

typedef struct mpf_tree_def_t
{
    int version;
    int node_count;
    mpf_tree_node_t *nodes;
    int profile_count;
    mpf_tree_bet_profile_t *profiles;
    int range_profile_count;
    mpf_tree_range_profile_t *range_profiles;
    int root_index;
    char *root_id;
} mpf_tree_def_t;

typedef struct
{
    int line;
    int column;
    char message[128];
} mpf_tree_error_t;

mpf_tree_def_t *mpf_tree_load_json(const char *json, size_t len, mpf_tree_error_t *err);
mpf_tree_def_t *mpf_tree_load_json_file(const char *path, mpf_tree_error_t *err);
int mpf_tree_validate(const mpf_tree_def_t *tree, mpf_tree_error_t *err);
char *mpf_tree_serialize_json(const mpf_tree_def_t *tree, size_t *out_len);
void mpf_tree_free(mpf_tree_def_t *tree);

/* FEAT-12: Opponent Models & Multi-Action Nodelock.
 *
 * Parse a compatible opponent model JSON into a structured form that
 * can be applied to a tree. The model maps node ids to per-action frequency
 * locks (e.g. {"RAISE_50": 0.3, "RAISE_100": 0.2, "CALL": 0.5}).
 *
 * Returns NULL on parse failure (err is populated). The caller owns the
 * returned model and must release it with mpf_tree_opponent_model_free.
 */
mpf_tree_opponent_model_t *mpf_tree_parse_opponent_model(const char *json,
                                                         size_t len,
                                                         mpf_tree_error_t *err);

/* Apply a parsed opponent model to a tree: for each node lock spec, resolve the
 * action labels to the node's actions, validate that the sum of locked
 * frequencies is <= 1.0, and normalize the residual across the remaining
 * un-locked actions. The resulting full-frequency lock vector is stored on the
 * node (reusing the existing locked_strategy machinery from #118), so the
 * solver path is unchanged.
 *
 * Returns 0 on success, -1 on error (err is populated). */
int mpf_tree_apply_opponent_model(mpf_tree_def_t *tree,
                                  const mpf_tree_opponent_model_t *model,
                                  mpf_tree_error_t *err);

/* Convenience: parse a model JSON and apply it in one step. Equivalent to
 * mpf_tree_parse_opponent_model followed by mpf_tree_apply_opponent_model,
 * freeing the intermediate model. Returns 0 on success, -1 on error. */
int pe_cfr_apply_opponent_model(mpf_tree_def_t *tree,
                                const char *model_json,
                                size_t len,
                                mpf_tree_error_t *err);

void mpf_tree_opponent_model_free(mpf_tree_opponent_model_t *model);

/* Normalize a partial per-action lock vector into a complete strategy vector.
 * locked[i] >= 0 marks a pinned frequency; -1.0 marks a free action. The free
 * residual (1 - sum(locked)) is distributed across the free actions in
 * proportion to `base_weights` (or uniformly when base_weights is NULL or all
 * zero). Writes `out` (length action_count). Returns 0 on success, -1 if the
 * locked sum exceeds 1.0 (within epsilon). */
int mpf_tree_normalize_lock(const double *locked,
                            int action_count,
                            const double *base_weights,
                            double *out);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_MPF_TREE_H */
