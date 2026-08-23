/*
 * test_best_response_ii.c - BR-02 vector infoset best response.
 */

#include <poker_eval/solver/pe_best_response.h>
#include <poker_eval/solver/pe_traversal.h>
#include <poker_eval/engine/solvers/cfr/cfr_core.h>

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

typedef struct br_state_t br_state_t;

struct br_state_t
{
    int terminal;
    int player;
    uint64_t infoset;
    const br_state_t *children[2];
    double payoff[3];
};

static br_state_t root;
static br_state_t first;
static br_state_t second;
static br_state_t terminals[4];

static int br_is_terminal(const void *state, void *user)
{
    (void)user;
    return ((const br_state_t *)state)->terminal;
}

static int br_is_chance(const void *state, void *user)
{
    (void)user;
    return state == &root;
}

static uint16_t br_chance_outcome_count(const void *state, void *user)
{
    (void)state;
    (void)user;
    return 2u;
}

static double br_chance_outcome_weight(const void *state, uint16_t outcome,
                                       void *user)
{
    (void)state;
    (void)user;
    return outcome == 0u ? 1.0 : 3.0;
}

static const void *br_apply_chance(const void *state, int outcome, void *user)
{
    (void)user;
    return state == &root && outcome == 0 ? &first
         : state == &root && outcome == 1 ? &second
         : NULL;
}

static int toy_cfr_current_player(cfr_game_t *game, uint64_t state,
                                   void *user)
{
    const br_state_t *node = (const br_state_t *)(uintptr_t)state;
    (void)game;
    (void)user;
    return node->terminal ? -1 : node->player;
}

static int toy_cfr_is_terminal(cfr_game_t *game, uint64_t state, void *user)
{
    (void)game;
    (void)user;
    return ((const br_state_t *)(uintptr_t)state)->terminal;
}

static int toy_cfr_get_actions(cfr_game_t *game, uint64_t state, int *out,
                               int max_actions, void *user)
{
    const br_state_t *node = (const br_state_t *)(uintptr_t)state;
    (void)game;
    (void)user;
    if (node->terminal || max_actions < 2)
        return 0;
    out[0] = 0;
    out[1] = 1;
    return 2;
}

static uint64_t toy_cfr_apply_action(cfr_game_t *game, uint64_t state,
                                     int action, void *user)
{
    const br_state_t *node = (const br_state_t *)(uintptr_t)state;
    (void)game;
    (void)user;
    return action < 2 && !node->terminal
        ? (uint64_t)(uintptr_t)node->children[action]
        : 0u;
}

static uint64_t toy_cfr_infoset_key(const void *state)
{
    const br_state_t *node = (const br_state_t *)state;
    return node->terminal ? (uint64_t)(uintptr_t)node : node->infoset;
}

static double toy_cfr_get_utility(cfr_game_t *game, uint64_t state, int player,
                                  void *user)
{
    const br_state_t *terminal = (const br_state_t *)(uintptr_t)state;
    double value = (terminal->payoff[0] + terminal->payoff[1] +
                    terminal->payoff[2]) / 3.0;
    (void)game;
    (void)user;
    return player == 0 ? value : -value;
}

static int toy_cfr_is_chance(cfr_game_t *game, uint64_t state, void *user)
{
    (void)game;
    return user && *(const int *)user &&
           (const br_state_t *)(uintptr_t)state == &root;
}

static int toy_cfr_chance_outcomes(cfr_game_t *game, uint64_t state,
                                   void *user)
{
    (void)game;
    (void)state;
    (void)user;
    return 2;
}

static double toy_cfr_chance_weight(cfr_game_t *game, uint64_t state,
                                    int outcome, void *user)
{
    (void)game;
    (void)state;
    (void)user;
    return outcome == 0 ? 1.0 : 3.0;
}

static uint64_t toy_cfr_apply_chance(cfr_game_t *game, uint64_t state,
                                      int outcome, void *user)
{
    (void)game;
    (void)user;
    return state == (uint64_t)(uintptr_t)&root && outcome == 0
        ? (uint64_t)(uintptr_t)&first
        : state == (uint64_t)(uintptr_t)&root && outcome == 1
            ? (uint64_t)(uintptr_t)&second
            : 0u;
}

