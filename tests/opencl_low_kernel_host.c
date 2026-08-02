/*
 * Compiles the OpenCL low evaluator as ordinary C, so its logic can be checked
 * against the CPU without a GPU or an OpenCL runtime.
 *
 * The kernel file carries its own copies of the HandVal macros, which collide
 * with the CPU headers, so it lives alone in this translation unit and is
 * reached only through the two wrappers below.  Keep this file free of any
 * poker-eval header.
 *
 * eval_low_kernel.cl holds no __kernel entry point -- it is a device-side
 * library included by the other kernels -- so two typedefs are all it takes to
 * build it for the host.  If that stops being true, the shims below are what
 * needs extending.  The kernel declares its functions "static inline", which
 * host C99 accepts as is; do not paper over a plain "inline" there by
 * redefining the keyword, as the resulting "static static" is a duplicate
 * storage-class specifier that not every compiler merely warns about.
 */

#include <stdint.h>

typedef uint32_t uint;
typedef uint8_t uchar;
#define __constant

#include "../src/gpu/opencl/eval_low_kernel.cl"

extern uint8_t nBitsTable[8192];

uint32_t opencl_host_eval_low_a5(uint32_t spades, uint32_t clubs,
                                 uint32_t diamonds, uint32_t hearts,
                                 int n_cards);
int opencl_host_low_qualifies(uint32_t low, int qualifier_rank);

uint32_t opencl_host_eval_low_a5(uint32_t spades, uint32_t clubs,
                                 uint32_t diamonds, uint32_t hearts,
                                 int n_cards) {
    OpenCLCardMask mask;
    mask.cards_n = (uint)n_cards;
    mask.cards[0] = spades;
    mask.cards[1] = clubs;
    mask.cards[2] = diamonds;
    mask.cards[3] = hearts;
    return opencl_eval_low_a5(mask, n_cards, nBitsTable);
}

int opencl_host_low_qualifies(uint32_t low, int qualifier_rank) {
    return opencl_low_qualifies(low, qualifier_rank);
}
