/* monker_key.c - never mistake a hash for a reversible board encoding. */

#include <poker_eval/solver/pe_monker_key.h>

static int valid_card_count(unsigned count)
{
    return count == 0u || count == 3u || count == 4u || count == 5u;
}

pe_monker_key_status_t pe_monker_key_decode_packed_board(
    uint64_t key, unsigned shift, unsigned card_count, mask_t *out_board)
{
    mask_t board;
    if (!out_board)
        return PE_MONKER_KEY_ERR_NULL_ARGUMENT;
    *out_board = MASK_EMPTY;
    if (shift > 12u || !valid_card_count(card_count))
        return PE_MONKER_KEY_ERR_INVALID_LAYOUT;
    board = (key >> shift) & ((((mask_t)1u) << MODERN_DECK_SIZE) - 1u);
    if (mask_popcount(board) != (int)card_count)
        return PE_MONKER_KEY_ERR_INVALID_BOARD;
    *out_board = board;
    return PE_MONKER_KEY_OK;
}

pe_monker_key_status_t pe_monker_key_decode_board(
    uint64_t raw_key, mask_t *out_board)
{
    (void)raw_key;
    if (!out_board)
        return PE_MONKER_KEY_ERR_NULL_ARGUMENT;
    *out_board = MASK_EMPTY;
    return PE_MONKER_KEY_ERR_NOT_INVERTIBLE;
}

const char *pe_monker_key_status_string(pe_monker_key_status_t status)
{
    switch (status)
    {
    case PE_MONKER_KEY_OK: return "ok";
    case PE_MONKER_KEY_ERR_NULL_ARGUMENT: return "null argument";
    case PE_MONKER_KEY_ERR_INVALID_LAYOUT: return "invalid packed layout";
    case PE_MONKER_KEY_ERR_INVALID_BOARD: return "packed value is not a board";
    case PE_MONKER_KEY_ERR_NOT_INVERTIBLE:
    default: return "raw key is a hash and is not invertible";
    }
}
