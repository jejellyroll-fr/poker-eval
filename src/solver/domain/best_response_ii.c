/*
 * best_response_ii.c - Vector information-set best response (BR-02)
 */

#include <poker_eval/core/pcg_rng.h>
#include <poker_eval/solver/pe_best_response.h>
#include <poker_eval/solver/pe_traversal.h>

#include "finite_double.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PE_BR_MAX_ACTIONS 32u
#define PE_BR_INITIAL_TABLE 64u

typedef struct
{
    uint64_t key;
    uint16_t action_count;
    uint16_t selected;
    double action_values[PE_BR_MAX_ACTIONS];
    int used;
} pe_br_info_t;

typedef struct
{
    const pe_vector_game_t *game;
    uint8_t br_player;
    uint16_t combo_count;
    pe_br_info_t *table;
    size_t table_capacity;
    size_t count;
    size_t visited_nodes;
    int failed;
} pe_br_ctx_t;

static size_t br_hash(uint64_t key, size_t capacity)
{
    return (size_t)(pe_rng_mix(key) & (uint64_t)(capacity - 1u));
}

static int br_table_grow(pe_br_ctx_t *ctx)
{
    pe_br_info_t *old = ctx->table;
    size_t old_capacity = ctx->table_capacity;
    size_t capacity = old_capacity ? old_capacity * 2u : PE_BR_INITIAL_TABLE;
    pe_br_info_t *grown = (pe_br_info_t *)calloc(capacity, sizeof(*grown));
    size_t i;

    if (!grown)
        return -1;
    ctx->table = grown;
    ctx->table_capacity = capacity;
    ctx->count = 0;
    for (i = 0; i < old_capacity; ++i)
    {
        size_t slot;
        if (!old[i].used)
            continue;
        slot = br_hash(old[i].key, capacity);
        while (grown[slot].used)
            slot = (slot + 1u) & (capacity - 1u);
        grown[slot] = old[i];
        ctx->count++;
    }
    free(old);
    return 0;
}

static pe_br_info_t *br_find_info(pe_br_ctx_t *ctx, uint64_t key,
                                  uint16_t action_count, int create)
{
    size_t slot;

    if (!ctx->table_capacity && br_table_grow(ctx) != 0)
        return NULL;
    if (create && (ctx->count + 1u) * 10u >= ctx->table_capacity * 7u)
    {
        if (br_table_grow(ctx) != 0)
            return NULL;
    }
    slot = br_hash(key, ctx->table_capacity);
    for (;;)
    {
        pe_br_info_t *entry = &ctx->table[slot];
        if (!entry->used)
        {
            if (!create)
                return NULL;
            memset(entry, 0, sizeof(*entry));
            entry->used = 1;
            entry->key = key;
            entry->action_count = action_count;
            ctx->count++;
            return entry;
        }
        if (entry->key == key)
        {
            if (entry->action_count != action_count)
                ctx->failed = 1;
            return entry;
        }
        slot = (slot + 1u) & (ctx->table_capacity - 1u);
    }
}

static void br_free_reach(pe_reach_vec_t *reach, uint8_t player_count)
{
    uint8_t player;
    for (player = 0; player < player_count; ++player)
        pe_vec_free(&reach[player]);
}

static void br_release_child(const pe_vector_game_t *game,
                             const void *child)
{
    if (game->release_state && child)
        game->release_state(child, game->user);
}

static int br_copy_reach(const pe_reach_vec_t *source,
                         pe_reach_vec_t *destination,
                         uint8_t player_count, uint16_t combo_count)
{
    uint8_t player;
    memset(destination, 0, PE_TRAVERSAL_MAX_PLAYERS * sizeof(*destination));
    for (player = 0; player < player_count; ++player)
    {
        if (pe_vec_alloc(&destination[player], combo_count) != PE_SOLVER_OK)
        {
            br_free_reach(destination, player);
            return -1;
        }
        pe_vec_copy(&destination[player], &source[player]);
    }
    return 0;
}

