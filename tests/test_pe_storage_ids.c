/*
 * test_pe_storage_ids.c - STO-01/STO-02: dense ids and ragged value slabs
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * Two properties, and everything downstream leans on both.
 *
 * Ids are dense and stable. Dense because the set of live ids being exactly
 * [0, count) is what lets a checkpoint, a device upload and a parallel merge
 * address infosets by index instead of by pointer. Stable because a key that
 * resolved to 7 must still resolve to 7 after ten thousand more insertions and
 * three rehashes — an id that moved would silently repoint every span already
 * handed out.
 *
 * Slabs are addressed the same way in both lanes. The vector lane carries one
 * value per combo and the scalar lane sets combo_count to 1; if a single
 * indexing rule did not cover both, the two lanes would need two storages, and
 * the reason this file exists would be gone.
 */

#include <poker_eval/solver/pe_storage.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Exact equality is the intent throughout: every value written here is read
 * back unchanged, never computed on. A tolerance would let a mis-indexed read
 * land on a neighbouring slot and still pass, which is the whole failure this
 * file exists to catch.
 */
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wfloat-equal"
#endif

static int g_failures = 0;

#define CHECK(cond, ...)                                       \
    do                                                         \
    {                                                          \
        if (!(cond))                                           \
        {                                                      \
            fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                      \
            fprintf(stderr, "\n");                             \
            g_failures++;                                      \
        }                                                      \
    } while (0)

/* Deterministic: a failing case has to be reproducible from the source. */
static uint64_t g_rng = 0x243F6A8885A308D3ull;
static uint64_t next_key(void)
{
    uint64_t x = g_rng;
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    g_rng = x;
    return x * 0x2545F4914F6CDD1Dull;
}

/* ------------------------------------------------------------------ *
 * Ids
 * ------------------------------------------------------------------ */

#define BIG 1000000

static void test_one_million_keys(void)
{
    pe_storage_t *s = pe_storage_create(0);   /* no hint: exercise the growth */
    uint64_t *keys = (uint64_t *)malloc(sizeof(uint64_t) * BIG);
    pe_infoset_id_t *ids = (pe_infoset_id_t *)malloc(sizeof(pe_infoset_id_t) * BIG);
    unsigned char *seen;
    int i;
    int distinct = 0;

    CHECK(s != NULL && keys != NULL && ids != NULL, "allocation failed");
    if (!s || !keys || !ids) { free(keys); free(ids); pe_storage_destroy(s); return; }

    for (i = 0; i < BIG; ++i)
    {
        keys[i] = next_key();
        ids[i] = pe_storage_resolve(s, keys[i], 3, 1, PE_STREET_UNKNOWN);
        CHECK(ids[i] != PE_INFOSET_ID_INVALID, "resolve failed at %d", i);
        if (ids[i] == PE_INFOSET_ID_INVALID) break;
    }

    /* Every id is used exactly once, and the range is exactly [0, count). */
    distinct = (int)pe_storage_count(s);
    seen = (unsigned char *)calloc((size_t)distinct, 1);
    CHECK(seen != NULL, "allocation failed");
    if (seen)
    {
        int gaps = 0, dups = 0;
        for (i = 0; i < BIG; ++i)
        {
            CHECK((size_t)ids[i] < (size_t)distinct,
                  "id %u is outside [0, %d)", ids[i], distinct);
            if ((size_t)ids[i] < (size_t)distinct)
                seen[ids[i]]++;
        }
        for (i = 0; i < distinct; ++i)
        {
            if (seen[i] == 0) gaps++;
            else if (seen[i] > 1) dups++;
        }
        CHECK(gaps == 0, "%d ids in [0, %d) were never handed out", gaps, distinct);
        /* A duplicate here means two distinct keys collapsed onto one id. The
           generator produces distinct keys, so any duplicate is the map's. */
        CHECK(dups == 0, "%d ids were handed to more than one key", dups);
        free(seen);
    }

    /* Lookup is stable after every rehash the insertions caused. */
    for (i = 0; i < BIG; ++i)
    {
        CHECK(pe_storage_find(s, keys[i]) == ids[i],
              "key %d moved from id %u to %u", i, ids[i], pe_storage_find(s, keys[i]));
        if (pe_storage_find(s, keys[i]) != ids[i]) break;
    }

    /* Resolving again returns the same id and creates nothing. */
    for (i = 0; i < 1000; ++i)
        CHECK(pe_storage_resolve(s, keys[i], 3, 1, PE_STREET_UNKNOWN) == ids[i],
              "re-resolving key %d changed its id", i);
    CHECK(pe_storage_count(s) == (size_t)distinct,
          "re-resolving created infosets: %zu vs %d", pe_storage_count(s), distinct);

    free(keys);
    free(ids);
    pe_storage_destroy(s);
}

