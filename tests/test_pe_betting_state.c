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
    pe_action_t check = action(PE_ACTION_CHECK, PE_AMOUNT_NONE, 0.0);

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

    setup(&state, &rules, 100.0, 100.0);
    state.to_act = 1;
    state.to_call = 25.0;
    state.current_bet = 25.0;
    state.round_contrib[1] = 25.0;
    state.invested[1] = 25.0;
    state.pot = 25.0;
    CHECK(pe_betting_action_is_legal(&state, &rules, &check) ==
              PE_BETTING_OK,
          "check is legal when the actor already matched the target");

    {
        pe_action_t raise = action(PE_ACTION_RAISE, PE_AMOUNT_CHIPS, 25.0);
        setup(&state, &rules, 100.0, 100.0);
        CHECK(pe_betting_apply_action(&state, &rules, &bet, &next) ==
                  PE_BETTING_OK &&
                  pe_betting_apply_action(&next, &rules, &raise, &state) ==
                  PE_BETTING_OK && fabs(state.round_contrib[1] - 50.0) <
                  1e-12,
              "raise reaches the requested total contribution");
        CHECK(pe_betting_apply_action(&state, &rules, &call, &next) ==
                  PE_BETTING_OK && next.round_complete &&
                  fabs(next.round_contrib[0] - 50.0) < 1e-12 &&
                  fabs(next.stack[0] - 50.0) < 1e-12 &&
                  fabs(next.pot - 100.0) < 1e-12,
              "call charges only the outstanding contribution");

        setup(&state, &rules, 100.0, 100.0);
        CHECK(pe_betting_apply_action(&state, &rules, &bet, &next) ==
                  PE_BETTING_OK &&
                  pe_betting_apply_action(&next, &rules, &raise, &state) ==
                  PE_BETTING_OK &&
                  pe_betting_apply_action(&state, &rules, &raise, &next) ==
                  PE_BETTING_OK && !next.all_in[0] &&
                  fabs(next.round_contrib[0] - 75.0) < 1e-12 &&
                  fabs(next.stack[0] - 25.0) < 1e-12 &&
                  fabs(next.to_call - 75.0) < 1e-12,
              "re-raise charges only the outstanding contribution");
    }
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
              fabs(next.to_call - 35.0) < 1e-12 && !next.round_complete &&
              next.to_act == 1,
          "unmatched all-in leaves the covering player a decision");
    {
        pe_action_t call = action(PE_ACTION_CALL, PE_AMOUNT_NONE, 0.0);
        pe_action_t raise = action(PE_ACTION_RAISE, PE_AMOUNT_CHIPS, 50.0);
        CHECK(pe_betting_action_is_legal(&next, &rules, &raise) ==
                  PE_BETTING_ERR_ILLEGAL_ACTION,
              "short all-in does not reopen a raise for an acted player");
        CHECK(pe_betting_apply_action(&next, &rules, &call, &state) ==
                  PE_BETTING_OK && state.round_complete && state.to_act == -1 &&
                  fabs(state.stack[1] - 90.0) < 1e-12 &&
                  fabs(state.pot - 70.0) < 1e-12,
              "covering player pays only the unmatched all-in amount");
    }

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

static void test_short_stack_call_is_not_an_all_in_call(void)
{
    pe_betting_rules_t rules;
    pe_betting_state_t state;
    pe_action_t call = action(PE_ACTION_CALL, PE_AMOUNT_NONE, 0.0);

    setup(&state, &rules, 5.0, 100.0);
    state.to_call = 10.0;
    state.current_bet = 10.0;
    state.round_contrib[1] = 10.0;
    state.invested[1] = 10.0;
    state.pot = 10.0;
    state.acted[1] = 1;
    CHECK(pe_betting_state_validate(&state, &rules) == PE_BETTING_OK,
          "short stack call setup");
    CHECK(pe_betting_action_is_legal(&state, &rules, &call) ==
              PE_BETTING_ERR_ILLEGAL_ACTION,
          "ordinary call cannot exceed the remaining stack");
}

int main(void)
{
    test_bet_call_and_round_end();
    test_fold_terminal();
    test_all_in_and_reopen_rule();
    test_short_stack_call_is_not_an_all_in_call();
    if (failures)
        fprintf(stderr, "test_pe_betting_state: %d failure(s)\n", failures);
    else
        puts("test_pe_betting_state: generic betting state passed");
    return failures ? 1 : 0;
}
