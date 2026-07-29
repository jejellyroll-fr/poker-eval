#ifndef RANGE_EQUITY_H
#define RANGE_EQUITY_H

#include <poker_eval/deck/deck_std.h>   // For StdDeck_CardMask
#include <poker_eval/core/enumdefs.h>   // For enum_game_t, enum_result_t
#include <poker_eval/equity/sidepots.h>
#include <stdbool.h>    // For bool

// Only define TRACE and ASSERT if NDEBUG is not defined
#ifndef NDEBUG
#include <stdio.h> // For printf in TRACE
#include <assert.h> // For assert in ASSERT
#define TRACE_RE(...) do { printf("RangeEquity: "__VA_ARGS__); } while (0)
#define ASSERT_RE assert
#else
#define TRACE_RE(...)
#define ASSERT_RE(x) ((void)0)
#endif

typedef struct {
    const StdDeck_CardMask* hand_masks; // Pointer to the array of hands
    const double* weights;             // Optional weights per hand (NULL if uniform)
    int count;                         // Number of hands in the range
    double total_weight;               // Sum of weights (if known, else >0)
} PlayerRange;

typedef struct {
    PlayerRange* ranges;
    double* stack_sizes;
    double* invested;
    int num_players;
} MultiwayPotState;

typedef struct {
    bool use_montecarlo;
    int iterations;
    int orderflag;
} MultiwayEquityOptions;

typedef struct {
    double equity[ENUM_MAXPLAYERS];
    double win_prob[ENUM_MAXPLAYERS];
    double tie_prob[ENUM_MAXPLAYERS];
    double ev[ENUM_MAXPLAYERS];
    double total_weighted_samples;
} MultiwayEquityResult;

int CalculateMultiwayEquity(
    enum_game_t game,
    const MultiwayPotState* state,
    StdDeck_CardMask board,
    StdDeck_CardMask dead_cards_initial,
    int nboard_cards_to_deal,
    const MultiwayEquityOptions* options,
    MultiwayEquityResult* result
);

/*
Calculates equity for player ranges against each other.
Parameters:
  game: The game type (e.g., game_omaha, game_holdem).
  player_ranges: Array of PlayerRange structs, one for each player.
  num_players: Number of players (and ranges).
  board: Known community cards.
  dead_cards_initial: Known dead cards before considering player hands from ranges.
                      These are cards that absolutely cannot be in any player's hand or future board.
  nboard_cards_to_deal: Number of additional board cards to deal out.
  use_montecarlo: If true, use enumSample; otherwise, use enumExhaustive.
  iterations_if_montecarlo: Number of iterations for enumSample.
  orderflag: orderflag for enumExhaustive/enumSample (usually 0 for default EV calc).
  aggregated_results: Pointer to an enum_result_t struct to store the final aggregated results.
Returns:
  Number of valid (non-conflicting) hand matchups evaluated.
  Returns -1 on critical error (e.g., too many players, memory allocation failure).
  Returns 0 if no valid matchups found (e.g., all ranges conflict, or a range is empty).
*/
int CalculateEquityForRanges(
    enum_game_t game,
    const PlayerRange player_ranges[],
    int num_players,
    StdDeck_CardMask board,
    StdDeck_CardMask dead_cards_initial,
    int nboard_cards_to_deal,
    bool use_montecarlo,
    int iterations_if_montecarlo,
    int orderflag,
    enum_result_t* aggregated_results
);

/*
Multithreaded version of CalculateEquityForRanges.
Additional parameter:
  num_threads: Number of threads to use. If <= 0, uses all available threads.
*/
int CalculateEquityForRanges_MT(
    enum_game_t game,
    const PlayerRange player_ranges[],
    int num_players,
    StdDeck_CardMask board,
    StdDeck_CardMask dead_cards_initial,
    int nboard_cards_to_deal,
    int use_montecarlo,
    int iterations_if_montecarlo,
    int orderflag,
    enum_result_t* aggregated_results,
    int num_threads
);

/*
Automatically chooses between single-threaded and multithreaded equity calculation
based on the size of the workload.
*/
int CalculateEquityForRanges_Auto(
    enum_game_t game,
    const PlayerRange player_ranges[],
    int num_players,
    StdDeck_CardMask board,
    StdDeck_CardMask dead_cards_initial,
    int nboard_cards_to_deal,
    int use_montecarlo,
    int iterations_if_montecarlo,
    int orderflag,
    enum_result_t* aggregated_results
);

/*
Alternative multithreaded implementation of CalculateEquityForRanges.
*/
int CalculateEquityForRanges_MT_v2(
    enum_game_t game,
    const PlayerRange player_ranges[],
    int num_players,
    StdDeck_CardMask board,
    StdDeck_CardMask dead_cards_initial,
    int nboard_cards_to_deal,
    int use_montecarlo,
    int iterations_if_montecarlo,
    int orderflag,
    enum_result_t* aggregated_results,
    int num_threads
);

/*
Version 3 of multithreaded implementation of CalculateEquityForRanges.
*/
int CalculateEquityForRanges_MT_v3(
    enum_game_t game,
    const PlayerRange player_ranges[],
    int num_players,
    StdDeck_CardMask board,
    StdDeck_CardMask dead_cards_initial,
    int nboard_cards_to_deal,
    int use_montecarlo,
    int iterations_if_montecarlo,
    int orderflag,
    enum_result_t* aggregated_results,
    int num_threads
);

/*
Version 4 of multithreaded implementation of CalculateEquityForRanges.
*/
int CalculateEquityForRanges_MT_v4(
    enum_game_t game,
    const PlayerRange player_ranges[],
    int num_players,
    StdDeck_CardMask board,
    StdDeck_CardMask dead_cards_initial,
    int nboard_cards_to_deal,
    int use_montecarlo,
    int iterations_if_montecarlo,
    int orderflag,
    enum_result_t* aggregated_results,
    int num_threads
);

/*
Version 5 of multithreaded implementation of CalculateEquityForRanges.
*/
int CalculateEquityForRanges_MT_v5(
    enum_game_t game,
    const PlayerRange player_ranges[],
    int num_players,
    StdDeck_CardMask board,
    StdDeck_CardMask dead_cards_initial,
    int nboard_cards_to_deal,
    int use_montecarlo,
    int iterations_if_montecarlo,
    int orderflag,
    enum_result_t* aggregated_results,
    int num_threads
);

/*
Batched multithreaded implementation of CalculateEquityForRanges.
Uses batched Monte Carlo simulation for improved performance.
*/
int CalculateEquityForRanges_MT_Batched(
    enum_game_t game,
    const PlayerRange player_ranges[],
    int num_players,
    StdDeck_CardMask board,
    StdDeck_CardMask dead_cards_initial,
    int nboard_cards_to_deal,
    int use_montecarlo,
    int iterations_if_montecarlo,
    int orderflag,
    enum_result_t* aggregated_results,
    int num_threads
);

#endif // RANGE_EQUITY_H
