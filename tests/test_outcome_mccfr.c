/* OUT-01: one trajectory per iteration with epsilon exploration. */

#include <poker_eval/solver/pe_outcome_traversal.h>
#include <poker_eval/solver/pe_storage.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>

static int is_terminal(const void *state, void *user)
{
    (void)user;
    return (uintptr_t)state >= 100u;
}

static int acting_player(const void *state, void *user)
{
    (void)state;
    (void)user;
    return 0;
}

static uint16_t action_count(const void *state, void *user)
{
    (void)user;
    return (uintptr_t)state == 10u ? 2u : 0u;
}

static uint64_t infoset_key(const void *state, void *user)
{
    (void)user;
    return (uint64_t)(uintptr_t)state;
}

static const void *apply_action(const void *state, uint16_t action, void *user)
{
    (void)user;
    if ((uintptr_t)state != 10u || action > 1u)
        return NULL;
    return (const void *)(uintptr_t)(action == 0u ? 100u : 110u);
}

static double action_probability(const void *state, uint64_t key,
                                 uint16_t action, void *user)
{
    (void)state;
    (void)key;
    (void)action;
    (void)user;
    return 0.5;
}

static double terminal_value(const void *state, int player, void *user)
{
    (void)user;
    if (player != 0)
        return 0.0;
    return (uintptr_t)state < 110u ? 1.0 : -1.0;
}

static int sample_chance(const void *state, pe_rng_t *rng,
                         pe_chance_sample_t *out)
{
    (void)state;
    (void)rng;
    (void)out;
    return 1;
}

int main(void)
{
    pe_external_game_t game = {0};
    pe_outcome_sampling_ctx_t ctx;
    pe_update_batch_t batch = {0};
    pe_storage_t *storage = pe_storage_create(2u);
    double regrets[2] = {0.0, 0.0};

    game.root = (const void *)(uintptr_t)10u;
    game.player_count = 1u;
    game.is_terminal = is_terminal;
    game.acting_player = acting_player;
    game.action_count = action_count;
    game.infoset_key = infoset_key;
    game.apply_action = apply_action;
    game.action_probability = action_probability;
    game.terminal_value = terminal_value;
    game.sample_chance = sample_chance;

    if (!storage || pe_outcome_sampling_ctx_init(
            &ctx, &game, pe_storage_ram_ops(), storage, 0, 0.6, 0x4321u) != 0)
    {
        fprintf(stderr, "test_outcome_mccfr: context setup failed\n");
        pe_storage_destroy(storage);
        return 1;
    }
    for (int iteration = 0; iteration < 10000; ++iteration)
    {
        if (pe_outcome_sampling_run(&ctx, &batch) != 0 || batch.count != 2u ||
            ctx.sampled_action_nodes != 1u)
        {
            fprintf(stderr, "test_outcome_mccfr: invalid trajectory\n");
            pe_update_batch_destroy(&batch);
            pe_outcome_sampling_ctx_destroy(&ctx);
            pe_storage_destroy(storage);
            return 1;
        }
        if (fabs(fabs(batch.items[0].delta) - 1.0) > 1e-12 ||
            fabs(fabs(batch.items[1].delta) - 1.0) > 1e-12)
        {
            fprintf(stderr, "test_outcome_mccfr: regret estimator kept action probability\n");
            pe_update_batch_destroy(&batch);
            pe_outcome_sampling_ctx_destroy(&ctx);
            pe_storage_destroy(storage);
            return 1;
        }
        regrets[0] += batch.items[0].delta;
        regrets[1] += batch.items[1].delta;
        if (fabs(batch.items[0].average_delta +
                 batch.items[1].average_delta - 1.0) > 1e-12)
        {
            fprintf(stderr, "test_outcome_mccfr: average weight not conserved\n");
            pe_update_batch_destroy(&batch);
            pe_outcome_sampling_ctx_destroy(&ctx);
            pe_storage_destroy(storage);
            return 1;
        }
    }
    if (regrets[0] <= 100.0 || regrets[1] >= -100.0 ||
        ctx.visited_nodes == 0u)
    {
        fprintf(stderr, "test_outcome_mccfr: estimator did not separate actions "
                        "(R %.3f/%.3f)\n", regrets[0], regrets[1]);
        pe_update_batch_destroy(&batch);
        pe_outcome_sampling_ctx_destroy(&ctx);
        pe_storage_destroy(storage);
        return 1;
    }
    printf("test_outcome_mccfr: epsilon outcome sampling passed (R %.3f/%.3f)\n",
           regrets[0], regrets[1]);
    pe_update_batch_destroy(&batch);
    pe_outcome_sampling_ctx_destroy(&ctx);
    pe_storage_destroy(storage);
    return 0;
}
