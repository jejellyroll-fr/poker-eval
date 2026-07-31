#include <poker_eval/equity/RangeEquity.h>
#include <poker_eval/equity/range_combo_buffers.h>
#include <poker_eval/equity/sidepots.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/equity/batched_montecarlo.h>
#include <poker_eval/equity/enumord.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

static int multiway_debug_enabled(void)
{
    static int cached = -1;
    if (cached == -1)
    {
        const char *env = getenv("POKER_DEBUG_MULTIWAY");
        if (!env || !*env)
            env = getenv("POKER_DEBUG_RANGE");
        cached = (env && *env) ? 1 : 0;
    }
    return cached;
}

#define MULTIWAY_DEBUG(...)

static void multiway_log_combo_stats(const range_combo_buffer_t *buffer, int player_idx)
{
    (void)buffer;
    (void)player_idx;
}

typedef struct {
    const MultiwayPotState* state;
    const MultiwayEquityOptions* options;
    const sidepot_t* sidepots;
    int num_sidepots;
    double total_pot;
    int num_players;
    StdDeck_CardMask board;
    StdDeck_CardMask dead_cards_initial;
    int nboard_cards_to_deal;
    enum_game_t game;
    bool has_high;
    bool has_low;
    enum_ordering_mode_t ordering_mode;
    double total_weight;
    double total_weighted_samples;
    double ev[ENUM_MAXPLAYERS];
    double win_prob[ENUM_MAXPLAYERS];
    double tie_prob[ENUM_MAXPLAYERS];
} multiway_accumulator_t;

static bool is_hand_valid(StdDeck_CardMask hand, StdDeck_CardMask board, StdDeck_CardMask dead_cards) {
    StdDeck_CardMask combined;
    StdDeck_CardMask_RESET(combined);
    StdDeck_CardMask_OR(combined, board, dead_cards);
    return !StdDeck_CardMask_ANY_SET(hand, combined);
}

static int count_valid_hands_in_range(const PlayerRange* range, StdDeck_CardMask board, StdDeck_CardMask dead_cards, double* out_total_weight) {
    int valid_count = 0;
    double weight_sum = 0.0;
    for (int i = 0; i < range->count; ++i) {
        double weight = range->weights ? range->weights[i] : 1.0;
        if (weight == 0.0)
            continue;
        if (is_hand_valid(range->hand_masks[i], board, dead_cards)) {
            valid_count++;
            weight_sum += weight;
            MULTIWAY_DEBUG("Multiway: hand %d valid (weight %.3f)\n", i, weight);
        } else if (multiway_debug_enabled()) {
            MULTIWAY_DEBUG("Multiway: hand %d conflicts with board/dead\n", i);
        }
    }
    if (out_total_weight)
        *out_total_weight = weight_sum;
    return valid_count;
}

static const MultiwayEquityOptions* get_options_or_default(const MultiwayEquityOptions* options, MultiwayEquityOptions* buffer) {
    if (options) {
        return options;
    }
    buffer->use_montecarlo = false;
    buffer->iterations = 0;
    buffer->orderflag = 1;
    return buffer;
}

static void determine_game_characteristics(multiway_accumulator_t* agg) {
    enum_gameparams_t* params = enumGameParams(agg->game);
    if (!params) {
        agg->has_high = true;
        agg->has_low = false;
        agg->ordering_mode = enum_ordering_mode_hi;
        MULTIWAY_DEBUG("Multiway: game %d fallback to high-only\n", agg->game);
        return;
    }

    agg->has_high = params->hashipot != 0;
    agg->has_low = params->haslopot != 0;

    if (agg->has_high && agg->has_low) {
        agg->ordering_mode = enum_ordering_mode_hilo;
    } else if (agg->has_high) {
        agg->ordering_mode = enum_ordering_mode_hi;
    } else if (agg->has_low) {
        agg->ordering_mode = enum_ordering_mode_lo;
    } else {
        /* Default to high-only payout to avoid undefined behaviour. */
        agg->has_high = true;
        agg->ordering_mode = enum_ordering_mode_hi;
    }
    MULTIWAY_DEBUG("Multiway: game=%d has_high=%d has_low=%d ordering_mode=%d\n",
                   agg->game, agg->has_high, agg->has_low, agg->ordering_mode);
}

