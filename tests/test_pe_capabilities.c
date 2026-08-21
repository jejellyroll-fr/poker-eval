/*
 * test_pe_capabilities.c - CTR-02 capability bits and their text form
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * Checks the three properties the ticket asks for — every bit has a unique
 * name, no two bits share a value, and caps -> string -> caps is the identity
 * — plus the edge cases that make the round-trip trustworthy in practice.
 *
 * The round-trip is tested over the whole uint64_t range, not just over
 * subsets of PE_CAP_ALL: unnamed bits travel through a hex token, so a plan
 * written by a build that knows more capabilities than this one still survives
 * a read/write cycle here.
 */

#include <poker_eval/solver/pe_capabilities.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(cond, ...)                                   \
    do                                                     \
    {                                                      \
        if (!(cond))                                       \
        {                                                  \
            fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                  \
            fprintf(stderr, "\n");                         \
            g_failures++;                                  \
        }                                                  \
    } while (0)

/* Deterministic 64-bit generator: the failing case of a random round-trip has
   to be reproducible from the source alone. */
static uint64_t g_rng_state = UINT64_C(0x9E3779B97F4A7C15);

static uint64_t next_random(void)
{
    uint64_t x = g_rng_state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    g_rng_state = x;
    return x * UINT64_C(0x2545F4914F6CDD1D);
}

/* ------------------------------------------------------------------ *
 * Every bit has a unique name and a unique value
 * ------------------------------------------------------------------ */

static void test_bits_are_distinct_and_named(void)
{
    uint64_t seen = 0;
    uint64_t union_of_all = 0;
    size_t i;
    size_t j;

    for (i = 0; i < PE_CAP_COUNT; ++i)
    {
        uint64_t bit = pe_cap_at(i);
        const char *name = pe_cap_name(bit);

        CHECK(bit != 0, "capability %zu has value 0", i);
        CHECK((bit & (bit - 1)) == 0, "capability %zu is not a single bit", i);
        CHECK(name != NULL, "capability %zu has no name", i);
        CHECK((seen & bit) == 0, "capability %zu (%s) reuses a value already taken",
              i, name != NULL ? name : "?");
        CHECK((bit & PE_CAP_ALL) != 0, "capability %zu is missing from PE_CAP_ALL", i);

        seen |= bit;
        union_of_all |= bit;

        /* The name maps back to the same bit, case-insensitively. */
        if (name != NULL)
        {
            CHECK(pe_cap_from_name(name) == bit, "%s does not map back to its bit", name);
        }

        /* No two capabilities share a name. */
        for (j = 0; j < i; ++j)
        {
            const char *other = pe_cap_name(pe_cap_at(j));
            if (name != NULL && other != NULL)
                CHECK(strcmp(name, other) != 0, "name %s is used twice", name);
        }
    }

    CHECK(union_of_all == (uint64_t)PE_CAP_ALL,
          "PE_CAP_ALL does not match the union of the declared bits");
    CHECK(pe_cap_at(PE_CAP_COUNT) == 0, "pe_cap_at accepts an out-of-range index");
}

static void test_name_lookup_edges(void)
{
    CHECK(pe_cap_name(0) == NULL, "the empty set should have no single name");
    CHECK(pe_cap_name(PE_CAP_VECTOR_FORM | PE_CAP_MULTIWAY) == NULL,
          "a multi-bit value should have no single name");
    CHECK(pe_cap_name(UINT64_C(1) << 63) == NULL, "an unnamed bit should have no name");

    CHECK(pe_cap_from_name(NULL) == 0, "NULL is not a capability name");
    CHECK(pe_cap_from_name("") == 0, "the empty string is not a capability name");
    CHECK(pe_cap_from_name("NOT_A_CAPABILITY") == 0, "an unknown name should not resolve");
    CHECK(pe_cap_from_name("vector_form") == PE_CAP_VECTOR_FORM,
          "name lookup should be case-insensitive");
    CHECK(pe_cap_from_name("Vector_Form") == PE_CAP_VECTOR_FORM,
          "name lookup should be case-insensitive");
    CHECK(pe_cap_from_name("VECTOR_FOR") == 0, "a prefix should not resolve");
    CHECK(pe_cap_from_name("VECTOR_FORMS") == 0, "a longer name should not resolve");
}

/* ------------------------------------------------------------------ *
 * Round-trip
 * ------------------------------------------------------------------ */

static void round_trip(uint64_t caps, const char *what)
{
    char buf[PE_CAPS_STRING_MAX];
    uint64_t parsed = ~caps; /* poison, so a no-op parse cannot pass */
    size_t needed;
    int rc;

    needed = pe_caps_to_string(caps, buf, sizeof(buf));
    CHECK(needed < sizeof(buf),
          "%s: PE_CAPS_STRING_MAX is too small (needed %zu)", what, needed);
    CHECK(strlen(buf) == needed, "%s: rendered length disagrees with the return value", what);

    rc = pe_caps_parse(buf, &parsed);
    CHECK(rc == 0, "%s: parse of \"%s\" failed at token %d", what, buf, rc);
    CHECK(parsed == caps, "%s: round-trip changed 0x%llx into 0x%llx via \"%s\"",
          what, (unsigned long long)caps, (unsigned long long)parsed, buf);
}

