#include <poker_eval/solver/pe_external_best_response.h>
#include <poker_eval/core/time_compat.h>

#include "finite_double.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

typedef struct {
    const pe_external_game_t *game;
    pe_rng_t rng;
    uint8_t br_player;
    uint16_t max_depth;
} br_context_t;

static int sample_action(br_context_t *ctx, const void *state, uint16_t actions)
{
    double probabilities[PE_EXTERNAL_MAX_ACTIONS];
    double sum = 0.0;
    double target;
    if (actions == 0u || actions > PE_EXTERNAL_MAX_ACTIONS) return -1;
    for (uint16_t a = 0u; a < actions; ++a)
    {
        double p = ctx->game->action_probability
            ? ctx->game->action_probability(state,
                ctx->game->infoset_key ? ctx->game->infoset_key(state, ctx->game->user) : 0u,
                a, ctx->game->user) : 1.0 / (double)actions;
        if (!pe_finite_double(p) || p < 0.0) return -1;
        probabilities[a] = p;
        sum += p;
    }
    if (!(sum > 0.0) || !pe_finite_double(sum)) return -1;
    target = pe_rng_uniform01(&ctx->rng) * sum;
    sum = 0.0;
    for (uint16_t a = 0u; a < actions; ++a)
    {
        sum += probabilities[a];
        if (target <= sum || a + 1u == actions) return (int)a;
    }
    return -1;
}

static const void *sample_chance(br_context_t *ctx, const void *state)
{
    pe_chance_sample_t sample;
    memset(&sample, 0, sizeof(sample));
    if (ctx->game->sample_chance_child && ctx->game->acting_player(state, ctx->game->user) < 0)
        return ctx->game->sample_chance_child(state, &ctx->rng, &sample, ctx->game->user);
    if ((ctx->game->sample_chance || ctx->game->sample_chance_with_user) &&
        ctx->game->apply_chance)
    {
        int result = ctx->game->sample_chance_with_user
            ? ctx->game->sample_chance_with_user(state, &ctx->rng, &sample, ctx->game->user)
            : ctx->game->sample_chance(state, &ctx->rng, &sample);
        if (result == 0 && sample.outcome >= 0)
            return ctx->game->apply_chance(state, sample.outcome, ctx->game->user);
    }
    return NULL;
}

static double policy_rollout(br_context_t *ctx, const void *state, uint16_t depth)
{
    const void *child;
    double value;
    if (!state || depth >= ctx->max_depth) return NAN;
    if (ctx->game->is_terminal(state, ctx->game->user))
        return ctx->game->terminal_value(state, ctx->br_player, ctx->game->user);
    if (ctx->game->acting_player(state, ctx->game->user) < 0)
    {
        child = sample_chance(ctx, state);
        value = policy_rollout(ctx, child, (uint16_t)(depth + 1u));
        if (child && ctx->game->release_state)
            ctx->game->release_state(child, ctx->game->user);
        return value;
    }
    uint16_t actions = ctx->game->action_count(state, ctx->game->user);
    int action = sample_action(ctx, state, actions);
    if (action < 0) return NAN;
    child = ctx->game->apply_action(state, (uint16_t)action, ctx->game->user);
    value = policy_rollout(ctx, child, (uint16_t)(depth + 1u));
    if (child && ctx->game->release_state)
        ctx->game->release_state(child, ctx->game->user);
    return value;
}

static double br_rollout(br_context_t *ctx, const void *state, uint16_t depth);

static double br_action_value(br_context_t *ctx, const void *state, uint16_t action,
                              uint16_t depth)
{
    const void *child = ctx->game->apply_action(state, action, ctx->game->user);
    double value = policy_rollout(ctx, child, (uint16_t)(depth + 1u));
    if (child && ctx->game->release_state)
        ctx->game->release_state(child, ctx->game->user);
    return value;
}

