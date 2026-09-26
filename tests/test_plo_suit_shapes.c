/* test_plo_suit_shapes.c - unambiguous suit structure for PLO5/PLO6 ranges.
 *
 * `ds`, `ss`, `ts`, `qs` and `r` are four-card words: ds is 2-2-0-0, rainbow is
 * 1-1-1-1.  Widening a hand past four cards breaks the words, not the
 * structure, so the solver takes the structure itself:
 *
 *     AAxxx[suits=2-2-1]     PLO5, two suits of two plus a singleton
 *     AAKKxx[suits=2-2-2]    PLO6, three suits of two
 *
 * These cases pin three things: the shape model is exact (group sizes, not a
 * name), a shape that sums to something other than the card count is refused
 * instead of reinterpreted, and every expanded hand really has the shape that
 * was asked for.  The class sizes are closed-form counts verified
 * independently by brute-force enumeration over C(52,5) and by suit-group
 * enumeration for the six-card cases.
 */

#include <poker_eval/core/cardmask_compat.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/range.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(cond, ...)                                             \
    do                                                               \
    {                                                                \
        if (!(cond))                                                 \
        {                                                            \
            fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__);   \
            fprintf(stderr, __VA_ARGS__);                            \
            fprintf(stderr, "\n");                                   \
            g_failures++;                                            \
        }                                                            \
    } while (0)

static StdDeck_CardMask no_dead(void)
{
    StdDeck_CardMask m;
    StdDeck_CardMask_RESET(m);
    return m;
}

static int card_count(StdDeck_CardMask hand)
{
    int n = 0;
    for (int c = 0; c < StdDeck_N_CARDS; ++c)
        if (StdDeck_CardMask_CARD_IS_SET(hand, c))
            ++n;
    return n;
}

static StdDeck_CardMask mask_of(const char *text)
{
    StdDeck_CardMask m;
    StdDeck_CardMask_RESET(m);
    for (size_t i = 0u; text[i] && text[i + 1u]; i += 2u)
    {
        int rank = -1;
        switch (text[i])
        {
            case 'A': rank = StdDeck_Rank_ACE; break;
            case 'K': rank = StdDeck_Rank_KING; break;
            case 'Q': rank = StdDeck_Rank_QUEEN; break;
            case 'J': rank = StdDeck_Rank_JACK; break;
            case 'T': rank = StdDeck_Rank_TEN; break;
            default: rank = text[i] - '2' + StdDeck_Rank_2; break;
        }
        int suit = StdDeck_Suit_CLUBS;
        switch (text[i + 1u])
        {
            case 's': suit = StdDeck_Suit_SPADES; break;
            case 'h': suit = StdDeck_Suit_HEARTS; break;
            case 'd': suit = StdDeck_Suit_DIAMONDS; break;
            default: suit = StdDeck_Suit_CLUBS; break;
        }
        StdDeck_CardMask_SET(m, StdDeck_MAKE_CARD(rank, suit));
    }
    return m;
}

/* ------------------------------------------------------------------ *
 * The shape model
 * ------------------------------------------------------------------ */

