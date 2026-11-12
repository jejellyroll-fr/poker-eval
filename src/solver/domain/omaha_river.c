#include <poker_eval/solver/pe_omaha_river.h>

#include <float.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int finite_double(double value)
{
    return value >= -DBL_MAX && value <= DBL_MAX;
}

#include <poker_eval/deck/deck_std.h>
#include <poker_eval/games/eval_omaha.h>
#include <poker_eval/solver/pe_pots.h>
#include <poker_eval/solver/pe_showdown.h>
#include <poker_eval/solver/pe_vector.h>

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
    double awards[PE_OMAHA_RIVER_MAX_PLAYERS];
    StdDeck_CardMask board = to_std_mask(ctx->board);
    uint8_t player;

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
        /* Zero-initialised so the distribution cannot read an entry the loop
         * above did not write when a state has no slices at all. */
        pe_pot_slice_t slices[PE_OMAHA_RIVER_MAX_PLAYERS] = {0};
        uint8_t winner_masks[PE_OMAHA_RIVER_MAX_PLAYERS] = {0};
        uint8_t slice;
        for (slice = 0u; slice < ctx->slice_count; ++slice)
        {
            slices[slice] = ctx->slices[slice];
            winner_masks[slice] = omaha_best_high_mask(
                strength, ctx->slices[slice].eligible_mask, player_count);
        }
        if (pe_pot_distribute(slices, ctx->slice_count, winner_masks,
                              player_count, awards) != 0)
            return 1;
    }
    else
    {
        pe_pot_slice_t slices[2u * PE_OMAHA_RIVER_MAX_PLAYERS];
        uint8_t winner_masks[2u * PE_OMAHA_RIVER_MAX_PLAYERS];
        uint8_t slice_count = omaha_hilo8_slices(
            ctx, strength, low_strength, player_count, slices, winner_masks);
        if (pe_pot_distribute(slices, slice_count, winner_masks, player_count,
                              awards) != 0)
            return 1;
    }
    for (player = 0u; player < player_count; ++player)
        ctx->values[player] += weight *
                               (awards[player] - ctx->state->invested[player]);
    return 0;
}

/* Heads-up, high-only river through the vector showdown kernel.
 *
 * Every combo is evaluated once against the board, and the per-combo share
 * comes from pe_showdown_vector_mass(), so a four-card range takes the
 * blocker-aware sweep instead of the deal-by-deal product.  The result keeps
 * this terminal's convention: net of the player's investment and divided by
 * the reach mass of the legal deals.  A second, unit-reach pass with the hero
 * above every possible hand yields the deal count, which the enumeration path
 * reports and the caller expects to stay exact.
 *
 * Returns 0 with the outputs filled, 1 when the shape is outside this path so
 * the caller falls back to enumeration, or -1 on error.
 */
