/*
 * test_vector_vs_scalar.c - VEC-09 lane-A parity gate.
 *
 * The public pe_solver lifecycle is still being assembled, so this gate is
 * deliberately one layer below it: it drives the released scalar reference
 * formula and the vector traversal/regret/average ports over the same compact
 * deterministic game trees. The four fixtures use the combo/player widths of
 * AKQ, Kuhn 2p, Kuhn 3p and Leduc; their tree payoffs are synthetic, which
 * keeps this test about representation parity rather than equilibrium quality.
 */

#include <poker_eval/core/pcg_rng.h>
#include <poker_eval/solver/pe_average.h>
#include <poker_eval/solver/pe_regret.h>
#include <poker_eval/solver/pe_traversal.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GATE_MAX_PLAYERS 3u
#define GATE_MAX_ACTIONS 3u
#define GATE_MAX_STATES 256u
#define GATE_MAX_INFOS 128u

typedef struct
{
    const char *name;
    uint8_t game_id;
    uint8_t player_count;
    uint16_t combo_count;
    uint16_t action_count;
    uint16_t depth;
    uint32_t iterations;
    uint64_t seed;
} gate_spec_t;

typedef struct
{
    int terminal;
    int player;
    uint16_t infoset;
    uint16_t child[GATE_MAX_ACTIONS];
    uint32_t path;
} gate_state_t;

typedef struct
{
    gate_spec_t spec;
    gate_state_t states[GATE_MAX_STATES];
    size_t state_count;
    size_t info_count;
    double *current;
    double *average;
    double *weighted;
    double *normalizer;
    double value_sum;
    pe_vector_game_t vector_game;
} gate_run_t;

static size_t strategy_offset(const gate_run_t *run, size_t info,
                              size_t action, size_t combo)
{
    return (info * run->spec.action_count + action) *
               run->spec.combo_count +
           combo;
}

static int build_node(gate_run_t *run, uint16_t depth, uint32_t path)
{
    gate_state_t *state;
    int index;
    uint16_t action;

    if (run->state_count >= GATE_MAX_STATES)
        return -1;
    index = (int)run->state_count++;
    state = &run->states[index];
    memset(state, 0, sizeof(*state));
    state->path = path;
    state->terminal = depth == run->spec.depth;
    if (state->terminal)
        state->player = -1;
    else
    {
        state->player = (int)(depth % run->spec.player_count);
        state->infoset = (uint16_t)run->info_count++;
        for (action = 0; action < run->spec.action_count; ++action)
        {
            int child = build_node(run, (uint16_t)(depth + 1u),
                                   path * run->spec.action_count + action);
            if (child < 0)
                return -1;
            state->child[action] = (uint16_t)child;
        }
    }
    return index;
}

static int gate_is_terminal(const void *state, void *user)
{
    (void)user;
    return ((const gate_state_t *)state)->terminal;
}

static int gate_acting_player(const void *state, void *user)
{
    (void)user;
    return ((const gate_state_t *)state)->player;
}

static uint16_t gate_action_count(const void *state, void *user)
{
    gate_run_t *run = (gate_run_t *)user;
    return ((const gate_state_t *)state)->terminal ? 0u :
           run->spec.action_count;
}

static uint64_t gate_infoset_key(const void *state, void *user)
{
    (void)user;
    return ((const gate_state_t *)state)->infoset;
}

static const void *gate_apply_action(const void *state, uint16_t action,
                                     void *user)
{
    gate_run_t *run = (gate_run_t *)user;
    const gate_state_t *node = (const gate_state_t *)state;
    if (node->terminal || action >= run->spec.action_count)
        return NULL;
    return &run->states[node->child[action]];
}

static double gate_payoff(const gate_run_t *run, const gate_state_t *state,
                          size_t combo)
{
    int raw = (int)((run->spec.game_id * 19u + state->path * 7u +
                     (uint32_t)combo * 11u) % 23u) - 11;
    return (double)raw / 5.0;
}

