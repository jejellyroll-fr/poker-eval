/*
 * pe-plo4-lane-a - solve the heads-up PLO4 river spot with the v3 vector lane.
 *
 * Issue #284 needs time-to-target numbers for DCFR schedules, but the sampled
 * preflop driver applies one update rule regardless of the preset. This tool
 * drives the lane A solver instead: the river game declared by issue #283 is
 * handed to pe_plo4_cert_solve(), the engine's regret updates run, and every
 * convergence measurement is the exact best response of the vector lane.
 *
 * The game is the same as pe-plo4-certify: one fixed bet size, no rake,
 * deterministic ranges over the legal four-card hands. A full range is
 * enumerated, a partial one is sampled without replacement, so runs are
 * reproducible without a range file.
 */

#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/solver/pe_external_best_response.h>
#include <poker_eval/solver/pe_omaha_deals.h>
#include <poker_eval/solver/pe_plo4_certification.h>
#include <poker_eval/solver/pe_solver_plan.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PE_LANE_A_MAX_COMBOS 178365u

typedef struct
{
    const char *board_text;
    const char *output;
    double pot;
    double invested[2];
    double stacks[2];
    double bet_size;
    double big_blind;
    double target_mbb;
    size_t combos;
    uint64_t iterations;
    uint64_t interval;
    uint64_t seed;
    pe_algorithm_preset_t algorithm;
    pe_dcfr_schedule_t schedule;
    uint64_t horizon;
    int first_to_act;
} options_t;

static int card_value(char rank, char suit)
{
    const char *ranks = "23456789TJQKA";
    const char *suits = "cdhs";
    const char *r = strchr(ranks, rank);
    const char *s = strchr(suits, suit);

    return r && s ? (int)(r - ranks) + 13 * (int)(s - suits) : -1;
}

static int parse_cards(const char *text, mask_t *out)
{
    size_t length = 0u;
    mask_t cards = MASK_EMPTY;
    size_t i = 0u;

    if (!text || !out)
        return -1;
    while (length < 64u && text[length] != '\0')
        ++length;
    if (length == 64u)
        return -1;
    while (i < length)
    {
        int card;

        while (i < length && (text[i] == ',' || text[i] == ' ' ||
                              text[i] == '\t'))
            ++i;
        if (i + 1u >= length)
            return -1;
        card = card_value(text[i], text[i + 1u]);
        if (card < 0 || mask_is_set(cards, card))
            return -1;
        cards = mask_set(cards, card);
        i += 2u;
    }
    *out = cards;
    return 0;
}

static uint64_t next_random(uint64_t *state)
{
    *state = *state * 6364136223846793005ULL + 1442695040888963407ULL;
    return *state >> 33;
}

static size_t live_cards(mask_t board, int *live)
{
    size_t live_count = 0u;
    int card;

    for (card = 0; card < MODERN_DECK_SIZE; ++card)
        if (!mask_is_set(board, card))
            live[live_count++] = card;
    return live_count;
}

static void enumerate_all_hands(const int *live, size_t live_count,
                                pe_omaha_combo_t *out)
{
    size_t produced = 0u;
    size_t a, b, c, d;

    for (a = 0u; a < live_count; ++a)
        for (b = a + 1u; b < live_count; ++b)
            for (c = b + 1u; c < live_count; ++c)
                for (d = c + 1u; d < live_count; ++d)
                {
                    mask_t hand = mask_set(MASK_EMPTY, live[a]);
                    hand = mask_set(hand, live[b]);
                    hand = mask_set(hand, live[c]);
                    hand = mask_set(hand, live[d]);
                    out[produced].cards = hand;
                    out[produced].weight = 1.0;
                    ++produced;
                }
}

