/*
 * run_it_twice.c - Multiple runout support for variance reduction
 *
 * Implementation of Run It Twice (RIT) for Hold'em and Omaha variants.
 */

#include <poker_eval/equity/run_it_twice.h>
#include <poker_eval/games/eval_omaha.h>
#include <poker_eval/games/rules_std.h>
#include <poker_eval/deck/deck_std.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

/* Platform-specific timing */
#ifdef _WIN32
#include <windows.h>
static double get_time_ms(void) {
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart * 1000.0 / (double)freq.QuadPart;
}
#else
#include <sys/time.h>
static double get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}
#endif

/* Error messages */
static const char* error_messages[] = {
    "Success",
    "Invalid configuration",
    "Too many players",
    "Invalid stage",
    "Deck exhausted",
    "Evaluation failed",
    "I/O error"
};

const char* rit_error_string(int error_code) {
    int idx = -error_code;
    if (idx < 0 || idx >= (int)(sizeof(error_messages) / sizeof(error_messages[0]))) {
        return "Unknown error";
    }
    return error_messages[idx];
}

/* Default configuration */
rit_config_t rit_config_default(void) {
    rit_config_t config;
    memset(&config, 0, sizeof(config));
    config.game_type = RIT_GAME_HOLDEM;
    config.stage = RIT_STAGE_RIVER;
    config.num_runouts = 2;
    config.seed = 0;  /* Will use system time */
    config.enable_simd = true;
    config.num_threads = 0;  /* Auto-detect */
    config.calc_variance = true;
    config.track_individual = true;
    return config;
}

rit_config_t rit_config_holdem_rit2(void) {
    rit_config_t config = rit_config_default();
    config.game_type = RIT_GAME_HOLDEM;
    config.num_runouts = 2;
    return config;
}

rit_config_t rit_config_omaha_rit2(void) {
    rit_config_t config = rit_config_default();
    config.game_type = RIT_GAME_OMAHA;
    config.num_runouts = 2;
    return config;
}

/* Configuration validation */
bool rit_config_validate(const rit_config_t *config) {
    if (!config) return false;
    if (config->num_runouts < 2 || config->num_runouts > RIT_MAX_RUNOUTS) return false;
    if (config->game_type < 0 || config->game_type > RIT_GAME_OMAHA8) return false;
    if (config->stage < 0 || config->stage > RIT_STAGE_TURN_RIVER) return false;
    return true;
}

/* Determine how many cards to deal based on stage and current board */
static int cards_to_deal_for_stage(rit_stage_t stage, int cards_on_board) {
    switch (stage) {
        case RIT_STAGE_FLOP:
            return (cards_on_board == 0) ? 3 : 0;
        case RIT_STAGE_TURN:
            return (cards_on_board == 3) ? 1 : 0;
        case RIT_STAGE_RIVER:
            return (cards_on_board == 4) ? 1 : 0;
        case RIT_STAGE_TURN_RIVER:
            return (cards_on_board == 3) ? 2 : 0;
        default:
            return 0;
    }
}

/* Fisher-Yates shuffle for deck */
static void shuffle_deck(int *deck, int deck_size, uint64_t *seed) {
    /* Simple LCG for portability */
    for (int i = deck_size - 1; i > 0; i--) {
        *seed = (*seed * 1103515245ULL + 12345ULL) & 0x7fffffffULL;
        int j = (*seed) % (i + 1);
        int temp = deck[i];
        deck[i] = deck[j];
        deck[j] = temp;
    }
}

