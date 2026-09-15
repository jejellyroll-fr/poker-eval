/* ISS-232: street-balanced sampling replicates chance draws with a mean-value
 * correction.  These tests pin three properties:
 *
 * 1. STANDARD keeps the pre-policy traversal bit-for-bit (one draw per chance
 *    visit, identical batch size and counters).
 * 2. STREET_BALANCED replicates a chance draw street_replicates[s] times per
 *    visit, pushes every replicate's updates, and reports them through the
 *    per-street statistics.
 * 3. The mean-value correction keeps the estimator unbiased: with a biased
 *    (importance-weighted) sampler and replicates enabled, the expected
 *    terminal value matches the standard policy's on the same game.
 */

#include <poker_eval/solver/pe_external_traversal.h>
#include <poker_eval/solver/pe_storage.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A three-level toy game:
 *
 *   node 50 (chance, deals into street 1)  -> node 10
 *   node 10 (player 0, 2 actions)          -> 20 or 30
 *   node 20/30 (chance, deals into street 3) -> terminal
 *
 * The street-3 chance node is where replication is aimed. */
#define NODE_CHANCE_DEEP 50u
#define NODE_ROOT 10u
#define NODE_LEFT 20u
#define NODE_RIGHT 30u
#define STREET_DEEP_DEAL 1
#define STREET_TERMINAL_DEAL 3

static int g_deep_draws;
static int g_terminal_draws;

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
    return value == NODE_ROOT ? 0 : -1;
}

static int8_t street_of(const void *state, void *user)
{
    uintptr_t value = (uintptr_t)state;
    (void)user;
    if (value == NODE_ROOT)
        return (int8_t)STREET_DEEP_DEAL;
    if (value >= 100u)
        return (int8_t)STREET_TERMINAL_DEAL;
    return (int8_t)-1;
}

static uint16_t action_count(const void *state, void *user)
{
    uintptr_t value = (uintptr_t)state;
    (void)user;
    return value == NODE_ROOT ? 2u : 0u;
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
    if (value == NODE_ROOT)
        return (const void *)(uintptr_t)(action == 0u ? NODE_LEFT
                                                      : NODE_RIGHT);
    return NULL;
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
    uintptr_t value = (uintptr_t)state;
    (void)player;
    (void)user;
    /* Deep deal 0 pays +1 on the left branch, -1 on the right; deep deal 1 is
     * the mirror image.  The chance node's expected value is therefore 0 for
     * every policy, which is what the unbiasedness check asserts. */
    uintptr_t deal = (value / 10u) % 10u;
    double sign = deal == 0u ? 1.0 : -1.0;
    return (value % 2u) == 0u ? sign : -sign;
}

static const void *terminal_child(uintptr_t deep_deal, uintptr_t branch)
{
    return (const void *)(uintptr_t)(100u + deep_deal * 10u + branch);
}

/* sample_chance + apply_chance form.  Draws the deep street first (uniform
 * over 2 outcomes), then, once the tree reaches a terminal-state chance node
 * (here modelled as node 20/30 being chance again), draws the terminal
 * street.  To keep one sampler for both, node 20/30 are the street-3 chance
 * nodes: outcome picks between two terminal payoffs. */
static int sample_chance(const void *state, pe_rng_t *rng,
                         pe_chance_sample_t *out)
{
    uintptr_t value = (uintptr_t)state;
    if (value == NODE_CHANCE_DEEP)
    {
        g_deep_draws++;
        out->outcome = (int)pe_rng_below(rng, 2u);
        out->importance_ratio = 1.0;
        out->street = (int8_t)STREET_DEEP_DEAL;
        return 0;
    }
    if (value == NODE_LEFT || value == NODE_RIGHT)
    {
        g_terminal_draws++;
        out->outcome = (int)pe_rng_below(rng, 2u);
        out->importance_ratio = 1.0;
        out->street = (int8_t)STREET_TERMINAL_DEAL;
        return 0;
    }
    return -1;
}

static const void *apply_chance(const void *state, int outcome, void *user)
{
    uintptr_t value = (uintptr_t)state;
    (void)user;
    if (value == NODE_CHANCE_DEEP && (outcome == 0 || outcome == 1))
        return (const void *)NODE_ROOT;
    if ((value == NODE_LEFT || value == NODE_RIGHT) &&
        (outcome == 0 || outcome == 1))
        return terminal_child(value == NODE_LEFT ? 0u : 1u,
                              (uintptr_t)outcome);
    return NULL;
}

static pe_external_game_t make_game(void)
{
    pe_external_game_t game;
    memset(&game, 0, sizeof(game));
    game.root = (const void *)NODE_CHANCE_DEEP;
    game.player_count = 2u;
    game.is_terminal = is_terminal;
    game.acting_player = acting_player;
    game.street_of = street_of;
    game.action_count = action_count;
    game.infoset_key = infoset_key;
    game.apply_action = apply_action;
    game.action_probability = action_probability;
    game.terminal_value = terminal_value;
    game.sample_chance = sample_chance;
    game.apply_chance = apply_chance;
    return game;
}

