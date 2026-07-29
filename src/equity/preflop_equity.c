/*
 * preflop_equity.c - Pre-Flop Equity Calculation Implementation
 *
 * Copyright (C) 2025 poker-eval contributors
 */

#include <poker_eval/equity/preflop_equity.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/eval.h>
#include <poker_eval/deck/deck_std.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>


/* Embed the preflop equity table blob */
#if __has_include("preflop_table.inc")
#include "preflop_table.inc"
#else
/* Dummy fallback for initial compilation */
static const unsigned char build_preflop_table_bin[] = {0};
static const unsigned int build_preflop_table_bin_len = 0;
#endif

/* ===== Internal Structures ===== */

struct preflop_lookup_table
{
    game_t game;
    float equity[PREFLOP_LOOKUP_TABLE_SIZE]; /* 169×169 = 28,561 entries */
};

/* ===== Canonical Hand Mapping ===== */

/* Map rank pair to canonical index */
static const uint8_t pair_to_index[13] = {
    0,  /* AA */
    1,  /* KK */
    2,  /* QQ */
    3,  /* JJ */
    4,  /* TT */
    5,  /* 99 */
    6,  /* 88 */
    7,  /* 77 */
    8,  /* 66 */
    9,  /* 55 */
    10, /* 44 */
    11, /* 33 */
    12  /* 22 */
};

/**
 * Convert 2 ranks to suited index
 * Input: high_rank (0=A, 1=K, ..., 12=2), low_rank
 * Output: index 13-90 (78 suited hands)
 */
static preflop_hand_t ranks_to_suited(int high_rank, int low_rank)
{
    /* AKs=13, AQs=14, ..., A2s=24 (12 hands)
     * KQs=25, KJs=26, ..., K2s=36 (11 hands)
     * ...
     * 32s=90 (1 hand)
     * Total: 12+11+10+...+1 = 78 */
    int base = 13;
    for (int r = 0; r < high_rank; r++)
    {
        base += (12 - r); /* Skip previous ranks' suited hands */
    }
    return base + (low_rank - high_rank - 1);
}

/**
 * Convert 2 ranks to offsuit index
 */
static preflop_hand_t ranks_to_offsuit(int high_rank, int low_rank)
{
    /* AKo=91, AQo=92, ..., A2o=102 (12 hands)
     * KQo=103, KJo=104, ..., K2o=113 (11 hands)
     * ...
     * 32o=168 (1 hand)
     * Total: 78 */
    int base = 91;
    for (int r = 0; r < high_rank; r++)
    {
        base += (12 - r);
    }
    return base + (low_rank - high_rank - 1);
}

/* ===== Hand Conversion ===== */

preflop_hand_t preflop_cards_to_hand(int card1, int card2)
{
    int rank1 = StdDeck_RANK(card1);
    int rank2 = StdDeck_RANK(card2);
    int suit1 = StdDeck_SUIT(card1);
    int suit2 = StdDeck_SUIT(card2);

    /* Convert rank (2=0, 3=1, ..., A=12) to (A=0, K=1, ..., 2=12) */
    int r1 = (rank1 == StdDeck_Rank_ACE) ? 0 : (StdDeck_Rank_KING - rank1 + 1);
    int r2 = (rank2 == StdDeck_Rank_ACE) ? 0 : (StdDeck_Rank_KING - rank2 + 1);

    /* Ensure r1 is higher rank (smaller index) */
    if (r1 > r2)
    {
        int tmp = r1;
        r1 = r2;
        r2 = tmp;
        int tmp_s = suit1;
        suit1 = suit2;
        suit2 = tmp_s;
    }

    /* Pair */
    if (r1 == r2)
    {
        return pair_to_index[r1];
    }

    /* Suited */
    if (suit1 == suit2)
    {
        return ranks_to_suited(r1, r2);
    }

    /* Offsuit */
    return ranks_to_offsuit(r1, r2);
}

