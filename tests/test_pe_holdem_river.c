#include <poker_eval/solver/pe_best_response.h>
#include <poker_eval/solver/pe_betting_vector.h>
#include <poker_eval/solver/pe_holdem_river.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition, message)                                      \
    do                                                                 \
    {                                                                  \
        if (!(condition))                                              \
        {                                                              \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,  \
                    message);                                         \
            failures++;                                                \
        }                                                              \
    } while (0)

typedef struct
{
    pe_holdem_river_spec_t spec;
} fixture_t;

static uint16_t no_actions(const pe_betting_state_t *state, void *user)
{
    (void)state;
    (void)user;
    return 0u;
}

static pe_action_status_t no_action(const pe_betting_state_t *state,
                                     uint16_t action, pe_action_t *out,
                                     void *user)
{
    (void)state;
    (void)action;
    (void)out;
    (void)user;
    return PE_ACTION_ERR_OUT_OF_RANGE;
}

static uint64_t terminal_key(const pe_betting_state_t *state, void *user)
{
    (void)state;
    (void)user;
    return 1u;
}

static int terminal_values(const pe_betting_state_t *state,
                           const pe_reach_vec_t *reach,
                           pe_value_vec_t *out_values, uint8_t player_count,
                           void *user)
{
    fixture_t *fixture = (fixture_t *)user;
    return pe_holdem_river_terminal_values(&fixture->spec, state, reach,
                                           out_values, player_count);
}

static mask_t card(int rank, int suit)
{
    return mask_set(MASK_EMPTY, MODERN_MAKE_CARD(rank, suit));
}

static void test_holdem_river_terminal(void)
{
    EvalConfig config = eval_config_holdem();
    EvalContext *context = eval_context_create(&config);
    const mask_t board = card(MODERN_RANK_2, MODERN_SUIT_CLUBS) |
                         card(MODERN_RANK_7, MODERN_SUIT_DIAMONDS) |
                         card(MODERN_RANK_T, MODERN_SUIT_HEARTS) |
                         card(MODERN_RANK_J, MODERN_SUIT_SPADES) |
                         card(MODERN_RANK_Q, MODERN_SUIT_CLUBS);
    const mask_t hole[] = {
        card(MODERN_RANK_A, MODERN_SUIT_SPADES) |
            card(MODERN_RANK_K, MODERN_SUIT_SPADES),
        card(MODERN_RANK_2, MODERN_SUIT_HEARTS) |
            card(MODERN_RANK_2, MODERN_SUIT_SPADES)};
    const double stacks[] = {5.0, 5.0};
    pe_betting_rules_t rules;
    pe_betting_state_t root;
    pe_betting_vector_game_t game;
    pe_betting_vector_ops_t ops;
    fixture_t fixture;
    pe_best_response_vector_config_t br_config;
    pe_exploitability_vector_result_t result;

    CHECK(context != NULL, "Hold'em context creation");
    if (!context)
        return;
    pe_betting_rules_default(&rules, 2u);
    CHECK(pe_betting_state_init(&root, &rules, stacks, 2u, 0, 10.0, 0.0) ==
              PE_BETTING_OK,
          "river state setup");
    root.invested[0] = 5.0;
    root.invested[1] = 5.0;
    root.round_complete = 1;
    memset(&fixture, 0, sizeof(fixture));
    fixture.spec.context = context;
    fixture.spec.board = board;
    fixture.spec.hole = hole;
    fixture.spec.combo_count = 1u;
    memset(&ops, 0, sizeof(ops));
    ops.action_count = no_actions;
    ops.action_at = no_action;
    ops.infoset_key = terminal_key;
    ops.terminal_values = terminal_values;
    CHECK(pe_betting_vector_game_init(&game, &rules, &root, 1u, &ops,
                                      &fixture) == PE_BETTING_OK,
          "Hold'em river vector adapter setup");
    if (failures)
    {
        eval_context_destroy(context);
        return;
    }
    br_config = pe_best_response_vector_config_default();
    CHECK(pe_exploitability_vector(&game.vector, &br_config, &result) ==
              PE_SOLVER_OK && result.converged &&
              fabs(result.policy_value[0] - 5.0) < 1e-9 &&
              fabs(result.policy_value[1] + 5.0) < 1e-9 &&
              fabs(result.exploitability_raw) < 1e-9,
          "Broadway straight beats pocket trips at zero exploitability");
    pe_betting_vector_game_destroy(&game);
    eval_context_destroy(context);
}

static void test_holdem_river_zero_reach(void)
{
    EvalConfig config = eval_config_holdem();
    EvalContext *context = eval_context_create(&config);
    const mask_t board = card(MODERN_RANK_2, MODERN_SUIT_CLUBS) |
                         card(MODERN_RANK_7, MODERN_SUIT_DIAMONDS) |
                         card(MODERN_RANK_T, MODERN_SUIT_HEARTS) |
                         card(MODERN_RANK_J, MODERN_SUIT_SPADES) |
                         card(MODERN_RANK_Q, MODERN_SUIT_CLUBS);
    const mask_t hole[] = {
        card(MODERN_RANK_A, MODERN_SUIT_SPADES) |
            card(MODERN_RANK_K, MODERN_SUIT_SPADES),
        card(MODERN_RANK_3, MODERN_SUIT_HEARTS) |
            card(MODERN_RANK_3, MODERN_SUIT_SPADES)};
    const double stacks[] = {5.0, 5.0};
    pe_betting_rules_t rules;
    pe_betting_state_t state;
    pe_holdem_river_spec_t spec;
    pe_reach_vec_t reach[2];
    pe_value_vec_t values[2];
    double reach0[] = {1.0};
    double reach1[] = {0.0};
    double values0[] = {123.0};
    double values1[] = {123.0};

    CHECK(context != NULL, "zero-reach context creation");
    if (!context)
        return;
    pe_betting_rules_default(&rules, 2u);
    CHECK(pe_betting_state_init(&state, &rules, stacks, 2u, 0, 10.0, 0.0) ==
              PE_BETTING_OK,
          "zero-reach state setup");
    state.invested[0] = 5.0;
    state.invested[1] = 5.0;
    state.round_complete = 1;
    spec.context = context;
    spec.board = board;
    spec.hole = hole;
    spec.combo_count = 1u;
    reach[0] = pe_vec_wrap(reach0, 1u);
    reach[1] = pe_vec_wrap(reach1, 1u);
    values[0] = pe_vec_wrap(values0, 1u);
    values[1] = pe_vec_wrap(values1, 1u);
    CHECK(pe_holdem_river_terminal_values(&spec, &state, reach, values, 2u) ==
              0 && values0[0] == 0.0 && fabs(values1[0] + 5.0) < 1e-12,
          "zero-reach showdown branch must contribute zero, not fail");
    eval_context_destroy(context);
}

int main(void)
{
    test_holdem_river_terminal();
    test_holdem_river_zero_reach();
    if (failures)
        fprintf(stderr, "test_pe_holdem_river: %d failure(s)\n", failures);
    else
        puts("test_pe_holdem_river: exact terminal passed through vector BR");
    return failures ? 1 : 0;
}
