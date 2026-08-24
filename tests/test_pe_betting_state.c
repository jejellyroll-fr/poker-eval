#include <poker_eval/solver/pe_betting_state.h>

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

static pe_action_t action(pe_action_kind_t kind, pe_amount_kind_t amount_kind,
                          double amount)
{
    pe_action_t out = pe_action_invalid();
    out.kind = kind;
    out.amount_kind = amount_kind;
    out.amount = amount;
    return out;
}

static void setup(pe_betting_state_t *state, pe_betting_rules_t *rules,
                  double first_stack, double second_stack)
{
    const double stacks[] = {first_stack, second_stack};
    pe_betting_rules_default(rules, 2u);
    rules->min_raise = 25.0;
    CHECK(pe_betting_state_init(state, rules, stacks, 2u, 0, 0.0, 0.0) ==
              PE_BETTING_OK,
          "state setup");
}

static void test_bet_call_and_round_end(void)
{
    pe_betting_rules_t rules;
    pe_betting_state_t state;
    pe_betting_state_t next;
    pe_action_t bet = action(PE_ACTION_BET, PE_AMOUNT_CHIPS, 25.0);
    pe_action_t call = action(PE_ACTION_CALL, PE_AMOUNT_NONE, 0.0);

    setup(&state, &rules, 100.0, 100.0);
    CHECK(pe_betting_apply_action(&state, &rules, &bet, &next) ==
              PE_BETTING_OK && next.to_act == 1 &&
              fabs(next.to_call - 25.0) < 1e-12 &&
              fabs(next.stack[0] - 75.0) < 1e-12,
          "bet transition");
    CHECK(pe_betting_apply_action(&next, &rules, &call, &state) ==
              PE_BETTING_OK && state.round_complete && state.to_act == -1 &&
              fabs(state.stack[1] - 75.0) < 1e-12 &&
              fabs(state.pot - 50.0) < 1e-12,
          "call closes a matched round");
}

static void test_fold_terminal(void)
{
    pe_betting_rules_t rules;
    pe_betting_state_t state;
    pe_betting_state_t next;
    pe_action_t fold = action(PE_ACTION_FOLD, PE_AMOUNT_NONE, 0.0);

    setup(&state, &rules, 100.0, 100.0);
    CHECK(pe_betting_apply_action(&state, &rules, &fold, &next) ==
              PE_BETTING_OK && next.terminal && next.winner == 1 &&
              next.to_act == -1,
          "fold terminal");
    CHECK(pe_betting_apply_action(&next, &rules, &fold, &state) ==
              PE_BETTING_ERR_TERMINAL,
          "terminal state rejects another action");
}

static void test_all_in_and_reopen_rule(void)
{
    pe_betting_rules_t rules;
    pe_betting_state_t state;
    pe_betting_state_t next;
    pe_action_t all_in = action(PE_ACTION_ALL_IN, PE_AMOUNT_NONE, 0.0);

    setup(&state, &rules, 35.0, 100.0);
    state.to_call = 25.0;
    state.current_bet = 25.0;
    state.min_raise = 25.0;
    state.round_contrib[1] = 25.0;
    state.invested[1] = 25.0;
    state.pot = 25.0;
    state.acted[1] = 1;
    CHECK(pe_betting_state_validate(&state, &rules) == PE_BETTING_OK,
          "short all-in setup");
    CHECK(pe_betting_apply_action(&state, &rules, &all_in, &next) ==
              PE_BETTING_OK && next.all_in[0] &&
              fabs(next.to_call - 35.0) < 1e-12 && next.round_complete,
          "short all-in advances the call without reopening a one-player round");

    setup(&state, &rules, 60.0, 100.0);
    state.to_call = 25.0;
    state.current_bet = 25.0;
    state.min_raise = 25.0;
    state.round_contrib[1] = 25.0;
    state.invested[1] = 25.0;
    state.pot = 25.0;
    state.acted[1] = 1;
    CHECK(pe_betting_apply_action(&state, &rules, &all_in, &next) ==
              PE_BETTING_OK && next.all_in[0] && next.acted[1] == 0 &&
              fabs(next.min_raise - 35.0) < 1e-12,
          "full all-in raise reopens action and updates minimum");
}

int main(void)
{
    test_bet_call_and_round_end();
    test_fold_terminal();
    test_all_in_and_reopen_rule();
    if (failures)
        fprintf(stderr, "test_pe_betting_state: %d failure(s)\n", failures);
    else
        puts("test_pe_betting_state: generic betting state passed");
    return failures ? 1 : 0;
}
