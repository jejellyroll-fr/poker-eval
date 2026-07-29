/*
 * eval_holdem_batch.cl - OpenCL kernel for batched Hold'em 7→5 evaluation
 *
 * Copyright (C) 2025 poker-eval contributors
 *
 * High-performance OpenCL kernel for evaluating thousands/millions of
 * 7-card poker hands in parallel. Mirrors StdDeck_StdRules_EVAL_N for
 * 5-card evaluation with lookup tables loaded from poker-eval.
 *
 * Approach:
 * - Each work-item evaluates one 7-card hand
 * - Enumerate all C(7,5)=21 combinations in parallel
 * - Uses poker-eval lookup tables for fast 5-card evaluation
 * - Coalesced memory access via SOA layout
 *
 * Compatible with OpenCL 1.2+ (AMD, Intel, NVIDIA GPUs)
 */

/* Card representation: 0-51 (standard deck) */
#define DECK_SIZE 52
#define NUM_RANKS 13
#define NUM_SUITS 4

/* HandVal type (matches poker-eval) */
typedef uint HandVal;

/* ===== HandVal encoding (matches handval.h) ===== */

#define HandVal_HANDTYPE_SHIFT 24
#define HandVal_HANDTYPE_VALUE(ht) ((ht) << HandVal_HANDTYPE_SHIFT)
#define HandVal_TOP_CARD_VALUE(c) ((c) << 16)
#define HandVal_SECOND_CARD_VALUE(c) ((c) << 12)
#define HandVal_THIRD_CARD_VALUE(c) ((c) << 8)
#define HandVal_FOURTH_CARD_VALUE(c) ((c) << 4)
#define HandVal_FIFTH_CARD_VALUE(c) ((c) << 0)
#define HandVal_CARD_WIDTH 4
#define HandVal_FIFTH_CARD_MASK 0x0000000F
#define HandVal_TOP_CARD_MASK 0x000F0000
#define HandVal_SECOND_CARD_MASK 0x0000F000
#define HandVal_THIRD_CARD_MASK 0x00000F00

/* Standard hand types (matches StdRules_HandType_*) */
#define StdRules_HandType_NOPAIR    0
#define StdRules_HandType_ONEPAIR   1
#define StdRules_HandType_TWOPAIR   2
#define StdRules_HandType_TRIPS     3
#define StdRules_HandType_STRAIGHT  4
#define StdRules_HandType_FLUSH     5
#define StdRules_HandType_FULLHOUSE 6
#define StdRules_HandType_QUADS     7
#define StdRules_HandType_STFLUSH   8

/* ===== All 21 C(7,5) combinations for Hold'em ===== */
__constant int HOLDEM_COMBOS[21][5] = {
    {0,1,2,3,4}, {0,1,2,3,5}, {0,1,2,3,6}, {0,1,2,4,5},
    {0,1,2,4,6}, {0,1,2,5,6}, {0,1,3,4,5}, {0,1,3,4,6},
    {0,1,3,5,6}, {0,1,4,5,6}, {0,2,3,4,5}, {0,2,3,4,6},
    {0,2,3,5,6}, {0,2,4,5,6}, {0,3,4,5,6}, {1,2,3,4,5},
    {1,2,3,4,6}, {1,2,3,5,6}, {1,2,4,5,6}, {1,3,4,5,6},
    {2,3,4,5,6}
};

/* ===== Omaha C(4,2) hole card combinations ===== */
__constant int OMAHA_HOLE_COMBOS[6][2] = {
    {0,1}, {0,2}, {0,3}, {1,2}, {1,3}, {2,3}
};

/* ===== Omaha C(5,3) board card combinations ===== */
__constant int OMAHA_BOARD_COMBOS[10][3] = {
    {0,1,2}, {0,1,3}, {0,1,4}, {0,2,3}, {0,2,4},
    {0,3,4}, {1,2,3}, {1,2,4}, {1,3,4}, {2,3,4}
};

/* ===== Omaha5 C(5,2) hole card combinations ===== */
__constant int OMAHA5_HOLE_COMBOS[10][2] = {
    {0,1}, {0,2}, {0,3}, {0,4}, {1,2},
    {1,3}, {1,4}, {2,3}, {2,4}, {3,4}
};

/* ===== Omaha6 C(6,2) hole card combinations ===== */
__constant int OMAHA6_HOLE_COMBOS[15][2] = {
    {0,1}, {0,2}, {0,3}, {0,4}, {0,5},
    {1,2}, {1,3}, {1,4}, {1,5}, {2,3},
    {2,4}, {2,5}, {3,4}, {3,5}, {4,5}
};

/* ===== Helper functions ===== */

/**
 * Evaluate a 5-card hand using poker-eval lookup tables
 *
 * This mirrors the CPU StdDeck_StdRules_EVAL_N logic exactly:
 * - Compute suit masks and rank union
 * - Check for flushes, straights, straight flushes
 * - Handle pairs, trips, quads, full houses via n_dups
 *
 * Returns HandVal (higher is better)
 */
