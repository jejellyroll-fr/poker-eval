#include <poker_eval/solver/pe_external_best_response.h>

#include <math.h>
#include <string.h>

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
        if (!isfinite(p) || p < 0.0) return -1;
        probabilities[a] = p;
        sum += p;
    }
    if (!(sum > 0.0) || !isfinite(sum)) return -1;
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
            if (!isfinite(value)) return NAN;
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
    pe_external_br_config_t config = {256u, 128u, 1u};
    return config;
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
        if (isfinite(value)) { policy += value; ++out->policy_samples; }
        value = br_rollout(&ctx, game->root, 0u);
        if (isfinite(value)) { br += value; ++out->br_samples; }
    }
    if (out->policy_samples == 0u || out->br_samples == 0u) return -1;
    out->policy_value = policy / (double)out->policy_samples;
    out->br_value = br / (double)out->br_samples;
    out->br_gap = out->br_value - out->policy_value;
    if (out->br_gap < 0.0) out->br_gap = 0.0;
    out->empirical = 1;
    return 0;
}