static int gate_terminal_values(const void *state,
                                const pe_reach_vec_t *reach,
                                pe_value_vec_t *out_values,
                                uint8_t player_count, void *user)
{
    gate_run_t *run = (gate_run_t *)user;
    const gate_state_t *terminal = (const gate_state_t *)state;
    size_t combo;
    uint8_t player;

    if (player_count != run->spec.player_count)
        return -1;
    for (combo = 0; combo < run->spec.combo_count; ++combo)
    {
        double joint_reach = 1.0;
        double payoff = gate_payoff(run, terminal, combo);
        for (player = 0; player < player_count; ++player)
            joint_reach *= reach[player].v[combo];
        run->value_sum += payoff * joint_reach;
        out_values[0].v[combo] = payoff;
        for (player = 1; player < player_count; ++player)
            out_values[player].v[combo] = -payoff /
                                           (double)(player_count - 1u);
    }
    return 0;
}

static int gate_strategy(const void *state, uint64_t infoset,
                         uint16_t action, pe_value_vec_t *out, void *user)
{
    gate_run_t *run = (gate_run_t *)user;
    const gate_state_t *node = (const gate_state_t *)state;
    size_t combo;

    (void)infoset;
    if (!out || out->n != run->spec.combo_count ||
        node->terminal || action >= run->spec.action_count)
        return -1;
    for (combo = 0; combo < run->spec.combo_count; ++combo)
        out->v[combo] = run->current[strategy_offset(
            run, node->infoset, action, combo)];
    return 0;
}

static void gate_make_vector_game(gate_run_t *run)
{
    memset(&run->vector_game, 0, sizeof(run->vector_game));
    run->vector_game.root = &run->states[0];
    run->vector_game.user = run;
    run->vector_game.player_count = run->spec.player_count;
    run->vector_game.combo_count = run->spec.combo_count;
    run->vector_game.is_terminal = gate_is_terminal;
    run->vector_game.acting_player = gate_acting_player;
    run->vector_game.action_count = gate_action_count;
    run->vector_game.infoset_key = gate_infoset_key;
    run->vector_game.strategy = gate_strategy;
    run->vector_game.apply_action = gate_apply_action;
    run->vector_game.terminal_values = gate_terminal_values;
}

static int gate_run_init(gate_run_t *run, const gate_spec_t *spec)
{
    size_t cells;

    memset(run, 0, sizeof(*run));
    run->spec = *spec;
    if (build_node(run, 0u, 0u) < 0 || run->info_count > GATE_MAX_INFOS)
        return -1;
    cells = run->info_count * spec->action_count * spec->combo_count;
    run->current = (double *)calloc(cells, sizeof(double));
    run->average = (double *)calloc(cells, sizeof(double));
    run->weighted = (double *)calloc(cells, sizeof(double));
    run->normalizer = (double *)calloc(run->info_count * spec->combo_count,
                                       sizeof(double));
    if (!run->current || !run->average || !run->weighted ||
        !run->normalizer)
    {
        free(run->current);
        free(run->average);
        free(run->weighted);
        free(run->normalizer);
        memset(run, 0, sizeof(*run));
        return -1;
    }
    gate_make_vector_game(run);
    return 0;
}

static void gate_run_destroy(gate_run_t *run)
{
    if (!run)
        return;
    free(run->current);
    free(run->average);
    free(run->weighted);
    free(run->normalizer);
    memset(run, 0, sizeof(*run));
}

static void gate_fill_regrets(const gate_run_t *run, pe_rng_t *rng,
                              double *regrets)
{
    size_t action;
    size_t combo;
    for (action = 0; action < run->spec.action_count; ++action)
        for (combo = 0; combo < run->spec.combo_count; ++combo)
            regrets[action * run->spec.combo_count + combo] =
                2.0 * pe_rng_uniform01(rng) - 0.8;
}

static void gate_scalar_regret_match(const gate_run_t *run,
                                     const double *regrets, double *strategy)
{
    size_t combo;
    size_t action;
    for (combo = 0; combo < run->spec.combo_count; ++combo)
    {
        double positive_sum = 0.0;
        for (action = 0; action < run->spec.action_count; ++action)
        {
            double positive = regrets[action * run->spec.combo_count + combo];
            if (positive > 0.0)
                positive_sum += positive;
        }
        for (action = 0; action < run->spec.action_count; ++action)
        {
            double positive = regrets[action * run->spec.combo_count + combo];
            strategy[action * run->spec.combo_count + combo] =
                positive_sum > 0.0 ?
                    (positive > 0.0 ? positive / positive_sum : 0.0) :
                    1.0 / (double)run->spec.action_count;
        }
    }
}

