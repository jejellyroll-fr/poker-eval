/*
 * pe_omaha_showdown_reference.h - how a reference test pays one showdown,
 * shared by the river and turn oracles.
 *
 * Both the river reference walk and the turn oracle have to decide which hands
 * win a slice and how the pot splits, and a test that got either rule slightly
 * wrong would disagree with the terminal it is checking without saying which of
 * the two is wrong.  The rules live here, once: the best eligible high hand,
 * ties included; the lowest qualifying eight-or-better eligible hand, ties
 * included, with 0 meaning nobody in the slice qualifies; and the payment that
 * turns a slice into a high half and a low half, sending the low half back to
 * the high winners when nobody qualifies.
 */

#ifndef PE_TESTS_OMAHA_SHOWDOWN_REFERENCE_H
#define PE_TESTS_OMAHA_SHOWDOWN_REFERENCE_H

#include <stdint.h>

#include <poker_eval/deck/deck_std.h>
#include <poker_eval/solver/pe_pots.h>

/* Winners of the high half of a slice: the strongest eligible hand, ties
 * included.  Returns a mask of players, or 0 when nobody is eligible. */
static inline uint8_t pe_test_best_high(const HandVal *strength,
                                        uint8_t eligible, uint8_t player_count)
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

/* Winners of the low half of a slice: the lowest qualifying eight-or-better
 * eligible hand, ties included, or 0 when nobody qualifies. */
static inline uint8_t pe_test_best_low(const LowHandVal *strength,
                                       uint8_t eligible, uint8_t player_count)
{
    uint8_t winners = 0u;
    LowHandVal best = LowHandVal_NOTHING;
    uint8_t player;

    for (player = 0u; player < player_count; ++player)
    {
        if (!(eligible & (uint8_t)(1u << player)))
            continue;
        if (strength[player] == LowHandVal_NOTHING)
            continue;
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

/* How many players a winner mask holds, for splitting a slice between tied
 * winners. */
static inline uint8_t pe_test_count_bits(uint8_t bits)
{
    uint8_t count = 0u;

    while (bits != 0u)
    {
        count = (uint8_t)(count + (bits & 1u));
        bits = (uint8_t)(bits >> 1u);
    }
    return count;
}

/* Pay `slices` for one showdown and clear `out_awards`, which holds one entry
 * per player.  Every slice goes to its best eligible high hand, split evenly
 * between ties; on a Hi/Lo board the other half goes to the best qualifying
 * low, split the same way, and a slice nobody can win pays nobody.  Strengths
 * are indexed by player and the low array is not read unless `hilo8` is set. */
static inline void pe_test_pay_slices(const pe_pot_slice_t *slices,
                                      uint8_t slice_count,
                                      const HandVal *high,
                                      const LowHandVal *low, int hilo8,
                                      uint8_t player_count, double *out_awards)
{
    uint8_t slice;
    uint8_t player;

    for (player = 0u; player < player_count; ++player)
        out_awards[player] = 0.0;
    for (slice = 0u; slice < slice_count; ++slice)
    {
        double amount = slices[slice].amount;
        uint8_t eligible = slices[slice].eligible_mask;
        uint8_t high_mask = pe_test_best_high(high, eligible, player_count);
        uint8_t high_count = pe_test_count_bits(high_mask);
        uint8_t low_mask = 0u;
        uint8_t low_count = 0u;

        if (high_count == 0u)
            continue;
        if (hilo8)
        {
            low_mask = pe_test_best_low(low, eligible, player_count);
            low_count = pe_test_count_bits(low_mask);
            /* No qualifying low sends its half back to the high winners,
             * which is the terminal's "no low, 100 % high" rule. */
            if (low_count == 0u)
            {
                low_mask = high_mask;
                low_count = high_count;
            }
        }
        for (player = 0u; player < player_count; ++player)
        {
            if (high_mask & (uint8_t)(1u << player))
                out_awards[player] +=
                    (hilo8 ? amount * 0.5 : amount) / (double)high_count;
            if (low_mask & (uint8_t)(1u << player))
                out_awards[player] += amount * 0.5 / (double)low_count;
        }
    }
}

#endif /* PE_TESTS_OMAHA_SHOWDOWN_REFERENCE_H */