static void accumulate_outcome(
    multiway_accumulator_t* agg,
    const sidepot_t* sidepots,
    int num_sidepots,
    const int hi_ranks[],
    const int lo_ranks[],
    double probability
) {
    if (probability <= 0.0)
        return;

    double payout[ENUM_MAXPLAYERS];
    memset(payout, 0, sizeof(payout));

    const bool consider_high = agg->has_high && hi_ranks;
    const bool consider_low = agg->has_low && lo_ranks;

    for (int pot_idx = 0; pot_idx < num_sidepots; ++pot_idx) {
        const sidepot_t* pot = &sidepots[pot_idx];
        double pot_amount = pot->amount;
        int hi_best_rank = agg->num_players + 1;
        int hi_winners = 0;
        int lo_best_rank = agg->num_players + 1;
        int lo_winners = 0;

        if (consider_high) {
            for (int i = 0; i < pot->num_eligible; ++i) {
                int player = pot->eligible_players[i];
                int rank = hi_ranks[player];
                if (rank < hi_best_rank) {
                    hi_best_rank = rank;
                    hi_winners = 1;
                } else if (rank == hi_best_rank) {
                    hi_winners++;
                }
            }
        }

        if (consider_low) {
            for (int i = 0; i < pot->num_eligible; ++i) {
                int player = pot->eligible_players[i];
                int rank = lo_ranks[player];
                if (rank >= agg->num_players)
                    continue;
                if (rank < lo_best_rank) {
                    lo_best_rank = rank;
                    lo_winners = 1;
                } else if (rank == lo_best_rank) {
                    lo_winners++;
                }
            }
        }

        const bool low_qualifies = (lo_winners > 0);
        double hi_amount = 0.0;
        double lo_amount = 0.0;

        if (consider_high && consider_low) {
            if (low_qualifies) {
                hi_amount = pot_amount * 0.5;
                lo_amount = pot_amount - hi_amount;
            } else {
                hi_amount = pot_amount;
            }
        } else if (consider_high) {
            hi_amount = pot_amount;
        } else if (consider_low) {
            if (low_qualifies) {
                lo_amount = pot_amount;
            }
        }

        double hi_share = (hi_winners > 0 && hi_amount > 0.0) ? (hi_amount / hi_winners) : 0.0;
        double lo_share = (lo_winners > 0 && lo_amount > 0.0) ? (lo_amount / lo_winners) : 0.0;

        for (int i = 0; i < pot->num_eligible; ++i) {
            int player = pot->eligible_players[i];
            if (consider_high && hi_winners > 0 && hi_ranks[player] == hi_best_rank) {
                payout[player] += hi_share;
                if (pot_idx == 0 && hi_amount > 0.0) {
                    if (hi_winners == 1) {
                        agg->win_prob[player] += probability;
                    } else {
                        agg->tie_prob[player] += probability;
                    }
                }
            }
            if (consider_low && lo_winners > 0) {
                int rank = lo_ranks[player];
                if (rank < agg->num_players && rank == lo_best_rank) {
                    payout[player] += lo_share;
                }
            }
        }
    }

    for (int player = 0; player < agg->num_players; ++player) {
        agg->ev[player] += payout[player] * probability;
        if (multiway_debug_enabled() && probability > 0.0 && payout[player] != 0.0)
        {
            MULTIWAY_DEBUG("Multiway: outcome prob=%.6f player %d payout=%.6f\n",
                           probability, player, payout[player]);
        }
    }
}

