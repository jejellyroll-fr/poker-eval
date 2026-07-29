#include <poker_eval/engine/solvers/cfr/mpf_export.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static const char *mpf_export_street_label(mpf_street_t street)
{
    switch (street)
    {
    case MPF_STREET_PREFLOP:
        return "PREFLOP";
    case MPF_STREET_FLOP:
        return "FLOP";
    case MPF_STREET_TURN:
        return "TURN";
    case MPF_STREET_RIVER:
        return "RIVER";
    case MPF_STREET_SHOWDOWN:
        return "SHOWDOWN";
    default:
        return "UNKNOWN";
    }
}

static const char *mpf_export_action_label(mpf_tree_action_type_t type)
{
    switch (type)
    {
    case MPF_TREE_ACTION_FOLD:
        return "fold";
    case MPF_TREE_ACTION_CALL:
        return "call";
    case MPF_TREE_ACTION_RAISE:
        return "raise";
    case MPF_TREE_ACTION_CHANCE:
        return "chance";
    case MPF_TREE_ACTION_TERMINAL:
        return "terminal";
    default:
        return "unknown";
    }
}

static const char *mpf_export_node_type_label(mpf_tree_node_type_t type)
{
    switch (type)
    {
    case MPF_TREE_NODE_PLAYER:
        return "player";
    case MPF_TREE_NODE_CHANCE:
        return "chance";
    case MPF_TREE_NODE_TERMINAL:
        return "terminal";
    default:
        return "unknown";
    }
}

static int mpf_node_passes_filter(const mpf_node_summary_t *node,
                                  const mpf_export_options_t *options)
{
    if (!node)
        return 0;
    if (!options)
        return node->visited ? 1 : 0;
    const mpf_export_filter_t *filter = &options->filter;
    if (!filter->include_unvisited && !node->visited)
        return 0;
    if (filter->street_filter_enabled && node->street != filter->street)
        return 0;
    if (filter->acting_player_filter_enabled && node->acting_player != filter->acting_player)
        return 0;
    if (filter->node_id_prefix && filter->node_id_prefix[0] != '\0')
    {
        const char *id = node->node_id ? node->node_id : "";
        size_t prefix_len = strlen(filter->node_id_prefix);
        if (strncmp(id, filter->node_id_prefix, prefix_len) != 0)
            return 0;
    }
    return 1;
}

