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

/* Issue #256: the adaptive group of a chance visit, from the street the
 * adapter tags it with or, failing that, from its chance depth. */
static int external_adaptive_group(const pe_external_sampling_ctx_t *ctx,
                                   int8_t street)
{
    int depth;
    if (street >= 0 && street < PE_SAMPLING_STREET_COUNT)
        return (int)street;
    depth = ctx->chance_depth < 3 ? ctx->chance_depth : 3;
    return PE_SAMPLING_STREET_COUNT + depth;
}

const char *pe_adaptive_group_name(int group)
{
    static const char *const names[PE_ADAPTIVE_GROUP_COUNT] = {
        "street:preflop", "street:flop", "street:turn", "street:river",
        "chance-depth:0", "chance-depth:1", "chance-depth:2",
        "chance-depth:3+"};
    if (group < 0 || group >= PE_ADAPTIVE_GROUP_COUNT)
        return "unknown";
    return names[group];
}

/* The plan for one chance visit: how many draws, and the weight each draw's
 * reach carries. Decided before anything is drawn. */
typedef struct
{
    uint16_t replicates;
    double scale;   /* 1 for the fixed policies, 1/R for adaptive */
    int group;      /* adaptive group, -1 when not adaptive */
    int capped;     /* the maximum (per visit or per trajectory) bound R */
} external_chance_plan_t;

static external_chance_plan_t external_plan(const pe_external_sampling_ctx_t *ctx,
                                            int8_t street)
{
    external_chance_plan_t plan;
    plan.group = -1;
    plan.scale = 1.0;
    plan.capped = 0;
    plan.replicates = external_replicates(ctx, street);
    if (ctx->policy == PE_SAMPLING_ADAPTIVE_VARIANCE &&
        ctx->updating_player >= 0 &&
        ctx->updating_player < (int)PE_TRAVERSAL_MAX_PLAYERS)
    {
        uint32_t budget;
        plan.group = external_adaptive_group(ctx, street);
        /* Only statistics of earlier, completed visits decide this: the
           draws below cannot influence how many of them are made. */
        budget = ctx->adaptive_budget[ctx->updating_player][plan.group];
        if (budget == 0u)
            budget = ctx->adaptive.min_samples ? ctx->adaptive.min_samples : 1u;
        /* Nested chance visits multiply: cap the trajectory's product. The
           cap depends only on the replicate counts above, fixed before any
           of this visit's draws, so it keeps R independent of them. */
        {
            uint64_t product = ctx->replicate_product ? ctx->replicate_product : 1u;
            uint64_t allowed = ctx->adaptive.max_samples
                                   ? (uint64_t)ctx->adaptive.max_samples / product
                                   : 1u;
            if (allowed < 1u)
                allowed = 1u;
            if ((uint64_t)budget >= allowed || budget >= ctx->adaptive.max_samples)
                plan.capped = 1;
            if ((uint64_t)budget > allowed)
                budget = (uint32_t)allowed;
        }
        if (budget > UINT16_MAX)
            budget = UINT16_MAX;
        plan.replicates = (uint16_t)budget;
        plan.scale = 1.0 / (double)plan.replicates;
    }
    return plan;
}

/* After an adaptive visit: fold its draws into the group's statistics and,
 * every check_interval visits, recompute the budget for the visits to come. */