static int sample_hands(mask_t board, size_t count, pe_omaha_combo_t *out)
{
    int live[MODERN_DECK_SIZE];
    size_t live_count = live_cards(board, live);
    size_t capacity = 16u;
    size_t produced = 0u;
    uint64_t state = 0x5eed2026u;
    mask_t *seen;

    while (capacity < count * 2u)
        capacity <<= 1u;
    seen = (mask_t *)calloc(capacity, sizeof(*seen));
    if (!seen)
        return -1;
    while (produced < count)
    {
        mask_t hand = MASK_EMPTY;
        size_t slot;

        while (mask_popcount(hand) < 4)
        {
            size_t pick = (size_t)(next_random(&state) %
                                   (uint64_t)live_count);
            hand = mask_set(hand, live[pick]);
        }
        slot = (size_t)(hand ^ (hand >> 32)) & (capacity - 1u);
        slot = (slot * 2654435761u) & (capacity - 1u);
        while (seen[slot] != MASK_EMPTY && seen[slot] != hand)
            slot = (slot + 1u) & (capacity - 1u);
        if (seen[slot] == hand)
            continue;
        seen[slot] = hand;
        out[produced].cards = hand;
        out[produced].weight = 1.0;
        ++produced;
    }
    free(seen);
    return 0;
}

static int enumerate_hands(mask_t board, size_t count, pe_omaha_combo_t *out)
{
    int live[MODERN_DECK_SIZE];
    size_t live_count = live_cards(board, live);

    if (count == pe_omaha_combo_count(board, 4u))
    {
        enumerate_all_hands(live, live_count, out);
        return 0;
    }
    return sample_hands(board, count, out);
}

static int parse_double(const char *text, double *out)
{
    char *end = NULL;
    double value;
    if (!text || !out)
        return -1;
    value = strtod(text, &end);
    if (end == text || *end != '\0' || !isfinite(value))
        return -1;
    *out = value;
    return 0;
}

static int parse_u64(const char *text, uint64_t *out)
{
    char *end = NULL;
    unsigned long long value;
    if (!text || !out)
        return -1;
    value = strtoull(text, &end, 10);
    if (end == text || *end != '\0')
        return -1;
    *out = (uint64_t)value;
    return 0;
}

static void usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s [options]\n"
            "  --board TEXT          five-card board (default Ks9d4c2hTs)\n"
            "  --pot CHIPS           pot at the root (default 63)\n"
            "  --invested CHIPS      both players' prior investment (default 0)\n"
            "  --stack CHIPS         remaining stack per player (default 68.5)\n"
            "  --bet-size CHIPS      the single bet size (default 63)\n"
            "  --big-blind CHIPS     unit for bb/mbb (default 2)\n"
            "  --combos N            combos per range up to 178365 (default 512)\n"
            "  --first-to-act SEAT   seat acting first at the root (default 1)\n"
            "  --iterations N        iteration cap (default 200)\n"
            "  --exploitability-interval N   exact-BR measurement period (default 20)\n"
            "  --target-mbb N        stop at N mbb/game, chips per hand (0 = off)\n"
            "  --algorithm NAME      v3 preset (default dcfr)\n"
            "  --dcfr-schedule NAME  fixed, hs15 or hs30 (default fixed)\n"
            "  --dcfr-horizon N      n in the schedule equations (default 1000)\n"
            "  --seed N              solver seed (default 1)\n"
            "  --output PATH         JSON destination (default stdout)\n"
            "  --help\n",
            program);
}

