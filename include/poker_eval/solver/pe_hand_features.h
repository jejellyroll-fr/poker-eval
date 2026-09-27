/*
 * pe_hand_features.h - PLO4/PLO5/PLO6 hand features and strategy buckets
 * (issue #238)
 *
 * An analysis layer: it maps a concrete Omaha hand on a board to typed
 * features, turns any subset of those features into a deterministic bucket
 * key, and aggregates a strategy table over the buckets. It is not an
 * abstraction and the solver does not use it; it is meant to be reusable by
 * reports, range filtering, diagnostics and a later abstraction engine, so it
 * depends on nothing above the card and evaluator layer.
 *
 * Two kinds of features are kept apart on purpose:
 *
 *   private structure  what the hole cards are on their own: pairs, ranks,
 *                      connectivity, suit multiplicity. Defined for any board,
 *                      including none.
 *   evaluated          what the hand does on the board: made hand, draws,
 *                      nuts, blockers. Defined once the board has three
 *                      cards, and always under Omaha rules -- exactly two
 *                      hole cards and exactly three board cards. A hole card
 *                      that would only play as a third private card never
 *                      counts, for the made hand or for a draw.
 *
 * Nothing here allocates. Board-derived work (texture, the absolute nuts, the
 * nut card per suit) is done once into a pe_hf_board_t, and every hand on that
 * board is classified against it into a caller-owned value type.
 *
 * Ranks are 0 (deuce) to 12 (ace), cards are rank + 13 * suit, as in
 * <poker_eval/core/modern_cardmask.h>.
 */

#ifndef POKER_EVAL_PE_HAND_FEATURES_H
#define POKER_EVAL_PE_HAND_FEATURES_H

#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/solver/pe_solver.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PE_HF_MIN_HOLE 4u
#define PE_HF_MAX_HOLE 6u

/* ------------------------------------------------------------------ *
 * Feature values
 * ------------------------------------------------------------------ */

/** Best high hand from two hole and three board cards. */
typedef enum {
    PE_HF_MADE_HIGH_CARD = 0,
    PE_HF_MADE_PAIR,
    PE_HF_MADE_TWO_PAIR,
    PE_HF_MADE_TRIPS,
    PE_HF_MADE_STRAIGHT,
    PE_HF_MADE_FLUSH,
    PE_HF_MADE_FULL_HOUSE,
    PE_HF_MADE_QUADS,
    PE_HF_MADE_STRAIGHT_FLUSH,
    PE_HF_MADE_COUNT
} pe_hf_made_t;

/**
 * Where the made hand sits against the board. "Top", "second" and "bottom"
 * refer to the board's distinct ranks, highest first.
 */
typedef enum {
    PE_HF_DETAIL_NONE = 0,
    PE_HF_DETAIL_OVERPAIR,      /* pocket pair above every board rank */
    PE_HF_DETAIL_UNDERPAIR,     /* pocket pair below the top board rank */
    PE_HF_DETAIL_TOP_PAIR,      /* one hole card pairs the top board rank */
    PE_HF_DETAIL_SECOND_PAIR,   /* ... the second board rank */
    PE_HF_DETAIL_LOW_PAIR,      /* ... a lower board rank */
    PE_HF_DETAIL_BOARD_PAIR,    /* the only pair is the board's own */
    PE_HF_DETAIL_TOP_TWO_PAIR,  /* hole cards pair the top two board ranks */
    PE_HF_DETAIL_TOP_SET,       /* pocket pair matching the top board rank */
    PE_HF_DETAIL_MIDDLE_SET,    /* ... a board rank between top and bottom */
    PE_HF_DETAIL_BOTTOM_SET,    /* ... the bottom board rank */
    PE_HF_DETAIL_TRIPS,         /* one hole card with a paired board rank */
    PE_HF_DETAIL_BOARD_TRIPS,   /* the trips are all on the board */
    PE_HF_DETAIL_COUNT
} pe_hf_detail_t;

