/*
 * range_parser.c - Range strings into prepared ranges (RNG-01)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * The adapter side of RNG-01. Turning "AKs" into card masks is the range
 * engine's job and it has done it for a long time; this only chains the parse
 * to the solver's preparation and makes sure a rejected string never comes
 * back as an empty range.
 */

#include <poker_eval/solver/pe_range.h>

#include <stddef.h>

pe_solver_status_t pe_solver_range_parse(enum_game_t variant,
                                         const char *range_str,
                                         StdDeck_CardMask dead_cards,
                                         pe_range_t **out_range)
{
    pe_range_t *parsed = NULL;
    pe_solver_status_t st;

    if (range_str == NULL || out_range == NULL)
        return PE_SOLVER_ERR_NULL_ARGUMENT;

    *out_range = NULL;

    if (pe_range_parse(variant, range_str, dead_cards, NULL, &parsed) != PE_STATUS_OK
        || parsed == NULL)
        return PE_SOLVER_ERR_INVALID_CONFIG;

    st = pe_solver_range_prepare(parsed);
    if (st != PE_SOLVER_OK)
    {
        /* A string that parses to nothing usable is a rejected string, not an
           empty range handed to the caller to discover later. */
        pe_range_free(parsed);
        return st;
    }

    *out_range = parsed;
    return PE_SOLVER_OK;
}