static double br_compatible_reach(const pe_br_ctx_t *ctx,
                                  const void *state,
                                  const pe_reach_vec_t *reach,
                                  uint8_t opponent,
                                  size_t br_combo)
{
    const pe_vector_game_t *game = ctx->game;
    double total = 0.0;
    uint16_t opponent_combo;

    /* Preserve the legacy scalar-vector contract when no blocker mapping is
       supplied: one combo index represents one complete outcome.  Range-aware
       games opt into aggregate compatible reach through the callback below. */
    if (!game->combo_compatible)
        return reach[opponent].v[br_combo];

    for (opponent_combo = 0u; opponent_combo < ctx->combo_count;
         ++opponent_combo)
    {
        if (game->combo_compatible(
                state, ctx->br_player, (uint16_t)br_combo, opponent,
                opponent_combo, game->user))
            total += reach[opponent].v[opponent_combo];
    }
    return total;
}

static int br_validate(const pe_vector_game_t *game, uint8_t br_player,
                       const pe_best_response_vector_config_t *config)
{
    return game && game->root && game->player_count > 0 &&
           game->player_count <= PE_TRAVERSAL_MAX_PLAYERS &&
           br_player < game->player_count && game->combo_count > 0 &&
           game->is_terminal && game->acting_player && game->action_count &&
           game->infoset_key && game->apply_action && game->terminal_values &&
           config && config->max_iterations > 0 && config->tie_tolerance >= 0.0 &&
           !isnan(config->tie_tolerance);
}

static int br_strategy(const pe_br_ctx_t *ctx, const void *state,
                       uint64_t key, uint16_t action, pe_vec_t *out)
{
    const pe_vector_game_t *game = ctx->game;
    uint16_t action_count = game->action_count(state, game->user);

    if (action_count == 0 || action_count > PE_BR_MAX_ACTIONS)
        return -1;
    if (game->strategy)
        return game->strategy(state, key, action, out, game->user);
    pe_vec_fill(out, 1.0 / (double)action_count);
    return 0;
}

static double br_chance_weight(const pe_vector_game_t *game,
                               const void *state, uint16_t outcome);
static int br_chance_node(const pe_vector_game_t *game, const void *state,
                          uint16_t *outcomes, double *total_weight);

typedef struct
{
    const pe_vector_game_t *game;
    uint16_t combo_count;
    size_t visited_nodes;
} pe_policy_ctx_t;

static int policy_copy_reach(const pe_reach_vec_t *source,
                             pe_reach_vec_t *destination,
                             uint8_t player_count, uint16_t combo_count)
{
    uint8_t player;

    memset(destination, 0, PE_TRAVERSAL_MAX_PLAYERS * sizeof(*destination));
    for (player = 0; player < player_count; ++player)
    {
        if (pe_vec_alloc(&destination[player], combo_count) != PE_SOLVER_OK)
        {
            br_free_reach(destination, player);
            return -1;
        }
        pe_vec_copy(&destination[player], &source[player]);
    }
    return 0;
}