static int gate_run_scalar(gate_run_t *run)
{
    pe_rng_t rng;
    double *regrets;
    double *iteration_strategy;
    size_t info;
    size_t action;
    size_t combo;
    uint32_t iteration;

    regrets = (double *)calloc(run->spec.action_count *
                                   run->spec.combo_count, sizeof(double));
    iteration_strategy = (double *)calloc(run->spec.action_count *
                                              run->spec.combo_count,
                                          sizeof(double));
    if (!regrets || !iteration_strategy)
    {
        free(regrets);
        free(iteration_strategy);
        return -1;
    }
    for (iteration = 0; iteration < run->spec.iterations; ++iteration)
    {
        pe_rng_seed(&rng, pe_rng_derive(run->spec.seed, iteration));
        for (info = 0; info < run->info_count; ++info)
        {
            gate_fill_regrets(run, &rng, regrets);
            gate_scalar_regret_match(run, regrets, iteration_strategy);
            for (action = 0; action < run->spec.action_count; ++action)
                for (combo = 0; combo < run->spec.combo_count; ++combo)
                    run->average[strategy_offset(run, info, action, combo)] +=
                        iteration_strategy[action * run->spec.combo_count +
                                           combo];
        }
    }
    for (info = 0; info < run->info_count; ++info)
        for (action = 0; action < run->spec.action_count; ++action)
            for (combo = 0; combo < run->spec.combo_count; ++combo)
                run->average[strategy_offset(run, info, action, combo)] /=
                    (double)run->spec.iterations;
    free(regrets);
    free(iteration_strategy);
    return 0;
}

static int gate_run_vector(gate_run_t *run)
{
    const pe_traversal_ops_t *ops = pe_traversal_full_vector_ops();
    pe_traversal_ctx_t traversal;
    pe_rng_t rng;
    double *regrets;
    double *ones;
    size_t info;
    size_t combo;
    uint32_t iteration;

    regrets = (double *)calloc(run->spec.action_count *
                                   run->spec.combo_count, sizeof(double));
    ones = (double *)malloc(run->spec.combo_count * sizeof(double));
    if (!regrets || !ones)
    {
        free(regrets);
        free(ones);
        return -1;
    }
    for (combo = 0; combo < run->spec.combo_count; ++combo)
        ones[combo] = 1.0;
    if (pe_traversal_ctx_init(&traversal, &run->vector_game) != 0)
    {
        free(regrets);
        free(ones);
        return -1;
    }
    for (iteration = 0; iteration < run->spec.iterations; ++iteration)
    {
        pe_rng_seed(&rng, pe_rng_derive(run->spec.seed, iteration));
        for (info = 0; info < run->info_count; ++info)
        {
            double *current = run->current +
                               info * run->spec.action_count *
                                   run->spec.combo_count;
            double *weighted = run->weighted +
                               info * run->spec.action_count *
                                   run->spec.combo_count;
            double *normalizer = run->normalizer +
                                 info * run->spec.combo_count;
            gate_fill_regrets(run, &rng, regrets);
            if (pe_regret_match_vector(regrets, current,
                                       run->spec.action_count,
                                       run->spec.combo_count) != 0 ||
                pe_average_accumulate_vector(weighted, normalizer, current,
                                             ones, run->spec.action_count,
                                             run->spec.combo_count, 1.0) != 0)
            {
                pe_traversal_ctx_destroy(&traversal);
                free(regrets);
                free(ones);
                return -1;
            }
        }
        if (ops->begin_iteration(&traversal, iteration) != 0 ||
            ops->run_iteration(&traversal, NULL) != 0 ||
            ops->end_iteration(&traversal, iteration) != 0)
        {
            pe_traversal_ctx_destroy(&traversal);
            free(regrets);
            free(ones);
            return -1;
        }
    }
    for (info = 0; info < run->info_count; ++info)
    {
        double *weighted = run->weighted +
                           info * run->spec.action_count *
                               run->spec.combo_count;
        double *normalizer = run->normalizer +
                             info * run->spec.combo_count;
        double *average = run->average +
                          info * run->spec.action_count *
                              run->spec.combo_count;
        if (pe_average_finalize_vector(weighted, normalizer, average,
                                       run->spec.action_count,
                                       run->spec.combo_count) != 0)
        {
            pe_traversal_ctx_destroy(&traversal);
            free(regrets);
            free(ones);
            return -1;
        }
    }
    memcpy(run->current, run->average,
           run->info_count * run->spec.action_count *
               run->spec.combo_count * sizeof(double));
    run->value_sum = 0.0;
    if (ops->begin_iteration(&traversal, run->spec.iterations) != 0 ||
        ops->run_iteration(&traversal, NULL) != 0 ||
        ops->end_iteration(&traversal, run->spec.iterations) != 0)
    {
        pe_traversal_ctx_destroy(&traversal);
        free(regrets);
        free(ones);
        return -1;
    }
    run->value_sum /= (double)run->spec.combo_count;
    pe_traversal_ctx_destroy(&traversal);
    free(regrets);
    free(ones);
    return 0;
}

