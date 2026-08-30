/*
 * traversal_chance_vector.c - Sampled vector traversal (VEC-08)
 */

#include <poker_eval/solver/pe_traversal.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

static int chance_game_valid(const pe_vector_game_t *game)
{
    return game && game->root && game->player_count > 0u &&
           game->player_count <= PE_TRAVERSAL_MAX_PLAYERS &&
           game->combo_count > 0u && game->is_terminal;
}

static int allocate_values(pe_value_vec_t *values, uint8_t player_count,
                           uint16_t combo_count)
{
    uint8_t player;
    for (player = 0u; player < player_count; ++player)
    {
        if (pe_vec_alloc(&values[player], combo_count) != PE_SOLVER_OK)
        {
            while (player > 0u)
                pe_vec_free(&values[--player]);
            return -1;
        }
    }
    return 0;
}

static void free_values(pe_value_vec_t *values, uint8_t player_count)
{
    uint8_t player;
    for (player = 0u; player < player_count; ++player)
        pe_vec_free(&values[player]);
}

static int visit_sampled(pe_chance_vector_ctx_t *ctx,
                         const void *state,
                         const pe_reach_vec_t *reach,
                         pe_value_vec_t *out_values)
{
    const pe_vector_game_t *game = ctx->game;
    uint8_t player;

    if (!state)
        return -1;
    ctx->visited_nodes++;

    if (game->is_terminal(state, game->user))
    {
        ctx->terminal_nodes++;
        if (!game->terminal_values)
            return 0;
        return game->terminal_values(state, reach, out_values,
                                     game->player_count, game->user);
    }

    if (ctx->sample_chance)
    {
        pe_chance_sample_t sample;
        const void *child;
        int rc;

        /* A chance callback reports nonzero for a non-chance state. A real
           chance sample is recognized by the callback's outcome being set. */
        memset(&sample, 0, sizeof(sample));
        rc = ctx->sample_chance(state, &ctx->rng, &sample, game->user);
        if (rc == 0)
        {
            if (sample.outcome < 0 || sample.importance_ratio < 0.0 ||
                isnan(sample.importance_ratio) || !ctx->apply_chance)
                return -1;
            child = ctx->apply_chance(state, sample.outcome, game->user);
            if (!child)
                return -1;
            rc = visit_sampled(ctx, child, reach, out_values);
            if (game->release_state)
                game->release_state(child, game->user);
            if (rc != 0)
                return -1;
            pe_vec_scale(&out_values[0], sample.importance_ratio);
            for (player = 1u; player < game->player_count; ++player)
                pe_vec_scale(&out_values[player], sample.importance_ratio);
            ctx->sampled_chance_nodes++;
            ctx->importance_ratio *= sample.importance_ratio;
            return 0;
        }
    }

    if (!game->acting_player || !game->action_count || !game->apply_action)
        return -1;
    {
        int acting = game->acting_player(state, game->user);
        uint16_t actions = game->action_count(state, game->user);
        uint16_t action;

        if (acting < 0 || acting >= (int)game->player_count || actions == 0u)
            return -1;
        for (action = 0u; action < actions; ++action)
        {
            pe_value_vec_t strategy = {0};
            pe_value_vec_t child_values[PE_TRAVERSAL_MAX_PLAYERS] = {{0}};
            pe_reach_vec_t child_reach[PE_TRAVERSAL_MAX_PLAYERS] = {{0}};
            uint64_t key = game->infoset_key
                ? game->infoset_key(state, game->user) : 0u;
            const void *child;
            uint8_t p;

            if (pe_vec_alloc(&strategy, game->combo_count) != PE_SOLVER_OK ||
                allocate_values(child_values, game->player_count,
                                game->combo_count) != 0)
            {
                pe_vec_free(&strategy);
                free_values(child_values, game->player_count);
                return -1;
            }
            if (game->strategy)
                p = (uint8_t)(game->strategy(state, key, action, &strategy,
                                              game->user) != 0);
            else
            {
                pe_vec_fill(&strategy, 1.0 / (double)actions);
                p = 0u;
            }
            if (p != 0u)
            {
                pe_vec_free(&strategy);
                free_values(child_values, game->player_count);
                return -1;
            }
            memset(child_reach, 0, sizeof(child_reach));
            for (p = 0u; p < game->player_count; ++p)
            {
                if (pe_vec_alloc(&child_reach[p], game->combo_count) != PE_SOLVER_OK)
                {
                    uint8_t q;
                    for (q = 0u; q < p; ++q)
                        pe_vec_free(&child_reach[q]);
                    pe_vec_free(&strategy);
                    free_values(child_values, game->player_count);
                    return -1;
                }
                pe_vec_copy(&child_reach[p], &reach[p]);
            }
            pe_vec_mul(&child_reach[acting], &strategy);
            child = game->apply_action(state, action, game->user);
            if (!child)
            {
                for (p = 0u; p < game->player_count; ++p)
                    pe_vec_free(&child_reach[p]);
                pe_vec_free(&strategy);
                free_values(child_values, game->player_count);
                return -1;
            }
            p = (uint8_t)(visit_sampled(ctx, child, child_reach, child_values) != 0);
            if (game->release_state)
                game->release_state(child, game->user);
            if (p != 0u)
            {
                for (p = 0u; p < game->player_count; ++p)
                    pe_vec_free(&child_reach[p]);
                pe_vec_free(&strategy);
                free_values(child_values, game->player_count);
                return -1;
            }
            for (p = 0u; p < game->player_count; ++p)
            {
                pe_vec_mul(&child_values[p], &strategy);
                pe_vec_axpy(&out_values[p], 1.0, &child_values[p]);
                pe_vec_free(&child_reach[p]);
            }
            pe_vec_free(&strategy);
            free_values(child_values, game->player_count);
        }
    }
    return 0;
}