static double br_rollout(br_context_t *ctx, const void *state, uint16_t depth)
{
    const void *child;
    double value;
    if (!state || depth >= ctx->max_depth) return NAN;
    if (ctx->game->is_terminal(state, ctx->game->user))
        return ctx->game->terminal_value(state, ctx->br_player, ctx->game->user);
    if (ctx->game->acting_player(state, ctx->game->user) < 0)
    {
        child = sample_chance(ctx, state);
        value = br_rollout(ctx, child, (uint16_t)(depth + 1u));
        if (child && ctx->game->release_state)
            ctx->game->release_state(child, ctx->game->user);
        return value;
    }
    uint16_t actions = ctx->game->action_count(state, ctx->game->user);
    int actor = ctx->game->acting_player(state, ctx->game->user);
    if (actions == 0u || actions > PE_EXTERNAL_MAX_ACTIONS) return NAN;
    if (actor == (int)ctx->br_player)
    {
        double best = -INFINITY;
        for (uint16_t a = 0u; a < actions; ++a)
        {
            double value = br_action_value(ctx, state, a, depth);
            if (!pe_finite_double(value)) return NAN;
            if (value > best) best = value;
        }
        return best;
    }
    int action = sample_action(ctx, state, actions);
    if (action < 0) return NAN;
    child = ctx->game->apply_action(state, (uint16_t)action, ctx->game->user);
    value = br_rollout(ctx, child, (uint16_t)(depth + 1u));
    if (child && ctx->game->release_state)
        ctx->game->release_state(child, ctx->game->user);
    return value;
}

pe_external_br_config_t pe_external_br_config_default(void)
{
    pe_external_br_config_t config = {
        256u, 128u, 1u, PE_BR_AUTO, 0u, 0u
    };
    return config;
}

/* ------------------------------------------------------------------ *
 * Issue #233: exact best response
 * ------------------------------------------------------------------ */

/* AUTO budgets when the caller configures none: conservative on purpose, so
   an accidental AUTO on a huge game degrades to sampling instead of hanging
   an exhaustive traversal in front of a desktop user. */
#define PE_BR_AUTO_MAX_NODES_DEFAULT 1000000ull
#define PE_BR_AUTO_TIME_MS_DEFAULT 5000ull

/* Wall-time checks are a syscall; do them once per this many states. */
#define PE_BR_TIME_CHECK_INTERVAL 64u

typedef struct {
    const pe_external_game_t *game;
    uint8_t br_player;
    uint16_t max_depth;
    uint64_t max_nodes;
    uint64_t max_time_ms;
    uint64_t nodes;
    long long deadline_ms; /* absolute monotonic deadline, ms; <0 = none */
    int budget_exceeded;
    int chance_not_enumerable;
    int next_time_check;
} exact_context_t;

static long long now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return -1;
    return (long long)ts.tv_sec * 1000ll + ts.tv_nsec / 1000000ll;
}

static int exact_check_time(exact_context_t *ctx, int force)
{
    if (ctx->deadline_ms < 0) return 0;
    if (!force && ++ctx->next_time_check < (int)PE_BR_TIME_CHECK_INTERVAL)
        return 0;
    ctx->next_time_check = 0;
    if (now_ms() > ctx->deadline_ms)
    {
        ctx->budget_exceeded = 1;
        return 1;
    }
    return 0;
}

/* Returns non-zero when the traversal must abort with PE_BR_ERR_BUDGET. */
static int exact_charge(exact_context_t *ctx)
{
    if (ctx->budget_exceeded) return 1;
    ++ctx->nodes;
    if (ctx->max_nodes && ctx->nodes > ctx->max_nodes)
    {
        ctx->budget_exceeded = 1;
        return 1;
    }
    if (exact_check_time(ctx, 0)) return 1;
    return 0;
}

typedef struct {
    const void *state;
    uint16_t depth;
    int actor;
    uint16_t actions;
    uint32_t child_count;
    uint32_t *children;
    const void **child_states;
    uint64_t infoset_key;
    int terminal;
} exact_node_t;

typedef struct {
    exact_node_t *nodes;
    size_t count;
    size_t capacity;
    uint16_t max_depth;
    struct exact_infoset_depth *infoset_depths;
    size_t infoset_depth_capacity;
    size_t infoset_depth_count;
    int infoset_depth_error;
} exact_tree_t;

