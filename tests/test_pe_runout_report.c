/* Exact conditional runout report: flop -> turn/river. */

#include <poker_eval/solver/pe_runout_report.h>

#include <math.h>
#include <stdio.h>

static int count_row(const pe_runout_report_row_t *row, void *user)
{
    size_t *count = user;
    if (!row || row->probability <= 0.0 || mask_popcount(row->board) != 5)
        return 1;
    ++*count;
    return 0;
}

int main(void)
{
    mask_t flop = MASK_EMPTY;
    size_t callback_count = 0u;
    size_t count = 0u;
    double probability_sum = 0.0;
    flop = mask_set(mask_set(mask_set(flop, 0), 13), 26);
    if (pe_holdem_runout_report_generate(
            flop, mask_set(mask_set(MASK_EMPTY, 1), 14), count_row,
            &callback_count, &count, &probability_sum) != 0 ||
        count != 1081u || callback_count != count ||
        fabs(probability_sum - 1.0) > 1e-12)
    {
        fprintf(stderr, "test_pe_runout_report: exact runouts failed\n");
        return 1;
    }
    puts("test_pe_runout_report: exact conditional runouts passed");
    return 0;
}
