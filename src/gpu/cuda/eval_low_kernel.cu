/*
 * eval_low_kernel.cu: CUDA kernels for low hand evaluation
 *
 * Implements A-5 lowball (Razz, Omaha8, Stud8) and 2-7 lowball evaluation
 */

#include <cuda_runtime.h>
#include <stdint.h>

#include "eval_cuda_device_common.cuh"
#include <poker_eval/core/low_qualifier.h>
#include <poker_eval/deck/deck_std.h>

typedef uint32_t LowHandVal;

/* Low hand value constants */
#define LowHandVal_NOTHING 0xFFFFFFFFU
#define LowHandVal_HANDTYPE_SHIFT 24
#define LowHandVal_TOP_CARD_SHIFT 16
#define LowHandVal_SECOND_CARD_SHIFT 12
#define LowHandVal_THIRD_CARD_SHIFT 8
#define LowHandVal_FOURTH_CARD_SHIFT 4
#define LowHandVal_FIFTH_CARD_SHIFT 0

/* Card rank values for low evaluation */
#define CARD_ACE 12
#define CARD_TWO 0
#define CARD_THREE 1
#define CARD_FOUR 2
#define CARD_FIVE 3
#define CARD_SIX 4
#define CARD_SEVEN 5
#define CARD_EIGHT 6

/*
 * Evaluate A-5 lowball hand (Ace is low, straights/flushes don't count)
 * Lower HandVal = better low hand
 * Returns LowHandVal_NOTHING if no qualifying low
 */
__device__ LowHandVal cuda_eval_low_a5(CudaCardMask cards, int n_cards) {
    uint32_t sc, sd, sh, ss;

    ss = cards.cards[0]; /* spades */
    sh = cards.cards[1]; /* hearts */
    sd = cards.cards[2]; /* diamonds */
    sc = cards.cards[3]; /* clubs */

    /* Get all ranks present (treating Ace as rank 0 for low) */
    uint32_t ranks = sc | sd | sh | ss;

    /* For A-5 low, Ace plays as lowest card (rank 0 in our representation is 2,
       but we need to map Ace from rank 12 to be lowest) */

    /* Check if Ace is present (rank 12) and map it to low position */
    uint32_t low_ranks = ranks & 0x0FFF; /* Ranks 0-11 (2 through King) */
    if (ranks & (1U << CARD_ACE)) {
        low_ranks |= 1U; /* Set bit 0 to represent Ace as lowest card */
    }

    /* Count how many different ranks we have */
    int n_ranks = d_nBitsTable[low_ranks & 0x1FFF];

    /* We need at least 5 different ranks for a low hand */
    if (n_ranks < 5) {
        return LowHandVal_NOTHING;
    }

    /* Find the best 5 cards (5 lowest ranks) */
    /* For A-5 low, we want: A-2-3-4-5 (wheel) is best, then A-2-3-4-6, etc. */

    uint32_t best_low_ranks = 0;
    int cards_selected = 0;

    /* Select cards from lowest to highest (Ace=0, 2=1, 3=2, ..., 8=7) */
    for (int rank = 0; rank <= 12 && cards_selected < 5; rank++) {
        int check_rank = rank;

        /* Map rank 0 check to Ace (rank 12) in original representation */
        if (rank == 0 && (ranks & (1U << CARD_ACE))) {
            best_low_ranks |= 1U; /* Mark Ace as selected at position 0 */
            cards_selected++;
            continue;
        }

        /* Check ranks 2-8 (indices 0-6 in original, but we skip Ace) */
        if (rank > 0 && rank <= 7) {
            check_rank = rank - 1; /* Map to actual rank (2=0, 3=1, ..., 8=6) */
        } else if (rank > 7) {
            break; /* We only want 8-or-better for qualifying lows */
        } else {
            continue;
        }

        if (low_ranks & (1U << check_rank)) {
            best_low_ranks |= (1U << rank);
            cards_selected++;
        }
    }

    if (cards_selected < 5) {
        return LowHandVal_NOTHING;
    }

    /* Build low hand value (lower is better) */
    /* Extract the 5 lowest ranks and encode them */
    LowHandVal result = 0;
    int shift = LowHandVal_TOP_CARD_SHIFT;

    for (int rank = 0; rank <= 12 && shift >= 0; rank++) {
        if (best_low_ranks & (1U << rank)) {
            result |= (rank << shift);
            shift -= 4;
        }
    }

    /* Invert the result so lower values are better */
    /* This way A-2-3-4-5 (wheel) has the highest HandVal */
    result = ~result;

    return result;
}

