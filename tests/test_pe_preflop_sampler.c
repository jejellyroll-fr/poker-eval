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

    /* A legal first-player draw can block every hand in the next range.
       The sampler must exclude that dead-end prefix while retaining the
       correct importance weight for the valid branch. */
    {
        pe_holdem_combo_t blocked[] = {
            {cards((int[]){0, 1}, 2u), 1.0},
            {cards((int[]){2, 3}, 2u), 2.0}};
        pe_holdem_combo_t only_unblocked[] = {
            {cards((int[]){0, 4}, 2u), 1.0}};
        pe_holdem_range_t ranges[] = {
            {blocked, 2u}, {only_unblocked, 1u}};
        if (pe_preflop_deal_sampler_init_holdem(
                &sampler, MASK_EMPTY, ranges, 2u) != 0)
        {
            fprintf(stderr, "test_pe_preflop_sampler: dead-end init failed\n");
            return 1;
        }
        pe_rng_seed(&rng, 0xdeadbeefu);
        for (int i = 0; i < 100; ++i)
        {
            pe_preflop_deal_sample_t sample;
            if (pe_preflop_deal_sampler_sample(&sampler, &rng, &sample) != 0 ||
                sample.holes[0] != blocked[1].cards ||
                fabs(sample.importance_ratio - 2.0) > 1e-12)
            {
                fprintf(stderr,
                        "test_pe_preflop_sampler: dead-end resampling failed\n");
                return 1;
            }
        }
    }
    /* Complete ranges (NULL range array): every player holds any hand.
       This is the only way 5- and 6-card Omaha is solvable -- a full PLO6
       range is C(52,6) = 20,358,520 combos -- so it must agree exactly with
       the enumerated path, not merely be close.

       Hold'em is the case where both paths can be built, so it is the one
       that pins the equivalence: an explicit range holding all 1326 combos
       at weight 1 must produce the same importance ratio as the complete
       draw, namely C(52,2) * C(50,2) = 1326 * 1225. */
    {
        pe_holdem_combo_t all[1326];
        pe_holdem_range_t enumerated[2];
        pe_preflop_deal_sampler_t complete;
        size_t n = 0u;
        double expected_ratio = 1326.0 * 1225.0;

        for (int a = 0; a < 52; ++a)
            for (int b = a + 1; b < 52; ++b)
            {
                all[n].cards = mask_set(mask_set(MASK_EMPTY, a), b);
                all[n].weight = 1.0;
                ++n;
            }
        if (n != 1326u)
        {
            fprintf(stderr, "test_pe_preflop_sampler: combo count %zu\n", n);
            return 1;
        }
        enumerated[0].combos = all; enumerated[0].count = n;
        enumerated[1].combos = all; enumerated[1].count = n;

        if (pe_preflop_deal_sampler_init_holdem(&complete, MASK_EMPTY, NULL, 2u) != 0 ||
            complete.complete_ranges != 1u)
        {
            fprintf(stderr, "test_pe_preflop_sampler: complete init failed\n");
            return 1;
        }
        /* Exact deal count and weight sum, with no list to walk. */
        if (pe_preflop_deal_sampler_measure(&complete, &deal_count, &weight_sum) != 0 ||
            deal_count != (size_t)expected_ratio ||
            fabs(weight_sum - expected_ratio) > 1e-6)
        {
            fprintf(stderr,
                    "test_pe_preflop_sampler: complete measure %zu / %.1f\n",
                    deal_count, weight_sum);
            return 1;
        }
        pe_rng_seed(&rng, 0x5EEDu);
        for (int i = 0; i < 500; ++i)
        {
            pe_preflop_deal_sample_t sample;
            if (pe_preflop_deal_sampler_sample(&complete, &rng, &sample) != 0 ||
                check_sample(&complete, &rng) != 0 ||
                fabs(sample.importance_ratio - expected_ratio) > 1e-6)
            {
                fprintf(stderr,
                        "test_pe_preflop_sampler: complete ratio mismatch\n");
                return 1;
            }
        }
        /* And the enumerated path over the same range agrees. */
        if (pe_preflop_deal_sampler_init_holdem(
                &sampler, MASK_EMPTY, enumerated, 2u) != 0)
        {
            fprintf(stderr, "test_pe_preflop_sampler: enumerated init failed\n");
            return 1;
        }
        for (int i = 0; i < 20; ++i)
        {
            pe_preflop_deal_sample_t sample;
            if (pe_preflop_deal_sampler_sample(&sampler, &rng, &sample) != 0 ||
                fabs(sample.importance_ratio - expected_ratio) > 1e-6)
            {
                fprintf(stderr,
                        "test_pe_preflop_sampler: enumerated ratio %.6f != %.1f\n",
                        sample.importance_ratio, expected_ratio);
                return 1;
            }
        }
        /* A dead board shrinks the live deck for both the count and the
           ratio: C(49,2) * C(47,2) once three cards are gone. */
        {
            mask_t board = cards((int[]){0, 1, 2}, 3u);
            double flop_ratio = (49.0 * 48.0 / 2.0) * (47.0 * 46.0 / 2.0);
            if (pe_preflop_deal_sampler_init_holdem(&complete, board, NULL, 2u) != 0)
            {
                fprintf(stderr, "test_pe_preflop_sampler: board init failed\n");
                return 1;
            }
            for (int i = 0; i < 200; ++i)
            {
                pe_preflop_deal_sample_t sample;
                if (pe_preflop_deal_sampler_sample(&complete, &rng, &sample) != 0 ||
                    mask_intersects(sample.holes[0], board) ||
                    mask_intersects(sample.holes[1], board) ||
                    mask_intersects(sample.holes[0], sample.holes[1]) ||
                    fabs(sample.importance_ratio - flop_ratio) > 1e-6)
                {
                    fprintf(stderr,
                            "test_pe_preflop_sampler: complete board draw failed\n");
                    return 1;
                }
            }
        }
        /* PLO6 six-handed needs 36 hole cards plus the board: still legal.
           Seven-handed does not fit and must be refused, not truncated. */
        {
            pe_preflop_deal_sampler_t plo6;
            if (pe_preflop_deal_sampler_init_omaha(&plo6, MASK_EMPTY, NULL, 6u, 6u) != 0)
            {
                fprintf(stderr, "test_pe_preflop_sampler: PLO6 6-max init failed\n");
                return 1;
            }
            pe_rng_seed(&rng, 0xB16u);
            for (int i = 0; i < 200; ++i)
                if (check_sample(&plo6, &rng) != 0)
                {
                    fprintf(stderr, "test_pe_preflop_sampler: PLO6 6-max draw failed\n");
                    return 1;
                }
            /* Eight-handed PLO6 needs 48 cards, which fits a full deck but
               not one with a five-card board dead (47 live).  Refused, not
               silently truncated. */
            if (pe_preflop_deal_sampler_init_omaha(&plo6, MASK_EMPTY, NULL, 8u, 6u) != 0)
            {
                fprintf(stderr,
                        "test_pe_preflop_sampler: 48 of 52 cards should fit\n");
                return 1;
            }
            if (pe_preflop_deal_sampler_init_omaha(
                    &plo6, cards((int[]){0, 1, 2, 3, 4}, 5u), NULL, 8u, 6u) == 0)
            {
                fprintf(stderr,
                        "test_pe_preflop_sampler: 48 hole cards + 5 board should not fit\n");
                return 1;
            }
        }
    }

    puts("test_pe_preflop_sampler: Hold'em/PLO4/PLO5/PLO6 card removal passed");
    puts("test_pe_preflop_sampler: complete ranges match the enumerated path");
    return 0;
}
