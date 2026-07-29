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

    double fractions[8] = {0};
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
    unsigned int target_samples
) {
    if (!agg || !aggregated_results || agg->total_weighted_samples <= 0.0 || target_samples == 0) {
        aggregated_results->nwinhi[player_index] = 0;
        aggregated_results->ntiehi[player_index] = 0;
        aggregated_results->nlosehi[player_index] = 0;
        aggregated_results->nwinlo[player_index] = 0;
        aggregated_results->ntielo[player_index] = 0;
        aggregated_results->nloselo[player_index] = 0;
        aggregated_results->nscoop[player_index] = 0;
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
        weightedAggregationFinalizePlayer(agg, aggregated_results, p, target_samples);
    }
    return target_samples;
}

#endif // RANGE_EQUITY_INTERNAL_H