static void init_toy_cfr_game(cfr_game_t *game)
{
    memset(game, 0, sizeof(*game));
    game->current_player = toy_cfr_current_player;
    game->is_terminal = toy_cfr_is_terminal;
    game->get_actions = toy_cfr_get_actions;
    game->apply_action = toy_cfr_apply_action;
    game->get_infoset_key = toy_cfr_infoset_key;
    game->get_utility = toy_cfr_get_utility;
    game->is_chance = toy_cfr_is_chance;
    game->get_chance_outcomes = toy_cfr_chance_outcomes;
    game->get_chance_weight = toy_cfr_chance_weight;
    game->apply_chance = toy_cfr_apply_chance;
    game->initial_state = &root;
    game->state_size = sizeof(uint64_t);
    game->num_players = 2;
}

/* A compact real Kuhn adapter used to compare both BR implementations on the
 * six ordered private-card deals, not only on the synthetic fixture above. */
#define K2_PH_ROOT 0
#define K2_PH_P2C 1
#define K2_PH_P1RB 2
#define K2_PH_P2B 3
#define K2_TERMINAL 4

typedef struct
{
    cfr_game_t cfr;
    cfr_storage_t *storage;
    pe_vector_game_t vector;
} k2_adapter_t;

static uint64_t k2_key(int p1, int p2, int phase, int last)
{
    return ((uint64_t)(p1 & 3)) | ((uint64_t)(p2 & 3) << 2) |
           ((uint64_t)(phase & 7) << 4) | ((uint64_t)(last & 3) << 7);
}

static void k2_unpack(uint64_t key, int *p1, int *p2, int *phase, int *last)
{
    *p1 = (int)(key & 3);
    *p2 = (int)((key >> 2) & 3);
    *phase = (int)((key >> 4) & 7);
    *last = (int)((key >> 7) & 3);
}

static int k2_current_player(cfr_game_t *game, uint64_t key, void *user)
{
    int phase = (int)(key >> 4) & 7;
    (void)game;
    (void)user;
    return phase == K2_PH_ROOT || phase == K2_PH_P1RB ? 0
         : phase == K2_PH_P2C || phase == K2_PH_P2B ? 1
         : -1;
}

static int k2_is_terminal(cfr_game_t *game, uint64_t key, void *user)
{
    (void)game;
    (void)user;
    return ((int)(key >> 4) & 7) >= K2_TERMINAL;
}

static double k2_utility(int p1, int p2, int phase, int last)
{
    int p1wins = p1 > p2;
    if (phase == K2_TERMINAL + K2_PH_P2C)
        return p1wins ? 1.0 : -1.0;
    if (phase == K2_TERMINAL + K2_PH_P1RB)
        return last == 0 ? -1.0 : (p1wins ? 2.0 : -2.0);
    if (phase == K2_TERMINAL + K2_PH_P2B)
        return last == 0 ? 1.0 : (p1wins ? 2.0 : -2.0);
    return 0.0;
}

static double k2_get_utility(cfr_game_t *game, uint64_t key, int player,
                             void *user)
{
    int p1, p2, phase, last;
    (void)game;
    (void)user;
    k2_unpack(key, &p1, &p2, &phase, &last);
    return player == 0 ? k2_utility(p1, p2, phase, last)
                       : -k2_utility(p1, p2, phase, last);
}

static int k2_get_actions(cfr_game_t *game, uint64_t key, int *out,
                          int max_actions, void *user)
{
    (void)game;
    (void)user;
    if (((int)(key >> 4) & 7) >= K2_TERMINAL || max_actions < 2)
        return 0;
    out[0] = 0;
    out[1] = 1;
    return 2;
}

static uint64_t k2_apply_action(cfr_game_t *game, uint64_t key, int action,
                                void *user)
{
    int p1, p2, phase, last;
    (void)game;
    (void)user;
    k2_unpack(key, &p1, &p2, &phase, &last);
    if (action < 0 || action > 1)
        return 0;
    if (phase == K2_PH_ROOT)
        return k2_key(p1, p2, action == 0 ? K2_PH_P2C : K2_PH_P2B, action);
    if (phase == K2_PH_P2C)
        return action == 0
            ? k2_key(p1, p2, K2_TERMINAL + K2_PH_P2C, action)
            : k2_key(p1, p2, K2_PH_P1RB, action);
    if (phase == K2_PH_P1RB)
        return k2_key(p1, p2, K2_TERMINAL + K2_PH_P1RB, action);
    if (phase == K2_PH_P2B)
        return k2_key(p1, p2, K2_TERMINAL + K2_PH_P2B, action);
    return 0;
}

