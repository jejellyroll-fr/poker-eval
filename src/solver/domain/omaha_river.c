#include <poker_eval/solver/pe_omaha_river.h>

#include <poker_eval/core/time_compat.h>

#include <float.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
    /* Per-player strengths indexed by combo, filled by the hoisted precompute
     * of the walk and NULL until it runs: `frozen` for the high value, and
     * `frozen_low` for the Hi/Lo low, which is NULL on the high-only path. */
    int64_t **frozen;
    int64_t **frozen_low;
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

/* Monotonic seconds for the probe, through the same shim the rest of the
 * solver uses. */
static double river_now(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0.0;
    return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
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

/* Distribute the pot for one deal from its per-player strengths and add the
 * weighted net award to the accumulator.  Shared by the live callback and the
 * frozen-strength probe, so the two cannot disagree about the payoff. */
static int omaha_award(omaha_showdown_ctx_t *ctx, const HandVal *strength,
                       const LowHandVal *low_strength, double weight)
{
    double awards[PE_OMAHA_RIVER_MAX_PLAYERS];
    uint8_t player;

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
                strength, ctx->slices[slice].eligible_mask,
                ctx->player_count);
        }
        if (pe_pot_distribute(slices, ctx->slice_count, winner_masks,
                              ctx->player_count, awards) != 0)
            return 1;
    }
    else
    {
        pe_pot_slice_t slices[2u * PE_OMAHA_RIVER_MAX_PLAYERS];
        uint8_t winner_masks[2u * PE_OMAHA_RIVER_MAX_PLAYERS];
        uint8_t slice_count = omaha_hilo8_slices(
            ctx, strength, low_strength, ctx->player_count, slices,
            winner_masks);
        if (pe_pot_distribute(slices, slice_count, winner_masks,
                              ctx->player_count, awards) != 0)
            return 1;
    }
    for (player = 0u; player < ctx->player_count; ++player)
        ctx->values[player] += weight *
                               (awards[player] - ctx->state->invested[player]);
    return 0;
}

/* The walk of a hoisted terminal: the same payoff the enumeration's callback
 * paid, with both strengths read from the tables the precompute filled, keyed
 * by the combo each player contributed.  It is indexed by combo, which is why
 * the enumeration reports indices. */
static int omaha_hoisted_callback(const mask_t *holes, const size_t *indices,
                                  uint8_t player_count, double weight,
                                  void *user)
{
    omaha_showdown_ctx_t *ctx = (omaha_showdown_ctx_t *)user;
    HandVal strength[PE_OMAHA_RIVER_MAX_PLAYERS];
    LowHandVal low_strength[PE_OMAHA_RIVER_MAX_PLAYERS];
    uint8_t player;

    (void)holes;
    if (player_count != ctx->player_count)
        return 1;
    for (player = 0u; player < player_count; ++player)
    {
        strength[player] = (HandVal)ctx->frozen[player][indices[player]];
        if (ctx->hilo8)
            low_strength[player] =
                (LowHandVal)ctx->frozen_low[player][indices[player]];
    }
    return omaha_award(ctx, strength, ctx->hilo8 ? low_strength : NULL,
                       weight);
}

/* Heads-up, high-only river through the vector showdown kernel.
 *
 * Every combo is evaluated once against the board, and the per-combo share
 * comes from pe_showdown_vector_mass(), so a four-card range takes the
 * blocker-aware sweep instead of the deal-by-deal product.  The result keeps
 * this terminal's convention: net of the player's investment and divided by
 * the reach mass of the legal deals.
 *
 * out_deal_count is reporting-only and stays 0 on this path: counting the
 * compatible pairs is not what the payoff needs, and the enumeration path
 * already reports it exactly.
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
    double *hero_reach = NULL;
    double *opp_reach = NULL;
    double *mass = NULL;
    pe_value_vec_t shares = {0};
    pe_showdown_path_t path = PE_SHOWDOWN_PATH_NONE;
    double weight_sum = 0.0;
    double net[2] = {0.0, 0.0};
    size_t i;
    int status = 1;

    if (slice_count != 1u || slices[0].eligible_mask != 0x03u ||
        !(slices[0].amount > 0.0) || hero_n == 0u || opp_n == 0u)
        return 1;

    hero_masks = (mask_t *)malloc(hero_n * sizeof(*hero_masks));
    opp_masks = (mask_t *)malloc(opp_n * sizeof(*opp_masks));
    hero_strength = (int64_t *)malloc(hero_n * sizeof(*hero_strength));
    opp_strength = (int64_t *)malloc(opp_n * sizeof(*opp_strength));
    hero_reach = (double *)malloc(hero_n * sizeof(*hero_reach));
    opp_reach = (double *)malloc(opp_n * sizeof(*opp_reach));
    mass = (double *)calloc(hero_n, sizeof(*mass));
    if (!hero_masks || !opp_masks || !hero_strength || !opp_strength ||
        !hero_reach || !opp_reach || !mass)
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
    }

    if (pe_vec_alloc(&shares, hero_n) != PE_SOLVER_OK)
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
    {
        double weight = hero_reach[i];

        weight_sum += weight * mass[i];
        net[0] += weight * (shares.v[i] - state->invested[0] * mass[i]);
        net[1] += weight * (slices[0].amount * mass[i] - shares.v[i] -
                            state->invested[1] * mass[i]);
    }
    if (!(weight_sum > 0.0))
        goto done;
    out_values[0] = net[0] / weight_sum;
    out_values[1] = net[1] / weight_sum;
    *out_deal_count = 0u;
    *out_weight_sum = weight_sum;
    status = 0;

done:
    pe_vec_free(&shares);
    free(mass);
    free(opp_reach);
    free(hero_reach);
    free(opp_strength);
    free(hero_strength);
    free(opp_masks);
    free(hero_masks);
    return status;
}

/* Design A of issue #332: evaluate every declared combo once against this
 * board, then walk the deals reading those strengths, exactly as the
 * enumeration read the evaluator's.  The visit order, the weights, the pot
 * distribution and the accumulation are the enumeration's, so the values, the
 * deal count and the deal mass are bit-identical to the per-deal version;
 * only the number of evaluations changes, from one per player per deal to one
 * per combo.  Hi/Lo hoists its low strength beside the high one.
 *
 * `probe` may be NULL.  When it is not, the precompute and the walk are timed
 * apart, which is the split issue #332 asked for; the walk itself is the one
 * the terminal always runs. */