typedef enum {
    PE_HF_FLUSH_DRAW_NONE = 0,
    PE_HF_FLUSH_DRAW_BACKDOOR,  /* flop only: two hole + one board card */
    PE_HF_FLUSH_DRAW,           /* two hole + two board cards of a suit */
    PE_HF_FLUSH_DRAW_NUT,       /* ... holding that suit's nut card */
    PE_HF_FLUSH_DRAW_COUNT
} pe_hf_flush_draw_t;

/**
 * Straight draws, by how many ranks improve the hand to a straight (or to a
 * higher straight than the one already made).
 */
typedef enum {
    PE_HF_STRAIGHT_DRAW_NONE = 0,
    PE_HF_STRAIGHT_DRAW_GUTSHOT,    /* one rank */
    PE_HF_STRAIGHT_DRAW_OPEN_ENDED, /* two ranks */
    PE_HF_STRAIGHT_DRAW_WRAP,       /* three or more ranks */
    PE_HF_STRAIGHT_DRAW_COUNT
} pe_hf_straight_draw_t;

/**
 * Hole-card connectivity, from the missing ranks inside the tightest span of
 * the distinct hole ranks (the ace counted high or low, whichever is tighter).
 */
typedef enum {
    PE_HF_CONNECT_NONE = 0, /* fewer than two distinct ranks */
    PE_HF_CONNECT_RUNDOWN,  /* no missing rank: a run */
    PE_HF_CONNECT_HIGH,     /* one missing rank */
    PE_HF_CONNECT_MEDIUM,   /* two or three */
    PE_HF_CONNECT_LOW,      /* four or more */
    PE_HF_CONNECT_COUNT
} pe_hf_connectivity_t;

/** Blocker flags (pe_hand_features_t::blockers). */
enum {
    /** Holds the nut card of a suit that has, or can still make, a flush. */
    PE_HF_BLOCKS_NUT_FLUSH = 1u << 0,
    /** Holds a rank the current nut straight needs from the hole. */
    PE_HF_BLOCKS_NUT_STRAIGHT = 1u << 1,
    /** Holds a card of a paired board rank (boats, quads). */
    PE_HF_BLOCKS_BOARD_PAIR = 1u << 2,
    /** Holds a card of the top board rank (top set, top pair). */
    PE_HF_BLOCKS_TOP_CARD = 1u << 3
};

/* ------------------------------------------------------------------ *
 * Board context
 * ------------------------------------------------------------------ */

/**
 * Everything about a board that does not depend on the hand. Prepare it once
 * with pe_hf_board_prepare() and share it across every hand on that board.
 */
typedef struct pe_hf_board_t {
    mask_t board;
    uint8_t card_count;          /* 0, 3, 4 or 5 */
    uint8_t rank_count[13];
    uint8_t suit_count[4];
    uint16_t rank_mask;          /* bit r: rank r is on the board */
    uint8_t distinct_ranks;
    int8_t top_rank;             /* distinct ranks, -1 when absent */
    int8_t second_rank;
    int8_t bottom_rank;
    uint8_t paired;              /* some rank twice or more */
    uint8_t max_suit;            /* most cards of one suit */
    uint8_t monotone;            /* three or more cards, one suit */
    uint8_t flush_possible;      /* a suit with three cards */
    uint8_t straight_windows;    /* five-rank windows holding 3+ board ranks */
    /** Top rank of the highest straight any two cards make, or -1. */
    int8_t nut_straight_top;
    /** Ranks that straight needs from the hole (bit r), 0 when none. */
    uint16_t nut_straight_need;
    /** Per suit: the highest card of that suit not on the board. */
    int8_t nut_flush_card[4];
    /** Best high value any two unseen cards make with three board cards. */
    uint32_t nut_value;
} pe_hf_board_t;

/**
 * Prepare a board. `board` holds 0, 3, 4 or 5 cards; with 0 only private
 * structure is available.
 *
 * @return PE_SOLVER_OK, PE_SOLVER_ERR_NULL_ARGUMENT, or
 *         PE_SOLVER_ERR_INVALID_CONFIG for any other card count.
 */
pe_solver_status_t pe_hf_board_prepare(mask_t board, pe_hf_board_t *out);

