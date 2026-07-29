/*
 * poker_eval_api.c - Stable C API implementation
 *
 * Copyright (C) 2025 poker-eval contributors
 *
 * Implementation of the stable C API with opaque handles.
 */

#include "poker_eval_api.h"
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/eval.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/games/rules_std.h>
#include <poker_eval/core/evx_defs.h>
#include <poker_eval/core/enumerate.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ===== Internal Context Structure ===== */

struct pe_context_t {
    pe_config_t config;
    char last_error[256];
};

struct pe_cfr_solver_t {
    pe_handle_t parent;
    pe_game_type_t game;
    /* CFR storage would go here */
    void* storage;
    uint64_t iteration;
};

struct pe_icm_calc_t {
    pe_handle_t parent;
};

/* ===== Version ===== */

void pe_get_version(int* major, int* minor, int* patch) {
    if (major) *major = PE_API_VERSION_MAJOR;
    if (minor) *minor = PE_API_VERSION_MINOR;
    if (patch) *patch = PE_API_VERSION_PATCH;
}

/* ===== Initialization ===== */

void pe_get_default_config(pe_config_t* config) {
    if (!config) return;
    config->use_monte_carlo = 0;
    config->monte_carlo_samples = 10000;
    config->num_threads = 0;
    config->verbose = 0;
}

pe_handle_t pe_init(const pe_config_t* config) {
    struct pe_context_t* ctx = (struct pe_context_t*)calloc(1, sizeof(struct pe_context_t));
    if (!ctx) return NULL;

    if (config) {
        ctx->config = *config;
    } else {
        pe_get_default_config(&ctx->config);
    }

    ctx->last_error[0] = '\0';
    return ctx;
}

void pe_free(pe_handle_t handle) {
    if (handle) {
        free(handle);
    }
}

const char* pe_get_error(pe_handle_t handle) {
    if (!handle) return "Invalid handle";
    return handle->last_error;
}

static void set_error(pe_handle_t handle, const char* msg) {
    if (handle && msg) {
        strncpy(handle->last_error, msg, sizeof(handle->last_error) - 1);
        handle->last_error[sizeof(handle->last_error) - 1] = '\0';
    }
}

/* ===== Card Parsing ===== */

int pe_parse_card(const char* card_str) {
    if (!card_str || strlen(card_str) < 2) return -1;

    int rank = -1;
    int suit = -1;

    /* Parse rank */
    switch (card_str[0]) {
        case '2': rank = StdDeck_Rank_2; break;
        case '3': rank = StdDeck_Rank_3; break;
        case '4': rank = StdDeck_Rank_4; break;
        case '5': rank = StdDeck_Rank_5; break;
        case '6': rank = StdDeck_Rank_6; break;
        case '7': rank = StdDeck_Rank_7; break;
        case '8': rank = StdDeck_Rank_8; break;
        case '9': rank = StdDeck_Rank_9; break;
        case 'T': case 't': rank = StdDeck_Rank_TEN; break;
        case 'J': case 'j': rank = StdDeck_Rank_JACK; break;
        case 'Q': case 'q': rank = StdDeck_Rank_QUEEN; break;
        case 'K': case 'k': rank = StdDeck_Rank_KING; break;
        case 'A': case 'a': rank = StdDeck_Rank_ACE; break;
        default: return -1;
    }

    /* Parse suit */
    switch (card_str[1]) {
        case 'h': case 'H': suit = StdDeck_Suit_HEARTS; break;
        case 'd': case 'D': suit = StdDeck_Suit_DIAMONDS; break;
        case 'c': case 'C': suit = StdDeck_Suit_CLUBS; break;
        case 's': case 'S': suit = StdDeck_Suit_SPADES; break;
        default: return -1;
    }

    return StdDeck_MAKE_CARD(rank, suit);
}

