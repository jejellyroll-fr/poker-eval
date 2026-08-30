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

#include "finite_double.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PE_VECTOR_ARENA_INITIAL_CAPACITY 65536u

typedef struct
{
    unsigned char *memory;
    size_t capacity;
    size_t used;
} pe_vector_arena_block_t;

typedef struct
{
    pe_vector_arena_block_t *blocks;
    size_t block_count;
    size_t block_capacity;
    size_t current_block;
} pe_vector_arena_t;

typedef struct
{
    size_t block_index;
    size_t used;
} pe_vector_arena_mark_t;

static pe_vector_arena_t *vector_arena_create(void)
{
    pe_vector_arena_t *arena = (pe_vector_arena_t *)calloc(1u, sizeof(*arena));
    if (arena != NULL)
        arena->current_block = SIZE_MAX;
    return arena;
}

static void vector_arena_destroy(pe_vector_arena_t *arena)
{
    size_t block_index;

    if (arena == NULL)
        return;
    for (block_index = 0u; block_index < arena->block_count; ++block_index)
        free(arena->blocks[block_index].memory);
    free(arena->blocks);
    free(arena);
}

static void *vector_arena_alloc(pe_vector_arena_t *arena, size_t bytes,
                                int *out_new_block)
{
    size_t block_index;
    size_t aligned_used;

    if (arena == NULL || bytes == 0u || out_new_block == NULL)
        return NULL;
    *out_new_block = 0;
    block_index = arena->current_block;
    if (block_index != SIZE_MAX && block_index < arena->block_count)
    {
        pe_vector_arena_block_t *block = &arena->blocks[block_index];
        if (block->used <= SIZE_MAX - 7u)
        {
            aligned_used = (block->used + 7u) & ~(size_t)7u;
            if (aligned_used <= block->capacity &&
                bytes <= block->capacity - aligned_used)
            {
                void *result = block->memory + aligned_used;
                block->used = aligned_used + bytes;
                return result;
            }
        }
    }

    /* A child may have used a later block before releasing back to its
       parent's mark. Reuse such a reset block before growing the arena; this
       is what keeps repeated sibling visits allocation-free. */
    if (block_index != SIZE_MAX)
        for (block_index += 1u; block_index < arena->block_count;
             ++block_index)
            if (arena->blocks[block_index].used == 0u &&
                arena->blocks[block_index].capacity >= bytes)
            {
                arena->blocks[block_index].used = bytes;
                arena->current_block = block_index;
                return arena->blocks[block_index].memory;
            }

    {
        size_t capacity = PE_VECTOR_ARENA_INITIAL_CAPACITY;
        size_t previous_capacity = 0u;
        pe_vector_arena_block_t *grown;
        unsigned char *memory;

        if (arena->block_count != 0u)
            previous_capacity = arena->blocks[arena->block_count - 1u].capacity;
        if (previous_capacity > capacity)
            capacity = previous_capacity;
        while (capacity < bytes)
        {
            if (capacity > SIZE_MAX / 2u)
            {
                capacity = bytes;
                break;
            }
            capacity *= 2u;
        }
        if (arena->block_count == arena->block_capacity)
        {
            size_t new_capacity;
            if (arena->block_capacity > SIZE_MAX / 2u)
                new_capacity = arena->block_count + 1u;
            else
                new_capacity = arena->block_capacity != 0u
                    ? arena->block_capacity * 2u : 4u;
            if (new_capacity < arena->block_count + 1u ||
                new_capacity > SIZE_MAX / sizeof(*grown))
                new_capacity = arena->block_count + 1u;
            grown = (pe_vector_arena_block_t *)realloc(
                arena->blocks, new_capacity * sizeof(*grown));
            if (grown == NULL)
                return NULL;
            arena->blocks = grown;
            arena->block_capacity = new_capacity;
        }
        memory = (unsigned char *)malloc(capacity);
        if (memory == NULL)
            return NULL;
        block_index = arena->block_count++;
        arena->blocks[block_index].memory = memory;
        arena->blocks[block_index].capacity = capacity;
        arena->blocks[block_index].used = bytes;
        arena->current_block = block_index;
        *out_new_block = 1;
        return memory;
    }
}

static pe_vector_arena_mark_t vector_arena_mark(
    const pe_vector_arena_t *arena)
{
    pe_vector_arena_mark_t mark = {SIZE_MAX, 0u};
    if (arena != NULL && arena->current_block != SIZE_MAX &&
        arena->current_block < arena->block_count)
    {
        mark.block_index = arena->current_block;
        mark.used = arena->blocks[arena->current_block].used;
    }
    return mark;
}

