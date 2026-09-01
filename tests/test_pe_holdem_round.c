#include <poker_eval/solver/pe_holdem_round.h>

#include <stdio.h>

static int failures;

#define CHECK(condition, message)                                      \
    do                                                                 \
    {                                                                  \
        if (!(condition))                                              \
        {                                                              \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,  \
                    message);                                         \
            ++failures;                                                \
        }                                                              \
    } while (0)

static mask_t card(int index)
{
    return mask_set(MASK_EMPTY, index);
}

static pe_action_t simple_action(pe_action_kind_t kind)
{
    pe_action_t action = pe_action_invalid();
    action.kind = kind;
    return action;
}

static void apply_check(const pe_holdem_round_state_t *state,
                        const pe_betting_rules_t *rules,
                        pe_holdem_round_state_t *out)
{
    pe_action_t check = simple_action(PE_ACTION_CHECK);
    CHECK(pe_holdem_round_apply_action(state, rules, &check, out) ==
              PE_HOLDEM_ROUND_OK,
          "check action was not applied");
}

static void test_action_and_street_path(void)
{
    const double stacks[] = {100.0, 100.0};
    const mask_t dead = card(3) | card(4);
    const mask_t flop = card(0) | card(1) | card(2);
    pe_betting_rules_t rules;
    pe_holdem_round_state_t state;
    pe_holdem_round_state_t next;

    pe_betting_rules_default(&rules, 2u);
    rules.min_raise = 10.0;
    CHECK(pe_holdem_round_init(&state, MASK_EMPTY, dead, &rules, stacks, 2u,
                               0, 0.0, 0.0) == PE_HOLDEM_ROUND_OK,
          "preflop round initialization failed");
    apply_check(&state, &rules, &next);
    apply_check(&next, &rules, &state);
    CHECK(state.street == PE_HOLDEM_PREFLOP &&
              state.betting.round_complete && state.betting.to_act == -1,
          "preflop checks did not close the betting round");

    CHECK(pe_holdem_round_advance(&state, &rules, flop, 0, &next) ==
              PE_HOLDEM_ROUND_OK &&
              next.street == PE_HOLDEM_FLOP && next.betting.to_act == 0 &&
              !next.betting.round_complete,
          "preflop-to-flop transition failed");
    {
        pe_action_t bet = pe_action_invalid();
        pe_action_t call = simple_action(PE_ACTION_CALL);
        bet.kind = PE_ACTION_BET;
        bet.amount_kind = PE_AMOUNT_CHIPS;
        bet.amount = 10.0;
        CHECK(pe_holdem_round_apply_action(&next, &rules, &bet, &state) ==
                  PE_HOLDEM_ROUND_OK && state.betting.to_act == 1,
              "flop bet failed");
        CHECK(pe_holdem_round_apply_action(&state, &rules, &call, &next) ==
                  PE_HOLDEM_ROUND_OK && next.betting.round_complete,
              "flop call did not close the round");
    }

    CHECK(pe_holdem_round_advance(&next, &rules, flop | card(5), 0, &state) ==
              PE_HOLDEM_ROUND_OK && state.street == PE_HOLDEM_TURN,
          "flop-to-turn transition failed");
    apply_check(&state, &rules, &next);
    apply_check(&next, &rules, &state);
    CHECK(pe_holdem_round_advance(&state, &rules, flop | card(5) | card(6), 0,
                                 &next) == PE_HOLDEM_ROUND_OK &&
              next.street == PE_HOLDEM_RIVER,
          "turn-to-river transition failed");
    apply_check(&next, &rules, &state);
    apply_check(&state, &rules, &next);
    CHECK(pe_holdem_round_showdown(&next, &rules, &state) ==
              PE_HOLDEM_ROUND_OK && state.street == PE_HOLDEM_SHOWDOWN &&
              state.betting.terminal,
          "river-to-showdown transition failed");
}