static int policy_value(pe_policy_ctx_t *ctx, const void *state,
                        const pe_reach_vec_t *reach,
                        double *out_values)
{
    const pe_vector_game_t *game = ctx->game;
    uint16_t action_count;
    uint16_t outcome;
    uint16_t outcomes;
    double total_weight;
    int chance;
    int player;
    uint64_t key;
    uint8_t p;

    if (!state || !out_values)
        return -1;
    ctx->visited_nodes++;
    for (p = 0u; p < game->player_count; ++p)
        out_values[p] = 0.0;

    if (game->is_terminal(state, game->user))
    {
        pe_value_vec_t terminal[PE_TRAVERSAL_MAX_PLAYERS] = {{0}};
        pe_vec_t joint_reach = {0};
        if (pe_vec_alloc(&joint_reach, ctx->combo_count) != PE_SOLVER_OK)
            return -1;
        pe_vec_fill(&joint_reach, 1.0);
        for (p = 0u; p < game->player_count; ++p)
        {
            if (pe_vec_alloc(&terminal[p], ctx->combo_count) !=
                PE_SOLVER_OK)
            {
                pe_vec_free(&joint_reach);
                while (p > 0u)
                    pe_vec_free(&terminal[--p]);
                return -1;
            }
            pe_vec_mul(&joint_reach, &reach[p]);
        }
        if (!game->terminal_values || game->terminal_values(
                state, reach, terminal, game->player_count, game->user) != 0)
        {
            pe_vec_free(&joint_reach);
            for (p = 0u; p < game->player_count; ++p)
                pe_vec_free(&terminal[p]);
            return -1;
        }
        for (p = 0u; p < game->player_count; ++p)
        {
            out_values[p] = pe_vec_dot(&terminal[p], &joint_reach) /
                            (double)ctx->combo_count;
            pe_vec_free(&terminal[p]);
        }
        pe_vec_free(&joint_reach);
        return 0;
    }

    chance = br_chance_node(game, state, &outcomes, &total_weight);
    if (chance < 0)
        return -1;
    if (chance > 0)
    {
        for (outcome = 0u; outcome < outcomes; ++outcome)
        {
            const void *child = game->apply_chance(
                state, (int)outcome, game->user);
            double child_values[PE_TRAVERSAL_MAX_PLAYERS];
            double weight = br_chance_weight(game, state, outcome) /
                            total_weight;
            if (!child || policy_value(ctx, child, reach, child_values) != 0)
            {
                br_release_child(game, child);
                return -1;
            }
            for (p = 0u; p < game->player_count; ++p)
                out_values[p] += weight * child_values[p];
            br_release_child(game, child);
        }
        return 0;
    }

    player = game->acting_player(state, game->user);
    action_count = game->action_count(state, game->user);
    if (player < 0 || player >= (int)game->player_count ||
        action_count == 0 || action_count > PE_BR_MAX_ACTIONS)
        return -1;
    key = game->infoset_key ? game->infoset_key(state, game->user) : 0u;
    for (outcome = 0u; outcome < action_count; ++outcome)
    {
        pe_reach_vec_t child_reach[PE_TRAVERSAL_MAX_PLAYERS];
        pe_vec_t strategy = {0};
        double child_values[PE_TRAVERSAL_MAX_PLAYERS];
        const void *child;
        int rc;

        if (policy_copy_reach(reach, child_reach, game->player_count,
                              ctx->combo_count) != 0)
            return -1;
        if (pe_vec_alloc(&strategy, ctx->combo_count) != PE_SOLVER_OK)
        {
            br_free_reach(child_reach, game->player_count);
            return -1;
        }
        if (game->strategy)
            rc = game->strategy(state, key, outcome, &strategy, game->user);
        else
        {
            pe_vec_fill(&strategy, 1.0 / (double)action_count);
            rc = 0;
        }
        if (rc == 0)
            pe_vec_mul(&child_reach[player], &strategy);
        child = game->apply_action(state, outcome, game->user);
        if (rc == 0 && (!child || policy_value(ctx, child, child_reach,
                                                child_values) != 0))
            rc = -1;
        if (rc == 0)
            for (p = 0u; p < game->player_count; ++p)
                out_values[p] += child_values[p];
        pe_vec_free(&strategy);
        br_free_reach(child_reach, game->player_count);
        br_release_child(game, child);
        if (rc != 0)
            return -1;
    }
    return 0;
}

static double br_chance_weight(const pe_vector_game_t *game,
                               const void *state, uint16_t outcome)
{
    double weight = game->chance_outcome_weight
        ? game->chance_outcome_weight(state, outcome, game->user)
        : 1.0;
    return weight > 0.0 && pe_finite_double(weight) ? weight : 0.0;
}

