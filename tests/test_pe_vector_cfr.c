/*
 * test_pe_vector_cfr.c - VEC-03: per-combo regret matching
 */

#include <poker_eval/solver/pe_regret.h>
#include <poker_eval/solver/pe_average.h>
#include <poker_eval/solver/pe_traversal.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond, ...)                                                   \
    do                                                                     \
    {                                                                      \
        if (!(cond))                                                       \
        {                                                                  \
            fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__);       \
            fprintf(stderr, __VA_ARGS__);                                 \
            fputc('\n', stderr);                                           \
            failures++;                                                    \
        }                                                                  \
    } while (0)

static void test_per_combo_normalization(void)
{
    /* Three actions, four combos; each column has a different positive mass. */
    const double regrets[] = {
         2.0, -1.0,  0.0,  4.0,
        -1.0,  3.0,  0.0, -2.0,
         1.0, -2.0,  0.0,  2.0
    };
    double strategy[sizeof(regrets) / sizeof(regrets[0])];
    uint16_t action;
    uint16_t combo;

    CHECK(pe_regret_match_vector(regrets, strategy, 3u, 4u) == 0,
          "vector regret matching failed");
    for (combo = 0; combo < 4u; ++combo)
    {
        double sum = 0.0;
        for (action = 0; action < 3u; ++action)
            sum += strategy[(size_t)action * 4u + combo];
        CHECK(fabs(sum - 1.0) <= 1e-12,
              "combo %u sums to %.17g, expected 1", combo, sum);
    }

    /* combo 0: 2/(2+1), 0, 1/(2+1). */
    CHECK(fabs(strategy[0] - 2.0 / 3.0) <= 1e-12,
          "combo 0 action 0 is %.17g", strategy[0]);
    CHECK(strategy[4] == 0.0 && fabs(strategy[8] - 1.0 / 3.0) <= 1e-12,
          "combo 0 was not normalized from positive regrets");
    /* combo 1: only action 1 is positive. */
    CHECK(strategy[1] == 0.0 && strategy[5] == 1.0 && strategy[9] == 0.0,
          "combo 1 did not select its sole positive regret");
}

static void test_uniform_fallback_and_invalid_inputs(void)
{
    const double regrets[] = {-3.0, -2.0, -1.0, 0.0, -4.0, -5.0};
    double strategy[6];
    size_t i;

    memset(strategy, 0, sizeof(strategy));
    CHECK(pe_regret_match_vector(regrets, strategy, 3u, 2u) == 0,
          "uniform fallback failed");
    for (i = 0; i < 6u; ++i)
        CHECK(fabs(strategy[i] - 1.0 / 3.0) <= 1e-12,
              "uniform fallback at %zu is %.17g", i, strategy[i]);

    CHECK(pe_regret_match_vector(NULL, strategy, 3u, 2u) != 0,
          "NULL regrets accepted");
    CHECK(pe_regret_match_vector(regrets, NULL, 3u, 2u) != 0,
          "NULL strategy accepted");
    CHECK(pe_regret_match_vector(regrets, strategy, 0u, 2u) != 0,
          "zero actions accepted");
    CHECK(pe_regret_match_vector(regrets, strategy, 3u, 0u) != 0,
          "zero combos accepted");
}

static void test_reach_weighted_average(void)
{
    const double reach[] = {1.0, 0.5, 2.0};
    const double strategy_a[] = {
        0.25, 0.75, 0.40,
        0.75, 0.25, 0.60
    };
    const double strategy_b[] = {
        0.75, 0.25, 0.80,
        0.25, 0.75, 0.20
    };
    double weighted[6] = {0};
    double normalizer[3] = {0};
    double average[6] = {0};
    uint16_t action;
    uint16_t combo;

    /* Three observations of A and one observation of B, with reach differing
       per combo. The reach must affect numerator and denominator equally, so
       the resulting strategy remains the weighted temporal average per combo. */
    CHECK(pe_average_accumulate_vector(weighted, normalizer, strategy_a,
                                       reach, 2u, 3u, 3.0) == 0,
          "first average update failed");
    CHECK(pe_average_accumulate_vector(weighted, normalizer, strategy_b,
                                       reach, 2u, 3u, 1.0) == 0,
          "second average update failed");
    CHECK(pe_average_finalize_vector(weighted, normalizer, average, 2u, 3u) == 0,
          "average finalization failed");

    for (combo = 0; combo < 3u; ++combo)
    {
        double expected = (3.0 * strategy_a[combo] + strategy_b[combo]) / 4.0;
        double sum = 0.0;
        for (action = 0; action < 2u; ++action)
            sum += average[(size_t)action * 3u + combo];
        CHECK(fabs(average[combo] - expected) <= 1e-12,
              "combo %u average is %.17g, expected %.17g",
              combo, average[combo], expected);
        CHECK(fabs(sum - 1.0) <= 1e-12,
              "combo %u average sums to %.17g", combo, sum);
        CHECK(fabs(normalizer[combo] - 4.0 * reach[combo]) <= 1e-12,
              "combo %u normalizer is %.17g, expected %.17g",
              combo, normalizer[combo], 4.0 * reach[combo]);
    }
}

