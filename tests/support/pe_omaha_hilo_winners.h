/*
 * pe_omaha_hilo_winners.h - who takes a pot slice, shared by the tests that
 * price a showdown themselves.
 *
 * Both the river reference walk and the turn oracle have to decide which hands
 * win a slice, and a test that got that rule slightly wrong would disagree with
 * the terminal it is checking without saying which of the two is wrong.  The
 * rule lives here, once: the best eligible high hand, ties included, and the
 * lowest qualifying eight-or-better eligible hand, ties included, with 0
 * meaning nobody in the slice qualifies for a low.
 */

#ifndef PE_TESTS_OMAHA_HILO_WINNERS_H
#define PE_TESTS_OMAHA_HILO_WINNERS_H

#include <stdint.h>

#include <poker_eval/deck/deck_std.h>

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

#endif /* PE_TESTS_OMAHA_HILO_WINNERS_H */