static int omaha_hoisted_walk(mask_t board, const pe_omaha_range_t *ranges,
                              uint8_t player_count, uint8_t hole_cards,
                              omaha_showdown_ctx_t *ctx,
                              pe_omaha_river_probe_t *probe,
                              size_t *out_deal_count, double *out_weight_sum)
{
    int64_t *high[PE_OMAHA_RIVER_MAX_PLAYERS] = {0};
    int64_t *low[PE_OMAHA_RIVER_MAX_PLAYERS] = {0};
    StdDeck_CardMask std_board = to_std_mask(board);
    uint8_t player;
    double started = 0.0;
    int status = 0;

    if (probe)
        started = river_now();
    for (player = 0u; player < player_count; ++player)
    {
        size_t count = ranges[player].count;
        size_t combo;

        if (count > 0u)
        {
            high[player] = (int64_t *)malloc(count * sizeof(*high[player]));
            if (!high[player])
            {
                status = -1;
                goto done;
            }
            if (ctx->hilo8)
            {
                low[player] = (int64_t *)malloc(count * sizeof(*low[player]));
                if (!low[player])
                {
                    status = -1;
                    goto done;
                }
            }
        }
        for (combo = 0u; combo < count; ++combo)
        {
            StdDeck_CardMask hole =
                to_std_mask(ranges[player].combos[combo].cards);
            HandVal value = HandVal_NOTHING;
            LowHandVal low_value = LowHandVal_NOTHING;

            /* A probe on a Hi/Lo board fills the low table in a pass of its
             * own, because that pass is the figure it reports; the terminal
             * fills both tables from one combined call. */
            if (ctx->hilo8 && !probe)
            {
                if (StdDeck_OmahaHiLow8_EVAL(hole, std_board, &value,
                                             &low_value) != 0)
                {
                    status = 1;
                    goto done;
                }
                low[player][combo] = (int64_t)low_value;
            }
            else if (StdDeck_OmahaHi_EVAL(hole, std_board, &value) != 0)
            {
                status = 1;
                goto done;
            }
            high[player][combo] = (int64_t)value;
        }
        if (probe)
            probe->combos += count;
    }
    if (probe)
        probe->precompute_seconds = river_now() - started;
    if (probe && ctx->hilo8)
    {
        started = river_now();
        for (player = 0u; player < player_count; ++player)
        {
            size_t count = ranges[player].count;
            size_t combo;

            for (combo = 0u; combo < count; ++combo)
            {
                HandVal discarded = HandVal_NOTHING;
                LowHandVal low_value = LowHandVal_NOTHING;

                if (StdDeck_OmahaHiLow8_EVAL(
                        to_std_mask(ranges[player].combos[combo].cards),
                        std_board, &discarded, &low_value) != 0)
                {
                    status = 1;
                    goto done;
                }
                low[player][combo] = (int64_t)low_value;
            }
        }
        probe->low_precompute_seconds = river_now() - started;
    }
    ctx->frozen = high;
    ctx->frozen_low = low;

    if (probe)
        started = river_now();
    status = pe_omaha_deals_enumerate_indexed(
        board, ranges, player_count, hole_cards, omaha_hoisted_callback, ctx,
        out_deal_count, out_weight_sum);
    if (probe)
    {
        probe->walk_seconds = river_now() - started;
        probe->deals = *out_deal_count;
    }

done:
    ctx->frozen = NULL;
    ctx->frozen_low = NULL;
    for (player = 0u; player < PE_OMAHA_RIVER_MAX_PLAYERS; ++player)
    {
        free(low[player]);
        free(high[player]);
    }
    return status;
}