static int br_chance_node(const pe_vector_game_t *game, const void *state,
                          uint16_t *outcomes, double *total_weight)
{
    uint16_t outcome;

    if (!game->is_chance || !game->is_chance(state, game->user))
        return 0;
    if (!game->chance_outcome_count || !game->apply_chance)
        return -1;
    *outcomes = game->chance_outcome_count(state, game->user);
    if (*outcomes == 0)
        return -1;
    *total_weight = 0.0;
    for (outcome = 0; outcome < *outcomes; ++outcome)
        *total_weight += br_chance_weight(game, state, outcome);
    return *total_weight > 0.0 && pe_finite_double(*total_weight) ? 1 : -1;
}

/* Return utility weighted by every non-BR player's reach. */
static int br_value(pe_br_ctx_t *ctx, const void *state,
                    const pe_reach_vec_t *reach, pe_value_vec_t *out)
{
    const pe_vector_game_t *game = ctx->game;
    uint16_t action_count;
    int player;
    uint64_t key;
    size_t combo;

    ctx->visited_nodes++;
    if (game->is_terminal(state, game->user))
    {
        pe_value_vec_t terminal[PE_TRAVERSAL_MAX_PLAYERS] = {{0}};
        if (pe_vec_alloc(out, ctx->combo_count) != PE_SOLVER_OK)
            return -1;
        for (player = 0; player < game->player_count; ++player)
            if (pe_vec_alloc(&terminal[player], ctx->combo_count) !=
                PE_SOLVER_OK)
            {
                while (player > 0)
                    pe_vec_free(&terminal[--player]);
                pe_vec_free(out);
                return -1;
            }
        if (game->terminal_values(state, reach, terminal,
                                  game->player_count, game->user) != 0)
        {
            for (player = 0; player < game->player_count; ++player)
                pe_vec_free(&terminal[player]);
            pe_vec_free(out);
            return -1;
        }
        for (combo = 0; combo < ctx->combo_count; ++combo)
        {
            double weight = 1.0;
            for (player = 0; player < game->player_count; ++player)
                if (player != (int)ctx->br_player)
                    weight *= br_compatible_reach(ctx, state, reach,
                                                  (uint8_t)player, combo);
            out->v[combo] = terminal[ctx->br_player].v[combo] * weight;
        }
        for (player = 0; player < game->player_count; ++player)
            pe_vec_free(&terminal[player]);
        return 0;
    }

    {
        uint16_t outcomes;
        double total_weight;
        int chance = br_chance_node(game, state, &outcomes, &total_weight);
        if (chance < 0)
            return -1;
        if (chance > 0)
        {
            if (pe_vec_alloc(out, ctx->combo_count) != PE_SOLVER_OK)
                return -1;
            pe_vec_fill(out, 0.0);
            for (action_count = 0; action_count < outcomes; ++action_count)
            {
                const void *child = game->apply_chance(
                    state, (int)action_count, game->user);
                pe_value_vec_t child_value = {0};
                if (!child || br_value(ctx, child, reach, &child_value) != 0)
                {
                    br_release_child(game, child);
                    pe_vec_free(out);
                    pe_vec_free(&child_value);
                    return -1;
                }
                pe_vec_axpy(out, br_chance_weight(game, state, action_count) /
                                  total_weight, &child_value);
                pe_vec_free(&child_value);
                br_release_child(game, child);
            }
            return 0;
        }
    }

    player = game->acting_player(state, game->user);
    action_count = game->action_count(state, game->user);
    if (player < 0 || player >= (int)game->player_count ||
        action_count == 0 || action_count > PE_BR_MAX_ACTIONS)
        return -1;
    key = game->infoset_key(state, game->user);
    if (pe_vec_alloc(out, ctx->combo_count) != PE_SOLVER_OK)
        return -1;
    pe_vec_fill(out, 0.0);

    {
        pe_br_info_t *entry = player == (int)ctx->br_player
            ? br_find_info(ctx, key, action_count, 1) : NULL;
        uint16_t selected = entry ? entry->selected : 0u;
        uint16_t action;
        if (player == (int)ctx->br_player && !entry)
        {
            pe_vec_free(out);
            return -1;
        }
        for (action = 0; action < action_count; ++action)
        {
            pe_reach_vec_t child_reach[PE_TRAVERSAL_MAX_PLAYERS];
            pe_vec_t child_value = {0};
            int rc;
            if (player == (int)ctx->br_player && action != selected)
                continue;
            if (br_copy_reach(reach, child_reach, game->player_count,
                              ctx->combo_count) != 0)
            {
                pe_vec_free(out);
                return -1;
            }
            if (player != (int)ctx->br_player)
            {
                pe_vec_t strategy = {0};
                if (pe_vec_alloc(&strategy, ctx->combo_count) !=
                    PE_SOLVER_OK || br_strategy(ctx, state, key, action,
                                                &strategy) != 0)
                {
                    pe_vec_free(&strategy);
                    br_free_reach(child_reach, game->player_count);
                    pe_vec_free(out);
                    return -1;
                }
                pe_vec_mul(&child_reach[player], &strategy);
                pe_vec_free(&strategy);
            }
            {
                const void *child = game->apply_action(state, action,
                                                       game->user);
                if (!child)
                    rc = -1;
                else
                {
                    rc = br_value(ctx, child, child_reach, &child_value);
                    br_release_child(game, child);
                }
            }
            br_free_reach(child_reach, game->player_count);
            if (rc != 0)
            {
                pe_vec_free(out);
                return -1;
            }
            pe_vec_axpy(out, 1.0, &child_value);
            pe_vec_free(&child_value);
        }
    }
    return 0;
}