static int omaha_heads_up_values(const mask_t board,
                                 const pe_omaha_range_t *ranges,
                                 const pe_betting_state_t *state,
                                 const pe_pot_slice_t *slices,
                                 uint8_t slice_count, double *out_values,
                                 size_t *out_deal_count,
                                 double *out_weight_sum)
{
    const pe_omaha_range_t *hero_range = &ranges[0];
    const pe_omaha_range_t *opp_range = &ranges[1];
    size_t hero_n = hero_range->count;
    size_t opp_n = opp_range->count;
    StdDeck_CardMask std_board = to_std_mask(board);
    mask_t *hero_masks = NULL;
    mask_t *opp_masks = NULL;
    int64_t *hero_strength = NULL;
    int64_t *opp_strength = NULL;
    int64_t *count_strength = NULL;
    double *hero_reach = NULL;
    double *opp_reach = NULL;
    double *opp_count_reach = NULL;
    double *mass = NULL;
    pe_value_vec_t shares = {0};
    pe_value_vec_t counts = {0};
    pe_showdown_path_t path = PE_SHOWDOWN_PATH_NONE;
    double weight_sum = 0.0;
    double net[2] = {0.0, 0.0};
    size_t deal_count = 0u;
    size_t i;
    int status = 1;

    if (slice_count != 1u || slices[0].eligible_mask != 0x03u ||
        !(slices[0].amount > 0.0) || hero_n == 0u || opp_n == 0u)
        return 1;

    hero_masks = (mask_t *)malloc(hero_n * sizeof(*hero_masks));
    opp_masks = (mask_t *)malloc(opp_n * sizeof(*opp_masks));
    hero_strength = (int64_t *)malloc(hero_n * sizeof(*hero_strength));
    opp_strength = (int64_t *)malloc(opp_n * sizeof(*opp_strength));
    count_strength = (int64_t *)malloc(hero_n * sizeof(*count_strength));
    hero_reach = (double *)malloc(hero_n * sizeof(*hero_reach));
    opp_reach = (double *)malloc(opp_n * sizeof(*opp_reach));
    opp_count_reach = (double *)malloc(opp_n * sizeof(*opp_count_reach));
    mass = (double *)calloc(hero_n, sizeof(*mass));
    if (!hero_masks || !opp_masks || !hero_strength || !opp_strength ||
        !count_strength || !hero_reach || !opp_reach || !opp_count_reach ||
        !mass)
    {
        status = -1;
        goto done;
    }

    for (i = 0u; i < hero_n; ++i)
    {
        mask_t hole = hero_range->combos[i].cards;
        HandVal value = HandVal_NOTHING;
        int valid = mask_popcount(hole) == 4 && (hole & board) == 0u &&
                    StdDeck_OmahaHi_EVAL(to_std_mask(hole), std_board,
                                         &value) == 0;

        hero_masks[i] = valid ? hole : (mask_t)(hole | board);
        hero_strength[i] = valid ? (int64_t)value : 0;
        hero_reach[i] = hero_range->combos[i].weight;
    }
    for (i = 0u; i < opp_n; ++i)
    {
        mask_t hole = opp_range->combos[i].cards;
        HandVal value = HandVal_NOTHING;
        int valid = mask_popcount(hole) == 4 && (hole & board) == 0u &&
                    StdDeck_OmahaHi_EVAL(to_std_mask(hole), std_board,
                                         &value) == 0;
        double weight = opp_range->combos[i].weight;

        opp_masks[i] = hole;
        opp_strength[i] = valid ? (int64_t)value : 0;
        opp_reach[i] = valid && weight > 0.0 ? weight : 0.0;
        opp_count_reach[i] = valid && weight > 0.0 ? 1.0 : 0.0;
    }

    if (pe_vec_alloc(&shares, hero_n) != PE_SOLVER_OK ||
        pe_vec_alloc(&counts, hero_n) != PE_SOLVER_OK)
    {
        status = -1;
        goto done;
    }
    if (pe_showdown_vector_mass(hero_masks, hero_strength, hero_n, opp_masks,
                                opp_strength, opp_reach, opp_n, board,
                                slices[0].amount, &shares, mass, &path) !=
        PE_SOLVER_OK)
        goto done;

    for (i = 0u; i < hero_n; ++i)
        count_strength[i] = INT64_MAX;
    if (pe_showdown_vector(hero_masks, count_strength, hero_n, opp_masks,
                           opp_strength, opp_count_reach, opp_n, board, 1.0,
                           &counts, &path) != PE_SOLVER_OK)
        goto done;

    for (i = 0u; i < hero_n; ++i)
    {
        double weight = hero_reach[i];

        weight_sum += weight * mass[i];
        net[0] += weight * (shares.v[i] - state->invested[0] * mass[i]);
        net[1] += weight * (slices[0].amount * mass[i] - shares.v[i] -
                            state->invested[1] * mass[i]);
        deal_count += (size_t)(counts.v[i] + 0.5);
    }
    if (!(weight_sum > 0.0))
        goto done;
    out_values[0] = net[0] / weight_sum;
    out_values[1] = net[1] / weight_sum;
    *out_deal_count = deal_count;
    *out_weight_sum = weight_sum;
    status = 0;

done:
    pe_vec_free(&counts);
    pe_vec_free(&shares);
    free(mass);
    free(opp_count_reach);
    free(opp_reach);
    free(hero_reach);
    free(count_strength);
    free(opp_strength);
    free(hero_strength);
    free(opp_masks);
    free(hero_masks);
    return status;
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
    double *out_weight_sum,
    pe_showdown_path_t *out_path)
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
    /* Four-card heads-up high-only ranges go through the vector showdown, with
       strengths evaluated once per combo against this board.  Smaller ranges
       keep the enumeration path, which is where the exact deal count and the
       independent oracle live. */
    if (!hilo8 && player_count == 2u && hole_cards == 4u &&
        ranges[0].count >= PE_SHOWDOWN_PLO4_MIN_OPPONENTS &&
        ranges[1].count >= PE_SHOWDOWN_PLO4_MIN_OPPONENTS)
    {
        int fast = omaha_heads_up_values(board, ranges, state, slices,
                                         slice_count, out_values,
                                         out_deal_count, out_weight_sum);
        if (fast == 0)
        {
            if (out_path)
                *out_path = PE_SHOWDOWN_PATH_PLO4;
            return 0;
        }
        if (fast < 0)
            return -1;
    }
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
    if (out_path)
        *out_path = PE_SHOWDOWN_PATH_NONE;
    return 0;
}

int pe_omaha_river_range_values_with_path(
    const EvalContext *context,
    mask_t board,
    const pe_omaha_range_t *ranges,
    const pe_betting_state_t *state,
    uint8_t hole_cards,
    double *out_values,
    uint8_t player_count,
    size_t *out_deal_count,
    double *out_weight_sum,
    pe_showdown_path_t *out_path)
{
    return omaha_river_range_values(context, board, ranges, state, hole_cards,
                                    0, out_values, player_count, out_deal_count,
                                    out_weight_sum, out_path);
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
                                    out_weight_sum, NULL);
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
                                    out_weight_sum, NULL);
}
