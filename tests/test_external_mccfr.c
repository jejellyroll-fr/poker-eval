/* LNB-01: external sampling explores every action of the updating player,
 * samples opponents/chance, and returns regret deltas for an explicit player. */

#include <poker_eval/solver/pe_external_traversal.h>
#include <poker_eval/solver/pe_storage.h>

#include <math.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

static int g_chance_samples;
static int g_opponent_samples;

static int is_terminal(const void *state, void *user)
{
    uintptr_t value = (uintptr_t)state;
    (void)user;
    return value >= 100u;
}

static int acting_player(const void *state, void *user)
{
    uintptr_t value = (uintptr_t)state;
    (void)user;
    if (value == 10u)
        return 0;
    if (value == 20u || value == 30u)
        return 1;
    return -1;
}

static uint16_t action_count(const void *state, void *user)
{
    uintptr_t value = (uintptr_t)state;
    (void)user;
    return (value == 10u || value == 20u || value == 30u) ? 2u : 0u;
}

static uint64_t infoset_key(const void *state, void *user)
{
    (void)user;
    return (uint64_t)(uintptr_t)state;
}

static const void *apply_action(const void *state, uint16_t action, void *user)
{
    uintptr_t value = (uintptr_t)state;
    (void)user;
    if (value == 10u)
        return (const void *)(uintptr_t)(action == 0u ? 20u : 30u);
    if (value == 20u)
        return (const void *)(uintptr_t)(100u + action);
    if (value == 30u)
        return (const void *)(uintptr_t)(110u + action);
    return NULL;
}

static double action_probability(const void *state, uint64_t key,
                                 uint16_t action, void *user)
{
    const double *regrets = (const double *)user;
    uintptr_t value = (uintptr_t)state;
    (void)key;
    if (value == 20u || value == 30u)
    {
        g_opponent_samples++;
        return 0.5;
    }
    if (value == 10u)
    {
        double positive0 = regrets[0] > 0.0 ? regrets[0] : 0.0;
        double positive1 = regrets[1] > 0.0 ? regrets[1] : 0.0;
        double total = positive0 + positive1;
        if (total > 0.0)
            return action == 0u ? positive0 : positive1;
    }
    return 0.5;
}

static double terminal_value(const void *state, int player, void *user)
{
    uintptr_t value = (uintptr_t)state;
    (void)user;
    if (player != 0)
        return 0.0;
    return value < 110u ? 1.0 : -1.0;
}

static int sample_chance(const void *state, pe_rng_t *rng,
                         pe_chance_sample_t *out)
{
    if ((uintptr_t)state != 50u)
        return -1;
    out->outcome = (int)pe_rng_below(rng, 2u);
    out->importance_ratio = 1.0;
    g_chance_samples++;
    return 0;
}

static const void *apply_chance(const void *state, int outcome, void *user)
{
    (void)user;
    if ((uintptr_t)state != 50u || outcome < 0 || outcome > 1)
        return NULL;
    return (const void *)(uintptr_t)10u;
}

int main(void)
{
    double regrets[2] = {0.0, 0.0};
    pe_external_game_t game = {0};
    pe_external_sampling_ctx_t ctx;
    pe_update_batch_t batch = {0};
    pe_storage_t *storage = pe_storage_create(4u);

    game.root = (const void *)(uintptr_t)50u;
    game.user = regrets;
    game.player_count = 2u;
    game.is_terminal = is_terminal;
    game.acting_player = acting_player;
    game.action_count = action_count;
    game.infoset_key = infoset_key;
    game.apply_action = apply_action;
    game.action_probability = action_probability;
    game.terminal_value = terminal_value;
    game.sample_chance = sample_chance;
    game.apply_chance = apply_chance;

    if (!storage || pe_external_sampling_ctx_init(&ctx, &game,
            pe_storage_ram_ops(), storage, 0, 0x1234u) != 0)
    {
        fprintf(stderr, "test_external_mccfr: context setup failed\n");
        pe_storage_destroy(storage);
        return 1;
    }

    for (int iteration = 0; iteration < 1000; ++iteration)
    {
        if (pe_external_sampling_run(&ctx, &batch) != 0 || batch.count != 2u)
        {
            fprintf(stderr, "test_external_mccfr: invalid iteration batch\n");
            pe_update_batch_destroy(&batch);
            pe_external_sampling_ctx_destroy(&ctx);
            pe_storage_destroy(storage);
            return 1;
        }
        if (ctx.sampled_chance_nodes != 1u)
        {
            fprintf(stderr, "test_external_mccfr: chance node was not sampled\n");
            pe_update_batch_destroy(&batch);
            pe_external_sampling_ctx_destroy(&ctx);
            pe_storage_destroy(storage);
            return 1;
        }
        for (size_t i = 0; i < batch.count; ++i)
            regrets[batch.items[i].action] += batch.items[i].delta;
        if (batch.items[0].average_delta + batch.items[1].average_delta < 0.99 ||
            batch.items[0].average_delta + batch.items[1].average_delta > 1.01)
        {
            fprintf(stderr, "test_external_mccfr: average reach was not conserved\n");
            pe_update_batch_destroy(&batch);
            pe_external_sampling_ctx_destroy(&ctx);
            pe_storage_destroy(storage);
            return 1;
        }
    }

    if (g_chance_samples != 1000 ||
        ctx.visited_nodes == 0u || g_opponent_samples == 0 ||
        regrets[0] <= 0.0 || regrets[1] >= 0.0)
    {
        fprintf(stderr,
                "test_external_mccfr: sampling or regret update failed "
                "(chance=%d/%" PRIu64 ", visited=%" PRIu64 ", opponent=%d, "
                "R=%.3f/%.3f)\n",
                g_chance_samples, ctx.sampled_chance_nodes, ctx.visited_nodes,
                g_opponent_samples, regrets[0], regrets[1]);
        pe_update_batch_destroy(&batch);
        pe_external_sampling_ctx_destroy(&ctx);
        pe_storage_destroy(storage);
        return 1;
    }

    printf("test_external_mccfr: explicit-player external sampling passed (R %.3f/%.3f)\n",
           regrets[0], regrets[1]);
    pe_update_batch_destroy(&batch);
    pe_external_sampling_ctx_destroy(&ctx);
    pe_storage_destroy(storage);
    return 0;
}
