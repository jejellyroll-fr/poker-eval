#include <poker_eval/solver/pe_holdem_streets.h>

#include <math.h>

static size_t choose2(size_t n)
{
    return n < 2u ? 0u : n * (n - 1u) / 2u;
}

static size_t choose3(size_t n)
{
    return n < 3u ? 0u : n * (n - 1u) * (n - 2u) / 6u;
}

int pe_holdem_street_from_board(mask_t board, pe_holdem_street_t *out_street)
{
    int count;
    if (!out_street || !mask_is_valid(board))
        return -1;
    count = mask_popcount(board);
    if (count == 0)
        *out_street = PE_HOLDEM_PREFLOP;
    else if (count == 3)
        *out_street = PE_HOLDEM_FLOP;
    else if (count == 4)
        *out_street = PE_HOLDEM_TURN;
    else if (count == 5)
        *out_street = PE_HOLDEM_RIVER;
    else
        return -2;
    return 0;
}

uint8_t pe_holdem_next_public_count(pe_holdem_street_t street)
{
    switch (street)
    {
    case PE_HOLDEM_PREFLOP:
        return 3u;
    case PE_HOLDEM_FLOP:
    case PE_HOLDEM_TURN:
        return 1u;
    case PE_HOLDEM_RIVER:
    case PE_HOLDEM_SHOWDOWN:
    default:
        return 0u;
    }
}

size_t pe_holdem_public_outcome_count(mask_t board, mask_t dead_cards)
{
    pe_holdem_street_t street;
    size_t available;
    uint8_t added;
    if (pe_holdem_street_from_board(board, &street) != 0 ||
        !mask_is_valid(dead_cards) || (board & dead_cards) != 0)
        return 0u;
    added = pe_holdem_next_public_count(street);
    available = (size_t)(MODERN_DECK_SIZE -
                         mask_popcount(board | dead_cards));
    if (added == 3u)
        return choose3(available);
    if (added == 1u)
        return available;
    return 0u;
}

int pe_holdem_public_chance_enumerate(
    mask_t board,
    mask_t dead_cards,
    pe_holdem_board_callback callback,
    void *user,
    size_t *out_count,
    double *out_weight_sum)
{
    pe_holdem_street_t street;
    uint8_t added;
    size_t count = 0u;
    double weight_sum = 0.0;
    int first;
    if (!callback || !out_count || !out_weight_sum ||
        pe_holdem_street_from_board(board, &street) != 0 ||
        !mask_is_valid(dead_cards) || (board & dead_cards) != 0)
        return -1;
    added = pe_holdem_next_public_count(street);
    if (added == 0u)
    {
        *out_count = 0u;
        *out_weight_sum = 0.0;
        return 0;
    }
    for (first = 0; first < MODERN_DECK_SIZE; ++first)
    {
        int second;
        if (mask_is_set(board | dead_cards, first))
            continue;
        if (added == 1u)
        {
            if (callback(mask_set(board, first), added, 1.0, user) != 0)
                return 1;
            ++count;
            weight_sum += 1.0;
            continue;
        }
        for (second = first + 1; second < MODERN_DECK_SIZE; ++second)
        {
            int third;
            if (mask_is_set(board | dead_cards, second))
                continue;
            for (third = second + 1; third < MODERN_DECK_SIZE; ++third)
            {
                mask_t next;
                if (mask_is_set(board | dead_cards, third))
                    continue;
                next = mask_set(mask_set(mask_set(board, first), second),
                                third);
                if (callback(next, added, 1.0, user) != 0)
                    return 1;
                ++count;
                weight_sum += 1.0;
            }
        }
    }
    *out_count = count;
    *out_weight_sum = weight_sum;
    return 0;
}