static int write_report(const char *path, const options_t *options,
                        const char *board_text,
                        const pe_plo4_cert_solve_result_t *result)
{
    FILE *file = path ? fopen(path, "w") : stdout;
    const pe_metrics_t *metrics = &result->metrics;

    if (!file)
    {
        fprintf(stderr, "pe-plo4-lane-a: cannot write %s\n", path);
        return -1;
    }
    fprintf(file,
            "{\"schema\":\"pe-plo4-lane-a/v1\",\"game\":\"plo4\","
            "\"board\":\"%s\",\"pot\":%.17g,\"stack\":%.17g,"
            "\"bet_size\":%.17g,\"big_blind\":%.17g,\"combos\":%zu,"
            "\"first_to_act\":%d,"
            "\"algorithm\":\"%s\",\"dcfr_schedule\":\"%s\","
            "\"dcfr_horizon\":%llu,\"seed\":%llu,"
            "\"progress\":{\"iteration\":%llu,\"complete\":%s,"
            "\"target_reached\":%s},"
            "\"metrics\":{\"exploitability_mbb_per_game\":%.17g,"
            "\"exploitability_chips\":%.17g,\"nash_conv\":%.17g,"
            "\"br_mode\":\"%s\",\"guarantee\":\"%s\","
            "\"br_gap\":[%.17g,%.17g],\"measurement_iteration\":%llu,"
            "\"sample_count\":%llu,\"storage_bytes\":%llu,"
            "\"infosets\":%llu}}\n",
            board_text, options->pot, options->stacks[0], options->bet_size,
            options->big_blind, options->combos, options->first_to_act,
            pe_preset_name(options->algorithm),
            pe_dcfr_schedule_name(options->schedule),
            (unsigned long long)options->horizon,
            (unsigned long long)options->seed,
            (unsigned long long)result->iterations,
            result->complete ? "true" : "false",
            result->target_reached ? "true" : "false",
            result->exploitability_mbb_per_game, result->exploitability,
            result->nash_conv, pe_br_mode_name(metrics->br_mode),
            metrics->guarantee == PE_GUARANTEE_NASH ? "nash" : "empirical",
            metrics->br_gap[0], metrics->br_gap[1],
            (unsigned long long)metrics->measurement_iteration,
            (unsigned long long)metrics->sample_count,
            (unsigned long long)metrics->storage_memory.storage_bytes,
            (unsigned long long)metrics->storage_memory.total_infosets);
    if (path)
        fclose(file);
    return 0;
}