int pe_chance_vector_ctx_init(pe_chance_vector_ctx_t *ctx,
                              const pe_vector_game_t *game,
                              pe_vector_chance_sample_fn sample_chance,
                              pe_vector_apply_chance_fn apply_chance,
                              uint64_t seed)
{
    uint8_t player;

    if (!ctx || !chance_game_valid(game) || !sample_chance || !apply_chance)
        return -1;
    memset(ctx, 0, sizeof(*ctx));
    ctx->game = game;
    ctx->sample_chance = sample_chance;
    ctx->apply_chance = apply_chance;
    ctx->user = game->user;
    pe_rng_seed(&ctx->rng, seed);
    for (player = 0u; player < game->player_count; ++player)
    {
        if (pe_vec_alloc(&ctx->reach[player], game->combo_count) != PE_SOLVER_OK)
        {
            pe_chance_vector_ctx_destroy(ctx);
            return -1;
        }
        pe_vec_fill(&ctx->reach[player], 1.0);
    }
    if (allocate_values(ctx->values, game->player_count, game->combo_count) != 0)
    {
        pe_chance_vector_ctx_destroy(ctx);
        return -1;
    }
    ctx->initialized = 1;
    return 0;
}

void pe_chance_vector_ctx_destroy(pe_chance_vector_ctx_t *ctx)
{
    if (!ctx)
        return;
    free_values(ctx->reach, ctx->game ? ctx->game->player_count : 0u);
    free_values(ctx->values, ctx->game ? ctx->game->player_count : 0u);
    memset(ctx, 0, sizeof(*ctx));
}

int pe_chance_vector_run(pe_chance_vector_ctx_t *ctx)
{
    uint8_t player;

    if (!ctx || !ctx->initialized)
        return -1;
    for (player = 0u; player < ctx->game->player_count; ++player)
    {
        pe_vec_fill(&ctx->reach[player], 1.0);
        pe_vec_fill(&ctx->values[player], 0.0);
    }
    ctx->visited_nodes = 0;
    ctx->terminal_nodes = 0;
    ctx->sampled_chance_nodes = 0;
    ctx->importance_ratio = 1.0;
    return visit_sampled(ctx, ctx->game->root, ctx->reach, ctx->values);
}

const pe_value_vec_t *pe_chance_vector_values(
    const pe_chance_vector_ctx_t *ctx, uint8_t player)
{
    if (!ctx || !ctx->initialized || player >= ctx->game->player_count)
        return NULL;
    return &ctx->values[player];
}