static uint64_t k2_infoset_key(const void *state)
{
    uint64_t key = (uint64_t)(uintptr_t)state;
    int p1, p2, phase, last, hand;
    if (key == 0)
        return 0;
    k2_unpack(key, &p1, &p2, &phase, &last);
    if (phase >= K2_TERMINAL)
        return (1ULL << 60) | key;
    hand = phase == K2_PH_ROOT || phase == K2_PH_P1RB ? p1 : p2;
    return (1ULL << 60) | ((uint64_t)hand << 8) | (uint64_t)phase;
}

static int k2_is_chance(cfr_game_t *game, uint64_t key, void *user)
{
    (void)game;
    (void)user;
    return key == 0;
}

static int k2_chance_outcomes(cfr_game_t *game, uint64_t key, void *user)
{
    (void)game;
    (void)key;
    (void)user;
    return 6;
}

static uint64_t k2_apply_chance(cfr_game_t *game, uint64_t key, int outcome,
                                void *user)
{
    static const int pairs[6][2] = {{0, 1}, {0, 2}, {1, 0},
                                    {1, 2}, {2, 0}, {2, 1}};
    (void)game;
    (void)user;
    return key == 0 && outcome >= 0 && outcome < 6
        ? k2_key(pairs[outcome][0], pairs[outcome][1], K2_PH_ROOT, 0)
        : 0;
}

static uint64_t k2_state_key(const k2_adapter_t *adapter, const void *state)
{
    return state == adapter ? 0 : (uint64_t)(uintptr_t)state;
}

static int k2_vector_is_terminal(const void *state, void *user)
{
    k2_adapter_t *adapter = (k2_adapter_t *)user;
    return k2_is_terminal(&adapter->cfr, k2_state_key(adapter, state), user);
}

static int k2_vector_acting_player(const void *state, void *user)
{
    k2_adapter_t *adapter = (k2_adapter_t *)user;
    return k2_current_player(&adapter->cfr, k2_state_key(adapter, state), user);
}

static uint16_t k2_vector_action_count(const void *state, void *user)
{
    k2_adapter_t *adapter = (k2_adapter_t *)user;
    int actions[2];
    return (uint16_t)k2_get_actions(&adapter->cfr,
                                    k2_state_key(adapter, state), actions, 2,
                                    user);
}

static uint64_t k2_vector_infoset_key(const void *state, void *user)
{
    k2_adapter_t *adapter = (k2_adapter_t *)user;
    uint64_t key = k2_state_key(adapter, state);
    return k2_infoset_key((const void *)(uintptr_t)key);
}

static int k2_vector_strategy(const void *state, uint64_t infoset,
                              uint16_t action, pe_value_vec_t *out,
                              void *user)
{
    k2_adapter_t *adapter = (k2_adapter_t *)user;
    double strategy[2];
    (void)state;
    if (!out || out->n != 1 || action > 1)
        return -1;
    cfr_storage_get_avg_strategy(adapter->storage, infoset, 2, strategy);
    out->v[0] = strategy[action];
    return 0;
}

static const void *k2_vector_apply_action(const void *state, uint16_t action,
                                          void *user)
{
    k2_adapter_t *adapter = (k2_adapter_t *)user;
    uint64_t key = k2_apply_action(&adapter->cfr,
                                  k2_state_key(adapter, state), (int)action,
                                  user);
    return key ? (const void *)(uintptr_t)key : NULL;
}

static int k2_vector_terminal_values(const void *state,
                                     const pe_reach_vec_t *reach,
                                     pe_value_vec_t *out,
                                     uint8_t player_count, void *user)
{
    k2_adapter_t *adapter = (k2_adapter_t *)user;
    uint64_t key = k2_state_key(adapter, state);
    (void)reach;
    if (player_count != 2 || !out)
        return -1;
    out[0].v[0] = k2_get_utility(&adapter->cfr, key, 0, user);
    out[1].v[0] = k2_get_utility(&adapter->cfr, key, 1, user);
    return 0;
}

static int k2_vector_is_chance(const void *state, void *user)
{
    k2_adapter_t *adapter = (k2_adapter_t *)user;
    return k2_is_chance(&adapter->cfr, k2_state_key(adapter, state), user);
}

