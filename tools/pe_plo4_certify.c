/*
 * pe-plo4-certify - heads-up PLO4 river exploitability certificate (issue 283).
 *
 * Builds the declared river tree, measures a deterministic fixed strategy
 * through pe_plo4_cert_evaluate(), and prints one JSON document on stdout
 * (schema pe-plo4-certification/v1).  Nothing is sampled: the tool either
 * publishes an exact certificate or labels the measurement not_certified with
 * the reason.
 *
 * Ranges are generated deterministically: the first N legal four-card hands in
 * lexicographic card order, weighted uniformly unless --weighted is given.
 * The strategy is uniform or a deterministic per-combo mix, so a run of the
 * tool is reproducible without a strategy file.
 */

#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/solver/pe_omaha_deals.h>
#include <poker_eval/solver/pe_plo4_certification.h>

#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PE_CERTIFY_DEFAULT_COMBOS 64u
#define PE_CERTIFY_MAX_COMBOS 4096u
#define PE_CERTIFY_DEFAULT_ITERATIONS 64u

typedef struct
{
    const char *board_text;
    double pot;
    double invested[2];
    double stacks[2];
    double bet_size;
    double big_blind;
    size_t combos;
    uint32_t max_iterations;
    int weighted;
    int mixed_strategy;
} options_t;

static void fail(const char *message)
{
    fprintf(stderr, "pe-plo4-certify: %s\n", message);
}

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

static void board_text(mask_t board, char out[16])
{
    static const char ranks[] = "23456789TJQKA";
    static const char suits[] = "cdhs";
    size_t at = 0u;
    int rank;
    int suit;

    for (rank = 0; rank < 13; ++rank)
        for (suit = 0; suit < 4; ++suit)
        {
            int card = MODERN_MAKE_CARD(rank, suit);

            if (mask_is_set(board, card))
            {
                out[at++] = ranks[rank];
                out[at++] = suits[suit];
            }
        }
    out[at] = '\0';
}

static int parse_double(const char *text, double *out)
{
    char *end = NULL;
    double value;

    if (!text || !out)
        return -1;
    value = strtod(text, &end);
    if (*end != '\0' || !(value <= DBL_MAX && value >= -DBL_MAX))
        return -1;
    *out = value;
    return 0;
}

static int parse_size(const char *text, size_t *out)
{
    char *end = NULL;
    unsigned long value;

    if (!text || !out)
        return -1;
    value = strtoul(text, &end, 10);
    if (*end != '\0' || value == 0ul)
        return -1;
    *out = (size_t)value;
    return 0;
}

static int parse_iterations(const char *text, uint32_t *out)
{
    size_t value;

    if (parse_size(text, &value) != 0 || value > 100000u)
        return -1;
    *out = (uint32_t)value;
    return 0;
}

static uint64_t next_random(uint64_t *state)
{
    *state = *state * 6364136223846793005ULL + 1442695040888963407ULL;
    return *state >> 33;
}

/* Deterministic sample of distinct four-card hands off the board.  A
 * lexicographic prefix would put nearly all combos on the same low cards and
 * leave the ranges with no legal pair at all, so the tool samples instead. */
static void enumerate_hands(mask_t board, size_t count, pe_omaha_combo_t *out)
{
    int live[MODERN_DECK_SIZE];
    size_t live_count = 0u;
    size_t produced = 0u;
    uint64_t state = 0x5eed2026u;
    int card;

    for (card = 0; card < MODERN_DECK_SIZE; ++card)
        if (!mask_is_set(board, card))
            live[live_count++] = card;
    while (produced < count)
    {
        mask_t hand = MASK_EMPTY;
        size_t duplicate = 0u;
        size_t other;

        while (mask_popcount(hand) < 4)
        {
            size_t pick = (size_t)(next_random(&state) %
                                   (uint64_t)live_count);
            hand = mask_set(hand, live[pick]);
        }
        for (other = 0u; other < produced; ++other)
            if (out[other].cards == hand)
            {
                duplicate = 1u;
                break;
            }
        if (duplicate)
            continue;
        out[produced].cards = hand;
        out[produced].weight = 1.0;
        ++produced;
    }
}

static int cert_strategy(void *user, uint16_t infoset, uint16_t action,
                         pe_value_vec_t *out)
{
    const int *mixed = (const int *)user;
    size_t combo;

    if (!mixed || !out || action >= PE_PLO4_CERT_ACTION_COUNT)
        return -1;
    for (combo = 0u; combo < out->n; ++combo)
    {
        double open =
            *mixed
                ? 0.15 + 0.7 * (double)((infoset * 13u + combo * 5u) % 7u) / 6.0
                : 0.5;
        out->v[combo] = action == 0u ? open : 1.0 - open;
    }
    return 0;
}

