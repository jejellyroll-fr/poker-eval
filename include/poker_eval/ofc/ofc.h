/*
 * ofc.h - Open Face Chinese Poker Public API
 *
 * Copyright (C) 2025 poker-eval contributors
 *
 * Public API for Open Face Chinese Poker game variant.
 * Use: #include <poker_eval/ofc/ofc.h>
 */

#ifndef POKER_EVAL_OFC_OFC_H
#define POKER_EVAL_OFC_OFC_H

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/status.h>
#include <poker_eval/core/pokereval_export.h>
#define DECK_STANDARD
#include <poker_eval/deck/deck_std.h>
#include <stdint.h>
#include <stdbool.h>

/* Include SIMD header for type definitions */
struct ofc_score_t;  /* Forward declaration to avoid circular dependency */

/* Define enum here to avoid forward declaration issue */
#ifndef OFC_PROCESSING_MODE_DEFINED
#define OFC_PROCESSING_MODE_DEFINED
typedef enum {
    OFC_PROCESS_CPU = 0,        /* Standard CPU processing */
    OFC_PROCESS_SIMD_AUTO,      /* Automatic SIMD selection */
    OFC_PROCESS_SIMD_SSE2,      /* Force SSE2 */
    OFC_PROCESS_SIMD_AVX2,      /* Force AVX2 */
    OFC_PROCESS_SIMD_AVX512,    /* Force AVX-512 */
    OFC_PROCESS_GPU             /* GPU acceleration */
} ofc_processing_mode_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ===== OFC Constants ===== */

#define OFC_TOP_CARDS       3
#define OFC_MIDDLE_CARDS    5
#define OFC_BOTTOM_CARDS    5
#define OFC_TOTAL_CARDS     13
#define OFC_MAX_PLAYERS     4

/* ===== OFC Types ===== */

/* OFC Hand positions */
typedef enum {
    OFC_TOP = 0,
    OFC_MIDDLE = 1,
    OFC_BOTTOM = 2,
    OFC_NUM_HANDS = 3
} ofc_position_t;

/* OFC Game states */
typedef enum {
    OFC_STATE_INITIAL,      /* Dealing 5 initial cards */
    OFC_STATE_PLACEMENT,    /* Placing cards one by one */
    OFC_STATE_COMPLETE,     /* All 13 cards placed */
    OFC_STATE_FANTASYLAND   /* Special fantasyland mode */
} ofc_state_t;

/* OFC Hand structure - represents one player's 3 hands */
typedef struct {
    StdDeck_CardMask hands[OFC_NUM_HANDS];  /* top, middle, bottom */
    int card_count[OFC_NUM_HANDS];          /* cards in each hand */
    int is_foul;                            /* hierarchy violation */
    int is_fantasyland;                     /* fantasyland eligible */
    int stay_fantasyland;                   /* stay in fantasyland next hand */
} ofc_hand_t;

/* OFC Player hand structure (alias for compatibility) */
typedef ofc_hand_t ofc_player_hand_t;

/* OFC Game structure */
typedef struct {
    ofc_player_hand_t players[OFC_MAX_PLAYERS];
    int num_players;
    ofc_state_t state;
    int current_round;
    StdDeck_CardMask deck;      /* Remaining cards */
    bool pineapple_mode; /* OFC Pineapple variant */
} ofc_game_t;

/* OFC Royalties structure */
typedef struct {
    int top_royalties;
    int middle_royalties;
    int bottom_royalties;
} ofc_royalties_t;

/* OFC Scoring structure */
typedef struct {
    int points[OFC_MAX_PLAYERS];
    int royalties[OFC_MAX_PLAYERS];
    bool scoop[OFC_MAX_PLAYERS];
    int total_score[OFC_MAX_PLAYERS];
} ofc_scoring_t;

/* OFC Score result between two players */
typedef struct {
    int points[OFC_NUM_HANDS];     /* points per hand */
    ofc_royalties_t royalties;     /* bonus points */
    int scoop_bonus;               /* +3 for winning all hands */
    int foul_penalty;              /* -6 for fouling */
    int total_score;               /* final score */
} ofc_score_t;

/* Placement comparison result structure */
typedef struct {
    ofc_position_t position;
    float foul_risk;
    int is_valid;                  /* Can the card be placed in this position? */
    const char* risk_level;        /* "LOW", "MEDIUM", "HIGH", "VERY HIGH" */
} ofc_placement_result_t;