/*
 * Evaluate 2-7 lowball hand (Ace is high, straights/flushes count against you)
 * Lower HandVal = better low hand
 */
__device__ LowHandVal cuda_eval_low_27(CudaCardMask cards, int n_cards) {
    int ranks[52];
    int suits[52];
    int count = 0;

    for (int suit = 0; suit < 4; suit++) {
        uint32_t mask = cards.cards[suit];
        while (mask) {
            int rank = __ffs(mask) - 1;
            mask &= mask - 1;
            ranks[count] = rank;
            suits[count] = suit;
            count++;
        }
    }

    if (count < 5)
        return LowHandVal_NOTHING;

    LowHandVal best = LowHandVal_NOTHING;

    for (int i = 0; i < count - 4; i++) {
        for (int j = i + 1; j < count - 3; j++) {
            for (int k = j + 1; k < count - 2; k++) {
                for (int l = k + 1; l < count - 1; l++) {
                    for (int m = l + 1; m < count; m++) {
                        int combo_ranks[5] = {
                            ranks[i], ranks[j], ranks[k], ranks[l], ranks[m]};
                        int combo_suits[5] = {
                            suits[i], suits[j], suits[k], suits[l], suits[m]};

                        bool flush = true;
                        for (int t = 1; t < 5; t++) {
                            if (combo_suits[t] != combo_suits[0]) {
                                flush = false;
                                break;
                            }
                        }
                        if (flush)
                            continue;

                        uint32_t rank_mask = 0;
                        int low = 13, high = -1;
                        for (int t = 0; t < 5; t++) {
                            int r = combo_ranks[t];
                            rank_mask |= (1U << r);
                            if (r < low) low = r;
                            if (r > high) high = r;
                        }
                        int distinct = __popc(rank_mask);
                        bool straight = (distinct == 5 && high - low == 4);
                        if (straight)
                            continue;

                        int sorted[5];
                        for (int t = 0; t < 5; t++)
                            sorted[t] = combo_ranks[t];
                        for (int a = 0; a < 5; a++) {
                            for (int b = a + 1; b < 5; b++) {
                                if (sorted[a] < sorted[b]) {
                                    int tmp = sorted[a];
                                    sorted[a] = sorted[b];
                                    sorted[b] = tmp;
                                }
                            }
                        }

                        LowHandVal val = 0;
                        int shift = LowHandVal_TOP_CARD_SHIFT;
                        for (int t = 0; t < 5; t++) {
                            val |= (sorted[t] << shift);
                            shift -= 4;
                        }

                        if (best == LowHandVal_NOTHING || val < best)
                            best = val;
                    }
                }
            }
        }
    }

    return best;
}

/*
 * Check if a low hand qualifies under 8-or-better rule
 * Returns 1 if qualifies, 0 otherwise
 */
static __device__ __forceinline__ int rotate_rank_for_low(int rank) {
    return (rank == StdDeck_Rank_ACE) ? 0 : (rank + 1);
}

static __device__ __forceinline__ int extract_highest_rank(LowHandVal low) {
    return (low >> LowHandVal_TOP_CARD_SHIFT) & 0xF;
}

__device__ int cuda_low_qualifies(LowHandVal lo, int qualifier) {
    if (lo == LowHandVal_NOTHING)
        return 0;

    int worst_rank = extract_highest_rank(lo);
    if (qualifier == LOW_QUALIFIER_NONE)
        return 1;
    if (qualifier == LOW_QUALIFIER_8)
        return worst_rank <= rotate_rank_for_low(StdDeck_Rank_8);
    if (qualifier == LOW_QUALIFIER_7)
        return worst_rank <= rotate_rank_for_low(StdDeck_Rank_7);
    return 0;
}

/*
 * Export the low evaluation function
 */
extern "C" __device__ LowHandVal cuda_eval_low_a5_device(CudaCardMask cards, int n_cards) {
    return cuda_eval_low_a5(cards, n_cards);
}

extern "C" __device__ int cuda_low_qualifies_device(LowHandVal lo, int qualifier) {
    return cuda_low_qualifies(lo, qualifier);
}

extern "C" __device__ LowHandVal cuda_eval_low_27_device(CudaCardMask cards, int n_cards) {
    return cuda_eval_low_27(cards, n_cards);
}