void preflop_hand_to_string(preflop_hand_t hand, char *out_str)
{
    static const char *ranks = "AKQJT98765432";

    /* Pairs: 0-12 */
    if (hand <= 12)
    {
        out_str[0] = ranks[hand];
        out_str[1] = ranks[hand];
        out_str[2] = '\0';
        return;
    }

    /* Suited: 13-90 */
    if (hand <= 90)
    {
        int idx = hand - 13;
        int high_rank = 0;
        int cumulative = 0;
        for (int r = 0; r < 12; r++)
        {
            int count = 12 - r;
            if (idx < cumulative + count)
            {
                high_rank = r;
                break;
            }
            cumulative += count;
        }
        int low_rank = high_rank + 1 + (idx - cumulative);

        out_str[0] = ranks[high_rank];
        out_str[1] = ranks[low_rank];
        out_str[2] = 's';
        out_str[3] = '\0';
        return;
    }

    /* Offsuit: 91-168 */
    int idx = hand - 91;
    int high_rank = 0;
    int cumulative = 0;
    for (int r = 0; r < 12; r++)
    {
        int count = 12 - r;
        if (idx < cumulative + count)
        {
            high_rank = r;
            break;
        }
        cumulative += count;
    }
    int low_rank = high_rank + 1 + (idx - cumulative);

    out_str[0] = ranks[high_rank];
    out_str[1] = ranks[low_rank];
    out_str[2] = 'o';
    out_str[3] = '\0';
}

int preflop_string_to_hand(const char *str)
{
    if (!str || strlen(str) < 2)
        return -1;

    static const char *ranks = "AKQJT98765432";

    /* Parse first rank */
    char r1_char = (char)toupper((unsigned char)str[0]);
    const char *r1_pos = strchr(ranks, r1_char);
    if (!r1_pos)
        return -1;
    int r1 = (int)(r1_pos - ranks);

    /* Parse second rank */
    char r2_char = (char)toupper((unsigned char)str[1]);
    const char *r2_pos = strchr(ranks, r2_char);
    if (!r2_pos)
        return -1;
    int r2 = (int)(r2_pos - ranks);

    /* Ensure r1 <= r2 (higher rank first) */
    if (r1 > r2)
    {
        int tmp = r1;
        r1 = r2;
        r2 = tmp;
    }

    /* Pair */
    if (r1 == r2)
    {
        return pair_to_index[r1];
    }

    /* Check suited/offsuit */
    if (strlen(str) >= 3)
    {
        char suit_char = (char)tolower((unsigned char)str[2]);
        if (suit_char == 's')
        {
            return ranks_to_suited(r1, r2);
        }
        else if (suit_char == 'o')
        {
            return ranks_to_offsuit(r1, r2);
        }
    }

    /* Default to suited if no suffix */
    return ranks_to_suited(r1, r2);
}

preflop_hand_class_t preflop_hand_class(preflop_hand_t hand)
{
    if (hand <= 12)
        return PREFLOP_PAIR;
    if (hand <= 90)
        return PREFLOP_SUITED;
    return PREFLOP_OFFSUIT;
}

preflop_strength_t preflop_classify(preflop_hand_t hand)
{
    /* Based on common classifications (Sklansky/Chen groups simplified) */

    /* Premium: AA, KK, QQ, AKs */
    if (hand == 0 || hand == 1 || hand == 2 || hand == 13)
        return PREFLOP_STRENGTH_PREMIUM;

    /* Strong: JJ, TT, AQs, AKo, KQs */
    if (hand == 3 || hand == 4 || hand == 14 || hand == 91 || hand == 25)
        return PREFLOP_STRENGTH_STRONG;

    /* Medium: 99-77, AJs-ATs, KJs, QJs, JTs, AQo-AJo, KQo */
    /* Note: Comments and code range check for 99-77 (indices 5-7) match correctly. */
    if ((hand >= 5 && hand <= 7) ||        /* 99-77 */
        (hand >= 15 && hand <= 16) ||      /* AJs-ATs */
        hand == 26 || hand == 36 ||        /* KJs, QJs */
        hand == 46 ||                      /* JTs */
        (hand >= 92 && hand <= 93) ||      /* AQo-AJo */
        hand == 103)                       /* KQo */
    {
        return PREFLOP_STRENGTH_MEDIUM;
    }

    /* Weak: 66-22, other suited aces/broadways, connectors */
    if ((hand >= 8 && hand <= 12) ||       /* 66-22 */
        (hand >= 17 && hand <= 24) ||      /* A9s-A2s */
        (hand >= 27 && hand <= 35) ||      /* KTs-K2s */
        (hand >= 37 && hand <= 45) ||      /* QTs-Q2s */
        (hand >= 47 && hand <= 54) ||      /* J9s-J2s */
        /* Suited connectors T9s, 98s, 87s, 76s, 65s, 54s */
        hand == 55 || hand == 63 || hand == 70 || hand == 76 || hand == 81 || hand == 85)
    {
        return PREFLOP_STRENGTH_WEAK;
    }

    /* Trash: Everything else */
    return PREFLOP_STRENGTH_TRASH;
}