typedef struct exact_infoset_depth {
    uint64_t key;
    uint16_t min_depth;
    uint16_t max_depth;
    unsigned char used;
} exact_infoset_depth_t;

static size_t exact_infoset_hash(uint64_t key)
{
    key ^= key >> 30;
    key *= UINT64_C(0xbf58476d1ce4e5b9);
    key ^= key >> 27;
    key *= UINT64_C(0x94d049bb133111eb);
    return (size_t)(key ^ (key >> 31));
}

static int exact_infoset_depth_reserve(exact_tree_t *tree, size_t needed)
{
    size_t old_capacity = tree->infoset_depth_capacity;
    size_t capacity = old_capacity ? old_capacity : 256u;
    exact_infoset_depth_t *entries;
    size_t i;

    while (needed * 10u >= capacity * 7u)
    {
        if (capacity > SIZE_MAX / 2u) return -1;
        capacity *= 2u;
    }
    if (capacity == old_capacity) return 0;
    entries = (exact_infoset_depth_t *)calloc(capacity, sizeof(*entries));
    if (!entries) return -1;
    for (i = 0u; i < old_capacity; ++i)
        if (tree->infoset_depths[i].used)
        {
            size_t slot = exact_infoset_hash(
                tree->infoset_depths[i].key) & (capacity - 1u);
            while (entries[slot].used)
                slot = (slot + 1u) & (capacity - 1u);
            entries[slot] = tree->infoset_depths[i];
        }
    free(tree->infoset_depths);
    tree->infoset_depths = entries;
    tree->infoset_depth_capacity = capacity;
    return 0;
}

static int exact_record_infoset_depth(exact_tree_t *tree, uint64_t key,
                                      uint16_t depth)
{
    size_t slot;
    if (exact_infoset_depth_reserve(tree, tree->infoset_depth_count + 1u) != 0)
        return -1;
    slot = exact_infoset_hash(key) & (tree->infoset_depth_capacity - 1u);
    while (tree->infoset_depths[slot].used)
    {
        exact_infoset_depth_t *entry = &tree->infoset_depths[slot];
        if (entry->key == key)
        {
            if (entry->min_depth != depth || entry->max_depth != depth)
                tree->infoset_depth_error = 1;
            if (depth < entry->min_depth) entry->min_depth = depth;
            if (depth > entry->max_depth) entry->max_depth = depth;
            return 0;
        }
        slot = (slot + 1u) & (tree->infoset_depth_capacity - 1u);
    }
    tree->infoset_depths[slot].used = 1u;
    tree->infoset_depths[slot].key = key;
    tree->infoset_depths[slot].min_depth = depth;
    tree->infoset_depths[slot].max_depth = depth;
    ++tree->infoset_depth_count;
    return 0;
}

/* Behavioral probability of one action at an opponent (or policy) state. */
static int exact_action_probability(exact_context_t *ctx, const void *state,
                                    uint16_t actions, uint16_t action,
                                    double *out_p)
{
    double p;
    uint64_t key = 0u;
    if (!ctx->game->action_probability)
    {
        *out_p = 1.0 / (double)actions;
        return 0;
    }
    if (ctx->game->infoset_key)
        key = ctx->game->infoset_key(state, ctx->game->user);
    p = ctx->game->action_probability(state, key, action, ctx->game->user);
    if (!pe_finite_double(p) || p < 0.0) return -1;
    *out_p = p;
    return 0;
}

static void exact_tree_destroy(exact_tree_t *tree, const pe_external_game_t *game)
{
    size_t i;
    if (!tree) return;
    for (i = 0u; i < tree->count; ++i)
    {
        exact_node_t *node = &tree->nodes[i];
        uint32_t c;
        if (game->release_state && node->child_states)
            for (c = 0u; c < node->child_count; ++c)
                if (node->child_states[c])
                    game->release_state(node->child_states[c], game->user);
        free(node->children);
        free((void *)node->child_states);
    }
    free(tree->nodes);
    free(tree->infoset_depths);
    memset(tree, 0, sizeof(*tree));
}

