/*
 * chance.c - Chance kind names (CHN-01)
 *
 * Copyright (C) 2026 poker-eval contributors
 */

#include <poker_eval/solver/pe_chance.h>

#include <stddef.h>

const char *pe_chance_kind_name(pe_chance_kind_t kind)
{
    static const char *const names[] = {
        "none", "private-hands", "flop-three", "board-one", "draw-n"
    };
    if ((int)kind < 0 || (int)kind >= (int)PE_CHANCE_KIND_COUNT)
        return NULL;
    return names[(int)kind];
}