static void external_adaptive_observe(pe_external_sampling_ctx_t *ctx,
                                      const external_chance_plan_t *plan,
                                      const pe_online_stats_t *draws)
{
    const int p = ctx->updating_player;
    const int g = plan->group;
    pe_adaptive_group_stats_t *stats;
    if (g < 0)
        return;
    stats = &ctx->adaptive_stats[g];

    pe_pooled_variance_add(&ctx->adaptive_within[p][g], draws);
    pe_online_stats_add(&ctx->adaptive_means[p][g], draws->mean);

    stats->estimates++;
    stats->samples += plan->replicates;
    if (plan->replicates <= ctx->adaptive.min_samples)
        stats->min_hits++;
    if (plan->capped)
        stats->max_hits++;
    if (pe_online_stats_resolved(draws, ctx->adaptive_z,
                                 ctx->adaptive.absolute_tolerance,
                                 ctx->adaptive.relative_tolerance))
        stats->resolved++;
    pe_pooled_variance_add(&stats->within, draws);
    pe_online_stats_add(&stats->means, draws->mean);

    if (ctx->adaptive_means[p][g].n >= ctx->adaptive_next_check[p][g])
    {
        ctx->adaptive_budget[p][g] = pe_adaptive_sampling_budget(
            &ctx->adaptive, ctx->adaptive_z,
            pe_pooled_variance_value(&ctx->adaptive_within[p][g]),
            ctx->adaptive_means[p][g].mean);
        ctx->adaptive_next_check[p][g] += ctx->adaptive.check_interval;
    }
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
        ctx->total_terminal_nodes++;
        return game->terminal_value(state, ctx->updating_player, game->user);
    }

    if (game->sample_chance_child && game->acting_player(state, game->user) < 0)
    {
        pe_chance_sample_t sample;
        const void *child;
        uint16_t replicates;
        external_chance_plan_t plan;
        pe_online_stats_t draws;
        double total = 0.0;
        memset(&sample, 0, sizeof(sample));
        sample.street = PE_STREET_UNKNOWN;
        pe_online_stats_reset(&draws);
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
         * `replicates` times more useful work per visit.  Issue #256: the
         * adaptive policy weights each replicate 1/R instead (plan.scale). */
        plan = external_plan(ctx, sample.street);
        replicates = plan.replicates;
        for (uint16_t r = 0; r < replicates; ++r)
        {
            double own = own_reach * sample.importance_ratio * plan.scale;
            double opponent = opponent_reach * sample.importance_ratio * plan.scale;
            int street_index = external_street_index(sample.street);
            double value;
            if (street_index >= 0)
                ctx->chance_samples_by_street[street_index]++;
            ctx->chance_depth++;
            ctx->replicate_product *= replicates;
            value = external_visit(ctx, child, own, opponent, batch);
            ctx->replicate_product /= replicates;
            ctx->chance_depth--;
            if (game->release_state) game->release_state(child, game->user);
            total += sample.importance_ratio * value;
            pe_online_stats_add(&draws, sample.importance_ratio * value);
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
        external_adaptive_observe(ctx, &plan, &draws);
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
            external_chance_plan_t plan;
            pe_online_stats_t draws;
            double total = 0.0;
            if (sample.outcome < 0 || !pe_finite_double(sample.importance_ratio) ||
                sample.importance_ratio < 0.0)
                return NAN;
            pe_online_stats_reset(&draws);
            plan = external_plan(ctx, sample.street);
            replicates = plan.replicates;
            for (uint16_t r = 0; r < replicates; ++r)
            {
                double own = own_reach * sample.importance_ratio * plan.scale;
                double opponent = opponent_reach * sample.importance_ratio * plan.scale;
                int street_index = external_street_index(sample.street);
                double value;
                child = game->apply_chance(state, sample.outcome, game->user);
                if (!child)
                    return NAN;
                ctx->sampled_chance_nodes++;
                if (street_index >= 0)
                    ctx->chance_samples_by_street[street_index]++;
                ctx->chance_depth++;
                ctx->replicate_product *= replicates;
                value = external_visit(ctx, child, own, opponent, batch);
                ctx->replicate_product /= replicates;
                ctx->chance_depth--;
                if (game->release_state) game->release_state(child, game->user);
                total += sample.importance_ratio * value;
                pe_online_stats_add(&draws, sample.importance_ratio * value);
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
            external_adaptive_observe(ctx, &plan, &draws);
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
    if (policy == PE_SAMPLING_ADAPTIVE_VARIANCE)
        (void)pe_external_sampling_set_adaptive(ctx, NULL);
}

int pe_external_sampling_set_adaptive(pe_external_sampling_ctx_t *ctx,
                                      const pe_adaptive_sampling_t *settings)
{
    pe_adaptive_sampling_t resolved;
    double z = 0.0;
    if (!ctx || pe_adaptive_sampling_resolve(settings, &resolved, &z) != 0)
        return -1;
    ctx->adaptive = resolved;
    ctx->adaptive_z = z;
    memset(ctx->adaptive_means, 0, sizeof(ctx->adaptive_means));
    memset(ctx->adaptive_within, 0, sizeof(ctx->adaptive_within));
    memset(ctx->adaptive_stats, 0, sizeof(ctx->adaptive_stats));
    for (size_t p = 0; p < PE_TRAVERSAL_MAX_PLAYERS; ++p)
        for (int g = 0; g < PE_ADAPTIVE_GROUP_COUNT; ++g)
        {
            /* Until a group has check_interval visits behind it, it draws the
               minimum: the pilot that measures its spread. */
            ctx->adaptive_budget[p][g] = resolved.min_samples;
            ctx->adaptive_next_check[p][g] = resolved.check_interval;
        }
    return 0;
}

void pe_external_sampling_get_adaptive_state(const pe_external_sampling_ctx_t *ctx,
                                             pe_adaptive_state_t *out)
{
    if (!ctx || !out)
        return;
    memcpy(out->budget, ctx->adaptive_budget, sizeof(out->budget));
    memcpy(out->next_check, ctx->adaptive_next_check, sizeof(out->next_check));
    memcpy(out->means, ctx->adaptive_means, sizeof(out->means));
    memcpy(out->within, ctx->adaptive_within, sizeof(out->within));
}

void pe_external_sampling_set_adaptive_state(pe_external_sampling_ctx_t *ctx,
                                             const pe_adaptive_state_t *state)
{
    if (!ctx || !state)
        return;
    memcpy(ctx->adaptive_budget, state->budget, sizeof(ctx->adaptive_budget));
    memcpy(ctx->adaptive_next_check, state->next_check,
           sizeof(ctx->adaptive_next_check));
    memcpy(ctx->adaptive_means, state->means, sizeof(ctx->adaptive_means));
    memcpy(ctx->adaptive_within, state->within, sizeof(ctx->adaptive_within));
}

static const unsigned char k_adaptive_tag[8] = {'P', 'E', 'A', 'D', 'A', 'P', 'T', '1'};

static unsigned char *put_bytes(unsigned char *p, const void *v, size_t n)
{
    memcpy(p, v, n);
    return p + n;
}

static const unsigned char *get_bytes(const unsigned char *p, void *v, size_t n)
{
    memcpy(v, p, n);
    return p + n;
}

size_t pe_adaptive_state_serialize(const pe_adaptive_state_t *state,
                                   unsigned char *out, size_t capacity)
{
    unsigned char *p = out;
    if (!state || !out || capacity < PE_ADAPTIVE_STATE_BYTES)
        return 0u;
    p = put_bytes(p, k_adaptive_tag, sizeof(k_adaptive_tag));
    for (size_t pl = 0; pl < PE_TRAVERSAL_MAX_PLAYERS; ++pl)
        for (int g = 0; g < PE_ADAPTIVE_GROUP_COUNT; ++g)
        {
            p = put_bytes(p, &state->budget[pl][g], 4u);
            p = put_bytes(p, &state->next_check[pl][g], 8u);
            p = put_bytes(p, &state->means[pl][g].n, 8u);
            p = put_bytes(p, &state->means[pl][g].mean, 8u);
            p = put_bytes(p, &state->means[pl][g].m2, 8u);
            p = put_bytes(p, &state->within[pl][g].dof, 8u);
            p = put_bytes(p, &state->within[pl][g].ssd, 8u);
        }
    return (size_t)(p - out);
}

int pe_adaptive_state_deserialize(pe_adaptive_state_t *state,
                                  const unsigned char *in, size_t size)
{
    const unsigned char *p = in;
    pe_adaptive_state_t s;
    unsigned char tag[8];
    if (!state || !in || size != PE_ADAPTIVE_STATE_BYTES)
        return -1;
    p = get_bytes(p, tag, sizeof(tag));
    if (memcmp(tag, k_adaptive_tag, sizeof(tag)) != 0)
        return -1;
    for (size_t pl = 0; pl < PE_TRAVERSAL_MAX_PLAYERS; ++pl)
        for (int g = 0; g < PE_ADAPTIVE_GROUP_COUNT; ++g)
        {
            p = get_bytes(p, &s.budget[pl][g], 4u);
            p = get_bytes(p, &s.next_check[pl][g], 8u);
            p = get_bytes(p, &s.means[pl][g].n, 8u);
            p = get_bytes(p, &s.means[pl][g].mean, 8u);
            p = get_bytes(p, &s.means[pl][g].m2, 8u);
            p = get_bytes(p, &s.within[pl][g].dof, 8u);
            p = get_bytes(p, &s.within[pl][g].ssd, 8u);
            if (!pe_finite_double(s.means[pl][g].mean) ||
                !pe_finite_double(s.means[pl][g].m2) ||
                !pe_finite_double(s.within[pl][g].ssd))
                return -1;
        }
    *state = s;
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
    ctx->replicate_product = 1u;
    ctx->chance_depth = 0;
    ctx->visited_nodes = 0u;
    ctx->terminal_nodes = 0u;
    ctx->sampled_chance_nodes = 0u;
    value = external_visit(ctx, ctx->game->root, 1.0, 1.0, out_batch);
    (void)value;
    return pe_finite_double(value) ? 0 : -1;
}
