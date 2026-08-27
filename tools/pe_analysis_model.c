/*
 * pe_analysis_model.c - equity and ICM analysis, without a GUI.
 *
 * See pe_analysis_model.h. The one thing worth stating here is the policy on
 * bad input: every entry point validates before it computes and reports which
 * field was wrong, because these are typed by hand and a silent zero is
 * indistinguishable from a real answer.
 */

#include "pe_analysis_model.h"

#include <poker_eval/core/eval.h>
#include <poker_eval/range.h>

#include <ctype.h>
#include <stdarg.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_error(char *error, size_t size, const char *fmt, ...)
{
    va_list args;
    if (error == NULL || size == 0u)
        return;
    va_start(args, fmt);
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
    vsnprintf(error, size, fmt, args); /* NOSONAR: internal format strings are checked at call sites. */
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
    va_end(args);
}

int pe_analysis_icm_decision(
    const pe_analysis_icm_decision_request_t *request,
    pe_analysis_icm_decision_report_t *out)
{
    pe_analysis_icm_request_t base_request;
    pe_analysis_icm_report_t base;
    icm_input_t input;
    icm_result_t win_result;
    icm_result_t lose_result;
    double payouts[ICM_MAX_PLAYERS];
    double win_stacks[ICM_MAX_PLAYERS];
    double lose_stacks[ICM_MAX_PLAYERS];
    double effective_win;
    double effective_loss;
    int payout_count = 0;
    int i;

    if (request == NULL || out == NULL)
        return -1;
    memset(out, 0, sizeof(*out));
    memset(&base_request, 0, sizeof(base_request));
    base_request.stacks = request->stacks;
    base_request.payouts = request->payouts;
    base_request.mode = PE_ANALYSIS_TOURNAMENT_ICM;
    if (pe_analysis_icm(&base_request, &base) != 0)
    {
        set_error(out->error, sizeof(out->error), "%s", base.error);
        return -1;
    }
    if (request->hero_index < 0 || request->hero_index >= base.player_count ||
        request->opponent_index < 0 ||
        request->opponent_index >= base.player_count ||
        request->hero_index == request->opponent_index)
    {
        set_error(out->error, sizeof(out->error),
                  "hero and opponent must be two different player seats");
        return -1;
    }
    if (!isfinite(request->win_probability) ||
        request->win_probability < 0.0 || request->win_probability > 1.0 ||
        !isfinite(request->chips_at_risk) || request->chips_at_risk < 0.0 ||
        !isfinite(request->chips_to_win) || request->chips_to_win < 0.0)
    {
        set_error(out->error, sizeof(out->error),
                  "probability and chip amounts must be finite and non-negative");
        return -1;
    }
    if (pe_analysis_parse_numbers(request->payouts, payouts, ICM_MAX_PLAYERS,
                                  &payout_count, out->error,
                                  sizeof(out->error)) != 0)
        return -1;
    memset(&input, 0, sizeof(input));
    input.num_players = base.player_count;
    input.num_payouts = payout_count;
    for (i = 0; i < base.player_count; ++i)
    {
        input.stacks[i] = base.stacks[i];
        win_stacks[i] = base.stacks[i];
        lose_stacks[i] = base.stacks[i];
    }
    for (i = 0; i < payout_count; ++i)
        input.payouts[i] = payouts[i];

    effective_win = request->chips_to_win <
        win_stacks[request->opponent_index]
        ? request->chips_to_win : win_stacks[request->opponent_index];
    effective_loss = request->chips_at_risk <
        lose_stacks[request->hero_index]
        ? request->chips_at_risk : lose_stacks[request->hero_index];
    win_stacks[request->hero_index] += effective_win;
    win_stacks[request->opponent_index] -= effective_win;
    lose_stacks[request->hero_index] -= effective_loss;
    lose_stacks[request->opponent_index] += effective_loss;
    for (int i = 0; i < ICM_MAX_PLAYERS; ++i)
        input.stacks[i] = win_stacks[i];
    if (pe_icm_calculate(&input, &win_result) != 0)
    {
        set_error(out->error, sizeof(out->error),
                  "ICM refused the win outcome");
        return -1;
    }
    for (int i = 0; i < ICM_MAX_PLAYERS; ++i)
        input.stacks[i] = lose_stacks[i];
    if (pe_icm_calculate(&input, &lose_result) != 0)
    {
        set_error(out->error, sizeof(out->error),
                  "ICM refused the loss outcome");
        return -1;
    }
    out->current_ev = base.ev[request->hero_index];
    out->win_ev = win_result.icm_ev[request->hero_index];
    out->lose_ev = lose_result.icm_ev[request->hero_index];
    out->decision_ev = request->win_probability * out->win_ev +
                       (1.0 - request->win_probability) * out->lose_ev;
    out->delta_vs_fold = out->decision_ev - out->current_ev;
    out->effective_win = effective_win;
    out->effective_loss = effective_loss;
    return 0;
}