static int exact_tree_reserve(exact_tree_t *tree, size_t needed)
{
    size_t capacity;
    exact_node_t *nodes;
    if (needed <= tree->capacity) return 0;
    capacity = tree->capacity ? tree->capacity * 2u : 256u;
    while (capacity < needed)
    {
        if (capacity > SIZE_MAX / 2u) return -1;
        capacity *= 2u;
    }
    nodes = (exact_node_t *)realloc(tree->nodes,
                                    capacity * sizeof(*tree->nodes));
    if (!nodes) return -1;
    memset(nodes + tree->capacity, 0,
           (capacity - tree->capacity) * sizeof(*tree->nodes));
    tree->nodes = nodes;
    tree->capacity = capacity;
    return 0;
}

static int exact_tree_build(exact_context_t *ctx, exact_tree_t *tree,
                            const void *state, uint16_t depth,
                            uint32_t *out_index)
{
    exact_node_t *node;
    uint32_t index;
    uint32_t c;

    if (!state || depth >= ctx->max_depth || exact_charge(ctx)) return -1;
    if (tree->count >= UINT32_MAX ||
        exact_tree_reserve(tree, tree->count + 1u) != 0)
        return -1;
    index = (uint32_t)tree->count++;
    node = &tree->nodes[index];
    node->state = state;
    node->depth = depth;
    if (depth > tree->max_depth) tree->max_depth = depth;
    node->terminal = ctx->game->is_terminal(state, ctx->game->user);
    if (node->terminal)
    {
        *out_index = index;
        return 0;
    }
    node->actor = ctx->game->acting_player(state, ctx->game->user);
    if (node->actor < -1 || node->actor >= (int)ctx->game->player_count)
        return -1;
    if (node->actor < 0)
    {
        uint32_t outcomes;
        uint32_t child_count;
        uint32_t *children;
        const void **child_states;
        if (!ctx->game->chance_outcome_count || !ctx->game->apply_chance)
        {
            ctx->chance_not_enumerable = 1;
            return -1;
        }
        outcomes = ctx->game->chance_outcome_count(state, ctx->game->user);
        if (outcomes == 0u)
            return -1;
        if (ctx->max_nodes &&
            (uint64_t)outcomes > ctx->max_nodes - ctx->nodes)
        {
            ctx->budget_exceeded = 1;
            return -1;
        }
        node->child_count = outcomes;
        child_count = node->child_count;
        node->children = (uint32_t *)calloc(outcomes, sizeof(uint32_t));
        node->child_states = (const void **)calloc(outcomes,
                                                   sizeof(const void *));
        if (!node->children || !node->child_states) return -1;
        children = node->children;
        child_states = node->child_states;
        for (c = 0u; c < child_count; ++c)
        {
            child_states[c] = ctx->game->apply_chance(
                state, (int)c, ctx->game->user);
            if (!child_states[c] || exact_tree_build(
                    ctx, tree, child_states[c], (uint16_t)(depth + 1u),
                    &children[c]) != 0)
                return -1;
        }
    }
    else
    {
        uint32_t *children;
        const void **child_states;
        uint32_t child_count;
        node->actions = ctx->game->action_count(state, ctx->game->user);
        if (node->actions == 0u || node->actions > PE_EXTERNAL_MAX_ACTIONS)
            return -1;
        node->child_count = node->actions;
        child_count = node->child_count;
        if (ctx->max_nodes &&
            (uint64_t)child_count > ctx->max_nodes - ctx->nodes)
        {
            ctx->budget_exceeded = 1;
            return -1;
        }
        node->children = (uint32_t *)calloc(node->child_count,
                                            sizeof(uint32_t));
        node->child_states = (const void **)calloc(node->child_count,
                                                   sizeof(const void *));
        if (!node->children || !node->child_states) return -1;
        children = node->children;
        child_states = node->child_states;
        if (ctx->game->infoset_key)
            node->infoset_key = ctx->game->infoset_key(state,
                                                       ctx->game->user);
        if (ctx->game->infoset_key && node->actor == (int)ctx->br_player &&
            exact_record_infoset_depth(tree, node->infoset_key, depth) != 0)
            return -1;
        for (c = 0u; c < child_count; ++c)
        {
            child_states[c] = ctx->game->apply_action(
                state, (uint16_t)c, ctx->game->user);
            if (!child_states[c] || exact_tree_build(
                    ctx, tree, child_states[c], (uint16_t)(depth + 1u),
                    &children[c]) != 0)
                return -1;
        }
    }
    *out_index = index;
    return 0;
}

