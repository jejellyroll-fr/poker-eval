/* BR-02: three-player Kuhn-style private-information parity gate. */

#include <poker_eval/engine/solvers/cfr/cfr_core.h>
#include <poker_eval/solver/pe_best_response.h>
#include <poker_eval/solver/pe_traversal.h>

#include <math.h>
#include <stdint.h>
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

/* Four-card, three-player Kuhn-style deal. Each player may stay or fold once;
 * the highest surviving card wins the three-ante pot. The game is deliberately
 * small, but it has private cards, 24 chance deals, multiway utilities and
 * infosets that aggregate hidden opponent cards. */
#define K3_TURN_END 3

typedef struct
{
    cfr_game_t cfr;
    cfr_storage_t *storage;
    pe_vector_game_t vector;
} k3_adapter_t;

static uint64_t k3_key(int p0, int p1, int p2, int turn, int folded)
{
    return (uint64_t)(p0 & 7) | ((uint64_t)(p1 & 7) << 3) |
           ((uint64_t)(p2 & 7) << 6) | ((uint64_t)(turn & 3) << 9) |
           ((uint64_t)(folded & 7) << 11);
}

static void k3_unpack(uint64_t key, int *p0, int *p1, int *p2, int *turn,
                      int *folded)
{
    *p0 = (int)(key & 7);
    *p1 = (int)((key >> 3) & 7);
    *p2 = (int)((key >> 6) & 7);
    *turn = (int)((key >> 9) & 3);
    *folded = (int)((key >> 11) & 7);
}

static int k3_current_player(cfr_game_t *game, uint64_t key, void *user)
{
    int p0, p1, p2, turn, folded;
    (void)game;
    (void)user;
    k3_unpack(key, &p0, &p1, &p2, &turn, &folded);
    return turn < K3_TURN_END ? turn : -1;
}

static int k3_is_terminal(cfr_game_t *game, uint64_t key, void *user)
{
    int p0, p1, p2, turn, folded;
    (void)game;
    (void)user;
    if (key == 0)
        return 0;
    k3_unpack(key, &p0, &p1, &p2, &turn, &folded);
    return turn == K3_TURN_END;
}

static int k3_get_actions(cfr_game_t *game, uint64_t key, int *out,
                          int max_actions, void *user)
{
    int p0, p1, p2, turn, folded;
    int folded_count;
    (void)game;
    (void)user;
    if (max_actions < 1)
        return 0;
    k3_unpack(key, &p0, &p1, &p2, &turn, &folded);
    if (turn >= K3_TURN_END)
        return 0;
    folded_count = ((folded >> 0) & 1) + ((folded >> 1) & 1) +
                   ((folded >> 2) & 1);
    out[0] = 0; /* stay */
    if (folded_count == 2)
        return 1;
    if (max_actions < 2)
        return 0;
    out[1] = 1; /* fold */
    return 2;
}

static uint64_t k3_apply_action(cfr_game_t *game, uint64_t key, int action,
                                void *user)
{
    int p0, p1, p2, turn, folded;
    (void)game;
    (void)user;
    if (action < 0 || action > 1)
        return 0;
    k3_unpack(key, &p0, &p1, &p2, &turn, &folded);
    if (turn >= K3_TURN_END)
        return 0;
    if (action == 1)
        folded |= 1 << turn;
    return k3_key(p0, p1, p2, turn + 1, folded);
}

static uint64_t k3_infoset_key(const void *state)
{
    uint64_t key = (uint64_t)(uintptr_t)state;
    int p0, p1, p2, turn, folded;
    int private_card;
    if (key == 0)
        return 0;
    k3_unpack(key, &p0, &p1, &p2, &turn, &folded);
    if (turn == K3_TURN_END)
        return (UINT64_C(1) << 60) | key;
    private_card = turn == 0 ? p0 : turn == 1 ? p1 : p2;
    return (UINT64_C(1) << 60) | (uint64_t)private_card |
           ((uint64_t)turn << 4) | ((uint64_t)folded << 6);
}

