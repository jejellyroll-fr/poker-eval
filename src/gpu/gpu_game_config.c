/*
 * gpu_game_config.c: Helper functions for GPU game configurations
 *
 * This file provides convenience functions to create game configurations
 * for different poker variants to be evaluated on GPU.
 */

#include <poker_eval/gpu/eval_gpu.h>

/* Create Hold'em configuration */
gpu_game_config_t gpu_game_config_holdem(void) {
    gpu_game_config_t config;
    config.game = GPU_GAME_HOLDEM;
    config.num_hole_cards = 2;
    config.num_board_cards = 5;
    config.eval_low = 0;
    config.split_pot = 0;
    config.low_qualifier = 0;
    return config;
}

/* Create Hold'em Hi/Lo configuration */
gpu_game_config_t gpu_game_config_holdem8(void) {
    gpu_game_config_t config;
    config.game = GPU_GAME_HOLDEM8;
    config.num_hole_cards = 2;
    config.num_board_cards = 5;
    config.eval_low = 1;
    config.split_pot = 1;
    config.low_qualifier = 8;  /* 8-or-better */
    return config;
}

/* Create Omaha configuration (4, 5, or 6 hole cards) */
gpu_game_config_t gpu_game_config_omaha(int num_hole_cards) {
    gpu_game_config_t config;

    if (num_hole_cards == 5) {
        config.game = GPU_GAME_OMAHA5;
    } else if (num_hole_cards == 6) {
        config.game = GPU_GAME_OMAHA6;
    } else {
        config.game = GPU_GAME_OMAHA;  /* Default to 4-card */
        num_hole_cards = 4;
    }

    config.num_hole_cards = num_hole_cards;
    config.num_board_cards = 5;
    config.eval_low = 0;
    config.split_pot = 0;
    config.low_qualifier = 0;
    return config;
}

/* Create Omaha Hi/Lo configuration (4, 5, or 6 hole cards) */
gpu_game_config_t gpu_game_config_omaha8(int num_hole_cards) {
    gpu_game_config_t config;
    config.game = GPU_GAME_OMAHA8;

    if (num_hole_cards < 4 || num_hole_cards > 6) {
        num_hole_cards = 4;  /* Default to 4-card */
    }

    config.num_hole_cards = num_hole_cards;
    config.num_board_cards = 5;
    config.eval_low = 1;
    config.split_pot = 1;
    config.low_qualifier = 8;  /* 8-or-better */
    return config;
}

/* Create 7-Card Stud configuration */
gpu_game_config_t gpu_game_config_stud(void) {
    gpu_game_config_t config;
    config.game = GPU_GAME_STUD;
    config.num_hole_cards = 7;  /* All cards are "hole" cards in Stud */
    config.num_board_cards = 0;  /* No community cards */
    config.eval_low = 0;
    config.split_pot = 0;
    config.low_qualifier = 0;
    return config;
}

/* Create 7-Card Stud Hi/Lo configuration */
gpu_game_config_t gpu_game_config_stud8(void) {
    gpu_game_config_t config;
    config.game = GPU_GAME_STUD8;
    config.num_hole_cards = 7;
    config.num_board_cards = 0;
    config.eval_low = 1;
    config.split_pot = 1;
    config.low_qualifier = 8;  /* 8-or-better */
    return config;
}

/* Create Razz configuration (A-5 lowball) */
gpu_game_config_t gpu_game_config_razz(void) {
    gpu_game_config_t config;
    config.game = GPU_GAME_RAZZ;
    config.num_hole_cards = 7;
    config.num_board_cards = 0;
    config.eval_low = 1;         /* Evaluate low only */
    config.split_pot = 0;        /* Not a split game, just low */
    config.low_qualifier = 0;    /* No qualifier in Razz */
    return config;
}

/* Create 2-7 Lowball configuration */
gpu_game_config_t gpu_game_config_lowball27(void) {
    gpu_game_config_t config;
    config.game = GPU_GAME_LOWBALL27;
    config.num_hole_cards = 5;   /* 2-7 lowball is a 5-card draw game */
    config.num_board_cards = 0;  /* No community cards in draw games */
    config.eval_low = 1;         /* Evaluate as low-only game */
    config.split_pot = 0;        /* Not a split game */
    config.low_qualifier = 0;    /* No qualifier in 2-7 */
    return config;
}