static void test_shape_model(void)
{
    pe_suit_shape_t a, b, c;
    char text[32];

    CHECK(pe_suit_shape_parse("2-2-1", 5u, &a), "2-2-1 is a five-card shape");
    CHECK(a.cards == 5u && a.group_count == 3u, "2-2-1 describes three groups");
    CHECK(a.groups[0] == 2u && a.groups[1] == 2u && a.groups[2] == 1u,
          "groups are stored largest first");

    CHECK(pe_suit_shape_parse("1-2-2", 5u, &b), "1-2-2 is accepted");
    CHECK(pe_suit_shape_equal(&a, &b),
          "1-2-2 and 2-2-1 describe the same hand, so they are one shape");

    CHECK(pe_suit_shape_format(&a, text, sizeof(text)) == 5 &&
              strcmp(text, "2-2-1") == 0,
          "format round-trips the canonical spelling, got '%s'", text);
    CHECK(pe_suit_shape_parse(text, 5u, &c) && pe_suit_shape_equal(&a, &c),
          "format then parse is the identity");

    CHECK(!pe_suit_shape_equal(&a, NULL), "a missing shape is not equal to one");

    /* The fields are public, so equality must validate them before using the
     * caller-provided group_count as an array bound. */
    {
        pe_suit_shape_t malformed = a;
        malformed.group_count = PE_SUIT_SHAPE_MAX_GROUPS + 1u;
        CHECK(!pe_suit_shape_equal(&malformed, &a),
              "an oversized public group_count is invalid");
        malformed = a;
        malformed.cards = 6u;
        CHECK(!pe_suit_shape_equal(&malformed, &a),
              "a shape whose groups do not sum to cards is invalid");
    }

    /* A shape that could mean two hands, or no hand, is refused. */
    static const char *bad[] = {
        "",           /* empty */
        "2-2",        /* sums to 4, not 5 */
        "2-2-1-1",    /* sums to 6, not 5 */
        "0-2-3",      /* a zero group is a second spelling of 2-3 */
        "2-2-1-",     /* trailing separator */
        "-2-2-1",     /* leading separator */
        "2--1-2",     /* empty group */
        "2-x-1",      /* not a number */
        "2-2-1junk",  /* trailing junk */
        "14-1",       /* a group larger than the number of ranks */
        "1-1-1-1-1",  /* five groups, but the deck has four suits */
        "123456-1",   /* wider than the deck */
    };
    for (size_t i = 0u; i < sizeof(bad) / sizeof(bad[0]); ++i)
        CHECK(!pe_suit_shape_parse(bad[i], 5u, &c),
              "malformed shape '%s' must be refused", bad[i]);

    /* Six-card shapes exist that a four-card vocabulary could never name. */
    CHECK(pe_suit_shape_parse("2-2-2", 6u, &a) && a.group_count == 3u,
          "2-2-2 is a six-card shape");

    /* From a concrete hand. */
    CHECK(pe_suit_shape_from_mask(mask_of("AsKs"), &a) && a.cards == 2u &&
              a.group_count == 1u && a.groups[0] == 2u,
          "AsKs is 2");
    CHECK(pe_suit_shape_from_mask(mask_of("AsKhQdJc"), &a) && a.cards == 4u &&
              a.group_count == 4u,
          "AsKhQdJc is rainbow");
    CHECK(pe_suit_shape_from_mask(mask_of("AsKsQsJsAhKh"), &a) && a.cards == 6u &&
              a.group_count == 2u && a.groups[0] == 4u && a.groups[1] == 2u,
          "four spades and two hearts is 4-2");
    CHECK(!pe_suit_shape_from_mask(no_dead(), &a), "the empty hand has no shape");

    /* pe_range_memory_bytes tracks the combo storage the caller allocated. */
    {
        pe_range_t *r = NULL;
        if (pe_range_parse(game_omaha5, "AAxxx[suits=2-2-1]", no_dead(), NULL,
                           &r) == PE_STATUS_OK && r)
        {
            CHECK(r->count == 41472u, "expected 41472 combos, got %zu", r->count);
            CHECK(pe_range_memory_bytes(r) >= r->count * sizeof(pe_combo_t),
                  "storage must cover the combos it holds");
            pe_range_free(r);
        }
        else
            CHECK(0, "the shaped PLO5 pattern did not parse");
    }
    CHECK(pe_range_memory_bytes(NULL) == 0u, "a NULL range holds nothing");
}

/* ------------------------------------------------------------------ *
 * Expansion: the counts and the shape of every hand
 * ------------------------------------------------------------------ */

typedef struct
{
    enum_game_t game;
    const char *range;
    const char *shape;
    int width;
    size_t expected;
} shape_case_t;

