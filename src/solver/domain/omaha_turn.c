/*
 * omaha_turn.c - exact PLO4/PLO5/PLO6 turn-to-river terminal
 *
 * Issue #285, phase A.  See pe_omaha_turn.h for the deal distribution and for
 * what the figures mean.  The implementation adds no new payoff model: it
 * filters each range for one river card and hands the five-card board to the
 * river terminal, then combines the conditional values by their deal mass.
 * That keeps one definition of a showdown value in the tree, and it means a
 * change to the river terminal's blockering or pot policy is picked up here
 * instead of being reimplemented.
 *
 * Cost is linear in the number of candidate rivers (40 for a full PLO4 turn
 * with no dead cards), each of which costs one river terminal.
 */

#include <poker_eval/solver/pe_omaha_turn.h>

#include <poker_eval/solver/pe_omaha_river.h>

#include <float.h>
#include <stdlib.h>
#include <string.h>

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
                               uint8_t hole_cards, uint8_t player_count,
                               const size_t *out_river_count,
                               const double *out_weight_sum)
{
    uint8_t player;

    if (!context || !ranges || !state || !out_river_count ||
        !out_weight_sum)
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

int pe_omaha_turn_river_count(mask_t board, mask_t dead)
{
    if (!mask_is_valid(board) || !mask_is_valid(dead) ||
        mask_popcount(board) != PE_OMAHA_TURN_BOARD_CARDS ||
        (board & dead) != 0u)
        return -1;
    return MODERN_DECK_SIZE - mask_popcount(board) - mask_popcount(dead);
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
    pe_omaha_combo_t *storage[PE_OMAHA_TURN_PLAYERS_MAX];
    pe_omaha_range_t filtered[PE_OMAHA_TURN_PLAYERS_MAX];
    double accumulated[PE_OMAHA_TURN_PLAYERS_MAX];
    double river_values[PE_OMAHA_TURN_PLAYERS_MAX];
    pe_betting_state_t river_state;
    double total_mass = 0.0;
    size_t rivers = 0u;
    int status = 0;
    uint8_t player;
    int card;

    memset(storage, 0, sizeof(storage));
    memset(filtered, 0, sizeof(filtered));
    memset(accumulated, 0, sizeof(accumulated));
    memset(river_values, 0, sizeof(river_values));
    memset(&river_state, 0, sizeof(river_state));

    /* Zero the outputs before anything can fail, so a caller that ignores the
     * status reads zeros rather than the previous call's figures. */
    for (player = 0u; player < player_count; ++player)
        out_values[player] = 0.0;
    if (out_river_count)
        *out_river_count = 0u;
    if (out_weight_sum)
        *out_weight_sum = 0.0;

    if (!turn_input_is_valid(context, board, dead, ranges, state, hole_cards,
                             player_count, out_river_count, out_weight_sum))
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
        mask_t blocked;
        size_t deal_count = 0u;
        double river_mass = 0.0;
        int rivers_with_deals = 1;

        if (mask_is_set(board, card) || mask_is_set(dead, card))
            continue;
        board_five = mask_set(board, card);
        blocked = (mask_t)1u << card;

        /* A combo holding the river card is not a private hand on this river,
         * so it leaves the range instead of keeping a zero weight: the river
         * terminal rejects a combo that touches its board, and a zero weight
         * would only hide the card from the deal while the leg stayed there.
         * The pairs the hands block are still the river terminal's business.
         * When no combo of some player survives, the river has no legal deal
         * at all and is skipped instead of pricing an empty range. */
        for (player = 0u; player < player_count; ++player)
        {
            const pe_omaha_range_t *range = &ranges[player];
            size_t kept = 0u;
            int live = 0;
            size_t index;

            for (index = 0u; index < range->count; ++index)
            {
                pe_omaha_combo_t combo = range->combos[index];

                if ((combo.cards & blocked) != 0u)
                    continue;
                if (combo.weight > 0.0)
                    live = 1;
                storage[player][kept++] = combo;
            }
            filtered[player].combos = storage[player];
            filtered[player].count = kept;
            if (!live)
            {
                rivers_with_deals = 0;
                break;
            }
        }
        if (!rivers_with_deals)
            continue;

        river_state = *state;
        status = pe_omaha_river_range_values(
            context, board_five, filtered, &river_state, hole_cards,
            river_values, player_count, &deal_count, &river_mass);
        if (status != 0)
            goto done;
        rivers++;
        if (!(river_mass > 0.0) || !finite_double(river_mass))
            continue;
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