static void test_invalid_transition(void)
{
    const double stacks[] = {100.0, 100.0};
    pe_betting_rules_t rules;
    pe_holdem_round_state_t state;
    pe_holdem_round_state_t next;
    pe_betting_rules_default(&rules, 2u);
    CHECK(pe_holdem_round_init(&state, card(0) | card(1) | card(2), card(3),
                               &rules, stacks, 2u, 0, 0.0, 0.0) ==
              PE_HOLDEM_ROUND_OK,
          "flop setup failed");
    state.betting.round_complete = 1;
    state.betting.to_act = -1;
    CHECK(pe_holdem_round_advance(&state, &rules,
                                  card(0) | card(1) | card(2) | card(4) |
                                      card(5),
                                  0, &next) == PE_HOLDEM_ROUND_ERR_INVALID_BOARD,
          "transition skipped a street instead of failing");
}

static void test_all_in_runout(void)
{
    const double stacks[] = {10.0, 10.0};
    const mask_t flop = card(0) | card(1) | card(2);
    const mask_t turn = flop | card(3);
    const mask_t river = turn | card(4);
    pe_betting_rules_t rules;
    pe_holdem_round_state_t state;
    pe_holdem_round_state_t next;
    pe_action_t all_in = simple_action(PE_ACTION_ALL_IN);
    pe_action_t call = simple_action(PE_ACTION_CALL);

    pe_betting_rules_default(&rules, 2u);
    CHECK(pe_holdem_round_init(&state, MASK_EMPTY, MASK_EMPTY, &rules,
                               stacks, 2u, 0, 0.0, 0.0) ==
              PE_HOLDEM_ROUND_OK,
          "all-in runout setup failed");
    CHECK(pe_holdem_round_apply_action(&state, &rules, &all_in, &next) ==
              PE_HOLDEM_ROUND_OK &&
              pe_holdem_round_apply_action(&next, &rules, &call, &state) ==
                  PE_HOLDEM_ROUND_OK && state.betting.round_complete,
          "all-in betting round did not complete");
    CHECK(pe_holdem_round_advance(&state, &rules, flop, 0, &next) ==
              PE_HOLDEM_ROUND_OK && next.betting.round_complete &&
              next.betting.to_act == -1,
          "all-in flop street was reopened for a nonexistent actor");
    CHECK(pe_holdem_round_advance(&next, &rules, turn, 0, &state) ==
              PE_HOLDEM_ROUND_OK && state.betting.round_complete &&
              state.betting.to_act == -1,
          "all-in turn street was not kept complete");
    CHECK(pe_holdem_round_advance(&state, &rules, river, 0, &next) ==
              PE_HOLDEM_ROUND_OK && next.betting.round_complete &&
              next.betting.to_act == -1,
          "all-in river street was not kept complete");
    CHECK(pe_holdem_round_showdown(&next, &rules, &state) ==
              PE_HOLDEM_ROUND_OK && state.street == PE_HOLDEM_SHOWDOWN,
          "all-in runout did not reach showdown");
}

static void test_first_to_act_must_be_eligible(void)
{
    const double stacks[] = {10.0, 10.0};
    const mask_t flop = card(0) | card(1) | card(2);
    pe_betting_rules_t rules;
    pe_holdem_round_state_t state;
    pe_holdem_round_state_t next;

    pe_betting_rules_default(&rules, 2u);
    CHECK(pe_holdem_round_init(&state, MASK_EMPTY, MASK_EMPTY, &rules,
                               stacks, 2u, 0, 0.0, 0.0) ==
              PE_HOLDEM_ROUND_OK,
          "ineligible first actor setup failed");
    state.betting.round_complete = 1;
    state.betting.to_act = -1;
    state.betting.all_in[0] = 1;
    state.betting.stack[0] = 0.0;
    CHECK(pe_holdem_round_advance(&state, &rules, flop, 0, &next) ==
              PE_HOLDEM_ROUND_ERR_INVALID_STATE,
          "ineligible first actor was accepted");
}

int main(void)
{
    test_action_and_street_path();
    test_invalid_transition();
    test_all_in_runout();
    test_first_to_act_must_be_eligible();
    if (failures)
        return 1;
    puts("test_pe_holdem_round: actions and all public streets passed");
    return 0;
}
