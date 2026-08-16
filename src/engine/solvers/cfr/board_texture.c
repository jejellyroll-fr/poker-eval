/*
 * board_texture.c - Multi-street board texture categorizer (FEAT-13)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * Implements pe_board_analyze() and the texture-id helper declared in
 * board_texture.h. The analyser is a generalisation of the 3-card flop
 * analyser (equity/flop_equity.c) to boards of any size so the same texture
 * abstraction can drive flop, turn and river node merging. All scoring is
 * deterministic and allocation-free so it is safe on the CFR hot path.
 */

#include <poker_eval/engine/solvers/cfr/board_texture.h>

#include <assert.h>
#include <string.h>

#define PE_MAX_FLUSH_DRAW_SUIT 4

/* ------------------------------------------------------------------ *
 * Names
 * ------------------------------------------------------------------ */

const char *pe_texture_filter_name(pe_texture_filter_level_t level)
{
    switch (level)
    {
    case PE_TEXTURE_FILTER_NONE:    return "None";
    case PE_TEXTURE_FILTER_SMALL:   return "Small";
    case PE_TEXTURE_FILTER_MEDIUM:  return "Medium";
    case PE_TEXTURE_FILTER_LARGE:   return "Large";
    case PE_TEXTURE_FILTER_PERFECT: return "Perfect";
    default:                        return "Unknown";
    }
}

const char *pe_board_texture_class_name(pe_board_texture_class_t cls)
{
    switch (cls)
    {
    case PE_BOARD_TEXTURE_DRY:     return "Dry";
    case PE_BOARD_TEXTURE_WET:     return "Wet";
    case PE_BOARD_TEXTURE_PAIRED:  return "Paired";
    case PE_BOARD_TEXTURE_MONOTONE:return "Monotone";
    default:                       return "Unknown";
    }
}

double pe_board_texture_density(pe_texture_filter_level_t level)
{
    switch (level)
    {
    case PE_TEXTURE_FILTER_PERFECT: return 1.0;
    case PE_TEXTURE_FILTER_LARGE:   return 0.75;
    case PE_TEXTURE_FILTER_MEDIUM:  return 0.5;
    case PE_TEXTURE_FILTER_SMALL:   return 0.25;
    case PE_TEXTURE_FILTER_NONE:    return 0.0;
    default:                        return 0.0;
    }
}

/* ------------------------------------------------------------------ *
 * Core analysis
 * ------------------------------------------------------------------ */

/* Poker strength of a rank index (0=A .. 12=2): A is highest. */
static int pe_rank_strength(int r)
{
    return (r == 0) ? 13 : 13 - r;
}

static int pe_rank_desc(const void *a, const void *b)
{
    int sa = pe_rank_strength(*(const int *)a);
    int sb = pe_rank_strength(*(const int *)b);
    return sb - sa;
}