static const char *path_name(pe_showdown_path_t path)
{
    switch (path)
    {
    case PE_SHOWDOWN_PATH_SORTED: return "sorted";
    case PE_SHOWDOWN_PATH_PAIRWISE: return "pairwise";
    case PE_SHOWDOWN_PATH_MULTIWAY: return "multiway";
    case PE_SHOWDOWN_PATH_PLO4: return "plo4";
    case PE_SHOWDOWN_PATH_NONE:
    default: return "enumeration";
    }
}

static void print_value(const char *name, const double values[2],
                        double big_blind)
{
    printf("  \"%s\": {\"chips\": [%.17g, %.17g], "
           "\"bb\": [%.17g, %.17g], \"mbb_per_game\": [%.17g, %.17g]},\n",
           name, values[0], values[1], values[0] / big_blind,
           values[1] / big_blind, values[0] / big_blind * 1000.0,
           values[1] / big_blind * 1000.0);
}

static void usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s [options]\n"
            "  --board TEXT         five-card board, e.g. Ks9d4c2hTs\n"
            "  --pot CHIPS          pot at the root (default 63)\n"
            "  --invested CHIPS     both players' prior investment (default 31.5)\n"
            "  --stack CHIPS        remaining stack per player (default 40)\n"
            "  --bet-size CHIPS     the single bet size (default: the stack)\n"
            "  --big-blind CHIPS    unit for bb/mbb (default 2)\n"
            "  --combos N           generated combos per range (default 64)\n"
            "  --max-iterations N   BR convergence budget (default 64)\n"
            "  --strategy MODE      uniform or mixed (default uniform)\n"
            "  --weighted           give the generated range a deterministic\n"
            "                       weight pattern instead of uniform weight\n"
            "  --help\n"
            "\n"
            "Prints a pe-plo4-certification/v1 JSON document on stdout.\n"
            "Schema reference: docs/cfr/guides/plo4_river_certification.md\n",
            program);
}

static int usage_error(const char *program)
{
    usage(program);
    return 2;
}