static void process_matchup_result(
    multiway_accumulator_t* agg,
    const enum_result_t* matchup_result,
    double combo_weight
) {
    if (!matchup_result || matchup_result->nsamples == 0 || combo_weight <= 0.0)
        return;

    agg->total_weight += combo_weight;
    agg->total_weighted_samples += combo_weight * matchup_result->nsamples;
    if (multiway_debug_enabled()) {
        MULTIWAY_DEBUG("Multiway: accumulate matchup weight=%.6f nsamples=%u\n",
                       combo_weight, matchup_result->nsamples);
    }

    enum_ordering_t* ordering = matchup_result->ordering;
    if (!ordering || !ordering->hist)
        return;

    enum_ordering_mode_t mode = ordering->mode;
    int hi_ranks[ENUM_MAXPLAYERS];
    int lo_ranks[ENUM_MAXPLAYERS];

    for (unsigned int entry = 0; entry < ordering->nentries; ++entry) {
        unsigned int count = ordering->hist[entry];
        if (count == 0)
            continue;

        double probability = combo_weight * ((double)count / (double)matchup_result->nsamples);
        switch (mode) {
            case enum_ordering_mode_none:
                /* No ordering data available for this entry. */
                continue;
            case enum_ordering_mode_hihi:
                /* Double-board high ordering is not handled yet. */
                continue;
            case enum_ordering_mode_hi:
                for (int p = 0; p < agg->num_players; ++p) {
                    hi_ranks[p] = (int)ENUM_ORDERING_DECODE_K(entry, agg->num_players, p);
                }
                accumulate_outcome(agg, agg->sidepots, agg->num_sidepots, hi_ranks, NULL, probability);
                break;
            case enum_ordering_mode_lo:
                for (int p = 0; p < agg->num_players; ++p) {
                    lo_ranks[p] = (int)ENUM_ORDERING_DECODE_K(entry, agg->num_players, p);
                }
                accumulate_outcome(agg, agg->sidepots, agg->num_sidepots, NULL, lo_ranks, probability);
                break;
            case enum_ordering_mode_hilo:
                for (int p = 0; p < agg->num_players; ++p) {
                    hi_ranks[p] = (int)ENUM_ORDERING_DECODE_HILO_K_HI(entry, agg->num_players, p);
                    lo_ranks[p] = (int)ENUM_ORDERING_DECODE_HILO_K_LO(entry, agg->num_players, p);
                }
                accumulate_outcome(agg, agg->sidepots, agg->num_sidepots, hi_ranks, lo_ranks, probability);
                break;
            default:
                /* Unsupported ordering mode */
                return;
        }
    }
}

static void evaluate_multiway_matchup(
    multiway_accumulator_t *agg,
    StdDeck_CardMask current_selection[],
    double combo_weight)
{
    /* Cleared, not allocated: the enumeration below starts with
     * enumResultClear(), which memsets the struct and would drop an ordering
     * allocated here without freeing it. orderflag is forced non-zero further
     * down, so the enumeration always allocates one itself, with the mode its
     * own game switch derives. */
    enum_result_t matchup_result;
    enumResultClear(&matchup_result);

    StdDeck_CardMask effective_dead;
    StdDeck_CardMask_RESET(effective_dead);
    StdDeck_CardMask_OR(effective_dead, effective_dead, agg->dead_cards_initial);
    for (int p = 0; p < agg->num_players; ++p)
    {
        StdDeck_CardMask_OR(effective_dead, effective_dead, current_selection[p]);
    }
    StdDeck_CardMask_OR(effective_dead, effective_dead, agg->board);

    int ret_eval;
    int orderflag = agg->options->orderflag ? agg->options->orderflag : 1;
    /* Pass the current board count instead of cards to deal, because enumExhaustive expects nboard_existing */
    int nboard_existing = StdDeck_numCards(agg->board);

    if (agg->options->use_montecarlo)
    {
        ret_eval = enumSampleBatched(
            agg->game,
            current_selection,
            agg->board,
            effective_dead,
            agg->num_players,
            nboard_existing,
            agg->options->iterations,
            orderflag,
            &matchup_result);
    }
    else
    {
        ret_eval = enumExhaustive_dispatch(
            agg->game,
            current_selection,
            agg->board,
            effective_dead,
            agg->num_players,
            nboard_existing,
            orderflag,
            &matchup_result);
    }

    if (ret_eval == 0)
    {
        process_matchup_result(agg, &matchup_result, combo_weight);
    }
    enumResultFree(&matchup_result);
}

