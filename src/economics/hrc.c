#include <poker_eval/economics/hrc.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const pe_hrc_config_t *config;
    double strategy[PE_HRC_MAX_NODES][PE_HRC_MAX_ACTIONS];
    double regrets[PE_HRC_MAX_NODES][PE_HRC_MAX_ACTIONS];
    double reach[PE_HRC_MAX_NODES];
    double action_value[PE_HRC_MAX_NODES][PE_HRC_MAX_ACTIONS];
    int active[PE_HRC_MAX_NODES];
    pe_hrc_profile_t *profiles;
    size_t profile_count;
    size_t profile_capacity;
} solve_context_t;

static int validate_node(const pe_hrc_tree_t *tree, int index, int *color)
{
    const pe_hrc_node_t *node;
    if (index < 0 || (size_t)index >= tree->node_count)
        return 0;
    if (color[index] == 1)
        return 0; /* A betting tree must be acyclic. */
    if (color[index] == 2)
        return 1;
    color[index] = 1;
    node = &tree->nodes[index];
    if (node->terminal) {
        if (node->action_count != 0)
            return 0;
    } else {
        if (node->player_to_act < 0 || node->player_to_act >= tree->num_players ||
            node->action_count == 0 || node->action_count > PE_HRC_MAX_ACTIONS)
            return 0;
        for (unsigned i = 0; i < node->action_count; ++i) {
            if (!validate_node(tree, node->actions[i].child_index, color))
                return 0;
        }
    }
    color[index] = 2;
    return 1;
}

pe_hrc_status_t pe_hrc_validate(const pe_hrc_config_t *config)
{
    int color[PE_HRC_MAX_NODES];
    if (!config || !config->tree.nodes || !config->terminal_value ||
        config->tree.node_count == 0 || config->tree.node_count > PE_HRC_MAX_NODES ||
        config->tree.num_players <= 0 || config->tree.num_players > PE_HRC_MAX_PLAYERS ||
        config->tree.root_index < 0 || (size_t)config->tree.root_index >= config->tree.node_count ||
        config->max_profiles == 0)
        return PE_HRC_ERR_NULL_ARGUMENT;
    memset(color, 0, sizeof(color));
    if (!validate_node(&config->tree, config->tree.root_index, color))
        return PE_HRC_ERR_INVALID_TREE;
    for (int p = 0; p < config->tree.num_players; ++p) {
        if (!config->ranges[p].combos || config->ranges[p].count == 0)
            return PE_HRC_ERR_INVALID_RANGE;
        for (size_t i = 0; i < config->ranges[p].count; ++i) {
            double w = config->ranges[p].combos[i].weight;
            if (!(w >= 0.0) || !isfinite(w))
                return PE_HRC_ERR_INVALID_RANGE;
        }
    }
    return PE_HRC_OK;
}

static void enumerate_profiles(solve_context_t *ctx, int player,
                               StdDeck_CardMask used, double weight,
                               pe_hrc_profile_t *current)
{
    const pe_range_view_t *range;
    if (ctx->profile_count >= ctx->profile_capacity)
        return;
    if (player == ctx->config->tree.num_players) {
        current->weight = weight;
        ctx->profiles[ctx->profile_count++] = *current;
        return;
    }
    range = &ctx->config->ranges[player];
    for (size_t i = 0; i < range->count; ++i) {
        const pe_combo_t *combo = &range->combos[i];
        if (combo->weight <= 0.0 || StdDeck_CardMask_ANY_SET(used, combo->hand))
            continue;
        current->combo_index[player] = (uint16_t)i;
        {
            StdDeck_CardMask next;
            StdDeck_CardMask_OR(next, used, combo->hand);
            enumerate_profiles(ctx, player + 1, next, weight * combo->weight, current);
        }
        if (ctx->profile_count >= ctx->profile_capacity)
            return;
    }
}

static int evaluate_node(solve_context_t *ctx, int node_index,
                         const pe_hrc_profile_t *profile,
                         uint16_t *path, size_t depth, double path_reach,
                         double *out)
{
    const pe_hrc_node_t *node = &ctx->config->tree.nodes[node_index];
    if (depth >= PE_HRC_MAX_DEPTH)
        return 0;
    if (node->terminal)
        return ctx->config->terminal_value(&ctx->config->tree, node_index, profile,
                                           path, depth, out, ctx->config->user_data) == 0;
    {
        double child_value[PE_HRC_MAX_PLAYERS];
        memset(out, 0, sizeof(double) * PE_HRC_MAX_PLAYERS);
        for (unsigned a = 0; a < node->action_count; ++a) {
            path[depth] = (uint16_t)a;
            if (!evaluate_node(ctx, node->actions[a].child_index, profile, path,
                               depth + 1, path_reach * ctx->strategy[node_index][a],
                               child_value))
                return 0;
            if (ctx->active[node_index]) {
                double reach = path_reach * profile->weight;
                ctx->action_value[node_index][a] += reach *
                    child_value[node->player_to_act];
            }
            for (int p = 0; p < ctx->config->tree.num_players; ++p)
                out[p] += ctx->strategy[node_index][a] * child_value[p];
        }
        if (ctx->active[node_index])
            ctx->reach[node_index] += profile->weight * path_reach;
        return 1;
    }
}