static int exact_policy_values(exact_context_t *ctx, const exact_tree_t *tree,
                               double *values)
{
    size_t i;
    for (i = tree->count; i-- > 0u;)
    {
        if (exact_check_time(ctx, 0)) return -1;
        const exact_node_t *node = &tree->nodes[i];
        double total = 0.0;
        uint32_t c;
        if (node->terminal)
            values[i] = ctx->game->terminal_value(node->state,
                                                   ctx->br_player,
                                                   ctx->game->user);
        else if (node->actor < 0)
        {
            for (c = 0u; c < node->child_count; ++c)
            {
                if (exact_check_time(ctx, 0)) return -1;
                total += values[node->children[c]];
            }
            values[i] = total / (double)node->child_count;
        }
        else
        {
            for (c = 0u; c < node->child_count; ++c)
            {
                if (exact_check_time(ctx, 0)) return -1;
                double p;
                if (exact_action_probability(ctx, node->state, node->actions,
                                              (uint16_t)c, &p) != 0)
                    return -1;
                total += p * values[node->children[c]];
            }
            values[i] = total;
        }
    }
    if (exact_check_time(ctx, 1)) return -1;
    return pe_finite_double(values[0]) ? 0 : -1;
}

static int exact_reach_values(exact_context_t *ctx, const exact_tree_t *tree,
                              double *reach)
{
    size_t i;
    reach[0] = 1.0;
    for (i = 0u; i < tree->count; ++i)
    {
        if (exact_check_time(ctx, 0)) return -1;
        const exact_node_t *node = &tree->nodes[i];
        uint32_t c;
        if (node->terminal) continue;
        if (node->actor < 0)
        {
            double p = 1.0 / (double)node->child_count;
            for (c = 0u; c < node->child_count; ++c)
            {
                if (exact_check_time(ctx, 0)) return -1;
                reach[node->children[c]] = reach[i] * p;
            }
        }
        else
        {
            for (c = 0u; c < node->child_count; ++c)
            {
                if (exact_check_time(ctx, 0)) return -1;
                double p = 1.0;
                if (node->actor != (int)ctx->br_player &&
                    exact_action_probability(ctx, node->state, node->actions,
                                              (uint16_t)c, &p) != 0)
                    return -1;
                reach[node->children[c]] = reach[i] * p;
            }
        }
    }
    return 0;
}

/* Solve information sets from the leaves upward.  Maximising independently
   at each history would allow the BR player to see hidden information, so
   all occurrences of an information set vote on one action using their
   counterfactual reach. */