static uint16_t k2_vector_chance_count(const void *state, void *user)
{
    k2_adapter_t *adapter = (k2_adapter_t *)user;
    return (uint16_t)k2_chance_outcomes(&adapter->cfr,
                                        k2_state_key(adapter, state), user);
}

static const void *k2_vector_apply_chance(const void *state, int outcome,
                                          void *user)
{
    k2_adapter_t *adapter = (k2_adapter_t *)user;
    uint64_t key = k2_apply_chance(&adapter->cfr,
                                   k2_state_key(adapter, state), outcome,
                                   user);
    return key ? (const void *)(uintptr_t)key : NULL;
}

static void k2_init(k2_adapter_t *adapter)
{
    pe_vector_game_t *vector = &adapter->vector;
    memset(adapter, 0, sizeof(*adapter));
    adapter->cfr.current_player = k2_current_player;
    adapter->cfr.is_terminal = k2_is_terminal;
    adapter->cfr.get_utility = k2_get_utility;
    adapter->cfr.get_actions = k2_get_actions;
    adapter->cfr.apply_action = k2_apply_action;
    adapter->cfr.get_infoset_key = k2_infoset_key;
    adapter->cfr.is_chance = k2_is_chance;
    adapter->cfr.get_chance_outcomes = k2_chance_outcomes;
    adapter->cfr.apply_chance = k2_apply_chance;
    adapter->cfr.initial_state = (void *)(uintptr_t)0;
    adapter->cfr.state_size = sizeof(uint64_t);
    adapter->cfr.num_players = 2;
    adapter->storage = cfr_storage_create();

    memset(vector, 0, sizeof(*vector));
    vector->root = adapter;
    vector->user = adapter;
    vector->player_count = 2;
    vector->combo_count = 1;
    vector->is_chance = k2_vector_is_chance;
    vector->chance_outcome_count = k2_vector_chance_count;
    vector->apply_chance = k2_vector_apply_chance;
    vector->is_terminal = k2_vector_is_terminal;
    vector->acting_player = k2_vector_acting_player;
    vector->action_count = k2_vector_action_count;
    vector->infoset_key = k2_vector_infoset_key;
    vector->strategy = k2_vector_strategy;
    vector->apply_action = k2_vector_apply_action;
    vector->terminal_values = k2_vector_terminal_values;
}

static int br_acting_player(const void *state, void *user)
{
    (void)user;
    return ((const br_state_t *)state)->player;
}

static uint16_t br_action_count(const void *state, void *user)
{
    (void)user;
    return ((const br_state_t *)state)->terminal ? 0u : 2u;
}

static uint64_t br_infoset_key(const void *state, void *user)
{
    (void)user;
    return ((const br_state_t *)state)->infoset;
}

static const void *br_apply_action(const void *state, uint16_t action,
                                   void *user)
{
    const br_state_t *node = (const br_state_t *)state;
    (void)user;
    return action < 2u && !node->terminal ? node->children[action] : NULL;
}

static int br_strategy(const void *state, uint64_t infoset, uint16_t action,
                       pe_value_vec_t *out, void *user)
{
    size_t combo;
    (void)state;
    (void)infoset;
    (void)user;
    if (action >= 2u || out->n != 3u)
        return -1;
    for (combo = 0; combo < out->n; ++combo)
        out->v[combo] = 0.5;
    return 0;
}

static int br_terminal_values(const void *state, const pe_reach_vec_t *reach,
                              pe_value_vec_t *out, uint8_t player_count,
                              void *user)
{
    const br_state_t *terminal = (const br_state_t *)state;
    size_t combo;
    (void)reach;
    (void)user;
    if (player_count != 2u)
        return -1;
    for (combo = 0; combo < 3u; ++combo)
    {
        out[0].v[combo] = terminal->payoff[combo];
        out[1].v[combo] = -terminal->payoff[combo];
    }
    return 0;
}

