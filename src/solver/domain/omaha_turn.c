/*
 * omaha_turn.c - exact PLO4/PLO5/PLO6 turn-to-river terminal
 *
 * Issue #285, phase A.  See pe_omaha_turn.h for the deal distribution and for
 * what the figures mean.  The implementation adds no new payoff model: it
 * filters each range for one river card and hands the five-card board to the
 * river terminal, then combines the conditional values by their deal mass.
 * That keeps one definition of a showdown value in the tree, and a change to
 * the river terminal's blockering or pot policy is picked up here instead of
 * being reimplemented.
 *
 * Cost is linear in the number of candidate rivers (48 for a full PLO4 turn
 * with no dead cards), each of which costs one river terminal.
 */

#include <poker_eval/solver/pe_omaha_turn.h>

#include <poker_eval/core/time_compat.h>
#include <poker_eval/solver/pe_omaha_river.h>

#include <float.h>
#include <stdlib.h>
#include <time.h>

static int finite_double(double value)
{
    return value >= -DBL_MAX && value <= DBL_MAX;
}

/* Every declared combo must be a legal private hand: `hole_cards` cards, none
 * of them on the board or dead, with a finite non-negative weight.  A combo
 * that holds a card the caller declared dead is rejected rather than silently
 * removed, because the two together describe an impossible deal. */
static int ranges_are_valid(mask_t board, mask_t dead,
                            const pe_omaha_range_t *ranges,
                            uint8_t hole_cards, uint8_t player_count)
{
    uint8_t player;

    for (player = 0u; player < player_count; ++player)
    {
        const pe_omaha_range_t *range = &ranges[player];
        size_t index;

        if (range->count == 0u || !range->combos)
            return 0;
        for (index = 0u; index < range->count; ++index)
        {
            mask_t hole = range->combos[index].cards;
            double weight = range->combos[index].weight;

            if (!mask_is_valid(hole) ||
                mask_popcount(hole) != (int)hole_cards ||
                (hole & board) != 0u || (hole & dead) != 0u ||
                !(weight >= 0.0) || !finite_double(weight))
                return 0;
        }
    }
    return 1;
}

static int turn_input_is_valid(const EvalContext *context, mask_t board,
                               mask_t dead, const pe_omaha_range_t *ranges,
                               const pe_betting_state_t *state,
                               uint8_t hole_cards, uint8_t player_count)
{
    uint8_t player;

    if (!context || !ranges || !state)
        return 0;
    if (!mask_is_valid(board) || !mask_is_valid(dead) ||
        mask_popcount(board) != PE_OMAHA_TURN_BOARD_CARDS ||
        (board & dead) != 0u)
        return 0;
    if (hole_cards < 4u || hole_cards > 6u || player_count == 0u ||
        player_count > PE_OMAHA_TURN_PLAYERS_MAX ||
        state->player_count != player_count || state->winner >= 0)
        return 0;
    if (!(state->pot > 0.0) || !finite_double(state->pot))
        return 0;
    for (player = 0u; player < player_count; ++player)
    {
        if (!state->active[player])
            return 0;
        if (!(state->invested[player] >= 0.0) ||
            !finite_double(state->invested[player]))
            return 0;
    }
    return ranges_are_valid(board, dead, ranges, hole_cards, player_count);
}

/* Monotonic seconds, through the same clock_gettime shim the solver runtime
 * uses, so the phase clocks stay portable.  A clock that refuses to report is
 * not a pricing failure: the counters still stand and the phase time reads
 * zero. */
static double turn_now(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0.0;
    return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}

/* Copy the combos that do not hold a card of `blocked` into `out` and return
 * how many survived.  A combo holding the river card is not a private hand on
 * that river, so it leaves the range rather than keeping a zero weight: the
 * river terminal rejects a combo that touches its board, and a zero weight
 * would leave the card in a leg the deal walk cannot use. */
static size_t compact_for_river(const pe_omaha_range_t *range, mask_t blocked,
                                pe_omaha_combo_t *out)
{
    size_t kept = 0u;
    size_t index;

    for (index = 0u; index < range->count; ++index)
        if ((range->combos[index].cards & blocked) == 0u)
            out[kept++] = range->combos[index];
    return kept;
}

/* The terminal itself.  `stats` is optional and NULL is the published path:
 * when it is NULL neither the clock nor the counters run, so a call without a
 * profile is the same work it was before the counters existed. */
