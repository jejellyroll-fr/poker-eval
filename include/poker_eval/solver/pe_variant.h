/* pe_variant.h - common rule metadata for the vector simulator variants */

#ifndef POKER_EVAL_PE_VARIANT_H
#define POKER_EVAL_PE_VARIANT_H

#include <stdint.h>

#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/universal_deck.h>
#include <poker_eval/solver/pe_capabilities.h>
#include <poker_eval/solver/pe_chance.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    PE_VARIANT_HOLDEM = 0,
    PE_VARIANT_SHORT_DECK,
    PE_VARIANT_HI_LO,
    PE_VARIANT_STUD,
    PE_VARIANT_DRAW
} pe_variant_family_t;

typedef struct
{
    enum_game_t game;
    pe_variant_family_t family;
    const char *name;
    deck_type_t deck;
    uint8_t deck_cards;
    uint8_t min_private_cards;
    uint8_t max_private_cards;
    uint8_t board_cards;
    uint8_t draw_rounds;
    int has_low;
    low_qualifier_t low_qualifier;
    pe_chance_kind_t first_private_chance;
    uint64_t capabilities;
} pe_variant_profile_t;

/* Resolve the existing game registry into the common simulator contract. */
int pe_variant_profile(enum_game_t game, pe_variant_profile_t *out);

const char *pe_variant_family_name(pe_variant_family_t family);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_VARIANT_H */