static void iterate_multiway_recursive_legacy(
    int current_player,
    StdDeck_CardMask current_selection[],
    double current_weight,
    multiway_accumulator_t* agg
) {
    if (current_player == agg->num_players) {
        evaluate_multiway_matchup(agg, current_selection, current_weight);
        return;
    }

    const PlayerRange* range = &agg->state->ranges[current_player];
    StdDeck_CardMask board_plus_dead;
    StdDeck_CardMask_RESET(board_plus_dead);
    StdDeck_CardMask_OR(board_plus_dead, agg->board, agg->dead_cards_initial);

    StdDeck_CardMask prior_masks;
    for (int hand_idx = 0; hand_idx < range->count; ++hand_idx) {
        double weight = range->weights ? range->weights[hand_idx] : 1.0;
        if (weight == 0.0)
            continue;

        StdDeck_CardMask hand = range->hand_masks[hand_idx];
        if (StdDeck_CardMask_ANY_SET(hand, board_plus_dead))
            continue;

        StdDeck_CardMask_RESET(prior_masks);
        for (int prev = 0; prev < current_player; ++prev) {
            StdDeck_CardMask_OR(prior_masks, prior_masks, current_selection[prev]);
        }
        if (StdDeck_CardMask_ANY_SET(hand, prior_masks))
            continue;

        current_selection[current_player] = hand;
        iterate_multiway_recursive_legacy(
            current_player + 1,
            current_selection,
            current_weight * weight,
            agg
        );
    }
}

static void iterate_multiway_recursive_prefiltered(
    int current_player,
    StdDeck_CardMask current_selection[],
    StdDeck_CardMask used_mask,
    double current_weight,
    const range_combo_buffer_t combo_buffers[],
    multiway_accumulator_t* agg
) {
    if (current_player == agg->num_players) {
        evaluate_multiway_matchup(agg, current_selection, current_weight);
        return;
    }

    const range_combo_buffer_t *buffer = &combo_buffers[current_player];
    for (int hand_idx = 0; hand_idx < buffer->count; ++hand_idx) {
        double weight = range_combo_buffer_weight(buffer, hand_idx);
        if (weight == 0.0)
            continue;

        StdDeck_CardMask hand = range_combo_buffer_hand(buffer, hand_idx);
        if (StdDeck_CardMask_ANY_SET(hand, used_mask))
            continue;

        StdDeck_CardMask new_used_mask = used_mask;
        StdDeck_CardMask_OR(new_used_mask, new_used_mask, hand);

        current_selection[current_player] = hand;
        iterate_multiway_recursive_prefiltered(
            current_player + 1,
            current_selection,
            new_used_mask,
            current_weight * weight,
            combo_buffers,
            agg
        );
    }
}