static void vector_arena_release(pe_vector_arena_t *arena,
                                 pe_vector_arena_mark_t mark)
{
    size_t block_index;

    if (arena == NULL)
        return;
    if (mark.block_index == SIZE_MAX)
    {
        for (block_index = 0u; block_index < arena->block_count; ++block_index)
            arena->blocks[block_index].used = 0u;
        arena->current_block = SIZE_MAX;
        return;
    }
    if (mark.block_index >= arena->block_count)
        return;
    arena->blocks[mark.block_index].used = mark.used;
    for (block_index = mark.block_index + 1u;
         block_index < arena->block_count; ++block_index)
        arena->blocks[block_index].used = 0u;
    arena->current_block = mark.block_index;
}

static int vector_arena_owns(const pe_vector_arena_t *arena,
                             const double *memory)
{
    size_t block_index;
    uintptr_t address;

    if (arena == NULL || memory == NULL)
        return 0;
    address = (uintptr_t)memory;
    for (block_index = 0u; block_index < arena->block_count; ++block_index)
    {
        uintptr_t begin = (uintptr_t)arena->blocks[block_index].memory;
        if (address >= begin && address - begin < arena->blocks[block_index].capacity)
            return 1;
    }
    return 0;
}

static int vector_valid(const pe_vector_game_t *game)
{
    return game && game->root && game->player_count > 0 &&
           game->player_count <= PE_TRAVERSAL_MAX_PLAYERS &&
           game->combo_count > 0 && game->is_terminal &&
           game->acting_player && game->action_count && game->apply_action;
}

static pe_solver_status_t vector_alloc(pe_traversal_ctx_t *ctx,
                                       pe_vec_t *out, size_t count)
{
    pe_vector_arena_t *arena;
    size_t bytes;
    int new_block = 0;

    if (out == NULL || count == 0u || count > SIZE_MAX / sizeof(double))
        return PE_SOLVER_ERR_INVALID_CONFIG;
    arena = ctx != NULL ? (pe_vector_arena_t *)ctx->arena : NULL;
    bytes = count * sizeof(double);
    if (arena != NULL)
    {
        double *memory = (double *)vector_arena_alloc(
            arena, bytes, &new_block);
        if (memory != NULL)
        {
            *out = pe_vec_wrap(memory, count);
            memset(memory, 0, bytes);
            if (ctx != NULL && new_block)
                ctx->counters.vec_allocs++;
            return PE_SOLVER_OK;
        }
    }
    if (pe_vec_alloc(out, count) != PE_SOLVER_OK)
        return PE_SOLVER_ERR_OUT_OF_MEMORY;
    if (ctx != NULL)
        ctx->counters.vec_allocs++;
    return PE_SOLVER_OK;
}

static void vector_free(pe_traversal_ctx_t *ctx, pe_vec_t *vector)
{
    pe_vector_arena_t *arena;

    if (vector == NULL)
        return;
    arena = ctx != NULL ? (pe_vector_arena_t *)ctx->arena : NULL;
    if (vector_arena_owns(arena, vector->v))
    {
        vector->v = NULL;
        vector->n = 0u;
        return;
    }
    pe_vec_free(vector);
}

/* Existing cleanup calls below all operate on vectors belonging to `ctx`.
   Heap vectors remain supported through vector_free's fallback. */
#define pe_vec_free(vector) vector_free(ctx, (vector))

static int vector_visit(pe_traversal_ctx_t *ctx, const void *state,
                        const pe_reach_vec_t *reach,
                        pe_value_vec_t *out_values,
                        pe_update_batch_t *out_batch,
                        double chance_reach);

