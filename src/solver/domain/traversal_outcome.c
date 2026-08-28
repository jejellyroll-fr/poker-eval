/* traversal_outcome.c - Outcome Sampling MCCFR (OUT-01). */

#include <poker_eval/solver/pe_outcome_traversal.h>

#include "finite_double.h"

#include <math.h>
#include <string.h>

static int outcome_valid(const pe_external_game_t *game, int player,
                         double epsilon)
{
    return game && game->root && game->player_count > 0u &&
           game->player_count <= PE_TRAVERSAL_MAX_PLAYERS &&
           player >= 0 && player < (int)game->player_count &&
           epsilon >= 0.0 && epsilon <= 1.0 && pe_finite_double(epsilon) &&
           game->is_terminal && game->acting_player && game->action_count &&
           game->apply_action && game->terminal_value;
}

static int outcome_probs(const pe_external_game_t *game, const void *state,
                         uint64_t key, uint16_t actions, double *out)
{
    double total = 0.0;
    if (actions == 0u || actions > PE_EXTERNAL_MAX_ACTIONS)
        return -1;
    for (uint16_t a = 0u; a < actions; ++a)
    {
        double p = game->action_probability
            ? game->action_probability(state, key, a, game->user)
            : 1.0 / (double)actions;
        if (!pe_finite_double(p) || p < 0.0)
            return -1;
        out[a] = p;
        total += p;
    }
    if (!(total > 0.0) || !pe_finite_double(total))
        return -1;
    for (uint16_t a = 0u; a < actions; ++a)
        out[a] /= total;
    return 0;
}

static int outcome_sample_action(pe_outcome_sampling_ctx_t *ctx,
                                  const double *target, uint16_t actions,
                                  double *sampling_probability)
{
    double draw = pe_rng_uniform01(&ctx->rng);
    double cumulative = 0.0;
    for (uint16_t a = 0u; a < actions; ++a)
    {
        double q = (1.0 - ctx->epsilon) * target[a] +
                   ctx->epsilon / (double)actions;
        cumulative += q;
        if (draw < cumulative || a + 1u == actions)
        {
            *sampling_probability = q;
            return (int)a;
        }
    }
    return -1;
}