static int br_collect(pe_br_ctx_t *ctx, const void *state,
                      const pe_reach_vec_t *reach, double chance_reach)
{
    const pe_vector_game_t *game = ctx->game;
    uint16_t action_count;
    int player;
    uint64_t key;
    uint16_t action;

    ctx->visited_nodes++;
    if (game->is_terminal(state, game->user))
        return 0;

    {
        uint16_t outcomes;
        double total_weight;
        int chance = br_chance_node(game, state, &outcomes, &total_weight);
        uint16_t outcome;
        (void)total_weight;
        if (chance < 0)
            return -1;
        if (chance > 0)
        {
            for (outcome = 0; outcome < outcomes; ++outcome)
            {
                const void *child = game->apply_chance(
                    state, (int)outcome, game->user);
                double weight = br_chance_weight(game, state, outcome) /
                                total_weight;
                if (!child || br_collect(ctx, child, reach,
                                         chance_reach * weight) != 0)
                {
                    br_release_child(game, child);
                    return -1;
                }
                br_release_child(game, child);
            }
            return 0;
        }
    }

    player = game->acting_player(state, game->user);
    action_count = game->action_count(state, game->user);
    if (player < 0 || player >= (int)game->player_count ||
        action_count == 0 || action_count > PE_BR_MAX_ACTIONS)
        return -1;
    key = game->infoset_key(state, game->user);

    for (action = 0; action < action_count; ++action)
    {
        pe_reach_vec_t child_reach[PE_TRAVERSAL_MAX_PLAYERS];
        const void *child = game->apply_action(state, action, game->user);
        if (!child || br_copy_reach(reach, child_reach, game->player_count,
                                    ctx->combo_count) != 0)
        {
            br_release_child(game, child);
            return -1;
        }
        if (player == (int)ctx->br_player)
        {
            pe_br_info_t *entry = br_find_info(ctx, key, action_count, 1);
            pe_vec_t action_value = {0};
            if (!entry || br_value(ctx, child, child_reach, &action_value) != 0)
            {
                br_free_reach(child_reach, game->player_count);
                pe_vec_free(&action_value);
                br_release_child(game, child);
                return -1;
            }
            /* br_value may discover a descendant infoset and grow the hash
             * table, so reacquire the entry before writing the aggregate. */
            entry = br_find_info(ctx, key, action_count, 0);
            if (!entry)
            {
                br_free_reach(child_reach, game->player_count);
                pe_vec_free(&action_value);
                br_release_child(game, child);
                return -1;
            }
            entry->action_values[action] +=
                chance_reach * pe_vec_sum(&action_value);
            pe_vec_free(&action_value);
        }
        if (player != (int)ctx->br_player)
        {
            pe_vec_t strategy = {0};
            if (pe_vec_alloc(&strategy, ctx->combo_count) != PE_SOLVER_OK ||
                br_strategy(ctx, state, key, action, &strategy) != 0)
            {
                pe_vec_free(&strategy);
                br_free_reach(child_reach, game->player_count);
                br_release_child(game, child);
                return -1;
            }
            pe_vec_mul(&child_reach[player], &strategy);
            pe_vec_free(&strategy);
        }
        if (br_collect(ctx, child, child_reach, chance_reach) != 0)
        {
            br_free_reach(child_reach, game->player_count);
            br_release_child(game, child);
            return -1;
        }
        br_free_reach(child_reach, game->player_count);
        br_release_child(game, child);
    }
    return 0;
}

