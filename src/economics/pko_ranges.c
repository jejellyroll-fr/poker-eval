#include <poker_eval/economics/pko.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../solver/domain/finite_double.h"

typedef struct {
    const pe_pko_range_input_t *input;
    pe_pko_range_profile_t *profiles;
    size_t count;
    size_t capacity;
} pko_enum_t;

static void enumerate(pko_enum_t *state, int player, StdDeck_CardMask used,
                      double weight, pe_pko_range_profile_t *profile)
{
    const pe_range_view_t *range;
    if (state->count >= state->capacity)
        return;
    if (player == state->input->base.icm.num_players) {
        profile->weight = weight;
        state->profiles[state->count++] = *profile;
        return;
    }
    range = &state->input->ranges[player];
    for (size_t i = 0; i < range->count; ++i) {
        const pe_combo_t *combo = &range->combos[i];
        if (combo->weight <= 0.0 || StdDeck_CardMask_ANY_SET(used, combo->hand))
            continue;
        profile->combo_index[player] = (uint16_t)i;
        profile->hand[player] = combo->hand;
        {
            StdDeck_CardMask next;
            StdDeck_CardMask_OR(next, used, combo->hand);
            enumerate(state, player + 1, next, weight * combo->weight, profile);
        }
        if (state->count >= state->capacity)
            return;
    }
}

int pe_pko_calculate_from_ranges(const pe_pko_range_input_t *input,
                                 pe_pko_range_result_t *result)
{
    pko_enum_t state;
    pe_pko_range_profile_t current;
    pe_pko_input_t pko;
    double total_weight = 0.0;
    int players;
    if (!input || !result || !input->outcome || input->max_profiles == 0 ||
        input->base.icm.num_players <= 0 || input->base.icm.num_players > ICM_MAX_PLAYERS)
        return -1;
    players = input->base.icm.num_players;
    for (int p = 0; p < players; ++p)
        if (!input->ranges[p].combos || input->ranges[p].count == 0 ||
            input->ranges[p].count > UINT16_MAX)
            return -1;
    memset(result, 0, sizeof(*result));
    memset(&state, 0, sizeof(state));
    state.input = input;
    state.capacity = input->max_profiles;
    state.profiles = calloc(state.capacity, sizeof(*state.profiles));
    if (!state.profiles)
        return -1;
    memset(&current, 0, sizeof(current));
    {
        StdDeck_CardMask empty;
        StdDeck_CardMask_RESET(empty);
        enumerate(&state, 0, empty, 1.0, &current);
    }
    if (state.count == state.capacity) {
        free(state.profiles);
        return -1;
    }
    for (size_t i = 0; i < state.count; ++i)
        total_weight += state.profiles[i].weight;
    if (!(total_weight > 0.0) || !pe_finite_double(total_weight)) {
        free(state.profiles);
        return -1;
    }
    pko = input->base;
    for (size_t i = 0; i < state.count; ++i) {
        double probabilities[ICM_MAX_PLAYERS][ICM_MAX_PLAYERS];
        memset(probabilities, 0, sizeof(probabilities));
        state.profiles[i].weight /= total_weight;
        if (input->outcome(&state.profiles[i], players, probabilities, input->user_data) != 0) {
            free(state.profiles);
            return -1;
        }
        for (int winner = 0; winner < players; ++winner) {
            for (int victim = 0; victim < players; ++victim) {
                double value = probabilities[winner][victim];
                if (!pe_finite_double(value) || value < 0.0 || value > 1.0 ||
                    (winner == victim && value > 0.0)) {
                    free(state.profiles);
                    return -1;
                }
                pko.elimination_probability[winner][victim] +=
                    state.profiles[i].weight * value;
                result->elimination_probability[winner][victim] +=
                    state.profiles[i].weight * value;
            }
        }
    }
    for (int victim = 0; victim < players; ++victim) {
        double sum = 0.0;
        for (int winner = 0; winner < players; ++winner)
            sum += pko.elimination_probability[winner][victim];
        if (sum > 1.0 + 1e-9) {
            free(state.profiles);
            return -1;
        }
    }
    if (pe_pko_calculate(&pko, &result->pko) != 0) {
        free(state.profiles);
        return -1;
    }
    result->profile_count = state.count;
    result->valid_profile_probability = total_weight;
    free(state.profiles);
    return 0;
}
