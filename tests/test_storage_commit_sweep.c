/* Issue #250: the commit sweep is bounded per span, and no write is lost.
 *
 * `pe_low_precision_commit_all` used to walk every allocated infoset on each
 * access and each resolve, which made FULL quadratic. The commit is now paid
 * by the span being touched -- by the accessor, the tier drop and the teardown,
 * each committing the span it is about to overwrite or free.
 *
 * That is only sound because the two guarantees below hold, so this test pins
 * them. They are the observable form of the rule `pe_low_precision_commit_one`
 * documents: the mark is retained after a flush because the caller may still
 * hold the decoded pointer, and `pe_storage_values` tells callers that a span
 * does not survive a resolve.
 */

#include <poker_eval/solver/pe_storage.h>

#include <math.h>
#include <stdio.h>

static int fail(const char *message)
{
    fprintf(stderr, "test_storage_commit_sweep: %s\n", message);
    return 1;
}

/* Both infosets must be droppable by a tier pass, so both are resolved at
   street 0 and the drop is asked for street 0. */
#define STREET 0

int main(void)
{
    pe_storage_t *f32 = pe_storage_create_precision(64u, PE_PREC_F32);
    pe_storage_t *fixed = pe_storage_create_precision(64u, PE_PREC_FIXED16);
    pe_infoset_id_t a, b;
    double *span_a;
    double *span_b;
    const double *read_back;

    if (!f32 || !fixed)
        return fail("storage creation failed");

    /* --- A write kept in a span survives another infoset's access ---------
       The accessor no longer flushes the whole storage, so A's span is the
       only place its mutation lives until A is touched again. */
    a = pe_storage_resolve(f32, 1u, 2, 1, STREET);
    b = pe_storage_resolve(f32, 2u, 2, 1, STREET);
    if (a == PE_INFOSET_ID_INVALID || b == PE_INFOSET_ID_INVALID)
        return fail("f32 resolve failed");
    span_a = pe_storage_values(f32, a, PE_VALUES_REGRET);
    if (!span_a)
        return fail("f32 span A unavailable");
    span_a[0] = 0.25;
    span_a[1] = -0.5;
    span_b = pe_storage_values(f32, b, PE_VALUES_REGRET);
    if (!span_b)
        return fail("f32 span B unavailable");
    span_b[0] = 0.75;
    read_back = pe_storage_values_const(f32, a, PE_VALUES_REGRET);
    if (!read_back || fabs(read_back[0] - 0.25) > 1e-9 ||
        fabs(read_back[1] + 0.5) > 1e-9)
        return fail("a write was lost when another infoset was accessed");
    read_back = pe_storage_values_const(f32, b, PE_VALUES_REGRET);
    if (!read_back || fabs(read_back[0] - 0.75) > 1e-9)
        return fail("a write to the second span was lost");

    /* --- A mutation made through a still-held pointer survives the flush ---
       The retained mark is what brings it back: nothing calls into the
       storage between the flush below (A's own read) and the late write. */
    span_a = pe_storage_values(f32, a, PE_VALUES_REGRET);
    if (!span_a)
        return fail("f32 span A unavailable on the second access");
    span_a[0] = 2.0; /* after A was flushed and decoded once already */
    read_back = pe_storage_values_const(f32, a, PE_VALUES_REGRET);
    if (!read_back || fabs(read_back[0] - 2.0) > 1e-9)
        return fail("a mutation through a held span was lost");
    if (pe_storage_staging_span_count(f32, PE_VALUES_REGRET) != 2u)
        return fail("f32 staging span count changed");

    /* --- The same, under a quantising representation and across a drop ----
       fixed16 flushes through a per-infoset scale; the drop frees the span
       after flushing it, and the next access re-materialises it. */
    a = pe_storage_resolve(fixed, 11u, 2, 1, STREET);
    b = pe_storage_resolve(fixed, 12u, 2, 1, STREET);
    if (a == PE_INFOSET_ID_INVALID || b == PE_INFOSET_ID_INVALID)
        return fail("fixed16 resolve failed");
    span_a = pe_storage_values(fixed, a, PE_VALUES_REGRET);
    span_b = pe_storage_values(fixed, b, PE_VALUES_REGRET);
    if (!span_a || !span_b)
        return fail("fixed16 spans unavailable");
    span_a[0] = 1.5;
    span_b[0] = -3.0;
    /* Flush A by reading it, then mutate it again through the held pointer. */
    read_back = pe_storage_values_const(fixed, a, PE_VALUES_REGRET);
    if (!read_back || fabs(read_back[0] - 1.5) > 0.001)
        return fail("fixed16 round-trip exceeded its quantization tolerance");
    span_a[0] = 7.25; /* the retained-mark case, once more */
    if (pe_storage_drop_recomputable(fixed, STREET) != 0)
        return fail("fixed16 tier drop failed");
    if (pe_storage_staging_span_count(fixed, PE_VALUES_REGRET) != 0u)
        return fail("the tier drop left a decoded span behind");
    read_back = pe_storage_values_const(fixed, a, PE_VALUES_REGRET);
    if (!read_back || fabs(read_back[0] - 7.25) > 0.001)
        return fail("a held-span mutation was lost across the tier drop");
    read_back = pe_storage_values_const(fixed, b, PE_VALUES_REGRET);
    if (!read_back || fabs(read_back[0] + 3.0) > 0.001)
        return fail("the second fixed16 infoset lost its value");

    printf("test_storage_commit_sweep: %zu f32 bytes, %zu fixed16 bytes\n",
           pe_storage_bytes(f32), pe_storage_bytes(fixed));
    pe_storage_destroy(f32);
    pe_storage_destroy(fixed);
    return 0;
}