pe_best_response_vector_config_t pe_best_response_vector_config_default(void)
{
    pe_best_response_vector_config_t config = {32u, 0.0};
    return config;
}

pe_solver_status_t pe_best_response_vector(
    const pe_vector_game_t *game,
    uint8_t br_player,
    const pe_best_response_vector_config_t *config,
    pe_best_response_vector_result_t *out_result)
{
    pe_br_ctx_t ctx;
    pe_reach_vec_t root_reach[PE_TRAVERSAL_MAX_PLAYERS] = {{0}};
    pe_vec_t root_value = {0};
    uint32_t iteration;
    uint8_t player;

    if (!out_result)
        return PE_SOLVER_ERR_NULL_ARGUMENT;
    if (!game || !config)
        return PE_SOLVER_ERR_NULL_ARGUMENT;
    if (!br_validate(game, br_player, config))
        return PE_SOLVER_ERR_INVALID_CONFIG;
    memset(out_result, 0, sizeof(*out_result));
    memset(&ctx, 0, sizeof(ctx));
    ctx.game = game;
    ctx.br_player = br_player;
    ctx.combo_count = game->combo_count;
    for (player = 0; player < game->player_count; ++player)
    {
        if (pe_vec_alloc(&root_reach[player], game->combo_count) !=
            PE_SOLVER_OK)
        {
            br_free_reach(root_reach, player);
            return PE_SOLVER_ERR_OUT_OF_MEMORY;
        }
        pe_vec_fill(&root_reach[player], 1.0);
    }

    for (iteration = 0; iteration < config->max_iterations; ++iteration)
    {
        size_t i;
        int changed = 0;
        for (i = 0; i < ctx.table_capacity; ++i)
            if (ctx.table[i].used)
                memset(ctx.table[i].action_values, 0,
                       sizeof(ctx.table[i].action_values));
        if (br_collect(&ctx, game->root, root_reach, 1.0) != 0 ||
            ctx.failed)
        {
            br_free_reach(root_reach, game->player_count);
            free(ctx.table);
            return PE_SOLVER_ERR_INVALID_STATE;
        }
        for (i = 0; i < ctx.table_capacity; ++i)
        {
            pe_br_info_t *entry = &ctx.table[i];
            uint16_t best;
            uint16_t action;
            if (!entry->used)
                continue;
            best = 0u;
            for (action = 1; action < entry->action_count; ++action)
                if (entry->action_values[action] >
                    entry->action_values[best] + config->tie_tolerance)
                    best = action;
            if (best != entry->selected)
            {
                entry->selected = best;
                changed = 1;
            }
        }
        out_result->iterations = iteration + 1u;
        if (!changed && iteration > 0u)
        {
            out_result->converged = 1;
            break;
        }
    }
    if (br_value(&ctx, game->root, root_reach, &root_value) != 0)
    {
        br_free_reach(root_reach, game->player_count);
        free(ctx.table);
        return PE_SOLVER_ERR_INVALID_STATE;
    }
    out_result->value = pe_vec_sum(&root_value) /
                        (double)game->combo_count;
    out_result->infosets = (uint32_t)ctx.count;
    out_result->visited_nodes = ctx.visited_nodes;
    pe_vec_free(&root_value);
    br_free_reach(root_reach, game->player_count);
    free(ctx.table);
    return PE_SOLVER_OK;
}

