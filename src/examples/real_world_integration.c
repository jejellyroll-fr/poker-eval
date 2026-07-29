/*
 * real_world_integration.c - Example of integrating MT optimizations in a real poker application
 * 
 * This example shows how to use the optimized range equity calculations
 * in a realistic poker analysis scenario.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <poker_eval/core/unistd_compat.h>
#include <poker_eval/core/pthread_compat.h>
#include <poker_eval/poker_eval.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/equity/RangeEquity.h>
#include <poker_eval/equity/RangeEquity_MT.h>

// Poker range parser (simplified)
typedef struct {
    char notation[32];  // e.g., "AA-TT", "AKs", "22+"
    StdDeck_CardMask *hands;
    int count;
    int capacity;
} PokerRange;

// Game state
typedef struct {
    StdDeck_CardMask board;
    StdDeck_CardMask dead_cards;
    int num_players;
    PokerRange *player_ranges;
    double pot_size;
    double bet_size;
} RealWorldGameState;

// Analysis result
typedef struct {
    double equity[ENUM_MAXPLAYERS];
    double ev[ENUM_MAXPLAYERS];
    double win_rate[ENUM_MAXPLAYERS];
    double tie_rate[ENUM_MAXPLAYERS];
    int total_matchups;
    double calculation_time;
} AnalysisResult;

// Async analysis using threads
typedef struct {
    RealWorldGameState *state;
    AnalysisResult *result;
    void (*callback)(AnalysisResult* result);
} AsyncAnalysisData;

/* Function prototypes */
static int parse_range_notation(const char *notation, PokerRange *range);
static void free_range(PokerRange *range);
static AnalysisResult analyze_equity(RealWorldGameState *state);
static void* async_analysis_worker(void *data);
static int start_async_analysis(RealWorldGameState *state, AnalysisResult *result, 
                              void (*callback)(AnalysisResult* result), pthread_t *thread_ptr);
static void analysis_complete_callback(AnalysisResult *result);

// Parse a simple range notation (basic implementation)
static int parse_range_notation(const char *notation, PokerRange *range) {
    range->count = 0;
    strncpy(range->notation, notation, 31);
    
    // Allocate space
    range->capacity = 200;
    range->hands = malloc(sizeof(StdDeck_CardMask) * range->capacity);
    
    // Parse pocket pairs
    if (strstr(notation, "AA")) {
        // Add all AA combinations
        for (int s1 = 0; s1 < 4; s1++) {
            for (int s2 = s1 + 1; s2 < 4; s2++) {
                StdDeck_CardMask_RESET(range->hands[range->count]);
                StdDeck_CardMask_SET(range->hands[range->count], 
                    StdDeck_MAKE_CARD(StdDeck_Rank_ACE, s1));
                StdDeck_CardMask_SET(range->hands[range->count], 
                    StdDeck_MAKE_CARD(StdDeck_Rank_ACE, s2));
                range->count++;
            }
        }
    }
    
    if (strstr(notation, "KK")) {
        // Add all KK combinations
        for (int s1 = 0; s1 < 4; s1++) {
            for (int s2 = s1 + 1; s2 < 4; s2++) {
                StdDeck_CardMask_RESET(range->hands[range->count]);
                StdDeck_CardMask_SET(range->hands[range->count], 
                    StdDeck_MAKE_CARD(StdDeck_Rank_KING, s1));
                StdDeck_CardMask_SET(range->hands[range->count], 
                    StdDeck_MAKE_CARD(StdDeck_Rank_KING, s2));
                range->count++;
            }
        }
    }
    
    if (strstr(notation, "QQ")) {
        // Add all QQ combinations
        for (int s1 = 0; s1 < 4; s1++) {
            for (int s2 = s1 + 1; s2 < 4; s2++) {
                StdDeck_CardMask_RESET(range->hands[range->count]);
                StdDeck_CardMask_SET(range->hands[range->count], 
                    StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, s1));
                StdDeck_CardMask_SET(range->hands[range->count], 
                    StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, s2));
                range->count++;
            }
        }
    }
    
    // Add more parsing logic as needed...
    
    return range->count;
}