/* The preamble every river terminal call shares: the argument check, the value
 * and reporting outputs zeroed, the pot slices and the callback context.
 * Returns 0 when the caller can walk, -1 for a rejected call, and the pot's own
 * status when the slices cannot be built. */
static int river_terminal_open(const EvalContext *context, mask_t board,
                               const pe_omaha_range_t *ranges,
                               const pe_betting_state_t *state,
                               uint8_t hole_cards, int hilo8,
                               double *out_values, uint8_t player_count,
                               size_t *out_deal_count, double *out_weight_sum,
                               pe_pot_slice_t *slices, uint8_t *slice_count,
                               omaha_showdown_ctx_t *callback_context)
{
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
    *out_deal_count = 0u;
    *out_weight_sum = 0.0;
    status = pe_pot_slices_build(state, slices, PE_OMAHA_RIVER_MAX_PLAYERS,
                                 slice_count);
    if (status != 0)
        return status;
    memset(callback_context, 0, sizeof(*callback_context));
    callback_context->context = context;
    callback_context->board = board;
    callback_context->state = state;
    callback_context->slices = slices;
    callback_context->slice_count = *slice_count;
    callback_context->player_count = player_count;
    callback_context->hole_cards = hole_cards;
    callback_context->hilo8 = hilo8;
    callback_context->values = out_values;
    return 0;
}

/* The tail every river terminal call shares: the walk's status, then the
 * normalisation by the deal mass. */
static int river_terminal_close(int status, double weight_sum,
                                double *out_values, uint8_t player_count)
{
    uint8_t player;

    if (status != 0 || weight_sum <= 0.0 || !finite_double(weight_sum))
        return status != 0 ? status : -1;
    for (player = 0u; player < player_count; ++player)
        out_values[player] /= weight_sum;
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
    double *out_weight_sum,
    pe_showdown_path_t *out_path,
    pe_omaha_river_probe_t *probe)
{
    pe_pot_slice_t slices[PE_OMAHA_RIVER_MAX_PLAYERS];
    omaha_showdown_ctx_t callback_context;
    uint8_t slice_count = 0u;
    int status;

    if (probe)
    {
        probe->precompute_seconds = 0.0;
        probe->low_precompute_seconds = 0.0;
        probe->walk_seconds = 0.0;
        probe->deals = 0u;
        probe->combos = 0u;
    }
    status = river_terminal_open(context, board, ranges, state, hole_cards,
                                 hilo8, out_values, player_count,
                                 out_deal_count, out_weight_sum, slices,
                                 &slice_count, &callback_context);
    if (status != 0)
        return status;
    /* Four-card heads-up high-only ranges go through the vector showdown, with
       strengths evaluated once per combo against this board.  Smaller ranges
       keep the enumeration path, which is where the exact deal count and the
       independent oracle live.  The probe never takes this branch: it prices
       the enumerated walk, whose precompute and deal visits it can split. */
    if (!hilo8 && !probe && player_count == 2u && hole_cards == 4u &&
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
    status = omaha_hoisted_walk(board, ranges, player_count, hole_cards,
                               &callback_context, probe, out_deal_count,
                               out_weight_sum);
    status = river_terminal_close(status, *out_weight_sum, out_values,
                                  player_count);
    if (status == 0 && out_path)
        *out_path = PE_SHOWDOWN_PATH_NONE;
    return status;
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
                                    out_weight_sum, out_path, NULL);
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
                                    out_weight_sum, NULL, NULL);
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
                                    out_weight_sum, NULL, NULL);
}

int pe_omaha_river_range_values_frozen(
    const EvalContext *context,
    mask_t board,
    const pe_omaha_range_t *ranges,
    const pe_betting_state_t *state,
    uint8_t hole_cards,
    double *out_values,
    uint8_t player_count,
    size_t *out_deal_count,
    double *out_weight_sum,
    pe_omaha_river_probe_t *probe)
{
    if (!probe)
        return -1;
    return omaha_river_range_values(context, board, ranges, state, hole_cards,
                                    0, out_values, player_count, out_deal_count,
                                    out_weight_sum, NULL, probe);
}

int pe_omaha_hilo8_river_range_values_frozen(
    const EvalContext *context,
    mask_t board,
    const pe_omaha_range_t *ranges,
    const pe_betting_state_t *state,
    uint8_t hole_cards,
    double *out_values,
    uint8_t player_count,
    size_t *out_deal_count,
    double *out_weight_sum,
    pe_omaha_river_probe_t *probe)
{
    if (!probe)
        return -1;
    return omaha_river_range_values(context, board, ranges, state, hole_cards,
                                    1, out_values, player_count, out_deal_count,
                                    out_weight_sum, NULL, probe);
}
