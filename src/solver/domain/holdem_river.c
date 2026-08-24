#include <poker_eval/solver/pe_holdem_river.h>

#include <limits.h>
#include <math.h>

typedef struct
{
    const EvalContext *context;
    mask_t board;
    const pe_betting_state_t *state;
    const pe_pot_slice_t *slices;
    uint8_t slice_count;
    uint8_t player_count;
    double *values;
} range_showdown_ctx_t;

static int range_showdown_callback(const mask_t *holes, uint8_t player_count,
                                   double weight, void *user)
{
    range_showdown_ctx_t *ctx = (range_showdown_ctx_t *)user;
    eval_t strengths[PE_BETTING_MAX_PLAYERS];
    uint8_t winners[PE_BETTING_MAX_PLAYERS];
    double awards[PE_BETTING_MAX_PLAYERS];
    uint8_t slice;
    uint8_t player;
    if (player_count != ctx->player_count)
        return 1;
    for (player = 0u; player < player_count; ++player)
        strengths[player] = pe_eval_7c(ctx->context,
                                       holes[player] | ctx->board);
    for (slice = 0u; slice < ctx->slice_count; ++slice)
    {
        eval_t best = 0;
        uint8_t found = 0u;
        winners[slice] = 0u;
        for (player = 0u; player < player_count; ++player)
        {
            if (!(ctx->slices[slice].eligible_mask & (uint8_t)(1u << player)))
                continue;
            if (!found || strengths[player] > best)
            {
                best = strengths[player];
                winners[slice] = (uint8_t)(1u << player);
                found = 1u;
            }
            else if (strengths[player] == best)
                winners[slice] |= (uint8_t)(1u << player);
        }
    }
    if (pe_pot_distribute(ctx->slices, ctx->slice_count, winners,
                          player_count, awards) != 0)
        return 1;
    for (player = 0u; player < player_count; ++player)
        ctx->values[player] += weight *
                               (awards[player] - ctx->state->invested[player]);
    return 0;
}

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

int pe_holdem_river_range_values(
    const EvalContext *context,
    mask_t board,
    const pe_holdem_range_t *ranges,
    const pe_betting_state_t *state,
    double *out_values,
    uint8_t player_count,
    size_t *out_deal_count,
    double *out_weight_sum)
{
    pe_pot_slice_t slices[PE_BETTING_MAX_PLAYERS];
    range_showdown_ctx_t callback_context;
    uint8_t slice_count = 0u;
    uint8_t player;
    int status;
    if (!context || !mask_is_valid(board) || mask_popcount(board) != 5 ||
        !ranges || !state || !out_values || !out_deal_count ||
        !out_weight_sum || player_count == 0u ||
        player_count > PE_BETTING_MAX_PLAYERS ||
        state->player_count != player_count || state->winner >= 0)
        return -1;
    for (player = 0u; player < player_count; ++player)
        out_values[player] = 0.0;
    status = pe_pot_slices_build(state, slices, PE_BETTING_MAX_PLAYERS,
                                 &slice_count);
    if (status != 0)
        return status;
    callback_context.context = context;
    callback_context.board = board;
    callback_context.state = state;
    callback_context.slices = slices;
    callback_context.slice_count = slice_count;
    callback_context.player_count = player_count;
    callback_context.values = out_values;
    status = pe_holdem_deals_enumerate(
        board, ranges, player_count, range_showdown_callback,
        &callback_context, out_deal_count, out_weight_sum);
    if (status != 0 || *out_weight_sum <= 0.0 ||
        !isfinite(*out_weight_sum))
        return status != 0 ? status : -1;
    for (player = 0u; player < player_count; ++player)
        out_values[player] /= *out_weight_sum;
    return 0;
}