// Free range memory
static void free_range(PokerRange *range) {
    if (range->hands) {
        free(range->hands);
        range->hands = NULL;
    }
    range->count = 0;
}

// Analyze equity with optimal MT version selection
static AnalysisResult analyze_equity(RealWorldGameState *state) {
    AnalysisResult result = {0};
    
    // Convert to PlayerRange format
    PlayerRange *ranges = malloc(sizeof(PlayerRange) * state->num_players);
    for (int i = 0; i < state->num_players; i++) {
        ranges[i].hand_masks = state->player_ranges[i].hands;
        ranges[i].weights = NULL;
        ranges[i].count = state->player_ranges[i].count;
        ranges[i].total_weight = (double)state->player_ranges[i].count;
    }
    
    // Calculate total potential matchups
    int total_potential = 1;
    for (int i = 0; i < state->num_players; i++) {
        total_potential *= ranges[i].count;
    }
    
    printf("Analyzing %d-way pot with %d potential matchups...\n", 
           state->num_players, total_potential);
    
    // Choose optimal calculation method
    enum_result_t enum_result;
    enumResultAlloc(&enum_result, state->num_players, enum_ordering_mode_hi);
    
    clock_t start = clock();
    
    if (total_potential < 100) {
        // Small: use single-threaded
        printf("Using single-threaded calculation (small range)\n");
        result.total_matchups = CalculateEquityForRanges(
            game_holdem, ranges, state->num_players, 
            state->board, state->dead_cards,
            5 - StdDeck_numCards(state->board),
            0, 0, 0, &enum_result
        );
    } else if (total_potential < 10000) {
        // Medium: use MT_v2 with 4 threads
        printf("Using MT_v2 with 4 threads (medium range)\n");
        result.total_matchups = CalculateEquityForRanges_MT_v2(
            game_holdem, ranges, state->num_players,
            state->board, state->dead_cards,
            5 - StdDeck_numCards(state->board),
            0, 0, 0, &enum_result, 4
        );
    } else {
        // Large: use MT_v3 for memory efficiency
        printf("Using MT_v3 with auto threads (large range)\n");
        result.total_matchups = CalculateEquityForRanges_MT_v3(
            game_holdem, ranges, state->num_players,
            state->board, state->dead_cards,
            5 - StdDeck_numCards(state->board),
            0, 0, 0, &enum_result, 0
        );
    }
    
    result.calculation_time = ((double)(clock() - start)) / CLOCKS_PER_SEC;
    
    // Extract results
    if (result.total_matchups > 0) {
        for (int i = 0; i < state->num_players; i++) {
            result.equity[i] = enum_result.ev[i] / result.total_matchups * 100;
            result.win_rate[i] = (double)enum_result.nwinhi[i] / result.total_matchups * 100;
            result.tie_rate[i] = (double)enum_result.ntiehi[i] / result.total_matchups * 100;
            
            // Calculate EV if pot and bet sizes are provided
            if (state->pot_size > 0) {
                result.ev[i] = (result.equity[i] / 100.0) * state->pot_size - state->bet_size;
            }
        }
    }
    
    enumResultFree(&enum_result);
    free(ranges);
    
    return result;
}

static void* async_analysis_worker(void *data) {
    AsyncAnalysisData *async_data = (AsyncAnalysisData*)data;
    *async_data->result = analyze_equity(async_data->state);
    
    if (async_data->callback) {
        async_data->callback(async_data->result);
    }
    
    return NULL;
}

// Start async analysis
static int start_async_analysis(RealWorldGameState *state, AnalysisResult *result, 
                              void (*callback)(AnalysisResult* result), pthread_t *thread_ptr) {
    AsyncAnalysisData *data = malloc(sizeof(AsyncAnalysisData));
    data->state = state;
    data->result = result;
    data->callback = callback;
    
    return pthread_create(thread_ptr, NULL, async_analysis_worker, data);
}

