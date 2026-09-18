/*
 * test_pe_storage_memory_report.c - Issue #235 (phase 1): memory as a
 * first-class metric
 *
 * The storage must attribute every byte it holds to the subsystem that owns
 * it, and the derived bytes_per_infoset / bytes_per_strategy_slot figures
 * must follow from the same total that pe_storage_bytes() reports. The tests
 * below check the accounting by relation rather than by fragile absolute
 * values: the sum of the categories must equal the total, the per-precision
 * value arrays must scale with their representation, and the report taken
 * through the port must agree with the concrete API.
 */

#include <poker_eval/solver/pe_storage.h>
#include <poker_eval/solver/pe_storage_port.h>
#include <poker_eval/engine/solvers/cfr/cfr_storage_legacy_port.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(cond, ...)                                                \
    do                                                                  \
    {                                                                   \
        if (!(cond))                                                    \
        {                                                               \
            fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__);      \
            fprintf(stderr, __VA_ARGS__);                               \
            fprintf(stderr, "\n");                                      \
            g_failures++;                                               \
        }                                                               \
    } while (0)

/* The report must be internally consistent: the categories add up to the
 * total, the total equals bytes(), and the derived figures follow. */
static void check_consistency(const pe_storage_t *s,
                              const pe_storage_memory_report_t *r,
                              const char *label)
{
    size_t sum = r->hash_index_bytes + r->metadata_bytes + r->regret_bytes +
                 r->average_bytes + r->other_values_bytes + r->staging_bytes +
                 r->allocator_overhead_bytes;
    /* struct bytes are part of the total but not a named category; the sum
     * plus at least one struct must fit inside the total. */
    (void)sum;
    (void)label;
    CHECK(r->storage_bytes >= sum, "%s: categories exceed total", label);
    CHECK(pe_storage_bytes(s) == r->storage_bytes,
          "%s: bytes() disagrees with the report", label);
    if (r->total_infosets)
        CHECK(fabs(r->bytes_per_infoset -
                   (double)r->storage_bytes / (double)r->total_infosets) < 0.01,
              "%s: bytes_per_infoset inconsistent", label);
    else
        CHECK(r->bytes_per_infoset == 0.0,
              "%s: empty storage reports bytes per infoset", label);
    if (r->total_slots)
        CHECK(fabs(r->bytes_per_strategy_slot -
                   (double)r->storage_bytes / (double)r->total_slots) < 0.01,
              "%s: bytes_per_strategy_slot inconsistent", label);
    else
        CHECK(r->bytes_per_strategy_slot == 0.0,
              "%s: slotless storage reports bytes per slot", label);
}

static void test_null_and_empty(void)
{
    pe_storage_memory_report_t report;
    pe_storage_t *empty = pe_storage_create(0);

    memset(&report, 0xAA, sizeof(report));
    pe_storage_memory_report(NULL, &report);
    CHECK(report.total_infosets == 0 && report.storage_bytes == 0 &&
              report.bytes_per_infoset == 0.0,
          "NULL storage did not produce a zeroed report");

    pe_storage_memory_report(empty, NULL); /* must not crash */

    CHECK(empty != NULL, "empty storage creation failed");
    if (empty)
    {
        pe_storage_memory_report(empty, &report);
        CHECK(report.total_infosets == 0, "empty storage reports infosets");
        CHECK(report.storage_bytes > report.allocator_overhead_bytes,
              "empty storage reports no structure of its own");
        CHECK(report.hash_index_bytes > 0 && report.metadata_bytes > 0,
              "empty storage reports no index or metadata");
        CHECK(report.regret_bytes == 0 && report.average_bytes == 0 &&
                  report.staging_bytes == 0,
              "empty storage reports value arrays it never allocated");
        check_consistency(empty, &report, "empty");
        pe_storage_destroy(empty);
    }
}