static void test_expansion(void)
{
    static const shape_case_t cases[] = {
        { game_omaha5, "AAxxx[suits=2-2-1]",      "2-2-1",   5, 41472u },
        { game_omaha5, "AKQxx[suits=3-1-1]",      "3-1-1",   5, 14268u },
        { game_omaha5, "AAKKx[suits=2-2-1]",      "2-2-1",   5,   684u },
        { game_omaha5, "AAxxx[suits=4-1]",        "4-1",     5,  2640u },
        { game_omaha5, "AAxxx[suits=3-2]",        "3-2",     5,  9504u },
        { game_omaha5, "AAxxx[suits=2-1-1-1]",    "2-1-1-1", 5, 32280u },
        { game_omaha6, "AKQJxx[suits=2-2-1-1]",   "2-2-1-1", 6, 61596u },
        { game_omaha6, "AAKKxx[suits=2-2-2]",     "2-2-2",   6,  3832u },
        { game_omaha6, "AAKKxx[suits=3-2-1]",     "3-2-1",   6, 11880u },
        { game_omaha6, "AKQJxx[suits=3-1-1-1]",   "3-1-1-1", 6, 26512u },
        { game_omaha6, "AAKKxx[suits=2-2-1-1]",   "2-2-1-1", 6, 13290u },
        { game_omaha6, "AAKKxx[suits=4-1-1]",     "4-1-1",   6,  1320u },
        { game_omaha6, "AAKKxx[suits=3-3]",       "3-3",     6,   726u },
    };

    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        const shape_case_t *c = &cases[i];
        pe_suit_shape_t want;
        pe_range_t *range = NULL;

        CHECK(pe_suit_shape_parse(c->shape, (unsigned)c->width, &want),
              "%s is a valid shape", c->shape);
        if (pe_range_parse(c->game, c->range, no_dead(), NULL, &range) !=
                PE_STATUS_OK ||
            !range)
        {
            CHECK(0, "%s did not parse", c->range);
            continue;
        }
        CHECK(range->count == c->expected,
              "%s gave %zu combos, want %zu", c->range, range->count,
              c->expected);

        for (size_t h = 0u; h < range->count; ++h)
        {
            pe_suit_shape_t actual;
            StdDeck_CardMask hand = range->combos[h].hand;

            if (card_count(hand) != c->width)
                CHECK(0, "%s produced a %d-card hand", c->range, card_count(hand));
            if (!pe_suit_shape_from_mask(hand, &actual))
                CHECK(0, "%s produced a shapeless hand", c->range);
            else if (!pe_suit_shape_equal(&actual, &want))
            {
                char got[32];
                pe_suit_shape_format(&actual, got, sizeof(got));
                CHECK(0, "%s produced a %s hand, want %s", c->range, got,
                      c->shape);
                break;
            }
            if (range->combos[h].weight != 1.0)
            {
                CHECK(0, "%s changed the default weight", c->range);
                break;
            }
        }

        /* A shape does not relax the "at least" semantics, so the same hand is
         * still reachable through several splits and must appear once. */
        for (size_t h = 1u; h < range->count && h < 64u; ++h)
            for (size_t k = h + 1u; k < range->count && k < h + 8u; ++k)
                if (StdDeck_CardMask_EQUAL(range->combos[h].hand,
                                           range->combos[k].hand))
                {
                    CHECK(0, "%s duplicated a hand", c->range);
                    h = range->count;
                    break;
                }

        printf("  %-28s %s  %zu combos\n", c->range, c->shape, range->count);
        pe_range_free(range);
    }
}

/* ------------------------------------------------------------------ *
 * Refusals: the old words, and shapes that do not fit
 * ------------------------------------------------------------------ */

static void test_refusals(void)
{
    static const struct
    {
        enum_game_t game;
        const char *range;
    } bad[] = {
        /* Four-card vocabulary, still refused on 5/6-card tokens. */
        { game_omaha5, "AAxxxds" },
        { game_omaha5, "AAxxxss" },
        { game_omaha5, "AAxxxr" },
        { game_omaha6, "AKQJxxds" },
        /* Short enough to be pattern-length, so the suffix check fires first. */
        { game_omaha5, "AAAds" },
        { game_omaha5, "AAAAr" },
        { game_omaha6, "AAKKds" },
        /* A word and a shape at once: two spellings of one constraint. */
        { game_omaha5, "AAxxx[suits=2-2-1]ds" },
        /* Shape that cannot describe this width. */
        { game_omaha5, "AAxxx[suits=2-2-1-1]" },
        { game_omaha5, "AAxxx[suits=2-2]" },
        { game_omaha5, "AAxxx[suits=1-1-1-1-1]" },
        { game_omaha5, "AAxxx[suits=0-2-3]" },
        { game_omaha5, "AAxxx[suits=2-2-2]" },
        { game_omaha6, "AAKKxx[suits=2-1-1-1-1]" },
        /* Malformed annotation syntax: refused, not ignored. */
        { game_omaha5, "AAxxx[2-2-1]" },
        { game_omaha5, "AAxxx[suit=2-2-1]" },
        { game_omaha5, "AAxxx[suits=]" },
        { game_omaha5, "AAxxx[suits=2-2-1" },
        { game_omaha5, "AAxxx[suits=2-2-1]]" },
        { game_omaha5, "AAxxx[suits=2 2 1]" },
        { game_omaha5, "AAxxx[suits=2-2-1][suits=2-2-1]" },
        { game_omaha5, "AAxxx]" },
        /* Wrong width is still wrong width, shape or not. */
        { game_omaha5, "AAxx[suits=2-2]" },
    };

    for (size_t i = 0u; i < sizeof(bad) / sizeof(bad[0]); ++i)
    {
        pe_range_t *range = NULL;
        pe_status_t st = pe_range_parse(bad[i].game, bad[i].range, no_dead(),
                                        NULL, &range);
        CHECK(st != PE_STATUS_OK && range == NULL, "'%s' must be refused",
              bad[i].range);
        if (range)
            pe_range_free(range);
    }

    /* A concrete hand carries its own shape: writing the matching one is
     * allowed, writing a different one re-describes the hand and is refused. */
    {
        pe_range_t *r = NULL;
        CHECK(pe_range_parse(game_omaha5, "AsKsQd3c9h[suits=2-1-1-1]", no_dead(),
                             NULL, &r) == PE_STATUS_OK &&
                  r && r->count == 1u,
              "a concrete hand with its own shape parses");
        pe_range_free(r);
        r = NULL;
        CHECK(pe_range_parse(game_omaha5, "AsKsQd3c9h[suits=2-2-1]", no_dead(),
                             NULL, &r) != PE_STATUS_OK,
              "a concrete hand with a shape it does not have is refused");
        if (r)
            pe_range_free(r);
    }
}

