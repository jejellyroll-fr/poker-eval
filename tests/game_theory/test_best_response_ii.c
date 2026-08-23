/*
 * test_best_response_ii.c - BR-02 vector infoset best response.
 */

#include <poker_eval/solver/pe_best_response.h>
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

    game.is_chance = br_is_chance;
    game.chance_outcome_count = br_chance_outcome_count;
    game.chance_outcome_weight = br_chance_outcome_weight;
    game.apply_chance = br_apply_chance;
    CHECK(pe_best_response_vector(&game, 0u, &config, &result) ==
              PE_SOLVER_OK,
          "chance-aware vector best response failed");
    CHECK(fabs(result.value - 4.0 / 3.0) <= 1e-12,
          "chance-weighted value %.17g, expected %.17g", result.value,
          4.0 / 3.0);

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
