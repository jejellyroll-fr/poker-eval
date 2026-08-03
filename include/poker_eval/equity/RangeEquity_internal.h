#ifndef RANGE_EQUITY_INTERNAL_H
#define RANGE_EQUITY_INTERNAL_H

#include <poker_eval/core/enumdefs.h>
#include <math.h>
#include <string.h>

typedef struct {
    double total_weight;
    double total_weighted_samples;
    double ev[ENUM_MAXPLAYERS];
    double nwinhi[ENUM_MAXPLAYERS];
    double ntiehi[ENUM_MAXPLAYERS];
    double nlosehi[ENUM_MAXPLAYERS];
    double nwinlo[ENUM_MAXPLAYERS];
    double ntielo[ENUM_MAXPLAYERS];
    double nloselo[ENUM_MAXPLAYERS];
    double nscoop[ENUM_MAXPLAYERS];
    /* Weighted counterparts of enum_result_t's share histograms:
     * nsharehi[i][H] is how often player i tied for the best high hand with H
     * players in total (H = 0 meaning no share). Carrying them through the
     * aggregation is what lets pe_equity report equity_hi / equity_lo for a
     * range; without them both come out zero. */
    double nsharehi[ENUM_MAXPLAYERS][ENUM_MAXPLAYERS + 1];
    double nsharelo[ENUM_MAXPLAYERS][ENUM_MAXPLAYERS + 1];
} WeightedAggregation;

static inline void weightedAggregationInit(WeightedAggregation* agg, int num_players) {
    (void)num_players; // kept for future assertions
    agg->total_weight = 0.0;
    agg->total_weighted_samples = 0.0;
    memset(agg->ev, 0, sizeof(double) * ENUM_MAXPLAYERS);
    memset(agg->nwinhi, 0, sizeof(double) * ENUM_MAXPLAYERS);
    memset(agg->ntiehi, 0, sizeof(double) * ENUM_MAXPLAYERS);
    memset(agg->nlosehi, 0, sizeof(double) * ENUM_MAXPLAYERS);
    memset(agg->nwinlo, 0, sizeof(double) * ENUM_MAXPLAYERS);
    memset(agg->ntielo, 0, sizeof(double) * ENUM_MAXPLAYERS);
    memset(agg->nloselo, 0, sizeof(double) * ENUM_MAXPLAYERS);
    memset(agg->nscoop, 0, sizeof(double) * ENUM_MAXPLAYERS);
    memset(agg->nsharehi, 0, sizeof(agg->nsharehi));
    memset(agg->nsharelo, 0, sizeof(agg->nsharelo));
}

static inline void weightedAggregationAccumulate(
    WeightedAggregation* agg,
    const enum_result_t* matchup_result,
    double weight,
    int num_players
) {
    if (!agg || !matchup_result || matchup_result->nsamples == 0 || weight <= 0.0)
        return;

    double weighted_samples = weight * (double)matchup_result->nsamples;
    agg->total_weight += weight;
    agg->total_weighted_samples += weighted_samples;

    for (int p = 0; p < num_players; ++p) {
        double avg_ev = matchup_result->ev[p] / matchup_result->nsamples;
        agg->ev[p] += avg_ev * weight;
        agg->nwinhi[p] += weight * matchup_result->nwinhi[p];
        agg->ntiehi[p] += weight * matchup_result->ntiehi[p];
        agg->nlosehi[p] += weight * matchup_result->nlosehi[p];
        agg->nwinlo[p] += weight * matchup_result->nwinlo[p];
        agg->ntielo[p] += weight * matchup_result->ntielo[p];
        agg->nloselo[p] += weight * matchup_result->nloselo[p];
        agg->nscoop[p] += weight * matchup_result->nscoop[p];
        for (int h = 0; h <= num_players; ++h) {
            agg->nsharehi[p][h] += weight * matchup_result->nsharehi[p][h];
            agg->nsharelo[p][h] += weight * matchup_result->nsharelo[p][h];
        }
    }
}