static double outcome_visit(pe_outcome_sampling_ctx_t *ctx,
                             const void *state, double inverse_sampling,
                             pe_update_batch_t *batch)
{
    const pe_external_game_t *game = ctx->game;
    if (!state || !pe_finite_double(inverse_sampling))
        return NAN;
    ctx->visited_nodes++;
    if (game->is_terminal(state, game->user))
    {
        ctx->terminal_nodes++;
        return game->terminal_value(state, ctx->updating_player, game->user);
    }

    if (game->sample_chance_child && game->acting_player(state, game->user) < 0)
    {
        pe_chance_sample_t sample;
        const void *child;
        memset(&sample, 0, sizeof(sample));
        child = game->sample_chance_child(state, &ctx->rng, &sample,
                                          game->user);
        if (!child || sample.outcome < 0 || !pe_finite_double(sample.importance_ratio) ||
            sample.importance_ratio <= 0.0)
            return NAN;
        ctx->sampled_chance_nodes++;
        double value = outcome_visit(ctx, child,
                                     inverse_sampling * sample.importance_ratio,
                                     batch);
        if (game->release_state) game->release_state(child, game->user);
        return value;
    }
    if ((game->sample_chance || game->sample_chance_with_user) &&
        game->apply_chance)
    {
        pe_chance_sample_t sample;
        const void *child;
        memset(&sample, 0, sizeof(sample));
        int sampled = game->sample_chance_with_user
            ? game->sample_chance_with_user(state, &ctx->rng, &sample,
                                            game->user)
            : game->sample_chance(state, &ctx->rng, &sample);
        if (sampled == 0)
        {
            if (sample.outcome < 0 || !pe_finite_double(sample.importance_ratio) ||
                sample.importance_ratio <= 0.0)
                return NAN;
            child = game->apply_chance(state, sample.outcome, game->user);
            if (!child)
                return NAN;
            ctx->sampled_chance_nodes++;
            double value = outcome_visit(ctx, child,
                                         inverse_sampling * sample.importance_ratio,
                                         batch);
            if (game->release_state) game->release_state(child, game->user);
            return value;
        }
    }

    int actor = game->acting_player(state, game->user);
    uint16_t actions = game->action_count(state, game->user);
    uint64_t key = game->infoset_key ? game->infoset_key(state, game->user) : 0u;
    double target[PE_EXTERNAL_MAX_ACTIONS];
    double sampling_probability;
    int selected;
    const void *child;
    double value;
    if (actor < 0 || actor >= (int)game->player_count ||
        outcome_probs(game, state, key, actions, target) != 0)
        return NAN;
    selected = outcome_sample_action(ctx, target, actions,
                                     &sampling_probability);
    if (selected < 0 || !(sampling_probability > 0.0))
        return NAN;
    child = game->apply_action(state, (uint16_t)selected, game->user);
    if (!child)
        return NAN;
    ctx->sampled_action_nodes++;
    value = outcome_visit(ctx, child,
                          inverse_sampling / sampling_probability, batch);
    if (game->release_state) game->release_state(child, game->user);
        if (!pe_finite_double(value))
        return NAN;

    if (actor == ctx->updating_player)
    {
        pe_infoset_id_t id;
        if (!ctx->storage_ops || !ctx->storage_ops->resolve)
            return NAN;
        id = ctx->storage_ops->resolve(ctx->storage, key, actions, 1u,
                                       PE_STREET_UNKNOWN);
        if (id == PE_INFOSET_ID_INVALID)
            return NAN;
        for (uint16_t a = 0u; a < actions; ++a)
        {
            pe_update_t update;
            update.infoset = id;
            update.action = a;
            update.combo = 0u;
            update.delta = inverse_sampling * value *
                           ((a == (uint16_t)selected ? 1.0 : 0.0) - target[a]);
            update.average_delta = inverse_sampling * target[a];
            if (pe_update_batch_push(batch, update) != 0)
                return NAN;
        }
    }
    (void)actor;
    return value;
}

int pe_outcome_sampling_ctx_init(pe_outcome_sampling_ctx_t *ctx,
                                 const pe_external_game_t *game,
                                 const pe_storage_ops_t *storage_ops,
                                 void *storage, int updating_player,
                                 double epsilon, uint64_t seed)
{
    if (!ctx || !outcome_valid(game, updating_player, epsilon) ||
        !storage_ops || !storage || !storage_ops->resolve)
        return -1;
    memset(ctx, 0, sizeof(*ctx));
    ctx->game = game;
    ctx->storage_ops = storage_ops;
    ctx->storage = storage;
    ctx->updating_player = updating_player;
    ctx->epsilon = epsilon;
    pe_rng_seed(&ctx->rng, seed);
    ctx->initialized = 1;
    return 0;
}

void pe_outcome_sampling_ctx_destroy(pe_outcome_sampling_ctx_t *ctx)
{
    if (ctx)
        memset(ctx, 0, sizeof(*ctx));
}

int pe_outcome_sampling_run(pe_outcome_sampling_ctx_t *ctx,
                            pe_update_batch_t *out_batch)
{
    double value;
    if (!ctx || !ctx->initialized || !out_batch)
        return -1;
    pe_update_batch_clear(out_batch);
    ctx->iteration++;
    ctx->visited_nodes = 0u;
    ctx->terminal_nodes = 0u;
    ctx->sampled_chance_nodes = 0u;
    ctx->sampled_action_nodes = 0u;
    value = outcome_visit(ctx, ctx->game->root, 1.0, out_batch);
    return pe_finite_double(value) ? 0 : -1;
}