inline HandVal eval_5cards(
    const uchar cards[5],
    __global const uchar* nbits_table,
    __global const uchar* straight_table,
    __global const uchar* top_card_table,
    __global const uint* top_five_table
) {
    /* Build per-suit rank masks */
    uint sc = 0;  /* clubs */
    uint sd = 0;  /* diamonds */
    uint sh = 0;  /* hearts */
    uint ss = 0;  /* spades */

    /* Card encoding: card = rank + suit * 13, where:
     * rank: 0-12 (2-A)
     * suit: 0=hearts, 1=diamonds, 2=clubs, 3=spades
     */
    for (int i = 0; i < 5; i++) {
        uint card = (uint)cards[i];
        uint rank = card % NUM_RANKS;
        uint suit = card / NUM_RANKS;
        uint bit = 1u << rank;

        if (suit == 0u) {
            sh |= bit;  /* hearts */
        } else if (suit == 1u) {
            sd |= bit;  /* diamonds */
        } else if (suit == 2u) {
            sc |= bit;  /* clubs */
        } else {
            ss |= bit;  /* spades */
        }
    }

    uint ranks = sc | sd | sh | ss;
    uint n_ranks = (uint)nbits_table[ranks];
    uint n_dups = 5u - n_ranks;
    HandVal retval = 0;

    /* Check for flush/straight/straight-flush (need at least 5 distinct ranks) */
    if (n_ranks >= 5u) {
        /* Check each suit for flush */
        if (nbits_table[ss] >= 5u) {
            uint st = (uint)straight_table[ss];
            if (st) {
                return HandVal_HANDTYPE_VALUE(StdRules_HandType_STFLUSH) +
                       HandVal_TOP_CARD_VALUE(st);
            }
            retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_FLUSH) +
                     top_five_table[ss];
        } else if (nbits_table[sc] >= 5u) {
            uint st = (uint)straight_table[sc];
            if (st) {
                return HandVal_HANDTYPE_VALUE(StdRules_HandType_STFLUSH) +
                       HandVal_TOP_CARD_VALUE(st);
            }
            retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_FLUSH) +
                     top_five_table[sc];
        } else if (nbits_table[sd] >= 5u) {
            uint st = (uint)straight_table[sd];
            if (st) {
                return HandVal_HANDTYPE_VALUE(StdRules_HandType_STFLUSH) +
                       HandVal_TOP_CARD_VALUE(st);
            }
            retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_FLUSH) +
                     top_five_table[sd];
        } else if (nbits_table[sh] >= 5u) {
            uint st = (uint)straight_table[sh];
            if (st) {
                return HandVal_HANDTYPE_VALUE(StdRules_HandType_STFLUSH) +
                       HandVal_TOP_CARD_VALUE(st);
            }
            retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_FLUSH) +
                     top_five_table[sh];
        } else {
            /* No flush, check for straight */
            uint st = (uint)straight_table[ranks];
            if (st) {
                retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_STRAIGHT) +
                         HandVal_TOP_CARD_VALUE(st);
            }
        }

        /* Early exit if we have a made hand and can't have FH/quads */
        if (retval && n_dups < 3u) {
            return retval;
        }
    }

    /* Handle paired hands based on number of duplicates */
    switch (n_dups) {
    case 0u:
        /* No pair - just high cards */
        return HandVal_HANDTYPE_VALUE(StdRules_HandType_NOPAIR) +
               top_five_table[ranks];

    case 1u: {
        /* One pair */
        uint two_mask = ranks ^ (sc ^ sd ^ sh ^ ss);
        uint t = ranks ^ two_mask;
        HandVal pair_val = HandVal_HANDTYPE_VALUE(StdRules_HandType_ONEPAIR) +
                           HandVal_TOP_CARD_VALUE((uint)top_card_table[two_mask]);
        /* Get three kickers from remaining cards */
        uint kickers = (top_five_table[t] >> HandVal_CARD_WIDTH) &
                       ~HandVal_FIFTH_CARD_MASK;
        return pair_val + kickers;
    }

    case 2u: {
        /* Either two pair or trips */
        uint two_mask = ranks ^ (sc ^ sd ^ sh ^ ss);
        if (two_mask) {
            /* Two pair */
            uint t = ranks ^ two_mask;
            return HandVal_HANDTYPE_VALUE(StdRules_HandType_TWOPAIR) +
                   (top_five_table[two_mask] & (HandVal_TOP_CARD_MASK | HandVal_SECOND_CARD_MASK)) +
                   HandVal_THIRD_CARD_VALUE((uint)top_card_table[t]);
        }

        /* Trips (three of a kind) */
        uint three_mask = ((sc & sd) | (sh & ss)) & ((sc & sh) | (sd & ss));
        HandVal trips_val = HandVal_HANDTYPE_VALUE(StdRules_HandType_TRIPS) +
                            HandVal_TOP_CARD_VALUE((uint)top_card_table[three_mask]);
        uint t = ranks ^ three_mask;
        uint second = (uint)top_card_table[t];
        trips_val += HandVal_SECOND_CARD_VALUE(second);
        t ^= (1u << second);
        trips_val += HandVal_THIRD_CARD_VALUE((uint)top_card_table[t]);
        return trips_val;
    }

    default: {
        /* Quads, full house, or two pair with 3+ duplicates */
        uint four_mask = sh & sd & sc & ss;
        if (four_mask) {
            /* Four of a kind */
            uint tc = (uint)top_card_table[four_mask];
            return HandVal_HANDTYPE_VALUE(StdRules_HandType_QUADS) +
                   HandVal_TOP_CARD_VALUE(tc) +
                   HandVal_SECOND_CARD_VALUE((uint)top_card_table[ranks ^ (1u << tc)]);
        }

        uint two_mask = ranks ^ (sc ^ sd ^ sh ^ ss);
        if (nbits_table[two_mask] != n_dups) {
            /* Full house (trips + pair) */
            uint three_mask = ((sc & sd) | (sh & ss)) & ((sc & sh) | (sd & ss));
            HandVal fh_val = HandVal_HANDTYPE_VALUE(StdRules_HandType_FULLHOUSE);
            uint tc = (uint)top_card_table[three_mask];
            fh_val += HandVal_TOP_CARD_VALUE(tc);
            uint t = (two_mask | three_mask) ^ (1u << tc);
            fh_val += HandVal_SECOND_CARD_VALUE((uint)top_card_table[t]);
            return fh_val;
        }

        /* Check if we already found a flush/straight */
        if (retval) {
            return retval;
        }

        /* Two pair (fallback case) */
        uint top = (uint)top_card_table[two_mask];
        uint second = (uint)top_card_table[two_mask ^ (1u << top)];
        return HandVal_HANDTYPE_VALUE(StdRules_HandType_TWOPAIR) +
               HandVal_TOP_CARD_VALUE(top) +
               HandVal_SECOND_CARD_VALUE(second) +
               HandVal_THIRD_CARD_VALUE((uint)top_card_table[ranks ^ (1u << top) ^ (1u << second)]);
    }
    }
}

