#include <poker_eval/solver/pe_variant_terminal.h>

#include <poker_eval/core/enumdefs.h>

#include <math.h>
#include <string.h>

static StdDeck_CardMask to_std(mask_t mask)
{
    StdDeck_CardMask out;
    int card;
    StdDeck_CardMask_RESET(out);
    for (card = 0; card < MODERN_DECK_SIZE; ++card)
        if (mask_is_set(mask, card))
            StdDeck_CardMask_SET(out, card);
    return out;
}

static int valid_cards(const mask_t *hands, uint8_t players, mask_t board,
                      const pe_variant_profile_t *profile)
{
    mask_t used = board;
    uint8_t player;
    int board_count = mask_popcount(board);

    if (board_count != (int)profile->board_cards)
        return 0;
    for (player = 0u; player < players; ++player)
    {
        int count = mask_popcount(hands[player]);
        if (count != (int)profile->max_private_cards ||
            (hands[player] & used) != MASK_EMPTY)
            return 0;
        used |= hands[player];
    }
    return 1;
}

int pe_variant_terminal_fixed(
    enum_game_t game,
    const mask_t *hands,
    uint8_t player_count,
    mask_t board,
    double pot,
    double *out_values)
{
    pe_variant_profile_t profile;
    StdDeck_CardMask pockets[PE_VARIANT_TERMINAL_MAX_PLAYERS];
    StdDeck_CardMask std_board;
    StdDeck_CardMask dead;
    enum_result_t result;
    uint8_t player;
    int rc;

    if (!hands || !out_values || player_count < 2u ||
        player_count > PE_VARIANT_TERMINAL_MAX_PLAYERS || !isfinite(pot) ||
        pot < 0.0 || pe_variant_profile(game, &profile) != 0 ||
        !valid_cards(hands, player_count, board, &profile))
        return -1;
    if (profile.deck == UNIVERSAL_DECK_JOKER)
        return -2; /* A fixed mask cannot encode Monker/Joker wild cards. */
    for (player = 0u; player < player_count; ++player)
        pockets[player] = to_std(hands[player]);
    std_board = to_std(board);
    StdDeck_CardMask_RESET(dead);
    memset(&result, 0, sizeof(result));
    if (enumResultAlloc(&result, player_count, enum_ordering_mode_none) != 0)
        return -3;
    rc = enumExhaustive(game, pockets, std_board, dead, player_count,
                        profile.board_cards, 0, &result);
    if (rc != 0 || result.nsamples <= 0)
    {
        enumResultFree(&result);
        return -4;
    }
    for (player = 0u; player < player_count; ++player)
        out_values[player] = pot * result.ev[player] /
                             (double)result.nsamples;
    enumResultFree(&result);
    return 0;
}