static inline void weightedAggregationMerge(
    WeightedAggregation* dest,
    const WeightedAggregation* src,
    int num_players
) {
    if (!dest || !src)
        return;

    dest->total_weight += src->total_weight;
    dest->total_weighted_samples += src->total_weighted_samples;

    for (int p = 0; p < num_players; ++p) {
        dest->ev[p] += src->ev[p];
        dest->nwinhi[p] += src->nwinhi[p];
        dest->ntiehi[p] += src->ntiehi[p];
        dest->nlosehi[p] += src->nlosehi[p];
        dest->nwinlo[p] += src->nwinlo[p];
        dest->ntielo[p] += src->ntielo[p];
        dest->nloselo[p] += src->nloselo[p];
        dest->nscoop[p] += src->nscoop[p];
        for (int h = 0; h <= num_players; ++h) {
            dest->nsharehi[p][h] += src->nsharehi[p][h];
            dest->nsharelo[p][h] += src->nsharelo[p][h];
        }
    }
}

static inline void distribute_probabilities(
    const double probs[],
    unsigned int counts[],
    int len,
    unsigned int target_total
) {
    if (target_total == 0) {
        for (int i = 0; i < len; ++i) {
            counts[i] = 0;
        }
        return;
    }

    /* Sized for the largest caller, the share histogram over 0..num_players
     * buckets. The scratch used to be a fixed double[8] while the function
     * already took an arbitrary len. */
    double fractions[ENUM_MAXPLAYERS + 1] = {0};
    if (len > (int)(sizeof(fractions) / sizeof(fractions[0])))
        return;
    unsigned int allocated = 0;

    for (int i = 0; i < len; ++i) {
        double clamped_prob = probs[i] < 0.0 ? 0.0 : probs[i];
        double scaled = clamped_prob * target_total;
        if (scaled < 0.0)
            scaled = 0.0;
        double floored = floor(scaled);
        unsigned int assigned = (floored < 0.0) ? 0U : (unsigned int)floored;
        counts[i] = assigned;
        fractions[i] = scaled - (double)assigned;
        allocated += assigned;
    }

    unsigned int remaining = (allocated > target_total) ? 0 : target_total - allocated;
    while (remaining > 0) {
        int best_idx = 0;
        double best_fraction = fractions[0];
        for (int i = 1; i < len; ++i) {
            if (fractions[i] > best_fraction) {
                best_fraction = fractions[i];
                best_idx = i;
            }
        }
        counts[best_idx]++;
        fractions[best_idx] = 0.0;
        --remaining;
    }
}