static int mpf_export_write_json(FILE *f,
                                 const mpf_node_summary_t *nodes,
                                 int count,
                                 const mpf_export_options_t *options)
{
    if (!f || !nodes || count < 0)
        return -1;
    int pretty = options ? options->pretty : 1;
    (void)pretty; /* pretty formatting currently always enabled */

    fprintf(f, "{\n  \"nodes\": [\n");
    int remaining = 0;
    for (int i = 0; i < count; ++i)
        if (mpf_node_passes_filter(&nodes[i], options))
            remaining++;
    int printed = 0;
    for (int i = 0; i < count; ++i)
    {
        const mpf_node_summary_t *node = &nodes[i];
        if (!mpf_node_passes_filter(node, options))
            continue;
        printed++;
        fprintf(f, "    {\n");
        fprintf(f, "      \"id\": \"%s\",\n", node->node_id ? node->node_id : "");
        fprintf(f, "      \"type\": \"%s\",\n", mpf_export_node_type_label(node->type));
        fprintf(f, "      \"street\": \"%s\",\n", mpf_export_street_label(node->street));
        fprintf(f, "      \"acting_player\": %d,\n", node->acting_player);
        fprintf(f, "      \"visited\": %s,\n", node->visited ? "true" : "false");
        fprintf(f, "      \"state_key\": \"0x%llx\"",
                (unsigned long long)node->state_key);
        if (node->range_profile_id)
            fprintf(f, ",\n      \"range_profile\": \"%s\"", node->range_profile_id);
        if (node->visited && node->type == MPF_TREE_NODE_PLAYER)
        {
            fprintf(f, ",\n      \"actions\": [");
            for (int j = 0; j < node->action_count; ++j)
            {
                const mpf_node_action_summary_t *act = &node->actions[j];
                const char *label = mpf_export_action_label(act->type);
                fprintf(f, "%s{\"type\":\"%s\",\"prob\":%.6f",
                        (j == 0) ? "" : ",",
                        label,
                        act->probability);
                if (act->type == MPF_TREE_ACTION_RAISE)
                    fprintf(f, ",\"bet_size\":%.6f", act->bet_size);
                fprintf(f, "}");
            }
            fprintf(f, "]");
            fprintf(f, ",\n      \"ev_mean\": %.6f,\n", node->ev_mean);
            fprintf(f, "      \"ev_stddev\": %.6f,\n", node->ev_stddev);
            fprintf(f, "      \"ev_count\": %llu",
                    (unsigned long long)node->ev_count);
        }
        else if (node->visited && node->type == MPF_TREE_NODE_TERMINAL)
        {
            fprintf(f, ",\n      \"terminal_ev\": %.6f,\n", node->ev_mean);
            fprintf(f, "      \"terminal_stddev\": %.6f,\n", node->ev_stddev);
            fprintf(f, "      \"terminal_count\": %llu",
                    (unsigned long long)node->ev_count);
        }

        if (options && options->include_combos && node->combo_count > 0)
        {
            fprintf(f, ",\n      \"combos\": [");
            for (int c = 0; c < node->combo_count; ++c)
            {
                const mpf_combo_summary_t *combo = &node->combos[c];
                fprintf(f, "%s{\n        \"hand\": \"%s\",\n        \"weight\": %.6f",
                        (c == 0) ? "" : ",",
                        combo->hand ? combo->hand : "",
                        combo->weight);
                if (combo->action_count > 0)
                {
                    fprintf(f, ",\n        \"actions\": [");
                    for (int a = 0; a < combo->action_count; ++a)
                    {
                        const mpf_node_action_summary_t *act = &node->actions[a];
                        const char *label = mpf_export_action_label(act->type);
                        double prob = combo->action_probabilities[a];
                        fprintf(f, "%s{\"type\":\"%s\",\"prob\":%.6f",
                                (a == 0) ? "" : ",",
                                label,
                                prob);
                        if (act->type == MPF_TREE_ACTION_RAISE)
                            fprintf(f, ",\"bet_size\":%.6f", act->bet_size);
                        fprintf(f, "}");
                    }
                    fprintf(f, "]");
                }
                fprintf(f, ",\n        \"ev\": %.6f\n      }", combo->ev);
            }
            fprintf(f, "\n      ]");
        }

        fprintf(f, "\n    }%s\n", (printed < remaining) ? "," : "");
    }
    fprintf(f, "  ]\n}\n");
    return 0;
}

static void mpf_export_write_csv_row(FILE *f,
                                     const mpf_node_summary_t *node,
                                     const char *hand,
                                     double weight,
                                     const char *action_label,
                                     double probability,
                                     double bet_size,
                                     double ev)
{
    fprintf(f, "%s,%s,%s,%d,%d,%s,%s,%.6f,%s,%.6f,%.6f,%.6f\n",
            node->node_id ? node->node_id : "",
            mpf_export_street_label(node->street),
            mpf_export_node_type_label(node->type),
            node->acting_player,
            node->visited ? 1 : 0,
            node->range_profile_id ? node->range_profile_id : "",
            hand ? hand : "",
            weight,
            action_label ? action_label : "",
            probability,
            bet_size,
            ev);
}

