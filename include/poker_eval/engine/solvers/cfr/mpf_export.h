#ifndef POKER_EVAL_MPF_EXPORT_H
#define POKER_EVAL_MPF_EXPORT_H

#include <stddef.h>
#include <stdint.h>

#include <poker_eval/engine/solvers/cfr/mpf_tree.h>
#include <poker_eval/engine/solvers/cfr/cfr_core.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    mpf_tree_action_type_t type;
    double probability;
    double bet_size;
} mpf_node_action_summary_t;

typedef struct
{
    const char *hand;
    double weight;
    int action_count;
    double action_probabilities[MPF_TREE_ACTION_MAX];
    double ev; /* expected value associated to the combo */
} mpf_combo_summary_t;

typedef struct
{
    const char *node_id;
    mpf_tree_node_type_t type;
    mpf_street_t street;
    int acting_player;
    int visited;
    uint64_t state_key;
    int action_count;
    mpf_node_action_summary_t actions[MPF_TREE_ACTION_MAX];
    double ev_mean;
    double ev_stddev;
    uint64_t ev_count;
    const char *range_profile_id;
    mpf_combo_summary_t *combos;
    int combo_count;
} mpf_node_summary_t;

typedef struct
{
    int include_unvisited;
    int street_filter_enabled;
    mpf_street_t street;
    int acting_player_filter_enabled;
    int acting_player;
    const char *node_id_prefix;
} mpf_export_filter_t;

typedef enum
{
    MPF_EXPORT_FORMAT_JSON = 0,
    MPF_EXPORT_FORMAT_CSV = 1
} mpf_export_format_t;

typedef struct
{
    mpf_export_format_t format;
    const char *path;
    mpf_export_filter_t filter;
    int include_combos;
    int pretty; /* only relevant for JSON */
} mpf_export_options_t;

int mpf_collect_node_results(const mpf_tree_def_t *tree,
                             cfr_storage_t *storage,
                             mpf_node_summary_t **out_summaries,
                             int *out_count);

void mpf_free_node_results(mpf_node_summary_t *summaries, int count);

int mpf_export_node_results(const mpf_tree_def_t *tree,
                            cfr_storage_t *storage,
                            const mpf_export_options_t *options);

int mpf_export_node_results_json(const mpf_tree_def_t *tree,
                                 cfr_storage_t *storage,
                                 const char *path);

#ifdef __cplusplus
#define MPF_EXPORT_OPTIONS_INIT_JSON(path_) \
    { MPF_EXPORT_FORMAT_JSON, (path_), {0, 0, MPF_STREET_PREFLOP, 0, 0, NULL}, 1, 1 }
#endif

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_MPF_EXPORT_H */
