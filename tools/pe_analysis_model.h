/*
 * pe_analysis_model.h - equity and ICM analysis, without a GUI.
 *
 * The Studio's ANALYSIS tab is a thin shell over this file. Keeping the
 * computation separate is what lets it be tested: a panel cannot be asserted
 * on, a report can.
 *
 * Everything here takes text exactly as a user typed it and returns either a
 * filled report or a message saying which field was wrong. No function here
 * prints, allocates for the caller, or knows what a widget is.
 */

#ifndef POKER_EVAL_PE_ANALYSIS_MODEL_H
#define POKER_EVAL_PE_ANALYSIS_MODEL_H

#include <poker_eval/equity.h>
#include <poker_eval/economics/icm.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PE_ANALYSIS_MAX_PLAYERS 6
#define PE_ANALYSIS_ERROR_MAX   256

/* ------------------------------------------------------------------ *
 * Equity
 * ------------------------------------------------------------------ */

typedef struct
{
    enum_game_t game;
    const char *ranges[PE_ANALYSIS_MAX_PLAYERS];
    int player_count;
    const char *board;        /* "AhKd7s", empty or NULL for preflop  */
    const char *dead;         /* removed cards, may be empty or NULL  */
    int monte_carlo;          /* force Monte Carlo instead of the heuristic */
    long iterations;          /* 0 keeps the library default          */
} pe_analysis_equity_request_t;

typedef struct
{
    int player_count;
    double equity[PE_ANALYSIS_MAX_PLAYERS];
    double win[PE_ANALYSIS_MAX_PLAYERS];
    double tie[PE_ANALYSIS_MAX_PLAYERS];
    size_t combos[PE_ANALYSIS_MAX_PLAYERS];
    /*
     * What the engine counted. Measured against known matchups this is the
     * number of valid range-vs-range combo pairs after card removal, not a
     * number of board draws -- AA vs AKs reports 12, which is the 6 AA combos
     * times the 2 AKs combos each of them leaves possible. Label it as
     * matchups when showing it, or a reader will take 12 for a sample size
     * and distrust a correct answer.
     */
    long samples;
    int exact;                /* the engine's own exact/sampled flag */
    char error[PE_ANALYSIS_ERROR_MAX];
} pe_analysis_equity_report_t;

/**
 * Run one equity calculation.
 * @return 0 on success; -1 with `error` filled otherwise.
 */
int pe_analysis_equity(const pe_analysis_equity_request_t *request,
                       pe_analysis_equity_report_t *out);

/* ------------------------------------------------------------------ *
 * Made-hand breakdown (the Flopzilla view)
 * ------------------------------------------------------------------ */

/* Ordered weakest to strongest, which is the order the table is read in. */
typedef enum
{
    PE_HAND_CLASS_HIGH_CARD = 0,
    PE_HAND_CLASS_PAIR,
    PE_HAND_CLASS_TWO_PAIR,
    PE_HAND_CLASS_TRIPS,
    PE_HAND_CLASS_STRAIGHT,
    PE_HAND_CLASS_FLUSH,
    PE_HAND_CLASS_FULL_HOUSE,
    PE_HAND_CLASS_QUADS,
    PE_HAND_CLASS_STRAIGHT_FLUSH,
    PE_HAND_CLASS_COUNT
} pe_hand_class_t;

const char *pe_hand_class_name(pe_hand_class_t hand_class);

typedef struct
{
    double weight[PE_HAND_CLASS_COUNT];  /* combo weight in each class   */
    double share[PE_HAND_CLASS_COUNT];   /* fraction of the live range   */
    double total_weight;
    size_t live_combos;                  /* combos not blocked by board  */
    size_t blocked_combos;
    char error[PE_ANALYSIS_ERROR_MAX];
} pe_analysis_breakdown_t;

/**
 * Classify every combo of `range` on `board`.
 *
 * Requires a board of at least three cards: with no board there is no made
 * hand to classify, and reporting "100% high card" for a preflop range would
 * be worse than refusing.
 *
 * @return 0 on success; -1 with `error` filled otherwise.
 */
int pe_analysis_breakdown(enum_game_t game, const char *range,
                          const char *board, const char *dead,
                          pe_analysis_breakdown_t *out);

/* ------------------------------------------------------------------ *
 * ICM
 * ------------------------------------------------------------------ */

typedef struct
{
    const char *stacks;   /* "5000, 3000, 2000" */
    const char *payouts;  /* "500, 300, 200"    */
} pe_analysis_icm_request_t;

typedef struct
{
    int player_count;
    int payout_count;
    double stacks[ICM_MAX_PLAYERS];
    double chip_share[ICM_MAX_PLAYERS];  /* stack / total, for comparison */
    double equity[ICM_MAX_PLAYERS];      /* ICM share of the prize pool   */
    double ev[ICM_MAX_PLAYERS];          /* ICM equity in currency        */
    double prize_pool;
    char error[PE_ANALYSIS_ERROR_MAX];
} pe_analysis_icm_report_t;

/**
 * Malmuth-Harville ICM over the given stacks and payout ladder.
 * @return 0 on success; -1 with `error` filled otherwise.
 */
int pe_analysis_icm(const pe_analysis_icm_request_t *request,
                    pe_analysis_icm_report_t *out);

/** Parse a comma or space separated list of non-negative numbers. */
int pe_analysis_parse_numbers(const char *text, double *out, int capacity,
                              int *out_count, char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_ANALYSIS_MODEL_H */