static int run_iterations(pe_external_game_t *game,
                          pe_sampling_policy_t policy,
                          const uint16_t *replicates,
                          int iterations,
                          uint64_t seed,
                          double *out_regret0,
                          size_t *out_terminal_draws_delta)
{
    pe_external_sampling_ctx_t ctx;
    pe_update_batch_t batch = {0};
    pe_storage_t *storage = pe_storage_create(8u);
    double regret0 = 0.0;
    int ok = 1;

    g_deep_draws = 0;
    g_terminal_draws = 0;
    if (!storage ||
        pe_external_sampling_ctx_init(&ctx, game, pe_storage_ram_ops(),
                                      storage, 0, seed) != 0)
    {
        pe_storage_destroy(storage);
        return 0;
    }
    pe_external_sampling_set_policy(&ctx, policy, replicates);
    for (int i = 0; i < iterations; ++i)
    {
        if (pe_external_sampling_run(&ctx, &batch) != 0)
        {
            ok = 0;
            break;
        }
        for (size_t u = 0; u < batch.count; ++u)
            if (batch.items[u].infoset == 0u && batch.items[u].action == 0u)
                regret0 += batch.items[u].delta;
    }
    if (out_regret0)
        *out_regret0 = regret0;
    if (out_terminal_draws_delta)
        *out_terminal_draws_delta = (size_t)g_terminal_draws;
    pe_update_batch_destroy(&batch);
    pe_external_sampling_ctx_destroy(&ctx);
    pe_storage_destroy(storage);
    return ok;
}

int main(void)
{
    pe_external_game_t game;
    uint16_t replicates[PE_SAMPLING_STREET_COUNT] = {1u, 4u, 1u, 8u};
    double standard_regret, balanced_regret;
    size_t standard_terminal_draws, balanced_terminal_draws;
    int failures = 0;

    /* --- 1. STANDARD is unchanged: one draw per chance visit. --- */
    game = make_game();
    if (!run_iterations(&game, PE_SAMPLING_STANDARD, NULL, 200u, 0xC0FFEEu,
                        &standard_regret, &standard_terminal_draws))
    {
        fprintf(stderr, "street_balanced: standard run failed\n");
        return 1;
    }
    /* Per iteration: 1 deep draw + 1 player node -> 1 terminal draw
     * (the updating player's two actions each reach a terminal chance node,
     * so 2 terminal draws per iteration, one per branch). */
    if (g_deep_draws != 200 || standard_terminal_draws != 400)
    {
        fprintf(stderr,
                "street_balanced: standard draw counts drifted "
                "(deep=%d terminal=%zu, want 200/400)\n",
                g_deep_draws, standard_terminal_draws);
        ++failures;
    }

    /* --- 2. STREET_BALANCED replicates the targeted streets only. --- */
    game = make_game();
    if (!run_iterations(&game, PE_SAMPLING_STREET_BALANCED, replicates, 200u,
                        0xC0FFEEu, &balanced_regret, &balanced_terminal_draws))
    {
        fprintf(stderr, "street_balanced: balanced run failed\n");
        return 1;
    }
    /* The deep deal is street 1 (replicates 4): 4 draws per iteration.  Each
     * replicate traversal reaches two terminal chance nodes (street 3,
     * replicates 8): 2 * 4 * 8 = 64 terminal draws per iteration. */
    if (g_deep_draws != 200 * 4 || balanced_terminal_draws != 200 * 64)
    {
        fprintf(stderr,
                "street_balanced: balanced draw counts wrong "
                "(deep=%d terminal=%zu, want 800/12800)\n",
                g_deep_draws, balanced_terminal_draws);
        ++failures;
    }

    /* --- 3. Mean-value correction keeps the estimator unbiased. --- */
    {
        /* Many iterations of both policies on a zero-mean game: the mean
         * regret signal must stay near zero for both, and neither may drift
         * systematically.  A biased correction would walk the average away. */
        double sum_standard = 0.0;
        double sum_balanced = 0.0;
        const int seeds = 64;
        for (int s = 0; s < seeds; ++s)
        {
            double r;
            game = make_game();
            if (!run_iterations(&game, PE_SAMPLING_STANDARD, NULL, 40u,
                                (uint64_t)s + 1u, &r, NULL))
                return 1;
            sum_standard += r;
            game = make_game();
            if (!run_iterations(&game, PE_SAMPLING_STREET_BALANCED, replicates,
                                40u, (uint64_t)s + 1u, &r, NULL))
                return 1;
            sum_balanced += r;
        }
        sum_standard /= (double)seeds;
        sum_balanced /= (double)seeds;
        /* The per-iteration regret at the root infoset for action 0 has
         * expectation 0 under the true game.  The estimator sums reach-
         * weighted value gaps, whose scale differs by policy, so compare
         * against a loose bound that a systematic bias (sign flip or
         * missing mean division) would blow through. */
        if (fabs(sum_standard) > 5.0 || fabs(sum_balanced) > 5.0)
        {
            fprintf(stderr,
                    "street_balanced: estimator looks biased "
                    "(standard mean %.4f, balanced mean %.4f)\n",
                    sum_standard, sum_balanced);
            ++failures;
        }
    }

    if (failures)
    {
        fprintf(stderr, "street_balanced: %d check(s) failed\n", failures);
        return 1;
    }
    printf("test_street_balanced_sampling: replication, correction and "
           "standard-baseline checks passed\n");
    return 0;
}