static void init_strategy(solve_context_t *ctx)
{
    for (size_t n = 0; n < ctx->config->tree.node_count; ++n) {
        const pe_hrc_node_t *node = &ctx->config->tree.nodes[n];
        if (node->terminal)
            continue;
        for (unsigned a = 0; a < node->action_count; ++a)
            ctx->strategy[n][a] = 1.0 / (double)node->action_count;
        ctx->active[n] = 1;
    }
}

static void update_strategy(solve_context_t *ctx)
{
    for (size_t n = 0; n < ctx->config->tree.node_count; ++n) {
        const pe_hrc_node_t *node = &ctx->config->tree.nodes[n];
        double positive_sum = 0.0;
        if (node->terminal || ctx->reach[n] <= 0.0)
            continue;
        for (unsigned a = 0; a < node->action_count; ++a) {
            double action = ctx->action_value[n][a] / ctx->reach[n];
            double baseline = 0.0;
            for (unsigned b = 0; b < node->action_count; ++b)
                baseline += ctx->strategy[n][b] *
                    (ctx->action_value[n][b] / ctx->reach[n]);
            ctx->regrets[n][a] += action - baseline;
            if (ctx->regrets[n][a] > 0.0)
                positive_sum += ctx->regrets[n][a];
        }
        if (positive_sum > 0.0) {
            for (unsigned a = 0; a < node->action_count; ++a)
                ctx->strategy[n][a] = fmax(ctx->regrets[n][a], 0.0) / positive_sum;
        } else {
            for (unsigned a = 0; a < node->action_count; ++a)
                ctx->strategy[n][a] = 1.0 / (double)node->action_count;
        }
    }
}

pe_hrc_status_t pe_hrc_solve(const pe_hrc_config_t *config,
                             pe_hrc_result_t *result)
{
    solve_context_t ctx;
    pe_hrc_profile_t current;
    double total_weight = 0.0;
    pe_hrc_status_t validation;
    if (!result)
        return PE_HRC_ERR_NULL_ARGUMENT;
    memset(result, 0, sizeof(*result));
    validation = pe_hrc_validate(config);
    if (validation != PE_HRC_OK)
        return validation;
    memset(&ctx, 0, sizeof(ctx));
    ctx.config = config;
    ctx.profile_capacity = config->max_profiles;
    ctx.profiles = calloc(ctx.profile_capacity, sizeof(*ctx.profiles));
    if (!ctx.profiles)
        return PE_HRC_ERR_PROFILE_LIMIT;
    memset(&current, 0, sizeof(current));
    {
        StdDeck_CardMask empty;
        StdDeck_CardMask_RESET(empty);
        enumerate_profiles(&ctx, 0, empty, 1.0, &current);
    }
    if (ctx.profile_count == ctx.profile_capacity) {
        free(ctx.profiles);
        return PE_HRC_ERR_PROFILE_LIMIT;
    }
    for (size_t i = 0; i < ctx.profile_count; ++i)
        total_weight += ctx.profiles[i].weight;
    if (!(total_weight > 0.0) || !isfinite(total_weight)) {
        free(ctx.profiles);
        return PE_HRC_ERR_INVALID_RANGE;
    }
    for (size_t i = 0; i < ctx.profile_count; ++i)
        ctx.profiles[i].weight /= total_weight;
    init_strategy(&ctx);
    {
        unsigned iterations = config->iterations ? config->iterations : 1u;
        for (unsigned it = 0; it < iterations; ++it) {
            memset(ctx.reach, 0, sizeof(ctx.reach));
            memset(ctx.action_value, 0, sizeof(ctx.action_value));
            for (size_t i = 0; i < ctx.profile_count; ++i) {
                double values[PE_HRC_MAX_PLAYERS];
                uint16_t path[PE_HRC_MAX_DEPTH];
                memset(values, 0, sizeof(values));
                if (!evaluate_node(&ctx, config->tree.root_index, &ctx.profiles[i],
                                   path, 0, 1.0, values)) {
                    free(ctx.profiles);
                    return PE_HRC_ERR_CALLBACK;
                }
                for (int p = 0; p < config->tree.num_players; ++p)
                    result->ev[p] += ctx.profiles[i].weight * values[p];
            }
            update_strategy(&ctx);
        }
    }
    /* Re-evaluate EV under the final strategy, instead of returning the sum of
     * every training iteration. */
    memset(result->ev, 0, sizeof(result->ev));
    memset(ctx.reach, 0, sizeof(ctx.reach));
    ctx.active[0] = 0;
    for (size_t i = 0; i < ctx.profile_count; ++i) {
        double values[PE_HRC_MAX_PLAYERS];
        uint16_t path[PE_HRC_MAX_DEPTH];
        memset(values, 0, sizeof(values));
        if (!evaluate_node(&ctx, config->tree.root_index, &ctx.profiles[i], path,
                           0, 1.0, values)) {
            free(ctx.profiles);
            return PE_HRC_ERR_CALLBACK;
        }
        for (int p = 0; p < config->tree.num_players; ++p)
            result->ev[p] += ctx.profiles[i].weight * values[p];
    }
    memcpy(result->action_probability, ctx.strategy, sizeof(result->action_probability));
    result->profile_count = ctx.profile_count;
    result->iterations = config->iterations ? config->iterations : 1u;
    free(ctx.profiles);
    return PE_HRC_OK;
}
