/*
 * OpenSpiel-compatible 3/4-player Kuhn poker qualification (ISSUE-09, #165).
 *
 * The game rules mirror open_spiel/games/kuhn_poker/kuhn_poker.cc:
 * N players ante one chip, receive one card from an N+1-card deck, then may
 * pass or bet/call one chip.  The first bet ends the hand after every other
 * player has had one response.  With no bet, the hand ends after N passes.
 *
 * The constants below were generated with OpenSpiel 2.0.2 using a uniform
 * TabularPolicy and its expected_game_score / exploitability.nash_conv APIs.
 */

#include <poker_eval/engine/solvers/cfr/cfr_core.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define KM_MAX_PLAYERS 4
#define KM_CARD_BITS 3
#define KM_HISTORY_SHIFT 12
#define KM_COUNT_SHIFT 19
#define KM_DEALT_SHIFT 22
#define KM_PLAYERS_SHIFT 23

#define CHECK(cond, msg)                                                   \
    do                                                                     \
    {                                                                      \
        if (!(cond))                                                       \
        {                                                                  \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
            return 1;                                                      \
        }                                                                  \
    } while (0)

#define CHECK_CLOSE(actual, expected, tolerance, msg)                         \
    do                                                                         \
    {                                                                          \
        if (fabs((actual) - (expected)) > (tolerance))                          \
        {                                                                      \
            fprintf(stderr, "FAIL: %s (got %.12g want %.12g tol %.3g)\n",     \
                    msg, (double)(actual), (double)(expected),                  \
                    (double)(tolerance));                                      \
            return 1;                                                          \
        }                                                                      \
    } while (0)

typedef struct
{
    int players;
    int chance_outcomes;
    double policy_value[KM_MAX_PLAYERS];
    double improvement[KM_MAX_PLAYERS];
    double nash_conv;
} km_open_spiel_reference_t;

static const km_open_spiel_reference_t KM_REFERENCES[] = {
    {3, 24,
     {15.0 / 64.0, -3.0 / 64.0, -3.0 / 16.0, 0.0},
     {35.0 / 64.0, 133.0 / 192.0, 79.0 / 96.0, 0.0},
     33.0 / 16.0},
    {4, 120,
     {119.0 / 384.0, 7.0 / 384.0, -49.0 / 384.0, -77.0 / 384.0},
     {265.0 / 384.0, 1589.0 / 1920.0, 603.0 / 640.0,
      1951.0 / 1920.0},
     3337.0 / 960.0}
};

static int km_players(uint64_t state)
{
    return (int)((state >> KM_PLAYERS_SHIFT) & 7u);
}

static int km_action_count(uint64_t state)
{
    return (int)((state >> KM_COUNT_SHIFT) & 7u);
}

static int km_action_at(uint64_t state, int index)
{
    return (int)((state >> (KM_HISTORY_SHIFT + index)) & 1u);
}

static int km_card(uint64_t state, int player)
{
    return (int)((state >> (player * KM_CARD_BITS)) & 7u);
}

static int km_first_bettor(uint64_t state)
{
    const int count = km_action_count(state);
    for (int i = 0; i < count; ++i)
        if (km_action_at(state, i) != 0)
            return i;
    return -1;
}

static int km_current_player(cfr_game_t *game, uint64_t state, void *user_data)
{
    (void)user_data;
    return km_action_count(state) % game->num_players;
}

static int km_is_terminal(cfr_game_t *game, uint64_t state, void *user_data)
{
    (void)user_data;
    if (((state >> KM_DEALT_SHIFT) & 1u) == 0u)
        return 0;

    const int count = km_action_count(state);
    const int bettor = km_first_bettor(state);
    if (bettor < 0)
        return count == game->num_players;
    return count == game->num_players + bettor;
}

static double km_get_utility(cfr_game_t *game, uint64_t state, int player,
                             void *user_data)
{
    (void)user_data;
    if (!km_is_terminal(game, state, NULL))
        return 0.0;

    int contribution[KM_MAX_PLAYERS] = {1, 1, 1, 1};
    int active[KM_MAX_PLAYERS] = {1, 1, 1, 1};
    const int bettor = km_first_bettor(state);
    const int count = km_action_count(state);
    if (bettor >= 0)
    {
        memset(active, 0, sizeof(active));
        for (int i = bettor; i < count; ++i)
        {
            const int acting_player = i % game->num_players;
            active[acting_player] = km_action_at(state, i);
            if (active[acting_player])
                contribution[acting_player] = 2;
        }
    }

    int winner = -1;
    int winning_card = -1;
    int pot = 0;
    for (int p = 0; p < game->num_players; ++p)
    {
        pot += contribution[p];
        if (active[p] && km_card(state, p) > winning_card)
        {
            winning_card = km_card(state, p);
            winner = p;
        }
    }

    if (player == winner)
        return (double)(pot - contribution[player]);
    return -(double)contribution[player];
}

static int km_get_actions(cfr_game_t *game, uint64_t state, int *out_actions,
                          int max_actions, void *user_data)
{
    (void)user_data;
    if (max_actions < 2 || km_is_terminal(game, state, NULL) ||
        ((state >> KM_DEALT_SHIFT) & 1u) == 0u)
        return 0;
    out_actions[0] = 0;
    out_actions[1] = 1;
    return 2;
}