/* Generate independent board runouts */
int rit_generate_runouts(
    StdDeck_CardMask base_board,
    StdDeck_CardMask dead_cards,
    int cards_to_deal,
    int num_runouts,
    uint64_t seed,
    StdDeck_CardMask boards[])
{
    if (!boards) return RIT_ERROR_INVALID_CONFIG;
    if (cards_to_deal < 0 || cards_to_deal > 5) return RIT_ERROR_INVALID_STAGE;
    if (num_runouts < 1 || num_runouts > RIT_MAX_RUNOUTS) return RIT_ERROR_INVALID_CONFIG;

    /* Initialize seed if not provided */
    if (seed == 0) {
        seed = (uint64_t)time(NULL) ^ (uint64_t)clock();
    }

    /* Build available deck (52 cards minus dead cards and base board) */
    int available_deck[StdDeck_N_CARDS];
    int deck_size = 0;

    for (int i = 0; i < StdDeck_N_CARDS; i++) {
        if (!StdDeck_CardMask_CARD_IS_SET(dead_cards, i) &&
            !StdDeck_CardMask_CARD_IS_SET(base_board, i)) {
            available_deck[deck_size++] = i;
        }
    }

    /* Check if we have enough cards */
    if (deck_size < cards_to_deal) {
        return RIT_ERROR_DECK_EXHAUSTED;
    }

    /* Generate each runout independently */
    for (int run = 0; run < num_runouts; run++) {
        /* Shuffle deck with unique seed for each runout */
        uint64_t runout_seed = seed + run * 123456789ULL;
        shuffle_deck(available_deck, deck_size, &runout_seed);

        /* Start with base board */
        boards[run] = base_board;

        /* Add cards_to_deal new cards */
        for (int c = 0; c < cards_to_deal; c++) {
            StdDeck_CardMask_SET(boards[run], available_deck[c]);
        }
    }

    return RIT_SUCCESS;
}

/* Evaluate single runout for all players */
int rit_evaluate_runout(
    const StdDeck_CardMask holes[],
    int num_players,
    StdDeck_CardMask board,
    rit_game_type_t game_type,
    rit_runout_result_t *result)
{
    if (!holes || !result) return RIT_ERROR_INVALID_CONFIG;
    if (num_players < 2 || num_players > RIT_MAX_PLAYERS) return RIT_ERROR_TOO_MANY_PLAYERS;

    memset(result, 0, sizeof(*result));
    result->board = board;
    for (int p = 0; p < RIT_MAX_PLAYERS; ++p) {
        result->player_low_hands[p] = LowHandVal_NOTHING;
    }

    /* Evaluate each player's hand */
    for (int p = 0; p < num_players; p++) {
        int eval_result;

        switch (game_type) {
            case RIT_GAME_HOLDEM: {
                StdDeck_CardMask combined;
                StdDeck_CardMask_OR(combined, holes[p], board);
                HandVal hand_val = StdDeck_StdRules_EVAL_N(combined, 7);
                if (hand_val == HandVal_NOTHING) return RIT_ERROR_EVAL_FAILED;
                result->player_hands[p] = hand_val;
                break;
            }

            case RIT_GAME_OMAHA:
            case RIT_GAME_OMAHA8: {
                HandVal hi_val = HandVal_NOTHING;
                LowHandVal lo_val = LowHandVal_NOTHING;

                if (game_type == RIT_GAME_OMAHA8) {
                    eval_result = StdDeck_OmahaHiLow8_EVAL(holes[p], board, &hi_val, &lo_val);
                } else {
                    eval_result = StdDeck_OmahaHi_EVAL(holes[p], board, &hi_val);
                }

                if (eval_result != 0) return RIT_ERROR_EVAL_FAILED;
                result->player_hands[p] = hi_val;
                if (game_type == RIT_GAME_OMAHA8) {
                    result->player_low_hands[p] = lo_val;
                }
                break;
            }

            default:
                return RIT_ERROR_INVALID_CONFIG;
        }
    }

    /* Determine winner(s) */
    HandVal best_hand = result->player_hands[0];
    result->winner_mask = (1 << 0);
    result->num_winners = 1;

    for (int p = 1; p < num_players; p++) {
        if (result->player_hands[p] > best_hand) {
            best_hand = result->player_hands[p];
            result->winner_mask = (1 << p);
            result->num_winners = 1;
        } else if (result->player_hands[p] == best_hand) {
            result->winner_mask |= (1 << p);
            result->num_winners++;
        }
    }

    double high_share = 1.0;
    double low_share = 0.0;

    result->low_available = false;
    result->low_winner_mask = 0;
    result->low_num_winners = 0;

    if (game_type == RIT_GAME_OMAHA8) {
        LowHandVal best_low = LowHandVal_NOTHING;
        for (int p = 0; p < num_players; ++p) {
            LowHandVal lo_val = result->player_low_hands[p];
            if (lo_val == LowHandVal_NOTHING)
                continue;

            if (!result->low_available || lo_val < best_low) {
                best_low = lo_val;
                result->low_available = true;
                result->low_winner_mask = (1 << p);
                result->low_num_winners = 1;
            } else if (lo_val == best_low) {
                result->low_winner_mask |= (1 << p);
                result->low_num_winners++;
            }
        }

        if (result->low_available && result->low_num_winners > 0) {
            high_share = 0.5;
            low_share = 0.5;
        }
    }

    double high_equity = high_share / result->num_winners;
    for (int p = 0; p < num_players; p++) {
        if (result->winner_mask & (1 << p)) {
            result->equities[p] += high_equity;
        }
    }

    if (result->low_available && result->low_num_winners > 0) {
        double low_equity = low_share / result->low_num_winners;
        for (int p = 0; p < num_players; ++p) {
            if (result->low_winner_mask & (1 << p)) {
                result->low_equities[p] = low_equity;
                result->equities[p] += low_equity;
            }
        }
    }

    return RIT_SUCCESS;
}

