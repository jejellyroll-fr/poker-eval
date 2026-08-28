#include <poker_eval/economics/hrc.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../solver/domain/finite_double.h"

typedef struct {
    const pe_hrc_config_t *config;
    size_t max_range_combos;
    size_t combo_stride;
    double *strategy;
    double *regrets;
    double *reach;
    double *action_value;
    double *public_reach;
    double (*public_strategy_mass)[PE_HRC_MAX_ACTIONS];
    int collect_public;
    pe_hrc_profile_t *profiles;
    size_t profile_count;
    size_t profile_capacity;
} solve_context_t;

static void solve_context_free(solve_context_t *ctx)
{
    if (!ctx)
        return;
    free(ctx->strategy);
    free(ctx->regrets);
    free(ctx->reach);
    free(ctx->action_value);
    free(ctx->profiles);
    free(ctx->public_reach);
    free(ctx->public_strategy_mass);
}

static size_t info_offset(const solve_context_t *ctx, int player, int node,
                          size_t combo, unsigned action)
{
    return (size_t)player * ctx->combo_stride +
           (size_t)node * ctx->max_range_combos * PE_HRC_MAX_ACTIONS +
           combo * PE_HRC_MAX_ACTIONS + action;
}

static double *info_strategy(solve_context_t *ctx, int player, int node,
                             size_t combo)
{
    return &ctx->strategy[info_offset(ctx, player, node, combo, 0)];
}

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
            if (!(w >= 0.0) || !pe_finite_double(w))
                return PE_HRC_ERR_INVALID_RANGE;
        }
        if (config->ranges[p].count > UINT16_MAX)
            return PE_HRC_ERR_INVALID_RANGE;
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
            double *strategy = info_strategy(ctx, node->player_to_act,
                                              node_index,
                                              profile->combo_index[node->player_to_act]);
            path[depth] = (uint16_t)a;
            if (!evaluate_node(ctx, node->actions[a].child_index, profile, path,
                               depth + 1, path_reach * strategy[a],
                               child_value))
                return 0;
            {
                double reach = path_reach * profile->weight;
                ctx->action_value[info_offset(ctx, node->player_to_act, node_index,
                                              profile->combo_index[node->player_to_act], a)] += reach *
                    child_value[node->player_to_act];
                if (ctx->collect_public) {
                    ctx->public_strategy_mass[node_index][a] += reach * strategy[a];
                }
            }
            for (int p = 0; p < ctx->config->tree.num_players; ++p)
                out[p] += info_strategy(ctx, node->player_to_act, node_index,
                                        profile->combo_index[node->player_to_act])[a] *
                          child_value[p];
        }
        ctx->reach[info_offset(ctx, node->player_to_act, node_index,
                               profile->combo_index[node->player_to_act], 0)] +=
            profile->weight * path_reach;
        if (ctx->collect_public)
            ctx->public_reach[node_index] += profile->weight * path_reach;
        return 1;
    }
}

static void init_strategy(solve_context_t *ctx)
{
    for (size_t n = 0; n < ctx->config->tree.node_count; ++n) {
        const pe_hrc_node_t *node = &ctx->config->tree.nodes[n];
        if (node->terminal)
            continue;
        int player = node->player_to_act;
        size_t count = ctx->config->ranges[player].count;
        for (size_t combo = 0; combo < count; ++combo) {
            double *strategy = info_strategy(ctx, player, (int)n, combo);
            for (unsigned a = 0; a < node->action_count; ++a)
                strategy[a] = 1.0 / (double)node->action_count;
        }
    }
}

