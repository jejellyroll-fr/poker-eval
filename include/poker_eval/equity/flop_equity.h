#ifndef POKER_EVAL_FLOP_EQUITY_H
#define POKER_EVAL_FLOP_EQUITY_H

#include <poker_eval/deck/deck_std.h>
#include <stdbool.h>

/* Flop texture categories */
typedef enum {
    FLOP_TEXTURE_DRY = 0,        /* K-7-2 rainbow */
    FLOP_TEXTURE_WET,             /* 9-8-7 two-tone */
    FLOP_TEXTURE_COORDINATED,     /* T-9-8 monotone */
    FLOP_TEXTURE_PAIRED,          /* K-K-7 */
    FLOP_TEXTURE_TRIPS            /* K-K-K */
} flop_texture_category_t;

/* Flop analysis result */
typedef struct {
    /* Board properties */
    bool is_paired;              /* Board has pair? */
    bool is_trips;               /* Board has trips? */
    bool is_monotone;            /* All same suit? */
    bool is_two_tone;            /* Two cards same suit? */
    bool is_rainbow;             /* All different suits? */

    /* Rank properties */
    int high_card_rank;          /* Highest rank (0=A, 1=K, ..., 12=2) */
    int middle_card_rank;
    int low_card_rank;
    int paired_rank;             /* Rank of the pair (-1 if none) */

    /* Connectivity */
    bool is_connected;           /* Sequential ranks (e.g., 9-8-7) */
    int max_gap;                 /* Largest gap between ranks */
    int n_broadway;              /* Number of T-A cards (0-3) */
    int n_low_cards;             /* Number of 2-6 cards (0-3) */

    /* Draw potential */
    int straight_draw_outs;      /* Number of straight draw outs */
    int flush_draw_outs;         /* Number of flush draw outs */
    bool has_oesd;               /* Open-ended straight draw possible? */
    bool has_gutshot;            /* Gutshot straight draw possible? */

    /* Overall texture */
    flop_texture_category_t texture;
    int texture_score;           /* 0-100: 0=dry, 100=wet */
} flop_analysis_t;

/* Main analysis function */
int analyze_flop_texture(
    StdDeck_CardMask flop,       /* Exactly 3 cards */
    flop_analysis_t *analysis
);

/* Helper functions */
int flop_count_suits(StdDeck_CardMask flop, int suit_counts[4]);
int flop_get_ranks(StdDeck_CardMask flop, int ranks[3]);
bool flop_is_monotone(StdDeck_CardMask flop);
bool flop_is_paired(StdDeck_CardMask flop);
int flop_texture_score(const flop_analysis_t *analysis);

/* String conversion */
void flop_texture_to_string(flop_texture_category_t texture, char *out, size_t out_size);

/* Flop equity result with detailed breakdown */
typedef struct {
    double equity;
    double variance;

    /* Made hands (current) */
    double prob_high_card;
    double prob_pair;
    double prob_two_pair;
    double prob_trips;
    double prob_straight;
    double prob_flush;
    double prob_full_house;
    double prob_quads;

    /* Drawing hands */
    double prob_flush_draw;
    double prob_oesd;              /* Open-ended straight draw */
    double prob_gutshot;
    double prob_backdoor_flush;
    double prob_backdoor_straight;

    /* Improvement odds */
    double turn_improvement;       /* Prob of improving on turn */
    double river_improvement;      /* Prob of improving by river */
    double runner_runner_improvement;

    /* Outs counting */
    int made_hand_outs;            /* Outs to make a hand */
    int draw_outs;                 /* Total draw outs */
} flop_equity_result_t;

/* Flop equity input */
typedef struct {
    StdDeck_CardMask pocket;       /* Player's 2 cards */
    StdDeck_CardMask flop;         /* Flop (3 cards) */
    int n_opponents;               /* 1-9 opponents */
    int n_samples;                 /* 0=exhaustive, >0=Monte Carlo */
} flop_equity_input_t;

/* Calculate flop equity */
int flop_calc_equity(
    const flop_equity_input_t *input,
    flop_equity_result_t *result
);

/* Calculate specific probabilities */
double flop_calc_made_hand_prob(
    StdDeck_CardMask pocket,
    StdDeck_CardMask flop,
    int hand_type  /* HandVal category */
);

double flop_calc_draw_prob(
    StdDeck_CardMask pocket,
    StdDeck_CardMask flop,
    int draw_type  /* Flush draw, OESD, etc. */
);

/* Outs counting */
int flop_count_outs(
    StdDeck_CardMask pocket,
    StdDeck_CardMask flop,
    int *flush_outs,
    int *straight_outs,
    int *pair_outs
);

#endif /* POKER_EVAL_FLOP_EQUITY_H */
