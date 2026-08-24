/* Correlated preflop sampler: Hold'em and PLO4/5/6 card removal. */

#include <poker_eval/solver/pe_preflop_deal_sampler.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static mask_t cards(const int *values, size_t count)
{
    mask_t result = MASK_EMPTY;
    for (size_t i = 0u; i < count; ++i)
        result = mask_set(result, values[i]);
    return result;
}

static int check_sample(const pe_preflop_deal_sampler_t *sampler,
                        pe_rng_t *rng)
{
    pe_preflop_deal_sample_t sample;
    if (pe_preflop_deal_sampler_sample(sampler, rng, &sample) != 0)
        return -1;
    if (!(sample.proposal_probability > 0.0) ||
        !(sample.importance_ratio > 0.0) ||
        !isfinite(sample.importance_ratio))
        return -1;
    mask_t used = sampler->board;
    for (uint8_t p = 0u; p < sampler->player_count; ++p)
    {
        if (mask_popcount(sample.holes[p]) != sampler->hole_cards ||
            mask_intersects(used, sample.holes[p]))
            return -1;
        used |= sample.holes[p];
    }
    return 0;
}

int main(void)
{
    const int h0[] = {0, 13};
    const int h1[] = {1, 14};
    const int h2[] = {2, 15};
    pe_holdem_combo_t holdem_combos0[] = {{cards(h0, 2), 1.0}, {cards(h1, 2), 2.0}};
    pe_holdem_combo_t holdem_combos1[] = {{cards(h2, 2), 1.0}};
    pe_holdem_range_t holdem_ranges[] = {
        {holdem_combos0, 2u}, {holdem_combos1, 1u}};
    pe_preflop_deal_sampler_t sampler;
    pe_rng_t rng;
    size_t deal_count;
    double weight_sum;

    if (pe_preflop_deal_sampler_init_holdem(
            &sampler, MASK_EMPTY, holdem_ranges, 2u) != 0 ||
        pe_preflop_deal_sampler_measure(&sampler, &deal_count, &weight_sum) != 0 ||
        deal_count != 2u || fabs(weight_sum - 3.0) > 1e-12)
    {
        fprintf(stderr, "test_pe_preflop_sampler: Hold'em measure failed\n");
        return 1;
    }
    sampler.reference_weight_sum = weight_sum;
    pe_rng_seed(&rng, 0x1234u);
    for (int i = 0; i < 1000; ++i)
        if (check_sample(&sampler, &rng) != 0)
        {
            fprintf(stderr, "test_pe_preflop_sampler: Hold'em sample failed\n");
            return 1;
        }

    for (uint8_t hole_cards = 4u; hole_cards <= 6u; ++hole_cards)
    {
        int values0[6] = {0, 1, 2, 3, 4, 5};
        int values1[6] = {6, 7, 8, 9, 10, 11};
        pe_omaha_combo_t combo0 = {cards(values0, hole_cards), 1.0};
        pe_omaha_combo_t combo1 = {cards(values1, hole_cards), 1.0};
        pe_omaha_range_t ranges[] = {{&combo0, 1u}, {&combo1, 1u}};
        if (pe_preflop_deal_sampler_init_omaha(
                &sampler, MASK_EMPTY, ranges, 2u, hole_cards) != 0 ||
            pe_preflop_deal_sampler_measure(&sampler, &deal_count, &weight_sum) != 0 ||
            deal_count != 1u || fabs(weight_sum - 1.0) > 1e-12)
        {
            fprintf(stderr, "test_pe_preflop_sampler: PLO%u measure failed\n",
                    hole_cards);
            return 1;
        }
        sampler.reference_weight_sum = weight_sum;
        pe_rng_seed(&rng, 0x9000u + hole_cards);
        if (check_sample(&sampler, &rng) != 0)
        {
            fprintf(stderr, "test_pe_preflop_sampler: PLO%u sample failed\n",
                    hole_cards);
            return 1;
        }
    }
    puts("test_pe_preflop_sampler: Hold'em/PLO4/PLO5/PLO6 card removal passed");
    return 0;
}