static void update_strategy(solve_context_t *ctx)
{
    for (size_t n = 0; n < ctx->config->tree.node_count; ++n) {
        const pe_hrc_node_t *node = &ctx->config->tree.nodes[n];
        int player;
        if (node->terminal)
            continue;
        player = node->player_to_act;
        for (size_t combo = 0; combo < ctx->config->ranges[player].count; ++combo) {
            size_t base = info_offset(ctx, player, (int)n, combo, 0);
            double positive_sum = 0.0;
            if (ctx->reach[base] <= 0.0)
                continue;
            for (unsigned a = 0; a < node->action_count; ++a) {
                double action = ctx->action_value[base + a] / ctx->reach[base];
                double baseline = 0.0;
                for (unsigned b = 0; b < node->action_count; ++b)
                    baseline += ctx->strategy[base + b] *
                        (ctx->action_value[base + b] / ctx->reach[base]);
                ctx->regrets[base + a] += action - baseline;
                if (ctx->regrets[base + a] > 0.0)
                    positive_sum += ctx->regrets[base + a];
            }
            if (positive_sum > 0.0) {
                for (unsigned a = 0; a < node->action_count; ++a)
                    ctx->strategy[base + a] = fmax(ctx->regrets[base + a], 0.0) / positive_sum;
            } else {
                for (unsigned a = 0; a < node->action_count; ++a)
                    ctx->strategy[base + a] = 1.0 / (double)node->action_count;
            }
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
    size_t total_slots;
    size_t max_range_combos = 0;
    if (!result)
        return PE_HRC_ERR_NULL_ARGUMENT;
    memset(result, 0, sizeof(*result));
    validation = pe_hrc_validate(config);
    if (validation != PE_HRC_OK)
        return validation;
    memset(&ctx, 0, sizeof(ctx));
    ctx.config = config;
    for (int p = 0; p < config->tree.num_players; ++p)
        if (config->ranges[p].count > max_range_combos)
            max_range_combos = config->ranges[p].count;
    ctx.max_range_combos = max_range_combos;
    if (max_range_combos > SIZE_MAX / PE_HRC_MAX_ACTIONS ||
        config->tree.node_count > SIZE_MAX /
            (max_range_combos * PE_HRC_MAX_ACTIONS))
        return PE_HRC_ERR_PROFILE_LIMIT;
    ctx.combo_stride = config->tree.node_count * max_range_combos * PE_HRC_MAX_ACTIONS;
    if (ctx.combo_stride == 0 ||
        ctx.combo_stride > SIZE_MAX / (size_t)config->tree.num_players)
        return PE_HRC_ERR_PROFILE_LIMIT;
    total_slots = (size_t)config->tree.num_players * ctx.combo_stride;
    if (total_slots > SIZE_MAX / sizeof(double) ||
        config->max_profiles > SIZE_MAX / sizeof(*ctx.profiles))
        return PE_HRC_ERR_PROFILE_LIMIT;
    ctx.strategy = calloc(total_slots, sizeof(*ctx.strategy));
    ctx.regrets = calloc(total_slots, sizeof(*ctx.regrets));
    ctx.reach = calloc(total_slots, sizeof(*ctx.reach));
    ctx.action_value = calloc(total_slots, sizeof(*ctx.action_value));
    ctx.profile_capacity = config->max_profiles;
    ctx.profiles = calloc(ctx.profile_capacity, sizeof(*ctx.profiles));
    ctx.public_reach = calloc(config->tree.node_count,
                              sizeof(*ctx.public_reach));
    ctx.public_strategy_mass = calloc(config->tree.node_count,
                                      sizeof(*ctx.public_strategy_mass));
    if (!ctx.strategy || !ctx.regrets || !ctx.reach || !ctx.action_value ||
        !ctx.profiles || !ctx.public_reach || !ctx.public_strategy_mass) {
        solve_context_free(&ctx);
        return PE_HRC_ERR_PROFILE_LIMIT;
    }
    memset(&current, 0, sizeof(current));
    {
        StdDeck_CardMask empty;
        StdDeck_CardMask_RESET(empty);
        enumerate_profiles(&ctx, 0, empty, 1.0, &current);
    }
    if (ctx.profile_count == ctx.profile_capacity) {
        solve_context_free(&ctx);
        return PE_HRC_ERR_PROFILE_LIMIT;
    }
    for (size_t i = 0; i < ctx.profile_count; ++i)
        total_weight += ctx.profiles[i].weight;
    if (!(total_weight > 0.0) || !pe_finite_double(total_weight)) {
        solve_context_free(&ctx);
        return PE_HRC_ERR_INVALID_RANGE;
    }
    for (size_t i = 0; i < ctx.profile_count; ++i)
        ctx.profiles[i].weight /= total_weight;
    init_strategy(&ctx);
    {
        unsigned iterations = config->iterations ? config->iterations : 1u;
        for (unsigned it = 0; it < iterations; ++it) {
            memset(ctx.reach, 0, total_slots * sizeof(*ctx.reach));
            memset(ctx.action_value, 0, total_slots * sizeof(*ctx.action_value));
            for (size_t i = 0; i < ctx.profile_count; ++i) {
                double values[PE_HRC_MAX_PLAYERS];
                uint16_t path[PE_HRC_MAX_DEPTH];
                memset(values, 0, sizeof(values));
                if (!evaluate_node(&ctx, config->tree.root_index, &ctx.profiles[i],
                                   path, 0, 1.0, values)) {
                    solve_context_free(&ctx);
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
    memset(ctx.reach, 0, total_slots * sizeof(*ctx.reach));
    memset(ctx.public_reach, 0,
           config->tree.node_count * sizeof(*ctx.public_reach));
    memset(ctx.public_strategy_mass, 0,
           config->tree.node_count * sizeof(*ctx.public_strategy_mass));
    ctx.collect_public = 1;
    for (size_t i = 0; i < ctx.profile_count; ++i) {
        double values[PE_HRC_MAX_PLAYERS];
        uint16_t path[PE_HRC_MAX_DEPTH];
        memset(values, 0, sizeof(values));
        if (!evaluate_node(&ctx, config->tree.root_index, &ctx.profiles[i], path,
                           0, 1.0, values)) {
            solve_context_free(&ctx);
            return PE_HRC_ERR_CALLBACK;
        }
        for (int p = 0; p < config->tree.num_players; ++p)
            result->ev[p] += ctx.profiles[i].weight * values[p];
    }
    memset(result->action_probability, 0, sizeof(result->action_probability));
    for (size_t n = 0; n < config->tree.node_count; ++n)
        if (ctx.public_reach[n] > 0.0)
            for (unsigned a = 0; a < config->tree.nodes[n].action_count; ++a)
                result->action_probability[n][a] =
                    ctx.public_strategy_mass[n][a] / ctx.public_reach[n];
    result->combo_action_probability = ctx.strategy;
    result->combo_stride = ctx.combo_stride;
    result->max_range_combos = ctx.max_range_combos;
    result->node_count = config->tree.node_count;
    result->num_players = config->tree.num_players;
    for (int p = 0; p < config->tree.num_players; ++p)
        result->range_combo_count[p] = config->ranges[p].count;
    result->profile_count = ctx.profile_count;
    result->iterations = config->iterations ? config->iterations : 1u;
    free(ctx.regrets);
    free(ctx.reach);
    free(ctx.action_value);
    free(ctx.profiles);
    free(ctx.public_reach);
    free(ctx.public_strategy_mass);
    return PE_HRC_OK;
}

double pe_hrc_result_combo_probability(const pe_hrc_result_t *result,
                                       int player, int node_index,
                                       size_t combo_index, int action)
{
    size_t offset;
    if (!result || !result->combo_action_probability || player < 0 ||
        player >= result->num_players || node_index < 0 ||
        (size_t)node_index >= result->node_count || combo_index >= result->range_combo_count[player] ||
        action < 0 || action >= PE_HRC_MAX_ACTIONS)
        return -1.0;
    offset = (size_t)player * result->combo_stride +
             (size_t)node_index * result->max_range_combos * PE_HRC_MAX_ACTIONS +
             combo_index * PE_HRC_MAX_ACTIONS + (size_t)action;
    return result->combo_action_probability[offset];
}

void pe_hrc_result_free(pe_hrc_result_t *result)
{
    if (!result)
        return;
    free(result->combo_action_probability);
    result->combo_action_probability = NULL;
    result->combo_stride = 0;
}