/* Calculate theoretical variance reduction */
double rit_theoretical_variance_reduction(int num_runouts) {
    if (num_runouts <= 0) return 1.0;
    return (double)num_runouts;
}

/* Core RIT calculation */
int rit_calculate_equity(
    const StdDeck_CardMask holes[],
    int num_players,
    StdDeck_CardMask board,
    StdDeck_CardMask dead_cards,
    const rit_config_t *config,
    rit_result_t *result)
{
    if (!holes || !config || !result) return RIT_ERROR_INVALID_CONFIG;
    if (!rit_config_validate(config)) return RIT_ERROR_INVALID_CONFIG;
    if (num_players < 2 || num_players > RIT_MAX_PLAYERS) return RIT_ERROR_TOO_MANY_PLAYERS;

    double start_time = get_time_ms();

    /* Initialize result */
    memset(result, 0, sizeof(*result));
    result->num_players = num_players;
    result->num_runouts = config->num_runouts;
    result->game_type = config->game_type;

    /* Count cards on board */
    int cards_on_board = 0;
    for (int i = 0; i < StdDeck_N_CARDS; i++) {
        if (StdDeck_CardMask_CARD_IS_SET(board, i)) {
            cards_on_board++;
        }
    }

    /* Determine how many cards to deal */
    int cards_to_deal = cards_to_deal_for_stage(config->stage, cards_on_board);
    if (cards_to_deal == 0) {
        return RIT_ERROR_INVALID_STAGE;
    }

    /* Combine all hole cards with dead cards */
    StdDeck_CardMask all_dead = dead_cards;
    for (int p = 0; p < num_players; p++) {
        StdDeck_CardMask_OR(all_dead, all_dead, holes[p]);
    }

    /* Generate runout boards */
    StdDeck_CardMask runout_boards[RIT_MAX_RUNOUTS];
    int gen_result = rit_generate_runouts(
        board, all_dead, cards_to_deal, config->num_runouts,
        config->seed, runout_boards);

    if (gen_result != RIT_SUCCESS) {
        return gen_result;
    }

    /* Evaluate each runout */
    for (int run = 0; run < config->num_runouts; run++) {
        rit_runout_result_t *runout_result =
            config->track_individual ? &result->runouts[run] : NULL;

        rit_runout_result_t temp_result;
        if (!runout_result) {
            runout_result = &temp_result;
        }

        int eval_result = rit_evaluate_runout(
            holes, num_players, runout_boards[run],
            config->game_type, runout_result);

        if (eval_result != RIT_SUCCESS) {
            return eval_result;
        }

        /* Accumulate statistics */
        for (int p = 0; p < num_players; p++) {
            result->avg_equity[p] += runout_result->equities[p];
            if (config->game_type == RIT_GAME_OMAHA8) {
                result->avg_low_equity[p] += runout_result->low_equities[p];
            }

            if (runout_result->winner_mask & (1 << p)) {
                if (runout_result->num_winners == 1) {
                    result->total_wins[p]++;
                } else {
                    result->total_ties[p]++;
                }
            } else {
                result->total_losses[p]++;
            }

            if (config->game_type == RIT_GAME_OMAHA8 && runout_result->low_available) {
                if (runout_result->low_winner_mask & (1 << p)) {
                    if (runout_result->low_num_winners == 1) {
                        result->total_low_wins[p]++;
                    } else {
                        result->total_low_ties[p]++;
                    }
                } else {
                    result->total_low_losses[p]++;
                }
            }
        }
    }

    /* Calculate average equity */
    for (int p = 0; p < num_players; p++) {
        result->avg_equity[p] /= config->num_runouts;
        if (config->game_type == RIT_GAME_OMAHA8) {
            result->avg_low_equity[p] /= config->num_runouts;
        }
    }

    /* Calculate variance if requested */
    if (config->calc_variance && config->track_individual) {
        for (int p = 0; p < num_players; p++) {
            double sum_sq_diff = 0.0;
            for (int run = 0; run < config->num_runouts; run++) {
                double diff = result->runouts[run].equities[p] - result->avg_equity[p];
                sum_sq_diff += diff * diff;
            }
            result->equity_variance[p] = sum_sq_diff / config->num_runouts;
            result->equity_stddev[p] = sqrt(result->equity_variance[p]);
        }

        if (config->game_type == RIT_GAME_OMAHA8) {
            for (int p = 0; p < num_players; p++) {
                double sum_sq_low = 0.0;
                for (int run = 0; run < config->num_runouts; run++) {
                    double diff = result->runouts[run].low_equities[p] - result->avg_low_equity[p];
                    sum_sq_low += diff * diff;
                }
                result->low_equity_variance[p] = sum_sq_low / config->num_runouts;
                result->low_equity_stddev[p] = sqrt(result->low_equity_variance[p]);
            }
        }
        result->variance_reduction = rit_theoretical_variance_reduction(config->num_runouts);
    }

    result->computation_time_ms = get_time_ms() - start_time;
    return RIT_SUCCESS;
}