/* ------------------------------------------------------------------ *
 * Hand features
 * ------------------------------------------------------------------ */

typedef struct pe_hand_features_t {
    /* Private structure: the hole cards alone. */
    uint8_t hole_count;
    uint8_t distinct_ranks;
    uint8_t highest_rank;
    uint8_t broadway_count;      /* ten through ace */
    uint8_t paired_ranks;        /* ranks held exactly twice */
    uint8_t trips_ranks;         /* ranks held three or more times */
    uint8_t rank_gaps;           /* missing ranks in the tightest span */
    uint8_t rank_components;     /* runs of consecutive ranks, ace high */
    uint8_t longest_run;         /* longest run, the ace counted both ways */
    uint8_t suit_shape[4];       /* cards per suit, descending */
    uint8_t suited_groups;       /* suits holding two or more cards */
    uint8_t connectivity;        /* pe_hf_connectivity_t */

    /* Evaluated: meaningful only when `evaluated` is set. */
    uint8_t evaluated;
    uint8_t made;                /* pe_hf_made_t */
    uint8_t detail;              /* pe_hf_detail_t */
    uint8_t is_nuts;             /* made value equals the board's nuts */
    uint8_t nut_flush;           /* made flush holding its suit's nut card */
    uint8_t flush_draw;          /* pe_hf_flush_draw_t, best over suits */
    uint8_t double_flush_draw;   /* flush draws (not backdoor) in two suits */
    uint8_t straight_draw;       /* pe_hf_straight_draw_t */
    uint8_t straight_outs;       /* unseen cards that complete it */
    uint8_t straight_draw_to_nuts; /* some out makes the nut straight */
    uint8_t combo_draw;          /* flush draw and an open-ended+ straight draw */
    uint8_t redraw;              /* made straight still drawing to a flush or
                                    a higher straight */
    uint8_t blockers;            /* PE_HF_BLOCKS_* */
    uint32_t made_value;         /* HandVal of the best two-plus-three hand */
} pe_hand_features_t;

/**
 * Classify one hand. `hole` holds four to six cards, none on the board.
 *
 * @return PE_SOLVER_OK, PE_SOLVER_ERR_NULL_ARGUMENT, or
 *         PE_SOLVER_ERR_INVALID_CONFIG for a bad hand.
 */
pe_solver_status_t pe_hand_features_compute(const pe_hf_board_t *board,
                                            mask_t hole,
                                            pe_hand_features_t *out);

/**
 * Classify `count` hands into `out`. Stops at the first invalid hand and
 * reports its index through `out_failed` (may be NULL).
 */
pe_solver_status_t pe_hand_features_compute_batch(const pe_hf_board_t *board,
                                                  const mask_t *holes,
                                                  size_t count,
                                                  pe_hand_features_t *out,
                                                  size_t *out_failed);

/* ------------------------------------------------------------------ *
 * Bucket keys
 * ------------------------------------------------------------------ */

/**
 * Feature dimensions a bucket can be keyed on. Any combination is a valid
 * bucket definition; the key records which dimensions it holds, so keys built
 * from different combinations never collide.
 */
typedef enum {
    PE_HF_DIM_MADE = 1u << 0,
    PE_HF_DIM_DETAIL = 1u << 1,
    PE_HF_DIM_NUTS = 1u << 2,
    PE_HF_DIM_FLUSH_DRAW = 1u << 3,
    PE_HF_DIM_STRAIGHT_DRAW = 1u << 4,
    PE_HF_DIM_COMBO_DRAW = 1u << 5,
    PE_HF_DIM_REDRAW = 1u << 6,
    PE_HF_DIM_BLOCKERS = 1u << 7,
    PE_HF_DIM_CONNECTIVITY = 1u << 8,
    PE_HF_DIM_SUITS = 1u << 9,
    PE_HF_DIM_PAIRS = 1u << 10,
    PE_HF_DIM_ALL = (1u << 11) - 1u
} pe_hf_dim_t;

