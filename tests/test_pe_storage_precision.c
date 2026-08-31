/* ABS-04 / STO-03: compact storage keeps resident values in the selected
 * representation, exposes the existing double-span port, and preserves
 * independent spans for multiple infosets. */

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
    pe_storage_t *f32 = pe_storage_create_precision(infosets, PE_PREC_F32);
    pe_storage_t *fixed = pe_storage_create_precision(infosets, PE_PREC_FIXED16);
    pe_infoset_id_t first = PE_INFOSET_ID_INVALID;
    pe_infoset_id_t second = PE_INFOSET_ID_INVALID;

    if (!f64 || !f32 || !fixed)
        return fail("storage creation failed");
    if (pe_storage_precision(f32) != PE_PREC_F32)
        return fail("f32 precision was not retained");
    if (pe_storage_precision(fixed) != PE_PREC_FIXED16)
        return fail("fixed16 precision was not retained");

    for (size_t i = 0; i < infosets; ++i)
    {
        pe_infoset_id_t a = pe_storage_resolve(f64, i * 17u + 3u, 4, 1, 0);
        pe_infoset_id_t c = pe_storage_resolve(f32, i * 17u + 3u, 4, 1, 0);
        pe_infoset_id_t b = pe_storage_resolve(fixed, i * 17u + 3u, 4, 1, 0);
        if (a == PE_INFOSET_ID_INVALID || c == PE_INFOSET_ID_INVALID ||
            b == PE_INFOSET_ID_INVALID)
            return fail("resolve failed");
        if (i == 0)
            first = b;
    }
    second = pe_storage_resolve(f32, 20u, 4, 1, 0);
    if (second == PE_INFOSET_ID_INVALID)
        return fail("second f32 resolve failed");

    double *values = pe_storage_values(fixed, first, PE_VALUES_REGRET);
    if (!values)
        return fail("fixed16 values span unavailable");
    if (!pe_storage_values(f64, first, PE_VALUES_REGRET))
        return fail("f64 values span unavailable");

    double *f32_first = pe_storage_values(f32, 0u, PE_VALUES_REGRET);
    double *f32_second = pe_storage_values(f32, second, PE_VALUES_REGRET);
    if (!f32_first || !f32_second)
        return fail("f32 values spans unavailable");
    f32_first[0] = 1.0 / 3.0;
    f32_second[0] = 2.0 / 3.0;
    const double *f32_first_read =
        pe_storage_values_const(f32, 0u, PE_VALUES_REGRET);
    const double *f32_second_read =
        pe_storage_values_const(f32, second, PE_VALUES_REGRET);
    if (!f32_first_read || !f32_second_read ||
        fabs(f32_first_read[0] - (1.0 / 3.0)) > 1e-6 ||
        fabs(f32_second_read[0] - (2.0 / 3.0)) > 1e-6)
        return fail("f32 infoset spans aliased or lost their updates");

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
    size_t f32_bytes = pe_storage_bytes(f32);
    size_t fixed_bytes = pe_storage_bytes(fixed);
    if (!(f32_bytes < f64_bytes) || !(fixed_bytes < f64_bytes))
    {
        fprintf(stderr, "compact storage did not shrink (%zu, %zu vs %zu)\n",
                f32_bytes, fixed_bytes, f64_bytes);
        return 1;
    }

    printf("test_pe_storage_precision: %zu -> %zu/%zu bytes, %zu rescale(s)\n",
           f64_bytes, f32_bytes, fixed_bytes, pe_storage_fixed16_rescales(fixed));
    pe_storage_destroy(f32);
    pe_storage_destroy(fixed);
    pe_storage_destroy(f64);
    return 0;
}