/* ===== Main evaluation kernels ===== */

/**
 * Evaluate batch of 7-card Hold'em hands (basic kernel)
 *
 * Each work-item handles one hand:
 * - Enumerate all 21 combinations of 5 cards
 * - Evaluate each combination using poker-eval tables
 * - Return best hand value
 *
 * @param hands           Input: 7 cards per hand [batch_size * 7]
 * @param batch_size      Number of hands
 * @param out_values      Output: HandVal per hand [batch_size]
 * @param nbits_table     Lookup table: number of bits set [8192]
 * @param straight_table  Lookup table: straight detection [8192]
 * @param top_card_table  Lookup table: highest rank [8192]
 * @param top_five_table  Lookup table: top five cards encoded [8192]
 */
__kernel void eval_holdem_batch_kernel(
    __global const uchar* restrict hands,
    const ulong batch_size,
    __global uint* restrict out_values,
    __global const uchar* restrict nbits_table,
    __global const uchar* restrict straight_table,
    __global const uchar* restrict top_card_table,
    __global const uint* restrict top_five_table
) {
    int idx = get_global_id(0);

    if (idx >= batch_size) return;

    /* Load 7 cards for this hand */
    uchar cards[7];
    for (int i = 0; i < 7; i++) {
        cards[i] = hands[idx * 7 + i];
    }

    /* Enumerate all C(7,5)=21 combinations */
    HandVal best = 0;

    #pragma unroll
    for (int c = 0; c < 21; c++) {
        uchar subset[5];
        #pragma unroll
        for (int i = 0; i < 5; i++) {
            subset[i] = cards[HOLDEM_COMBOS[c][i]];
        }

        HandVal val = eval_5cards(subset, nbits_table, straight_table,
                                  top_card_table, top_five_table);
        best = max(best, val);
    }

    out_values[idx] = best;
}

/**
 * Optimized kernel using local memory for lookup tables
 *
 * Loads lookup tables into local (shared) memory for faster access
 *
 * @param hands           Input: 7 cards per hand [batch_size * 7]
 * @param batch_size      Number of hands
 * @param out_values      Output: HandVal per hand [batch_size]
 * @param nbits_table     Lookup table: number of bits set [8192]
 * @param straight_table  Lookup table: straight detection [8192]
 * @param top_card_table  Lookup table: highest rank [8192]
 * @param top_five_table  Lookup table: top five cards encoded [8192]
 * @param s_local_mem     Local memory for caching (32KB minimum)
 */
