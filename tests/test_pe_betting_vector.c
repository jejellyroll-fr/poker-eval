#include <poker_eval/solver/pe_betting_vector.h>
#include <poker_eval/solver/pe_best_response.h>

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

static uint16_t fixture_action_count(const pe_betting_state_t *state,
                                     void *user)
{
    (void)state;
    (void)user;
    return 2u;
}

static pe_action_status_t fixture_action_at(const pe_betting_state_t *state,
                                            uint16_t action, pe_action_t *out,
                                            void *user)
{
    (void)user;
    if (!out || action > 1u)
        return PE_ACTION_ERR_OUT_OF_RANGE;
    *out = pe_action_invalid();
    if (state->to_call <= 1e-9)
    {
        out->kind = action == 0u ? PE_ACTION_CHECK : PE_ACTION_BET;
        out->amount_kind = action == 0u ? PE_AMOUNT_NONE : PE_AMOUNT_CHIPS;
        out->amount = action == 0u ? 0.0 : 25.0;
    }
    else
    {
        out->kind = action == 0u ? PE_ACTION_FOLD : PE_ACTION_CALL;
    }
    return PE_ACTION_OK;
}

static uint64_t fixture_infoset_key(const pe_betting_state_t *state,
                                    void *user)
{
    (void)user;
    return ((uint64_t)(uint8_t)state->to_act << 32) |
           (state->to_call > 1e-9 ? UINT64_C(1) : UINT64_C(0));
}

static int fixture_strategy(const pe_betting_state_t *state,
                            uint64_t infoset_key, uint16_t action,
                            pe_value_vec_t *out, void *user)
{
    size_t combo;
    (void)state;
    (void)infoset_key;
    (void)user;
    if (action > 1u)
        return -1;
    for (combo = 0u; combo < out->n; ++combo)
        out->v[combo] = 0.5;
    return 0;
}

static int fixture_terminal_values(const pe_betting_state_t *state,
                                   const pe_reach_vec_t *reach,
                                   pe_value_vec_t *out_values,
                                   uint8_t player_count, void *user)
{
    size_t combo;
    (void)reach;
    (void)user;
    if (player_count != 2u)
        return -1;
    for (combo = 0u; combo < out_values[0].n; ++combo)
    {
        double p0;
        if (state->winner == 0)
            p0 = state->pot - state->invested[0];
        else if (state->winner == 1)
            p0 = -state->invested[0];
        else
            p0 = combo == 0u ? state->pot - state->invested[0]
                            : -state->invested[0];
        out_values[0].v[combo] = p0;
        out_values[1].v[combo] = -p0;
    }
    return 0;
}

static void test_vector_adapter(void)
{
    pe_betting_rules_t rules;
    pe_betting_state_t root;
    pe_betting_vector_game_t adapter;
    pe_betting_vector_ops_t ops;
    pe_traversal_ctx_t traversal;
    pe_update_batch_t batch = {0};
    pe_best_response_vector_config_t br_config;
    pe_exploitability_vector_result_t exploitability;
    const pe_traversal_ops_t *traversal_ops = pe_traversal_full_vector_ops();
    const double stacks[] = {100.0, 100.0};

    pe_betting_rules_default(&rules, 2u);
    rules.min_raise = 25.0;
    CHECK(pe_betting_state_init(&root, &rules, stacks, 2u, 0, 0.0, 0.0) ==
              PE_BETTING_OK,
          "root setup");
    memset(&ops, 0, sizeof(ops));
    ops.action_count = fixture_action_count;
    ops.action_at = fixture_action_at;
    ops.infoset_key = fixture_infoset_key;
    ops.strategy = fixture_strategy;
    ops.terminal_values = fixture_terminal_values;
    CHECK(pe_betting_vector_game_init(&adapter, &rules, &root, 2u, &ops,
                                      NULL) == PE_BETTING_OK,
          "vector adapter init");
    if (failures)
        return;

    CHECK(adapter.vector.root == &adapter.root &&
              adapter.vector.player_count == 2u &&
              adapter.vector.combo_count == 2u,
          "vector view exposes root dimensions");
    CHECK(pe_traversal_ctx_init(&traversal, &adapter.vector) == 0,
          "vector traversal init");
    CHECK(traversal_ops->begin_iteration(&traversal, 0u) == 0 &&
              traversal_ops->run_iteration(&traversal, &batch) == 0 &&
              traversal.terminal_nodes > 0u,
          "generic betting state traverses through vector lane");
    pe_update_batch_destroy(&batch);
    pe_traversal_ctx_destroy(&traversal);
    CHECK(adapter.owned_count > 0u, "adapter allocated child states");
    br_config = pe_best_response_vector_config_default();
    br_config.max_iterations = 8u;
    CHECK(pe_exploitability_vector(&adapter.vector, &br_config,
                                   &exploitability) == PE_SOLVER_OK &&
              exploitability.converged &&
              isfinite(exploitability.exploitability_raw),
          "vector adapter reaches information-set exploitability");
    pe_betting_vector_game_destroy(&adapter);
    CHECK(adapter.owned_states == NULL && adapter.owned_count == 0u,
          "adapter releases child states");
}

int main(void)
{
    test_vector_adapter();
    if (failures)
        fprintf(stderr, "test_pe_betting_vector: %d failure(s)\n", failures);
    else
        puts("test_pe_betting_vector: vector adapter passed");
    return failures ? 1 : 0;
}