static int mpf_export_write_csv(FILE *f,
                                const mpf_node_summary_t *nodes,
                                int count,
                                const mpf_export_options_t *options)
{
    if (!f || !nodes || count < 0)
        return -1;

    fprintf(f, "node_id,street,type,acting_player,visited,range_profile,hand,weight,action,probability,bet_size,ev\n");
    for (int i = 0; i < count; ++i)
    {
        const mpf_node_summary_t *node = &nodes[i];
        if (!mpf_node_passes_filter(node, options))
            continue;
        if (options && options->include_combos && node->combo_count > 0)
        {
            for (int c = 0; c < node->combo_count; ++c)
            {
                const mpf_combo_summary_t *combo = &node->combos[c];
                if (combo->action_count > 0 && node->action_count > 0)
                {
                    for (int a = 0; a < combo->action_count; ++a)
                    {
                        const mpf_node_action_summary_t *act = &node->actions[a];
                        const char *label = mpf_export_action_label(act->type);
                        double prob = combo->action_probabilities[a];
                        double bet = (act->type == MPF_TREE_ACTION_RAISE) ? act->bet_size : 0.0;
                        mpf_export_write_csv_row(f,
                                                 node,
                                                 combo->hand,
                                                 combo->weight,
                                                 label,
                                                 prob,
                                                 bet,
                                                 combo->ev);
                    }
                }
                else
                {
                    mpf_export_write_csv_row(f,
                                             node,
                                             combo->hand,
                                             combo->weight,
                                             "",
                                             0.0,
                                             0.0,
                                             combo->ev);
                }
            }
        }
        else if (node->action_count > 0)
        {
            for (int a = 0; a < node->action_count; ++a)
            {
                const mpf_node_action_summary_t *act = &node->actions[a];
                const char *label = mpf_export_action_label(act->type);
                double bet = (act->type == MPF_TREE_ACTION_RAISE) ? act->bet_size : 0.0;
                mpf_export_write_csv_row(f,
                                         node,
                                         "",
                                         1.0,
                                         label,
                                         act->probability,
                                         bet,
                                         node->ev_mean);
            }
        }
        else
        {
            const char *label = (node->type == MPF_TREE_NODE_TERMINAL)
                                    ? "terminal"
                                    : mpf_export_node_type_label(node->type);
            mpf_export_write_csv_row(f,
                                     node,
                                     "",
                                     1.0,
                                     label,
                                     1.0,
                                     0.0,
                                     node->ev_mean);
        }
    }
    return 0;
}

int mpf_collect_node_results(const mpf_tree_def_t *tree,
                             cfr_storage_t *storage,
                             mpf_node_summary_t **out_summaries,
                             int *out_count)
{
    if (!tree || !out_summaries || !out_count)
        return -1;
    int count = tree->node_count;
    if (count <= 0)
    {
        *out_summaries = NULL;
        *out_count = 0;
        return 0;
    }
    mpf_node_summary_t *arr = (mpf_node_summary_t *)calloc((size_t)count, sizeof(mpf_node_summary_t));
    if (!arr)
        return -1;

    for (int i = 0; i < count; ++i)
    {
        const mpf_tree_node_t *node = &tree->nodes[i];
        mpf_node_summary_t *dst = &arr[i];
        dst->node_id = node->id ? node->id : "";
        dst->type = node->type;
        dst->street = node->street;
        dst->acting_player = node->acting_player;
        dst->state_key = node->state_key;
        dst->visited = 0;
        if (storage && dst->state_key != 0)
        {
            dst->visited = cfr_storage_has_entry(storage, dst->state_key);
        }
        dst->range_profile_id = NULL;
        dst->combos = NULL;
        dst->combo_count = 0;
        if (node->range_profile && node->range_profile->id)
            dst->range_profile_id = node->range_profile->id;
        else if (node->range_profile_id)
            dst->range_profile_id = node->range_profile_id;

        if (!dst->visited)
            continue;

        if (node->type == MPF_TREE_NODE_PLAYER)
        {
            int n_actions = node->action_count;
            dst->action_count = n_actions;
            if (n_actions > 0)
            {
                double strategy[MPF_TREE_ACTION_MAX];
                if (cfr_storage_peek_avg_strategy(storage, node->state_key, n_actions, strategy) != 0)
                {
                    for (int j = 0; j < n_actions && j < MPF_TREE_ACTION_MAX; ++j)
                        strategy[j] = 1.0 / n_actions;
                }
                double denom = 0.0;
                for (int j = 0; j < n_actions; ++j)
                    denom += strategy[j];
                if (denom <= 0.0)
                    denom = (double)n_actions;
                for (int j = 0; j < n_actions && j < MPF_TREE_ACTION_MAX; ++j)
                {
                    mpf_node_action_summary_t *act = &dst->actions[j];
                    act->type = node->actions[j].type;
                    act->probability = (denom > 0.0) ? (strategy[j] / denom) : (1.0 / n_actions);
                    if (node->actions[j].type == MPF_TREE_ACTION_RAISE)
                    {
                        int idx = node->actions[j].size_index;
                        if (idx >= 0 && idx < node->bet_size_count)
                            act->bet_size = node->bet_sizes[idx];
                        else
                            act->bet_size = 0.0;
                    }
                    else
                    {
                        act->bet_size = 0.0;
                    }
                }
            }
            double mean = 0.0;
            double stddev = 0.0;
            uint64_t sample_count = 0;
            if (cfr_storage_get_ev_stats(storage, node->state_key, &mean, &stddev, &sample_count) == 0)
            {
                dst->ev_mean = mean;
                dst->ev_stddev = stddev;
                dst->ev_count = sample_count;
            }
        }
        else if (node->type == MPF_TREE_NODE_TERMINAL)
        {
            double mean = 0.0;
            double stddev = 0.0;
            uint64_t sample_count = 0;
            if (cfr_storage_get_ev_stats(storage, node->state_key, &mean, &stddev, &sample_count) == 0)
            {
                dst->ev_mean = mean;
                dst->ev_stddev = stddev;
                dst->ev_count = sample_count;
            }
        }

        if (dst->visited &&
            node->type == MPF_TREE_NODE_PLAYER &&
            node->range_profile &&
            node->range_profile->combo_count > 0 &&
            dst->action_count > 0)
        {
            int combo_count = node->range_profile->combo_count;
            dst->combos = (mpf_combo_summary_t *)calloc((size_t)combo_count, sizeof(mpf_combo_summary_t));
            if (!dst->combos)
            {
                mpf_free_node_results(arr, count);
                return -1;
            }
            dst->combo_count = combo_count;
            for (int c = 0; c < combo_count; ++c)
            {
                mpf_combo_summary_t *combo = &dst->combos[c];
                const mpf_tree_range_combo_t *src_combo = &node->range_profile->combos[c];
                combo->hand = src_combo->hand ? src_combo->hand : "";
                combo->weight = src_combo->weight;
                combo->action_count = dst->action_count;
                combo->ev = dst->ev_mean;
                for (int j = 0; j < dst->action_count && j < MPF_TREE_ACTION_MAX; ++j)
                    combo->action_probabilities[j] = dst->actions[j].probability;
            }
        }
    }

    *out_summaries = arr;
    *out_count = count;
    return 0;
}

