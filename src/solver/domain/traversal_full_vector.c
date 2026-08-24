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
#include <poker_eval/solver/pe_regret.h>
#include <poker_eval/solver/pe_traversal.h>

#include <math.h>
#include <stdlib.h>
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

int pe_traversal_ctx_set_storage(pe_traversal_ctx_t *ctx,
                                 const pe_storage_ops_t *storage,
                                 void *storage_self)
{
    if (!ctx || !ctx->initialized || !storage || !storage_self ||
        !storage->resolve || !storage->values)
        return -1;
    ctx->storage = storage;
    ctx->storage_self = storage_self;
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
                        const pe_reach_vec_t *reach,
                        pe_value_vec_t *out_values,
                        pe_update_batch_t *out_batch)
{
    const pe_vector_game_t *game = ctx->game;
    uint16_t actions;
    int player;
    uint64_t key = 0;
    pe_infoset_id_t infoset = PE_INFOSET_ID_INVALID;
    uint8_t p;

    if (!state || !out_values)
        return -1;
    ctx->visited_nodes++;
    if (game->is_terminal(state, game->user))
    {
        if (game->terminal_values && game->terminal_values(
                state, reach, out_values, game->player_count, game->user) != 0)
            return -1;
        ctx->terminal_nodes++;
        return 0;
    }

    if (game->is_chance && game->is_chance(state, game->user))
    {
        uint16_t outcomes;
        double total_weight = 0.0;
        pe_value_vec_t *child_values;
        uint16_t outcome;

        if (!game->chance_outcome_count || !game->apply_chance ||
            !game->chance_outcome_weight)
            return -1;
        outcomes = game->chance_outcome_count(state, game->user);
        if (outcomes == 0u)
            return -1;
        child_values = (pe_value_vec_t *)calloc(
            (size_t)outcomes * game->player_count, sizeof(*child_values));
        if (!child_values)
            return -1;
        for (outcome = 0u; outcome < outcomes; ++outcome)
        {
            double weight = game->chance_outcome_weight(
                state, outcome, game->user);
            const void *child;
            int rc;
            if (weight < 0.0 || !isfinite(weight))
            {
                for (uint16_t o = 0u; o < outcome; ++o)
                    for (uint8_t p = 0u; p < game->player_count; ++p)
                        pe_vec_free(&child_values[(size_t)o *
                                                  game->player_count + p]);
                free(child_values);
                return -1;
            }
            total_weight += weight;
            for (uint8_t p = 0u; p < game->player_count; ++p)
                if (pe_vec_alloc(&child_values[(size_t)outcome *
                                                game->player_count + p],
                                 game->combo_count) != PE_SOLVER_OK)
                {
                    for (uint16_t o = 0u; o <= outcome; ++o)
                        for (uint8_t q = 0u; q < game->player_count; ++q)
                            pe_vec_free(&child_values[(size_t)o *
                                                      game->player_count + q]);
                    free(child_values);
                    return -1;
                }
            child = game->apply_chance(state, outcome, game->user);
            rc = child ? vector_visit(
                              ctx, child, reach,
                              &child_values[(size_t)outcome * game->player_count],
                              out_batch)
                       : -1;
            if (rc != 0)
            {
                for (uint16_t o = 0u; o <= outcome; ++o)
                    for (uint8_t q = 0u; q < game->player_count; ++q)
                        pe_vec_free(&child_values[(size_t)o *
                                                  game->player_count + q]);
                free(child_values);
                return -1;
            }
        }
        if (!(total_weight > 0.0) || !isfinite(total_weight))
        {
            for (outcome = 0u; outcome < outcomes; ++outcome)
                for (uint8_t p = 0u; p < game->player_count; ++p)
                    pe_vec_free(&child_values[(size_t)outcome *
                                              game->player_count + p]);
            free(child_values);
            return -1;
        }
        for (uint8_t p = 0u; p < game->player_count; ++p)
            pe_vec_fill(&out_values[p], 0.0);
        for (outcome = 0u; outcome < outcomes; ++outcome)
        {
            double weight = game->chance_outcome_weight(
                state, outcome, game->user) / total_weight;
            for (uint8_t p = 0u; p < game->player_count; ++p)
            {
                pe_value_vec_t *child = &child_values[
                    (size_t)outcome * game->player_count + p];
                for (uint16_t combo = 0u; combo < game->combo_count; ++combo)
                    out_values[p].v[combo] += weight * child->v[combo];
            }
        }
        for (outcome = 0u; outcome < outcomes; ++outcome)
            for (uint8_t p = 0u; p < game->player_count; ++p)
                pe_vec_free(&child_values[(size_t)outcome *
                                          game->player_count + p]);
        free(child_values);
        return 0;
    }

    player = game->acting_player(state, game->user);
    actions = game->action_count(state, game->user);
    if (player < 0 || player >= (int)game->player_count || actions == 0)
        return -1;
    if (game->infoset_key)
        key = game->infoset_key(state, game->user);
    if (ctx->storage != NULL)
    {
        infoset = ctx->storage->resolve(ctx->storage_self, key, actions,
                                        game->combo_count, PE_STREET_UNKNOWN);
        if (infoset == PE_INFOSET_ID_INVALID)
            return -1;
        /* Allocate the average slab on first sight. The update/averaging
           tranche will fill it; until then, the zero slab means uniform. */
        if (ctx->storage->values(ctx->storage_self, infoset,
                                 PE_VALUES_AVERAGE, NULL) == NULL)
            return -1;
    }

    if (actions > SIZE_MAX / (size_t)game->combo_count ||
        actions * (size_t)game->combo_count > SIZE_MAX / sizeof(double))
        return -1;
    double *strategies = (double *)calloc(
        (size_t)actions * game->combo_count, sizeof(*strategies));
    pe_value_vec_t *child_values = (pe_value_vec_t *)calloc(
        (size_t)actions * game->player_count, sizeof(*child_values));
    if (!strategies || !child_values)
    {
        free(strategies);
        free(child_values);
        return -1;
    }

    if (game->strategy == NULL && ctx->storage != NULL)
    {
        double *regrets = ctx->storage->values(
            ctx->storage_self, infoset, PE_VALUES_REGRET, NULL);
        if (regrets == NULL || pe_regret_match_vector(
                regrets, strategies, actions, game->combo_count) != 0)
        {
            free(strategies);
            free(child_values);
            return -1;
        }
    }

    for (uint16_t action = 0u; action < actions; ++action)
    {
        pe_value_vec_t strategy = pe_vec_wrap(
            strategies + (size_t)action * game->combo_count,
            game->combo_count);
        pe_reach_vec_t child_reach[PE_TRAVERSAL_MAX_PLAYERS] = {{0}};
        const void *child;
        int rc;

        if (game->strategy != NULL)
            rc = game->strategy(state, key, action, &strategy, game->user);
        else if (ctx->storage == NULL)
        {
            pe_vec_fill(&strategy, 1.0 / (double)actions);
            rc = 0;
        }
        else
            rc = 0;
        if (rc != 0)
        {
            free(strategies);
            free(child_values);
            return -1;
        }

        for (p = 0u; p < game->player_count; ++p)
        {
            if (pe_vec_alloc(&child_reach[p], game->combo_count) != PE_SOLVER_OK)
            {
                while (p > 0u)
                    pe_vec_free(&child_reach[--p]);
                free(strategies);
                free(child_values);
                return -1;
            }
            pe_vec_copy(&child_reach[p], &reach[p]);
        }
        pe_vec_mul(&child_reach[player], &strategy);
        child = game->apply_action(state, action, game->user);
        for (p = 0u; p < game->player_count; ++p)
        {
            if (pe_vec_alloc(&child_values[(size_t)action * game->player_count + p],
                             game->combo_count) != PE_SOLVER_OK)
            {
                for (uint8_t q = 0u; q < game->player_count; ++q)
                    pe_vec_free(&child_reach[q]);
                for (uint16_t a = 0u; a < action; ++a)
                    for (uint8_t q = 0u; q < game->player_count; ++q)
                        pe_vec_free(&child_values[(size_t)a * game->player_count + q]);
                free(strategies);
                free(child_values);
                return -1;
            }
        }
        rc = child ? vector_visit(ctx, child, child_reach,
                                  &child_values[(size_t)action * game->player_count],
                                  out_batch) : -1;
        for (p = 0u; p < game->player_count; ++p)
            pe_vec_free(&child_reach[p]);
        if (rc != 0)
        {
            for (uint16_t a = 0u; a <= action; ++a)
                for (uint8_t q = 0u; q < game->player_count; ++q)
                    pe_vec_free(&child_values[(size_t)a * game->player_count + q]);
            free(strategies);
            free(child_values);
            return -1;
        }
    }

    for (p = 0u; p < game->player_count; ++p)
        pe_vec_fill(&out_values[p], 0.0);
    for (uint16_t action = 0u; action < actions; ++action)
    {
        for (p = 0u; p < game->player_count; ++p)
        {
            pe_value_vec_t *child = &child_values[
                (size_t)action * game->player_count + p];
            for (uint16_t combo = 0u; combo < game->combo_count; ++combo)
                out_values[p].v[combo] += strategies[
                    (size_t)action * game->combo_count + combo] * child->v[combo];
        }
    }

    if (out_batch != NULL && ctx->storage != NULL)
    {
        for (uint16_t action = 0u; action < actions; ++action)
        {
            pe_value_vec_t *action_values = &child_values[
                (size_t)action * game->player_count + player];
            for (uint16_t combo = 0u; combo < game->combo_count; ++combo)
            {
                double opponent_reach = 1.0;
                for (p = 0u; p < game->player_count; ++p)
                    if (p != (uint8_t)player)
                        opponent_reach *= reach[p].v[combo];
                pe_update_t update = {
                    infoset,
                    action,
                    combo,
                    opponent_reach * (action_values->v[combo] -
                                      out_values[player].v[combo]),
                    reach[player].v[combo] * strategies[
                        (size_t)action * game->combo_count + combo]
                };
                if (pe_update_batch_push(out_batch, update) != 0)
                {
                    for (uint16_t a = 0u; a < actions; ++a)
                        for (uint8_t q = 0u; q < game->player_count; ++q)
                            pe_vec_free(&child_values[(size_t)a * game->player_count + q]);
                    free(strategies);
                    free(child_values);
                    return -1;
                }
            }
        }
    }

    for (uint16_t action = 0u; action < actions; ++action)
        for (uint8_t q = 0u; q < game->player_count; ++q)
            pe_vec_free(&child_values[(size_t)action * game->player_count + q]);
    free(strategies);
    free(child_values);
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
    pe_value_vec_t values[PE_TRAVERSAL_MAX_PLAYERS] = {{0}};
    uint8_t p;

    if (!ctx || !ctx->initialized)
        return -1;
    if (out_batch)
        pe_update_batch_clear(out_batch);
    for (p = 0u; p < ctx->game->player_count; ++p)
        if (pe_vec_alloc(&values[p], ctx->game->combo_count) != PE_SOLVER_OK)
        {
            while (p > 0u)
                pe_vec_free(&values[--p]);
            return -1;
        }
    int rc = vector_visit(ctx, ctx->game->root, ctx->reach, values, out_batch);
    for (p = 0u; p < ctx->game->player_count; ++p)
        pe_vec_free(&values[p]);
    return rc;
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
