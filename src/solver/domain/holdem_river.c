#include <poker_eval/solver/pe_holdem_river.h>

#include <math.h>

static int valid_spec(const pe_holdem_river_spec_t *spec,
                      const pe_betting_state_t *state,
                      const pe_reach_vec_t *reach,
                      pe_value_vec_t *out_values,
                      uint8_t player_count)
{
    return spec && spec->context && spec->hole && spec->combo_count > 0u &&
           state && reach && out_values && player_count == 2u &&
           state->player_count == player_count &&
           out_values[0].n == spec->combo_count &&
           out_values[1].n == spec->combo_count &&
           reach[0].n == spec->combo_count &&
           reach[1].n == spec->combo_count;
}

static double net_share(const pe_betting_state_t *state, uint8_t player,
                        double share)
{
    return share - state->invested[player];
}

int pe_holdem_river_terminal_values(
    const pe_holdem_river_spec_t *spec,
    const pe_betting_state_t *state,
    const pe_reach_vec_t *reach,
    pe_value_vec_t *out_values,
    uint8_t player_count)
{
    uint8_t player;
    if (!valid_spec(spec, state, reach, out_values, player_count))
        return -1;
    if (!isfinite(state->pot) || state->pot < 0.0 ||
        !isfinite(state->invested[0]) || !isfinite(state->invested[1]) ||
        state->invested[0] < 0.0 || state->invested[1] < 0.0)
        return -1;
    for (player = 0u; player < player_count; ++player)
    {
        uint16_t combo;
        for (combo = 0u; combo < spec->combo_count; ++combo)
        {
            const mask_t own = spec->hole[player * spec->combo_count + combo];
            double value = 0.0;
            if ((own & spec->board) != 0)
                return -1;
            if (state->winner >= 0)
            {
                value = net_share(
                    state, player,
                    state->winner == (int)player ? state->pot : 0.0);
            }
            else
            {
                uint8_t opponent = (uint8_t)(1u - player);
                double weight_sum = 0.0;
                uint16_t other;
                for (other = 0u; other < spec->combo_count; ++other)
                {
                    const mask_t opponent_hand =
                        spec->hole[opponent * spec->combo_count + other];
                    if ((own & opponent_hand) != 0 ||
                        (opponent_hand & spec->board) != 0)
                        continue;
                    if (reach[opponent].v[other] > 0.0)
                        weight_sum += reach[opponent].v[other];
                }
                if (weight_sum <= 0.0)
                    return -1;
                {
                    eval_t own_value = pe_eval_7c(
                        spec->context, own | spec->board);
                    for (other = 0u; other < spec->combo_count; ++other)
                    {
                        const mask_t opponent_hand =
                            spec->hole[opponent * spec->combo_count + other];
                        eval_t opponent_value;
                        double share;
                        if ((own & opponent_hand) != 0 ||
                            (opponent_hand & spec->board) != 0 ||
                            reach[opponent].v[other] <= 0.0)
                            continue;
                        opponent_value = pe_eval_7c(
                            spec->context, opponent_hand | spec->board);
                        if (own_value > opponent_value)
                            share = state->pot;
                        else if (own_value == opponent_value)
                            share = state->pot * 0.5;
                        else
                            share = 0.0;
                        value += reach[opponent].v[other] *
                                 net_share(state, player, share) / weight_sum;
                    }
                }
            }
            out_values[player].v[combo] = value;
        }
    }
    return 0;
}
