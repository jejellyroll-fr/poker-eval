/* ABS-04 / STO-03: fixed16 storage keeps a per-infoset scale, exposes the
 * existing double-span port, and re-normalizes instead of clipping overflow. */

#include <poker_eval/solver/pe_storage.h>

#include <math.h>
#include <stdio.h>

static int fail(const char *message)
{
    fprintf(stderr, "test_pe_storage_precision: %s\n", message);
    return 1;
}

int main(void)
{
    const size_t infosets = 10000u;
    pe_storage_t *f64 = pe_storage_create(infosets);
    pe_storage_t *fixed = pe_storage_create_precision(infosets, PE_PREC_FIXED16);
    pe_infoset_id_t first = PE_INFOSET_ID_INVALID;

    if (!f64 || !fixed)
        return fail("storage creation failed");
    if (pe_storage_precision(fixed) != PE_PREC_FIXED16)
        return fail("fixed16 precision was not retained");

    for (size_t i = 0; i < infosets; ++i)
    {
        pe_infoset_id_t a = pe_storage_resolve(f64, i * 17u + 3u, 4, 1, 0);
        pe_infoset_id_t b = pe_storage_resolve(fixed, i * 17u + 3u, 4, 1, 0);
        if (a == PE_INFOSET_ID_INVALID || b == PE_INFOSET_ID_INVALID)
            return fail("resolve failed");
        if (i == 0)
            first = b;
    }

    double *values = pe_storage_values(fixed, first, PE_VALUES_REGRET);
    if (!values)
        return fail("fixed16 values span unavailable");
    if (!pe_storage_values(f64, first, PE_VALUES_REGRET))
        return fail("f64 values span unavailable");
    values[0] = 1.25;
    values[1] = -2.5;
    values[2] = 0.125;
    values[3] = 0.0;
    const double *round_trip = pe_storage_values_const(fixed, first, PE_VALUES_REGRET);
    if (!round_trip || fabs(round_trip[0] - 1.25) > 0.001 ||
        fabs(round_trip[1] + 2.5) > 0.001 ||
        fabs(round_trip[2] - 0.125) > 0.001)
        return fail("fixed16 round-trip exceeded quantization tolerance");

    values = pe_storage_values(fixed, first, PE_VALUES_REGRET);
    values[0] = 1000000000.0;
    round_trip = pe_storage_values_const(fixed, first, PE_VALUES_REGRET);
    if (!round_trip || fabs(round_trip[0] - 1000000000.0) > 100000.0 ||
        pe_storage_fixed16_rescales(fixed) == 0u)
        return fail("fixed16 overflow was clipped instead of rescaled");

    size_t f64_bytes = pe_storage_bytes(f64);
    size_t fixed_bytes = pe_storage_bytes(fixed);
    if (!(fixed_bytes < f64_bytes))
    {
        fprintf(stderr, "fixed16 resident storage did not shrink (%zu vs %zu)\n",
                fixed_bytes, f64_bytes);
        return 1;
    }

    printf("test_pe_storage_precision: %zu -> %zu bytes, %zu rescale(s)\n",
           f64_bytes, fixed_bytes, pe_storage_fixed16_rescales(fixed));
    pe_storage_destroy(fixed);
    pe_storage_destroy(f64);
    return 0;
}
