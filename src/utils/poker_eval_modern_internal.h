#ifndef POKER_EVAL_MODERN_INTERNAL_H
#define POKER_EVAL_MODERN_INTERNAL_H

#include <poker_eval/core/poker_eval_modern.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/deck/deck_short.h>
#include <poker_eval/deck/deck_joker.h>
#include <poker_eval/deck/deck_astud.h>
#include <poker_eval/core/eval_cache.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

struct poker_eval_context {
    poker_deck_type_t deck_type;
    bool caching_enabled;
    eval_cache_t* cache;
};

struct poker_eval_hand {
    poker_deck_type_t deck_type;
    union {
        StdDeck_CardMask std_mask;
        ShortDeck_CardMask short_mask;
        JokerDeck_CardMask joker_mask;
        AStudDeck_CardMask astud_mask;
    } cards;
    size_t card_count;
};

struct poker_eval_result {
    poker_deck_type_t deck_type;
    uint32_t raw_value;
    poker_hand_type_t hand_type;
    bool is_valid;
};

#endif /* POKER_EVAL_MODERN_INTERNAL_H */