int CalculateMultiwayEquity(
    enum_game_t game,
    const MultiwayPotState* state,
    StdDeck_CardMask board,
    StdDeck_CardMask dead_cards_initial,
    int nboard_cards_to_deal,
    const MultiwayEquityOptions* options,
    MultiwayEquityResult* result
) {
    if (!state || !state->ranges || state->num_players <= 0 || state->num_players > ENUM_MAXPLAYERS || !result) {
        return -1;
    }
    MULTIWAY_DEBUG("Multiway: CalculateMultiwayEquity(game=%d, players=%d)\n", game, state->num_players);

    MultiwayEquityOptions options_buffer;
    const MultiwayEquityOptions* opts = get_options_or_default(options, &options_buffer);

    sidepot_t sidepots[ENUM_MAXPLAYERS];
    double total_pot = 0.0;
    int num_sidepots = pe_calculate_sidepots(state->invested, state->num_players, sidepots, ENUM_MAXPLAYERS, &total_pot);

    if (num_sidepots == 0 || total_pot <= 0.0) {
        MULTIWAY_DEBUG("Multiway: no sidepots (num=%d total=%.3f)\n", num_sidepots, total_pot);
        memset(result, 0, sizeof(MultiwayEquityResult));
        return 0;
    }

    StdDeck_CardMask current_selection[ENUM_MAXPLAYERS];
    for (int i = 0; i < state->num_players; ++i) {
        StdDeck_CardMask_RESET(current_selection[i]);
    }

    range_combo_mode_t combo_mode = range_combo_resolve_mode();
    const bool use_prefilter = (combo_mode == RANGE_COMBO_MODE_PREFILTER);
    range_combo_buffer_t combo_buffers[ENUM_MAXPLAYERS];
    memset(combo_buffers, 0, sizeof(combo_buffers));

    int total_valid_hands = 1;
    if (use_prefilter)
    {
        for (int i = 0; i < state->num_players; ++i)
        {
            if (range_combo_buffer_build(&combo_buffers[i], &state->ranges[i], board, dead_cards_initial) != 0)
            {
                MULTIWAY_DEBUG("Multiway: failed to build combo buffer for player %d\n", i);
                memset(result, 0, sizeof(MultiwayEquityResult));
                for (int j = 0; j <= i; ++j)
                    range_combo_buffer_free(&combo_buffers[j]);
                return 0;
            }
            multiway_log_combo_stats(&combo_buffers[i], i);
            if (combo_buffers[i].count == 0)
            {
                MULTIWAY_DEBUG("Multiway: player %d has no valid combos after filtering\n", i);
                memset(result, 0, sizeof(MultiwayEquityResult));
                for (int j = 0; j < state->num_players; ++j)
                    range_combo_buffer_free(&combo_buffers[j]);
                return 0;
            }
            total_valid_hands *= combo_buffers[i].count;
        }
    }
    else
    {
        for (int i = 0; i < state->num_players; ++i) {
            double filtered_weight = 0.0;
            int valid = count_valid_hands_in_range(&state->ranges[i], board, dead_cards_initial, &filtered_weight);
            if (valid == 0 || filtered_weight <= 0.0) {
                MULTIWAY_DEBUG("Multiway: player %d invalid range (valid=%d filtered_weight=%.3f)\n",
                               i, valid, filtered_weight);
                memset(result, 0, sizeof(MultiwayEquityResult));
                return 0;
            }
            total_valid_hands *= valid;
            MULTIWAY_DEBUG("Multiway: player %d valid hands=%d filtered_weight=%.3f\n",
                           i, valid, filtered_weight);
        }
    }

    multiway_accumulator_t aggregator;
    memset(&aggregator, 0, sizeof(aggregator));
    aggregator.state = state;
    aggregator.options = opts;
    aggregator.sidepots = sidepots;
    aggregator.num_sidepots = num_sidepots;
    aggregator.total_pot = total_pot;
    aggregator.num_players = state->num_players;
    aggregator.board = board;
    aggregator.dead_cards_initial = dead_cards_initial;
    aggregator.nboard_cards_to_deal = nboard_cards_to_deal;
    aggregator.game = game;
    determine_game_characteristics(&aggregator);

    if (use_prefilter)
    {
        StdDeck_CardMask used_mask;
        StdDeck_CardMask_RESET(used_mask);
        StdDeck_CardMask_OR(used_mask, used_mask, board);
        StdDeck_CardMask_OR(used_mask, used_mask, dead_cards_initial);
        iterate_multiway_recursive_prefiltered(0, current_selection, used_mask, 1.0, combo_buffers, &aggregator);
    }
    else
    {
        iterate_multiway_recursive_legacy(0, current_selection, 1.0, &aggregator);
    }

    memset(result, 0, sizeof(MultiwayEquityResult));
    result->total_weighted_samples = aggregator.total_weighted_samples;

    if (aggregator.total_weight <= 0.0 || total_pot <= 0.0) {
        MULTIWAY_DEBUG("Multiway: no aggregate weight (total_weight=%.6f total_pot=%.3f)\n",
                       aggregator.total_weight, total_pot);
        if (use_prefilter) {
            for (int i = 0; i < state->num_players; ++i)
                range_combo_buffer_free(&combo_buffers[i]);
        }
        return 0;
    }

    for (int i = 0; i < aggregator.num_players; ++i) {
        double ev = aggregator.ev[i] / aggregator.total_weight;
        result->ev[i] = ev;
        result->equity[i] = ev / total_pot;
        result->win_prob[i] = aggregator.win_prob[i] / aggregator.total_weight;
        result->tie_prob[i] = aggregator.tie_prob[i] / aggregator.total_weight;
        MULTIWAY_DEBUG("Multiway: player %d EV=%.6f equity=%.6f win=%.6f tie=%.6f\n",
                       i, result->ev[i], result->equity[i], result->win_prob[i], result->tie_prob[i]);
    }

    if (use_prefilter) {
        for (int i = 0; i < state->num_players; ++i)
            range_combo_buffer_free(&combo_buffers[i]);
    }

    return total_valid_hands;
}