pe_error_t pe_card_to_string(int card, char* buffer, size_t size) {
    if (!buffer || size < 3) return PE_ERROR_INVALID_ARGUMENT;
    if (card < 0 || card >= 52) return PE_ERROR_INVALID_ARGUMENT;

    static const char ranks[] = "23456789TJQKA";
    static const char suits[] = "hdcs";

    int rank = StdDeck_RANK(card);
    int suit = StdDeck_SUIT(card);

    buffer[0] = ranks[rank];
    buffer[1] = suits[suit];
    buffer[2] = '\0';

    return PE_OK;
}

int pe_parse_board(const char* board_str, uint8_t* cards, int max_cards) {
    if (!board_str || !cards || max_cards <= 0) return -1;

    int count = 0;
    const char* p = board_str;

    while (*p && count < max_cards) {
        /* Skip whitespace */
        while (*p == ' ' || *p == ',') p++;
        if (!*p) break;

        /* Parse card */
        int card = pe_parse_card(p);
        if (card < 0) return -1;

        cards[count++] = (uint8_t)card;
        p += 2;
    }

    return count;
}

/* ===== Hand Evaluation ===== */

static const char* hand_type_names[] = {
    "NoPair", "OnePair", "TwoPair", "Trips",
    "Straight", "Flush", "FullHouse", "Quads", "StFlush"
};

pe_error_t pe_evaluate_hand(pe_handle_t handle,
                            const uint8_t* cards,
                            int num_cards,
                            pe_hand_result_t* result) {
    if (!handle || !cards || !result) return PE_ERROR_INVALID_ARGUMENT;
    if (num_cards < 5 || num_cards > 7) {
        set_error(handle, "Number of cards must be between 5 and 7");
        return PE_ERROR_INVALID_ARGUMENT;
    }

    /* Build card mask */
    StdDeck_CardMask hand;
    StdDeck_CardMask_RESET(hand);

    for (int i = 0; i < num_cards; i++) {
        if (cards[i] >= 52) {
            set_error(handle, "Invalid card number");
            return PE_ERROR_INVALID_ARGUMENT;
        }
        StdDeck_CardMask_SET(hand, cards[i]);
    }

    /* Evaluate */
    HandVal hv;
    if (num_cards == 5) {
        hv = StdDeck_StdRules_EVAL_N(hand, 5);
    } else if (num_cards == 6) {
        hv = StdDeck_StdRules_EVAL_N(hand, 6);
    } else {
        hv = StdDeck_StdRules_EVAL_N(hand, 7);
    }

    result->hand_value = hv;
    result->hand_type = HandVal_HANDTYPE(hv);

    if (result->hand_type < 9) {
        strncpy(result->hand_name, hand_type_names[result->hand_type],
                sizeof(result->hand_name) - 1);
    } else {
        strncpy(result->hand_name, "Unknown", sizeof(result->hand_name) - 1);
    }

    /* Extract best 5 cards (simplified - just copy first 5) */
    for (int i = 0; i < 5 && i < num_cards; i++) {
        result->cards[i] = cards[i];
    }

    return PE_OK;
}

pe_error_t pe_evaluate_holdem(pe_handle_t handle,
                              const char* hole_cards,
                              const char* board,
                              pe_hand_result_t* result) {
    if (!handle || !hole_cards || !result) return PE_ERROR_INVALID_ARGUMENT;

    uint8_t cards[7];
    int num_cards = 0;

    /* Parse hole cards */
    int parsed = pe_parse_board(hole_cards, cards, 2);
    if (parsed != 2) {
        set_error(handle, "Invalid hole cards");
        return PE_ERROR_PARSE_FAILED;
    }
    num_cards = 2;

    /* Parse board if provided */
    if (board && *board) {
        parsed = pe_parse_board(board, cards + 2, 5);
        if (parsed < 0) {
            set_error(handle, "Invalid board");
            return PE_ERROR_PARSE_FAILED;
        }
        num_cards += parsed;
    }

    if (num_cards < 5) {
        set_error(handle, "Need at least 5 cards for evaluation");
        return PE_ERROR_INVALID_ARGUMENT;
    }

    return pe_evaluate_hand(handle, cards, num_cards, result);
}

/* ===== Equity Calculation ===== */