static void test_key_zero_is_a_key(void)
{
    pe_storage_t *s = pe_storage_create(8);
    pe_infoset_id_t a, b;

    /* The map stores id+1 with 0 meaning empty precisely so that key 0 needs no
       special case. Infoset keys are hashes: every 64-bit value is reachable. */
    a = pe_storage_resolve(s, 0, 2, 1, 0);
    b = pe_storage_resolve(s, UINT64_MAX, 2, 1, 0);
    CHECK(a == 0, "key 0 should be the first id, got %u", a);
    CHECK(b == 1, "the second key should be id 1, got %u", b);
    CHECK(pe_storage_find(s, 0) == a, "key 0 is not found again");
    CHECK(pe_storage_find(s, UINT64_MAX) == b, "key UINT64_MAX is not found again");
    CHECK(pe_storage_find(s, 12345) == PE_INFOSET_ID_INVALID,
          "an absent key was found");
    pe_storage_destroy(s);
}

static void test_degenerate_inputs(void)
{
    pe_storage_t *s = pe_storage_create(4);

    CHECK(pe_storage_resolve(s, 1, 0, 1, 0) == PE_INFOSET_ID_INVALID,
          "an infoset with no action should be refused");
    CHECK(pe_storage_resolve(s, 1, 2, 0, 0) == PE_INFOSET_ID_INVALID,
          "an infoset with no combo should be refused");
    CHECK(pe_storage_count(s) == 0, "a refused resolve created an infoset");

    CHECK(pe_storage_find(NULL, 1) == PE_INFOSET_ID_INVALID, "NULL storage");
    CHECK(pe_storage_resolve(NULL, 1, 1, 1, 0) == PE_INFOSET_ID_INVALID, "NULL storage");
    CHECK(pe_storage_meta(s, 0) == NULL, "meta of an absent id");
    CHECK(pe_storage_values(s, 0, PE_VALUES_REGRET) == NULL, "values of an absent id");
    CHECK(pe_storage_count(NULL) == 0, "count of NULL");
    CHECK(pe_storage_bytes(NULL) == 0, "bytes of NULL");
    pe_storage_destroy(NULL);
    pe_storage_destroy(s);
}

static void test_shape_conflicts_are_counted(void)
{
    pe_storage_t *s = pe_storage_create(4);
    pe_infoset_id_t id = pe_storage_resolve(s, 7, 3, 1, 0);

    CHECK(pe_storage_shape_conflicts(s) == 0, "a fresh storage reports a conflict");

    /* The slab is placed; re-shaping it would move every later infoset. The
       original shape comes back and the mismatch is counted. */
    CHECK(pe_storage_resolve(s, 7, 5, 1, 0) == id, "the id changed");
    CHECK(pe_storage_meta(s, id)->action_count == 3, "the shape was overwritten");
    CHECK(pe_storage_shape_conflicts(s) == 1, "the mismatch was not counted");

    CHECK(pe_storage_resolve(s, 7, 3, 4, 0) == id, "the id changed");
    CHECK(pe_storage_shape_conflicts(s) == 2, "a combo mismatch was not counted");

    pe_storage_destroy(s);
}