/* ------------------------------------------------------------------ *
 * Dead cards, weights and unions
 * ------------------------------------------------------------------ */

static void test_dead_cards(void)
{
    StdDeck_CardMask dead = mask_of("AsKs");
    pe_range_t *range = NULL;
    size_t i;

    CHECK(pe_range_parse(game_omaha5, "AAxxx[suits=2-2-1]", dead, NULL,
                         &range) == PE_STATUS_OK && range,
          "the shaped pattern parses with dead cards");
    if (!range)
        return;
    /* As and Ks are gone, so every spade-flavoured split that needed them is
     * gone with them.  Independent count: 19080. */
    CHECK(range->count == 19080u, "dead cards left %zu combos, want 19080",
          range->count);
    for (i = 0u; i < range->count; ++i)
    {
        pe_suit_shape_t actual;
        if (StdDeck_CardMask_ANY_SET(range->combos[i].hand, dead))
        {
            CHECK(0, "a dead card reached the range");
            break;
        }
        if (!pe_suit_shape_from_mask(range->combos[i].hand, &actual) ||
            actual.group_count != 3u || actual.groups[0] != 2u ||
            actual.groups[1] != 2u || actual.groups[2] != 1u)
        {
            CHECK(0, "a dead-card hand lost its shape");
            break;
        }
    }
    pe_range_free(range);
}

static void test_weights_and_unions(void)
{
    pe_range_t *range = NULL;
    size_t i;

    CHECK(pe_range_parse(game_omaha5, "AAxxx[suits=2-2-1]:0.25", no_dead(), NULL,
                         &range) == PE_STATUS_OK && range,
          "a shaped pattern takes a weight");
    if (range)
    {
        CHECK(range->count == 41472u, "weighting changed the combo count");
        for (i = 0u; i < range->count; ++i)
            if (range->combos[i].weight != 0.25)
            {
                CHECK(0, "weight %g on combo %zu, want 0.25",
                      range->combos[i].weight, i);
                break;
            }
        CHECK(fabs(range->total_weight - 0.25 * (double)range->count) < 1.0,
              "total_weight %g does not match the combos",
              range->total_weight);
        pe_range_free(range);
    }

    /* Two shapes agree on nothing: a 2-2-1 hand is not a 3-1-1 hand, so the
     * union is the sum and the weights are per-term. */
    range = NULL;
    CHECK(pe_range_parse(game_omaha5,
                         "AAxxx[suits=2-2-1],AKQxx[suits=3-1-1]:2", no_dead(),
                         NULL, &range) == PE_STATUS_OK && range,
          "two shaped patterns union");
    if (range)
    {
        CHECK(range->count == 41472u + 14268u, "union gave %zu combos, want %zu",
              range->count, 41472u + 14268u);
        CHECK(fabs(range->total_weight -
                   (41472.0 + 2.0 * 14268.0)) < 1.0,
              "union total_weight %g is wrong", range->total_weight);
        pe_range_free(range);
    }
}

int main(void)
{
    test_shape_model();
    test_expansion();
    test_refusals();
    test_dead_cards();
    test_weights_and_unions();

    if (g_failures)
    {
        fprintf(stderr, "test_plo_suit_shapes: %d failure(s)\n", g_failures);
        return 1;
    }
    puts("test_plo_suit_shapes: PLO5/PLO6 suit shapes passed");
    return 0;
}