pe_error_t pe_calculate_equity_holdem(pe_handle_t handle,
                                      const char* hand1,
                                      const char* hand2,
                                      const char* board,
                                      pe_equity_result_t* result) {
    if (!handle || !hand1 || !hand2 || !result) return PE_ERROR_INVALID_ARGUMENT;

    uint8_t h1[2], h2[2], b[5];
    int board_cards = 0;

    /* Parse hands */
    if (pe_parse_board(hand1, h1, 2) != 2) {
        set_error(handle, "Invalid hand1");
        return PE_ERROR_PARSE_FAILED;
    }
    if (pe_parse_board(hand2, h2, 2) != 2) {
        set_error(handle, "Invalid hand2");
        return PE_ERROR_PARSE_FAILED;
    }

    /* Parse board */
    if (board && *board) {
        board_cards = pe_parse_board(board, b, 5);
        if (board_cards < 0) {
            set_error(handle, "Invalid board");
            return PE_ERROR_PARSE_FAILED;
        }
    }

    /* Build masks */
    StdDeck_CardMask hand1_mask, hand2_mask, board_mask, dead_mask;
    StdDeck_CardMask_RESET(hand1_mask);
    StdDeck_CardMask_RESET(hand2_mask);
    StdDeck_CardMask_RESET(board_mask);
    StdDeck_CardMask_RESET(dead_mask);

    StdDeck_CardMask_SET(hand1_mask, h1[0]);
    StdDeck_CardMask_SET(hand1_mask, h1[1]);
    StdDeck_CardMask_SET(hand2_mask, h2[0]);
    StdDeck_CardMask_SET(hand2_mask, h2[1]);

    for (int i = 0; i < board_cards; i++) {
        StdDeck_CardMask_SET(board_mask, b[i]);
    }

    /* Dead cards = all known cards */
    StdDeck_CardMask_OR(dead_mask, hand1_mask, hand2_mask);
    StdDeck_CardMask_OR(dead_mask, dead_mask, board_mask);

    /* Enumerate remaining boards */
    uint64_t wins = 0, ties = 0, losses = 0, total = 0;

    if (board_cards == 5) {
        /* Complete board - single evaluation */
        StdDeck_CardMask full1, full2;
        StdDeck_CardMask_OR(full1, hand1_mask, board_mask);
        StdDeck_CardMask_OR(full2, hand2_mask, board_mask);

        HandVal hv1 = StdDeck_StdRules_EVAL_N(full1, 7);
        HandVal hv2 = StdDeck_StdRules_EVAL_N(full2, 7);

        if (hv1 > hv2) wins = 1;
        else if (hv1 < hv2) losses = 1;
        else ties = 1;
        total = 1;
    } else {
        /* Enumerate remaining cards */
        int cards_to_deal = 5 - board_cards;
        StdDeck_CardMask remaining;

        DECK_ENUMERATE_N_CARDS_D(StdDeck, remaining, cards_to_deal, dead_mask,
        {
            StdDeck_CardMask full_board;
            StdDeck_CardMask full1;
            StdDeck_CardMask full2;
            StdDeck_CardMask_OR(full_board, board_mask, remaining);
            StdDeck_CardMask_OR(full1, hand1_mask, full_board);
            StdDeck_CardMask_OR(full2, hand2_mask, full_board);

            HandVal hv1 = StdDeck_StdRules_EVAL_N(full1, 7);
            HandVal hv2 = StdDeck_StdRules_EVAL_N(full2, 7);

            if (hv1 > hv2) wins++;
            else if (hv1 < hv2) losses++;
            else ties++;
            total++;
        });
    }

    result->samples = total;
    if (total > 0) {
        result->win_pct = (double)wins / total;
        result->tie_pct = (double)ties / total;
        result->lose_pct = (double)losses / total;
        result->equity = result->win_pct + (result->tie_pct / 2.0);
    } else {
        result->win_pct = 0;
        result->tie_pct = 0;
        result->lose_pct = 0;
        result->equity = 0;
    }

    return PE_OK;
}