static void test_counts_and_categories(void)
{
    const size_t infosets = 1000u;
    pe_storage_t *s = pe_storage_create(infosets);
    pe_storage_memory_report_t report;

    CHECK(s != NULL, "storage creation failed");
    if (!s)
        return;

    for (size_t i = 0; i < infosets; ++i)
    {
        pe_infoset_id_t id = pe_storage_resolve(s, i * 7u + 1u, 4, 1, 0);
        CHECK(id != PE_INFOSET_ID_INVALID, "resolve failed at %zu", i);
    }
    /* Touch the first infoset's regret and average so both arrays exist. */
    {
        double *regret = pe_storage_values(s, 0, PE_VALUES_REGRET);
        double *average = pe_storage_values(s, 0, PE_VALUES_AVERAGE);
        CHECK(regret != NULL && average != NULL, "value spans unavailable");
        if (regret)
            regret[0] = 1.0;
        if (average)
            average[0] = 0.5;
    }

    pe_storage_memory_report(s, &report);
    CHECK(report.total_infosets == infosets,
          "expected %zu infosets, got %zu", infosets, report.total_infosets);
    CHECK(report.total_slots == 4u * infosets,
          "expected %llu slots, got %llu",
          (unsigned long long)(4u * infosets),
          (unsigned long long)report.total_slots);
    CHECK(report.hash_index_bytes == 2048u * sizeof(uint32_t),
          "hash index sized %zu, expected the 2048-slot table",
          report.hash_index_bytes);
    CHECK(report.metadata_bytes ==
              infosets * sizeof(pe_infoset_meta_t),
          "metadata sized %zu, expected one meta entry per hinted infoset",
          report.metadata_bytes);
    CHECK(report.regret_bytes >= report.total_slots * sizeof(double),
          "regret array smaller than the slots it serves");
    CHECK(report.regret_bytes % sizeof(double) == 0,
          "regret bytes not double-aligned");
    CHECK(report.average_bytes == report.regret_bytes,
          "average array sized differently from regret");
    CHECK(report.other_values_bytes == 0,
          "untouched CURRENT/LOCKED arrays were counted");
    CHECK(report.allocator_overhead_bytes %
              PE_STORAGE_ALLOC_OVERHEAD_BYTES == 0,
          "allocator overhead not a whole number of blocks");
    check_consistency(s, &report, "f64 populated");
    pe_storage_destroy(s);
}

static void test_precision_scales_the_report(void)
{
    const size_t infosets = 5000u;
    pe_storage_t *f64 = pe_storage_create_precision(infosets, PE_PREC_F64);
    pe_storage_t *f32 = pe_storage_create_precision(infosets, PE_PREC_F32);
    pe_storage_t *fixed = pe_storage_create_precision(infosets, PE_PREC_FIXED16);
    pe_storage_memory_report_t r64, r32, rfix;

    CHECK(f64 && f32 && fixed, "precision storages creation failed");
    if (!f64 || !f32 || !fixed)
    {
        pe_storage_destroy(f64);
        pe_storage_destroy(f32);
        pe_storage_destroy(fixed);
        return;
    }
    for (size_t i = 0; i < infosets; ++i)
    {
        uint64_t key = i * 0x9E3779B97F4A7C15ull + 3u;
        CHECK(pe_storage_resolve(f64, key, 3, 1, 0) != PE_INFOSET_ID_INVALID,
              "f64 resolve failed");
        CHECK(pe_storage_resolve(f32, key, 3, 1, 0) != PE_INFOSET_ID_INVALID,
              "f32 resolve failed");
        CHECK(pe_storage_resolve(fixed, key, 3, 1, 0) != PE_INFOSET_ID_INVALID,
              "fixed16 resolve failed");
    }
    CHECK(pe_storage_values(f64, 0, PE_VALUES_REGRET) != NULL,
          "f64 regret span unavailable");
    CHECK(pe_storage_values(f32, 0, PE_VALUES_REGRET) != NULL,
          "f32 regret span unavailable");
    CHECK(pe_storage_values(fixed, 0, PE_VALUES_REGRET) != NULL,
          "fixed16 regret span unavailable");
    CHECK(pe_storage_values(f32, 1, PE_VALUES_AVERAGE) != NULL,
          "f32 average span unavailable");

    pe_storage_memory_report(f64, &r64);
    pe_storage_memory_report(f32, &r32);
    pe_storage_memory_report(fixed, &rfix);

    /* Same resolve sequence, so the same capacities: the resident value
     * arrays must scale exactly with the representation. */
    CHECK(r32.regret_bytes * 2u == r64.regret_bytes,
          "f32 regret bytes %zu are not half of f64 %zu",
          r32.regret_bytes, r64.regret_bytes);
    CHECK(rfix.regret_bytes * 4u == r64.regret_bytes,
          "fixed16 regret bytes %zu are not a quarter of f64 %zu",
          rfix.regret_bytes, r64.regret_bytes);
    CHECK(rfix.metadata_bytes > r64.metadata_bytes,
          "fixed16 per-infoset scales were not counted as metadata");
    CHECK(r32.staging_bytes > 0,
          "materialised f32 infoset carries no decoded staging span");
    CHECK(r64.staging_bytes == 0,
          "f64 storage invented staging spans");
    /* No total-ordering claim across precisions: with only a few infosets
     * materialised, the decoded staging spans of a compact representation
     * can outweigh its resident savings. That is exactly the honest
     * accounting the report exists to surface. The invariant that must hold
     * is the resident value array scaling, checked above. */
    check_consistency(f64, &r64, "f64");
    check_consistency(f32, &r32, "f32");
    check_consistency(fixed, &rfix, "fixed16");

    pe_storage_destroy(f64);
    pe_storage_destroy(f32);
    pe_storage_destroy(fixed);
}