__kernel void eval_holdem_batch_kernel_opt(
    __global const uchar* restrict hands,
    const ulong batch_size,
    __global uint* restrict out_values,
    __global const uchar* restrict nbits_table,
    __global const uchar* restrict straight_table,
    __global const uchar* restrict top_card_table,
    __global const uint* restrict top_five_table,
    __local uint* s_local_mem
) {
    int global_id = get_global_id(0);
    int local_id = get_local_id(0);
    int local_size = get_local_size(0);

    /* Local memory layout:
     * [0..8191]: top_five_table (32KB)
     * For other tables we use global memory (they're small reads)
     */
    __local uint* s_top_five = s_local_mem;

    /* Cooperative loading of top_five_table to local memory */
    for (int i = local_id; i < 8192; i += local_size) {
        s_top_five[i] = top_five_table[i];
    }
    barrier(CLK_LOCAL_MEM_FENCE);

    if (global_id >= batch_size) return;

    /* Load 7 cards for this hand */
    uchar cards[7];
    for (int i = 0; i < 7; i++) {
        cards[i] = hands[global_id * 7 + i];
    }

    HandVal best = 0;

    #pragma unroll
    for (int c = 0; c < 21; c++) {
        uchar subset[5];
        #pragma unroll
        for (int i = 0; i < 5; i++) {
            subset[i] = cards[HOLDEM_COMBOS[c][i]];
        }

        /* Inline evaluation using local memory for top_five_table */
        uint sc = 0, sd = 0, sh = 0, ss = 0;

        for (int i = 0; i < 5; i++) {
            uint card = (uint)subset[i];
            uint rank = card % NUM_RANKS;
            uint suit = card / NUM_RANKS;
            uint bit = 1u << rank;

            if (suit == 0u) sh |= bit;
            else if (suit == 1u) sd |= bit;
            else if (suit == 2u) sc |= bit;
            else ss |= bit;
        }

        uint ranks = sc | sd | sh | ss;
        uint n_ranks = (uint)nbits_table[ranks];
        uint n_dups = 5u - n_ranks;
        HandVal val = 0;

        if (n_ranks >= 5u) {
            if (nbits_table[ss] >= 5u) {
                uint st = (uint)straight_table[ss];
                if (st) {
                    val = HandVal_HANDTYPE_VALUE(StdRules_HandType_STFLUSH) +
                          HandVal_TOP_CARD_VALUE(st);
                } else {
                    val = HandVal_HANDTYPE_VALUE(StdRules_HandType_FLUSH) +
                          s_top_five[ss];
                }
            } else if (nbits_table[sc] >= 5u) {
                uint st = (uint)straight_table[sc];
                if (st) {
                    val = HandVal_HANDTYPE_VALUE(StdRules_HandType_STFLUSH) +
                          HandVal_TOP_CARD_VALUE(st);
                } else {
                    val = HandVal_HANDTYPE_VALUE(StdRules_HandType_FLUSH) +
                          s_top_five[sc];
                }
            } else if (nbits_table[sd] >= 5u) {
                uint st = (uint)straight_table[sd];
                if (st) {
                    val = HandVal_HANDTYPE_VALUE(StdRules_HandType_STFLUSH) +
                          HandVal_TOP_CARD_VALUE(st);
                } else {
                    val = HandVal_HANDTYPE_VALUE(StdRules_HandType_FLUSH) +
                          s_top_five[sd];
                }
            } else if (nbits_table[sh] >= 5u) {
                uint st = (uint)straight_table[sh];
                if (st) {
                    val = HandVal_HANDTYPE_VALUE(StdRules_HandType_STFLUSH) +
                          HandVal_TOP_CARD_VALUE(st);
                } else {
                    val = HandVal_HANDTYPE_VALUE(StdRules_HandType_FLUSH) +
                          s_top_five[sh];
                }
            } else {
                uint st = (uint)straight_table[ranks];
                if (st) {
                    val = HandVal_HANDTYPE_VALUE(StdRules_HandType_STRAIGHT) +
                          HandVal_TOP_CARD_VALUE(st);
                }
            }

            if (val && n_dups < 3u) {
                best = max(best, val);
                continue;
            }
        }

        if (val == 0) {
            switch (n_dups) {
            case 0u:
                val = HandVal_HANDTYPE_VALUE(StdRules_HandType_NOPAIR) +
                      s_top_five[ranks];
                break;
            case 1u: {
                uint two_mask = ranks ^ (sc ^ sd ^ sh ^ ss);
                uint t = ranks ^ two_mask;
                val = HandVal_HANDTYPE_VALUE(StdRules_HandType_ONEPAIR) +
                      HandVal_TOP_CARD_VALUE((uint)top_card_table[two_mask]);
                uint kickers = (s_top_five[t] >> HandVal_CARD_WIDTH) &
                               ~HandVal_FIFTH_CARD_MASK;
                val += kickers;
                break;
            }
            case 2u: {
                uint two_mask = ranks ^ (sc ^ sd ^ sh ^ ss);
                if (two_mask) {
                    uint t = ranks ^ two_mask;
                    val = HandVal_HANDTYPE_VALUE(StdRules_HandType_TWOPAIR) +
                          (s_top_five[two_mask] & (HandVal_TOP_CARD_MASK | HandVal_SECOND_CARD_MASK)) +
                          HandVal_THIRD_CARD_VALUE((uint)top_card_table[t]);
                } else {
                    uint three_mask = ((sc & sd) | (sh & ss)) & ((sc & sh) | (sd & ss));
                    val = HandVal_HANDTYPE_VALUE(StdRules_HandType_TRIPS) +
                          HandVal_TOP_CARD_VALUE((uint)top_card_table[three_mask]);
                    uint t = ranks ^ three_mask;
                    uint second = (uint)top_card_table[t];
                    val += HandVal_SECOND_CARD_VALUE(second);
                    t ^= (1u << second);
                    val += HandVal_THIRD_CARD_VALUE((uint)top_card_table[t]);
                }
                break;
            }
            default: {
                uint four_mask = sh & sd & sc & ss;
                if (four_mask) {
                    uint tc = (uint)top_card_table[four_mask];
                    val = HandVal_HANDTYPE_VALUE(StdRules_HandType_QUADS) +
                          HandVal_TOP_CARD_VALUE(tc) +
                          HandVal_SECOND_CARD_VALUE((uint)top_card_table[ranks ^ (1u << tc)]);
                } else {
                    uint two_mask = ranks ^ (sc ^ sd ^ sh ^ ss);
                    if (nbits_table[two_mask] != n_dups) {
                        uint three_mask = ((sc & sd) | (sh & ss)) & ((sc & sh) | (sd & ss));
                        uint tc = (uint)top_card_table[three_mask];
                        val = HandVal_HANDTYPE_VALUE(StdRules_HandType_FULLHOUSE) +
                              HandVal_TOP_CARD_VALUE(tc);
                        uint t = (two_mask | three_mask) ^ (1u << tc);
                        val += HandVal_SECOND_CARD_VALUE((uint)top_card_table[t]);
                    } else {
                        uint top = (uint)top_card_table[two_mask];
                        uint second = (uint)top_card_table[two_mask ^ (1u << top)];
                        val = HandVal_HANDTYPE_VALUE(StdRules_HandType_TWOPAIR) +
                              HandVal_TOP_CARD_VALUE(top) +
                              HandVal_SECOND_CARD_VALUE(second) +
                              HandVal_THIRD_CARD_VALUE((uint)top_card_table[ranks ^ (1u << top) ^ (1u << second)]);
                    }
                }
                break;
            }
            }
        }

        best = max(best, val);
    }

    out_values[global_id] = best;
}

