#include <poker_eval/solver/pe_pots.h>

#include <math.h>
#include <string.h>

static int nearly_equal(double a, double b)
{
    const double scale = fmax(1.0, fmax(fabs(a), fabs(b)));
    return fabs(a - b) <= 1e-9 * scale;
}

int pe_pot_slices_build(const pe_betting_state_t *state,
                        pe_pot_slice_t *out, uint8_t capacity,
                        uint8_t *out_count)
{
    double levels[PE_BETTING_MAX_PLAYERS];
    double invested_sum = 0.0;
    double previous = 0.0;
    double dead_money;
    uint8_t level_count = 0u;
    uint8_t player;
    uint8_t index;
    if (!state || !out_count || (capacity > 0u && !out) ||
        state->player_count == 0u || state->player_count > PE_BETTING_MAX_PLAYERS ||
        !isfinite(state->pot) || state->pot < 0.0)
        return -1;
    for (player = 0u; player < state->player_count; ++player)
    {
        if (!isfinite(state->invested[player]) || state->invested[player] < 0.0)
            return -1;
        invested_sum += state->invested[player];
        if (state->invested[player] <= 0.0)
            continue;
        for (index = 0u; index < level_count; ++index)
            if (nearly_equal(levels[index], state->invested[player]))
                break;
        if (index == level_count)
        {
            if (level_count >= PE_BETTING_MAX_PLAYERS)
                return -1;
            levels[level_count++] = state->invested[player];
        }
    }
    for (index = 1u; index < level_count; ++index)
    {
        uint8_t cursor = index;
        while (cursor > 0u && levels[cursor] < levels[cursor - 1u])
        {
            double tmp = levels[cursor];
            levels[cursor] = levels[cursor - 1u];
            levels[cursor - 1u] = tmp;
            --cursor;
        }
    }
    dead_money = state->pot - invested_sum;
    if (dead_money < -1e-8 * fmax(1.0, state->pot))
        return -1;
    if (level_count == 0u)
    {
        if (state->pot > 0.0)
        {
            if (capacity < 1u)
                return -2;
            out[0].amount = state->pot;
            out[0].eligible_mask = 0u;
            *out_count = 1u;
        }
        else
            *out_count = 0u;
        return 0;
    }
    *out_count = 0u;
    for (index = 0u; index < level_count; ++index)
    {
        double delta = levels[index] - previous;
        double amount = delta * (double)(state->player_count);
        uint8_t eligible = 0u;
        if (index == 0u)
            amount += dead_money;
        for (player = 0u; player < state->player_count; ++player)
        {
            if (state->invested[player] + 1e-9 >= levels[index])
                eligible |= state->active[player] ? (uint8_t)(1u << player) : 0u;
        }
        /* Contributions below this level do not pay into this slice. */
        {
            uint8_t contributors = 0u;
            for (player = 0u; player < state->player_count; ++player)
                if (state->invested[player] + 1e-9 >= levels[index])
                    ++contributors;
            amount = delta * (double)contributors + (index == 0u ? dead_money : 0.0);
        }
        if (amount <= 1e-12)
        {
            previous = levels[index];
            continue;
        }
        if (*out_count >= capacity)
            return -2;
        out[*out_count].amount = amount;
        out[*out_count].eligible_mask = eligible;
        ++*out_count;
        previous = levels[index];
    }
    if (!nearly_equal(invested_sum + dead_money, state->pot))
        return -1;
    return 0;
}

int pe_pot_distribute(const pe_pot_slice_t *slices, uint8_t slice_count,
                      const uint8_t *winner_masks, uint8_t player_count,
                      double *out_awards)
{
    uint8_t slice;
    if (!slices || !winner_masks || !out_awards || player_count == 0u ||
        player_count > PE_BETTING_MAX_PLAYERS)
        return -1;
    memset(out_awards, 0, player_count * sizeof(*out_awards));
    for (slice = 0u; slice < slice_count; ++slice)
    {
        uint8_t winners = winner_masks[slice] & slices[slice].eligible_mask;
        uint8_t winner_count = 0u;
        uint8_t player;
        if (!isfinite(slices[slice].amount) || slices[slice].amount < 0.0)
            return -1;
        for (player = 0u; player < player_count; ++player)
            if (winners & (uint8_t)(1u << player))
                ++winner_count;
        if (winner_count == 0u)
            continue;
        for (player = 0u; player < player_count; ++player)
            if (winners & (uint8_t)(1u << player))
                out_awards[player] += slices[slice].amount / winner_count;
    }
    return 0;
}
