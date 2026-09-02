#include <poker_eval/solver/pe_omaha_river.h>

#include <float.h>
#include <string.h>

static int finite_double(double value)
{
    return value >= -DBL_MAX && value <= DBL_MAX;
}

#include <poker_eval/deck/deck_std.h>
#include <poker_eval/games/eval_omaha.h>
#include <poker_eval/solver/pe_pots.h>

#define PE_OMAHA_RIVER_MAX_PLAYERS 8u

typedef struct
{
    const EvalContext *context;
    mask_t board;
    const pe_betting_state_t *state;
    const pe_pot_slice_t *slices;
    uint8_t slice_count;
    uint8_t player_count;
    uint8_t hole_cards;
    double *values;
} omaha_showdown_ctx_t;

static StdDeck_CardMask to_std_mask(mask_t cards)
{
    StdDeck_CardMask result;
    int card;
    StdDeck_CardMask_RESET(result);
    for (card = 0; card < MODERN_DECK_SIZE; ++card)
        if (mask_is_set(cards, card))
            StdDeck_CardMask_SET(result, card);
    return result;
}

static int omaha_callback(const mask_t *holes, uint8_t player_count,
                          double weight, void *user)
{
    omaha_showdown_ctx_t *ctx = (omaha_showdown_ctx_t *)user;
    HandVal strength[PE_OMAHA_RIVER_MAX_PLAYERS];
    uint8_t winner_masks[PE_OMAHA_RIVER_MAX_PLAYERS];
    double awards[PE_OMAHA_RIVER_MAX_PLAYERS];
    StdDeck_CardMask board = to_std_mask(ctx->board);
    uint8_t slice;
    uint8_t player;
    if (player_count != ctx->player_count)
        return 1;
    for (player = 0u; player < player_count; ++player)
    {
        StdDeck_CardMask hole = to_std_mask(holes[player]);
        if (StdDeck_OmahaHi_EVAL(hole, board, &strength[player]) != 0)
            return 1;
    }
    for (slice = 0u; slice < ctx->slice_count; ++slice)
    {
        uint8_t found = 0u;
        HandVal best = HandVal_NOTHING;
        winner_masks[slice] = 0u;
        for (player = 0u; player < player_count; ++player)
        {
            if (!(ctx->slices[slice].eligible_mask & (uint8_t)(1u << player)))
                continue;
            if (!found || strength[player] > best)
            {
                best = strength[player];
                winner_masks[slice] = (uint8_t)(1u << player);
                found = 1u;
            }
            else if (strength[player] == best)
                winner_masks[slice] |= (uint8_t)(1u << player);
        }
    }
    if (pe_pot_distribute(ctx->slices, ctx->slice_count, winner_masks,
                          player_count, awards) != 0)
        return 1;
    for (player = 0u; player < player_count; ++player)
        ctx->values[player] += weight *
                               (awards[player] - ctx->state->invested[player]);
    return 0;
}

int pe_omaha_river_range_values(
    const EvalContext *context,
    mask_t board,
    const pe_omaha_range_t *ranges,
    const pe_betting_state_t *state,
    uint8_t hole_cards,
    double *out_values,
    uint8_t player_count,
    size_t *out_deal_count,
    double *out_weight_sum)
{
    pe_pot_slice_t slices[PE_OMAHA_RIVER_MAX_PLAYERS];
    omaha_showdown_ctx_t callback_context;
    uint8_t slice_count = 0u;
    uint8_t player;
    int status;
    if (!context || !mask_is_valid(board) || mask_popcount(board) != 5 ||
        !ranges || !state || !out_values || !out_deal_count ||
        !out_weight_sum || player_count == 0u ||
        player_count > PE_OMAHA_RIVER_MAX_PLAYERS ||
        state->player_count != player_count || state->winner >= 0 ||
        hole_cards < 4u || hole_cards > 6u)
        return -1;
    for (player = 0u; player < player_count; ++player)
        out_values[player] = 0.0;
    status = pe_pot_slices_build(state, slices, PE_OMAHA_RIVER_MAX_PLAYERS,
                                 &slice_count);
    if (status != 0)
        return status;
    memset(&callback_context, 0, sizeof(callback_context));
    callback_context.context = context;
    callback_context.board = board;
    callback_context.state = state;
    callback_context.slices = slices;
    callback_context.slice_count = slice_count;
    callback_context.player_count = player_count;
    callback_context.hole_cards = hole_cards;
    callback_context.values = out_values;
    status = pe_omaha_deals_enumerate(
        board, ranges, player_count, hole_cards, omaha_callback,
        &callback_context, out_deal_count, out_weight_sum);
    if (status != 0 || *out_weight_sum <= 0.0 ||
        !finite_double(*out_weight_sum))
        return status != 0 ? status : -1;
    for (player = 0u; player < player_count; ++player)
        out_values[player] /= *out_weight_sum;
    return 0;
}