/**
 * Equity calculation kernel
 *
 * Evaluate hero and villain hands and compute equity (wins/ties/losses)
 *
 * @param hero0/hero1     Hero's 2 hole cards
 * @param villain0/1      Villain's 2 hole cards
 * @param boards          Array of 5-card boards [num_boards * 5]
 * @param num_boards      Number of boards
 * @param out_wins        Atomically incremented for hero wins
 * @param out_ties        Atomically incremented for ties
 * @param out_losses      Atomically incremented for hero losses
 * @param nbits_table     Lookup table: number of bits set [8192]
 * @param straight_table  Lookup table: straight detection [8192]
 * @param top_card_table  Lookup table: highest rank [8192]
 * @param top_five_table  Lookup table: top five cards encoded [8192]
 */
__kernel void eval_equity_kernel(
    const uchar hero0,
    const uchar hero1,
    const uchar villain0,
    const uchar villain1,
    __global const uchar* restrict boards,
    const ulong num_boards,
    __global volatile ulong* out_wins,
    __global volatile ulong* out_ties,
    __global volatile ulong* out_losses,
    __global const uchar* restrict nbits_table,
    __global const uchar* restrict straight_table,
    __global const uchar* restrict top_card_table,
    __global const uint* restrict top_five_table
) {
    int idx = get_global_id(0);
    if (idx >= num_boards) return;

    /* Construct 7-card hands */
    uchar hero_7[7], villain_7[7];

    hero_7[0] = hero0;
    hero_7[1] = hero1;

    villain_7[0] = villain0;
    villain_7[1] = villain1;

    for (int i = 0; i < 5; i++) {
        hero_7[2 + i] = boards[idx * 5 + i];
        villain_7[2 + i] = boards[idx * 5 + i];
    }

    /* Evaluate hero's best 5-card hand */
    HandVal hero_best = 0;

    #pragma unroll
    for (int c = 0; c < 21; c++) {
        uchar subset[5];
        #pragma unroll
        for (int i = 0; i < 5; i++) {
            subset[i] = hero_7[HOLDEM_COMBOS[c][i]];
        }
        HandVal val = eval_5cards(subset, nbits_table, straight_table,
                                  top_card_table, top_five_table);
        hero_best = max(hero_best, val);
    }

    /* Evaluate villain's best 5-card hand */
    HandVal villain_best = 0;

    #pragma unroll
    for (int c = 0; c < 21; c++) {
        uchar subset[5];
        #pragma unroll
        for (int i = 0; i < 5; i++) {
            subset[i] = villain_7[HOLDEM_COMBOS[c][i]];
        }
        HandVal val = eval_5cards(subset, nbits_table, straight_table,
                                  top_card_table, top_five_table);
        villain_best = max(villain_best, val);
    }

    /* Compare and increment counters atomically */
    if (hero_best > villain_best) {
        atomic_add(out_wins, 1UL);
    } else if (hero_best == villain_best) {
        atomic_add(out_ties, 1UL);
    } else {
        atomic_add(out_losses, 1UL);
    }
}

/**
 * Omaha-4 batch evaluation kernel
 *
 * Each work-item evaluates one 9-card Omaha hand (4 hole + 5 board)
 * Must use exactly 2 hole cards and 3 board cards
 *
 * @param hands           Input: 9 cards per hand [batch_size * 9] (4 hole + 5 board)
 * @param batch_size      Number of hands
 * @param out_values      Output: HandVal per hand [batch_size]
 * @param nbits_table     Lookup table: number of bits set [8192]
 * @param straight_table  Lookup table: straight detection [8192]
 * @param top_card_table  Lookup table: highest rank [8192]
 * @param top_five_table  Lookup table: top five cards encoded [8192]
 */
__kernel void eval_omaha_batch_kernel(
    __global const uchar* restrict hands,
    const ulong batch_size,
    __global uint* restrict out_values,
    __global const uchar* restrict nbits_table,
    __global const uchar* restrict straight_table,
    __global const uchar* restrict top_card_table,
    __global const uint* restrict top_five_table
) {
    int idx = get_global_id(0);
    if (idx >= batch_size) return;

    /* Load 9 cards: 4 hole + 5 board */
    uchar hole[4], board[5];

    for (int i = 0; i < 4; i++) {
        hole[i] = hands[idx * 9 + i];
    }
    for (int i = 0; i < 5; i++) {
        board[i] = hands[idx * 9 + 4 + i];
    }

    HandVal best = 0;

    /* Enumerate C(4,2) * C(5,3) = 6 * 10 = 60 combinations */
    #pragma unroll
    for (int h = 0; h < 6; h++) {
        #pragma unroll
        for (int b = 0; b < 10; b++) {
            uchar hand5[5];
            hand5[0] = hole[OMAHA_HOLE_COMBOS[h][0]];
            hand5[1] = hole[OMAHA_HOLE_COMBOS[h][1]];
            hand5[2] = board[OMAHA_BOARD_COMBOS[b][0]];
            hand5[3] = board[OMAHA_BOARD_COMBOS[b][1]];
            hand5[4] = board[OMAHA_BOARD_COMBOS[b][2]];

            HandVal val = eval_5cards(hand5, nbits_table, straight_table,
                                      top_card_table, top_five_table);
            best = max(best, val);
        }
    }

    out_values[idx] = best;
}