/* A parsed range can still be unusable as a matchup: the ranges may be
 * individually valid while every combination shares a card with another
 * player's combination. Detect that before the low-level evaluator is called
 * repeatedly for combinations that can never be played. */
static int ranges_have_compatible_matchup(const pe_range_t *const ranges[],
                                          int player_count,
                                          int player,
                                          StdDeck_CardMask used_cards)
{
    size_t combo;

    if (player == player_count)
        return 1;
    for (combo = 0u; combo < ranges[player]->count; ++combo)
    {
        StdDeck_CardMask next_used;
        if (ranges[player]->combos[combo].weight <= 0.0 ||
            StdDeck_CardMask_ANY_SET(ranges[player]->combos[combo].hand,
                                     used_cards))
            continue;
        StdDeck_CardMask_OR(next_used, used_cards,
                            ranges[player]->combos[combo].hand);
        if (ranges_have_compatible_matchup(ranges, player_count,
                                           player + 1, next_used))
            return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ *
 * Card text
 * ------------------------------------------------------------------ */

/*
 * Parse "AhKd7s" or "Ah Kd 7s" into a mask.
 *
 * Duplicates are refused rather than silently collapsed: "AhAh" is a typo,
 * and accepting it would quietly change the number of board cards.
 */
static int parse_cards(const char *text, StdDeck_CardMask *out_mask,
                       int *out_count, const char *field,
                       char *error, size_t error_size)
{
    StdDeck_CardMask mask;
    int count = 0;
    size_t i = 0u;

    StdDeck_CardMask_RESET(mask);
    *out_count = 0;
    *out_mask = mask;
    if (text == NULL)
        return 0;
    while (text[i] != '\0')
    {
        char card_text[3];
        int card = 0;
        if (isspace((unsigned char)text[i]) || text[i] == ',')
        {
            ++i;
            continue;
        }
        if (text[i + 1u] == '\0')
        {
            set_error(error, error_size,
                      "%s: '%s' ends with a half card", field, text);
            return -1;
        }
        card_text[0] = text[i];
        card_text[1] = text[i + 1u];
        card_text[2] = '\0';
        if (StdDeck_stringToCard(card_text, &card) == 0 ||
            card < 0 || card >= StdDeck_N_CARDS)
        {
            set_error(error, error_size, "%s: '%s' is not a card", field,
                      card_text);
            return -1;
        }
        if (StdDeck_CardMask_CARD_IS_SET(mask, card))
        {
            set_error(error, error_size, "%s: %s appears twice", field,
                      card_text);
            return -1;
        }
        StdDeck_CardMask_SET(mask, card);
        ++count;
        i += 2u;
    }
    *out_mask = mask;
    *out_count = count;
    return 0;
}

/* ------------------------------------------------------------------ *
 * Numbers
 * ------------------------------------------------------------------ */

int pe_analysis_parse_numbers(const char *text, double *out, int capacity,
                              int *out_count, char *error, size_t error_size)
{
    int count = 0;
    const char *cursor = text;

    if (out == NULL || out_count == NULL || capacity <= 0)
        return -1;
    *out_count = 0;
    if (text == NULL)
        return 0;
    while (*cursor != '\0')
    {
        char *end = NULL;
        double value;
        if (isspace((unsigned char)*cursor) || *cursor == ',' ||
            *cursor == ';')
        {
            ++cursor;
            continue;
        }
        errno = 0;
        value = strtod(cursor, &end);
        if (end == cursor || errno != 0 || !isfinite(value))
        {
            set_error(error, error_size, "'%s' is not a number", cursor);
            return -1;
        }
        if (value < 0.0)
        {
            set_error(error, error_size, "negative value %g", value);
            return -1;
        }
        if (count == capacity)
        {
            set_error(error, error_size, "more than %d values", capacity);
            return -1;
        }
        out[count++] = value;
        cursor = end;
    }
    *out_count = count;
    return 0;
}

/* ------------------------------------------------------------------ *
 * Equity
 * ------------------------------------------------------------------ */

int pe_analysis_equity(const pe_analysis_equity_request_t *request,
                       pe_analysis_equity_report_t *out)
{
    pe_range_t *ranges[PE_ANALYSIS_MAX_PLAYERS];
    const pe_range_t *const_ranges[PE_ANALYSIS_MAX_PLAYERS];
    StdDeck_CardMask board;
    StdDeck_CardMask dead;
    StdDeck_CardMask blocked;
    pe_equity_result_multi_t result;
    pe_equity_opts_t opts;
    int board_count = 0;
    int dead_count = 0;
    int player;
    int rc = -1;

    if (request == NULL || out == NULL)
        return -1;
    memset(out, 0, sizeof(*out));
    memset(ranges, 0, sizeof(ranges));
    if (request->player_count < 2 ||
        request->player_count > PE_ANALYSIS_MAX_PLAYERS)
    {
        set_error(out->error, sizeof(out->error),
                  "player count must be between 2 and %d",
                  PE_ANALYSIS_MAX_PLAYERS);
        return -1;
    }
    if (parse_cards(request->board, &board, &board_count, "board",
                    out->error, sizeof(out->error)) != 0)
        return -1;
    if (board_count == 1 || board_count == 2 || board_count > 5)
    {
        set_error(out->error, sizeof(out->error),
                  "a board has 0, 3, 4 or 5 cards, got %d", board_count);
        return -1;
    }
    if (parse_cards(request->dead, &dead, &dead_count, "dead cards",
                    out->error, sizeof(out->error)) != 0)
        return -1;
    StdDeck_CardMask_OR(blocked, board, dead);
    if (StdDeck_CardMask_ANY_SET(board, dead))
    {
        set_error(out->error, sizeof(out->error),
                  "a dead card is also on the board");
        return -1;
    }

    for (player = 0; player < request->player_count; ++player)
    {
        const char *text = request->ranges[player];
        if (text == NULL || text[0] == '\0')
        {
            set_error(out->error, sizeof(out->error),
                      "range for player %d is empty", player + 1);
            goto cleanup;
        }
        if (pe_range_parse(request->game, text, blocked, NULL,
                           &ranges[player]) != PE_STATUS_OK ||
            ranges[player] == NULL)
        {
            set_error(out->error, sizeof(out->error),
                      "range for player %d could not be parsed: %s",
                      player + 1, text);
            goto cleanup;
        }
        if (ranges[player]->count == 0u)
        {
            set_error(out->error, sizeof(out->error),
                      "range for player %d has no combo left once the board "
                      "and dead cards are removed", player + 1);
            goto cleanup;
        }
        const_ranges[player] = ranges[player];
        out->combos[player] = ranges[player]->count;
    }

    {
        StdDeck_CardMask no_cards;
        StdDeck_CardMask_RESET(no_cards);
        if (!ranges_have_compatible_matchup(
                const_ranges,
                request->player_count, 0, no_cards))
        {
            set_error(out->error, sizeof(out->error),
                      "no compatible matchups remain after card-conflict "
                      "filtering; check the player ranges");
            goto cleanup;
        }
    }

    memset(&opts, 0, sizeof(opts));
    opts.is_monte_carlo = request->monte_carlo ? 1 : 0;
    opts.iterations = request->iterations > 0 ? request->iterations : 200000;
    memset(&result, 0, sizeof(result));

    if (request->player_count == 2)
    {
        if (pe_equity_range_vs_range(NULL, request->game, ranges[0], ranges[1],
                                     board, dead, &opts, &result) !=
            PE_STATUS_OK)
        {
            set_error(out->error, sizeof(out->error),
                      "the equity engine refused this configuration");
            goto cleanup;
        }
    }
    else if (pe_equity_multiway(NULL, request->game, const_ranges,
                                request->player_count, board, dead, &opts,
                                &result) != PE_STATUS_OK)
    {
        set_error(out->error, sizeof(out->error),
                  "the multiway equity engine refused this configuration");
        goto cleanup;
    }

    out->player_count = request->player_count;
    for (player = 0; player < request->player_count; ++player)
    {
        out->equity[player] = result.results[player].equity;
        out->win[player] = result.results[player].win_prob;
        out->tie[player] = result.results[player].tie_prob;
    }
    out->samples = result.samples;
    out->exact = result.exact;
    rc = 0;

cleanup:
    for (player = 0; player < PE_ANALYSIS_MAX_PLAYERS; ++player)
        if (ranges[player] != NULL)
            pe_range_free(ranges[player]);
    return rc;
}

/* ------------------------------------------------------------------ *
 * Breakdown
 * ------------------------------------------------------------------ */

const char *pe_hand_class_name(pe_hand_class_t hand_class)
{
    static const char *const names[PE_HAND_CLASS_COUNT] = {
        "High card", "Pair", "Two pair", "Trips", "Straight",
        "Flush", "Full house", "Quads", "Straight flush"
    };
    return hand_class < PE_HAND_CLASS_COUNT ? names[hand_class] : "?";
}

/* The evaluator's hand types map one-to-one onto the table above; the switch
   makes the correspondence explicit rather than relying on both enums
   happening to be in the same order. */
static int class_of_handval(HandVal value, pe_hand_class_t *out)
{
    switch (HandVal_HANDTYPE(value))
    {
    case StdRules_HandType_NOPAIR:   *out = PE_HAND_CLASS_HIGH_CARD; return 0;
    case StdRules_HandType_ONEPAIR:  *out = PE_HAND_CLASS_PAIR; return 0;
    case StdRules_HandType_TWOPAIR:  *out = PE_HAND_CLASS_TWO_PAIR; return 0;
    case StdRules_HandType_TRIPS:    *out = PE_HAND_CLASS_TRIPS; return 0;
    case StdRules_HandType_STRAIGHT: *out = PE_HAND_CLASS_STRAIGHT; return 0;
    case StdRules_HandType_FLUSH:    *out = PE_HAND_CLASS_FLUSH; return 0;
    case StdRules_HandType_FULLHOUSE:*out = PE_HAND_CLASS_FULL_HOUSE; return 0;
    case StdRules_HandType_QUADS:    *out = PE_HAND_CLASS_QUADS; return 0;
    case StdRules_HandType_STFLUSH:  *out = PE_HAND_CLASS_STRAIGHT_FLUSH;
                                     return 0;
    default:                         return -1;
    }
}

int pe_analysis_breakdown(enum_game_t game, const char *range,
                          const char *board, const char *dead,
                          pe_analysis_breakdown_t *out)
{
    pe_range_t *parsed = NULL;
    StdDeck_CardMask board_mask;
    StdDeck_CardMask dead_mask;
    StdDeck_CardMask blocked;
    int board_count = 0;
    int dead_count = 0;
    size_t i;
    int rc = -1;

    if (out == NULL)
        return -1;
    memset(out, 0, sizeof(*out));
    if (game != game_holdem)
    {
        /* The classifier evaluates hole + board as one five-card-best hand,
           which is the Hold'em rule. Omaha's "exactly two of four" would give
           a wrong answer here, so refuse rather than mislead. */
        set_error(out->error, sizeof(out->error),
                  "the made-hand breakdown currently covers Hold'em only");
        return -1;
    }
    if (parse_cards(board, &board_mask, &board_count, "board",
                    out->error, sizeof(out->error)) != 0)
        return -1;
    if (board_count < 3 || board_count > 5)
    {
        set_error(out->error, sizeof(out->error),
                  "the breakdown needs a board of 3, 4 or 5 cards, got %d",
                  board_count);
        return -1;
    }
    if (parse_cards(dead, &dead_mask, &dead_count, "dead cards",
                    out->error, sizeof(out->error)) != 0)
        return -1;
    StdDeck_CardMask_OR(blocked, board_mask, dead_mask);
    if (range == NULL || range[0] == '\0')
    {
        set_error(out->error, sizeof(out->error), "the range is empty");
        return -1;
    }
    if (pe_range_parse(game, range, blocked, NULL, &parsed) != PE_STATUS_OK ||
        parsed == NULL)
    {
        set_error(out->error, sizeof(out->error),
                  "the range could not be parsed: %s", range);
        return -1;
    }

    for (i = 0u; i < parsed->count; ++i)
    {
        StdDeck_CardMask combined;
        HandVal value;
        pe_hand_class_t hand_class;

        if (StdDeck_CardMask_ANY_SET(parsed->combos[i].hand, blocked))
        {
            out->blocked_combos++;
            continue;
        }
        StdDeck_CardMask_OR(combined, parsed->combos[i].hand, board_mask);
        value = StdDeck_StdRules_EVAL_N(combined, board_count + 2);
        if (class_of_handval(value, &hand_class) != 0)
        {
            set_error(out->error, sizeof(out->error),
                      "the evaluator returned an unknown hand type");
            goto cleanup;
        }
        out->weight[hand_class] += parsed->combos[i].weight;
        out->total_weight += parsed->combos[i].weight;
        out->live_combos++;
    }
    if (out->total_weight > 0.0)
        for (i = 0u; i < (size_t)PE_HAND_CLASS_COUNT; ++i)
            out->share[i] = out->weight[i] / out->total_weight;
    rc = 0;

cleanup:
    pe_range_free(parsed);
    return rc;
}

/* ------------------------------------------------------------------ *
 * ICM
 * ------------------------------------------------------------------ */

int pe_analysis_icm(const pe_analysis_icm_request_t *request,
                    pe_analysis_icm_report_t *out)
{
    icm_input_t input;
    icm_result_t result;
    pe_analysis_tournament_mode_t mode;
    double payouts[ICM_MAX_PLAYERS];
    double total_chips = 0.0;
    int payout_count = 0;
    int player_count = 0;
    int i;

    if (request == NULL || out == NULL)
        return -1;
    memset(out, 0, sizeof(*out));
    memset(&input, 0, sizeof(input));
    memset(&result, 0, sizeof(result));
    mode = request->mode <= PE_ANALYSIS_TOURNAMENT_FGS
        ? request->mode : PE_ANALYSIS_TOURNAMENT_ICM;

    if (pe_analysis_parse_numbers(request->stacks, input.stacks,
                                  ICM_MAX_PLAYERS, &player_count,
                                  out->error, sizeof(out->error)) != 0)
    {
        char detail[PE_ANALYSIS_ERROR_MAX];
        snprintf(detail, sizeof(detail), "stacks: %s", out->error);
        snprintf(out->error, sizeof(out->error), "%s", detail);
        return -1;
    }
    if (player_count < 2)
    {
        set_error(out->error, sizeof(out->error),
                  "give at least two stacks");
        return -1;
    }
    if (pe_analysis_parse_numbers(request->payouts, payouts, ICM_MAX_PLAYERS,
                                  &payout_count, out->error,
                                  sizeof(out->error)) != 0)
    {
        char detail[PE_ANALYSIS_ERROR_MAX];
        snprintf(detail, sizeof(detail), "payouts: %s", out->error);
        snprintf(out->error, sizeof(out->error), "%s", detail);
        return -1;
    }
    if (payout_count < 1)
    {
        set_error(out->error, sizeof(out->error),
                  "give at least one payout");
        return -1;
    }
    if (payout_count > player_count)
    {
        set_error(out->error, sizeof(out->error),
                  "%d payouts for %d players: someone would be paid for a "
                  "place that cannot be finished", payout_count,
                  player_count);
        return -1;
    }
    for (i = 0; i < player_count; ++i)
    {
        if (input.stacks[i] <= 0.0)
        {
            set_error(out->error, sizeof(out->error),
                      "stack %d is zero: a busted player is not in the model",
                      i + 1);
            return -1;
        }
        total_chips += input.stacks[i];
    }
    for (i = 1; i < payout_count; ++i)
        if (payouts[i] > payouts[i - 1])
        {
            set_error(out->error, sizeof(out->error),
                      "payout %d is larger than payout %d: the ladder must "
                      "not increase down the places", i + 1, i);
            return -1;
        }

    out->mode = mode;
    out->fgs_depth = request->fgs_depth;
    out->player_count = player_count;
    out->payout_count = payout_count;
    for (i = 0; i < player_count; ++i)
        out->stacks[i] = input.stacks[i];
    for (i = 0; i < payout_count; ++i)
    {
        input.payouts[i] = payouts[i];
        out->prize_pool += payouts[i];
    }
    if (!(out->prize_pool > 0.0) || !isfinite(out->prize_pool))
    {
        set_error(out->error, sizeof(out->error),
                  "payouts must contain at least one positive value");
        return -1;
    }

    for (i = 0; i < player_count; ++i)
    {
        out->chip_share[i] = input.stacks[i] / total_chips;
        if (mode == PE_ANALYSIS_TOURNAMENT_CHIP_EV)
        {
            out->equity[i] = out->chip_share[i];
            out->ev[i] = out->chip_share[i] * out->prize_pool;
        }
    }

    if (mode == PE_ANALYSIS_TOURNAMENT_CHIP_EV)
        return 0;

    if (mode == PE_ANALYSIS_TOURNAMENT_FGS)
    {
        double pot_values[1];
        double win_probability[ICM_MAX_PLAYERS];
        int win_count = 0;
        double win_sum = 0.0;
        pe_fgs_scenario_input_t fgs_input;
        pe_fgs_node_t *nodes = NULL;
        pe_fgs_edge_t *edges = NULL;
        pe_fgs_tree_t tree;
        pe_fgs_result_t fgs_result;

        if (request->fgs_depth < 0 ||
            request->fgs_depth > PE_FGS_MAX_DEPTH)
        {
            set_error(out->error, sizeof(out->error),
                      "FGS depth must be between 0 and %d",
                      PE_FGS_MAX_DEPTH);
            return -1;
        }
        if (pe_analysis_parse_numbers(request->fgs_pot, pot_values, 1,
                                      &win_count, out->error,
                                      sizeof(out->error)) != 0 ||
            win_count != 1 || !(pot_values[0] >= 0.0) ||
            !isfinite(pot_values[0]))
        {
            set_error(out->error, sizeof(out->error),
                      "FGS pot must be one non-negative number");
            return -1;
        }
        if (request->fgs_win_probabilities == NULL ||
            request->fgs_win_probabilities[0] == '\0')
        {
            for (i = 0; i < player_count; ++i)
                win_probability[i] = 1.0;
        }
        else
        {
            if (pe_analysis_parse_numbers(request->fgs_win_probabilities,
                                          win_probability, ICM_MAX_PLAYERS,
                                          &win_count, out->error,
                                          sizeof(out->error)) != 0 ||
                win_count != player_count)
            {
                set_error(out->error, sizeof(out->error),
                          "FGS win weights need one value per player");
                return -1;
            }
        }
        for (i = 0; i < player_count; ++i)
        {
            if (!isfinite(win_probability[i]) || win_probability[i] < 0.0)
            {
                set_error(out->error, sizeof(out->error),
                          "FGS win weights must be non-negative numbers");
                return -1;
            }
            win_sum += win_probability[i];
        }
        if (!(win_sum > 0.0) || !isfinite(win_sum))
        {
            set_error(out->error, sizeof(out->error),
                      "FGS needs at least one positive win weight");
            return -1;
        }

        nodes = (pe_fgs_node_t *)calloc(4096u, sizeof(*nodes));
        edges = (pe_fgs_edge_t *)calloc(16384u, sizeof(*edges));
        if (nodes == NULL || edges == NULL)
        {
            free(nodes);
            free(edges);
            set_error(out->error, sizeof(out->error),
                      "not enough memory for the FGS transition tree");
            return -1;
        }
        memset(&fgs_input, 0, sizeof(fgs_input));
        fgs_input.num_players = player_count;
        fgs_input.num_payouts = payout_count;
        fgs_input.pot = pot_values[0];
        fgs_input.depth = request->fgs_depth;
        for (i = 0; i < player_count; ++i)
        {
            fgs_input.stacks[i] = input.stacks[i];
            fgs_input.win_probability[i] = win_probability[i];
        }
        for (i = 0; i < payout_count; ++i)
            fgs_input.payouts[i] = payouts[i];
        memset(&tree, 0, sizeof(tree));
        memset(&fgs_result, 0, sizeof(fgs_result));
        if (pe_fgs_generate_even_contribution(&fgs_input, nodes, 4096u,
                                               edges, 16384u, &tree) != 0 ||
            pe_fgs_calculate_tree(&tree, &fgs_result) != 0)
        {
            free(nodes);
            free(edges);
            set_error(out->error, sizeof(out->error),
                      "FGS transition tree is invalid or exceeds its capacity");
            return -1;
        }
        out->fgs_leaf_count = fgs_result.leaf_count;
        for (i = 0; i < player_count; ++i)
        {
            out->ev[i] = fgs_result.ev[i];
            out->equity[i] = out->prize_pool > 0.0
                ? out->ev[i] / out->prize_pool : 0.0;
        }
        free(nodes);
        free(edges);
        return 0;
    }

    input.num_players = player_count;
    input.num_payouts = payout_count;
    /* ChipEV was returned above; only ICM reaches the Malmuth-Harville call. */
    if (pe_icm_calculate(&input, &result) != 0)
    {
        set_error(out->error, sizeof(out->error),
                  "the ICM model refused this configuration");
        return -1;
    }

    for (i = 0; i < player_count; ++i)
    {
        out->equity[i] = result.equity[i];
        out->ev[i] = result.icm_ev[i];
    }
    return 0;
}
