/* Range-aware multiway tournament decision tree (HRC-style foundation). */
#ifndef POKER_EVAL_ECONOMICS_HRC_H
#define POKER_EVAL_ECONOMICS_HRC_H

#include <stddef.h>
#include <stdint.h>

#include <poker_eval/solver/pe_range.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PE_HRC_MAX_PLAYERS 8
#define PE_HRC_MAX_ACTIONS 8
#define PE_HRC_MAX_NODES 4096
#define PE_HRC_MAX_DEPTH 128

typedef enum {
    PE_HRC_OK = 0,
    PE_HRC_ERR_NULL_ARGUMENT = -1,
    PE_HRC_ERR_INVALID_TREE = -2,
    PE_HRC_ERR_INVALID_RANGE = -3,
    PE_HRC_ERR_PROFILE_LIMIT = -4,
    PE_HRC_ERR_CALLBACK = -5
} pe_hrc_status_t;

typedef struct {
    const char *label;
    double amount;
    int child_index;
} pe_hrc_action_t;

typedef struct {
    int terminal;
    int player_to_act;
    unsigned action_count;
    pe_hrc_action_t actions[PE_HRC_MAX_ACTIONS];
} pe_hrc_node_t;

typedef struct {
    const pe_hrc_node_t *nodes;
    size_t node_count;
    int root_index;
    int num_players;
} pe_hrc_tree_t;

typedef struct {
    uint16_t combo_index[PE_HRC_MAX_PLAYERS];
    double weight;
} pe_hrc_profile_t;

/* The callback is called at every terminal for one collision-free private
 * hand profile. It may use the action path to calculate pot/stack payoffs. */
typedef int (*pe_hrc_terminal_fn)(const pe_hrc_tree_t *tree,
                                  int node_index,
                                  const pe_hrc_profile_t *profile,
                                  const uint16_t *action_path,
                                  size_t action_path_length,
                                  double *out_payoffs,
                                  void *user_data);

typedef struct {
    pe_hrc_tree_t tree;
    pe_range_view_t ranges[PE_HRC_MAX_PLAYERS];
    unsigned iterations;
    size_t max_profiles;
    pe_hrc_terminal_fn terminal_value;
    void *user_data;
} pe_hrc_config_t;

typedef struct {
    double action_probability[PE_HRC_MAX_NODES][PE_HRC_MAX_ACTIONS];
    double ev[PE_HRC_MAX_PLAYERS];
    size_t profile_count;
    unsigned iterations;
} pe_hrc_result_t;

/* Validate the tree and all range views without changing caller-owned data. */
pe_hrc_status_t pe_hrc_validate(const pe_hrc_config_t *config);

/* Enumerate collision-free range profiles and solve the bounded action tree
 * with regret matching. Strategies are aggregate per public node; the private
 * range is used exactly for profile probabilities and card removal. */
pe_hrc_status_t pe_hrc_solve(const pe_hrc_config_t *config,
                             pe_hrc_result_t *result);

#ifdef __cplusplus
}
#endif

#endif