static void init_game(pe_vector_game_t *game)
{
    memset(&root, 0, sizeof(root));
    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));
    memset(terminals, 0, sizeof(terminals));
    root.player = 1;
    root.infoset = 1u;
    root.children[0] = &first;
    root.children[1] = &second;
    first.player = second.player = 0;
    /* Both concrete states deliberately share one information set. */
    first.infoset = second.infoset = 42u;
    first.children[0] = &terminals[0];
    first.children[1] = &terminals[1];
    second.children[0] = &terminals[2];
    second.children[1] = &terminals[3];
    terminals[0].terminal = terminals[1].terminal =
        terminals[2].terminal = terminals[3].terminal = 1;
    terminals[0].payoff[0] = 2.0;
    terminals[0].payoff[1] = 4.0;
    terminals[0].payoff[2] = 1.0;
    terminals[1].payoff[0] = 0.0;
    terminals[1].payoff[1] = 1.0;
    terminals[1].payoff[2] = 3.0;
    terminals[2].payoff[0] = 1.0;
    terminals[2].payoff[1] = 0.0;
    terminals[2].payoff[2] = 2.0;
    terminals[3].payoff[0] = 3.0;
    terminals[3].payoff[1] = 2.0;
    terminals[3].payoff[2] = 0.0;

    memset(game, 0, sizeof(*game));
    game->root = &root;
    game->player_count = 2u;
    game->combo_count = 3u;
    game->is_terminal = br_is_terminal;
    game->acting_player = br_acting_player;
    game->action_count = br_action_count;
    game->infoset_key = br_infoset_key;
    game->strategy = br_strategy;
    game->apply_action = br_apply_action;
    game->terminal_values = br_terminal_values;
}

static void test_shared_infoset_and_convergence(void)
{
    pe_vector_game_t game;
    pe_best_response_vector_config_t config;
    pe_best_response_vector_result_t result;
    cfr_game_t cfr_game;
    cfr_storage_t *cfr_storage;
    int chance_enabled = 0;
    double scalar_value;

    init_game(&game);
    config = pe_best_response_vector_config_default();
    CHECK(pe_best_response_vector(&game, 0u, &config, &result) ==
              PE_SOLVER_OK,
          "vector infoset best response failed");
    CHECK(result.infosets == 1u, "expected one shared infoset, got %u",
          result.infosets);
    CHECK(result.converged && result.iterations <= config.max_iterations,
          "fixed point did not converge: %u passes", result.iterations);
    CHECK(fabs(result.value - 5.0 / 3.0) <= 1e-12,
          "shared-infoset value %.17g, expected %.17g", result.value,
          5.0 / 3.0);
    CHECK(result.visited_nodes > 0u, "no nodes were visited");

    init_toy_cfr_game(&cfr_game);
    cfr_storage = cfr_storage_create();
    CHECK(cfr_storage != NULL, "scalar reference storage allocation");
    scalar_value = cfr_best_response_value_infoset(
        &cfr_game, cfr_storage, 0, &chance_enabled);
    CHECK(fabs(result.value - scalar_value) <= 1e-12,
          "vector/scalar infoset value mismatch: %.17g vs %.17g",
          result.value, scalar_value);

    game.is_chance = br_is_chance;
    game.chance_outcome_count = br_chance_outcome_count;
    game.chance_outcome_weight = br_chance_outcome_weight;
    game.apply_chance = br_apply_chance;
    CHECK(pe_best_response_vector(&game, 0u, &config, &result) ==
              PE_SOLVER_OK,
          "chance-aware vector best response failed");
    CHECK(fabs(result.value - 19.0 / 12.0) <= 1e-12,
          "chance-weighted value %.17g, expected %.17g", result.value,
          19.0 / 12.0);

    chance_enabled = 1;
    scalar_value = cfr_best_response_value_infoset(
        &cfr_game, cfr_storage, 0, &chance_enabled);
    CHECK(fabs(result.value - scalar_value) <= 1e-12,
          "chance vector/scalar value mismatch: %.17g vs %.17g",
          result.value, scalar_value);
    cfr_storage_destroy(cfr_storage);

    game.is_chance = NULL;
    game.chance_outcome_count = NULL;
    game.chance_outcome_weight = NULL;
    game.apply_chance = NULL;

    config.max_iterations = 1u;
    CHECK(pe_best_response_vector(&game, 0u, &config, &result) ==
              PE_SOLVER_OK,
          "bounded vector infoset best response failed");
    CHECK(!result.converged && result.iterations == 1u,
          "one-pass bound did not report non-convergence");
}