pe_error_t pe_calculate_equity_omaha(pe_handle_t handle,
                                     const char* hand1,
                                     const char* hand2,
                                     const char* board,
                                     pe_equity_result_t* result) {
    /* Omaha equity - simplified implementation */
    if (!handle || !hand1 || !hand2 || !result) return PE_ERROR_INVALID_ARGUMENT;

    set_error(handle, "Omaha equity not fully implemented in C API - use Python bindings");
    return PE_ERROR_NOT_SUPPORTED;
}

pe_error_t pe_calculate_equity_multiway(pe_handle_t handle,
                                        pe_game_type_t game,
                                        const char** hands,
                                        int num_players,
                                        const char* board,
                                        pe_multiway_result_t* result) {
    if (!handle || !hands || !result || num_players < 2) {
        return PE_ERROR_INVALID_ARGUMENT;
    }

    /* Allocate result array */
    result->num_players = num_players;
    result->player_results = (pe_equity_result_t*)calloc(num_players, sizeof(pe_equity_result_t));
    if (!result->player_results) {
        return PE_ERROR_OUT_OF_MEMORY;
    }

    /* For now, only support 2-player Hold'em */
    if (game == PE_GAME_HOLDEM && num_players == 2) {
        pe_error_t err = pe_calculate_equity_holdem(handle, hands[0], hands[1], board,
                                                    &result->player_results[0]);
        if (err != PE_OK) {
            free(result->player_results);
            result->player_results = NULL;
            return err;
        }

        /* Player 2's equity is inverse */
        result->player_results[1].equity = 1.0 - result->player_results[0].equity;
        result->player_results[1].win_pct = result->player_results[0].lose_pct;
        result->player_results[1].lose_pct = result->player_results[0].win_pct;
        result->player_results[1].tie_pct = result->player_results[0].tie_pct;
        result->player_results[1].samples = result->player_results[0].samples;

        result->total_samples = result->player_results[0].samples;
        return PE_OK;
    }

    set_error(handle, "Multiway equity only supports 2-player Hold'em in C API");
    free(result->player_results);
    result->player_results = NULL;
    return PE_ERROR_NOT_SUPPORTED;
}

void pe_free_multiway_result(pe_multiway_result_t* result) {
    if (result && result->player_results) {
        free(result->player_results);
        result->player_results = NULL;
        result->num_players = 0;
    }
}

pe_error_t pe_calculate_range_equity(pe_handle_t handle,
                                     pe_game_type_t game,
                                     const char* range1,
                                     const char* range2,
                                     const char* board,
                                     pe_equity_result_t* result) {
    if (!handle) return PE_ERROR_INVALID_HANDLE;

    /* Range parsing requires the full library - use Python bindings */
    set_error(handle, "Range equity requires Python bindings for range expansion");
    return PE_ERROR_NOT_SUPPORTED;
}

/* ===== CFR Solver ===== */

pe_cfr_handle_t pe_cfr_create(pe_handle_t handle,
                              pe_game_type_t game,
                              const char* game_tree) {
    (void)game_tree;
    if (!handle) return NULL;

    struct pe_cfr_solver_t* cfr = (struct pe_cfr_solver_t*)calloc(1, sizeof(struct pe_cfr_solver_t));
    if (!cfr) return NULL;

    cfr->parent = handle;
    cfr->game = game;
    cfr->iteration = 0;

    return cfr;
}

void pe_cfr_free(pe_cfr_handle_t cfr) {
    if (cfr) {
        free(cfr);
    }
}

pe_error_t pe_cfr_solve(pe_cfr_handle_t cfr, int iterations) {
    if (!cfr || iterations <= 0) return PE_ERROR_INVALID_ARGUMENT;

    /* CFR solving requires full library integration */
    set_error(cfr->parent, "CFR solving requires linking with poker_engine library");
    return PE_ERROR_NOT_SUPPORTED;
}

int pe_cfr_get_strategy(pe_cfr_handle_t cfr,
                        uint64_t infoset_key,
                        double* strategy,
                        int max_actions) {
    (void)infoset_key;
    (void)strategy;
    (void)max_actions;
    if (!cfr) return -1;
    return -1;
}