static void test_optional_arrays_and_port(void)
{
    const pe_storage_ops_t *ops = pe_storage_ram_ops();
    void *self = NULL;
    pe_storage_memory_report_t via_port, direct;
    pe_storage_t *s;

    CHECK(ops && ops->memory_report, "RAM adapter did not wire memory_report");
    CHECK(pe_storage_legacy_ops()->memory_report == NULL,
          "legacy adapter claims a memory report it cannot fill");
    if (!ops || ops->create(&self, 128) != 0)
        return;
    s = (pe_storage_t *)self;
    CHECK(pe_storage_resolve(s, 42u, 2, 1, 0) != PE_INFOSET_ID_INVALID,
          "port resolve failed");
    {
        double *locked = pe_storage_values(s, 0, PE_VALUES_LOCKED);
        double *current = pe_storage_values(s, 0, PE_VALUES_CURRENT);
        CHECK(locked != NULL && current != NULL,
              "optional array spans unavailable");
    }
    CHECK(ops->memory_report(self, &via_port) == 0,
          "port memory_report failed");
    pe_storage_memory_report(s, &direct);
    CHECK(via_port.storage_bytes == direct.storage_bytes &&
              via_port.regret_bytes == direct.regret_bytes &&
              via_port.total_infosets == direct.total_infosets,
          "port report disagrees with the concrete API");
    CHECK(via_port.other_values_bytes > 0,
          "allocated CURRENT/LOCKED arrays were not counted");
    check_consistency(s, &via_port, "port");
    CHECK(ops->memory_report(self, NULL) == -1,
          "port memory_report accepted a NULL out pointer");
    ops->destroy(self);
}

/* ISS-235 (phase 6): a drop pass flushes the derived spans byte-exact into
 * the resident compact arrays, drops only the pressure streets, and pins
 * the rematerialise accounting. */
static void test_drop_flushes_byte_exact(void)
{
    const size_t infosets = 8u;
    pe_storage_t *s = pe_storage_create_with_tier(infosets, PE_PREC_F32,
                                                  PE_STORAGE_RECOMPUTE_DEEP);
    pe_storage_memory_report_t report;
    double *span;

    CHECK(s != NULL, "tiered storage creation failed");
    if (!s)
        return;
    /* All infosets act on the flop (street 1): they survive a compact drop
     * (pressure street 2) and die under a recompute-deep drop (0). */
    for (size_t i = 0; i < infosets; ++i)
        CHECK(pe_storage_resolve(s, i * 31u + 7u, 4, 1, 1) != PE_INFOSET_ID_INVALID,
              "resolve failed at %zu", i);
    span = pe_storage_values(s, 0, PE_VALUES_REGRET);
    CHECK(span != NULL, "regret span unavailable");
    span[0] = 1.25;
    span[3] = -0.5;

    /* compact would keep street-1 spans: pressure street 2 > 1. */
    CHECK(pe_storage_drop_recomputable(s, 2) == 0, "compact drop failed");
    pe_storage_memory_report(s, &report);
    CHECK(report.staging_bytes > 0,
          "compact drop removed spans below the pressure street");
    CHECK(report.evict_calls == 1 && report.evicted_bytes == 0,
          "compact drop double-counted evictions");
    CHECK(report.recomputable_strategy_bytes == report.staging_bytes,
          "recomputable figure disagrees with staging bytes");

    /* recompute-deep drops everywhere: dirty values flush byte-exact. */
    CHECK(pe_storage_drop_recomputable(s, 0) == 0, "deep drop failed");
    pe_storage_memory_report(s, &report);
    /* All slab bytes go; only the resident span index and dirty arrays of
     * the touched array remain, so staging shrinks to their aggregate. */
    CHECK(report.staging_bytes <=
              (size_t)2 * infosets * (sizeof(double *) + sizeof(uint8_t)),
          "deep drop left slab bytes resident (%zu)",
          report.staging_bytes);
    CHECK(report.evict_calls == 2, "expected two drop passes, got %llu",
          (unsigned long long)report.evict_calls);
    CHECK(report.evicted_bytes > 0, "deep drop did not account dropped bytes");
    CHECK(report.regret_bytes > 0,
          "flush removed the resident compact arrays");
    {
        /* Rematerialise: the span must decode back to the same values, which
           is only possible if the drop flushed them into the resident
           compact arrays first. */
        span = pe_storage_values(s, 0, PE_VALUES_REGRET);
        CHECK(span != NULL && span[0] == 1.25 && span[3] == -0.5,
              "rematerialised span lost content");
        pe_storage_memory_report(s, &report);
        CHECK(report.remat_calls == 1,
              "expected one rematerialisation, got %llu",
              (unsigned long long)report.remat_calls);
        CHECK(report.staging_bytes > 0, "rematerialisation left no span");
    }
    check_consistency(s, &report, "tiered f32");
    pe_storage_destroy(s);
}