pe_solver_status_t pe_exploitability_vector(
    const pe_vector_game_t *game,
    const pe_best_response_vector_config_t *config,
    pe_exploitability_vector_result_t *out_result)
{
    pe_policy_ctx_t policy;
    pe_reach_vec_t root_reach[PE_TRAVERSAL_MAX_PLAYERS] = {{0}};
    double policy_values[PE_SOLVER_MAX_PLAYERS] = {0.0};
    uint8_t player;
    int all_converged = 1;

    if (!out_result || !game || !config)
        return PE_SOLVER_ERR_NULL_ARGUMENT;
    if (game->player_count == 0u ||
        game->player_count > PE_SOLVER_MAX_PLAYERS)
        return PE_SOLVER_ERR_INVALID_CONFIG;
    memset(out_result, 0, sizeof(*out_result));
    memset(&policy, 0, sizeof(policy));
    policy.game = game;
    policy.combo_count = game->combo_count;
    if (!game->root || game->combo_count == 0u || !game->is_terminal ||
        !game->acting_player || !game->action_count || !game->infoset_key ||
        !game->apply_action || !game->terminal_values ||
        config->max_iterations == 0u || config->tie_tolerance < 0.0 ||
        isnan(config->tie_tolerance))
        return PE_SOLVER_ERR_INVALID_CONFIG;
    for (player = 0u; player < game->player_count; ++player)
    {
        if (pe_vec_alloc(&root_reach[player], game->combo_count) !=
            PE_SOLVER_OK)
        {
            br_free_reach(root_reach, player);
            return PE_SOLVER_ERR_OUT_OF_MEMORY;
        }
        pe_vec_fill(&root_reach[player], 1.0);
    }
    if (policy_value(&policy, game->root, root_reach, policy_values) != 0)
    {
        br_free_reach(root_reach, game->player_count);
        return PE_SOLVER_ERR_INVALID_STATE;
    }
    br_free_reach(root_reach, game->player_count);
    for (player = 0u; player < game->player_count; ++player)
    {
        pe_best_response_vector_result_t br = {0};
        double gap;
        pe_solver_status_t status = pe_best_response_vector(
            game, player, config, &br);
        if (status != PE_SOLVER_OK)
            return status;
        out_result->policy_value[player] = policy_values[player];
        out_result->br_value[player] = br.value;
        out_result->br_iterations[player] = br.iterations;
        if (!br.converged)
            all_converged = 0;
        gap = br.value - policy_values[player];
        /* A finite best-response iteration can lose only round-off against
           the policy it is replacing. Keep the public metric non-negative. */
        out_result->br_gap[player] = gap > 0.0 ? gap : 0.0;
        out_result->exploitability_raw += out_result->br_gap[player];
    }
    out_result->converged = all_converged;
    return PE_SOLVER_OK;
}