static void test_kuhn_two_player_parity(void)
{
    k2_adapter_t adapter;
    pe_best_response_vector_config_t config;
    pe_best_response_vector_result_t result;
    double scalar_value;

    k2_init(&adapter);
    CHECK(adapter.storage != NULL, "Kuhn scalar storage allocation");
    if (!adapter.storage)
        return;
    config = pe_best_response_vector_config_default();
    CHECK(pe_best_response_vector(&adapter.vector, 0u, &config, &result) ==
              PE_SOLVER_OK,
          "Kuhn vector best response failed");
    scalar_value = cfr_best_response_value_infoset(
        &adapter.cfr, adapter.storage, 0, NULL);
    CHECK(result.converged, "Kuhn vector best response did not converge");
    CHECK(fabs(result.value - scalar_value) <= 1e-9,
          "Kuhn vector/scalar mismatch: %.17g vs %.17g", result.value,
          scalar_value);
    CHECK(result.infosets > 0u, "Kuhn vector BR found no infosets");
    cfr_storage_destroy(adapter.storage);
}

static void test_exploitability_metrics(void)
{
    pe_metrics_t metrics;
    CHECK(pe_best_response_metrics_from_raw(0.001, 1.0, &metrics) ==
              PE_SOLVER_OK,
          "raw exploitability conversion failed");
    CHECK(fabs(metrics.exploitability_raw - 0.001) <= 1e-15,
          "raw exploitability was not preserved");
    CHECK(fabs(metrics.exploitability_mbb_per_game - 1.0) <= 1e-12,
          "0.001 BB should equal 1.0 mbb/g, got %.17g",
          metrics.exploitability_mbb_per_game);
    CHECK(metrics.guarantee == PE_GUARANTEE_UNSPECIFIED,
          "raw conversion must not infer a game guarantee");
    CHECK(pe_best_response_metrics_from_raw(-1.0, 1.0, &metrics) ==
              PE_SOLVER_ERR_INVALID_CONFIG,
          "negative exploitability must be rejected");
    CHECK(pe_best_response_metrics_from_raw(0.001, 0.0, &metrics) ==
              PE_SOLVER_ERR_INVALID_CONFIG,
          "zero big blind must be rejected");
}

static void test_multiway_guarantee_contract(void)
{
    pe_guarantee_t guarantee = PE_GUARANTEE_UNSPECIFIED;

    CHECK(pe_best_response_guarantee_for_game(2u, 1, &guarantee) ==
              PE_SOLVER_OK && guarantee == PE_GUARANTEE_NASH,
          "two-player zero-sum games should report Nash");
    CHECK(pe_best_response_guarantee_for_game(3u, 1, &guarantee) ==
              PE_SOLVER_OK && guarantee == PE_GUARANTEE_NO_REGRET_ONLY,
          "multiway zero-sum games should report no-regret only");
    CHECK(pe_best_response_guarantee_for_game(2u, 0, &guarantee) ==
              PE_SOLVER_OK && guarantee == PE_GUARANTEE_EMPIRICAL,
          "non-zero-sum games should report an empirical guarantee");
    CHECK(pe_best_response_guarantee_for_game(0u, 1, &guarantee) ==
              PE_SOLVER_ERR_INVALID_CONFIG,
          "zero players must be rejected");
    CHECK(pe_best_response_guarantee_for_game(3u, 2, &guarantee) ==
              PE_SOLVER_ERR_INVALID_CONFIG,
          "non-boolean zero-sum flag must be rejected");
}

static void test_exploitability_target(void)
{
    int reached = -1;
    CHECK(pe_best_response_target_reached(4.9, 5.0, &reached) ==
              PE_SOLVER_OK && reached,
          "target should stop below threshold");
    CHECK(pe_best_response_target_reached(5.0, 5.0, &reached) ==
              PE_SOLVER_OK && reached,
          "target should stop at threshold");
    CHECK(pe_best_response_target_reached(5.1, 5.0, &reached) ==
              PE_SOLVER_OK && !reached,
          "target should continue above threshold");
    CHECK(pe_best_response_target_reached(1.0, 0.0, &reached) ==
              PE_SOLVER_OK && !reached,
          "zero target should disable the stop condition");
    CHECK(pe_best_response_target_reached(-1.0, 5.0, &reached) ==
              PE_SOLVER_ERR_INVALID_CONFIG,
          "negative measured exploitability must be rejected");
}

int main(void)
{
    test_shared_infoset_and_convergence();
    test_kuhn_two_player_parity();
    test_exploitability_metrics();
    test_multiway_guarantee_contract();
    test_exploitability_target();
    if (failures != 0)
    {
        fprintf(stderr, "test_best_response_ii: %d failure(s)\n", failures);
        return 1;
    }
    puts("test_best_response_ii: shared-infoset vector BR matches enumeration");
    return 0;
}
