/*
 * test_monker_classes.c - Four-card hand-class numbering
 *
 * The expected values exercise the complete deterministic table, which is why
 * a checksum over the whole table is the main assertion rather than a handful
 * of spot values.
 *
 * The checksum matters because several wrong numberings look right from a
 * distance. A rank-major deck also yields a bijection onto 16432 classes, and
 * so does sorting the canonical forms instead of minting indices in order of
 * first appearance. Both disagree with the format's required ordering, and
 * both would pass any test that only counted classes.
 */

#include <poker_eval/solver/pe_monker_classes.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(condition, ...)                                      \
    do                                                             \
    {                                                              \
        if (!(condition))                                          \
        {                                                          \
            fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                          \
            fputc('\n', stderr);                                   \
            failures++;                                            \
        }                                                          \
    } while (0)

/* Colexicographic rank of a sorted four-subset: the order the table is
   stored in, so walking it in this order is walking the table. */
static uint64_t colex_rank(const unsigned *v)
{
    static const uint64_t binom[52][5] = {{0}};
    uint64_t total = 0u;
    unsigned i;
    (void)binom;
    for (i = 0u; i < 4u; ++i)
    {
        uint64_t c = 1u;
        unsigned k;
        for (k = 0u; k <= i; ++k)
            c = c * (uint64_t)(v[i] - k) / (uint64_t)(k + 1u);
        total += c;
    }
    return total;
}

int main(void)
{
    pe_monker_classes_t *classes = NULL;
    uint64_t hash = 0xcbf29ce484222325ULL;
    uint16_t *by_colex;
    unsigned c0, c1, c2, c3;
    uint32_t seen_max = 0u;

    if (pe_monker_classes_create(&classes) != PE_MONKER_OK || classes == NULL)
    {
        fprintf(stderr, "FAILED: the class table was not built\n");
        return 1;
    }

    /* Spot values, taken from the running solver. Named so a failure says
       which hand moved. */
    {
        struct { int cards[4]; uint32_t expect; const char *name; } cases[] = {
            { { 0,  1,  2,  3}, 0u,     "2s3s4s5s" },
            { { 0,  1,  2, 13}, 10u,    "2s3s4s2h" },
            { { 0, 13, 26, 39}, 3040u,  "2s2h2c2d" },
            { { 0, 14, 28, 42}, 3407u,  "2s3h4c5d" },
            { {48, 49, 50, 51}, 16299u, "JdQdKdAd" },
            { {11, 12, 24, 25}, 16423u, "KsAsKhAh" },
            { {12, 25, 38, 51}, 16431u, "AsAhAcAd" }
        };
        size_t i;
        for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i)
        {
            uint32_t got = 0xFFFFFFFFu;
            CHECK(pe_monker_class_of(classes, cases[i].cards, &got) ==
                      PE_MONKER_OK && got == cases[i].expect,
                  "%s is class %u, format expects %u",
                  cases[i].name, got, cases[i].expect);
        }
    }

    /* Order of the cards must not matter: a hand is a set. */
    {
        int forward[4] = {11, 12, 24, 25};
        int shuffled[4] = {25, 11, 24, 12};
        uint32_t a = 0u, b = 1u;
        pe_monker_class_of(classes, forward, &a);
        pe_monker_class_of(classes, shuffled, &b);
        CHECK(a == b, "the same four cards in another order gave %u and %u",
              a, b);
    }

    /* Every class has a reversible representative for vector-lane decoding. */
    {
        uint32_t class_index;
        for (class_index = 0u; class_index < PE_MONKER_CLASS_COUNT;
             ++class_index)
        {
            int cards[4] = {0, 0, 0, 0};
            uint32_t round_trip = UINT32_MAX;
            CHECK(pe_monker_class_representative(classes, class_index, cards) ==
                      PE_MONKER_OK &&
                      pe_monker_class_of(classes, cards, &round_trip) ==
                      PE_MONKER_OK && round_trip == class_index,
                  "class %u representative round-tripped as %u",
                  class_index, round_trip);
        }
    }

    /* Suit relabelling must not matter either — that is what a class is. */
    {
        int spades_hearts[4] = {0, 1, 13, 14};   /* 2s3s 2h3h */
        int clubs_diamonds[4] = {26, 27, 39, 40};/* 2c3c 2d3d */
        uint32_t a = 0u, b = 1u;
        pe_monker_class_of(classes, spades_hearts, &a);
        pe_monker_class_of(classes, clubs_diamonds, &b);
        CHECK(a == b, "the same hand in other suits gave %u and %u", a, b);
    }

    /* Degenerate input is refused rather than indexed. */
    {
        int repeated[4] = {5, 5, 6, 7};
        int off_deck[4] = {0, 1, 2, 52};
        uint32_t out = 0u;
        CHECK(pe_monker_class_of(classes, repeated, &out) != PE_MONKER_OK,
              "a hand with a repeated card was given a class");
        CHECK(pe_monker_class_of(classes, off_deck, &out) != PE_MONKER_OK,
              "a card outside the deck was given a class");
        CHECK(pe_monker_class_of(NULL, repeated, &out) != PE_MONKER_OK,
              "a NULL table was accepted");
    }

    /*
     * The whole table. Every four-card hand is looked up and folded into an
     * FNV-1a hash in colexicographic order; the constant is the expected hash.
     * Spot values can survive a numbering that
     * is wrong nearly everywhere — this cannot.
     */
    by_colex = (uint16_t *)calloc(270725u, sizeof(*by_colex));
    CHECK(by_colex != NULL, "table buffer allocation failed");
    if (by_colex == NULL)
    {
        pe_monker_classes_destroy(classes);
        return 1;
    }
    for (c0 = 0u; c0 < 52u; ++c0)
        for (c1 = c0 + 1u; c1 < 52u; ++c1)
            for (c2 = c1 + 1u; c2 < 52u; ++c2)
                for (c3 = c2 + 1u; c3 < 52u; ++c3)
                {
                    int cards[4];
                    unsigned v[4];
                    uint32_t cls = 0u;
                    uint64_t rank;
                    cards[0] = (int)c0; cards[1] = (int)c1;
                    cards[2] = (int)c2; cards[3] = (int)c3;
                    v[0] = c0; v[1] = c1; v[2] = c2; v[3] = c3;
                    if (pe_monker_class_of(classes, cards, &cls) != PE_MONKER_OK)
                    {
                        CHECK(0, "a legal hand had no class");
                        goto done;
                    }
                    if (cls > seen_max)
                        seen_max = cls;
                    rank = colex_rank(v);
                    by_colex[rank] = (uint16_t)cls;
                }
    {
        size_t i;
        for (i = 0u; i < 270725u; ++i)
        {
            hash ^= (uint64_t)(by_colex[i] & 0xFFu);
            hash *= 0x100000001b3ULL;
            hash ^= (uint64_t)((by_colex[i] >> 8) & 0xFFu);
            hash *= 0x100000001b3ULL;
        }
    }
    CHECK(seen_max + 1u == PE_MONKER_CLASS_COUNT,
          "the enumeration produced %u classes, expected %u",
          seen_max + 1u, PE_MONKER_CLASS_COUNT);
    CHECK(hash == 0x328313139b869cd5ULL,
          "the table hashes to 0x%016llx; expected hash is "
          "0x328313139b869cd5", (unsigned long long)hash);

done:
    free(by_colex);
    pe_monker_classes_destroy(classes);
    if (failures)
    {
        fprintf(stderr, "test_monker_classes: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_monker_classes: 16432 classes, table matches format\n");
    return 0;
}
