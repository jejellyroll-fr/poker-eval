/* preflop_deal_sampler.c - sampled correlated private deals (Lane B). */

#include <poker_eval/solver/pe_preflop_deal_sampler.h>

#include <float.h>
#include <math.h>
#include <string.h>

static int finite_positive(double value)
{
    return value > 0.0 && value <= DBL_MAX;
}

static int valid_variant(pe_preflop_variant_t variant, uint8_t hole_cards)
{
    return (variant == PE_PREFLOP_HOLDEM && hole_cards == 2u) ||
           (variant == PE_PREFLOP_PLO4 && hole_cards == 4u) ||
           (variant == PE_PREFLOP_PLO5 && hole_cards == 5u) ||
           (variant == PE_PREFLOP_PLO6 && hole_cards == 6u);
}

static int valid_common(mask_t board, uint8_t players)
{
    return mask_is_valid(board) && players > 0u &&
           players <= PE_PREFLOP_MAX_PLAYERS && mask_popcount(board) <= 5;
}

int pe_preflop_deal_sampler_init_holdem(
    pe_preflop_deal_sampler_t *out, mask_t board,
    const pe_holdem_range_t *ranges, uint8_t player_count)
{
    if (!out || !ranges || !valid_common(board, player_count)) return -1;
    memset(out, 0, sizeof(*out));
    out->variant = PE_PREFLOP_HOLDEM;
    out->board = board;
    out->player_count = player_count;
    out->hole_cards = 2u;
    out->ranges = ranges;
    return 0;
}

int pe_preflop_deal_sampler_init_omaha(
    pe_preflop_deal_sampler_t *out, mask_t board,
    const pe_omaha_range_t *ranges, uint8_t player_count,
    uint8_t hole_cards)
{
    pe_preflop_variant_t variant;
    if (hole_cards == 4u) variant = PE_PREFLOP_PLO4;
    else if (hole_cards == 5u) variant = PE_PREFLOP_PLO5;
    else if (hole_cards == 6u) variant = PE_PREFLOP_PLO6;
    else return -1;
    if (!out || !ranges || !valid_common(board, player_count) ||
        !valid_variant(variant, hole_cards))
        return -1;
    memset(out, 0, sizeof(*out));
    out->variant = variant;
    out->board = board;
    out->player_count = player_count;
    out->hole_cards = hole_cards;
    out->ranges = ranges;
    return 0;
}

int pe_preflop_deal_sampler_measure(const pe_preflop_deal_sampler_t *sampler,
                                    size_t *out_deal_count,
                                    double *out_weight_sum)
{
    if (!sampler || !sampler->ranges || !out_deal_count || !out_weight_sum)
        return -1;
    if (sampler->variant == PE_PREFLOP_HOLDEM)
        return pe_holdem_deals_measure(
            sampler->board, (const pe_holdem_range_t *)sampler->ranges,
            sampler->player_count, out_deal_count, out_weight_sum);
    return pe_omaha_deals_measure(
        sampler->board, (const pe_omaha_range_t *)sampler->ranges,
        sampler->player_count, sampler->hole_cards,
        out_deal_count, out_weight_sum);
}

static size_t range_count(const pe_preflop_deal_sampler_t *sampler,
                          uint8_t player)
{
    if (sampler->variant == PE_PREFLOP_HOLDEM)
        return ((const pe_holdem_range_t *)sampler->ranges)[player].count;
    return ((const pe_omaha_range_t *)sampler->ranges)[player].count;
}

static mask_t range_cards(const pe_preflop_deal_sampler_t *sampler,
                          uint8_t player, size_t index)
{
    if (sampler->variant == PE_PREFLOP_HOLDEM)
        return ((const pe_holdem_range_t *)sampler->ranges)[player]
            .combos[index].cards;
    return ((const pe_omaha_range_t *)sampler->ranges)[player]
        .combos[index].cards;
}

static double range_weight(const pe_preflop_deal_sampler_t *sampler,
                           uint8_t player, size_t index)
{
    if (sampler->variant == PE_PREFLOP_HOLDEM)
        return ((const pe_holdem_range_t *)sampler->ranges)[player]
            .combos[index].weight;
    return ((const pe_omaha_range_t *)sampler->ranges)[player]
        .combos[index].weight;
}

int pe_preflop_deal_sampler_sample(const pe_preflop_deal_sampler_t *sampler,
                                   pe_rng_t *rng,
                                   pe_preflop_deal_sample_t *out)
{
    mask_t used;
    double target = 1.0;
    double proposal = 1.0;
    if (!sampler || !sampler->ranges || !rng || !out ||
        !valid_common(sampler->board, sampler->player_count) ||
        !valid_variant(sampler->variant, sampler->hole_cards))
        return -1;
    memset(out, 0, sizeof(*out));
    used = sampler->board;
    for (uint8_t player = 0u; player < sampler->player_count; ++player)
    {
        double total = 0.0;
        double draw;
        double cumulative = 0.0;
        size_t selected = SIZE_MAX;
        size_t last_legal = SIZE_MAX;
        size_t count = range_count(sampler, player);
        for (size_t i = 0u; i < count; ++i)
        {
            mask_t cards = range_cards(sampler, player, i);
            double weight = range_weight(sampler, player, i);
            if (!mask_is_valid(cards) || mask_popcount(cards) != sampler->hole_cards ||
                mask_intersects(cards, used) || !finite_positive(weight))
                continue;
            total += weight;
        }
        if (!finite_positive(total)) return -1;
        draw = pe_rng_uniform01(rng) * total;
        for (size_t i = 0u; i < count; ++i)
        {
            mask_t cards = range_cards(sampler, player, i);
            double weight = range_weight(sampler, player, i);
            if (!mask_is_valid(cards) || mask_popcount(cards) != sampler->hole_cards ||
                mask_intersects(cards, used) || !finite_positive(weight))
                continue;
            last_legal = i;
            cumulative += weight;
            if (draw < cumulative)
            {
                selected = i;
                break;
            }
        }
        if (selected == SIZE_MAX) selected = last_legal;
        if (selected == SIZE_MAX) return -1;
        out->holes[player] = range_cards(sampler, player, selected);
        used |= out->holes[player];
        target *= range_weight(sampler, player, selected);
        proposal *= range_weight(sampler, player, selected) / total;
    }
    out->target_weight = target;
    out->proposal_probability = proposal;
    out->importance_ratio = sampler->reference_weight_sum > 0.0
        ? (target / sampler->reference_weight_sum) / proposal : 1.0;
    return finite_positive(out->importance_ratio) ? 0 : -1;
}