static void test_average_uniform_fallback(void)
{
    const double weighted[] = {1.0, 2.0, 3.0, 4.0};
    const double normalizer[] = {0.0, 2.0};
    double out[4] = {0};
    size_t i;

    CHECK(pe_average_finalize_vector(weighted, normalizer, out, 2u, 2u) == 0,
          "zero-reach average finalization failed");
    CHECK(out[0] == 0.5 && out[2] == 0.5,
          "zero-reach combo did not fall back to uniform");
    CHECK(out[1] == 1.0 && out[3] == 2.0,
          "non-zero combo was normalized incorrectly");
    CHECK(pe_average_accumulate_vector(out, out, out, out, 0u, 2u, 1.0) != 0,
          "zero actions accepted by average update");
    for (i = 0; i < 4u; ++i)
        CHECK(!isnan(out[i]), "average produced NaN at %zu", i);
}

static void test_delayed_linear_average(void)
{
    const double strategy[] = {0.25, 0.75};
    const double reach[] = {1.0};
    double weighted[] = {0.0, 0.0};
    double normalizer[] = {0.0};

    CHECK(pe_average_accumulate_delayed_linear_vector(
              weighted, normalizer, strategy, reach, 2u, 1u, 100u, 100u) == 0,
          "delayed iteration should be accepted as a no-op");
    CHECK(weighted[0] == 0.0 && weighted[1] == 0.0 && normalizer[0] == 0.0,
          "averaging delay contributed too early");

    CHECK(pe_average_accumulate_delayed_linear_vector(
              weighted, normalizer, strategy, reach, 2u, 1u, 101u, 100u) == 0,
          "first post-delay iteration failed");
    CHECK(fabs(weighted[0] - 0.25) <= 1e-12 &&
              fabs(weighted[1] - 0.75) <= 1e-12 &&
              fabs(normalizer[0] - 1.0) <= 1e-12,
          "first post-delay contribution must have weight one");

    CHECK(pe_average_accumulate_delayed_linear_vector(
              weighted, normalizer, strategy, reach, 2u, 1u, 102u, 100u) == 0,
          "second post-delay iteration failed");
    CHECK(fabs(weighted[0] - 0.75) <= 1e-12 &&
              fabs(weighted[1] - 2.25) <= 1e-12 &&
              fabs(normalizer[0] - 3.0) <= 1e-12,
          "linear post-delay weight is incorrect");
    CHECK(pe_average_accumulate_delayed_linear_vector(
              weighted, normalizer, strategy, reach, 2u, 1u, 0u, 100u) != 0,
          "zero iteration must be rejected");
}

static void test_importance_weighted_average(void)
{
    const double strategy[] = {0.25, 0.75};
    const double reach[] = {2.0};
    double weighted[] = {0.0, 0.0};
    double normalizer[] = {0.0};
    double average[] = {0.0, 0.0};

    CHECK(pe_average_accumulate_importance_vector(
              weighted, normalizer, strategy, reach, 2u, 1u, 0.25, 1.0) == 0,
          "importance average update failed");
    CHECK(fabs(weighted[0] - 2.0) <= 1e-12 &&
              fabs(weighted[1] - 6.0) <= 1e-12 &&
              fabs(normalizer[0] - 8.0) <= 1e-12,
          "inverse sampling probability was not applied");
    CHECK(pe_average_finalize_vector(weighted, normalizer, average, 2u, 1u) == 0,
          "importance average finalization failed");
    CHECK(fabs(average[0] - 0.25) <= 1e-12 &&
              fabs(average[1] - 0.75) <= 1e-12,
          "importance correction changed the observed strategy");
    CHECK(pe_average_accumulate_importance_vector(
              weighted, normalizer, strategy, reach, 2u, 1u, 0.0, 1.0) != 0,
          "zero sampling probability must be rejected");
    CHECK(pe_average_accumulate_importance_vector(
              weighted, normalizer, strategy, reach, 2u, 1u, 1.1, 1.0) != 0,
          "sampling probability above one must be rejected");
}