static int exact_best_response_values(exact_context_t *ctx,
                                      const exact_tree_t *tree,
                                      double *values)
{
    double *reach = (double *)calloc(tree->count, sizeof(*reach));
    unsigned char *processed = (unsigned char *)calloc(tree->count, 1u);
    int depth;
    size_t i;
    if (!reach || !processed)
    {
        free(reach);
        free(processed);
        return -1;
    }
    if (exact_reach_values(ctx, tree, reach) != 0)
    {
        free(reach);
        free(processed);
        return -1;
    }
    for (i = 0u; i < tree->count; ++i)
    {
        if (exact_check_time(ctx, 0))
        {
            free(reach);
            free(processed);
            return -1;
        }
        if (tree->nodes[i].terminal)
            values[i] = ctx->game->terminal_value(tree->nodes[i].state,
                                                   ctx->br_player,
                                                   ctx->game->user);
    }

    for (depth = (int)tree->max_depth; depth >= 0; --depth)
    {
        for (i = 0u; i < tree->count; ++i)
        {
            if (exact_check_time(ctx, 0))
            {
                free(reach);
                free(processed);
                return -1;
            }
            const exact_node_t *node = &tree->nodes[i];
            double total = 0.0;
            uint32_t c;
            if ((int)node->depth != depth || node->terminal ||
                node->actor == (int)ctx->br_player)
                continue;
            if (node->actor < 0)
            {
                for (c = 0u; c < node->child_count; ++c)
                {
                    if (exact_check_time(ctx, 0))
                    {
                        free(reach);
                        free(processed);
                        return -1;
                    }
                    total += values[node->children[c]];
                }
                values[i] = total / (double)node->child_count;
            }
            else
            {
                for (c = 0u; c < node->child_count; ++c)
                {
                    if (exact_check_time(ctx, 0))
                    {
                        free(reach);
                        free(processed);
                        return -1;
                    }
                    double p;
                    if (exact_action_probability(ctx, node->state,
                                                  node->actions,
                                                  (uint16_t)c, &p) != 0)
                    {
                        free(reach);
                        free(processed);
                        return -1;
                    }
                    total += p * values[node->children[c]];
                }
                values[i] = total;
            }
        }

        for (i = 0u; i < tree->count; ++i)
        {
            if (exact_check_time(ctx, 0))
            {
                free(reach);
                free(processed);
                return -1;
            }
            const exact_node_t *node = &tree->nodes[i];
            double scores[PE_EXTERNAL_MAX_ACTIONS] = {0.0};
            uint64_t key;
            uint16_t actions;
            uint16_t best = 0u;
            size_t j;
            uint32_t c;
            if ((int)node->depth != depth || node->terminal ||
                node->actor != (int)ctx->br_player || processed[i])
                continue;
            key = ctx->game->infoset_key ? node->infoset_key : (uint64_t)i;
            actions = node->actions;
            for (j = i; j < tree->count; ++j)
            {
                if (exact_check_time(ctx, 0))
                {
                    free(reach);
                    free(processed);
                    return -1;
                }
                const exact_node_t *member = &tree->nodes[j];
                uint64_t member_key = ctx->game->infoset_key
                    ? member->infoset_key : (uint64_t)j;
                if ((int)member->depth != depth || member->terminal ||
                    member->actor != (int)ctx->br_player ||
                    member_key != key)
                    continue;
                if (member->actions != actions)
                {
                    free(reach);
                    free(processed);
                    return -1;
                }
                processed[j] = 1u;
                for (c = 0u; c < actions; ++c)
                {
                    if (exact_check_time(ctx, 0))
                    {
                        free(reach);
                        free(processed);
                        return -1;
                    }
                    scores[c] += reach[j] * values[member->children[c]];
                }
            }
            for (c = 1u; c < actions; ++c)
                if (scores[c] > scores[best]) best = (uint16_t)c;
            for (j = i; j < tree->count; ++j)
            {
                if (exact_check_time(ctx, 0))
                {
                    free(reach);
                    free(processed);
                    return -1;
                }
                const exact_node_t *member = &tree->nodes[j];
                uint64_t member_key = ctx->game->infoset_key
                    ? member->infoset_key : (uint64_t)j;
                if ((int)member->depth == depth && !member->terminal &&
                    member->actor == (int)ctx->br_player &&
                    member_key == key)
                    values[j] = values[member->children[best]];
            }
        }
        for (i = 0u; i < tree->count; ++i)
        {
            if (exact_check_time(ctx, 0))
            {
                free(reach);
                free(processed);
                return -1;
            }
            if ((int)tree->nodes[i].depth == depth &&
                !pe_finite_double(values[i]))
            {
                free(reach);
                free(processed);
                return -1;
            }
        }
    }
    if (exact_check_time(ctx, 1))
    {
        free(reach);
        free(processed);
        return -1;
    }
    free(reach);
    free(processed);
    return pe_finite_double(values[0]) ? 0 : -1;
}