/** Predefined bucket definitions. */
#define PE_HF_BUCKET_MADE (PE_HF_DIM_MADE | PE_HF_DIM_DETAIL)
#define PE_HF_BUCKET_DRAWS \
    (PE_HF_DIM_FLUSH_DRAW | PE_HF_DIM_STRAIGHT_DRAW | PE_HF_DIM_COMBO_DRAW)
#define PE_HF_BUCKET_MADE_AND_DRAWS \
    (PE_HF_DIM_MADE | PE_HF_DIM_FLUSH_DRAW | PE_HF_DIM_STRAIGHT_DRAW)
#define PE_HF_BUCKET_STRUCTURE \
    (PE_HF_DIM_PAIRS | PE_HF_DIM_CONNECTIVITY | PE_HF_DIM_SUITS)

/**
 * The bucket key of `features` over the dimensions in `dims`. Deterministic
 * and comparable: sorting keys groups identical feature combinations, and two
 * hands share a key exactly when they agree on every selected dimension.
 * Evaluated dimensions read as zero when the features were not evaluated.
 */
uint64_t pe_hf_key(const pe_hand_features_t *features, uint32_t dims);

/** The dimensions a key was built over. */
uint32_t pe_hf_key_dims(uint64_t key);

/** Whether `features` falls in the bucket `key` (the key's own dimensions). */
int pe_hf_key_matches(const pe_hand_features_t *features, uint64_t key);

/**
 * Write the key as "dimension=value" pairs joined by commas, e.g.
 * "made=two_pair,flush_draw=nut,suits=2-2-1". Behaves like snprintf: returns
 * the length the full text needs, and writes at most `size` bytes including
 * the terminator.
 */
size_t pe_hf_key_format(uint64_t key, char *buf, size_t size);

/* ------------------------------------------------------------------ *
 * Strategy aggregation
 * ------------------------------------------------------------------ */

#define PE_HF_MAX_ACTIONS 16u

/** One combo of a strategy table. */
typedef struct pe_strategy_row_t {
    mask_t hand;
    double weight;            /* range weight (reach), >= 0 */
    const double *freq;       /* action_count frequencies */
    const double *ev;         /* action_count EVs, or NULL when unknown */
} pe_strategy_row_t;

typedef struct pe_strategy_bucket_t {
    uint64_t key;
    size_t combos;
    double weight;            /* summed range weight */
    double weight_share;      /* weight / total weight of the table */
    double freq[PE_HF_MAX_ACTIONS]; /* weight-averaged action frequencies */
    size_t ev_combos;         /* combos that carried EVs */
    double ev_weight;         /* their summed weight */
    double ev[PE_HF_MAX_ACTIONS];   /* weight-averaged EV per action */
    /** Weight-averaged ev[a] minus the combo's best action EV (<= 0). */
    double ev_delta[PE_HF_MAX_ACTIONS];
} pe_strategy_bucket_t;

/**
 * Group a strategy table by bucket key over `dims`.
 *
 * Each row is classified on `board`, and its combo count, weight, frequencies
 * and, when present, EVs are accumulated into the bucket of its key. Averages
 * are weighted by range weight; a bucket whose weight is zero keeps zero
 * averages. `buckets` is caller-owned; on success `*out_count` buckets are
 * written, sorted by key, so the output is the same for any row order.
 *
 * @return PE_SOLVER_OK;
 *         PE_SOLVER_ERR_NULL_ARGUMENT;
 *         PE_SOLVER_ERR_INVALID_CONFIG for an invalid hand, a negative or
 *         non-finite weight, frequency or EV, or an action count of 0 or above
 *         PE_HF_MAX_ACTIONS;
 *         PE_SOLVER_ERR_BUDGET_EXCEEDED when more than `capacity` buckets
 *         are needed.
 */
pe_solver_status_t pe_strategy_bucket_aggregate(const pe_hf_board_t *board,
                                                const pe_strategy_row_t *rows,
                                                size_t row_count,
                                                size_t action_count,
                                                uint32_t dims,
                                                pe_strategy_bucket_t *buckets,
                                                size_t capacity,
                                                size_t *out_count);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_HAND_FEATURES_H */
