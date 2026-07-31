#include <poker_eval/equity/RangeEquity.h>
#include <poker_eval/equity/enumord.h> // For enum_ordering_mode_t for enumResultAlloc
#include <poker_eval/core/enumerate.h> // For enumSample, enumExhaustive, enumResultAlloc, enumResultFree, enumResultClear
#include <poker_eval/equity/batched_montecarlo.h>
#include <poker_eval/equity/RangeEquity_internal.h>
#include <poker_eval/equity/range_combo_buffers.h>
#include <stdio.h>   // For TRACE_RE if not fully optimized out
#include <string.h>  // For memset (though enumResultClear handles most result init)
#include <stdlib.h>  // For getenv

// Define TRACE_RE if not already defined
#ifndef TRACE_RE
#define TRACE_RE(...) fprintf(stderr, __VA_ARGS__)
#endif

static int range_equity_debug_enabled(void)
{
    static int cached = -1;
    if (cached == -1)
    {
        const char *env = getenv("POKER_DEBUG_RANGE");
        cached = (env && *env) ? 1 : 0;
    }
    return cached;
}

#define RANGE_DEBUG(...)                       \
    do                                         \
    {                                          \
        if (range_equity_debug_enabled())      \
            fprintf(stderr, __VA_ARGS__);      \
    } while (0)

static int range_combo_stats_enabled(void)
{
    static int cached = -1;
    if (cached == -1)
    {
        const char *env = getenv("PE_RANGE_COMBO_TRACE");
        cached = (env && *env) ? 1 : 0;
    }
    return cached || range_equity_debug_enabled();
}

static void range_combo_log_stats(const char *label,
                                  int player_idx,
                                  const range_combo_buffer_t *buffer)
{
    if (!range_combo_stats_enabled() || !buffer)
        return;
    TRACE_RE("%s P%d: total=%d playable=%d total_w=%.2f playable_w=%.2f\n",
             label ? label : "RangeEquity",
             player_idx,
             buffer->stats.total_combos,
             buffer->stats.playable_combos,
             buffer->stats.total_weight,
             buffer->stats.playable_weight);
}

// Helper function to validate a hand against board and dead cards
static bool is_hand_valid(StdDeck_CardMask hand, StdDeck_CardMask board, StdDeck_CardMask dead_cards) {
    StdDeck_CardMask combined;
    StdDeck_CardMask_RESET(combined);
    StdDeck_CardMask_OR(combined, board, dead_cards);
    return !StdDeck_CardMask_ANY_SET(hand, combined);
}

// Helper function to count valid hands in a range and accumulate their total weight
static int count_valid_hands_in_range(
    const PlayerRange* range,
    StdDeck_CardMask board,
    StdDeck_CardMask dead_cards,
    double* out_total_weight
) {
    int valid_count = 0;
    double weight_sum = 0.0;
    for (int i = 0; i < range->count; ++i) {
        double weight = range->weights ? range->weights[i] : 1.0;
        if (weight == 0.0)
            continue;
        if (is_hand_valid(range->hand_masks[i], board, dead_cards)) {
            valid_count++;
            weight_sum += weight;
            RANGE_DEBUG("RangeEquity: hand %d valid (weight %.3f)\n", i, weight);
        } else {
            RANGE_DEBUG("RangeEquity: hand %d conflicts with board/dead cards\n", i);
        }
    }
    if (out_total_weight) {
        *out_total_weight = weight_sum;
    }
    return valid_count;
}

