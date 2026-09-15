/* traversal_external.c - External Sampling MCCFR (LNB-01). */

#include <poker_eval/solver/pe_external_traversal.h>

#include "finite_double.h"

#include <math.h>
#include <string.h>

/* Street tags travel as int8_t with PE_STREET_UNKNOWN for "the adapter does
 * not say"; everything outside the table collapses to "no street stats". */
static int external_street_index(int8_t street)
{
    if (street < 0 || street >= PE_SAMPLING_STREET_COUNT)
        return -1;
    return (int)street;
}

static uint16_t external_replicates(const pe_external_sampling_ctx_t *ctx,
                                    int8_t street)
{
    return pe_sampling_replicates_for(ctx->policy, ctx->chance_replicates,
                                      (int)street);
}

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
        if (!pe_finite_double(p) || p < 0.0)
            return -1;
        out[a] = p;
        sum += p;
    }
    if (!(sum > 0.0) || !pe_finite_double(sum))
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
        uint16_t replicates;
        double total = 0.0;
        memset(&sample, 0, sizeof(sample));
        sample.street = PE_STREET_UNKNOWN;
        child = game->sample_chance_child(state, &ctx->rng, &sample,
                                          game->user);
        if (!child || sample.outcome < 0 || !pe_finite_double(sample.importance_ratio) ||
            sample.importance_ratio < 0.0)
            return NAN;
        ctx->sampled_chance_nodes++;
        /* ISS-232: the policy may ask for several independent draws of this
         * chance node.  Each replicate is a full unbiased trajectory, the
         * node's value is their mean (the unbiasedness correction), and every
         * replicate's updates enter the batch, so the street dealt here gets
         * `replicates` times more useful work per visit. */
        replicates = external_replicates(ctx, sample.street);
        for (uint16_t r = 0; r < replicates; ++r)
        {
            double own = own_reach * sample.importance_ratio;
            double opponent = opponent_reach * sample.importance_ratio;
            int street_index = external_street_index(sample.street);
            double value;
            if (street_index >= 0)
                ctx->chance_samples_by_street[street_index]++;
            value = external_visit(ctx, child, own, opponent, batch);
            if (game->release_state) game->release_state(child, game->user);
            total += sample.importance_ratio * value;
            if (r + 1u == replicates)
                break;
            memset(&sample, 0, sizeof(sample));
            sample.street = PE_STREET_UNKNOWN;
            child = game->sample_chance_child(state, &ctx->rng, &sample,
                                              game->user);
            if (!child || sample.outcome < 0 ||
                !pe_finite_double(sample.importance_ratio) ||
                sample.importance_ratio < 0.0)
                return NAN;
        }
        return total / (double)replicates;
    }
    if ((game->sample_chance || game->sample_chance_with_user) &&
        game->apply_chance)
    {
        pe_chance_sample_t sample;
        const void *child;
        /* Legacy adapters only fill outcome/importance_ratio; the street tag
         * must start unknown so a policy reading it before the first callback
         * falls back to the standard single draw instead of an arbitrary
         * replicate quota (ISS-232 review). */
        memset(&sample, 0, sizeof(sample));
        sample.street = PE_STREET_UNKNOWN;
        int sampled = game->sample_chance_with_user
            ? game->sample_chance_with_user(state, &ctx->rng, &sample,
                                            game->user)
            : game->sample_chance(state, &ctx->rng, &sample);
        if (sampled == 0)
        {
            uint16_t replicates;
            double total = 0.0;
            if (sample.outcome < 0 || !pe_finite_double(sample.importance_ratio) ||
                sample.importance_ratio < 0.0)
                return NAN;
            replicates = external_replicates(ctx, sample.street);
            for (uint16_t r = 0; r < replicates; ++r)
            {
                double own = own_reach * sample.importance_ratio;
                double opponent = opponent_reach * sample.importance_ratio;
                int street_index = external_street_index(sample.street);
                double value;
                child = game->apply_chance(state, sample.outcome, game->user);
                if (!child)
                    return NAN;
                ctx->sampled_chance_nodes++;
                if (street_index >= 0)
                    ctx->chance_samples_by_street[street_index]++;
                value = external_visit(ctx, child, own, opponent, batch);
                if (game->release_state) game->release_state(child, game->user);
                total += sample.importance_ratio * value;
                if (r + 1u == replicates)
                    break;
                memset(&sample, 0, sizeof(sample));
                sample.street = PE_STREET_UNKNOWN;
                sampled = game->sample_chance_with_user
                    ? game->sample_chance_with_user(state, &ctx->rng, &sample,
                                                    game->user)
                    : game->sample_chance(state, &ctx->rng, &sample);
                if (sampled != 0 || sample.outcome < 0 ||
                    !pe_finite_double(sample.importance_ratio) ||
                    sample.importance_ratio < 0.0)
                    return NAN;
            }
            return total / (double)replicates;
        }
    }

    int actor = game->acting_player(state, game->user);
    uint16_t actions = game->action_count(state, game->user);
    uint64_t key = game->infoset_key ? game->infoset_key(state, game->user) : 0u;
    int street_index = game->street_of
        ? external_street_index(game->street_of(state, game->user)) : -1;
    double probs[PE_EXTERNAL_MAX_ACTIONS];
    if (actor < 0 || actor >= (int)game->player_count ||
        external_probabilities(game, state, key, actions, probs) != 0)
        return NAN;
    if (street_index >= 0)
        ctx->visits_by_street[street_index]++;

    if (actor == ctx->updating_player)
    {
        double values[PE_EXTERNAL_MAX_ACTIONS];
        double node_value = 0.0;
        pe_infoset_id_t id;
        if (!ctx->storage_ops || !ctx->storage_ops->resolve)
            return NAN;
        id = ctx->storage_ops->resolve(ctx->storage, key, actions, 1u,
                                       street_index >= 0
                                           ? (int8_t)street_index
                                           : PE_STREET_UNKNOWN);
        if (id == PE_INFOSET_ID_INVALID)
            return NAN;
        for (uint16_t a = 0u; a < actions; ++a)
        {
            const void *child = game->apply_action(state, a, game->user);
            if (!child)
                return NAN;
            values[a] = external_visit(ctx, child, own_reach * probs[a],
                                       opponent_reach, batch);
            if (game->release_state) game->release_state(child, game->user);
            if (!pe_finite_double(values[a]))
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
        if (street_index >= 0)
            ctx->updates_by_street[street_index] += actions;
        return node_value;
    }

    int action = external_sample_action(&ctx->rng, probs, actions);
    if (action < 0)
        return NAN;
    const void *child = game->apply_action(state, (uint16_t)action, game->user);
    if (!child)
        return NAN;
    /* The opponent action was already sampled according to probs[action].
     * Its sampling probability must not be folded into opponent_reach again:
     * doing so squares the opponent policy in the counterfactual estimator. */
    double value = external_visit(ctx, child, own_reach,
                                  opponent_reach, batch);
    if (game->release_state) game->release_state(child, game->user);
    return value;
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
    ctx->policy = PE_SAMPLING_STANDARD;
    pe_rng_seed(&ctx->rng, seed);
    ctx->initialized = 1;
    return 0;
}

void pe_external_sampling_set_policy(pe_external_sampling_ctx_t *ctx,
                                     pe_sampling_policy_t policy,
                                     const uint16_t *street_replicates)
{
    if (!ctx)
        return;
    ctx->policy = policy;
    for (int street = 0; street < PE_SAMPLING_STREET_COUNT; ++street)
    {
        uint16_t requested = street_replicates ? street_replicates[street] : 0u;
        /* Zero means "one", so a zero-initialised table stays valid; anything
           the policy does not boost keeps the standard single draw. */
        ctx->chance_replicates[street] = requested ? requested : 1u;
    }
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
    return pe_finite_double(value) ? 0 : -1;
}