/* ===== Low evaluation support for Razz / Hi-Lo kernels ===== */

/* LowHandVal constants */
#define LowHandVal_NOTHING \
    (HandVal_HANDTYPE_VALUE(StdRules_HandType_STFLUSH) + \
     HandVal_TOP_CARD_VALUE(12) + 1)

#define LowHandVal_WORST_EIGHT \
    (HandVal_HANDTYPE_VALUE(StdRules_HandType_NOPAIR)   \
     + HandVal_TOP_CARD_VALUE(7)                         \
     + HandVal_SECOND_CARD_VALUE(6)                      \
     + HandVal_THIRD_CARD_VALUE(5)                       \
     + HandVal_FOURTH_CARD_VALUE(4)                      \
     + HandVal_FIFTH_CARD_VALUE(3))

#define HandVal_FOURTH_CARD_VALUE(c) ((c) << 4)

/* Rotate Ace from high (rank 12) to low (bit 0) for A-5 lowball */
#define Lowball_ROTATE_RANKS(ranks) \
    ((((ranks) & ~(1U << 12)) << 1) | (((ranks) >> 12) & 0x01U))

/**
 * Evaluate A-5 low hand from 7 uchar cards (card = rank + suit*13).
 *
 * In A-5 lowball: Ace is low, straights/flushes don't count.
 * Lower result = better hand.  Best: A-2-3-4-5 (wheel).
 * Returns LowHandVal_NOTHING if fewer than 5 distinct ranks.
 *
 * Uses bottomFiveCardsTable and bottomCardTable for fast lookup.
 */
inline HandVal eval_low_a5_7cards(
    const uchar cards[7],
    __global const uint* restrict bottom_five_table,
    __global const uchar* restrict bottom_card_table,
    __global const uchar* restrict nbits_table
) {
    /* Build per-suit rank bitmasks */
    uint sc = 0, sd = 0, sh = 0, ss = 0;

    for (int i = 0; i < 7; i++) {
        uint card = (uint)cards[i];
        uint rank = card % NUM_RANKS;
        uint suit = card / NUM_RANKS;
        uint bit = 1u << rank;

        if (suit == 0u) sh |= bit;
        else if (suit == 1u) sd |= bit;
        else if (suit == 2u) sc |= bit;
        else ss |= bit;
    }

    uint ranks = sc | sd | sh | ss;

    /* Rotate ranks: Ace (bit 12) becomes bit 0 */
    uint rotated = Lowball_ROTATE_RANKS(ranks);

    uint n_unique = (uint)nbits_table[rotated & 0x1FFF];
    if (n_unique < 5u) {
        return LowHandVal_NOTHING;
    }

    /* Use bottom_five_table to pick the 5 lowest distinct ranks */
    return HandVal_HANDTYPE_VALUE(StdRules_HandType_NOPAIR)
         + bottom_five_table[rotated & 0x1FFF];
}

/**
 * Evaluate A-5 low hand from 5 uchar cards.
 * Returns LowHandVal_NOTHING if not all 5 ranks are distinct.
 */
inline HandVal eval_low_a5_5cards(
    const uchar cards[5],
    __global const uint* restrict bottom_five_table,
    __global const uchar* restrict bottom_card_table,
    __global const uchar* restrict nbits_table
) {
    uint sc = 0, sd = 0, sh = 0, ss = 0;

    for (int i = 0; i < 5; i++) {
        uint card = (uint)cards[i];
        uint rank = card % NUM_RANKS;
        uint suit = card / NUM_RANKS;
        uint bit = 1u << rank;

        if (suit == 0u) sh |= bit;
        else if (suit == 1u) sd |= bit;
        else if (suit == 2u) sc |= bit;
        else ss |= bit;
    }

    uint ranks = sc | sd | sh | ss;
    uint rotated = Lowball_ROTATE_RANKS(ranks);

    uint n_unique = (uint)nbits_table[rotated & 0x1FFF];
    if (n_unique < 5u) {
        return LowHandVal_NOTHING;
    }

    return HandVal_HANDTYPE_VALUE(StdRules_HandType_NOPAIR)
         + bottom_five_table[rotated & 0x1FFF];
}

/* ===== Stud batch kernel (7-card, high eval — same as Hold'em) ===== */