static void range_equity_evaluate_matchup(
    StdDeck_CardMask current_selection[],
    double matchup_weight,
    double *p_total_weight,
    unsigned int *p_total_valid_matchups,
    enum_game_t game,
    int num_players,
    StdDeck_CardMask board,
    StdDeck_CardMask dead_cards_initial,
    bool use_montecarlo,
    int iterations_if_montecarlo,
    int orderflag,
    WeightedAggregation *weighted_agg)
{
    /* Only clear it: both enumSampleBatched() and enumExhaustive_dispatch()
     * start with enumResultClear(), which memsets the struct and would drop an
     * ordering pointer allocated here without freeing it. They allocate their
     * own ordering when orderflag is set. */
    enum_result_t matchup_result;
    enumResultClear(&matchup_result);

    StdDeck_CardMask effective_dead_cards;
    StdDeck_CardMask_RESET(effective_dead_cards);
    StdDeck_CardMask_OR(effective_dead_cards, effective_dead_cards, dead_cards_initial);
    StdDeck_CardMask_OR(effective_dead_cards, effective_dead_cards, board);
    for (int p = 0; p < num_players; ++p)
    {
        StdDeck_CardMask_OR(effective_dead_cards, effective_dead_cards, current_selection[p]);
    }

    int ret_eval;
    int num_cards_on_board = StdDeck_numCards(board);
    if (use_montecarlo)
    {
        ret_eval = enumSampleBatched(game, current_selection, board, effective_dead_cards,
                              num_players, num_cards_on_board, iterations_if_montecarlo,
                              orderflag, &matchup_result);
    }
    else
    {
        ret_eval = enumExhaustive_dispatch(game, current_selection, board, effective_dead_cards,
                                           num_players, num_cards_on_board, orderflag,
                                           &matchup_result);
    }

    if (ret_eval == 0)
    {
        weightedAggregationAccumulate(weighted_agg, &matchup_result, matchup_weight, num_players);
        if (p_total_valid_matchups)
            (*p_total_valid_matchups)++;
        if (p_total_weight)
            (*p_total_weight) += matchup_weight;
        RANGE_DEBUG("RangeEquity: matchup success, nsamples=%u\n", matchup_result.nsamples);
    }
    else
    {
        TRACE_RE("Evaluation for a specific matchup failed with code: %d\n", ret_eval);
        RANGE_DEBUG("RangeEquity: evaluation failed (code %d)\n", ret_eval);
    }
    enumResultFree(&matchup_result);
}

// Static helper function for recursive iteration
static void iterate_range_matchups_recursive_legacy(
    int current_player_idx,
    StdDeck_CardMask current_selection[], // Holds one specific hand for each player up to current_player_idx-1
    double current_weight,
    double *p_total_weight,
    unsigned int* p_total_valid_matchups, // Pointer to accumulate count of valid matchups
    unsigned int* p_total_skipped_hands,  // Track skipped hands for debugging
    // Parameters passed through from CalculateEquityForRanges:
    enum_game_t game,
    const PlayerRange player_ranges[],
    int num_players,
    StdDeck_CardMask board,
    StdDeck_CardMask dead_cards_initial, // Original dead cards
    int nboard_cards_to_deal,
    bool use_montecarlo,
    int iterations_if_montecarlo,
    int orderflag,
    enum_result_t* aggregated_results,
    WeightedAggregation* weighted_agg
) {
    if (current_player_idx == num_players) {
        return;
    }

    // Recursive Step: Iterate through hands for current_player_idx
    const PlayerRange* current_player_range_ptr = &player_ranges[current_player_idx];
    StdDeck_CardMask combined_priors; // Mask of cards selected by players before current_player_idx
    StdDeck_CardMask board_plus_dead;
    
    // Pre-compute board plus dead cards
    StdDeck_CardMask_RESET(board_plus_dead);
    StdDeck_CardMask_OR(board_plus_dead, board, dead_cards_initial);

    for (int i = 0; i < current_player_range_ptr->count; ++i) {
        StdDeck_CardMask hand_choice = current_player_range_ptr->hand_masks[i];
        double weight_choice = current_player_range_ptr->weights ? current_player_range_ptr->weights[i] : 1.0;
        if (weight_choice == 0.0)
            continue;
        RANGE_DEBUG("RangeEquity: player %d evaluating hand %d (weight %.3f)\n",
                    current_player_idx, i, weight_choice);

        // First check if hand conflicts with board or dead cards
        if (StdDeck_CardMask_ANY_SET(hand_choice, board_plus_dead)) {
                        // Only show warning once per player to avoid spam
            if (p_total_skipped_hands && *p_total_skipped_hands == 0) {
                TRACE_RE("RangeEquity: Warning: Some hands from player %d's range conflict with board/dead cards and will be skipped.\n", 
                        current_player_idx);
            }
            if (p_total_skipped_hands) {
                (*p_total_skipped_hands)++;
            }
            RANGE_DEBUG("RangeEquity: player %d hand %d conflicts with board/dead cards\n",
                        current_player_idx, i);
            continue;
        }

        // Conflict check: hand_choice vs hands of prior players in this specific matchup
        StdDeck_CardMask_RESET(combined_priors);
        for (int j = 0; j < current_player_idx; ++j) {
            StdDeck_CardMask_OR(combined_priors, combined_priors, current_selection[j]);
        }

        if (StdDeck_CardMask_ANY_SET(hand_choice, combined_priors)) {
            // Affiche la main courante et la main en conflit
            for (int j = 0; j < current_player_idx; ++j) {
                if (StdDeck_CardMask_ANY_SET(hand_choice, current_selection[j])) {
                                    }
            }
            RANGE_DEBUG("RangeEquity: player %d hand %d conflicts with earlier player\n",
                        current_player_idx, i);
            continue; // Conflict with another player's chosen hand in this matchup, skip.
        }

        current_selection[current_player_idx] = hand_choice;

        // If this is the last player, proceed to evaluation, otherwise recurse
        double new_weight = current_weight * weight_choice;

        if (current_player_idx == num_players - 1) {
            RANGE_DEBUG("RangeEquity: evaluating matchup at leaf (weight %.6f)\n", new_weight);
            range_equity_evaluate_matchup(current_selection, new_weight, p_total_weight,
                                          p_total_valid_matchups, game, num_players,
                                          board, dead_cards_initial, use_montecarlo,
                                          iterations_if_montecarlo, orderflag, weighted_agg);
        } else {
             iterate_range_matchups_recursive_legacy(current_player_idx + 1, current_selection, new_weight, p_total_weight, p_total_valid_matchups,
                                            p_total_skipped_hands, game, player_ranges, num_players, board, 
                                            dead_cards_initial, nboard_cards_to_deal, use_montecarlo, 
                                            iterations_if_montecarlo, orderflag, aggregated_results, weighted_agg);
        }
    }
}