int pe_external_best_response_exact(const pe_external_game_t *game,
                                    uint8_t br_player,
                                    const pe_external_br_config_t *config,
                                    pe_external_br_result_t *out)
{
    exact_context_t ctx;
    exact_tree_t tree;
    double *policy_values;
    double *br_values;
    long long start;
    double policy;
    double br;
    uint32_t root_index;
    int rc;

    if (!game || !out || !game->root || !game->is_terminal ||
        !game->acting_player || !game->action_count || !game->apply_action ||
        !game->terminal_value || br_player >= game->player_count)
        return PE_BR_ERR_INVALID;
    if (!config) return PE_BR_ERR_INVALID;
    if (config->max_depth == 0u) return PE_BR_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    memset(&ctx, 0, sizeof(ctx));
    memset(&tree, 0, sizeof(tree));
    ctx.game = game;
    ctx.br_player = br_player;
    ctx.max_depth = config->max_depth;
    ctx.max_nodes = config->max_br_nodes;
    ctx.max_time_ms = config->max_br_time_ms;
    if (ctx.max_time_ms)
    {
        start = now_ms();
        if (start < 0) return PE_BR_ERR_INVALID;
        ctx.deadline_ms = start + (long long)ctx.max_time_ms;
    }
    else
    {
        ctx.deadline_ms = -1;
    }

    rc = exact_tree_build(&ctx, &tree, game->root, 0u, &root_index);
    if (rc != 0)
    {
        int infoset_depth_error = tree.infoset_depth_error;
        exact_tree_destroy(&tree, game);
        return infoset_depth_error ? PE_BR_ERR_INFOSET_DEPTH
             : ctx.budget_exceeded ? PE_BR_ERR_BUDGET
             : ctx.chance_not_enumerable ? PE_BR_ERR_CHANCE_NOT_ENUMERABLE
             : PE_BR_ERR_TRAVERSAL;
    }
    (void)root_index;
    if (tree.infoset_depth_error)
    {
        exact_tree_destroy(&tree, game);
        return PE_BR_ERR_INFOSET_DEPTH;
    }
    policy_values = (double *)calloc(tree.count, sizeof(*policy_values));
    br_values = (double *)calloc(tree.count, sizeof(*br_values));
    if (!policy_values || !br_values)
    {
        free(policy_values);
        free(br_values);
        exact_tree_destroy(&tree, game);
        return PE_BR_ERR_TRAVERSAL;
    }
    if (exact_policy_values(&ctx, &tree, policy_values) != 0 ||
        exact_best_response_values(&ctx, &tree, br_values) != 0)
    {
        free(policy_values);
        free(br_values);
        exact_tree_destroy(&tree, game);
        return ctx.budget_exceeded ? PE_BR_ERR_BUDGET : PE_BR_ERR_TRAVERSAL;
    }
    policy = policy_values[0];
    br = br_values[0];

    out->policy_value = policy;
    out->br_value = br;
    out->br_gap = br - policy;
    if (out->br_gap < 0.0) out->br_gap = 0.0;
    out->nodes_visited = tree.count;
    out->mode = PE_BR_EXACT;
    out->guarantee = game->player_count == 2u
        ? PE_GUARANTEE_NASH
        : PE_GUARANTEE_NO_REGRET_ONLY;
    free(policy_values);
    free(br_values);
    exact_tree_destroy(&tree, game);
    return PE_BR_OK;
}

/* ------------------------------------------------------------------ *
 * Issue #233: mode dispatch (AUTO / EXACT / SAMPLED)
 * ------------------------------------------------------------------ */

const char *pe_br_mode_name(pe_br_mode_t mode)
{
    switch (mode)
    {
    case PE_BR_EXACT:   return "exact";
    case PE_BR_SAMPLED: return "sampled";
    case PE_BR_AUTO:    return "auto";
    default:            return "auto";
    }
}