static int k3_is_chance(cfr_game_t *game, uint64_t key, void *user)
{
    (void)game;
    (void)user;
    return key == 0;
}

static int k3_chance_outcomes(cfr_game_t *game, uint64_t key, void *user)
{
    (void)game;
    (void)key;
    (void)user;
    return 24;
}

static uint64_t k3_apply_chance(cfr_game_t *game, uint64_t key, int outcome,
                                void *user)
{
    int cards[4] = {0, 1, 2, 3};
    int i;
    int rank0, rank1, rank2;
    (void)game;
    (void)user;
    if (key != 0 || outcome < 0 || outcome >= 24)
        return 0;
    /* Lehmer-style unranking of the 24 ordered distinct triples. */
    i = outcome / 6;
    rank0 = cards[i];
    cards[i] = cards[3];
    rank1 = cards[(outcome % 6) / 2];
    cards[(outcome % 6) / 2] = cards[2];
    rank2 = cards[outcome % 2];
    return k3_key(rank0, rank1, rank2, 0, 0);
}

static double k3_get_utility(cfr_game_t *game, uint64_t key, int player,
                             void *user)
{
    int cards[3];
    int turn, folded;
    int active = 0;
    int winner = -1;
    int p;
    double value;
    (void)game;
    (void)user;
    k3_unpack(key, &cards[0], &cards[1], &cards[2], &turn, &folded);
    for (p = 0; p < 3; ++p)
        if ((folded & (1 << p)) == 0)
        {
            active++;
            if (winner < 0 || cards[p] > cards[winner])
                winner = p;
        }
    if (player == winner)
        value = 3.0 / (double)active - 1.0;
    else
        value = -1.0;
    return value;
}

static uint64_t k3_state_key(const k3_adapter_t *adapter, const void *state)
{
    return state == adapter ? 0 : (uint64_t)(uintptr_t)state;
}

static int k3_v_terminal(const void *state, void *user)
{
    k3_adapter_t *adapter = (k3_adapter_t *)user;
    return k3_is_terminal(&adapter->cfr, k3_state_key(adapter, state), user);
}

static int k3_v_player(const void *state, void *user)
{
    k3_adapter_t *adapter = (k3_adapter_t *)user;
    return k3_current_player(&adapter->cfr, k3_state_key(adapter, state), user);
}

static uint16_t k3_v_actions(const void *state, void *user)
{
    k3_adapter_t *adapter = (k3_adapter_t *)user;
    int actions[2];
    return (uint16_t)k3_get_actions(&adapter->cfr,
                                    k3_state_key(adapter, state), actions, 2,
                                    user);
}

static uint64_t k3_v_infoset(const void *state, void *user)
{
    k3_adapter_t *adapter = (k3_adapter_t *)user;
    return k3_infoset_key((const void *)(uintptr_t)k3_state_key(adapter, state));
}

static int k3_v_strategy(const void *state, uint64_t infoset, uint16_t action,
                         pe_value_vec_t *out, void *user)
{
    k3_adapter_t *adapter = (k3_adapter_t *)user;
    double strategy[2];
    int actions[2];
    int action_count = k3_get_actions(&adapter->cfr,
                                      k3_state_key(adapter, state), actions, 2,
                                      user);
    if (!out || out->n != 1 || action >= (uint16_t)action_count)
        return -1;
    cfr_storage_get_avg_strategy(adapter->storage, infoset, action_count,
                                 strategy);
    out->v[0] = strategy[action];
    return 0;
}

static const void *k3_v_apply_action(const void *state, uint16_t action,
                                     void *user)
{
    k3_adapter_t *adapter = (k3_adapter_t *)user;
    uint64_t child = k3_apply_action(&adapter->cfr,
                                     k3_state_key(adapter, state), action,
                                     user);
    return child ? (const void *)(uintptr_t)child : NULL;
}