int main(int argc, char **argv)
{
    options_t options;
    pe_omaha_combo_t *combos = NULL;
    pe_omaha_range_t ranges[2];
    pe_plo4_cert_spec_t spec;
    pe_plo4_cert_result_t result;
    pe_best_response_vector_config_t config;
    pe_solver_status_t status;
    mask_t board = MASK_EMPTY;
    size_t index;
    int arg;
    int exit_code = 1;
    char board_buffer[16];

    memset(&options, 0, sizeof(options));
    options.board_text = "Ks9d4c2hTs";
    options.pot = 63.0;
    options.invested[0] = 31.5;
    options.invested[1] = 31.5;
    options.stacks[0] = 40.0;
    options.stacks[1] = 40.0;
    options.bet_size = 0.0;
    options.big_blind = 2.0;
    options.combos = PE_CERTIFY_DEFAULT_COMBOS;
    options.max_iterations = PE_CERTIFY_DEFAULT_ITERATIONS;

    for (arg = 1; arg < argc; ++arg)
    {
        const char *name = argv[arg];
        const char *value = arg + 1 < argc ? argv[arg + 1] : NULL;

        if (strcmp(name, "--help") == 0 || strcmp(name, "-h") == 0)
        {
            usage(argv[0]);
            return 0;
        }
        if (strcmp(name, "--weighted") == 0)
        {
            options.weighted = 1;
            continue;
        }
        if (!value)
            return usage_error(argv[0]);
        if (strcmp(name, "--board") == 0)
            options.board_text = value;
        else if (strcmp(name, "--pot") == 0)
        {
            if (parse_double(value, &options.pot) != 0)
                return usage_error(argv[0]);
        }
        else if (strcmp(name, "--invested") == 0)
        {
            if (parse_double(value, &options.invested[0]) != 0)
                return usage_error(argv[0]);
            options.invested[1] = options.invested[0];
        }
        else if (strcmp(name, "--stack") == 0)
        {
            if (parse_double(value, &options.stacks[0]) != 0)
                return usage_error(argv[0]);
            options.stacks[1] = options.stacks[0];
        }
        else if (strcmp(name, "--bet-size") == 0)
        {
            if (parse_double(value, &options.bet_size) != 0)
                return usage_error(argv[0]);
        }
        else if (strcmp(name, "--big-blind") == 0)
        {
            if (parse_double(value, &options.big_blind) != 0)
                return usage_error(argv[0]);
        }
        else if (strcmp(name, "--combos") == 0)
        {
            if (parse_size(value, &options.combos) != 0 ||
                options.combos > PE_CERTIFY_MAX_COMBOS)
                return usage_error(argv[0]);
        }
        else if (strcmp(name, "--max-iterations") == 0)
        {
            if (parse_iterations(value, &options.max_iterations) != 0)
                return usage_error(argv[0]);
        }
        else if (strcmp(name, "--strategy") == 0)
        {
            if (strcmp(value, "uniform") == 0)
                options.mixed_strategy = 0;
            else if (strcmp(value, "mixed") == 0)
                options.mixed_strategy = 1;
            else
                return usage_error(argv[0]);
        }
        else
            return usage_error(argv[0]);
        ++arg;
    }

    if (parse_cards(options.board_text, &board) != 0 ||
        mask_popcount(board) != 5)
    {
        fail("board must hold five distinct cards");
        return 2;
    }
    if (options.bet_size < 0.0)
    {
        fail("--bet-size must not be negative");
        return 2;
    }
    if (!(options.bet_size > 0.0))
        options.bet_size = options.stacks[0] < options.stacks[1]
                               ? options.stacks[0]
                               : options.stacks[1];

    if (options.combos > pe_omaha_combo_count(board, 4u))
    {
        fail("--combos exceeds the number of legal four-card hands");
        return 2;
    }
    combos = (pe_omaha_combo_t *)calloc(options.combos, sizeof(*combos));
    if (!combos)
    {
        fail("allocation failed");
        return 1;
    }
    enumerate_hands(board, options.combos, combos);
    if (options.weighted)
        for (index = 3u; index < options.combos; index += 5u)
            combos[index].weight = 0.25;

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
    spec.strategy = cert_strategy;
    spec.strategy_user = &options.mixed_strategy;

    config = pe_best_response_vector_config_default();
    config.max_iterations = options.max_iterations;
    status = pe_plo4_cert_evaluate(&spec, &config, &result);
    if (status != PE_SOLVER_OK)
    {
        fprintf(stderr, "pe-plo4-certify: evaluation failed with status %d\n",
                (int)status);
        goto cleanup;
    }

    board_text(board, board_buffer);
    printf("{\n");
    printf("  \"schema\": \"pe-plo4-certification/v1\",\n");
    printf("  \"generated_by\": \"pe-plo4-certify\",\n");
    printf("  \"issue\": 283,\n");
    printf("  \"board\": \"%s\",\n", board_buffer);
    printf("  \"game_scope\": \"%s\",\n", result.game_scope);
    printf("  \"mode\": \"%s\",\n", result.mode);
    printf("  \"guarantee\": \"%s\",\n", result.guarantee);
    printf("  \"verdict\": \"%s\",\n",
           pe_plo4_cert_verdict_name(result.verdict));
    printf("  \"reason\": \"%s\",\n",
           pe_plo4_cert_reason_name(result.reason));
    printf("  \"converged\": %s,\n", result.converged ? "true" : "false");
    printf("  \"sampled\": false,\n");
    printf("  \"showdown_path\": \"%s\",\n",
           path_name(result.showdown_path));
    printf("  \"rake\": 0,\n");
    printf("  \"pot_reference\": %.17g,\n", result.pot_reference);
    printf("  \"big_blind\": %.17g,\n", result.big_blind);
    printf("  \"combos\": {\"declared\": [%zu, %zu], \"width\": %u, "
           "\"live\": [%zu, %zu], \"joint_mass\": %.17g},\n",
           ranges[0].count, ranges[1].count, (unsigned)result.combo_count,
           result.live_combos[0], result.live_combos[1],
           result.joint_mass);
    printf("  \"br_iterations\": [%u, %u],\n",
           result.br_iterations[0], result.br_iterations[1]);
    print_value("policy_value", result.policy_value, result.big_blind);
    print_value("br_value", result.br_value, result.big_blind);
    print_value("br_gap", result.br_gap, result.big_blind);
    printf("  \"nashconv\": {\"chips\": %.17g, \"bb\": %.17g, "
           "\"mbb_per_game\": %.17g},\n",
           result.nashconv, result.nashconv / result.big_blind,
           result.nashconv / result.big_blind * 1000.0);
    printf("  \"exploitability\": {\"chips\": %.17g, \"bb\": %.17g, "
           "\"mbb_per_game\": %.17g, \"percent_of_reference_pot\": %.17g},\n",
           result.exploitability, result.exploitability / result.big_blind,
           result.exploitability_mbb_per_game,
           result.exploitability_pct_of_pot);
    printf("  \"input\": {\"pot\": %.17g, \"invested\": [%.17g, %.17g], "
           "\"stacks\": [%.17g, %.17g], \"bet_size\": %.17g, "
           "\"strategy\": \"%s\", \"weighted\": %s, "
           "\"max_iterations\": %u}\n",
           options.pot, options.invested[0], options.invested[1],
           options.stacks[0], options.stacks[1], options.bet_size,
           options.mixed_strategy ? "mixed" : "uniform",
           options.weighted ? "true" : "false", options.max_iterations);
    printf("}\n");
    exit_code = 0;

cleanup:
    free(combos);
    return exit_code;
}