typedef struct
{
    int chance;
    int outcome;
} chance_state_t;

static const chance_state_t chance_root = {1, -1};
static chance_state_t chance_outcomes[44];

static int chance_is_terminal(const void *state, void *user)
{
    (void)user;
    return !((const chance_state_t *)state)->chance;
}

static int chance_sample(const void *state, pe_rng_t *rng,
                         pe_chance_sample_t *out, void *user)
{
    uint32_t draw;
    (void)user;
    if (!((const chance_state_t *)state)->chance || !rng || !out)
        return 1;
    draw = pe_rng_below(rng, 88u);
    if (draw < 22u)
    {
        out->outcome = (int)draw;
        out->importance_ratio = 2.0;
    }
    else
    {
        out->outcome = 22 + (int)((draw - 22u) % 22u);
        out->importance_ratio = 2.0 / 3.0;
    }
    return 0;
}

static const void *chance_apply(const void *state, int outcome, void *user)
{
    (void)state;
    (void)user;
    return outcome >= 0 && outcome < 44 ? &chance_outcomes[outcome] : NULL;
}

static int chance_terminal_values(const void *state,
                                  const pe_reach_vec_t *reach,
                                  pe_value_vec_t *out_values,
                                  uint8_t player_count, void *user)
{
    const chance_state_t *terminal = (const chance_state_t *)state;
    size_t combo;
    (void)reach;
    (void)user;
    if (player_count != 2u)
        return -1;
    for (combo = 0; combo < out_values[0].n; ++combo)
    {
        out_values[0].v[combo] = (double)terminal->outcome / 43.0;
        out_values[1].v[combo] = -out_values[0].v[combo];
    }
    return 0;
}

static void test_sampled_chance_is_unbiased(void)
{
    pe_vector_game_t game;
    pe_chance_vector_ctx_t first;
    pe_chance_vector_ctx_t second;
    const pe_value_vec_t *values;
    double total = 0.0;
    size_t iteration;
    size_t combo;

    for (iteration = 0; iteration < 44u; ++iteration)
        chance_outcomes[iteration] = (chance_state_t){0, (int)iteration};
    memset(&game, 0, sizeof(game));
    game.root = &chance_root;
    game.player_count = 2u;
    game.combo_count = 3u;
    game.is_terminal = chance_is_terminal;
    game.terminal_values = chance_terminal_values;

    CHECK(pe_chance_vector_ctx_init(&first, &game, chance_sample,
                                    chance_apply, 17u) == 0,
          "chance context init failed");
    CHECK(pe_chance_vector_ctx_init(&second, &game, chance_sample,
                                    chance_apply, 17u) == 0,
          "second chance context init failed");
    if (!first.initialized || !second.initialized)
        return;
    CHECK(pe_chance_vector_run(&first) == 0, "first chance run failed");
    CHECK(pe_chance_vector_run(&second) == 0, "second chance run failed");
    CHECK(first.sampled_chance_nodes == 1u && first.terminal_nodes == 1u,
          "chance traversal counters are %zu/%zu",
          first.sampled_chance_nodes, first.terminal_nodes);
    values = pe_chance_vector_values(&first, 0u);
    CHECK(values != NULL, "chance values unavailable");
    {
        const pe_value_vec_t *same_values =
            pe_chance_vector_values(&second, 0u);
        CHECK(values != NULL && same_values != NULL &&
                  fabs(values->v[0] - same_values->v[0]) <= 1e-15,
              "same seed did not reproduce the sampled outcome");
    }
    if (values)
    {
        for (combo = 0; combo < values->n; ++combo)
        {
            total = 0.0;
            for (iteration = 0; iteration < 10000u; ++iteration)
            {
                CHECK(pe_chance_vector_run(&first) == 0,
                      "chance iteration %zu failed", iteration);
                total += values->v[combo];
            }
            CHECK(fabs(total / 10000.0 - 0.5) < 0.02,
                  "combo %zu estimate is %.17g, expected 0.5",
                  combo, total / 10000.0);
        }
    }
    pe_chance_vector_ctx_destroy(&first);
    pe_chance_vector_ctx_destroy(&second);
}

typedef struct
{
    int terminal;
    int action;
} sampled_action_state_t;

static const sampled_action_state_t sampled_action_root = {0, -1};
static const sampled_action_state_t sampled_action_leaves[] = {
    {1, 0}, {1, 1}};