int pe_board_analyze(mask_t board,
                     pe_texture_filter_level_t filter_level,
                     pe_board_texture_t *out)
{
    if (!out)
        return -1;
    memset(out, 0, sizeof(*out));

    int cards[5];
    int n = 0;
    for (int c = 0; c < MODERN_DECK_SIZE && n < 5; ++c)
    {
        if (mask_is_set(board, c))
            cards[n++] = c;
    }
    if (n < 3 || n > 5)
        return -1;

    out->n_cards = n;
    out->filter_level = filter_level;

    /* Suit structure. */
    for (int i = 0; i < n; ++i)
        out->suit_counts[MODERN_GET_SUIT(cards[i])]++;
    out->max_suit_count = 0;
    out->n_suits = 0;
    for (int s = 0; s < 4; ++s)
    {
        if (out->suit_counts[s] > out->max_suit_count)
            out->max_suit_count = out->suit_counts[s];
        if (out->suit_counts[s] > 0)
            out->n_suits++;
    }
    out->is_monotone = (out->max_suit_count == n);
    out->is_rainbow  = (out->n_suits == n);
    out->is_two_tone = (out->n_suits == 2);

    /* Rank structure. */
    int distinct[5];
    int nd = 0;
    for (int r = 0; r < 13; ++r)
    {
        int cnt = 0;
        for (int i = 0; i < n; ++i)
            if (MODERN_GET_RANK(cards[i]) == r)
                cnt++;
        out->rank_counts[r] = cnt;
        if (cnt > 0)
        {
            distinct[nd++] = r;
            if (cnt >= 2)
            {
                out->is_paired = true;
                out->paired_rank = r;
            }
            if (cnt >= 3)
                out->is_trips = true;
        }
    }
    out->n_ranks = nd;
    /* Sort distinct ranks descending. */
    qsort(distinct, (size_t)nd, sizeof(int), pe_rank_desc);
    for (int i = 0; i < nd; ++i)
        out->ranks[i] = distinct[i];
    for (int i = nd; i < 5; ++i)
        out->ranks[i] = -1;
    out->high_card_rank = nd > 0 ? distinct[0] : -1;
    out->low_card_rank  = nd > 0 ? distinct[nd - 1] : -1;

    /* Connectivity (gaps between adjacent distinct ranks, by poker strength so
     * that A(0) and K(1) are adjacent). A negative gap means the two ranks are
     * themselves consecutive, so it does not widen the straight gap. */
    out->max_gap = 0;
    for (int i = 0; i + 1 < nd; ++i)
    {
        int gap = pe_rank_strength(distinct[i]) - pe_rank_strength(distinct[i + 1]) - 1;
        if (gap > out->max_gap)
            out->max_gap = gap;
    }
    out->is_connected = (nd >= 2) && (out->max_gap == 0);

    /* Paired/trips boards have no clean straight scale; treat as not connected. */
    if (out->is_paired)
    {
        out->is_connected = false;
        out->max_gap = 13; /* sentinel: no straight scale */
    }

    /* Broadway / low counts (T..A and 2..6). A has rank 0 here, so the
     * broadway test is r >= 9 OR r == 0; the low test is r >= 8 (2..6). */
    for (int i = 0; i < n; ++i)
    {
        int r = MODERN_GET_RANK(cards[i]);
        if (r >= 9 || r == 0)
            out->n_broadway++;
        if (r >= 8)
            out->n_low_cards++;
    }

    /* Draw potential. */
    out->has_flush_draw = (out->max_suit_count == PE_MAX_FLUSH_DRAW_SUIT) && !out->is_monotone;
    out->flush_draw_outs = out->has_flush_draw ? (out->n_cards == 3 ? 4 : out->n_cards == 4 ? 3 : 0)
                                               : 0;
    /* A straight draw exists when 4+ distinct ranks leave exactly one gap of 1. */
    if (!out->is_paired && nd >= 2)
    {
        int gaps_eq_one = 0;
        int gaps_gt_one = 0;
        for (int i = 0; i + 1 < nd; ++i)
        {
            int g = pe_rank_strength(distinct[i]) - pe_rank_strength(distinct[i + 1]) - 1;
            if (g == 1)
                gaps_eq_one++;
            else if (g > 1)
                gaps_gt_one++;
        }
        out->has_straight_draw = (gaps_gt_one == 0) && (gaps_eq_one >= 1);
    }

    /* Coarse classification. */
    if (out->is_monotone)
        out->texture_class = PE_BOARD_TEXTURE_MONOTONE;
    else if (out->is_paired)
        out->texture_class = PE_BOARD_TEXTURE_PAIRED;
    else if (out->is_connected || out->has_flush_draw || out->has_straight_draw ||
             out->max_gap <= 1 || out->n_broadway >= 2)
        out->texture_class = PE_BOARD_TEXTURE_WET;
    else
        out->texture_class = PE_BOARD_TEXTURE_DRY;

    /* Texture score: 0 (dry) .. 100 (wet). */
    {
        int score = 0;
        if (out->is_monotone)      score += 60;
        else if (out->is_two_tone) score += 30;
        if (out->is_paired)        score += 15;
        if (out->is_connected)     score += 25;
        else                       score += (int)((12 - out->max_gap) * 2.0);
        if (out->has_flush_draw)   score += 20;
        if (out->has_straight_draw)score += 15;
        if (score > 100) score = 100;
        if (score < 0)   score = 0;
        out->texture_score = score;
    }

    out->density = pe_board_texture_density(filter_level);

    return 0;
}

pe_board_texture_class_t pe_board_texture_classify(const pe_board_texture_t *b)
{
    if (!b)
        return PE_BOARD_TEXTURE_COUNT;
    return b->texture_class;
}

/* ------------------------------------------------------------------ *
 * Texture-merged abstraction id
 * ------------------------------------------------------------------ */

uint64_t pe_board_texture_id(mask_t board, pe_texture_filter_level_t level)
{
    if (level == PE_TEXTURE_FILTER_PERFECT || level == PE_TEXTURE_FILTER_NONE)
        return (uint64_t)board;

    pe_board_texture_t b;
    if (pe_board_analyze(board, level, &b) != 0)
        return (uint64_t)board;

    uint64_t id = 0;
    switch (level)
    {
    case PE_TEXTURE_FILTER_LARGE:
        /* coarse texture class (0..3) + suit count (2..4 -> 0..2). Compact so
         * the id fits the 8-bit texture field of an infoset key. */
        id = ((uint64_t)b.texture_class << 2) |
             ((uint64_t)(b.n_suits > 2 ? b.n_suits - 2 : 0) & 0x3);
        break;
    case PE_TEXTURE_FILTER_MEDIUM:
        /* wet/dry axis + pairedness, 2 bits. */
        id = ((uint64_t)(b.texture_class == PE_BOARD_TEXTURE_WET ? 1 : 0) << 1) |
             ((uint64_t)(b.is_paired ? 1 : 0));
        break;
    case PE_TEXTURE_FILTER_SMALL:
        /* wet/dry axis only, 1 bit. */
        id = (uint64_t)(b.texture_class == PE_BOARD_TEXTURE_WET ? 1 : 0);
        break;
    default:
        return (uint64_t)board;
    }
    return id;
}