static inline void weightedAggregationFinalizePlayer(
    const WeightedAggregation* agg,
    enum_result_t* aggregated_results,
    int player_index,
    int num_players,
    unsigned int target_samples
) {
    if (!aggregated_results) {
        return;
    }

    if (!agg || agg->total_weighted_samples <= 0.0 || target_samples == 0) {
        aggregated_results->nwinhi[player_index] = 0;
        aggregated_results->ntiehi[player_index] = 0;
        aggregated_results->nlosehi[player_index] = 0;
        aggregated_results->nwinlo[player_index] = 0;
        aggregated_results->ntielo[player_index] = 0;
        aggregated_results->nloselo[player_index] = 0;
        aggregated_results->nscoop[player_index] = 0;
        memset(aggregated_results->nsharehi[player_index], 0,
               sizeof(aggregated_results->nsharehi[player_index]));
        memset(aggregated_results->nsharelo[player_index], 0,
               sizeof(aggregated_results->nsharelo[player_index]));
        return;
    }

    double inv_total_samples = 1.0 / agg->total_weighted_samples;

    double hi_probs[3] = {
        agg->nwinhi[player_index] * inv_total_samples,
        agg->ntiehi[player_index] * inv_total_samples,
        agg->nlosehi[player_index] * inv_total_samples
    };
    unsigned int hi_counts[3];
    distribute_probabilities(hi_probs, hi_counts, 3, target_samples);
    aggregated_results->nwinhi[player_index] = hi_counts[0];
    aggregated_results->ntiehi[player_index] = hi_counts[1];
    aggregated_results->nlosehi[player_index] = hi_counts[2];

    double lo_probs[3] = {
        agg->nwinlo[player_index] * inv_total_samples,
        agg->ntielo[player_index] * inv_total_samples,
        agg->nloselo[player_index] * inv_total_samples
    };
    unsigned int lo_counts[3];
    distribute_probabilities(lo_probs, lo_counts, 3, target_samples);
    aggregated_results->nwinlo[player_index] = lo_counts[0];
    aggregated_results->ntielo[player_index] = lo_counts[1];
    aggregated_results->nloselo[player_index] = lo_counts[2];

    double scoop_prob = agg->nscoop[player_index] * inv_total_samples;
    long long rounded_scoop = llround(scoop_prob * target_samples);
    if (rounded_scoop < 0)
        rounded_scoop = 0;
    unsigned int scoop_count = (unsigned int)rounded_scoop;
    if (scoop_count > aggregated_results->nwinhi[player_index]) {
        scoop_count = aggregated_results->nwinhi[player_index];
    }
    aggregated_results->nscoop[player_index] = scoop_count;

    /* Share histograms: one bucket per possible number of players sharing the
     * pot, 0..num_players, so the row sums to the sample count exactly like
     * the win/tie/lose triples above. */
    int share_buckets = num_players + 1;
    double share_probs[ENUM_MAXPLAYERS + 1];
    unsigned int share_counts[ENUM_MAXPLAYERS + 1];

    memset(aggregated_results->nsharehi[player_index], 0,
           sizeof(aggregated_results->nsharehi[player_index]));
    memset(aggregated_results->nsharelo[player_index], 0,
           sizeof(aggregated_results->nsharelo[player_index]));

    for (int h = 0; h < share_buckets; ++h)
        share_probs[h] = agg->nsharehi[player_index][h] * inv_total_samples;
    distribute_probabilities(share_probs, share_counts, share_buckets, target_samples);
    for (int h = 0; h < share_buckets; ++h)
        aggregated_results->nsharehi[player_index][h] = share_counts[h];

    for (int h = 0; h < share_buckets; ++h)
        share_probs[h] = agg->nsharelo[player_index][h] * inv_total_samples;
    distribute_probabilities(share_probs, share_counts, share_buckets, target_samples);
    for (int h = 0; h < share_buckets; ++h)
        aggregated_results->nsharelo[player_index][h] = share_counts[h];
}

static inline unsigned int weightedAggregationFinalize(
    const WeightedAggregation* agg,
    enum_result_t* aggregated_results,
    int num_players
) {
    if (!agg || !aggregated_results)
        return 0;

    if (agg->total_weight > 0.0) {
        for (int p = 0; p < num_players; ++p) {
            aggregated_results->ev[p] = agg->ev[p] / agg->total_weight;
        }
    } else {
        for (int p = 0; p < num_players; ++p) {
            aggregated_results->ev[p] = 0.0;
        }
    }

    unsigned int target_samples = 0;
    if (agg->total_weighted_samples > 0.0) {
        long long rounded_total = llround(agg->total_weighted_samples);
        if (rounded_total < 0)
            rounded_total = 0;
        target_samples = (unsigned int)rounded_total;
    }
    for (int p = 0; p < num_players; ++p) {
        weightedAggregationFinalizePlayer(agg, aggregated_results, p, num_players, target_samples);
    }
    return target_samples;
}

/**
 * @brief Compute a per-matchup Monte-Carlo iteration budget from a total budget.
 *
 * `iterations_if_montecarlo` is treated as a TOTAL budget for the whole
 * range-vs-range call, not a per-matchup budget. Evaluating every matchup with
 * the full budget would multiply the cost by the number of matchups (the
 * product of the per-player valid-combo counts), which hangs for wide ranges.
 *
 * @param iterations_if_montecarlo The caller's total iteration budget (> 0).
 * @param matchup_estimate         Upper bound on the number of valid matchups
 *                                 (product of per-player valid-combo counts).
 * @return Per-matchup iteration count, always >= 1.
 */
static inline int range_equity_per_matchup_budget(int iterations_if_montecarlo,
                                                  double matchup_estimate)
{
    int per = iterations_if_montecarlo > 0 ? iterations_if_montecarlo : 200000;
    if (matchup_estimate > 1.0)
    {
        per = per / (int)matchup_estimate;
    }
    if (per < 1)
        per = 1;
    return per;
}

#endif // RANGE_EQUITY_INTERNAL_H