/* ------------------------------------------------------------------ *
 * Slabs
 * ------------------------------------------------------------------ */

/* Value written at (infoset, action, combo): distinct for every slot in the
   whole storage, so a mis-indexed read lands on a recognisably wrong number. */
static double cell(int i, int a, int c)
{
    return (double)i * 1000000.0 + (double)a * 2000.0 + (double)c + 0.5;
}

static void fill_and_verify(const char *label, int n_infosets,
                            uint16_t (*actions_of)(int), uint16_t (*combos_of)(int))
{
    pe_storage_t *s = pe_storage_create((size_t)n_infosets);
    int i;
    uint64_t expected_slots = 0;

    for (i = 0; i < n_infosets; ++i)
    {
        uint16_t na = actions_of(i);
        uint16_t nc = combos_of(i);
        pe_infoset_id_t id = pe_storage_resolve(s, (uint64_t)i * 0x9E3779B9ull + 1u,
                                                na, nc, (int8_t)(i % 4));
        const pe_infoset_meta_t *meta;
        double *v;
        int a, c;

        CHECK(id == (pe_infoset_id_t)i, "%s: id %u for infoset %d", label, id, i);
        meta = pe_storage_meta(s, id);
        CHECK(meta != NULL, "%s: no metadata for %d", label, i);
        if (!meta) break;

        /* Slabs are packed end to end, in id order. */
        CHECK(meta->value_offset == expected_slots,
              "%s: infoset %d starts at %llu, expected %llu", label, i,
              (unsigned long long)meta->value_offset,
              (unsigned long long)expected_slots);
        expected_slots += (uint64_t)na * (uint64_t)nc;

        v = pe_storage_values(s, id, PE_VALUES_REGRET);
        CHECK(v != NULL, "%s: no regret span for %d", label, i);
        if (!v) break;
        for (a = 0; a < na; ++a)
            for (c = 0; c < nc; ++c)
                v[pe_storage_slot_index(meta, (uint16_t)a, (uint16_t)c)] = cell(i, a, c);
    }

    CHECK(pe_storage_slot_count(s) == expected_slots,
          "%s: slot count is %llu, expected %llu", label,
          (unsigned long long)pe_storage_slot_count(s),
          (unsigned long long)expected_slots);

    /* Read every slot back after all the growth: a realloc that failed to
       carry the data, or an offset that drifted, shows up here and nowhere
       earlier. */
    for (i = 0; i < n_infosets; ++i)
    {
        const pe_infoset_meta_t *meta = pe_storage_meta(s, (pe_infoset_id_t)i);
        const double *v = pe_storage_values_const(s, (pe_infoset_id_t)i, PE_VALUES_REGRET);
        int a, c, bad = 0;
        if (!meta || !v) { CHECK(0, "%s: missing span for %d", label, i); break; }
        for (a = 0; a < meta->action_count && !bad; ++a)
            for (c = 0; c < meta->combo_count; ++c)
            {
                double got = v[pe_storage_slot_index(meta, (uint16_t)a, (uint16_t)c)];
                if (got != cell(i, a, c))
                {
                    CHECK(0, "%s: infoset %d slot (%d,%d) is %.17g, expected %.17g",
                          label, i, a, c, got, cell(i, a, c));
                    bad = 1;
                    break;
                }
            }
        if (bad) break;
    }

    /* An array never touched costs nothing; asking for it allocates it zeroed,
       including for infosets created long before. */
    CHECK(pe_storage_values_const(s, 0, PE_VALUES_LOCKED) == NULL,
          "%s: an untouched array was allocated", label);
    {
        double *locked = pe_storage_values(s, 0, PE_VALUES_LOCKED);
        CHECK(locked != NULL && locked[0] == 0.0,
              "%s: a lazily allocated array is not zeroed", label);
    }

    pe_storage_destroy(s);
}