static void iterate_range_matchups_prefiltered(
    int current_player_idx,
    StdDeck_CardMask current_selection[],
    StdDeck_CardMask used_mask,
    double current_weight,
    double *p_total_weight,
    unsigned int *p_total_valid_matchups,
    enum_game_t game,
    const range_combo_buffer_t combo_buffers[],
    int num_players,
    StdDeck_CardMask board,
    StdDeck_CardMask dead_cards_initial,
    bool use_montecarlo,
    int iterations_if_montecarlo,
    int orderflag,
    WeightedAggregation *weighted_agg)
{
    if (current_player_idx == num_players)
    {
        RANGE_DEBUG("RangeEquity: evaluating matchup at leaf (weight %.6f)\n", current_weight);
        range_equity_evaluate_matchup(current_selection, current_weight, p_total_weight,
                                      p_total_valid_matchups, game, num_players,
                                      board, dead_cards_initial, use_montecarlo,
                                      iterations_if_montecarlo, orderflag, weighted_agg);
        return;
    }

    const range_combo_buffer_t *buffer = &combo_buffers[current_player_idx];
    for (int idx = 0; idx < buffer->count; ++idx)
    {
        StdDeck_CardMask hand = range_combo_buffer_hand(buffer, idx);
        if (StdDeck_CardMask_ANY_SET(hand, used_mask))
            continue;
        double weight_choice = range_combo_buffer_weight(buffer, idx);
        if (weight_choice == 0.0)
            continue;

        current_selection[current_player_idx] = hand;
        StdDeck_CardMask new_used_mask = used_mask;
        StdDeck_CardMask_OR(new_used_mask, new_used_mask, hand);
        double new_weight = current_weight * weight_choice;

        iterate_range_matchups_prefiltered(current_player_idx + 1,
                                           current_selection,
                                           new_used_mask,
                                           new_weight,
                                           p_total_weight,
                                           p_total_valid_matchups,
                                           game,
                                           combo_buffers,
                                           num_players,
                                           board,
                                           dead_cards_initial,
                                           use_montecarlo,
                                           iterations_if_montecarlo,
                                           orderflag,
                                           weighted_agg);
    }
}