static double gate_scalar_value(const gate_run_t *run, const gate_state_t *state,
                                size_t combo)
{
    double value = 0.0;
    uint16_t action;

    if (state->terminal)
        return gate_payoff(run, state, combo);
    for (action = 0; action < run->spec.action_count; ++action)
        value += run->average[strategy_offset(run, state->infoset, action,
                                              combo)] *
                 gate_scalar_value(run, &run->states[state->child[action]],
                                   combo);
    return value;
}

static int gate_compare(const gate_spec_t *spec)
{
    gate_run_t scalar = {0};
    gate_run_t vector = {0};
    double scalar_value = 0.0;
    size_t info;
    size_t action;
    size_t combo;

    if (gate_run_init(&scalar, spec) != 0 || gate_run_init(&vector, spec) != 0)
    {
        gate_run_destroy(&scalar);
        gate_run_destroy(&vector);
        fprintf(stderr, "%s: setup failed\n", spec->name);
        return 1;
    }
    if (gate_run_scalar(&scalar) != 0 || gate_run_vector(&vector) != 0)
    {
        gate_run_destroy(&scalar);
        gate_run_destroy(&vector);
        fprintf(stderr, "%s: lane execution failed\n", spec->name);
        return 1;
    }
    for (combo = 0; combo < spec->combo_count; ++combo)
        scalar_value += gate_scalar_value(&scalar, &scalar.states[0], combo);
    scalar_value /= (double)spec->combo_count;
    if (fabs(scalar_value - vector.value_sum) > 1e-9)
    {
        fprintf(stderr, "%s: value mismatch scalar=%.17g vector=%.17g\n",
                spec->name, scalar_value, vector.value_sum);
        gate_run_destroy(&scalar);
        gate_run_destroy(&vector);
        return 1;
    }
    for (info = 0; info < scalar.info_count; ++info)
        for (action = 0; action < spec->action_count; ++action)
            for (combo = 0; combo < spec->combo_count; ++combo)
            {
                double a = scalar.average[strategy_offset(&scalar, info,
                                                           action, combo)];
                double b = vector.average[strategy_offset(&vector, info,
                                                           action, combo)];
                if (fabs(a - b) > 1e-7)
                {
                    fprintf(stderr,
                            "%s: strategy mismatch info=%zu action=%zu "
                            "combo=%zu scalar=%.17g vector=%.17g\n",
                            spec->name, info, action, combo, a, b);
                    gate_run_destroy(&scalar);
                    gate_run_destroy(&vector);
                    return 1;
                }
            }
    printf("  %-10s value %.12f, %zu infosets: ok\n", spec->name,
           scalar_value, scalar.info_count);
    gate_run_destroy(&scalar);
    gate_run_destroy(&vector);
    return 0;
}

int main(void)
{
    static const gate_spec_t specs[] = {
        {"AKQ", 0u, 2u, 6u, 2u, 3u, 96u, 0xA001ULL},
        {"Kuhn 2p", 1u, 2u, 6u, 2u, 3u, 96u, 0xA002ULL},
        {"Kuhn 3p", 2u, 3u, 18u, 3u, 3u, 96u, 0xA003ULL},
        {"Leduc", 3u, 2u, 20u, 3u, 4u, 96u, 0xA004ULL}
    };
    size_t i;

    for (i = 0; i < sizeof(specs) / sizeof(specs[0]); ++i)
        if (gate_compare(&specs[i]) != 0)
            return 1;
    return 0;
}
