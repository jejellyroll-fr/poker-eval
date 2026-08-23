/*
 * traversal_full_vector.c - Exact vector traversal skeleton (VEC-02)
 *
 * This first lane-A implementation deliberately stops at traversal. It walks
 * every legal action with one reach vector per player and delegates terminal
 * payoff calculation to the rules adapter. VEC-03 adds regret matching and
 * raw per-(action, combo) update production; no regret or averaging policy is
 * hidden in this module.
 */

#include <poker_eval/solver/pe_capabilities.h>
#include <poker_eval/solver/pe_traversal.h>

#include <string.h>

static int vector_valid(const pe_vector_game_t *game)
{
    return game && game->root && game->player_count > 0 &&
           game->player_count <= PE_TRAVERSAL_MAX_PLAYERS &&
           game->combo_count > 0 && game->is_terminal &&
           game->acting_player && game->action_count && game->apply_action;
}

int pe_traversal_ctx_init(pe_traversal_ctx_t *ctx,
                          const pe_vector_game_t *game)
{
    unsigned p;

    if (!ctx || !vector_valid(game))
        return -1;
    memset(ctx, 0, sizeof(*ctx));
    ctx->game = game;
    for (p = 0; p < game->player_count; ++p)
    {
        if (pe_vec_alloc(&ctx->reach[p], game->combo_count) != PE_SOLVER_OK)
        {
            pe_traversal_ctx_destroy(ctx);
            return -1;
        }
        pe_vec_fill(&ctx->reach[p], 1.0);
    }
    ctx->initialized = 1;
    return 0;
}

void pe_traversal_ctx_destroy(pe_traversal_ctx_t *ctx)
{
    unsigned p;

    if (!ctx)
        return;
    for (p = 0; p < PE_TRAVERSAL_MAX_PLAYERS; ++p)
        pe_vec_free(&ctx->reach[p]);
    memset(ctx, 0, sizeof(*ctx));
}

static int vector_visit(pe_traversal_ctx_t *ctx, const void *state,
                        const pe_reach_vec_t *reach)
{
    const pe_vector_game_t *game = ctx->game;
    uint16_t actions;
    int player;
    uint64_t key = 0;

    if (!state)
        return -1;
    ctx->visited_nodes++;
    if (game->is_terminal(state, game->user))
    {
        if (game->terminal_values)
        {
            pe_value_vec_t values[PE_TRAVERSAL_MAX_PLAYERS];
            unsigned p;
            memset(values, 0, sizeof(values));
            for (p = 0; p < game->player_count; ++p)
            {
                if (pe_vec_alloc(&values[p], game->combo_count) != PE_SOLVER_OK)
                {
                    while (p > 0)
                        pe_vec_free(&values[--p]);
                    return -1;
                }
            }
            if (game->terminal_values(state, reach, values,
                                      game->player_count, game->user) != 0)
            {
                for (p = 0; p < game->player_count; ++p)
                    pe_vec_free(&values[p]);
                return -1;
            }
            for (p = 0; p < game->player_count; ++p)
                pe_vec_free(&values[p]);
        }
        ctx->terminal_nodes++;
        return 0;
    }

    player = game->acting_player(state, game->user);
    actions = game->action_count(state, game->user);
    if (player < 0 || player >= (int)game->player_count || actions == 0)
        return -1;
    if (game->infoset_key)
        key = game->infoset_key(state, game->user);

    for (uint16_t action = 0; action < actions; ++action)
    {
        pe_value_vec_t strategy = {0};
        pe_reach_vec_t child_reach[PE_TRAVERSAL_MAX_PLAYERS];
        const void *child;
        unsigned p;
        int rc;

        if (pe_vec_alloc(&strategy, game->combo_count) != PE_SOLVER_OK)
            return -1;
        if (game->strategy)
            rc = game->strategy(state, key, action, &strategy, game->user);
        else
        {
            pe_vec_fill(&strategy, 1.0 / (double)actions);
            rc = 0;
        }
        if (rc != 0)
        {
            pe_vec_free(&strategy);
            return -1;
        }

        memset(child_reach, 0, sizeof(child_reach));
        for (p = 0; p < game->player_count; ++p)
        {
            if (pe_vec_alloc(&child_reach[p], game->combo_count) != PE_SOLVER_OK)
            {
                while (p > 0)
                    pe_vec_free(&child_reach[--p]);
                pe_vec_free(&strategy);
                return -1;
            }
            pe_vec_copy(&child_reach[p], &reach[p]);
        }
        pe_vec_mul(&child_reach[player], &strategy);
        child = game->apply_action(state, action, game->user);
        rc = child ? vector_visit(ctx, child, child_reach) : -1;
        for (p = 0; p < game->player_count; ++p)
            pe_vec_free(&child_reach[p]);
        pe_vec_free(&strategy);
        if (rc != 0)
            return rc;
    }
    return 0;
}

static int vector_begin(pe_traversal_ctx_t *ctx, uint64_t iteration)
{
    unsigned p;
    if (!ctx || !ctx->initialized)
        return -1;
    ctx->iteration = iteration;
    ctx->visited_nodes = 0;
    ctx->terminal_nodes = 0;
    for (p = 0; p < ctx->game->player_count; ++p)
        pe_vec_fill(&ctx->reach[p], 1.0);
    return 0;
}

static int vector_run(pe_traversal_ctx_t *ctx, pe_update_batch_t *out_batch)
{
    if (!ctx || !ctx->initialized)
        return -1;
    if (out_batch)
        pe_update_batch_clear(out_batch);
    return vector_visit(ctx, ctx->game->root, ctx->reach);
}

static int vector_end(pe_traversal_ctx_t *ctx, uint64_t iteration)
{
    if (!ctx || !ctx->initialized || ctx->iteration != iteration)
        return -1;
    return 0;
}

const pe_traversal_ops_t *pe_traversal_full_vector_ops(void)
{
    static const pe_traversal_ops_t ops = {
        "full_vector",
        PE_CAP_VECTOR_FORM | PE_CAP_ENUMERATED_CHANCE,
        vector_begin,
        vector_run,
        vector_end
    };
    return &ops;
}