int pe_traversal_ctx_init(pe_traversal_ctx_t *ctx,
                          const pe_vector_game_t *game)
{
    unsigned p;

    if (!ctx || !vector_valid(game))
        return -1;
    memset(ctx, 0, sizeof(*ctx));
    ctx->game = game;
    ctx->arena = vector_arena_create();
    for (p = 0; p < game->player_count; ++p)
    {
        if (vector_alloc(ctx, &ctx->reach[p], game->combo_count) != PE_SOLVER_OK)
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
    vector_arena_destroy((pe_vector_arena_t *)ctx->arena);
    memset(ctx, 0, sizeof(*ctx));
}

static int vector_visit_impl(pe_traversal_ctx_t *ctx, const void *state,
                        const pe_reach_vec_t *reach,
                        pe_value_vec_t *out_values,
                        pe_update_batch_t *out_batch,
                        double chance_reach)
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
        double *outcome_weights;
        uint16_t outcome;

        if (!game->chance_outcome_count || !game->apply_chance ||
            !game->chance_outcome_weight)
            return -1;
        outcomes = game->chance_outcome_count(state, game->user);
        if (outcomes == 0u)
            return -1;
        child_values = (pe_value_vec_t *)calloc(
            (size_t)outcomes * game->player_count, sizeof(*child_values));
        outcome_weights = (double *)calloc(outcomes, sizeof(*outcome_weights));
        if (!child_values || !outcome_weights)
        {
            free(child_values);
            free(outcome_weights);
            return -1;
        }
        for (outcome = 0u; outcome < outcomes; ++outcome)
        {
            double weight = game->chance_outcome_weight(
                state, outcome, game->user);
            if (weight < 0.0 || !pe_finite_double(weight))
            {
                free(child_values);
                free(outcome_weights);
                return -1;
            }
            outcome_weights[outcome] = weight;
            total_weight += weight;
        }
        if (!(total_weight > 0.0) || !pe_finite_double(total_weight))
        {
            free(child_values);
            free(outcome_weights);
            return -1;
        }
        for (outcome = 0u; outcome < outcomes; ++outcome)
        {
            for (uint8_t p = 0u; p < game->player_count; ++p)
                if (vector_alloc(ctx, &child_values[(size_t)outcome *
                                                game->player_count + p],
                                 game->combo_count) != PE_SOLVER_OK)
                {
                    for (uint16_t o = 0u; o <= outcome; ++o)
                        for (uint8_t q = 0u; q < game->player_count; ++q)
                            pe_vec_free(&child_values[(size_t)o *
                                                      game->player_count + q]);
                    free(child_values);
                    free(outcome_weights);
                    return -1;
                }
        }
        for (outcome = 0u; outcome < outcomes; ++outcome)
        {
            const void *child = game->apply_chance(state, outcome, game->user);
            double weight = outcome_weights[outcome] / total_weight;
            int rc = child ? vector_visit(
                                  ctx, child, reach,
                                  &child_values[(size_t)outcome * game->player_count],
                                  out_batch, chance_reach * weight)
                           : -1;
            if (child && game->release_state)
                game->release_state(child, game->user);
            if (rc != 0)
            {
                for (uint16_t o = 0u; o <= outcome; ++o)
                    for (uint8_t q = 0u; q < game->player_count; ++q)
                        pe_vec_free(&child_values[(size_t)o *
                                                  game->player_count + q]);
                free(child_values);
                free(outcome_weights);
                return -1;
            }
        }
        for (uint8_t p = 0u; p < game->player_count; ++p)
            pe_vec_fill(&out_values[p], 0.0);
        for (outcome = 0u; outcome < outcomes; ++outcome)
        {
            double weight = outcome_weights[outcome] / total_weight;
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
        free(outcome_weights);
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
            if (vector_alloc(ctx, &child_reach[p], game->combo_count) != PE_SOLVER_OK)
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
            if (vector_alloc(ctx, &child_values[(size_t)action * game->player_count + p],
                             game->combo_count) != PE_SOLVER_OK)
            {
                if (child && game->release_state)
                    game->release_state(child, game->user);
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
                                  out_batch, chance_reach) : -1;
        if (child && game->release_state)
            game->release_state(child, game->user);
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
        pe_vec_t opponent = {NULL, 0u};
        double *opponent_reach;
        double *deltas;
        double *average_deltas;

        /* The opponent product is independent of the action.  Computing it
           once per combo removes a player-count loop from the action loop.
           It comes from the same arena as every other per-node vector, so a
           node costs no allocation once the arena has grown. */
        if (vector_alloc(ctx, &opponent,
                         (size_t)game->combo_count) != PE_SOLVER_OK)
        {
            for (uint16_t a = 0u; a < actions; ++a)
                for (uint8_t q = 0u; q < game->player_count; ++q)
                    pe_vec_free(&child_values[(size_t)a * game->player_count + q]);
            free(strategies);
            free(child_values);
            return -1;
        }
        opponent_reach = opponent.v;
        for (uint16_t combo = 0u; combo < game->combo_count; ++combo)
        {
            opponent_reach[combo] = 1.0;
            for (p = 0u; p < game->player_count; ++p)
                if (p != (uint8_t)player)
                    opponent_reach[combo] *= reach[p].v[combo];
        }
        if (pe_update_batch_soa_begin_group(
                out_batch, infoset, actions, game->combo_count,
                &deltas, &average_deltas) != 0)
        {
            pe_vec_free(&opponent);
            for (uint16_t a = 0u; a < actions; ++a)
                for (uint8_t q = 0u; q < game->player_count; ++q)
                    pe_vec_free(&child_values[(size_t)a * game->player_count + q]);
            free(strategies);
            free(child_values);
            return -1;
        }
        ctx->counters.updates_emitted +=
            (uint64_t)actions * (uint64_t)game->combo_count;
        {
            uint64_t bytes = (uint64_t)out_batch->soa.group_count *
                (uint64_t)sizeof(pe_update_group_t) +
                (uint64_t)out_batch->soa.value_count * 2u *
                (uint64_t)sizeof(double);
            if (bytes > ctx->counters.batch_peak_bytes)
                ctx->counters.batch_peak_bytes = bytes;
        }
        for (uint16_t action = 0u; action < actions; ++action)
        {
            pe_value_vec_t *action_values = &child_values[
                (size_t)action * game->player_count + player];
            for (uint16_t combo = 0u; combo < game->combo_count; ++combo)
            {
                size_t slot = (size_t)action * game->combo_count + combo;
                deltas[slot] += chance_reach * opponent_reach[combo] *
                    (action_values->v[combo] - out_values[player].v[combo]);
                average_deltas[slot] += chance_reach * reach[player].v[combo] *
                    strategies[slot];
            }
        }
        pe_vec_free(&opponent);
    }

    for (uint16_t action = 0u; action < actions; ++action)
        for (uint8_t q = 0u; q < game->player_count; ++q)
            pe_vec_free(&child_values[(size_t)action * game->player_count + q]);
    free(strategies);
    free(child_values);
    return 0;
}

