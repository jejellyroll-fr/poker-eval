/* traversal_external.c - External Sampling MCCFR (LNB-01). */

#include <poker_eval/solver/pe_external_traversal.h>

#include <math.h>
#include <string.h>

static int external_valid(const pe_external_game_t *game, int player)
{
    return game && game->root && game->player_count > 0u &&
           game->player_count <= PE_TRAVERSAL_MAX_PLAYERS &&
           player >= 0 && player < (int)game->player_count &&
           game->is_terminal && game->acting_player &&
           game->action_count && game->apply_action &&
           game->terminal_value;
}

static int external_probabilities(const pe_external_game_t *game,
                                  const void *state, uint64_t key,
                                  uint16_t actions, double *out)
{
    double sum = 0.0;
    if (actions == 0u || actions > PE_EXTERNAL_MAX_ACTIONS)
        return -1;
    for (uint16_t a = 0u; a < actions; ++a)
    {
        double p = game->action_probability
            ? game->action_probability(state, key, a, game->user)
            : 1.0 / (double)actions;
        if (!isfinite(p) || p < 0.0)
            return -1;
        out[a] = p;
        sum += p;
    }
    if (!(sum > 0.0) || !isfinite(sum))
        return -1;
    for (uint16_t a = 0u; a < actions; ++a)
        out[a] /= sum;
    return 0;
}

static int external_sample_action(pe_rng_t *rng, const double *probs,
                                  uint16_t actions)
{
    double target = pe_rng_uniform01(rng);
    double cumulative = 0.0;
    for (uint16_t a = 0u; a < actions; ++a)
    {
        cumulative += probs[a];
        if (target < cumulative || a + 1u == actions)
            return (int)a;
    }
    return -1;
}

static double external_visit(pe_external_sampling_ctx_t *ctx,
                              const void *state, double own_reach,
                              double opponent_reach,
                              pe_update_batch_t *batch)
{
    const pe_external_game_t *game = ctx->game;
    if (!state)
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
        if (!child || sample.outcome < 0 || !isfinite(sample.importance_ratio) ||
            sample.importance_ratio < 0.0)
            return NAN;
        ctx->sampled_chance_nodes++;
        own_reach *= sample.importance_ratio;
        opponent_reach *= sample.importance_ratio;
        return sample.importance_ratio *
               external_visit(ctx, child, own_reach, opponent_reach, batch);
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
            if (sample.outcome < 0 || !isfinite(sample.importance_ratio) ||
                sample.importance_ratio < 0.0)
                return NAN;
            child = game->apply_chance(state, sample.outcome, game->user);
            if (!child)
                return NAN;
            ctx->sampled_chance_nodes++;
            own_reach *= sample.importance_ratio;
            opponent_reach *= sample.importance_ratio;
            return sample.importance_ratio *
                   external_visit(ctx, child, own_reach,
                                  opponent_reach, batch);
        }
    }

    int actor = game->acting_player(state, game->user);
    uint16_t actions = game->action_count(state, game->user);
    uint64_t key = game->infoset_key ? game->infoset_key(state, game->user) : 0u;
    double probs[PE_EXTERNAL_MAX_ACTIONS];
    if (actor < 0 || actor >= (int)game->player_count ||
        external_probabilities(game, state, key, actions, probs) != 0)
        return NAN;

    if (actor == ctx->updating_player)
    {
        double values[PE_EXTERNAL_MAX_ACTIONS];
        double node_value = 0.0;
        pe_infoset_id_t id;
        if (!ctx->storage_ops || !ctx->storage_ops->resolve)
            return NAN;
        id = ctx->storage_ops->resolve(ctx->storage, key, actions, 1u,
                                       PE_STREET_UNKNOWN);
        if (id == PE_INFOSET_ID_INVALID)
            return NAN;
        for (uint16_t a = 0u; a < actions; ++a)
        {
            const void *child = game->apply_action(state, a, game->user);
            if (!child)
                return NAN;
            values[a] = external_visit(ctx, child, own_reach * probs[a],
                                       opponent_reach, batch);
            if (!isfinite(values[a]))
                return NAN;
            node_value += probs[a] * values[a];
        }
        for (uint16_t a = 0u; a < actions; ++a)
        {
            pe_update_t update;
            update.infoset = id;
            update.action = a;
            update.combo = 0u;
            update.delta = opponent_reach * (values[a] - node_value);
            update.average_delta = own_reach * probs[a];
            if (pe_update_batch_push(batch, update) != 0)
                return NAN;
        }
        return node_value;
    }

    int action = external_sample_action(&ctx->rng, probs, actions);
    if (action < 0)
        return NAN;
    const void *child = game->apply_action(state, (uint16_t)action, game->user);
    if (!child)
        return NAN;
    return external_visit(ctx, child, own_reach,
                          opponent_reach * probs[action], batch);
}

int pe_external_sampling_ctx_init(pe_external_sampling_ctx_t *ctx,
                                  const pe_external_game_t *game,
                                  const pe_storage_ops_t *storage_ops,
                                  void *storage,
                                  int updating_player,
                                  uint64_t seed)
{
    if (!ctx || !external_valid(game, updating_player) ||
        !storage_ops || !storage || !storage_ops->resolve)
        return -1;
    memset(ctx, 0, sizeof(*ctx));
    ctx->game = game;
    ctx->storage_ops = storage_ops;
    ctx->storage = storage;
    ctx->updating_player = updating_player;
    pe_rng_seed(&ctx->rng, seed);
    ctx->initialized = 1;
    return 0;
}

void pe_external_sampling_ctx_destroy(pe_external_sampling_ctx_t *ctx)
{
    if (ctx)
        memset(ctx, 0, sizeof(*ctx));
}

int pe_external_sampling_run(pe_external_sampling_ctx_t *ctx,
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
    value = external_visit(ctx, ctx->game->root, 1.0, 1.0, out_batch);
    (void)value;
    return isfinite(value) ? 0 : -1;
}