static int sampled_action_is_terminal(const void *state, void *user)
{
    (void)user;
    return ((const sampled_action_state_t *)state)->terminal;
}

static int sampled_action_sample_chance(const void *state, pe_rng_t *rng,
                                        pe_chance_sample_t *out, void *user)
{
    (void)state;
    (void)rng;
    (void)out;
    (void)user;
    return 1;
}

static const void *sampled_action_apply_chance(const void *state, int outcome,
                                               void *user)
{
    (void)state;
    (void)outcome;
    (void)user;
    return NULL;
}

static int sampled_action_acting_player(const void *state, void *user)
{
    (void)state;
    (void)user;
    return 0;
}

static uint16_t sampled_action_count(const void *state, void *user)
{
    (void)user;
    return ((const sampled_action_state_t *)state)->terminal ? 0u : 2u;
}

static int sampled_action_strategy(const void *state, uint64_t key,
                                   uint16_t action, pe_value_vec_t *out,
                                   void *user)
{
    static const double strategies[2][3] = {
        {0.2, 0.8, 0.4},
        {0.8, 0.2, 0.6}};
    (void)state;
    (void)key;
    (void)user;
    if (action >= 2u || out->n != 3u)
        return -1;
    memcpy(out->v, strategies[action], sizeof(strategies[action]));
    return 0;
}

static const void *sampled_action_apply(const void *state, uint16_t action,
                                         void *user)
{
    (void)state;
    (void)user;
    return action < 2u ? &sampled_action_leaves[action] : NULL;
}

static int sampled_action_compatible(const void *state, uint8_t player,
                                     uint16_t player_combo, uint8_t opponent,
                                     uint16_t opponent_combo, void *user)
{
    (void)state;
    (void)user;
    return player != opponent && player_combo != opponent_combo;
}

static int sampled_action_terminal_values(const void *state,
                                          const pe_reach_vec_t *reach,
                                          pe_value_vec_t *out_values,
                                          uint8_t player_count, void *user)
{
    const sampled_action_state_t *leaf = state;
    size_t combo;
    (void)reach;
    (void)user;
    if (player_count != 2u)
        return -1;
    for (combo = 0u; combo < out_values[0].n; ++combo)
    {
        double value = leaf->action == 0 ? 2.0 : 4.0;
        out_values[0].v[combo] = value;
        out_values[1].v[combo] = value;
    }
    return 0;
}

static void test_sampled_action_uses_compatible_reach(void)
{
    pe_vector_game_t game;
    pe_chance_vector_ctx_t ctx;
    const pe_value_vec_t *values;

    memset(&game, 0, sizeof(game));
    game.root = &sampled_action_root;
    game.player_count = 2u;
    game.combo_count = 3u;
    game.is_terminal = sampled_action_is_terminal;
    game.acting_player = sampled_action_acting_player;
    game.action_count = sampled_action_count;
    game.strategy = sampled_action_strategy;
    game.apply_action = sampled_action_apply;
    game.terminal_values = sampled_action_terminal_values;
    game.combo_compatible = sampled_action_compatible;
    CHECK(pe_chance_vector_ctx_init(&ctx, &game,
                                    sampled_action_sample_chance,
                                    sampled_action_apply_chance, 23u) == 0,
          "sampled action context init failed");
    if (!ctx.initialized)
        return;
    CHECK(pe_chance_vector_run(&ctx) == 0, "sampled action run failed");
    values = pe_chance_vector_values(&ctx, 0u);
    CHECK(values != NULL && fabs(values->v[0] - 3.6) <= 1e-12,
          "acting combo value is %.17g, expected 3.6",
          values ? values->v[0] : -1.0);
    values = pe_chance_vector_values(&ctx, 1u);
    CHECK(values != NULL && fabs(values->v[0] - 2.8) <= 1e-12,
          "compatible opponent value is %.17g, expected 2.8",
          values ? values->v[0] : -1.0);
    pe_chance_vector_ctx_destroy(&ctx);
}

int main(void)
{
    test_per_combo_normalization();
    test_uniform_fallback_and_invalid_inputs();
    test_reach_weighted_average();
    test_average_uniform_fallback();
    test_delayed_linear_average();
    test_importance_weighted_average();
    test_sampled_chance_is_unbiased();
    test_sampled_action_uses_compatible_reach();
    if (failures != 0)
    {
        fprintf(stderr, "test_pe_vector_cfr: %d failure(s)\n", failures);
        return 1;
    }
    puts("test_pe_vector_cfr: regret matching is normalized per combo");
    return 0;
}