pe_solver_status_t pe_best_response_metrics_from_raw(
    double raw_value, double big_blind, pe_metrics_t *out_metrics)
{
    if (!out_metrics)
        return PE_SOLVER_ERR_NULL_ARGUMENT;
    if (!pe_finite_double(raw_value) || raw_value < 0.0 ||
        !pe_finite_double(big_blind) || big_blind <= 0.0)
        return PE_SOLVER_ERR_INVALID_CONFIG;
    memset(out_metrics, 0, sizeof(*out_metrics));
    out_metrics->exploitability_raw = raw_value;
    out_metrics->big_blind = big_blind;
    out_metrics->exploitability_mbb_per_game = raw_value / big_blind * 1000.0;
    return PE_SOLVER_OK;
}

pe_solver_status_t pe_best_response_guarantee_for_game(
    uint8_t num_players, int is_zero_sum, pe_guarantee_t *out_guarantee)
{
    if (!out_guarantee)
        return PE_SOLVER_ERR_NULL_ARGUMENT;
    if (num_players == 0 || num_players > PE_SOLVER_MAX_PLAYERS ||
        (is_zero_sum != 0 && is_zero_sum != 1))
        return PE_SOLVER_ERR_INVALID_CONFIG;

    if (!is_zero_sum)
        *out_guarantee = PE_GUARANTEE_EMPIRICAL;
    else if (num_players == 2)
        *out_guarantee = PE_GUARANTEE_NASH;
    else
        *out_guarantee = PE_GUARANTEE_NO_REGRET_ONLY;
    return PE_SOLVER_OK;
}

pe_solver_status_t pe_best_response_metrics_from_multiway(
    uint8_t num_players, int is_zero_sum, const double *br_gaps,
    double cce_gap, double utility_imbalance, double big_blind,
    pe_metrics_t *out_metrics)
{
    pe_guarantee_t guarantee;
    double total = 0.0;
    uint8_t player;

    if (!out_metrics || !br_gaps)
        return PE_SOLVER_ERR_NULL_ARGUMENT;
    if (pe_best_response_guarantee_for_game(num_players, is_zero_sum,
                                            &guarantee) != PE_SOLVER_OK ||
        !pe_finite_double(cce_gap) || cce_gap < 0.0 ||
        !pe_finite_double(utility_imbalance) || utility_imbalance < 0.0 ||
        !pe_finite_double(big_blind) || big_blind <= 0.0)
        return PE_SOLVER_ERR_INVALID_CONFIG;

    for (player = 0; player < num_players; ++player)
    {
        if (!pe_finite_double(br_gaps[player]) || br_gaps[player] < 0.0)
            return PE_SOLVER_ERR_INVALID_CONFIG;
        total += br_gaps[player];
    if (!pe_finite_double(total))
            return PE_SOLVER_ERR_INVALID_CONFIG;
    }

    memset(out_metrics, 0, sizeof(*out_metrics));
    out_metrics->exploitability_raw = total;
    out_metrics->exploitability_mbb_per_game = total / big_blind * 1000.0;
    out_metrics->big_blind = big_blind;
    out_metrics->guarantee = guarantee;
    out_metrics->num_players = num_players;
    for (player = 0; player < num_players; ++player)
        out_metrics->br_gap[player] = br_gaps[player];
    out_metrics->cce_gap = cce_gap;
    out_metrics->utility_imbalance = utility_imbalance;
    return PE_SOLVER_OK;
}

pe_solver_status_t pe_best_response_target_reached(
    double measured_mbb, double target_mbb, int *out_reached)
{
    if (!out_reached)
        return PE_SOLVER_ERR_NULL_ARGUMENT;
    if (!pe_finite_double(measured_mbb) || measured_mbb < 0.0 ||
        !pe_finite_double(target_mbb) || target_mbb < 0.0)
        return PE_SOLVER_ERR_INVALID_CONFIG;
    *out_reached = target_mbb > 0.0 && measured_mbb <= target_mbb;
    return PE_SOLVER_OK;
}
