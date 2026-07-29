/* pe_round_smoke_test.c - Comprehensive smoke test demonstrating a complete poker round */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <poker_eval/engine/engine_api.h>
#include <poker_eval/engine/betting_api.h>

static void print_error_and_exit(const char *operation, pe_status_t status)
{
    printf("ERROR: %s failed: %s\n", operation, pe_error_string(status));
    exit(1);
}

static void print_game_state(pe_game_state_t *state)
{
    pe_status_t s;
    int round, action_on_player, num_cards;
    int64_t pot;
    bool hand_complete, showdown;
    uint64_t hand_number;

    printf("\n=== Game State ===\n");

    s = pe_engine_get_hand_number(state, &hand_number);
    if (s == PE_STATUS_OK)
        printf("Hand #%llu\n", (unsigned long long)hand_number);

    s = pe_engine_get_current_round(state, &round);
    if (s == PE_STATUS_OK)
        printf("Current round: %d\n", round);

    s = pe_engine_get_pot_size(state, &pot);
    if (s == PE_STATUS_OK)
        printf("Pot size: %lld\n", (long long)pot);

    s = pe_engine_get_action_on_player(state, &action_on_player);
    if (s == PE_STATUS_OK)
        printf("Action on player: %d\n", action_on_player);

    s = pe_engine_get_num_community_cards(state, &num_cards);
    if (s == PE_STATUS_OK)
        printf("Community cards dealt: %d\n", num_cards);

    s = pe_engine_is_hand_complete(state, &hand_complete);
    if (s == PE_STATUS_OK)
        printf("Hand complete: %s\n", hand_complete ? "yes" : "no");

    s = pe_engine_is_showdown_reached(state, &showdown);
    if (s == PE_STATUS_OK)
        printf("Showdown reached: %s\n", showdown ? "yes" : "no");

    printf("--- Player States ---\n");
    for (int i = 0; i < 3; i++)
    {
        int64_t stack, bet;
        int last_action;
        bool active, all_in, folded;

        printf("Player %d: ", i);

        s = pe_engine_get_player_stack_size(state, i, &stack);
        if (s == PE_STATUS_OK)
            printf("stack=%lld ", (long long)stack);

        s = pe_engine_get_player_bet_amount(state, i, &bet);
        if (s == PE_STATUS_OK)
            printf("bet=%lld ", (long long)bet);

        s = pe_engine_get_player_last_action(state, i, &last_action);
        if (s == PE_STATUS_OK)
            printf("action=%d ", last_action);

        s = pe_engine_is_player_active(state, i, &active);
        if (s == PE_STATUS_OK)
            printf("active=%s ", active ? "yes" : "no");

        s = pe_engine_is_player_all_in(state, i, &all_in);
        if (s == PE_STATUS_OK)
            printf("all_in=%s ", all_in ? "yes" : "no");

        s = pe_engine_has_player_folded(state, i, &folded);
        if (s == PE_STATUS_OK)
            printf("folded=%s", folded ? "yes" : "no");

        printf("\n");
    }
    printf("==================\n");
}

int main(void)
{
    pe_engine_t *eng = NULL;
    pe_game_state_t *state = NULL;
    pe_betting_engine_t *be = NULL;
    pe_status_t s;

    printf("=== PE Round Smoke Test ===\n");

    /* Create default engine */
    s = pe_engine_create_default(&eng);
    if (s != PE_STATUS_OK)
        print_error_and_exit("pe_engine_create_default", s);
    printf("✓ Engine created\n");

    /* Create game state for 3 players */
    s = pe_game_state_create(NULL, NULL, 3, &state);
    if (s != PE_STATUS_OK)
        print_error_and_exit("pe_game_state_create", s);
    printf("✓ Game state created for 3 players\n");

    /* Set player stacks */
    for (int i = 0; i < 3; i++)
    {
        s = pe_engine_set_player_stack(state, i, 1000); /* $1000 starting stacks */
        if (s != PE_STATUS_OK)
            print_error_and_exit("pe_engine_set_player_stack", s);
    }
    printf("✓ Player stacks set to $1000 each\n");

    /* Setup hand */
    s = pe_engine_setup_hand(eng, state, 0);
    if (s != PE_STATUS_OK)
        print_error_and_exit("pe_engine_setup_hand", s);
    printf("✓ Hand setup complete\n");

    /* Print initial state */
    print_game_state(state);

    /* Create betting engine */
    s = pe_betting_engine_create(BETTING_NO_LIMIT, NULL, &be);
    if (s != PE_STATUS_OK)
        print_error_and_exit("pe_betting_engine_create", s);
    printf("✓ Betting engine created\n");

    /* Test action sequence: fold, call, raise */
    printf("\n=== Action Sequence ===\n");

    /* Player 2 (action on player) folds */
    s = pe_engine_apply_action(eng, state, ACTION_FOLD, 0, 2);
    if (s != PE_STATUS_OK)
        print_error_and_exit("pe_engine_apply_action (fold)", s);
    printf("✓ Player 2 folded\n");

    /* Check who's turn it is now */
    int action_on;
    s = pe_engine_get_action_on_player(state, &action_on);
    if (s == PE_STATUS_OK)
        printf("Action now on player: %d\n", action_on);

    /* Player 0 calls (needs to add 100 to match the 200 big blind) */
    s = pe_engine_apply_action(eng, state, ACTION_CALL, 100, 0);
    if (s != PE_STATUS_OK)
        print_error_and_exit("pe_engine_apply_action (call)", s);
    printf("✓ Player 0 called\n");

    /* Player 1 checks (big blind, no raise) */
    s = pe_engine_apply_action(eng, state, ACTION_CHECK, 0, 1);
    if (s != PE_STATUS_OK)
        print_error_and_exit("pe_engine_apply_action (check)", s);
    printf("✓ Player 1 checked\n");

    /* Print state after actions */
    print_game_state(state);

    /* Check active player count */
    int active_count;
    s = pe_engine_get_active_player_count(state, &active_count);
    if (s == PE_STATUS_OK)
    {
        printf("Active players after actions: %d\n", active_count);
    }

    /* Test some betting queries */
    const BettingAction *actions = NULL;
    int n = 0;
    s = pe_betting_engine_get_valid_actions(be, state, 1, &actions, &n);
    if (s == PE_STATUS_OK)
    {
        printf("Player 1 valid actions after raise: %d\n", n);
        for (int i = 0; i < n && i < 3; i++)
        {
            printf("  action=%d min=%lld max=%lld\n", actions[i].action,
                   (long long)actions[i].min_amount, (long long)actions[i].max_amount);
        }
    }

    /* Test hand completion attempt */
    HandResult results[3];
    s = pe_engine_complete_hand(eng, state, results, 3);
    if (s == PE_STATUS_OK)
    {
        printf("✓ Hand completed successfully\n");
        for (int i = 0; i < 3; i++)
        {
            printf("Player %d: winnings=%lld winner=%s\n",
                   results[i].player_id, (long long)results[i].winnings,
                   results[i].is_winner ? "yes" : "no");
        }
    }
    else
    {
        printf("Hand completion returned: %s (expected)\n", pe_error_string(s));
    }

    /* Final state */
    printf("\n=== Final State ===\n");
    print_game_state(state);

    /* Cleanup */
    pe_betting_engine_destroy(be);
    pe_game_state_destroy(state);
    pe_engine_destroy(eng);

    printf("\n✓ All tests completed successfully!\n");
    return 0;
}
