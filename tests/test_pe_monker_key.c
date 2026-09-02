/* Raw Monker keys are rejected; documented packed layouts are decoded. */

#include <poker_eval/solver/pe_monker_key.h>

#include <stdint.h>
#include <stdio.h>

int main(void)
{
    mask_t board = MASK_EMPTY;
    mask_t packed = mask_set(mask_set(mask_set(MASK_EMPTY, 0), 13), 26);
    if (pe_monker_key_decode_board(UINT64_C(0x123456789abcdef0), &board) !=
            PE_MONKER_KEY_ERR_NOT_INVERTIBLE ||
        pe_monker_key_decode_packed_board(packed, 0u, 3u, &board) !=
            PE_MONKER_KEY_OK || board != packed)
    {
        fprintf(stderr, "test_pe_monker_key: safe decoding failed\n");
        return 1;
    }
    puts("test_pe_monker_key: hash refusal and packed decode passed");
    return 0;
}