/* Convenience wrapper for Hold'em */
int rit_holdem_equity(
    const StdDeck_CardMask holes[],
    int num_players,
    StdDeck_CardMask board,
    int num_runouts,
    rit_result_t *result)
{
    rit_config_t config = rit_config_holdem_rit2();
    config.num_runouts = num_runouts;

    /* Auto-detect stage from board */
    int cards_on_board = 0;
    for (int i = 0; i < StdDeck_N_CARDS; i++) {
        if (StdDeck_CardMask_CARD_IS_SET(board, i)) cards_on_board++;
    }

    if (cards_on_board == 0) config.stage = RIT_STAGE_FLOP;
    else if (cards_on_board == 3) config.stage = RIT_STAGE_TURN_RIVER;
    else if (cards_on_board == 4) config.stage = RIT_STAGE_RIVER;
    else return RIT_ERROR_INVALID_STAGE;

    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);

    return rit_calculate_equity(holes, num_players, board, dead, &config, result);
}

/* Convenience wrapper for Omaha */
int rit_omaha_equity(
    const StdDeck_CardMask holes[],
    int num_players,
    StdDeck_CardMask board,
    int num_runouts,
    rit_result_t *result)
{
    rit_config_t config = rit_config_omaha_rit2();
    config.num_runouts = num_runouts;

    /* Auto-detect stage from board */
    int cards_on_board = 0;
    for (int i = 0; i < StdDeck_N_CARDS; i++) {
        if (StdDeck_CardMask_CARD_IS_SET(board, i)) cards_on_board++;
    }

    if (cards_on_board == 0) config.stage = RIT_STAGE_FLOP;
    else if (cards_on_board == 3) config.stage = RIT_STAGE_TURN_RIVER;
    else if (cards_on_board == 4) config.stage = RIT_STAGE_RIVER;
    else return RIT_ERROR_INVALID_STAGE;

    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);

    return rit_calculate_equity(holes, num_players, board, dead, &config, result);
}

