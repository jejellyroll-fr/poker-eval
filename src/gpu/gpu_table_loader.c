/*
 * gpu_table_loader.c - Load poker-eval lookup tables for GPU
 *
 * Copyright (C) 2025 poker-eval contributors
 *
 * Loads the real poker-eval 13-bit rank lookup tables and prepares
 * them for GPU constant/texture memory.
 */

#include "gpu_table_loader.h"
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_std.h>
#include <string.h>
#include <stdio.h>

/**
 * Load nBits table
 *
 * Maps 13-bit rank masks to the number of bits set.
 *
 * @param out_table  Output buffer [8192]
 * @return 0 on success
 */
int gpu_load_nbits_table(uint8_t* out_table) {
    if (!out_table) return -1;
    memcpy(out_table, nBitsTable, sizeof(nBitsTable));
    return 0;
}

/**
 * Load straight evaluation table
 *
 * Maps 13-bit rank masks to straight rank (or 0 if not a straight).
 *
 * @param out_table  Output buffer [8192]
 * @return 0 on success
 */
int gpu_load_straight_table(uint8_t* out_table) {
    if (!out_table) return -1;
    memcpy(out_table, straightTable, sizeof(straightTable));
    return 0;
}

/**
 * Load top-card table
 *
 * Maps 13-bit rank masks to highest rank.
 *
 * @param out_table  Output buffer [8192]
 * @return 0 on success
 */
int gpu_load_top_card_table(uint8_t* out_table) {
    if (!out_table) return -1;
    memcpy(out_table, topCardTable, sizeof(topCardTable));
    return 0;
}

/**
 * Load top-five-cards table
 *
 * Maps 13-bit rank masks to the top five cards encoded as HandVal cards.
 *
 * @param out_table  Output buffer [8192]
 * @return 0 on success
 */
int gpu_load_top_five_table(uint32_t* out_table) {
    if (!out_table) return -1;
    memcpy(out_table, topFiveCardsTable, sizeof(topFiveCardsTable));
    return 0;
}

/**
 * Load all GPU tables at once
 *
 * @param tables  Pointer to gpu_lookup_tables_t structure
 * @return 0 on success
 */
int gpu_load_all_tables(gpu_lookup_tables_t* tables) {
    if (!tables) return -1;

    int result = 0;

    result |= gpu_load_nbits_table(tables->nbits_table);
    result |= gpu_load_straight_table(tables->straight_table);
    result |= gpu_load_top_card_table(tables->top_card_table);
    result |= gpu_load_top_five_table(tables->top_five_table);

    if (result != 0) {
        fprintf(stderr, "Failed to load GPU lookup tables\n");
        return -1;
    }

    return 0;
}

/**
 * Validate table integrity
 *
 * Performs sanity checks on loaded tables.
 *
 * @param tables  Tables to validate
 * @return 0 if valid, -1 if invalid
 */
int gpu_validate_tables(const gpu_lookup_tables_t* tables) {
    if (!tables) return -1;

    int nonzero_straights = 0;
    int nonzero_topfive = 0;

    for (int i = 0; i < 8192; i++) {
        if (tables->straight_table[i] != 0) nonzero_straights++;
        if (tables->top_five_table[i] != 0) nonzero_topfive++;
    }

    if (nonzero_straights == 0 || nonzero_topfive == 0) {
        fprintf(stderr, "GPU tables appear invalid (straight/top-five all zero)\n");
        return -1;
    }

    return 0;
}

/**
 * Print table statistics (for debugging)
 *
 * @param tables  Tables to analyze
 */
void gpu_print_table_stats(const gpu_lookup_tables_t* tables) {
    if (!tables) return;

    int straight_nonzero = 0, topfive_nonzero = 0;

    for (int i = 0; i < 8192; i++) {
        if (tables->straight_table[i] != 0) straight_nonzero++;
        if (tables->top_five_table[i] != 0) topfive_nonzero++;
    }

    printf("GPU Table Statistics:\n");
    printf("  Straight table: %d / 8192 entries non-zero\n", straight_nonzero);
    printf("  Top-five table: %d / 8192 entries non-zero\n", topfive_nonzero);

    /* Sample some values */
    printf("  Sample straight[0x1F]: 0x%02x\n", tables->straight_table[0x1F]);
    printf("  Sample top-five[0x1F00]: 0x%08x\n", tables->top_five_table[0x1F00]);
}

/* ===== Low Evaluation Tables ===== */

/**
 * Load bottom-five-cards table
 *
 * Maps 13-bit rank masks to the bottom five cards encoded as LowHandVal.
 * Used for A-5 lowball evaluation (Razz, Omaha8, Stud8).
 *
 * @param out_table  Output buffer [8192]
 * @return 0 on success
 */
int gpu_load_bottom_five_table(uint32_t* out_table) {
    if (!out_table) return -1;
    memcpy(out_table, bottomFiveCardsTable, sizeof(bottomFiveCardsTable));
    return 0;
}

/**
 * Load bottom-card table
 *
 * Maps 13-bit rank masks to lowest rank present.
 *
 * @param out_table  Output buffer [8192]
 * @return 0 on success
 */
int gpu_load_bottom_card_table(uint8_t* out_table) {
    if (!out_table) return -1;
    memcpy(out_table, bottomCardTable, sizeof(bottomCardTable));
    return 0;
}

/**
 * Load all low evaluation GPU tables at once
 *
 * @param tables  Pointer to gpu_low_lookup_tables_t structure
 * @return 0 on success
 */
int gpu_load_low_tables(gpu_low_lookup_tables_t* tables) {
    if (!tables) return -1;

    int result = 0;

    result |= gpu_load_bottom_five_table(tables->bottom_five_table);
    result |= gpu_load_bottom_card_table(tables->bottom_card_table);

    if (result != 0) {
        fprintf(stderr, "Failed to load GPU low lookup tables\n");
        return -1;
    }

    return 0;
}

/**
 * Validate low table integrity
 *
 * @param tables  Tables to validate
 * @return 0 if valid, -1 if invalid
 */
int gpu_validate_low_tables(const gpu_low_lookup_tables_t* tables) {
    if (!tables) return -1;

    int nonzero_bottom_five = 0;

    for (int i = 0; i < 8192; i++) {
        if (tables->bottom_five_table[i] != 0) nonzero_bottom_five++;
    }

    if (nonzero_bottom_five == 0) {
        fprintf(stderr, "GPU low tables appear invalid (bottom-five all zero)\n");
        return -1;
    }

    return 0;
}
