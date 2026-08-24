/* pe_monker_key.h - safe board decoding for raw Monker/information keys. */

#ifndef POKER_EVAL_PE_MONKER_KEY_H
#define POKER_EVAL_PE_MONKER_KEY_H

#include <stdint.h>

#include <poker_eval/core/modern_cardmask.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    PE_MONKER_KEY_OK = 0,
    PE_MONKER_KEY_ERR_NULL_ARGUMENT,
    PE_MONKER_KEY_ERR_INVALID_LAYOUT,
    PE_MONKER_KEY_ERR_INVALID_BOARD,
    PE_MONKER_KEY_ERR_NOT_INVERTIBLE
} pe_monker_key_status_t;

/* Decode a board only when the caller supplies a documented packed layout. */
pe_monker_key_status_t pe_monker_key_decode_packed_board(
    uint64_t key, unsigned shift, unsigned card_count, mask_t *out_board);

/* Raw Monker infoset keys are hashes unless an external layout is supplied. */
pe_monker_key_status_t pe_monker_key_decode_board(
    uint64_t raw_key, mask_t *out_board);

const char *pe_monker_key_status_string(pe_monker_key_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* PE_MONKER_KEY_H */
