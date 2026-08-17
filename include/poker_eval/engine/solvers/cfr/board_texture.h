/*
 * board_texture.h - Multi-street board texture categorizer (FEAT-13)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * Issue #149 asks for a board-texture abstraction layer that mirrors
 * MonkerSolver's node abstraction settings: a Strength Buckets count paired
 * with a Texture Filter level (Perfect / Large / Medium / Small / None). This
 * header defines the texture half of that pair.
 *
 * The analyser generalises the existing 3-card flop analyser
 * (poker_eval/equity/flop_equity.h) to boards of any size (3 = flop, 4 = turn,
 * 5 = river) so the same categorisation can be reused street-by-street. It
 * reports the low-level structural properties (paired / monotone / two-tone /
 * rainbow / connected / high-card) and folds them into a coarse texture filter
 * level that downstream code uses to decide how aggressively to merge streets.
 *
 *   - PE_TEXTURE_FILTER_PERFECT : no merging, every distinct board texture is
 *                                kept separate (full granularity).
 *   - PE_TEXTURE_FILTER_LARGE   : only merge boards that share a coarse texture
 *                                class (dry / wet / paired / monotone ...).
 *   - PE_TEXTURE_FILTER_MEDIUM  : merge on a single "wet vs dry" axis plus
 *                                pairedness, collapsing most of the state space.
 *   - PE_TEXTURE_FILTER_SMALL   : merge on the wet/dry axis only.
 *   - PE_TEXTURE_FILTER_NONE    : texture is ignored for abstraction.
 *
 * The level is exposed as both a stable enum and a density estimate
 * (pe_board_texture_density) in [0,1] so the abstraction engine can translate a
 * filter level into a target number of texture buckets per street.
 */

#ifndef POKER_EVAL_BOARD_TEXTURE_H
#define POKER_EVAL_BOARD_TEXTURE_H

#include <poker_eval/core/modern_cardmask.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ *
 * Texture filter levels (MonkerSolver-style node abstraction)
 * ------------------------------------------------------------------ */

typedef enum pe_texture_filter_level_e
{
    PE_TEXTURE_FILTER_NONE = 0,    /* texture abstraction disabled */
    PE_TEXTURE_FILTER_SMALL,       /* wet/dry axis only */
    PE_TEXTURE_FILTER_MEDIUM,      /* wet/dry + pairedness */
    PE_TEXTURE_FILTER_LARGE,       /* coarse texture class */
    PE_TEXTURE_FILTER_PERFECT,     /* no merging (full granularity) */
    PE_TEXTURE_FILTER_COUNT
} pe_texture_filter_level_t;

/* Coarse texture classes shared across every street. */
typedef enum pe_board_texture_class_e
{
    PE_BOARD_TEXTURE_DRY = 0,      /* disconnected, rainbow, no pair */
    PE_BOARD_TEXTURE_WET,          /* connected and/or multi-suited draws */
    PE_BOARD_TEXTURE_PAIRED,       /* at least one pair on board */
    PE_BOARD_TEXTURE_MONOTONE,     /* all one suit */
    PE_BOARD_TEXTURE_COUNT
} pe_board_texture_class_t;

/* Detailed structural analysis of a board of any size (3..5 cards). */
typedef struct pe_board_texture_s
{
    int n_cards;             /* number of board cards (3, 4 or 5) */

    /* Suit structure */
    int suit_counts[4];      /* cards per suit */
    int max_suit_count;      /* largest suit_counts entry */
    int n_suits;             /* distinct suits present */
    bool is_monotone;        /* every card the same suit */
    bool is_two_tone;        /* exactly two suits present */
    bool is_rainbow;         /* every card a different suit */

    /* Rank structure */
    int ranks[5];            /* distinct ranks, descending, then -1 */
    int n_ranks;             /* distinct ranks present */
    int rank_counts[13];     /* occurrences per rank (0..n_cards) */
    bool is_paired;          /* any rank appears >= 2 times */
    bool is_trips;           /* any rank appears >= 3 times */
    int paired_rank;         /* rank of a pair/trips, -1 if none */
    int high_card_rank;      /* highest rank index (0=A .. 12=2) */
    int low_card_rank;       /* lowest rank index (0=A .. 12=2) */

    /* Connectivity */
    int max_gap;             /* largest gap between adjacent distinct ranks */
    bool is_connected;       /* max_gap == 0 (run of adjacent ranks) */
    int n_broadway;          /* number of T..A cards */
    int n_low_cards;         /* number of 2..6 cards */

    /* Draw potential (turn/river aware) */
    int flush_draw_outs;     /* cards that complete a flush given max_suit_count */
    bool has_flush_draw;     /* max_suit_count == 4 (and not already a flush) */
    bool has_straight_draw;  /* a straight is reachable with one more card */

    /* Coarse classification + filter level */
    pe_board_texture_class_t texture_class;
    pe_texture_filter_level_t filter_level; /* level this board was analysed at */
    int texture_score;       /* 0 (dry) .. 100 (wet), higher = wetter */
    double density;          /* estimated abstraction density in [0,1] */
} pe_board_texture_t;

/**
 * Analyse a board of 3, 4 or 5 cards.
 *
 * @param board        Board cards (exactly 3, 4 or 5 set bits)
 * @param filter_level Texture filter level driving the coarse classification
 * @param out          Receives the analysis (must not be NULL)
 * @return 0 on success, -1 on invalid input (wrong card count / NULL out)
 */
int pe_board_analyze(mask_t board,
                     pe_texture_filter_level_t filter_level,
                     pe_board_texture_t *out);

/**
 * Coarse texture class, independent of the filter level (so callers can build
 * their own merging policy). Returns PE_BOARD_TEXTURE_COUNT on bad input.
 */
pe_board_texture_class_t pe_board_texture_classify(const pe_board_texture_t *b);

/**
 * Map a filter level to an estimated per-street abstraction density in [0,1].
 * PERFECT => 1.0 (no merging), NONE => 0.0 (texture ignored). The intermediate
 * levels are monotonic: PERFECT > LARGE > MEDIUM > SMALL > NONE.
 */
double pe_board_texture_density(pe_texture_filter_level_t level);

/**
 * Human-readable name for a filter level ("Perfect", "Large", ...).
 * Returns a static string (do not free).
 */
const char *pe_texture_filter_name(pe_texture_filter_level_t level);

/**
 * Human-readable name for a texture class ("Dry", "Wet", ...).
 * Returns a static string (do not free).
 */
const char *pe_board_texture_class_name(pe_board_texture_class_t cls);

/**
 * Compute the texture-merged abstraction id for a board under a filter level.
 *
 * Two boards collide to the same id (and therefore the same abstract node)
 * when they are indistinguishable under the given level:
 *   - PERFECT/NONE : the raw board mask itself (no merging).
 *   - LARGE        : coarse texture class + paired rank + high-card rank.
 *   - MEDIUM       : wet/dry axis + pairedness only.
 *   - SMALL        : wet/dry axis only.
 *
 * The returned id is stable and cheap to compute; it is meant to fold into a
 * CFR infoset key so that texture-differentiated boards share a node when the
 * solver's node abstraction requests coarser granularity.
 */
uint64_t pe_board_texture_id(mask_t board,
                             pe_texture_filter_level_t level);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_BOARD_TEXTURE_H */
