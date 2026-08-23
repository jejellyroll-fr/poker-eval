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

int main(void)
{
    test_shared_infoset_and_convergence();
    if (failures != 0)
    {
        fprintf(stderr, "test_best_response_ii: %d failure(s)\n", failures);
        return 1;
    }
    puts("test_best_response_ii: shared-infoset vector BR matches enumeration");
    return 0;
}