pe_error_t pe_cfr_save(pe_cfr_handle_t cfr, const char* filepath) {
    (void)filepath;
    if (!cfr) return PE_ERROR_INVALID_HANDLE;
    return PE_ERROR_NOT_SUPPORTED;
}

pe_error_t pe_cfr_load(pe_cfr_handle_t cfr, const char* filepath) {
    (void)filepath;
    if (!cfr) return PE_ERROR_INVALID_HANDLE;
    return PE_ERROR_NOT_SUPPORTED;
}

pe_error_t pe_cfr_get_exploitability(pe_cfr_handle_t cfr, double* exploitability) {
    if (!cfr || !exploitability) return PE_ERROR_INVALID_ARGUMENT;
    *exploitability = 0.0;
    return PE_ERROR_NOT_SUPPORTED;
}

/* ===== ICM Calculator ===== */

pe_icm_handle_t pe_icm_create(pe_handle_t handle) {
    if (!handle) return NULL;

    struct pe_icm_calc_t* icm = (struct pe_icm_calc_t*)calloc(1, sizeof(struct pe_icm_calc_t));
    if (!icm) return NULL;

    icm->parent = handle;
    return icm;
}

void pe_icm_free(pe_icm_handle_t icm) {
    if (icm) {
        free(icm);
    }
}

/* Recursive ICM calculation using Malmuth-Harville formula */
static double icm_recursive(const double* stacks, int num_players, int player,
                           double total_chips, const double* payouts, int payout_idx) {
    if (payout_idx >= num_players || payouts[payout_idx] <= 0.0) {
        return 0.0;
    }

    double equity = 0.0;
    double prob = stacks[player] / total_chips;

    if (prob > 0) {
        equity += prob * payouts[payout_idx];

        /* Recursive calculation for remaining places */
        if (payout_idx + 1 < num_players && payouts[payout_idx + 1] > 0) {
            double new_total = total_chips - stacks[player];
            if (new_total > 0) {
                double* new_stacks = (double*)malloc(num_players * sizeof(double));
                if (new_stacks) {
                    for (int i = 0; i < num_players; i++) {
                        new_stacks[i] = (i == player) ? 0 : stacks[i];
                    }

                    for (int other = 0; other < num_players; other++) {
                        if (other != player && stacks[other] > 0) {
                            equity += prob * icm_recursive(new_stacks, num_players, other,
                                                          new_total, payouts, payout_idx + 1);
                        }
                    }
                    free(new_stacks);
                }
            }
        }
    }

    return equity;
}

pe_error_t pe_icm_calculate(pe_icm_handle_t icm,
                            const double* stacks,
                            const double* payouts,
                            int num_players,
                            pe_icm_player_result_t* results) {
    if (!icm || !stacks || !payouts || !results || num_players < 2) {
        return PE_ERROR_INVALID_ARGUMENT;
    }

    /* Calculate total chips */
    double total_chips = 0;
    for (int i = 0; i < num_players; i++) {
        total_chips += stacks[i];
    }

    if (total_chips <= 0) {
        return PE_ERROR_INVALID_ARGUMENT;
    }

    /* Calculate ICM equity for each player */
    for (int i = 0; i < num_players; i++) {
        results[i].chip_ev = stacks[i] / total_chips;

        /* Use recursive ICM formula */
        results[i].icm_equity = 0;
        for (int place = 0; place < num_players; place++) {
            if (payouts[place] > 0) {
                results[i].icm_equity += icm_recursive(stacks, num_players, i,
                                                       total_chips, payouts, 0);
                break;  /* Start from first place only once */
            }
        }

        /* Simplified: direct probability-weighted payout */
        double simple_icm = 0;
        double prob_first = stacks[i] / total_chips;
        simple_icm += prob_first * payouts[0];

        /* Approximate remaining places */
        if (num_players > 1 && payouts[1] > 0) {
            double remaining = 1.0 - prob_first;
            simple_icm += remaining * payouts[1] * (stacks[i] / (total_chips - stacks[i] + 0.001));
        }

        results[i].icm_equity = simple_icm;
    }

    return PE_OK;
}