static int vector_visit(pe_traversal_ctx_t *ctx, const void *state,
                        const pe_reach_vec_t *reach,
                        pe_value_vec_t *out_values,
                        pe_update_batch_t *out_batch,
                        double chance_reach)
{
    pe_vector_arena_t *arena = ctx != NULL
        ? (pe_vector_arena_t *)ctx->arena : NULL;
    pe_vector_arena_mark_t mark = vector_arena_mark(arena);
    int rc = vector_visit_impl(ctx, state, reach, out_values, out_batch,
                               chance_reach);
    vector_arena_release(arena, mark);
    return rc;
}

static int vector_begin(pe_traversal_ctx_t *ctx, uint64_t iteration)
{
    unsigned p;
    if (!ctx || !ctx->initialized)
        return -1;
    ctx->iteration = iteration;
    ctx->visited_nodes = 0;
    ctx->terminal_nodes = 0;
    memset(&ctx->counters, 0, sizeof(ctx->counters));
    for (p = 0; p < ctx->game->player_count; ++p)
        pe_vec_fill(&ctx->reach[p], 1.0);
    return 0;
}

static int vector_run_impl(pe_traversal_ctx_t *ctx,
                           pe_update_batch_t *out_batch)
{
    pe_value_vec_t values[PE_TRAVERSAL_MAX_PLAYERS] = {{0}};
    uint8_t p;

    if (!ctx || !ctx->initialized)
        return -1;
    if (out_batch)
        pe_update_batch_clear(out_batch);
    for (p = 0u; p < ctx->game->player_count; ++p)
        if (vector_alloc(ctx, &values[p], ctx->game->combo_count) != PE_SOLVER_OK)
        {
            while (p > 0u)
                pe_vec_free(&values[--p]);
            return -1;
        }
    int rc = vector_visit(ctx, ctx->game->root, ctx->reach, values, out_batch,
                          1.0);
    for (p = 0u; p < ctx->game->player_count; ++p)
        pe_vec_free(&values[p]);
    return rc;
}

static int vector_run(pe_traversal_ctx_t *ctx, pe_update_batch_t *out_batch)
{
    pe_vector_arena_t *arena = ctx != NULL
        ? (pe_vector_arena_t *)ctx->arena : NULL;
    pe_vector_arena_mark_t mark = vector_arena_mark(arena);
    int rc = vector_run_impl(ctx, out_batch);
    vector_arena_release(arena, mark);
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