void mpf_free_node_results(mpf_node_summary_t *summaries, int count)
{
    if (!summaries)
        return;
    if (count < 0)
        count = 0;
    for (int i = 0; i < count; ++i)
    {
        free(summaries[i].combos);
        summaries[i].combos = NULL;
        summaries[i].combo_count = 0;
    }
    free(summaries);
}

int mpf_export_node_results(const mpf_tree_def_t *tree,
                            cfr_storage_t *storage,
                            const mpf_export_options_t *options)
{
    if (!tree || !storage || !options || !options->path)
        return -1;
    mpf_node_summary_t *summaries = NULL;
    int count = 0;
    if (mpf_collect_node_results(tree, storage, &summaries, &count) != 0)
        return -1;

    FILE *f = fopen(options->path, "w");
    if (!f)
    {
        mpf_free_node_results(summaries, count);
        return -1;
    }
    int rc = 0;
    if (options->format == MPF_EXPORT_FORMAT_CSV)
        rc = mpf_export_write_csv(f, summaries, count, options);
    else if (options->format == MPF_EXPORT_FORMAT_JSON)
        rc = mpf_export_write_json(f, summaries, count, options);
    else
        rc = -1;
    fclose(f);
    mpf_free_node_results(summaries, count);
    return rc;
}

int mpf_export_node_results_json(const mpf_tree_def_t *tree,
                                 cfr_storage_t *storage,
                                 const char *path)
{
    if (!tree || !storage || !path)
        return -1;
    mpf_export_options_t opts;
    memset(&opts, 0, sizeof(opts));
    opts.format = MPF_EXPORT_FORMAT_JSON;
    opts.path = path;
    opts.include_combos = 1;
    opts.pretty = 1;
    return mpf_export_node_results(tree, storage, &opts);
}
