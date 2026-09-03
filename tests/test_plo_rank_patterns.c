/* test_plo_rank_patterns.c - ProPokerTools rank patterns for 5/6-card Omaha.
 *
 * ARP implements this notation for PLO4, but OmahaHand_Instantiate is written
 * as hand-unrolled four-card loops over C(52,4) ranked tables.  A PLO5/PLO6
 * pattern sent through it came back as FOUR-card hands, so the solver ran the
 * wrong game without saying so.  These cases pin the n-card expander:
 * every hand has the right width, carries the required ranks with "at least"
 * multiplicity as PPT defines it, and the class sizes match the closed form.
 */

#include <poker_eval/core/cardmask_compat.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/range.h>

#include <stdio.h>
#include <string.h>

static int card_count(StdDeck_CardMask hand)
{
    int n = 0;
    for (int c = 0; c < StdDeck_N_CARDS; ++c)
        if (StdDeck_CardMask_CARD_IS_SET(hand, c))
            ++n;
    return n;
}

static int rank_count(StdDeck_CardMask hand, int rank)
{
    int n = 0;
    for (int s = 0; s < StdDeck_Suit_COUNT; ++s)
        if (StdDeck_CardMask_CARD_IS_SET(hand, StdDeck_MAKE_CARD(rank, s)))
            ++n;
    return n;
}

typedef struct
{
    enum_game_t game;
    const char *pattern;
    int width;
    size_t expected;     /* closed-form class size, dead cards empty */
    int required[4];     /* rank, minimum multiplicity pairs */
    int minimum[4];
    int required_count;
} pattern_case_t;

int main(void)
{
    /* Counts verified by inclusion-exclusion over "rank absent":
     *   AAxxx  = C(52,5) - C(48,5) - 4*C(48,4)          = 108336
     *   AAKKx  = 6*6*44 + C(4,3)*6 + 6*C(4,3)           =   1632
     *   AKQxx / AKQJxx from the same inclusion-exclusion.  */
    static const pattern_case_t cases[] = {
        { game_omaha5, "AAxxx",  5, 108336u,
          { StdDeck_Rank_ACE }, { 2 }, 1 },
        { game_omaha5, "AKQxx",  5,  62064u,
          { StdDeck_Rank_ACE, StdDeck_Rank_KING, StdDeck_Rank_QUEEN },
          { 1, 1, 1 }, 3 },
        { game_omaha5, "AAKKx",  5,   1632u,
          { StdDeck_Rank_ACE, StdDeck_Rank_KING }, { 2, 2 }, 2 },
        { game_omaha6, "AKQJxx", 6, 221056u,
          { StdDeck_Rank_ACE, StdDeck_Rank_KING, StdDeck_Rank_QUEEN,
            StdDeck_Rank_JACK }, { 1, 1, 1, 1 }, 4 }
    };
    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);

    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        const pattern_case_t *c = &cases[i];
        pe_range_t *range = NULL;

        if (pe_range_parse(c->game, c->pattern, dead, NULL, &range) != PE_STATUS_OK ||
            !range)
        {
            fprintf(stderr, "test_plo_rank_patterns: %s did not parse\n", c->pattern);
            return 1;
        }
        if (range->count != c->expected)
        {
            fprintf(stderr, "test_plo_rank_patterns: %s gave %zu combos, want %zu\n",
                    c->pattern, range->count, c->expected);
            return 1;
        }
        for (size_t h = 0u; h < range->count; ++h)
        {
            StdDeck_CardMask hand = range->combos[h].hand;
            if (card_count(hand) != c->width)
            {
                fprintf(stderr,
                        "test_plo_rank_patterns: %s produced a %d-card hand\n",
                        c->pattern, card_count(hand));
                return 1;
            }
            for (int r = 0; r < c->required_count; ++r)
                if (rank_count(hand, c->required[r]) < c->minimum[r])
                {
                    fprintf(stderr,
                            "test_plo_rank_patterns: %s hand missing a required rank\n",
                            c->pattern);
                    return 1;
                }
        }
        /* "At least" semantics reach the same hand through several splits;
         * the expansion must have deduplicated. */
        for (size_t h = 1u; h < range->count; ++h)
            for (size_t k = h + 1u; k < range->count && k < h + 8u; ++k)
                if (StdDeck_CardMask_EQUAL(range->combos[h].hand,
                                           range->combos[k].hand))
                {
                    fprintf(stderr, "test_plo_rank_patterns: %s duplicated a hand\n",
                            c->pattern);
                    return 1;
                }
        printf("  %-8s %zu combos\n", c->pattern, range->count);
        pe_range_free(range);
    }

    /* Suit suffixes are four-card notation: 2-2-0-0 for ds, 1-1-1-1 for
     * rainbow.  Those shapes have no agreed 5/6-card meaning, so they are
     * refused rather than guessed at. */
    {
        static const char *suffixed[] = { "AAxxxds", "AAxxxss", "AAxxxr" };
        for (size_t i = 0u; i < sizeof(suffixed) / sizeof(suffixed[0]); ++i)
        {
            pe_range_t *range = NULL;
            if (pe_range_parse(game_omaha5, suffixed[i], dead, NULL, &range) ==
                PE_STATUS_OK)
            {
                fprintf(stderr, "test_plo_rank_patterns: %s should be refused\n",
                        suffixed[i]);
                pe_range_free(range);
                return 1;
            }
        }
    }

    /* A 5-card game must never accept a four-card pattern: that was the old
     * silent failure, where the wrong-width hands reached the solver. */
    {
        pe_range_t *range = NULL;
        if (pe_range_parse(game_omaha5, "AAxx", dead, NULL, &range) == PE_STATUS_OK)
        {
            fprintf(stderr, "test_plo_rank_patterns: AAxx is not a 5-card hand\n");
            pe_range_free(range);
            return 1;
        }
    }

    /* Concrete hands keep working alongside the patterns. */
    {
        pe_range_t *range = NULL;
        if (pe_range_parse(game_omaha6, "AsKsQd3c9h8d", dead, NULL, &range) !=
                PE_STATUS_OK ||
            !range || range->count != 1u ||
            card_count(range->combos[0].hand) != 6)
        {
            fprintf(stderr, "test_plo_rank_patterns: concrete PLO6 hand failed\n");
            return 1;
        }
        pe_range_free(range);
    }

    puts("test_plo_rank_patterns: PPT rank patterns for PLO5/PLO6 passed");
    return 0;
}