int main(int argc, char **argv)
{
    options_t options;
    pe_plo4_cert_spec_t spec;
    pe_solver_config_t config = pe_solver_config_default();
    pe_plo4_cert_solve_result_t result;
    pe_omaha_range_t ranges[2];
    pe_omaha_combo_t *combos = NULL;
    mask_t board = MASK_EMPTY;
    int arg;

    memset(&options, 0, sizeof(options));
    options.board_text = "Ks9d4c2hTs";
    options.pot = 63.0;
    options.stacks[0] = options.stacks[1] = 68.5;
    options.bet_size = 63.0;
    options.big_blind = 2.0;
    options.combos = 512u;
    options.iterations = 200u;
    options.interval = 20u;
    options.seed = 1u;
    options.algorithm = PE_PRESET_DCFR;
    options.schedule = PE_DCFR_SCHEDULE_FIXED;
    options.horizon = 1000u;
    options.first_to_act = 1;

    arg = 1;
    while (arg < argc)
    {
        const char *name = argv[arg];
        const char *value = arg + 1 < argc ? argv[arg + 1] : NULL;

        if (strcmp(name, "--help") == 0 || strcmp(name, "-h") == 0)
        {
            usage(argv[0]);
            return 0;
        }
        if (strcmp(name, "--board") == 0 && value)
            options.board_text = value;
        else if (strcmp(name, "--output") == 0 && value)
            options.output = value;
        else if (strcmp(name, "--pot") == 0 && value)
        {
            if (parse_double(value, &options.pot) != 0)
                return 2;
        }
        else if (strcmp(name, "--invested") == 0 && value)
        {
            if (parse_double(value, &options.invested[0]) != 0)
                return 2;
            options.invested[1] = options.invested[0];
        }
        else if (strcmp(name, "--stack") == 0 && value)
        {
            if (parse_double(value, &options.stacks[0]) != 0)
                return 2;
            options.stacks[1] = options.stacks[0];
        }
        else if (strcmp(name, "--bet-size") == 0 && value)
        {
            if (parse_double(value, &options.bet_size) != 0)
                return 2;
        }
        else if (strcmp(name, "--big-blind") == 0 && value)
        {
            if (parse_double(value, &options.big_blind) != 0)
                return 2;
        }
        else if (strcmp(name, "--target-mbb") == 0 && value)
        {
            if (parse_double(value, &options.target_mbb) != 0)
                return 2;
        }
        else if (strcmp(name, "--combos") == 0 && value)
        {
            uint64_t combos_value;
            if (parse_u64(value, &combos_value) != 0 ||
                combos_value == 0u || combos_value > PE_LANE_A_MAX_COMBOS)
                return 2;
            options.combos = (size_t)combos_value;
        }
        else if (strcmp(name, "--iterations") == 0 && value)
        {
            if (parse_u64(value, &options.iterations) != 0)
                return 2;
        }
        else if (strcmp(name, "--exploitability-interval") == 0 && value)
        {
            if (parse_u64(value, &options.interval) != 0)
                return 2;
        }
        else if (strcmp(name, "--seed") == 0 && value)
        {
            if (parse_u64(value, &options.seed) != 0)
                return 2;
        }
        else if (strcmp(name, "--dcfr-horizon") == 0 && value)
        {
            if (parse_u64(value, &options.horizon) != 0 ||
                options.horizon == 0u)
                return 2;
        }
        else if (strcmp(name, "--first-to-act") == 0 && value)
        {
            uint64_t seat;
            if (parse_u64(value, &seat) != 0 || seat > 1u)
                return 2;
            options.first_to_act = (int)seat;
        }
        else if (strcmp(name, "--dcfr-schedule") == 0 && value)
        {
            if (pe_dcfr_schedule_parse(value, &options.schedule) != 0)
                return 2;
        }
        else if (strcmp(name, "--algorithm") == 0 && value)
        {
            options.algorithm = pe_preset_from_name(value);
            if (options.algorithm == PE_PRESET_COUNT)
                return 2;
        }
        else
        {
            usage(argv[0]);
            return 2;
        }
        /* Every accepted option takes one value. */
        arg += 2;
    }

    if (parse_cards(options.board_text, &board) != 0 ||
        mask_popcount(board) != 5)
    {
        fprintf(stderr, "pe-plo4-lane-a: board must hold five distinct cards\n");
        return 2;
    }
    if (options.combos > pe_omaha_combo_count(board, 4u))
    {
        fprintf(stderr, "pe-plo4-lane-a: --combos exceeds the legal hands\n");
        return 2;
    }

    combos = (pe_omaha_combo_t *)calloc(options.combos, sizeof(*combos));
    if (!combos || enumerate_hands(board, options.combos, combos) != 0)
    {
        fprintf(stderr, "pe-plo4-lane-a: range generation failed\n");
        free(combos);
        return 1;
    }
    ranges[0].combos = combos;
    ranges[0].count = options.combos;
    ranges[1].combos = combos;
    ranges[1].count = options.combos;

    memset(&spec, 0, sizeof(spec));
    spec.board = board;
    spec.ranges = ranges;
    spec.pot = options.pot;
    spec.invested[0] = options.invested[0];
    spec.invested[1] = options.invested[1];
    spec.stacks[0] = options.stacks[0];
    spec.stacks[1] = options.stacks[1];
    spec.bet_size = options.bet_size;
    spec.big_blind = options.big_blind;
    spec.first_to_act = (uint8_t)options.first_to_act;

    config.algorithm.preset = options.algorithm;
    config.algorithm.dcfr_schedule = options.schedule;
    config.algorithm.dcfr_schedule_horizon = options.horizon;
    config.max_iterations = options.iterations;
    config.exploitability_interval = options.interval;
    config.target_exploitability_mbb = options.target_mbb;
    config.seed = options.seed;

    if (pe_plo4_cert_solve(&spec, &config, &result) != PE_SOLVER_OK)
    {
        fprintf(stderr, "pe-plo4-lane-a: solve failed\n");
        free(combos);
        return 1;
    }
    if (write_report(options.output, &options, options.board_text, &result) != 0)
    {
        free(combos);
        return 1;
    }
    free(combos);
    return 0;
}
