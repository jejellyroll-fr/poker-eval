/*
 * gpu_table_loader.h - Load poker-eval lookup tables for GPU
 *
 * Copyright (C) 2025 poker-eval contributors
 *
 * Interface for loading real poker-eval lookup tables for GPU use.
 * These tables are used by GPU kernels to mirror the CPU
 * StdDeck_StdRules_EVAL_N 5-card evaluation logic.
 *
 * Tables loaded (high evaluation):
 * - nBitsTable[8192]:        Number of bits set in 13-bit rank mask
 * - straightTable[8192]:     Straight detection (top card rank or 0)
 * - topCardTable[8192]:      Highest rank in 13-bit mask
 * - topFiveCardsTable[8192]: Top five cards encoded as HandVal components
 *
 * Tables loaded (low evaluation):
 * - bottomFiveCardsTable[8192]: Bottom five cards encoded as LowHandVal
 * - bottomCardTable[8192]:      Lowest rank in 13-bit mask
 */

#ifndef POKER_EVAL_GPU_TABLE_LOADER_H
#define POKER_EVAL_GPU_TABLE_LOADER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * GPU lookup tables structure
 *
 * Contains the core 13-bit lookup tables needed to mirror
 * StdDeck_StdRules_EVAL_N for 5-card evaluation on GPU.
 *
 * These tables use 13-bit rank masks where bit i represents rank i:
 *   bit 0 = 2, bit 1 = 3, ..., bit 12 = Ace
 *
 * The GPU kernels use these tables identically to the CPU evaluator
 * to ensure parity between CPU and GPU hand evaluations.
 */
typedef struct {
    uint8_t nbits_table[8192];      /* Number of bits set in rank mask */
    uint8_t straight_table[8192];   /* Straight detection: 0 or top rank */
    uint8_t top_card_table[8192];   /* Highest rank in mask */
    uint32_t top_five_table[8192];  /* Top five ranks as HandVal encoding */
} gpu_lookup_tables_t;

/**
 * Load nBits table
 *
 * Maps 13-bit rank masks to the number of bits set.
 * Used to count distinct ranks in a hand.
 *
 * @param out_table  Output buffer [8192]
 * @return 0 on success, -1 on error
 */
int gpu_load_nbits_table(uint8_t* out_table);

/**
 * Load straight evaluation table
 *
 * Maps 13-bit rank masks to straight rank (or 0 if not a straight).
 * Returns the top card rank of the straight (0-12) or 0 if no straight.
 * Special case: A-2-3-4-5 (wheel) returns rank of 5 (index 3).
 *
 * @param out_table  Output buffer [8192]
 * @return 0 on success, -1 on error
 */
int gpu_load_straight_table(uint8_t* out_table);

/**
 * Load top-card table
 *
 * Maps 13-bit rank masks to highest rank present.
 * Returns the index (0-12) of the highest set bit.
 *
 * @param out_table  Output buffer [8192]
 * @return 0 on success, -1 on error
 */
int gpu_load_top_card_table(uint8_t* out_table);

/**
 * Load top-five-cards table
 *
 * Maps 13-bit rank masks to the top five cards encoded in HandVal format:
 *   bits 16-19: top card
 *   bits 12-15: second card
 *   bits 8-11:  third card
 *   bits 4-7:   fourth card
 *   bits 0-3:   fifth card
 *
 * @param out_table  Output buffer [8192]
 * @return 0 on success, -1 on error
 */
int gpu_load_top_five_table(uint32_t* out_table);

/**
 * Load all GPU tables at once
 *
 * Convenience function to load all four lookup tables needed
 * for 5-card hand evaluation on GPU.
 *
 * @param tables  Pointer to gpu_lookup_tables_t structure
 * @return 0 on success, -1 on error
 */
int gpu_load_all_tables(gpu_lookup_tables_t* tables);

/**
 * Validate table integrity
 *
 * Performs sanity checks on loaded tables to ensure they
 * contain valid data (e.g., non-zero entries where expected).
 *
 * @param tables  Tables to validate
 * @return 0 if valid, -1 if invalid
 */
int gpu_validate_tables(const gpu_lookup_tables_t* tables);

/**
 * Print table statistics (for debugging)
 *
 * Outputs statistics about loaded tables including
 * non-zero entry counts and sample values.
 *
 * @param tables  Tables to analyze
 */
void gpu_print_table_stats(const gpu_lookup_tables_t* tables);

/**
 * GPU low evaluation lookup tables structure
 *
 * Contains the 13-bit lookup tables needed for low hand evaluation
 * (A-5 lowball used in Razz, Omaha8, Stud8).
 *
 * These tables use the same 13-bit rank mask format as the high tables,
 * but encode the LOWEST cards rather than highest.
 */
typedef struct {
    uint32_t bottom_five_table[8192]; /* Bottom five ranks as LowHandVal encoding */
    uint8_t  bottom_card_table[8192]; /* Lowest rank in mask */
} gpu_low_lookup_tables_t;

/**
 * Load bottom-five-cards table
 *
 * Maps 13-bit rank masks to the bottom five cards encoded in LowHandVal format.
 * Used for A-5 lowball evaluation (Razz, Omaha8, Stud8).
 *
 * @param out_table  Output buffer [8192]
 * @return 0 on success, -1 on error
 */
int gpu_load_bottom_five_table(uint32_t* out_table);

/**
 * Load bottom-card table
 *
 * Maps 13-bit rank masks to lowest rank present.
 * Returns the index (0-12) of the lowest set bit.
 *
 * @param out_table  Output buffer [8192]
 * @return 0 on success, -1 on error
 */
int gpu_load_bottom_card_table(uint8_t* out_table);

/**
 * Load all low evaluation GPU tables at once
 *
 * @param tables  Pointer to gpu_low_lookup_tables_t structure
 * @return 0 on success, -1 on error
 */
int gpu_load_low_tables(gpu_low_lookup_tables_t* tables);

/**
 * Validate low table integrity
 *
 * @param tables  Tables to validate
 * @return 0 if valid, -1 if invalid
 */
int gpu_validate_low_tables(const gpu_low_lookup_tables_t* tables);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_GPU_TABLE_LOADER_H */