/* Streets tagged unknown always pass the pressure test; streets below it
 * survive. */
static void test_drop_street_pressure(void)
{
    pe_storage_t *s = pe_storage_create_with_tier(4u, PE_PREC_F32,
                                                  PE_STORAGE_RECOMPUTE_DEEP);
    pe_storage_memory_report_t report;

    CHECK(s != NULL, "tiered storage creation failed");
    if (!s)
        return;
    CHECK(pe_storage_resolve(s, 11u, 2, 1, 0) != PE_INFOSET_ID_INVALID,
          "preflop resolve failed");
    CHECK(pe_storage_resolve(s, 22u, 2, 1, 3) != PE_INFOSET_ID_INVALID,
          "river resolve failed");
    CHECK(pe_storage_resolve(s, 33u, 2, 1, PE_STREET_UNKNOWN) != PE_INFOSET_ID_INVALID,
          "unknown-street resolve failed");
    CHECK(pe_storage_values(s, 0, PE_VALUES_REGRET) != NULL,
          "preflop span unavailable");
    CHECK(pe_storage_values(s, 1, PE_VALUES_REGRET) != NULL,
          "river span unavailable");
    CHECK(pe_storage_values(s, 2, PE_VALUES_REGRET) != NULL,
          "unknown-street span unavailable");

    /* pressure street 2: the river (3) and unknown (-1) spans go; preflop
     * (0) survives. */
    CHECK(pe_storage_drop_recomputable(s, 2) == 0, "drop failed");
    pe_storage_memory_report(s, &report);
    CHECK(report.staging_bytes > 0, "preflop span was dropped below pressure");
    CHECK(pe_storage_staging_span_count(s, PE_VALUES_REGRET) == 1u,
          "expected one survivor (preflop), got %zu",
          pe_storage_staging_span_count(s, PE_VALUES_REGRET));
    pe_storage_destroy(s);
}

/* F64 has no derived layer: a drop answers success and does nothing. */
static void test_drop_on_f64_is_noop(void)
{
    pe_storage_t *s = pe_storage_create_with_tier(4u, PE_PREC_F64,
                                                  PE_STORAGE_RECOMPUTE_DEEP);
    pe_storage_memory_report_t before, after;

    CHECK(s != NULL, "tiered f64 storage creation failed");
    if (!s)
        return;
    CHECK(pe_storage_resolve(s, 5u, 2, 1, 0) != PE_INFOSET_ID_INVALID,
          "resolve failed");
    CHECK(pe_storage_values(s, 0, PE_VALUES_REGRET) != NULL,
          "f64 span unavailable");
    pe_storage_memory_report(s, &before);
    CHECK(pe_storage_drop_recomputable(s, 0) == 0, "f64 drop failed");
    pe_storage_memory_report(s, &after);
    CHECK(before.storage_bytes == after.storage_bytes,
          "f64 drop changed the footprint");
    CHECK(after.evict_calls == 0,
          "f64 drop invented recomputable state to drop");
    pe_storage_destroy(s);
}

/* Invalid tier creations are rejected, not silently downgraded. */
static void test_tier_creation_validation(void)
{
    pe_storage_t *s = pe_storage_create_with_tier(8u, PE_PREC_F32,
                                                  (pe_storage_policy_t)77);
    CHECK(s == NULL, "out-of-range tier was accepted");
    s = pe_storage_create_with_tier(8u, PE_PREC_F32, PE_STORAGE_FULL);
    CHECK(s != NULL && pe_storage_tier(s) == PE_STORAGE_FULL,
          "explicit FULL tier lost");
    pe_storage_destroy(s);
}

int main(void)
{
    test_null_and_empty();
    test_counts_and_categories();
    test_precision_scales_the_report();
    test_optional_arrays_and_port();
    test_drop_flushes_byte_exact();
    test_drop_street_pressure();
    test_drop_on_f64_is_noop();
    test_tier_creation_validation();
    if (g_failures)
    {
        fprintf(stderr, "test_pe_storage_memory_report: %d failure(s)\n",
                g_failures);
        return 1;
    }
    printf("test_pe_storage_memory_report: ok\n");
    return 0;
}