__kernel void eval_stud_batch_kernel(
    __global const uchar* restrict hands,
    const ulong batch_size,
    __global uint* restrict out_values,
    __global const uchar* restrict nbits_table,
    __global const uchar* restrict straight_table,
    __global const uchar* restrict top_card_table,
    __global const uint* restrict top_five_table
) {
    int idx = get_global_id(0);
    if (idx >= batch_size) return;

    uchar cards[7];
    for (int i = 0; i < 7; i++) {
        cards[i] = hands[idx * 7 + i];
    }

    HandVal best = 0;

    #pragma unroll
    for (int c = 0; c < 21; c++) {
        uchar subset[5];
        #pragma unroll
        for (int i = 0; i < 5; i++) {
            subset[i] = cards[HOLDEM_COMBOS[c][i]];
        }

        HandVal val = eval_5cards(subset, nbits_table, straight_table,
                                  top_card_table, top_five_table);
        best = max(best, val);
    }

    out_values[idx] = best;
}

/* ===== Razz batch kernel (7-card, A-5 low eval) ===== */

__kernel void eval_razz_batch_kernel(
    __global const uchar* restrict hands,
    const ulong batch_size,
    __global uint* restrict out_lo_values,
    __global const uchar* restrict nbits_table,
    __global const uint* restrict bottom_five_table,
    __global const uchar* restrict bottom_card_table
) {
    int idx = get_global_id(0);
    if (idx >= batch_size) return;

    uchar cards[7];
    for (int i = 0; i < 7; i++) {
        cards[i] = hands[idx * 7 + i];
    }

    out_lo_values[idx] = eval_low_a5_7cards(cards, bottom_five_table,
                                             bottom_card_table, nbits_table);
}

/* ===== Omaha Hi/Lo batch kernel (4 hole + 5 board, hi + lo) ===== */

__kernel void eval_omaha_hilo_batch_kernel(
    __global const uchar* restrict hands,
    const ulong batch_size,
    __global uint* restrict out_hi_values,
    __global uint* restrict out_lo_values,
    __global int* restrict out_lo_qualifies,
    __global const uchar* restrict nbits_table,
    __global const uchar* restrict straight_table,
    __global const uchar* restrict top_card_table,
    __global const uint* restrict top_five_table,
    __global const uint* restrict bottom_five_table,
    __global const uchar* restrict bottom_card_table
) {
    int idx = get_global_id(0);
    if (idx >= batch_size) return;

    uchar hole[4], board[5];

    for (int i = 0; i < 4; i++) {
        hole[i] = hands[idx * 9 + i];
    }
    for (int i = 0; i < 5; i++) {
        board[i] = hands[idx * 9 + 4 + i];
    }

    HandVal best_hi = 0;
    HandVal best_lo = LowHandVal_NOTHING;

    #pragma unroll
    for (int h = 0; h < 6; h++) {
        #pragma unroll
        for (int b = 0; b < 10; b++) {
            uchar hand5[5];
            hand5[0] = hole[OMAHA_HOLE_COMBOS[h][0]];
            hand5[1] = hole[OMAHA_HOLE_COMBOS[h][1]];
            hand5[2] = board[OMAHA_BOARD_COMBOS[b][0]];
            hand5[3] = board[OMAHA_BOARD_COMBOS[b][1]];
            hand5[4] = board[OMAHA_BOARD_COMBOS[b][2]];

            HandVal hi_val = eval_5cards(hand5, nbits_table, straight_table,
                                         top_card_table, top_five_table);
            best_hi = max(best_hi, hi_val);

            HandVal lo_val = eval_low_a5_5cards(hand5, bottom_five_table,
                                                 bottom_card_table, nbits_table);
            if (lo_val < best_lo) {
                best_lo = lo_val;
            }
        }
    }

    out_hi_values[idx] = best_hi;
    out_lo_values[idx] = best_lo;

    if (best_lo != LowHandVal_NOTHING && best_lo <= LowHandVal_WORST_EIGHT) {
        out_lo_qualifies[idx] = 1;
    } else {
        out_lo_qualifies[idx] = 0;
    }
}

/* ===== Generic Hi/Lo batch kernel (any card count) ===== */

__kernel void eval_hilo_batch_kernel(
    __global const uchar* restrict hands,
    const ulong batch_size,
    const int cards_per_hand,
    __global uint* restrict out_hi_values,
    __global uint* restrict out_lo_values,
    __global int* restrict out_lo_qualifies,
    __global const uchar* restrict nbits_table,
    __global const uchar* restrict straight_table,
    __global const uchar* restrict top_card_table,
    __global const uint* restrict top_five_table,
    __global const uint* restrict bottom_five_table,
    __global const uchar* restrict bottom_card_table
) {
    int idx = get_global_id(0);
    if (idx >= batch_size) return;

    uchar cards[11];
    int n = min(cards_per_hand, 11);
    for (int i = 0; i < n; i++) {
        cards[i] = hands[idx * cards_per_hand + i];
    }

    HandVal best_hi = 0;
    HandVal best_lo = LowHandVal_NOTHING;

    /* Enumerate all C(n,5) combinations */
    for (int a = 0; a < n - 4; a++) {
        for (int b = a + 1; b < n - 3; b++) {
            for (int c = b + 1; c < n - 2; c++) {
                for (int d = c + 1; d < n - 1; d++) {
                    for (int e = d + 1; e < n; e++) {
                        uchar hand5[5];
                        hand5[0] = cards[a];
                        hand5[1] = cards[b];
                        hand5[2] = cards[c];
                        hand5[3] = cards[d];
                        hand5[4] = cards[e];

                        HandVal hi_val = eval_5cards(hand5, nbits_table,
                                                     straight_table,
                                                     top_card_table,
                                                     top_five_table);
                        best_hi = max(best_hi, hi_val);

                        HandVal lo_val = eval_low_a5_5cards(hand5,
                                                             bottom_five_table,
                                                             bottom_card_table,
                                                             nbits_table);
                        if (lo_val < best_lo) {
                            best_lo = lo_val;
                        }
                    }
                }
            }
        }
    }

    out_hi_values[idx] = best_hi;
    out_lo_values[idx] = best_lo;

    if (best_lo != LowHandVal_NOTHING && best_lo <= LowHandVal_WORST_EIGHT) {
        out_lo_qualifies[idx] = 1;
    } else {
        out_lo_qualifies[idx] = 0;
    }
}

