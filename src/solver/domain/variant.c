#include <poker_eval/solver/pe_variant.h>

#include <string.h>

static int is_holdem(enum_game_t game)
{
    return game == game_holdem || game == game_holdem8;
}

static int is_short_deck(enum_game_t game)
{
    return game == game_sdholdem;
}

static int is_stud(enum_game_t game)
{
    return game == game_7stud || game == game_7stud8 ||
           game == game_7studnsq || game == game_razz || game == game_astud;
}

static int is_draw(enum_game_t game)
{
    return game == game_5draw || game == game_5draw8 ||
           game == game_5drawnsq || game == game_lowball ||
           game == game_lowball27 || game == game_27_triple_draw ||
           game == game_a5_triple_draw;
}

static int has_low(enum_game_t game)
{
    enum_gameparams_t *params = enumGameParams(game);
    return params ? params->haslopot != 0 : 0;
}

int pe_variant_profile(enum_game_t game, pe_variant_profile_t *out)
{
    enum_gameparams_t *params;
    pe_variant_family_t family;
    uint8_t draw_rounds = 0u;

    if (!out)
        return -1;
    params = enumGameParams(game);
    if (!params)
        return -1;
    if (is_short_deck(game))
        family = PE_VARIANT_SHORT_DECK;
    else if (is_holdem(game))
        family = has_low(game) ? PE_VARIANT_HI_LO : PE_VARIANT_HOLDEM;
    else if (is_stud(game))
        family = has_low(game) ? PE_VARIANT_HI_LO : PE_VARIANT_STUD;
    else if (is_draw(game))
        family = PE_VARIANT_DRAW;
    else if (game == game_omaha8 || game == game_omaha85 ||
             game == game_omaha86)
        family = PE_VARIANT_HI_LO;
    else
        return -1;

    if (game == game_27_triple_draw || game == game_a5_triple_draw)
        draw_rounds = 3u;
    else if (family == PE_VARIANT_DRAW ||
             (family == PE_VARIANT_HI_LO && is_draw(game)))
        draw_rounds = 1u;

    memset(out, 0, sizeof(*out));
    out->game = game;
    out->family = family;
    out->name = params->name;
    out->deck = Universal_DetermineRequiredDeckType(game);
    out->deck_cards = is_short_deck(game) ? 36u :
                      (out->deck == UNIVERSAL_DECK_JOKER ? 53u : 52u);
    out->min_private_cards = (uint8_t)params->minpocket;
    out->max_private_cards = (uint8_t)params->maxpocket;
    out->board_cards = (uint8_t)params->maxboard;
    out->draw_rounds = draw_rounds;
    out->has_low = params->haslopot != 0;
    out->low_qualifier = params->low_qualifier;
    out->first_private_chance = PE_CHANCE_PRIVATE_HANDS;
    out->capabilities = PE_CAP_PRIVATE_RANGES | PE_CAP_MULTIWAY |
                        PE_CAP_ENUMERATED_CHANCE |
                        PE_CAP_DIRECT_CHANCE_SAMPLING |
                        PE_CAP_ZERO_SUM_GUARANTEE;
    if (out->board_cards >= 3u)
        out->capabilities |= PE_CAP_FLOP_CHANCE;
    if (out->draw_rounds != 0u)
        out->capabilities |= PE_CAP_DRAW_CHANCE;
    return 0;
}

const char *pe_variant_family_name(pe_variant_family_t family)
{
    switch (family)
    {
    case PE_VARIANT_HOLDEM:
        return "holdem";
    case PE_VARIANT_SHORT_DECK:
        return "short-deck";
    case PE_VARIANT_HI_LO:
        return "hi-lo";
    case PE_VARIANT_STUD:
        return "stud";
    case PE_VARIANT_DRAW:
        return "draw";
    default:
        return "unknown";
    }
}