static int k3_v_terminal_values(const void *state, const pe_reach_vec_t *reach,
                                pe_value_vec_t *out, uint8_t player_count,
                                void *user)
{
    k3_adapter_t *adapter = (k3_adapter_t *)user;
    uint64_t key = k3_state_key(adapter, state);
    uint8_t player;
    (void)reach;
    if (!out || player_count != 3)
        return -1;
    for (player = 0; player < player_count; ++player)
        out[player].v[0] = k3_get_utility(&adapter->cfr, key, player, user);
    return 0;
}

static int k3_v_chance(const void *state, void *user)
{
    k3_adapter_t *adapter = (k3_adapter_t *)user;
    return k3_is_chance(&adapter->cfr, k3_state_key(adapter, state), user);
}

static uint16_t k3_v_chance_count(const void *state, void *user)
{
    k3_adapter_t *adapter = (k3_adapter_t *)user;
    return (uint16_t)k3_chance_outcomes(&adapter->cfr,
                                        k3_state_key(adapter, state), user);
}

static const void *k3_v_apply_chance(const void *state, int outcome, void *user)
{
    k3_adapter_t *adapter = (k3_adapter_t *)user;
    uint64_t child = k3_apply_chance(&adapter->cfr,
                                     k3_state_key(adapter, state), outcome,
                                     user);
    return child ? (const void *)(uintptr_t)child : NULL;
}

static void k3_init(k3_adapter_t *adapter)
{
    memset(adapter, 0, sizeof(*adapter));
    adapter->cfr.current_player = k3_current_player;
    adapter->cfr.is_terminal = k3_is_terminal;
    adapter->cfr.get_utility = k3_get_utility;
    adapter->cfr.get_actions = k3_get_actions;
    adapter->cfr.apply_action = k3_apply_action;
    adapter->cfr.get_infoset_key = k3_infoset_key;
    adapter->cfr.is_chance = k3_is_chance;
    adapter->cfr.get_chance_outcomes = k3_chance_outcomes;
    adapter->cfr.apply_chance = k3_apply_chance;
    adapter->cfr.initial_state = (void *)(uintptr_t)0;
    adapter->cfr.state_size = sizeof(uint64_t);
    adapter->cfr.num_players = 3;
    adapter->storage = cfr_storage_create();

    adapter->vector.root = adapter;
    adapter->vector.user = adapter;
    adapter->vector.player_count = 3;
    adapter->vector.combo_count = 1;
    adapter->vector.is_chance = k3_v_chance;
    adapter->vector.chance_outcome_count = k3_v_chance_count;
    adapter->vector.apply_chance = k3_v_apply_chance;
    adapter->vector.is_terminal = k3_v_terminal;
    adapter->vector.acting_player = k3_v_player;
    adapter->vector.action_count = k3_v_actions;
    adapter->vector.infoset_key = k3_v_infoset;
    adapter->vector.strategy = k3_v_strategy;
    adapter->vector.apply_action = k3_v_apply_action;
    adapter->vector.terminal_values = k3_v_terminal_values;
}

int main(void)
{
    k3_adapter_t adapter;
    pe_best_response_vector_config_t config;
    pe_best_response_vector_result_t result;
    double scalar_value;

    k3_init(&adapter);
    CHECK(adapter.storage != NULL, "Kuhn 3p scalar storage allocation");
    if (!adapter.storage)
        return 1;
    config = pe_best_response_vector_config_default();
    CHECK(pe_best_response_vector(&adapter.vector, 0u, &config, &result) ==
              PE_SOLVER_OK,
          "Kuhn 3p vector best response failed");
    scalar_value = cfr_best_response_value_infoset(
        &adapter.cfr, adapter.storage, 0, NULL);
    CHECK(result.converged, "Kuhn 3p vector best response did not converge");
    CHECK(result.infosets > 0u, "Kuhn 3p vector BR found no infosets");
    CHECK(fabs(result.value - scalar_value) <= 1e-9,
          "Kuhn 3p vector/scalar mismatch: %.17g vs %.17g", result.value,
          scalar_value);
    cfr_storage_destroy(adapter.storage);
    if (failures)
        return 1;
    puts("test_best_response_ii_multiway: Kuhn 3p parity passed");
    return 0;
}