static void test_round_trip_random(void)
{
    int i;

    /* 1000 arbitrary 64-bit values: named bits, unnamed bits, and mixtures. */
    for (i = 0; i < 1000; ++i)
        round_trip(next_random(), "random uint64");

    /* 1000 subsets of the named capabilities, which is what the registry
       actually manipulates. */
    for (i = 0; i < 1000; ++i)
        round_trip(next_random() & (uint64_t)PE_CAP_ALL, "random named subset");
}

static void test_round_trip_edges(void)
{
    size_t i;

    round_trip(0, "empty set");
    round_trip((uint64_t)PE_CAP_ALL, "every named capability");
    round_trip(UINT64_MAX, "every bit");
    round_trip(~(uint64_t)PE_CAP_ALL, "only unnamed bits");

    for (i = 0; i < PE_CAP_COUNT; ++i)
        round_trip(pe_cap_at(i), "single capability");
}

/* ------------------------------------------------------------------ *
 * Text form details
 * ------------------------------------------------------------------ */

static void test_rendering(void)
{
    char buf[PE_CAPS_STRING_MAX];
    size_t needed;

    pe_caps_to_string(0, buf, sizeof(buf));
    CHECK(strcmp(buf, PE_CAPS_NONE_TOKEN) == 0, "the empty set should render as %s",
          PE_CAPS_NONE_TOKEN);

    pe_caps_to_string(PE_CAP_VECTOR_FORM, buf, sizeof(buf));
    CHECK(strcmp(buf, "VECTOR_FORM") == 0, "unexpected rendering \"%s\"", buf);

    /* Declaration order, not bit order reversed, and no stray separator. */
    pe_caps_to_string(PE_CAP_CPU_PARALLEL | PE_CAP_VECTOR_FORM, buf, sizeof(buf));
    CHECK(strcmp(buf, "VECTOR_FORM|CPU_PARALLEL") == 0, "unexpected rendering \"%s\"", buf);

    /* Truncation is reported, and what is written stays NUL-terminated. */
    needed = pe_caps_to_string(PE_CAP_VECTOR_FORM, buf, 4);
    CHECK(needed == strlen("VECTOR_FORM"), "truncation should report the full length");
    CHECK(strlen(buf) < 4, "a truncated buffer must stay NUL-terminated");

    /* Measuring without writing. */
    needed = pe_caps_to_string(PE_CAP_VECTOR_FORM, NULL, 0);
    CHECK(needed == strlen("VECTOR_FORM"), "a NULL buffer should still measure");
}

static void test_parse_details(void)
{
    uint64_t caps = 0;

    CHECK(pe_caps_parse(NULL, &caps) == -1, "a NULL text should be rejected");
    CHECK(pe_caps_parse("VECTOR_FORM", NULL) == -1, "a NULL output should be rejected");

    caps = UINT64_MAX;
    CHECK(pe_caps_parse("", &caps) == 0 && caps == 0, "an empty text is the empty set");

    caps = UINT64_MAX;
    CHECK(pe_caps_parse("NONE", &caps) == 0 && caps == 0, "NONE is the empty set");

    caps = 0;
    CHECK(pe_caps_parse("  VECTOR_FORM | CPU_PARALLEL  ", &caps) == 0,
          "spaces around separators should be tolerated");
    CHECK(caps == (PE_CAP_VECTOR_FORM | PE_CAP_CPU_PARALLEL), "unexpected parse result");

    /* An unrecognised token is reported by its 1-based position. */
    CHECK(pe_caps_parse("VECTOR_FORM|NOPE", &caps) == 2, "should report token 2");
    CHECK(pe_caps_parse("NOPE|VECTOR_FORM", &caps) == 1, "should report token 1");
    CHECK(pe_caps_parse("VECTOR_FORM||MULTIWAY", &caps) == 2, "an empty token is malformed");
    CHECK(pe_caps_parse("VECTOR_FORM|", &caps) == 2, "a trailing separator is malformed");
    CHECK(pe_caps_parse("NONE|MULTIWAY", &caps) == 1, "NONE cannot be combined");
    CHECK(pe_caps_parse("0xZZ", &caps) == 1, "a malformed hex token should be reported");

    /* A failed parse leaves the output alone. */
    caps = UINT64_C(0xDEADBEEF);
    (void)pe_caps_parse("NOPE", &caps);
    CHECK(caps == UINT64_C(0xDEADBEEF), "a failed parse must not touch the output");
}

int main(void)
{
    test_bits_are_distinct_and_named();
    test_name_lookup_edges();
    test_round_trip_random();
    test_round_trip_edges();
    test_rendering();
    test_parse_details();

    if (g_failures != 0)
    {
        fprintf(stderr, "test_pe_capabilities: %d failure(s)\n", g_failures);
        return 1;
    }

    printf("test_pe_capabilities: %d capabilities, all checks passed\n", PE_CAP_COUNT);
    return 0;
}