// Example callback
static void analysis_complete_callback(AnalysisResult *result) {
    printf("\nAnalysis complete! (callback)\n");
    printf("Calculation took: %.3f seconds\n", result->calculation_time);
}

// Main example
int main(void) {
    printf("=== Real World Integration Example ===\n\n");
    
    // Example 1: Simple heads-up analysis
    printf("Example 1: Heads-up on the flop\n");
    printf("Board: As Kh 5c\n");
    printf("Player 1: AA\n");
    printf("Player 2: KK\n\n");
    
    RealWorldGameState state1 = {0};
    state1.num_players = 2;
    state1.player_ranges = malloc(sizeof(PokerRange) * 2);
    state1.pot_size = 100.0;
    state1.bet_size = 0.0;
    
    // Set up board
    StdDeck_CardMask_RESET(state1.board);
    StdDeck_CardMask_SET(state1.board, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(state1.board, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(state1.board, StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_RESET(state1.dead_cards);
    
    // Parse ranges
    parse_range_notation("AA", &state1.player_ranges[0]);
    parse_range_notation("KK", &state1.player_ranges[1]);
    
    // Analyze
    AnalysisResult result1 = analyze_equity(&state1);
    
    printf("\nResults:\n");
    printf("Valid matchups analyzed: %d\n", result1.total_matchups);
    printf("Player 1 (AA): %.2f%% equity, %.2f%% win, %.2f%% tie\n",
           result1.equity[0], result1.win_rate[0], result1.tie_rate[0]);
    printf("Player 2 (KK): %.2f%% equity, %.2f%% win, %.2f%% tie\n",
           result1.equity[1], result1.win_rate[1], result1.tie_rate[1]);
    printf("Calculation time: %.3f seconds\n", result1.calculation_time);
    
    // Example 2: Multi-way pot
    printf("\n\nExample 2: 3-way pot preflop\n");
    printf("Player 1: AA-QQ\n");
    printf("Player 2: KK\n");
    printf("Player 3: QQ\n\n");
    
    RealWorldGameState state2 = {0};
    state2.num_players = 3;
    state2.player_ranges = malloc(sizeof(PokerRange) * 3);
    state2.pot_size = 150.0;
    state2.bet_size = 25.0;
    
    StdDeck_CardMask_RESET(state2.board);
    StdDeck_CardMask_RESET(state2.dead_cards);
    
    parse_range_notation("AA,KK,QQ", &state2.player_ranges[0]);
    parse_range_notation("KK", &state2.player_ranges[1]);
    parse_range_notation("QQ", &state2.player_ranges[2]);
    
    AnalysisResult result2 = analyze_equity(&state2);
    
    printf("\nResults:\n");
    printf("Valid matchups analyzed: %d\n", result2.total_matchups);
    for (int i = 0; i < 3; i++) {
        printf("Player %d: %.2f%% equity, EV: $%.2f\n",
               i + 1, result2.equity[i], result2.ev[i]);
    }
    printf("Calculation time: %.3f seconds\n", result2.calculation_time);
    
    // Example 3: Async analysis
    printf("\n\nExample 3: Async analysis\n");
    printf("Starting background calculation...\n");
    
    AnalysisResult result3;
    pthread_t async_thread_ptr;
    start_async_analysis(&state2, &result3, analysis_complete_callback, &async_thread_ptr);
    
    // Do other work while calculating...
    printf("Doing other work while equity calculates...\n");
    for (int i = 0; i < 3; i++) {
        printf("Working... %d\n", i + 1);
        usleep(100000); // 100ms
    }
    
    // Wait for completion
    pthread_join(async_thread_ptr, NULL);
    
    // Cleanup
    for (int i = 0; i < 2; i++) {
        free_range(&state1.player_ranges[i]);
    }
    free(state1.player_ranges);
    
    for (int i = 0; i < 3; i++) {
        free_range(&state2.player_ranges[i]);
    }
    free(state2.player_ranges);
    
    printf("\n=== Integration Example Complete ===\n");
    
    return 0;
}