static uint64_t km_apply_action(cfr_game_t *game, uint64_t state, int action,
                                void *user_data)
{
    (void)game;
    (void)user_data;
    const int count = km_action_count(state);
    if (action != 0)
        state |= 1ULL << (KM_HISTORY_SHIFT + count);
    state &= ~(7ULL << KM_COUNT_SHIFT);
    state |= (uint64_t)(count + 1) << KM_COUNT_SHIFT;
    return state;
}

static uint64_t km_infoset_key(const void *opaque_state)
{
    const uint64_t state = (uint64_t)(uintptr_t)opaque_state;
    if (((state >> KM_DEALT_SHIFT) & 1u) == 0u)
        return 0;

    const int players = km_players(state);
    const int count = km_action_count(state);
    const int player = count % players;
    const uint64_t history =
        (state >> KM_HISTORY_SHIFT) & ((1ULL << (2 * players - 1)) - 1ULL);
    return (1ULL << 60) | ((uint64_t)players << 56) |
           ((uint64_t)player << 52) | ((uint64_t)km_card(state, player) << 48) |
           ((uint64_t)count << 8) | history;
}

static int km_is_chance(cfr_game_t *game, uint64_t state, void *user_data)
{
    (void)game;
    (void)user_data;
    return ((state >> KM_DEALT_SHIFT) & 1u) == 0u;
}

static int km_factorial(int value)
{
    int result = 1;
    for (int i = 2; i <= value; ++i)
        result *= i;
    return result;
}

static int km_chance_outcomes(cfr_game_t *game, uint64_t state,
                              void *user_data)
{
    (void)state;
    (void)user_data;
    return km_factorial(game->num_players + 1);
}

static uint64_t km_apply_chance(cfr_game_t *game, uint64_t state, int outcome,
                                void *user_data)
{
    (void)state;
    (void)user_data;
    int deck[KM_MAX_PLAYERS + 1] = {0, 1, 2, 3, 4};
    uint64_t dealt = (1ULL << KM_DEALT_SHIFT) |
                     ((uint64_t)game->num_players << KM_PLAYERS_SHIFT);
    int code = outcome;
    for (int player = 0; player < game->num_players; ++player)
    {
        const int remaining = game->num_players + 1 - player;
        const int selected = code % remaining;
        code /= remaining;
        dealt |= (uint64_t)deck[selected] << (player * KM_CARD_BITS);
        for (int i = selected; i + 1 < remaining; ++i)
            deck[i] = deck[i + 1];
    }
    return dealt;
}

static void km_init_game(cfr_game_t *game, int players)
{
    memset(game, 0, sizeof(*game));
    game->current_player = km_current_player;
    game->is_terminal = km_is_terminal;
    game->get_utility = km_get_utility;
    game->get_actions = km_get_actions;
    game->apply_action = km_apply_action;
    game->get_infoset_key = km_infoset_key;
    game->is_chance = km_is_chance;
    game->get_chance_outcomes = km_chance_outcomes;
    game->apply_chance = km_apply_chance;
    game->initial_state = (void *)(uintptr_t)0;
    game->state_size = sizeof(uint64_t);
    game->num_players = players;
}

static int km_test_reference(const km_open_spiel_reference_t *reference)
{
    cfr_game_t game;
    km_init_game(&game, reference->players);
    CHECK(game.get_chance_outcomes(&game, 0, NULL) ==
              reference->chance_outcomes,
          "OpenSpiel deal count");

    cfr_storage_t *storage = cfr_storage_create();
    CHECK(storage != NULL, "storage allocation");

    cfr_policy_value_result_t policy;
    CHECK(cfr_compute_policy_values_detailed(&game, storage, NULL, &policy) == 0,
          "uniform policy evaluation");

    cfr_multiway_audit_result_t audit;
    CHECK(cfr_audit_multiway(&game, storage, NULL, &audit) == 0,
          "uniform infoset-consistent exploitability audit");

    double nash_conv = 0.0;
    double value_sum = 0.0;
    for (int player = 0; player < reference->players; ++player)
    {
        CHECK_CLOSE(policy.ev[player], reference->policy_value[player], 1e-12,
                    "OpenSpiel uniform policy value");
        CHECK_CLOSE(audit.max_player_exploitability[player],
                    reference->improvement[player], 1e-12,
                    "OpenSpiel uniform unilateral improvement");
        nash_conv += audit.max_player_exploitability[player];
        value_sum += policy.ev[player];
    }
    CHECK_CLOSE(value_sum, 0.0, 1e-12, "zero-sum policy values");
    CHECK_CLOSE(nash_conv, reference->nash_conv, 1e-12,
                "OpenSpiel uniform NashConv");

    printf("  %d-player Kuhn: policy and NashConv %.12g match OpenSpiel\n",
           reference->players, nash_conv);
    cfr_storage_destroy(storage);
    return 0;
}

int main(void)
{
    printf("OpenSpiel multiway Kuhn qualification (ISSUE-09)\n");
    for (size_t i = 0; i < sizeof(KM_REFERENCES) / sizeof(KM_REFERENCES[0]); ++i)
        if (km_test_reference(&KM_REFERENCES[i]) != 0)
            return 1;
    printf("All OpenSpiel multiway Kuhn checks passed.\n");
    return 0;
}