/**
 * Omaha-5 batch evaluation kernel
 *
 * Each work-item evaluates one 10-card Omaha-5 hand (5 hole + 5 board)
 * Must use exactly 2 hole cards and 3 board cards
 *
 * @param hands           Input: 10 cards per hand [batch_size * 10] (5 hole + 5 board)
 * @param batch_size      Number of hands
 * @param out_values      Output: HandVal per hand [batch_size]
 * @param nbits_table     Lookup table: number of bits set [8192]
 * @param straight_table  Lookup table: straight detection [8192]
 * @param top_card_table  Lookup table: highest rank [8192]
 * @param top_five_table  Lookup table: top five cards encoded [8192]
 */
__kernel void eval_omaha5_batch_kernel(
    __global const uchar* restrict hands,
    const ulong batch_size,
    __global uint* restrict out_values,
    __global const uchar* restrict nbits_table,
    __global const uchar* restrict straight_table,
    __global const uchar* restrict top_card_table,
    __global const uint* restrict top_five_table
) {
    int idx = get_global_id(0);
    if (idx >= batch_size) return;

    /* Load 10 cards: 5 hole + 5 board */
    uchar hole[5], board[5];

    for (int i = 0; i < 5; i++) {
        hole[i] = hands[idx * 10 + i];
    }
    for (int i = 0; i < 5; i++) {
        board[i] = hands[idx * 10 + 5 + i];
    }

    HandVal best = 0;

    /* Enumerate C(5,2) * C(5,3) = 10 * 10 = 100 combinations */
    #pragma unroll
    for (int h = 0; h < 10; h++) {
        #pragma unroll
        for (int b = 0; b < 10; b++) {
            uchar hand5[5];
            hand5[0] = hole[OMAHA5_HOLE_COMBOS[h][0]];
            hand5[1] = hole[OMAHA5_HOLE_COMBOS[h][1]];
            hand5[2] = board[OMAHA_BOARD_COMBOS[b][0]];
            hand5[3] = board[OMAHA_BOARD_COMBOS[b][1]];
            hand5[4] = board[OMAHA_BOARD_COMBOS[b][2]];

            HandVal val = eval_5cards(hand5, nbits_table, straight_table,
                                      top_card_table, top_five_table);
            best = max(best, val);
        }
    }

    out_values[idx] = best;
}

/**
 * Omaha-6 batch evaluation kernel
 *
 * Each work-item evaluates one 11-card Omaha-6 hand (6 hole + 5 board)
 * Must use exactly 2 hole cards and 3 board cards
 *
 * @param hands           Input: 11 cards per hand [batch_size * 11] (6 hole + 5 board)
 * @param batch_size      Number of hands
 * @param out_values      Output: HandVal per hand [batch_size]
 * @param nbits_table     Lookup table: number of bits set [8192]
 * @param straight_table  Lookup table: straight detection [8192]
 * @param top_card_table  Lookup table: highest rank [8192]
 * @param top_five_table  Lookup table: top five cards encoded [8192]
 */
__kernel void eval_omaha6_batch_kernel(
    __global const uchar* restrict hands,
    const ulong batch_size,
    __global uint* restrict out_values,
    __global const uchar* restrict nbits_table,
    __global const uchar* restrict straight_table,
    __global const uchar* restrict top_card_table,
    __global const uint* restrict top_five_table
) {
    int idx = get_global_id(0);
    if (idx >= batch_size) return;

    /* Load 11 cards: 6 hole + 5 board */
    uchar hole[6], board[5];

    for (int i = 0; i < 6; i++) {
        hole[i] = hands[idx * 11 + i];
    }
    for (int i = 0; i < 5; i++) {
        board[i] = hands[idx * 11 + 6 + i];
    }

    HandVal best = 0;

    /* Enumerate C(6,2) * C(5,3) = 15 * 10 = 150 combinations */
    #pragma unroll
    for (int h = 0; h < 15; h++) {
        #pragma unroll
        for (int b = 0; b < 10; b++) {
            uchar hand5[5];
            hand5[0] = hole[OMAHA6_HOLE_COMBOS[h][0]];
            hand5[1] = hole[OMAHA6_HOLE_COMBOS[h][1]];
            hand5[2] = board[OMAHA_BOARD_COMBOS[b][0]];
            hand5[3] = board[OMAHA_BOARD_COMBOS[b][1]];
            hand5[4] = board[OMAHA_BOARD_COMBOS[b][2]];

            HandVal val = eval_5cards(hand5, nbits_table, straight_table,
                                      top_card_table, top_five_table);
            best = max(best, val);
        }
    }

    out_values[idx] = best;
}