static int turn_range_values(
    const EvalContext *context,
    mask_t board,
    mask_t dead,
    const pe_omaha_range_t *ranges,
    const pe_betting_state_t *state,
    uint8_t hole_cards,
    double *out_values,
    uint8_t player_count,
    size_t *out_river_count,
    double *out_weight_sum,
    pe_omaha_turn_stats_t *stats)
{
    pe_omaha_combo_t *storage[PE_OMAHA_TURN_PLAYERS_MAX] = {0};
    pe_omaha_range_t filtered[PE_OMAHA_TURN_PLAYERS_MAX];
    double accumulated[PE_OMAHA_TURN_PLAYERS_MAX] = {0.0};
    double river_values[PE_OMAHA_TURN_PLAYERS_MAX];
    double total_mass = 0.0;
    size_t rivers = 0u;
    int status = 0;
    uint8_t player;
    int card;

    /* The counters are cleared before anything can fail, so a caller that
     * ignores the status reads zeros rather than the previous call's figures,
     * exactly as the value outputs do. */
    if (stats)
    {
        stats->combos_examined = 0u;
        stats->combos_kept = 0u;
        stats->rivers_to_terminal = 0u;
        stats->deals = 0u;
        stats->filter_seconds = 0.0;
        stats->terminal_seconds = 0.0;
    }

    if (!out_values || !out_river_count || !out_weight_sum)
        return -1;

    /* The outputs are cleared before anything can fail, so a caller that
     * ignores the status reads zeros rather than the previous call's figures.
     * The loop is bounded by the accepted player maximum, so a bogus count
     * cannot run past the caller's array. */
    for (player = 0u;
         player < player_count && player < PE_OMAHA_TURN_PLAYERS_MAX;
         ++player)
        out_values[player] = 0.0;
    *out_river_count = 0u;
    *out_weight_sum = 0.0;

    if (!turn_input_is_valid(context, board, dead, ranges, state, hole_cards,
                             player_count))
        return -1;

    for (player = 0u; player < player_count; ++player)
    {
        size_t count = ranges[player].count;

        storage[player] =
            (pe_omaha_combo_t *)malloc(count * sizeof(*storage[player]));
        if (!storage[player])
        {
            status = -2;
            goto done;
        }
        filtered[player].combos = storage[player];
        filtered[player].count = count;
    }

    for (card = 0; card < MODERN_DECK_SIZE; ++card)
    {
        mask_t board_five;
        size_t deal_count = 0u;
        size_t examined = 0u;
        size_t kept = 0u;
        double river_mass = 0.0;
        double started = 0.0;
        int priced = 1;

        if (mask_is_set(board, card) || mask_is_set(dead, card))
            continue;
        if (stats)
            started = turn_now();
        for (player = 0u; player < player_count; ++player)
        {
            filtered[player].combos = storage[player];
            filtered[player].count = compact_for_river(
                &ranges[player], (mask_t)1u << card, storage[player]);
            if (stats)
            {
                examined += ranges[player].count;
                kept += filtered[player].count;
            }
            /* A player left without a combo has no legal deal on this river;
             * pricing it would divide by a reach mass of zero. */
            if (filtered[player].count == 0u)
            {
                priced = 0;
                break;
            }
        }
        if (stats)
        {
            stats->filter_seconds += turn_now() - started;
            stats->combos_examined += examined;
            stats->combos_kept += kept;
        }
        if (!priced)
            continue;

        board_five = mask_set(board, card);
        if (stats)
            started = turn_now();
        status = pe_omaha_river_range_values(context, board_five, filtered,
                                             state, hole_cards, river_values,
                                             player_count, &deal_count,
                                             &river_mass);
        if (stats)
        {
            stats->terminal_seconds += turn_now() - started;
            ++stats->rivers_to_terminal;
            /* A river the terminal reports no deal on still cost the call, so
             * the time above counts it; only its deal count is not there. */
            if (status == 0)
                stats->deals += deal_count;
        }
        /* The river terminal reports -1 when no pair is compatible on that
         * board: the turn simply has no deal on that river, which is not an
         * input error.  Its success path reports a positive, finite mass. */
        if (status == -1)
            continue;
        if (status != 0)
            goto done;
        rivers++;
        for (player = 0u; player < player_count; ++player)
            accumulated[player] += river_values[player] * river_mass;
        total_mass += river_mass;
    }

    if (!(total_mass > 0.0) || !finite_double(total_mass))
    {
        status = 1;
        goto done;
    }
    for (player = 0u; player < player_count; ++player)
        out_values[player] = accumulated[player] / total_mass;
    *out_river_count = rivers;
    *out_weight_sum = total_mass;

done:
    for (player = 0u; player < player_count; ++player)
        free(storage[player]);
    return status;
}

int pe_omaha_turn_range_values(
    const EvalContext *context,
    mask_t board,
    mask_t dead,
    const pe_omaha_range_t *ranges,
    const pe_betting_state_t *state,
    uint8_t hole_cards,
    double *out_values,
    uint8_t player_count,
    size_t *out_river_count,
    double *out_weight_sum)
{
    return turn_range_values(context, board, dead, ranges, state, hole_cards,
                             out_values, player_count, out_river_count,
                             out_weight_sum, NULL);
}

int pe_omaha_turn_range_values_profiled(
    const EvalContext *context,
    mask_t board,
    mask_t dead,
    const pe_omaha_range_t *ranges,
    const pe_betting_state_t *state,
    uint8_t hole_cards,
    double *out_values,
    uint8_t player_count,
    size_t *out_river_count,
    double *out_weight_sum,
    pe_omaha_turn_stats_t *stats)
{
    return turn_range_values(context, board, dead, ranges, state, hole_cards,
                             out_values, player_count, out_river_count,
                             out_weight_sum, stats);
}
