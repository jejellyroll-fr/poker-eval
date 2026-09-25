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
    int hilo8;              /* split each slice high/low 8-or-better */
    double *values;
} omaha_showdown_ctx_t;

/* The winning players of one half.  `strength` is ranked so that a larger
 * value is a better high hand; a player with no qualifying low carries
 * LowHandVal_NOTHING, which is also the worst possible low, so the same loop
 * pattern excludes them from the low half without a special case. */
static uint8_t omaha_best_high_mask(const HandVal *strength, uint8_t eligible,
                                    uint8_t player_count)
{
    uint8_t winners = 0u;
    HandVal best = HandVal_NOTHING;
    uint8_t player;
    for (player = 0u; player < player_count; ++player)
    {
        if (!(eligible & (uint8_t)(1u << player)))
            continue;
        if (winners == 0u || strength[player] > best)
        {
            best = strength[player];
            winners = (uint8_t)(1u << player);
        }
        else if (strength[player] == best)
            winners |= (uint8_t)(1u << player);
    }
    return winners;
}

static uint8_t omaha_best_low_mask(const LowHandVal *strength, uint8_t eligible,
                                   uint8_t player_count)
{
    uint8_t winners = 0u;
    LowHandVal best = LowHandVal_NOTHING;
    uint8_t player;
    for (player = 0u; player < player_count; ++player)
    {
        if (!(eligible & (uint8_t)(1u << player)))
            continue;
        if (strength[player] == LowHandVal_NOTHING)
            continue;   /* not an 8-or-better low */
        if (winners == 0u || strength[player] < best)
        {
            best = strength[player];
            winners = (uint8_t)(1u << player);
        }
        else if (strength[player] == best)
            winners |= (uint8_t)(1u << player);
    }
    return winners;
}

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

/* Hi/Lo splits every slice into two half-slices -- one decided by the high
 * hands, one by the low -- and hands both to the one distribution routine, so
 * the two halves of a pot cannot disagree about eligibility, ties or
 * rounding.  A slice with no qualifying low sends its second half back to the
 * high winners, which is how "no low, 100% high" falls out instead of being a
 * separate branch. */
static uint8_t omaha_hilo8_slices(const omaha_showdown_ctx_t *ctx,
                                  const HandVal *strength,
                                  const LowHandVal *low_strength,
                                  uint8_t player_count,
                                  pe_pot_slice_t *out_slices,
                                  uint8_t *out_winners)
{
    uint8_t count = 0u;
    uint8_t slice;
    for (slice = 0u; slice < ctx->slice_count; ++slice)
    {
        uint8_t eligible = ctx->slices[slice].eligible_mask;
        uint8_t high_winners = omaha_best_high_mask(strength, eligible,
                                                    player_count);
        uint8_t low_winners = omaha_best_low_mask(low_strength, eligible,
                                                  player_count);
        out_slices[count].amount = ctx->slices[slice].amount * 0.5;
        out_slices[count].eligible_mask = eligible;
        out_winners[count++] = high_winners;
        out_slices[count].amount = ctx->slices[slice].amount * 0.5;
        out_slices[count].eligible_mask = eligible;
        out_winners[count++] = low_winners ? low_winners : high_winners;
    }
    return count;
}

static int omaha_callback(const mask_t *holes, uint8_t player_count,
                          double weight, void *user)
{
    omaha_showdown_ctx_t *ctx = (omaha_showdown_ctx_t *)user;
    HandVal strength[PE_OMAHA_RIVER_MAX_PLAYERS];
    LowHandVal low_strength[PE_OMAHA_RIVER_MAX_PLAYERS];
    pe_pot_slice_t slices[2u * PE_OMAHA_RIVER_MAX_PLAYERS];
    uint8_t winner_masks[2u * PE_OMAHA_RIVER_MAX_PLAYERS];
    double awards[PE_OMAHA_RIVER_MAX_PLAYERS];
    StdDeck_CardMask board = to_std_mask(ctx->board);
    uint8_t slice_count;    /* 0 means "the ctx's own slices" */
    uint8_t player;
    pe_pot_slice_t high_only[PE_OMAHA_RIVER_MAX_PLAYERS];
    uint8_t high_only_winners[PE_OMAHA_RIVER_MAX_PLAYERS];
    if (player_count != ctx->player_count)
        return 1;
    for (player = 0u; player < player_count; ++player)
    {
        StdDeck_CardMask hole = to_std_mask(holes[player]);
        if (ctx->hilo8)
        {
            if (StdDeck_OmahaHiLow8_EVAL(hole, board, &strength[player],
                                         &low_strength[player]) != 0)
                return 1;
        }
        else if (StdDeck_OmahaHi_EVAL(hole, board, &strength[player]) != 0)
            return 1;
    }

    if (!ctx->hilo8)
    {
        for (slice_count = 0u; slice_count < ctx->slice_count; ++slice_count)
        {
            high_only[slice_count] = ctx->slices[slice_count];
            high_only_winners[slice_count] = omaha_best_high_mask(
                strength, ctx->slices[slice_count].eligible_mask,
                player_count);
        }
        if (pe_pot_distribute(high_only, slice_count, high_only_winners,
                              player_count, awards) != 0)
            return 1;
    }
    else
    {
        slice_count = omaha_hilo8_slices(ctx, strength, low_strength,
                                         player_count, slices, winner_masks);
        if (pe_pot_distribute(slices, slice_count, winner_masks, player_count,
                              awards) != 0)
            return 1;
    }
    for (player = 0u; player < player_count; ++player)
        ctx->values[player] += weight *
                               (awards[player] - ctx->state->invested[player]);
    return 0;
}

static int omaha_river_range_values(
    const EvalContext *context,
    mask_t board,
    const pe_omaha_range_t *ranges,
    const pe_betting_state_t *state,
    uint8_t hole_cards,
    int hilo8,
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
    callback_context.hilo8 = hilo8;
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
    return omaha_river_range_values(context, board, ranges, state, hole_cards,
                                    0, out_values, player_count, out_deal_count,
                                    out_weight_sum);
}

int pe_omaha_hilo8_river_range_values(
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
    return omaha_river_range_values(context, board, ranges, state, hole_cards,
                                    1, out_values, player_count, out_deal_count,
                                    out_weight_sum);
}