/* Complete hand analysis structure */
typedef struct {
    /* Basic scoring */
    ofc_royalties_t royalties;     /* Royalties for each position */
    int total_royalties;           /* Sum of all royalties */
    int is_foul;                   /* Hand is fouled */
    int is_fantasyland;            /* Qualifies for fantasyland */
    int stay_fantasyland;          /* Stays in fantasyland */

    /* Hand strength analysis (0-100 scale) */
    int top_strength;              /* Top hand strength */
    int middle_strength;           /* Middle hand strength */
    int bottom_strength;           /* Bottom hand strength */
    int overall_strength;          /* Combined strength */

    /* Risk assessment */
    float foul_margin;             /* How close to fouling (0-1, 0=safe) */
    const char* risk_level;        /* "SAFE", "MODERATE", "HIGH", "FOULED" */

    /* Comparative analysis */
    float equity_vs_average;       /* Equity vs average opponent (0-1) */
    float scoop_probability;       /* Probability of scooping (0-1) */

    /* Strategic recommendations */
    const char* strategy_rating;   /* "EXCELLENT", "GOOD", "AVERAGE", "POOR" */
    const char* main_weakness;     /* Primary area for improvement */
    const char* main_strength;     /* Primary advantage */
} ofc_complete_analysis_t;

/* ===== OFC Game Management ===== */

/* Initialize new OFC game */
PE_API pe_status_t pe_ofc_game_create(int num_players, bool pineapple_mode, ofc_game_t** out_game);

/* Destroy OFC game and free resources */
PE_API void pe_ofc_game_destroy(ofc_game_t* game);

/* Reset game for new hand */
PE_API pe_status_t pe_ofc_game_reset(ofc_game_t* game);

/* Deal initial cards to all players */
PE_API pe_status_t pe_ofc_deal_initial(ofc_game_t* game);

/* ===== Card Placement ===== */

/* Place card in player's hand at specified position */
PE_API pe_status_t pe_ofc_place_card(ofc_game_t* game, int player_id,
                                     StdDeck_CardMask card, ofc_position_t position);

/* Validate card placement (check for fouls) */
PE_API pe_status_t pe_ofc_validate_placement(const ofc_game_t* game, int player_id,
                                            StdDeck_CardMask card, ofc_position_t position,
                                            bool* out_valid);

/* Check if player hand is fouled */
PE_API pe_status_t pe_ofc_check_foul(const ofc_player_hand_t* hand, bool* out_fouled);

/* ===== Hand Evaluation ===== */

/* Evaluate OFC hand strength for scoring */
PE_API pe_status_t pe_ofc_evaluate_hand(const ofc_player_hand_t* hand,
                                        HandVal* top_val, HandVal* middle_val, HandVal* bottom_val);

/* Calculate royalties for a hand */
PE_API pe_status_t pe_ofc_calculate_royalties(const ofc_player_hand_t* hand, int* out_royalties);

/* Check fantasyland qualification */
PE_API pe_status_t pe_ofc_check_fantasyland(const ofc_player_hand_t* hand, bool* out_qualified);

/* ===== Scoring ===== */

/* Calculate complete game scoring */
PE_API pe_status_t pe_ofc_calculate_scoring(const ofc_game_t* game, ofc_scoring_t* out_scoring);

/* Compare two OFC hands (returns player who wins each position) */
PE_API pe_status_t pe_ofc_compare_hands(const ofc_player_hand_t* hand1,
                                        const ofc_player_hand_t* hand2,
                                        int* out_winner_top, int* out_winner_middle, int* out_winner_bottom);

/* ===== Game State Queries ===== */

/* Get current game state */
PE_API pe_status_t pe_ofc_get_state(const ofc_game_t* game, ofc_state_t* out_state);

/* Get number of cards placed by player */
PE_API pe_status_t pe_ofc_get_cards_placed(const ofc_game_t* game, int player_id, int* out_count);

/* Check if game is complete */
PE_API pe_status_t pe_ofc_is_complete(const ofc_game_t* game, bool* out_complete);

/* ===== Utility Functions ===== */

/* Convert OFC hand to string representation */
PE_API pe_status_t pe_ofc_hand_to_string(const ofc_player_hand_t* hand, char* buffer, size_t buffer_size);

/* Get hand name for position */
PE_API const char* pe_ofc_position_name(ofc_position_t position);

/* Get state name */
PE_API const char* pe_ofc_state_name(ofc_state_t state);

/* ===== Legacy OFC Functions (for compatibility) ===== */