int CalculateEquityForRanges(
    enum_game_t game,
    const PlayerRange player_ranges[],
    int num_players,
    StdDeck_CardMask board,
    StdDeck_CardMask dead_cards_initial,
    int nboard_cards_to_deal,
    bool use_montecarlo,
    int iterations_if_montecarlo,
    int orderflag,
    enum_result_t* aggregated_results
) {
    RANGE_DEBUG("RangeEquity: CalculateEquityForRanges(game=%d, players=%d, montecarlo=%d, iter=%d, board_to_deal=%d)\n",
                game, num_players, use_montecarlo ? 1 : 0, iterations_if_montecarlo, nboard_cards_to_deal);
    if (num_players <= 0 || num_players > ENUM_MAXPLAYERS) {
        TRACE_RE("Error: Invalid number of players: %d\n", num_players);
        return -1;
    }
    if (!aggregated_results) {
        TRACE_RE("Error: aggregated_results pointer is NULL.\n");
        return -1;
    }
    
    range_combo_mode_t combo_mode = range_combo_resolve_mode();
    const bool use_prefilter = (combo_mode == RANGE_COMBO_MODE_PREFILTER);
    range_combo_buffer_t combo_buffers[ENUM_MAXPLAYERS];
    memset(combo_buffers, 0, sizeof(combo_buffers));

    if (use_prefilter)
    {
        for (int i = 0; i < num_players; ++i)
        {
            if (player_ranges[i].count <= 0 || !player_ranges[i].hand_masks)
            {
                TRACE_RE("Error: Player %d has an empty or invalid range.\n", i);
                /* Players before this one already have their buffers built. */
                for (int j = 0; j < i; ++j)
                    range_combo_buffer_free(&combo_buffers[j]);
                enumResultClear(aggregated_results);
                aggregated_results->nplayers = num_players;
                aggregated_results->game = game;
                return 0;
            }
            if (range_combo_buffer_build(&combo_buffers[i], &player_ranges[i], board, dead_cards_initial) != 0)
            {
                TRACE_RE("Error: unable to build combo buffer for player %d (memory?).\n", i);
                for (int j = 0; j <= i; ++j)
                    range_combo_buffer_free(&combo_buffers[j]);
                enumResultClear(aggregated_results);
                aggregated_results->nplayers = num_players;
                aggregated_results->game = game;
                return 0;
            }
            range_combo_log_stats("RangeEquity", i, &combo_buffers[i]);
            if (combo_buffers[i].count == 0)
            {
                TRACE_RE("Error: Player %d has no valid hands in their range after filtering conflicts.\n", i);
                for (int j = 0; j < num_players; ++j)
                    range_combo_buffer_free(&combo_buffers[j]);
                enumResultClear(aggregated_results);
                aggregated_results->nplayers = num_players;
                aggregated_results->game = game;
                return 0;
            }
        }
    }
    else
    {
        for (int i = 0; i < num_players; ++i) {
            if (player_ranges[i].count <= 0 || !player_ranges[i].hand_masks) {
                TRACE_RE("Error: Player %d has an empty or invalid range.\n", i);
                enumResultClear(aggregated_results);
                aggregated_results->nplayers = num_players;
                aggregated_results->game = game;
                return 0;
            }

            double declared_weight = player_ranges[i].total_weight;
            if (declared_weight <= 0.0)
                declared_weight = (double)player_ranges[i].count;
            if (declared_weight <= 0.0) {
                TRACE_RE("Error: Player %d has zero weight in range.\n", i);
                enumResultClear(aggregated_results);
                aggregated_results->nplayers = num_players;
                aggregated_results->game = game;
                return 0;
            }
            
            // Count valid hands in this player's range and associated total weight
            double filtered_weight = 0.0;
            int valid = count_valid_hands_in_range(&player_ranges[i], board, dead_cards_initial, &filtered_weight);
            
            if (valid == 0) {
                TRACE_RE("Error: Player %d has no valid hands in their range after filtering conflicts with board/dead cards.\n", i);
                TRACE_RE("  Range had %d hands, but all conflict with current board/dead cards.\n", player_ranges[i].count);
                enumResultClear(aggregated_results);
                aggregated_results->nplayers = num_players;
                aggregated_results->game = game;
                return 0;
            }
            
            if (valid < player_ranges[i].count) {
                TRACE_RE("Info: Player %d range filtered from %d to %d valid hands.\n", 
                        i, player_ranges[i].count, valid);
            }

            if (filtered_weight <= 0.0) {
                TRACE_RE("Error: Player %d has zero effective weight after filtering conflicts.\n", i);
                enumResultClear(aggregated_results);
                aggregated_results->nplayers = num_players;
                aggregated_results->game = game;
                return 0;
            }
        }
    }

    enumResultClear(aggregated_results);
    aggregated_results->game = game;
    aggregated_results->nplayers = num_players;
    aggregated_results->sampleType = use_montecarlo ? ENUM_SAMPLE : ENUM_EXHAUSTIVE;
    // Note: enumResultAlloc is not called on aggregated_results directly here,
    // assuming it's pre-allocated by the caller. If not, it should be.
    // The `ev` and count arrays within it should be zeroed by enumResultClear.

    StdDeck_CardMask current_selection[ENUM_MAXPLAYERS];
    unsigned int total_valid_matchups = 0;
    unsigned int total_skipped_hands = 0;

    // Initialize current_selection to empty masks
    for(int i = 0; i < num_players; ++i) {
        StdDeck_CardMask_RESET(current_selection[i]);
    }

    double total_weight = 0.0;
    WeightedAggregation weighted_results;
    weightedAggregationInit(&weighted_results, num_players);
    if (use_prefilter)
    {
        StdDeck_CardMask used_mask_init;
        StdDeck_CardMask_RESET(used_mask_init);
        StdDeck_CardMask_OR(used_mask_init, used_mask_init, board);
        StdDeck_CardMask_OR(used_mask_init, used_mask_init, dead_cards_initial);
        iterate_range_matchups_prefiltered(0, current_selection, used_mask_init, 1.0,
                                           &total_weight, &total_valid_matchups,
                                           game, combo_buffers, num_players,
                                           board, dead_cards_initial, use_montecarlo,
                                           iterations_if_montecarlo, orderflag,
                                           &weighted_results);
    }
    else
    {
        iterate_range_matchups_recursive_legacy(0, current_selection, 1.0, &total_weight, &total_valid_matchups,
                                         &total_skipped_hands, game, player_ranges, num_players,
                                         board, dead_cards_initial, nboard_cards_to_deal,
                                         use_montecarlo, iterations_if_montecarlo,
                                         orderflag, aggregated_results, &weighted_results);
    }

    aggregated_results->nsamples = total_valid_matchups; // raw matchup count (for API compatibility)
    unsigned int target_samples = weightedAggregationFinalize(&weighted_results, aggregated_results, num_players);
    if (weighted_results.total_weight <= 0.0) {
        TRACE_RE("Warning: No weight accumulated for matchups.\n");
    }
    if (target_samples > 0) {
        aggregated_results->sampleType = use_montecarlo ? ENUM_SAMPLE : ENUM_EXHAUSTIVE;
    }

    if (total_weight > 0.0) {
        TRACE_RE("Info: Evaluated %u valid matchups (total matchup weight %.3f, weighted samples %.3f).\n",
                 aggregated_results->nsamples, total_weight, weighted_results.total_weighted_samples);
    } else {
        TRACE_RE("Warning: No valid matchups found after filtering conflicts.\n");
    }
    if (total_skipped_hands > 0) {
        TRACE_RE("Info: Skipped %u hand combinations due to conflicts.\n", total_skipped_hands);
    }
    if (range_equity_debug_enabled()) {
        RANGE_DEBUG("RangeEquity: Final EVs:");
        for (int p = 0; p < num_players; ++p) {
            RANGE_DEBUG(" P%d=%.6f", p, aggregated_results->ev[p]);
        }
        RANGE_DEBUG("\n");
    }
    if (use_prefilter)
    {
        for (int i = 0; i < num_players; ++i)
            range_combo_buffer_free(&combo_buffers[i]);
    }
    return total_valid_matchups;
}