static uint16_t scalar_actions(int i) { return (uint16_t)(2 + (i % 7)); }
static uint16_t scalar_combos(int i)  { (void)i; return 1; }
static uint16_t vector_actions(int i) { return (uint16_t)(2 + (i % 4)); }
static uint16_t vector_combos(int i)  { return (uint16_t)(1 + (i % 5) * 331); }

static void test_slab_layout(void)
{
    /* Lane B: one value per action. */
    fill_and_verify("scalar", 1000, scalar_actions, scalar_combos);
    /* Lane A: up to 1325 combos, the Hold'em order of magnitude. */
    fill_and_verify("vector", 1000, vector_actions, vector_combos);
}

static void test_full_holdem_width(void)
{
    pe_storage_t *s = pe_storage_create(4);
    pe_infoset_id_t id = pe_storage_resolve(s, 99, 3, 1326, 2);
    const pe_infoset_meta_t *meta = pe_storage_meta(s, id);
    double *v = pe_storage_values(s, id, PE_VALUES_AVERAGE);
    int a, c, bad = 0;

    CHECK(meta != NULL && v != NULL, "no span for a full-width infoset");
    if (!meta || !v) { pe_storage_destroy(s); return; }

    CHECK(pe_storage_slab_size(meta) == 3u * 1326u,
          "slab is %zu slots, expected %u", pe_storage_slab_size(meta), 3u * 1326u);

    for (a = 0; a < 3; ++a)
        for (c = 0; c < 1326; ++c)
            v[pe_storage_slot_index(meta, (uint16_t)a, (uint16_t)c)] = cell(0, a, c);
    for (a = 0; a < 3 && !bad; ++a)
        for (c = 0; c < 1326; ++c)
            if (v[pe_storage_slot_index(meta, (uint16_t)a, (uint16_t)c)] != cell(0, a, c))
            {
                CHECK(0, "1326-combo slot (%d,%d) reads back wrong", a, c);
                bad = 1;
                break;
            }

    /* Value arrays are independent: writing the average must not touch the
       regret, which shares an offset but not an array. */
    {
        const double *r = pe_storage_values_const(s, id, PE_VALUES_REGRET);
        CHECK(r == NULL, "the regret array was allocated by an average write");
    }

    pe_storage_destroy(s);
}

static void test_flags(void)
{
    pe_storage_t *s = pe_storage_create(4);
    pe_infoset_id_t id = pe_storage_resolve(s, 5, 2, 1, 1);

    CHECK(pe_storage_meta(s, id)->flags == 0, "a new infoset carries flags");
    CHECK(pe_storage_set_flags(s, id, PE_INFOSET_LOCKED, 0) == 0, "set failed");
    CHECK(pe_storage_meta(s, id)->flags == PE_INFOSET_LOCKED, "flag not set");
    CHECK(pe_storage_set_flags(s, id, PE_INFOSET_PRUNED, PE_INFOSET_LOCKED) == 0,
          "set/clear failed");
    CHECK(pe_storage_meta(s, id)->flags == PE_INFOSET_PRUNED,
          "flags are %u, expected PRUNED alone", pe_storage_meta(s, id)->flags);
    CHECK(pe_storage_set_flags(s, PE_INFOSET_ID_INVALID, 1, 0) == -1,
          "an invalid id was accepted");
    pe_storage_destroy(s);
}

int main(void)
{
    test_one_million_keys();
    test_key_zero_is_a_key();
    test_degenerate_inputs();
    test_shape_conflicts_are_counted();
    test_slab_layout();
    test_full_holdem_width();
    test_flags();

    if (g_failures != 0)
    {
        fprintf(stderr, "test_pe_storage_ids: %d failure(s)\n", g_failures);
        return 1;
    }

    printf("test_pe_storage_ids: dense ids and ragged slabs verified\n");
    return 0;
}