/* Initialize OFC hand */
extern int OFC_InitializeHand(ofc_player_hand_t *hand);

/* Validate OFC hierarchy */
extern int OFC_ValidateHierarchy(const ofc_player_hand_t *hand);

/* Check if hand is complete */
extern int OFC_IsComplete(const ofc_player_hand_t *hand);

/* Evaluate OFC hand value */
extern HandVal OFC_EvaluateHand(StdDeck_CardMask cards, int num_cards);

/* Compare two OFC hands */
extern int OFC_CompareHands(StdDeck_CardMask hand1, int cards1, StdDeck_CardMask hand2, int cards2, ofc_position_t position);

/* Get position name string */
extern const char* OFC_PositionToString(ofc_position_t position);

/* Calculate royalties for different positions */
extern int OFC_CalculateTopRoyalties(StdDeck_CardMask top_hand);
extern int OFC_CalculateMiddleRoyalties(StdDeck_CardMask middle_hand);
extern int OFC_CalculateBottomRoyalties(StdDeck_CardMask bottom_hand);
extern int OFC_CalculateRoyalties(const ofc_player_hand_t *hand, ofc_royalties_t *royalties);

/* OFC scoring and printing functions */
extern int OFC_ScoreHands(const ofc_player_hand_t *hand1, const ofc_player_hand_t *hand2,
                         ofc_score_t *score1, ofc_score_t *score2);
extern void OFC_PrintHand(const ofc_player_hand_t *hand);
extern void OFC_PrintScore(const ofc_score_t *score);

/* Place card in hand at specified position */
extern int OFC_PlaceCard(ofc_player_hand_t *hand, int card, ofc_position_t position);

/* Processing mode enum - defined in ofc_simd.h */

/* SIMD hierarchy validation */
extern int OFC_ValidateHierarchySIMD(
    const ofc_player_hand_t *hands,
    int batch_size,
    int *valid_flags,
    ofc_processing_mode_t mode
);

/* ===== Helper and Analysis Functions ===== */

/* Check if card is available in partial hand */
extern int OFC_IsCardAvailable(const ofc_player_hand_t *partial_hand, int card);

/* Get list of available cards */
extern int OFC_GetAvailableCards(const ofc_player_hand_t *partial_hand, int *available_cards);

/* Calculate foul risk for placing a card */
extern float OFC_CalculateFoulRisk(const ofc_player_hand_t *partial_hand, int card, ofc_position_t position);

/* Calculate fantasyland probability */
extern float OFC_CalculateFantasylandProbability(const ofc_player_hand_t *partial_hand);

/* Check if hand qualifies for fantasyland */
extern int OFC_CheckFantasyland(const ofc_player_hand_t *hand);

/* Check if hand stays in fantasyland */
extern int OFC_CheckStayFantasyland(const ofc_player_hand_t *hand);

/* Compare placement options */
extern int OFC_ComparePlacements(const ofc_player_hand_t *partial_hand, int card,
                                ofc_position_t positions[], int num_positions,
                                ofc_placement_result_t results[]);

/* Get recommended placement */
extern ofc_position_t OFC_GetRecommendedPlacement(const ofc_placement_result_t results[], int num_results);

/* Calculate expected royalties for position */
extern float OFC_CalculateExpectedRoyalties(const ofc_player_hand_t *partial_hand, ofc_position_t position);

/* Get hand strength (0-100) */
extern int OFC_GetHandStrength(StdDeck_CardMask cards, int num_cards, ofc_position_t position);

/* Analyze complete hand */
extern int OFC_AnalyzeCompleteHand(const ofc_player_hand_t *hand, ofc_complete_analysis_t *analysis);

/* Advanced analysis functions */
extern float OFC_CalculateEquityVsRandom(const ofc_player_hand_t *hand, int num_simulations);
extern float OFC_CalculateScoopProbability(const ofc_player_hand_t *hand, int num_simulations);
extern int OFC_CompareCompleteHands(const ofc_player_hand_t *hand1, const ofc_player_hand_t *hand2,
                                   ofc_complete_analysis_t *analysis1, ofc_complete_analysis_t *analysis2,
                                   ofc_score_t *score1, ofc_score_t *score2);
extern void OFC_PrintCompleteAnalysis(const ofc_complete_analysis_t *analysis);
extern const char* OFC_GetStrategicAdvice(const ofc_complete_analysis_t *analysis);
extern int OFC_InitializeSIMD(void);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_OFC_OFC_H */
