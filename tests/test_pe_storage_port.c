/*
 * test_pe_storage_port.c - STO-03: the storage really is a port
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * This file includes pe_storage_port.h and nothing else from the solver. It
 * never names pe_storage_t, pe_infoset_meta_t or any other concrete type. If
 * it needed one more include, an operation would be missing from the port and
 * a second backend would have to reach around it.
 *
 * The same suite runs twice: once against the RAM adapter, once against a
 * deliberately unrelated one defined below — fixed capacity, linear search,
 * no hashing, its own layout. A port with a single implementation is not a
 * port, it is a typedef; running the contract against two is the only way to
 * find that out before CUDA arrives and it is expensive to discover.
 */

#include <poker_eval/solver/pe_storage_port.h>
/* Declares the legacy adapter only. Still no concrete storage type in sight:
   the adapter that wraps the v2 storage is declared by the module that owns
   it, not by the port. */
#include <poker_eval/engine/solvers/cfr/cfr_storage_legacy_port.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;
static const char *g_adapter = "?";

#define CHECK(cond, ...)                                            \
    do                                                              \
    {                                                               \
        if (!(cond))                                                \
        {                                                           \
            fprintf(stderr, "FAILED [%s] %s:%d: ", g_adapter, __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                           \
            fprintf(stderr, "\n");                                  \
            g_failures++;                                           \
        }                                                           \
    } while (0)

/* Exact equality is the intent: values are written and read back, never
   computed on. */
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wfloat-equal"
#endif

/* ------------------------------------------------------------------ *
 * A second adapter, sharing nothing with the first
 * ------------------------------------------------------------------ */

#define TOY_MAX_INFOSETS 64
#define TOY_MAX_SLOTS 8192

typedef struct
{
    uint64_t key[TOY_MAX_INFOSETS];
    uint16_t actions[TOY_MAX_INFOSETS];
    uint16_t combos[TOY_MAX_INFOSETS];
    int8_t street[TOY_MAX_INFOSETS];
    uint8_t flags[TOY_MAX_INFOSETS];
    size_t offset[TOY_MAX_INFOSETS];
    size_t count;
    uint64_t slots;
    /* Whole arrays up front rather than on demand: an adapter is free to
       differ here, and the contract must not depend on it. */
    double values[PE_VALUES_COUNT][TOY_MAX_SLOTS];
    int touched[PE_VALUES_COUNT];
} toy_t;

static int toy_create(void **self, size_t hint)
{
    toy_t *t;
    (void)hint;
    if (!self)
        return -1;
    t = (toy_t *)calloc(1, sizeof(toy_t));
    if (!t)
        return -1;
    *self = t;
    return 0;
}

static void toy_destroy(void *self) { free(self); }

static pe_infoset_id_t toy_find(const void *self, uint64_t key)
{
    const toy_t *t = (const toy_t *)self;
    size_t i;
    if (!t)
        return PE_INFOSET_ID_INVALID;
    for (i = 0; i < t->count; ++i)          /* linear, on purpose */
        if (t->key[i] == key)
            return (pe_infoset_id_t)i;
    return PE_INFOSET_ID_INVALID;
}

static pe_infoset_id_t toy_resolve(void *self, uint64_t key, uint16_t na,
                                   uint16_t nc, int8_t street)
{
    toy_t *t = (toy_t *)self;
    pe_infoset_id_t id;
    size_t slab;

    if (!t || na == 0 || nc == 0)
        return PE_INFOSET_ID_INVALID;

    id = toy_find(t, key);
    if (id != PE_INFOSET_ID_INVALID)
        return id;

    slab = (size_t)na * (size_t)nc;
    if (t->count >= TOY_MAX_INFOSETS || t->slots + slab > TOY_MAX_SLOTS)
        return PE_INFOSET_ID_INVALID;

    id = (pe_infoset_id_t)t->count;
    t->key[id] = key;
    t->actions[id] = na;
    t->combos[id] = nc;
    t->street[id] = street;
    t->flags[id] = 0;
    t->offset[id] = (size_t)t->slots;
    t->slots += slab;
    t->count++;
    return id;
}

static int toy_shape(const void *self, pe_infoset_id_t id, uint16_t *na,
                     uint16_t *nc, int8_t *street)
{
    const toy_t *t = (const toy_t *)self;
    if (!t || (size_t)id >= t->count)
        return -1;
    if (na) *na = t->actions[id];
    if (nc) *nc = t->combos[id];
    if (street) *street = t->street[id];
    return 0;
}

static double *toy_values(void *self, pe_infoset_id_t id, pe_value_array_t w,
                          size_t *out_len)
{
    toy_t *t = (toy_t *)self;
    if (!t || (size_t)id >= t->count || (int)w < 0 || w >= PE_VALUES_COUNT)
        return NULL;
    t->touched[w] = 1;
    if (out_len)
        *out_len = (size_t)t->actions[id] * (size_t)t->combos[id];
    return &t->values[w][t->offset[id]];
}

static const double *toy_values_const(const void *self, pe_infoset_id_t id,
                                      pe_value_array_t w, size_t *out_len)
{
    const toy_t *t = (const toy_t *)self;
    if (!t || (size_t)id >= t->count || (int)w < 0 || w >= PE_VALUES_COUNT)
        return NULL;
    if (!t->touched[w])
        return NULL;                 /* same contract, different mechanism */
    if (out_len)
        *out_len = (size_t)t->actions[id] * (size_t)t->combos[id];
    return &t->values[w][t->offset[id]];
}

static size_t toy_count(const void *self)
{ const toy_t *t = (const toy_t *)self; return t ? t->count : 0u; }

static uint64_t toy_slot_count(const void *self)
{ const toy_t *t = (const toy_t *)self; return t ? t->slots : 0u; }

static size_t toy_bytes(const void *self)
{ (void)self; return sizeof(toy_t); }

static int toy_set_flags(void *self, pe_infoset_id_t id, uint8_t set, uint8_t clear)
{
    toy_t *t = (toy_t *)self;
    if (!t || (size_t)id >= t->count)
        return -1;
    t->flags[id] = (uint8_t)((t->flags[id] & (uint8_t)~clear) | set);
    return 0;
}

static int toy_get_flags(const void *self, pe_infoset_id_t id, uint8_t *out)
{
    const toy_t *t = (const toy_t *)self;
    if (!t || (size_t)id >= t->count)
        return -1;
    if (out) *out = t->flags[id];
    return 0;
}

static const pe_storage_ops_t k_toy_ops = {
    "toy",
    0,                  /* no width limit */
    PE_VALUES_ALL,
    toy_create, toy_destroy, toy_resolve, toy_find, toy_shape,
    toy_values, toy_values_const, toy_count, toy_slot_count, toy_bytes,
    toy_set_flags, toy_get_flags
};

/* ------------------------------------------------------------------ *
 * The contract, run against whatever it is handed
 * ------------------------------------------------------------------ */

static double cell(int i, int a, int c)
{
    return (double)i * 10000.0 + (double)a * 100.0 + (double)c + 0.25;
}

static void run_contract(const pe_storage_ops_t *ops)
{
    void *s = NULL;
    int i, a, c;
    size_t len = 0;
    uint8_t flags = 0;
    uint64_t expected_slots = 0;

    g_adapter = ops->name;

    CHECK(ops->name != NULL, "an adapter must name itself");
    CHECK(ops->create(&s, 16) == 0, "create failed");
    if (!s) return;

    CHECK(ops->count(s) == 0, "a fresh storage holds %zu infosets", ops->count(s));
    CHECK(ops->slot_count(s) == 0, "a fresh storage holds slots");
    CHECK(ops->bytes(s) > 0, "an allocated storage reports no memory");
    CHECK(ops->find(s, 42) == PE_INFOSET_ID_INVALID, "an absent key was found");

    /* Resolve, write, read back. Shapes vary so the layout is exercised in
       both lanes: combo_count 1, and combo_count > 1. */
    for (i = 0; i < 16; ++i)
    {
        uint16_t na = (uint16_t)(2 + (i % 3));
        /* Widths the adapter declares it cannot hold are not asked of it. A
           scalar backend refusing a vector shape is correct behaviour, not a
           contract failure — what would be a failure is accepting it and
           truncating. */
        uint16_t nc = pe_storage_accepts_width(ops, 8)
                          ? (uint16_t)(1 + (i % 4) * 7)
                          : (uint16_t)1;
        pe_infoset_id_t id = ops->resolve(s, 0x5000ull + (uint64_t)i, na, nc,
                                          (int8_t)(i % 4));
        uint16_t got_a = 0, got_c = 0;
        int8_t got_s = 0;
        double *v;

        CHECK(id == (pe_infoset_id_t)i, "id %u for infoset %d", id, i);
        CHECK(ops->shape(s, id, &got_a, &got_c, &got_s) == 0, "shape failed");
        CHECK(got_a == na && got_c == nc && got_s == (int8_t)(i % 4),
              "shape came back %u/%u/%d", got_a, got_c, (int)got_s);

        v = ops->values(s, id, PE_VALUES_REGRET, &len);
        CHECK(v != NULL, "no regret span for %d", i);
        CHECK(len == (size_t)na * (size_t)nc,
              "span length is %zu, expected %u", len, (unsigned)(na * nc));
        if (!v) break;

        for (a = 0; a < na; ++a)
            for (c = 0; c < nc; ++c)
                v[pe_storage_slot_at(nc, (uint16_t)a, (uint16_t)c)] = cell(i, a, c);

        expected_slots += (uint64_t)na * (uint64_t)nc;
    }

    CHECK(ops->count(s) == 16, "count is %zu, expected 16", ops->count(s));
    CHECK(ops->slot_count(s) == expected_slots,
          "slot count is %llu, expected %llu",
          (unsigned long long)ops->slot_count(s),
          (unsigned long long)expected_slots);

    /* Iterating is a loop over [0, count): the port offers no iterator because
       dense ids make one unnecessary, and this is where that is used. */
    for (i = 0; i < (int)ops->count(s); ++i)
    {
        uint16_t na = 0, nc = 0;
        const double *v;
        int bad = 0;

        CHECK(ops->shape(s, (pe_infoset_id_t)i, &na, &nc, NULL) == 0, "shape failed");
        v = ops->values_const(s, (pe_infoset_id_t)i, PE_VALUES_REGRET, &len);
        CHECK(v != NULL && len == (size_t)na * (size_t)nc, "span for %d", i);
        if (!v) break;
        for (a = 0; a < na && !bad; ++a)
            for (c = 0; c < nc; ++c)
                if (v[pe_storage_slot_at(nc, (uint16_t)a, (uint16_t)c)] != cell(i, a, c))
                {
                    CHECK(0, "infoset %d slot (%d,%d) reads %.17g, expected %.17g",
                          i, a, c,
                          v[pe_storage_slot_at(nc, (uint16_t)a, (uint16_t)c)],
                          cell(i, a, c));
                    bad = 1;
                    break;
                }
        if (bad) break;
    }

    /* Optional arrays: allocated on request, zeroed, addressing the same slab
       as the mandatory ones, and independent of them. Checking only that a
       fresh array reads zero at slot 0 would let a span offset by one slot
       pass, since the next slot is zero too. */
    if (!pe_storage_serves(ops, PE_VALUES_LOCKED))
    {
        /* An array the adapter does not serve reads NULL through both paths,
           always — never a span into something else. */
        CHECK(ops->values(s, 0, PE_VALUES_LOCKED, &len) == NULL,
              "an unserved array handed out a writable span");
        CHECK(ops->values_const(s, 0, PE_VALUES_LOCKED, NULL) == NULL,
              "an unserved array handed out a readable span");
    }
    else
    {
    CHECK(ops->values_const(s, 0, PE_VALUES_LOCKED, NULL) == NULL,
          "an untouched array was readable");
    {
        uint16_t na = 0, nc = 0;
        size_t n, k;
        double *locked;
        const double *reread;
        const double *regret;

        CHECK(ops->shape(s, 0, &na, &nc, NULL) == 0, "shape failed");
        n = (size_t)na * (size_t)nc;

        locked = ops->values(s, 0, PE_VALUES_LOCKED, &len);
        CHECK(locked != NULL, "no locked span");
        CHECK(len == n, "locked span is %zu slots, expected %zu", len, n);
        if (!locked) { ops->destroy(s); return; }

        for (k = 0; k < n; ++k)
            CHECK(locked[k] == 0.0, "locked slot %zu is %.17g, expected 0",
                  k, locked[k]);

        /* Write the whole slab, then read it back through the const path: an
           optional array pointing one slot off, or at another infoset, shows
           up here and nowhere else. */
        for (k = 0; k < n; ++k)
            locked[k] = 900.0 + (double)k;

        reread = ops->values_const(s, 0, PE_VALUES_LOCKED, &len);
        CHECK(reread != NULL && len == n, "locked array unreadable after writing");
        if (reread)
            for (k = 0; k < n; ++k)
                CHECK(reread[k] == 900.0 + (double)k,
                      "locked slot %zu reads %.17g, expected %.17g",
                      k, reread[k], 900.0 + (double)k);

        /* And the arrays do not alias: the regret slab still holds what the
           first loop wrote. */
        regret = ops->values_const(s, 0, PE_VALUES_REGRET, NULL);
        CHECK(regret != NULL, "regret span vanished");
        if (regret)
            CHECK(regret[0] == cell(0, 0, 0),
                  "writing the locked array changed the regret array");
    }
    }

    /* A width the adapter refuses must be refused, not truncated. */
    if (!pe_storage_accepts_width(ops, 2))
        CHECK(ops->resolve(s, 0xDEAD, 2, 2, 0) == PE_INFOSET_ID_INVALID,
              "a scalar-only adapter accepted a two-combo infoset");

    /* Re-resolving is idempotent. */
    CHECK(ops->resolve(s, 0x5000ull, 2, 1, 0) == 0, "re-resolving moved the id");
    CHECK(ops->count(s) == 16, "re-resolving created an infoset");
    CHECK(ops->find(s, 0x5000ull) == 0, "find disagrees with resolve");

    /* Flags round-trip. */
    CHECK(ops->set_flags(s, 3, PE_INFOSET_LOCKED, 0) == 0, "set_flags failed");
    CHECK(ops->get_flags(s, 3, &flags) == 0, "get_flags failed");
    CHECK(flags == PE_INFOSET_LOCKED, "flags read back %u", flags);
    CHECK(ops->set_flags(s, 3, PE_INFOSET_PRUNED, PE_INFOSET_LOCKED) == 0, "set/clear");
    CHECK(ops->get_flags(s, 3, &flags) == 0 && flags == PE_INFOSET_PRUNED,
          "flags read back %u after clearing", flags);

    /* Out-of-range ids are refused, not trusted. */
    CHECK(ops->shape(s, 9999, NULL, NULL, NULL) == -1, "shape accepted a bad id");
    CHECK(ops->values(s, 9999, PE_VALUES_REGRET, NULL) == NULL, "values accepted a bad id");
    CHECK(ops->get_flags(s, 9999, &flags) == -1, "get_flags accepted a bad id");
    CHECK(ops->set_flags(s, 9999, 1, 0) == -1, "set_flags accepted a bad id");
    CHECK(ops->resolve(s, 1, 0, 1, 0) == PE_INFOSET_ID_INVALID, "zero actions accepted");
    CHECK(ops->resolve(s, 1, 1, 0, 0) == PE_INFOSET_ID_INVALID, "zero combos accepted");

    CHECK(ops->bytes(s) > 0, "a populated storage reports no memory");
    ops->destroy(s);
    ops->destroy(NULL);   /* documented as safe */
}

int main(void)
{
    run_contract(pe_storage_ram_ops());
    run_contract(&k_toy_ops);
    /* The one adapter that exists for a reason other than being tested. */
    run_contract(pe_storage_legacy_ops());

    if (g_failures != 0)
    {
        fprintf(stderr, "test_pe_storage_port: %d failure(s)\n", g_failures);
        return 1;
    }

    printf("test_pe_storage_port: the contract holds for ram, toy and legacy\n");
    return 0;
}