/* Print results */
void rit_print_result(const rit_result_t *result, bool verbose) {
    if (!result) return;

    printf("\n=== Run It %d Times Results ===\n", result->num_runouts);
    printf("Players: %d\n", result->num_players);
    printf("Game: %s\n",
        result->game_type == RIT_GAME_HOLDEM ? "Hold'em" :
        result->game_type == RIT_GAME_OMAHA ? "Omaha" : "Omaha Hi/Lo");
    printf("Computation time: %.2f ms\n\n", result->computation_time_ms);

    printf("Player  Avg Equity   Wins  Ties  Losses   StdDev\n");
    printf("------  ----------  ----  ----  -------  ------\n");
    for (int p = 0; p < result->num_players; p++) {
        printf("  %d     %6.2f%%    %3d   %3d    %3d    %.4f\n",
            p + 1,
            result->avg_equity[p] * 100.0,
            result->total_wins[p],
            result->total_ties[p],
            result->total_losses[p],
            result->equity_stddev[p]);
    }

    if (result->game_type == RIT_GAME_OMAHA8) {
        printf("\n--- Low Pot Summary ---\n");
        printf("Player  Low Equity  Wins  Ties  Losses   StdDev\n");
        printf("------  ----------  ----  ----  -------  ------\n");
        for (int p = 0; p < result->num_players; ++p) {
            printf("  %d     %6.2f%%    %3d   %3d    %3d    %.4f\n",
                   p + 1,
                   result->avg_low_equity[p] * 100.0,
                   result->total_low_wins[p],
                   result->total_low_ties[p],
                   result->total_low_losses[p],
                   result->low_equity_stddev[p]);
        }
    }

    if (result->variance_reduction > 1.0) {
        printf("\nVariance reduction factor: %.2fx\n", result->variance_reduction);
    }

    if (verbose) {
        printf("\n--- Individual Runouts ---\n");
        for (int run = 0; run < result->num_runouts; run++) {
            printf("\nRunout %d: ", run + 1);
            StdDeck_CardMask board_copy = result->runouts[run].board;
            StdDeck_printMask(board_copy);
            printf(" Winners: %d\n", result->runouts[run].num_winners);
            for (int p = 0; p < result->num_players; p++) {
                printf("  Player %d: equity=%.2f%%\n",
                    p + 1, result->runouts[run].equities[p] * 100.0);
                if (result->game_type == RIT_GAME_OMAHA8 && result->runouts[run].low_available) {
                    printf("             low_eq=%.2f%%\n",
                           result->runouts[run].low_equities[p] * 100.0);
                }
            }
            if (result->game_type == RIT_GAME_OMAHA8 && result->runouts[run].low_available) {
                printf("  Low winners mask: 0x%X (count %d)\n",
                       result->runouts[run].low_winner_mask,
                       result->runouts[run].low_num_winners);
            }
        }
    }
}

/* Export to CSV */
int rit_export_csv(const rit_result_t *result, const char *filename) {
    if (!result || !filename) return RIT_ERROR_INVALID_CONFIG;

    FILE *f = fopen(filename, "w");
    if (!f) return RIT_ERROR_IO_ERROR;

    fprintf(f, "player,avg_equity,avg_low_equity,wins,ties,losses,low_wins,low_ties,low_losses,stddev,low_stddev\n");
    for (int p = 0; p < result->num_players; p++) {
        fprintf(f, "%d,%.6f,%.6f,%d,%d,%d,%d,%d,%d,%.6f,%.6f\n",
            p + 1,
            result->avg_equity[p],
            result->avg_low_equity[p],
            result->total_wins[p],
            result->total_ties[p],
            result->total_losses[p],
            result->total_low_wins[p],
            result->total_low_ties[p],
            result->total_low_losses[p],
            result->equity_stddev[p],
            result->low_equity_stddev[p]);
    }

    fclose(f);
    return RIT_SUCCESS;
}