int pe_external_best_response(const pe_external_game_t *game,
                              uint8_t br_player,
                              const pe_external_br_config_t *config,
                              pe_external_br_result_t *out)
{
    pe_external_br_config_t defaults = pe_external_br_config_default();
    pe_external_br_config_t effective;
    int exact_status;

    if (!config) config = &defaults;
    switch (config->mode)
    {
    case PE_BR_SAMPLED:
        return pe_external_best_response_sampled(game, br_player, config, out);
    case PE_BR_EXACT:
        return pe_external_best_response_exact(game, br_player, config, out);
    case PE_BR_AUTO:
    default:
        break;
    }

    /* AUTO: try exact under the configured budget, with conservative
       defaults when none was given. */
    effective = *config;
    if (!effective.max_br_nodes)
        effective.max_br_nodes = PE_BR_AUTO_MAX_NODES_DEFAULT;
    if (!effective.max_br_time_ms)
        effective.max_br_time_ms = PE_BR_AUTO_TIME_MS_DEFAULT;
    effective.mode = PE_BR_EXACT;
    exact_status = pe_external_best_response_exact(game, br_player, &effective,
                                                   out);
    if (exact_status == PE_BR_OK) return PE_BR_OK;

    /* AUTO may fall back, but never silently: the result records the sampled
       mode and the empirical guarantee, so nothing downstream can mistake it
       for an exact measurement. Budget refusals and non-enumerable chance are
       the expected reasons; anything else is a hard game error. */
    if (exact_status != PE_BR_ERR_BUDGET &&
        exact_status != PE_BR_ERR_CHANCE_NOT_ENUMERABLE &&
        exact_status != PE_BR_ERR_INFOSET_DEPTH)
        return exact_status;
    {
        int sampled_status;
        pe_external_br_result_t sampled_out;
        effective.mode = PE_BR_SAMPLED;
        sampled_status = pe_external_best_response_sampled(
            game, br_player, &effective, &sampled_out);
        if (sampled_status != PE_BR_OK) return sampled_status;
        if (out) *out = sampled_out;
    }
    return PE_BR_OK;
}

int pe_external_best_response_sampled(const pe_external_game_t *game,
                                      uint8_t br_player,
                                      const pe_external_br_config_t *config,
                                      pe_external_br_result_t *out)
{
    pe_external_br_config_t defaults = pe_external_br_config_default();
    br_context_t ctx;
    double policy = 0.0;
    double br = 0.0;
    uint32_t samples;
    if (!game || !out || !game->root || !game->is_terminal || !game->acting_player ||
        !game->action_count || !game->apply_action || !game->terminal_value ||
        br_player >= game->player_count) return -1;
    if (!game->sample_chance_child &&
        !( (game->sample_chance || game->sample_chance_with_user) && game->apply_chance) &&
        game->acting_player(game->root, game->user) < 0) return -1;
    if (!config) config = &defaults;
    samples = config->samples ? config->samples : defaults.samples;
    if (config->max_depth == 0u) return -1;
    memset(out, 0, sizeof(*out));
    memset(&ctx, 0, sizeof(ctx));
    ctx.game = game; ctx.br_player = br_player; ctx.max_depth = config->max_depth;
    pe_rng_seed(&ctx.rng, config->seed);
    for (uint32_t i = 0u; i < samples; ++i)
    {
        double value = policy_rollout(&ctx, game->root, 0u);
        if (pe_finite_double(value)) { policy += value; ++out->policy_samples; }
        value = br_rollout(&ctx, game->root, 0u);
        if (pe_finite_double(value)) { br += value; ++out->br_samples; }
    }
    if (out->policy_samples == 0u || out->br_samples == 0u) return -1;
    out->policy_value = policy / (double)out->policy_samples;
    out->br_value = br / (double)out->br_samples;
    out->br_gap = out->br_value - out->policy_value;
    if (out->br_gap < 0.0) out->br_gap = 0.0;
    out->empirical = 1;
    out->mode = PE_BR_SAMPLED;
    out->guarantee = PE_GUARANTEE_EMPIRICAL;
    return 0;
}