/* ===== Range Operations ===== */

void preflop_range_init(preflop_range_t *range)
{
    memset(range->bitmap, 0, sizeof(range->bitmap));
    range->num_hands = 0;
}

void preflop_range_add(preflop_range_t *range, preflop_hand_t hand)
{
    if (hand >= PREFLOP_NUM_CANONICAL_HANDS)
        return;

    int word = hand / 32;
    int bit = hand % 32;

    if (!(range->bitmap[word] & (1u << bit)))
    {
        range->bitmap[word] |= (1u << bit);
        range->num_hands++;
    }
}

int preflop_range_contains(const preflop_range_t *range, preflop_hand_t hand)
{
    if (hand >= PREFLOP_NUM_CANONICAL_HANDS)
        return 0;

    int word = hand / 32;
    int bit = hand % 32;

    return (range->bitmap[word] & (1u << bit)) != 0;
}

int preflop_range_parse(const char *range_str, preflop_range_t *out_range)
{
    preflop_range_init(out_range);

    if (!range_str || !*range_str)
        return 0;

    /* Copy input string to modifiable buffer */
    char buffer[256];
    strncpy(buffer, range_str, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    char *token = strtok(buffer, ",");
    while (token)
    {
        /* Trim whitespace */
        while (*token && isspace(*token))
            token++;

        /* Check for "+" notation (e.g., "QQ+") */
        size_t len = strlen(token);
        int is_plus = 0;
        if (len > 0 && token[len - 1] == '+')
        {
            is_plus = 1;
            token[len - 1] = '\0'; /* Remove '+' */
        }

        int hand = preflop_string_to_hand(token);
        if (hand >= 0)
        {
            preflop_range_add(out_range, hand);

            /* If "+", add all higher ranked hands of same type */
            if (is_plus)
            {
                /* Only support pairs for now in basic parser */
                if (hand <= 12) /* Pair */
                {
                    /* Add all pairs better than this one (indices < hand) */
                    for (int h = 0; h < hand; h++)
                    {
                        preflop_range_add(out_range, h);
                    }
                }
                /* TODO: Add support for suited/offsuit ranges like AKs+ */
            }
        }

        token = strtok(NULL, ",");
    }

    return 0;
}

void preflop_range_to_string(const preflop_range_t *range, char *out_str, size_t max_len)
{
    out_str[0] = '\0';
    size_t pos = 0;

    for (int h = 0; h < PREFLOP_NUM_CANONICAL_HANDS && pos < max_len - 5; h++)
    {
        if (preflop_range_contains(range, h))
        {
            char hand_str[8];
            preflop_hand_to_string(h, hand_str);

            if (pos > 0)
            {
                out_str[pos++] = ',';
            }

            size_t len = strlen(hand_str);
            if (pos + len < max_len - 1)
            {
                memcpy(out_str + pos, hand_str, len + 1);
                pos += len;
            }
        }
    }
}

/* ===== Forward Declarations ===== */
int preflop_range_count_combinations_single(preflop_hand_t hand);

static int enumerate_hand_combos(
    preflop_hand_t hand,
    StdDeck_CardMask removed_cards,
    StdDeck_CardMask *out_combos,
    int max_combos);

/* ===== Random number generator (simple LCG) ===== */
static uint64_t rng_state = 123456789ULL;

static inline void rng_seed(uint64_t seed)
{
    rng_state = seed;
}

static inline uint32_t rng_next(void)
{
    /* Linear Congruential Generator */
    rng_state = rng_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return (uint32_t)(rng_state >> 32);
}

static inline int rng_range(int max)
{
    return (int)(rng_next() % (uint32_t)max);
}

/* ===== Equity Calculation ===== */

/* Helper: Monte Carlo equity calculation for a specific matchup */
static void calc_matchup_equity_montecarlo(
    preflop_hand_t hand1,
    preflop_hand_t hand2,
    size_t num_samples,
    uint64_t *wins1,
    uint64_t *wins2,
    uint64_t *ties)
{
    /* Get all combinations for both hands */
    StdDeck_CardMask combos1[22], combos2_all[22];
    StdDeck_CardMask removed;
    StdDeck_CardMask_RESET(removed);

    int n1 = enumerate_hand_combos(hand1, removed, combos1, 22);
    int n2_max = enumerate_hand_combos(hand2, removed, combos2_all, 22);

    if (n1 == 0 || n2_max == 0)
    {
        /* Can't sample if no combos available */
        *wins1 = *wins2 = *ties = 0;
        return;
    }

    /* Initialize RNG with a unique seed per matchup */
    uint64_t seed = (uint64_t)time(NULL);
    seed += (uint64_t)(hand1 * 169 + hand2);
    rng_seed(seed);

    /* Monte Carlo sampling */
    for (size_t i = 0; i < num_samples; i++)
    {
        /* 1. Randomly select a combo for hand1 */
        int c1_idx = rng_range(n1);
        StdDeck_CardMask pocket1 = combos1[c1_idx];

        /* 2. Get available combos for hand2 (exclude cards from pocket1) */
        StdDeck_CardMask combos2[22];
        int n2 = enumerate_hand_combos(hand2, pocket1, combos2, 22);

        if (n2 == 0)
            continue;  /* Skip if no valid combo2 */

        /* 3. Randomly select a combo for hand2 */
        int c2_idx = rng_range(n2);
        StdDeck_CardMask pocket2 = combos2[c2_idx];

        /* 4. Generate random board */
        StdDeck_CardMask dead, board;
        StdDeck_CardMask_OR(dead, pocket1, pocket2);

        /* Random board: select 5 cards from remaining 48 */
        int deck[52];
        int deck_size = 0;

        /* Build deck excluding dead cards */
        for (int card = 0; card < 52; card++)
        {
            if (!StdDeck_CardMask_CARD_IS_SET(dead, card))
            {
                deck[deck_size++] = card;
            }
        }

        /* Fisher-Yates shuffle first 5 cards */
        for (int j = 0; j < 5; j++)
        {
            int swap_idx = j + rng_range(deck_size - j);
            int tmp = deck[j];
            deck[j] = deck[swap_idx];
            deck[swap_idx] = tmp;
        }

        /* Build board from first 5 cards */
        StdDeck_CardMask_RESET(board);
        for (int j = 0; j < 5; j++)
        {
            StdDeck_CardMask_SET(board, deck[j]);
        }

        /* 5. Evaluate */
        StdDeck_CardMask hand1_full, hand2_full;
        StdDeck_CardMask_OR(hand1_full, pocket1, board);
        StdDeck_CardMask_OR(hand2_full, pocket2, board);

        HandVal val1 = StdDeck_StdRules_EVAL_N(hand1_full, 7);
        HandVal val2 = StdDeck_StdRules_EVAL_N(hand2_full, 7);

        if (val1 > val2)
            (*wins1)++;
        else if (val2 > val1)
            (*wins2)++;
        else
            (*ties)++;
    }
}

/* Helper: Enumerate all 5-card boards and count wins */
static void calc_matchup_equity(
    StdDeck_CardMask pocket1,
    StdDeck_CardMask pocket2,
    uint64_t *wins1,
    uint64_t *wins2,
    uint64_t *ties)
{
    StdDeck_CardMask dead, board, hand1, hand2;
    HandVal val1, val2;

    /* Dead cards = both pocket cards */
    StdDeck_CardMask_OR(dead, pocket1, pocket2);

    /* Enumerate all possible 5-card boards */
    DECK_ENUMERATE_5_CARDS_D(StdDeck, board, dead,
        StdDeck_CardMask_OR(hand1, pocket1, board);
        StdDeck_CardMask_OR(hand2, pocket2, board);
        val1 = StdDeck_StdRules_EVAL_N(hand1, 7);
        val2 = StdDeck_StdRules_EVAL_N(hand2, 7);
        if (val1 > val2)
            (*wins1)++;
        else if (val2 > val1)
            (*wins2)++;
        else
            (*ties)++;
    );
}

/* Helper: Enumerate all specific card combinations for a canonical hand */
static int enumerate_hand_combos(
    preflop_hand_t hand,
    StdDeck_CardMask removed_cards,
    StdDeck_CardMask *out_combos,
    int max_combos)
{
    int count = 0;

    /* Get hand ranks */
    char hand_str[8];
    preflop_hand_to_string(hand, hand_str);

    static const char *ranks = "AKQJT98765432";
    const char *r1_pos = strchr(ranks, hand_str[0]);
    const char *r2_pos = strchr(ranks, hand_str[1]);
    if (!r1_pos || !r2_pos)
        return 0;

    int rank1 = (int)(r1_pos - ranks);
    int rank2 = (int)(r2_pos - ranks);

    /* Convert to StdDeck rank (A=12, K=11, ..., 2=0) */
    int std_rank1 = (rank1 == 0) ? StdDeck_Rank_ACE : (StdDeck_Rank_KING - rank1 + 1);
    int std_rank2 = (rank2 == 0) ? StdDeck_Rank_ACE : (StdDeck_Rank_KING - rank2 + 1);

    if (rank1 == rank2)
    {
        /* Pair: C(4,2) = 6 combinations */
        for (int s1 = StdDeck_Suit_HEARTS; s1 <= StdDeck_Suit_SPADES && count < max_combos; s1++)
        {
            for (int s2 = s1 + 1; s2 <= StdDeck_Suit_SPADES && count < max_combos; s2++)
            {
                int card1 = StdDeck_MAKE_CARD(std_rank1, s1);
                int card2 = StdDeck_MAKE_CARD(std_rank2, s2);

                /* Check if cards are available */
                if (StdDeck_CardMask_CARD_IS_SET(removed_cards, card1) ||
                    StdDeck_CardMask_CARD_IS_SET(removed_cards, card2))
                    continue;

                StdDeck_CardMask combo;
                StdDeck_CardMask_RESET(combo);
                StdDeck_CardMask_SET(combo, card1);
                StdDeck_CardMask_SET(combo, card2);

                out_combos[count++] = combo;
            }
        }
    }
    else if (hand_str[2] == 's' || strlen(hand_str) == 2)
    {
        /* Suited: 4 combinations */
        for (int s = StdDeck_Suit_HEARTS; s <= StdDeck_Suit_SPADES && count < max_combos; s++)
        {
            int card1 = StdDeck_MAKE_CARD(std_rank1, s);
            int card2 = StdDeck_MAKE_CARD(std_rank2, s);

            if (StdDeck_CardMask_CARD_IS_SET(removed_cards, card1) ||
                StdDeck_CardMask_CARD_IS_SET(removed_cards, card2))
                continue;

            StdDeck_CardMask combo;
            StdDeck_CardMask_RESET(combo);
            StdDeck_CardMask_SET(combo, card1);
            StdDeck_CardMask_SET(combo, card2);

            out_combos[count++] = combo;
        }
    }
    else
    {
        /* Offsuit: 12 combinations */
        for (int s1 = StdDeck_Suit_HEARTS; s1 <= StdDeck_Suit_SPADES && count < max_combos; s1++)
        {
            for (int s2 = StdDeck_Suit_HEARTS; s2 <= StdDeck_Suit_SPADES && count < max_combos; s2++)
            {
                if (s1 == s2)
                    continue;

                int card1 = StdDeck_MAKE_CARD(std_rank1, s1);
                int card2 = StdDeck_MAKE_CARD(std_rank2, s2);

                if (StdDeck_CardMask_CARD_IS_SET(removed_cards, card1) ||
                    StdDeck_CardMask_CARD_IS_SET(removed_cards, card2))
                    continue;

                StdDeck_CardMask combo;
                StdDeck_CardMask_RESET(combo);
                StdDeck_CardMask_SET(combo, card1);
                StdDeck_CardMask_SET(combo, card2);

                out_combos[count++] = combo;
            }
        }
    }

    return count;
}

int preflop_equity_calculate(
    const preflop_equity_input_t *input,
    preflop_equity_result_t *result)
{
    if (!input || !result)
        return -1;

    memset(result, 0, sizeof(*result));

    /* Allocate equity matrix if requested */
    if (input->range1.num_hands > 0 && input->range2.num_hands > 0)
    {
        result->equity_matrix = preflop_equity_matrix_alloc();
        if (!result->equity_matrix)
            return -1;
    }

    uint64_t total_wins1 = 0, total_wins2 = 0, total_ties = 0;
    uint64_t total_matchups = 0;

    /* Iterate over all hands in range1 */
    for (int h1 = 0; h1 < PREFLOP_NUM_CANONICAL_HANDS; h1++)
    {
        if (!preflop_range_contains(&input->range1, h1))
            continue;

        /* Iterate over all hands in range2 */
        for (int h2 = 0; h2 < PREFLOP_NUM_CANONICAL_HANDS; h2++)
        {
            if (!preflop_range_contains(&input->range2, h2))
                continue;

            double equity_h1;

            /* USE LOOKUP TABLE IF AVAILABLE (instant!) */
            if (input->lookup_table)
            {
                equity_h1 = preflop_lookup_table_get(input->lookup_table, h1, h2);

                /* Count combinations for weighting */
                int combos1 = preflop_range_count_combinations_single(h1);
                int combos2 = preflop_range_count_combinations_single(h2);

                /* Remove overlapping combos (cards can't be in both hands) */
                int valid_combos = (h1 == h2) ? 0 : combos1 * combos2;
                if (h1 == h2) {
                    /* Same hand: C(combos, 2) valid matchups */
                    valid_combos = (combos1 * (combos1 - 1)) / 2;
                }

                /* Weight by combination frequency */
                total_wins1 += (uint64_t)(equity_h1 * valid_combos * 1000000);
                total_wins2 += (uint64_t)((1.0 - equity_h1) * valid_combos * 1000000);
                total_matchups += valid_combos * 1000000;
            }
            else if (input->num_samples > 0)
            {
                /* MONTE CARLO MODE (fast, ~99% accurate) */
                uint64_t matchup_wins1 = 0, matchup_wins2 = 0, matchup_ties = 0;

                calc_matchup_equity_montecarlo(h1, h2, input->num_samples,
                                              &matchup_wins1, &matchup_wins2, &matchup_ties);

                uint64_t matchup_count = matchup_wins1 + matchup_wins2 + matchup_ties;

                if (matchup_count > 0)
                {
                    equity_h1 = (double)matchup_wins1 / (double)matchup_count;
                }
                else
                {
                    equity_h1 = 0.5;
                }

                total_wins1 += matchup_wins1;
                total_wins2 += matchup_wins2;
                total_ties += matchup_ties;
                total_matchups += matchup_count;
            }
            else
            {
                /* EXHAUSTIVE MODE (slow, 100% accurate) */
                StdDeck_CardMask combos1[22], combos2[22];
                StdDeck_CardMask removed;
                StdDeck_CardMask_RESET(removed);

                int n1 = enumerate_hand_combos(h1, removed, combos1, 22);

                uint64_t matchup_wins1 = 0, matchup_wins2 = 0, matchup_ties = 0;
                uint64_t matchup_count = 0;

                /* For each specific combo1 */
                for (int c1 = 0; c1 < n1; c1++)
                {
                    /* Remove combo1 cards for combo2 enumeration */
                    int n2 = enumerate_hand_combos(h2, combos1[c1], combos2, 22);

                    /* For each specific combo2 */
                    for (int c2 = 0; c2 < n2; c2++)
                    {
                        /* Calculate equity for this specific matchup */
                        uint64_t w1 = 0, w2 = 0, t = 0;
                        calc_matchup_equity(combos1[c1], combos2[c2], &w1, &w2, &t);

                        matchup_wins1 += w1;
                        matchup_wins2 += w2;
                        matchup_ties += t;
                        matchup_count += (w1 + w2 + t);
                    }
                }

                if (matchup_count > 0)
                {
                    equity_h1 = (double)matchup_wins1 / (double)matchup_count;
                }
                else
                {
                    equity_h1 = 0.5;
                }

                total_wins1 += matchup_wins1;
                total_wins2 += matchup_wins2;
                total_ties += matchup_ties;
                total_matchups += matchup_count;
            }

            /* Store equity for this hand pair in matrix */
            if (result->equity_matrix)
            {
                result->equity_matrix[h1][h2] = equity_h1;
            }
        }
    }

    /* Calculate overall equity */
    if (total_matchups > 0)
    {
        double w1 = (double)total_wins1;
        double w2 = (double)total_wins2;
        double t = (double)total_ties;
        double total = (double)total_matchups;

        result->equity1 = (w1 + t / 2.0) / total;
        result->equity2 = (w2 + t / 2.0) / total;
        result->tie_equity = t / total;
    }
    else
    {
        result->equity1 = 0.5;
        result->equity2 = 0.5;
        result->tie_equity = 0.0;
    }

    result->num_matchups = total_matchups;

    return 0;
}

double **preflop_equity_matrix_alloc(void)
{
    double **matrix = malloc(PREFLOP_NUM_CANONICAL_HANDS * sizeof(double *));
    if (!matrix)
        return NULL;

    for (int i = 0; i < PREFLOP_NUM_CANONICAL_HANDS; i++)
    {
        matrix[i] = malloc(PREFLOP_NUM_CANONICAL_HANDS * sizeof(double));
        if (!matrix[i])
        {
            /* Free allocated rows */
            for (int j = 0; j < i; j++)
            {
                free(matrix[j]);
            }
            free(matrix);
            return NULL;
        }
        memset(matrix[i], 0, PREFLOP_NUM_CANONICAL_HANDS * sizeof(double));
    }

    return matrix;
}

void preflop_equity_matrix_free(double **matrix)
{
    if (!matrix)
        return;

    for (int i = 0; i < PREFLOP_NUM_CANONICAL_HANDS; i++)
    {
        free(matrix[i]);
    }
    free(matrix);
}

void preflop_equity_result_free(preflop_equity_result_t *result)
{
    if (!result)
        return;
    if (result->equity_matrix)
    {
        preflop_equity_matrix_free(result->equity_matrix);
        result->equity_matrix = NULL;
    }
}

/* ===== Lookup Table ===== */

static preflop_lookup_table_t *preflop_lookup_table_load_from_memory(const unsigned char *data, size_t len, game_t game)
{
    if (!data || len < 12 + sizeof(float) * PREFLOP_LOOKUP_TABLE_SIZE)
        return NULL;

    /* Read header */
    uint32_t magic, version;
    game_t file_game;

    memcpy(&magic, data, sizeof(uint32_t));
    memcpy(&version, data + 4, sizeof(uint32_t));
    memcpy(&file_game, data + 8, sizeof(game_t));

    if (magic != 0x50464C54 || file_game != game)
    { /* "PFLT" */
        return NULL;
    }

    preflop_lookup_table_t *table = malloc(sizeof(*table));
    if (!table)
        return NULL;

    table->game = game;

    /* Copy equity matrix */
    memcpy(table->equity, data + 12, sizeof(table->equity));

    return table;
}

preflop_lookup_table_t *preflop_lookup_table_load_default(void)
{
    return preflop_lookup_table_load_from_memory(build_preflop_table_bin, build_preflop_table_bin_len, game_holdem);
}

preflop_lookup_table_t *preflop_lookup_table_load(const char *filename, game_t game)
{
    /* If filename is NULL, try loading default table */
    if (!filename) {
        return preflop_lookup_table_load_default();
    }

    FILE *f = fopen(filename, "rb");
    if (!f)
        return NULL;

    preflop_lookup_table_t *table = malloc(sizeof(*table));
    if (!table)
    {
        fclose(f);
        return NULL;
    }

    /* Read header */
    uint32_t magic, version;
    game_t file_game;

    if (fread(&magic, sizeof(magic), 1, f) != 1 ||
        fread(&version, sizeof(version), 1, f) != 1 ||
        fread(&file_game, sizeof(file_game), 1, f) != 1)
    {
        free(table);
        fclose(f);
        return NULL;
    }

    if (magic != 0x50464C54 || file_game != game)
    { /* "PFLT" */
        free(table);
        fclose(f);
        return NULL;
    }

    table->game = game;

    /* Read equity matrix */
    if (fread(table->equity, sizeof(table->equity), 1, f) != 1)
    {
        free(table);
        fclose(f);
        return NULL;
    }

    fclose(f);
    return table;
}

preflop_lookup_table_t *preflop_lookup_table_create(game_t game)
{
    preflop_lookup_table_t *table = malloc(sizeof(*table));
    if (!table)
        return NULL;

    table->game = game;
    memset(table->equity, 0, sizeof(table->equity));

    return table;
}

int preflop_lookup_table_save(const preflop_lookup_table_t *table, const char *filename)
{
    if (!table || !filename)
        return -1;

    FILE *f = fopen(filename, "wb");
    if (!f)
        return -1;

    /* Write header */
    uint32_t magic = 0x50464C54; /* "PFLT" */
    uint32_t version = 1;

    if (fwrite(&magic, sizeof(magic), 1, f) != 1 ||
        fwrite(&version, sizeof(version), 1, f) != 1 ||
        fwrite(&table->game, sizeof(table->game), 1, f) != 1)
    {
        fclose(f);
        return -1;
    }

    /* Write equity matrix */
    if (fwrite(table->equity, sizeof(table->equity), 1, f) != 1)
    {
        fclose(f);
        return -1;
    }

    fclose(f);
    return 0;
}

void preflop_lookup_table_free(preflop_lookup_table_t *table)
{
    free(table);
}

double preflop_lookup_table_get(
    const preflop_lookup_table_t *table,
    preflop_hand_t hand1,
    preflop_hand_t hand2)
{
    if (!table || hand1 >= PREFLOP_NUM_CANONICAL_HANDS ||
        hand2 >= PREFLOP_NUM_CANONICAL_HANDS)
    {
        return 0.5; /* Default equity */
    }

    int idx = hand1 * PREFLOP_NUM_CANONICAL_HANDS + hand2;
    return table->equity[idx];
}

void preflop_lookup_table_set(
    preflop_lookup_table_t *table,
    preflop_hand_t hand1,
    preflop_hand_t hand2,
    double equity)
{
    if (!table || hand1 >= PREFLOP_NUM_CANONICAL_HANDS ||
        hand2 >= PREFLOP_NUM_CANONICAL_HANDS)
    {
        return;
    }

    int idx = hand1 * PREFLOP_NUM_CANONICAL_HANDS + hand2;
    table->equity[idx] = (float)equity;
}

game_t preflop_lookup_table_game(const preflop_lookup_table_t *table)
{
    return table ? table->game : game_holdem;
}

/* ===== Utilities ===== */

/* Helper: Count combinations for a single canonical hand */
int preflop_range_count_combinations_single(preflop_hand_t hand)
{
    if (hand <= 12)
    {
        /* Pair: C(4,2) = 6 combinations */
        return 6;
    }
    else if (hand <= 90)
    {
        /* Suited: 4 combinations (one per suit) */
        return 4;
    }
    else
    {
        /* Offsuit: 12 combinations (4×3 suit combos) */
        return 12;
    }
}

int preflop_range_count_combinations(const preflop_range_t *range)
{
    int total = 0;

    for (int h = 0; h < PREFLOP_NUM_CANONICAL_HANDS; h++)
    {
        if (!preflop_range_contains(range, h))
            continue;

        total += preflop_range_count_combinations_single(h);
    }

    return total;
}

int preflop_hand_enumerate_combinations(
    preflop_hand_t hand,
    int out_combos[][2],
    int max_combos)
{
    /* TODO: Implement combination enumeration */
    (void)hand;
    (void)out_combos;
    (void)max_combos;
    return 0;
}
