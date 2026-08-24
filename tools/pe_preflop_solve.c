/* pe_preflop_solve.c - product-facing Lane B preflop solver driver.
 *
 * This is the first complete vertical slice of the scalable preflop lane:
 * ranges are parsed once, private hands are sampled with card removal, the
 * sampled deal enters a real betting state, and called terminals are settled
 * by deterministic Monte-Carlo showdown.  The current product slice is
 * deliberately heads-up Hold'em; unsupported variants fail explicitly.
 */

#include <poker_eval/core/enumdefs.h>
#include <poker_eval/range.h>
#include <poker_eval/solver/pe_preflop_allin_game.h>
#include <poker_eval/solver/pe_range.h>
#include <poker_eval/solver/pe_solver.h>
#include <poker_eval/solver/pe_solver_config.h>
#include <poker_eval/solver/pe_ports.h>

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_ITERATIONS 10000u
#define DEFAULT_SHOWDOWN_SAMPLES 128
#define DEFAULT_STACK 100.0
#define DEFAULT_SMALL_BLIND 0.5
#define DEFAULT_BIG_BLIND 1.0
#define DEFAULT_MIN_RAISE 1.0

typedef struct {
    const char *game;
    const char *range[2];
    uint64_t iterations;
    int showdown_samples;
    double stack;
    double small_blind;
    double big_blind;
    double min_raise;
    double raise_sizes[PE_PREFLOP_ALLIN_MAX_RAISE_SIZES];
    int raise_count;
    int allow_nonallin_call;
    uint64_t seed;
    const char *output;
} options_t;

static void usage(FILE *stream)
{
    fprintf(stream,
        "Usage: pe-preflop-solve [options]\n"
        "  --game holdem                 heads-up Hold'em (only supported lane)\n"
        "  --range0 TEXT --range1 TEXT  weighted private ranges (default 100%%)\n"
        "  --iterations N               MCCFR iterations (default %u)\n"
        "  --samples N                  showdown boards per called terminal\n"
        "  --stack BB                   effective stack for both players\n"
        "  --sb BB --bb BB              forced bets\n"
        "  --min-raise BB               minimum raise increment\n"
        "  --raise AMOUNT[,AMOUNT...]   raise increments above the call\n"
        "  --allow-calls                allow calls before all-in\n"
        "  --seed N                     deterministic RNG seed\n"
        "  --output FILE                write a JSON run report\n"
        "  --help                       show this help\n", DEFAULT_ITERATIONS);
}

static int parse_u64(const char *text, uint64_t *out)
{
    char *end = NULL;
    unsigned long long value;
    if (!text || !out || !*text)
        return -1;
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno || end == text || *end != '\0')
        return -1;
    *out = (uint64_t)value;
    return 0;
}

static int parse_positive_double(const char *text, double *out)
{
    char *end = NULL;
    double value;
    if (!text || !out || !*text)
        return -1;
    errno = 0;
    value = strtod(text, &end);
    if (errno || end == text || *end != '\0' || !(value > 0.0))
        return -1;
    *out = value;
    return 0;
}

static int parse_raise_sizes(const char *text, options_t *options)
{
    char buffer[512];
    char *token;
    if (!text || !options || strlen(text) >= sizeof(buffer))
        return -1;
    snprintf(buffer, sizeof(buffer), "%s", text);
    token = strtok(buffer, ",");
    while (token != NULL) {
        double amount;
        if (options->raise_count >= PE_PREFLOP_ALLIN_MAX_RAISE_SIZES ||
            parse_positive_double(token, &amount) != 0)
            return -1;
        options->raise_sizes[options->raise_count++] = amount;
        token = strtok(NULL, ",");
    }
    return options->raise_count > 0 ? 0 : -1;
}

static int parse_options(int argc, char **argv, options_t *options)
{
    memset(options, 0, sizeof(*options));
    options->game = "holdem";
    options->range[0] = "100%";
    options->range[1] = "100%";
    options->iterations = DEFAULT_ITERATIONS;
    options->showdown_samples = DEFAULT_SHOWDOWN_SAMPLES;
    options->stack = DEFAULT_STACK;
    options->small_blind = DEFAULT_SMALL_BLIND;
    options->big_blind = DEFAULT_BIG_BLIND;
    options->min_raise = DEFAULT_MIN_RAISE;
    options->seed = UINT64_C(0x50455f5052464c42);
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        const char *value = NULL;
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            usage(stdout);
            return 1;
        }
        if (i + 1 < argc)
            value = argv[i + 1];
        if ((strcmp(arg, "--game") == 0 || strcmp(arg, "--range0") == 0 ||
             strcmp(arg, "--range1") == 0 || strcmp(arg, "--iterations") == 0 ||
             strcmp(arg, "--samples") == 0 || strcmp(arg, "--stack") == 0 ||
             strcmp(arg, "--sb") == 0 || strcmp(arg, "--bb") == 0 ||
             strcmp(arg, "--min-raise") == 0 || strcmp(arg, "--raise") == 0 ||
             strcmp(arg, "--seed") == 0 || strcmp(arg, "--output") == 0) &&
            (!value || value[0] == '-')) {
            fprintf(stderr, "missing value for %s\n", arg);
            return -1;
        }
        if (strcmp(arg, "--game") == 0) options->game = value;
        else if (strcmp(arg, "--range0") == 0) options->range[0] = value;
        else if (strcmp(arg, "--range1") == 0) options->range[1] = value;
        else if (strcmp(arg, "--iterations") == 0) {
            if (parse_u64(value, &options->iterations) != 0 || options->iterations == 0u)
                return -1;
        } else if (strcmp(arg, "--samples") == 0) {
            uint64_t samples;
            if (parse_u64(value, &samples) != 0 || samples == 0u || samples > 1000000u)
                return -1;
            options->showdown_samples = (int)samples;
        } else if (strcmp(arg, "--stack") == 0) {
            if (parse_positive_double(value, &options->stack) != 0) return -1;
        } else if (strcmp(arg, "--sb") == 0) {
            if (parse_positive_double(value, &options->small_blind) != 0) return -1;
        } else if (strcmp(arg, "--bb") == 0) {
            if (parse_positive_double(value, &options->big_blind) != 0) return -1;
        } else if (strcmp(arg, "--min-raise") == 0) {
            if (parse_positive_double(value, &options->min_raise) != 0) return -1;
        } else if (strcmp(arg, "--raise") == 0) {
            if (parse_raise_sizes(value, options) != 0) return -1;
        } else if (strcmp(arg, "--allow-calls") == 0) {
            options->allow_nonallin_call = 1;
            continue;
        } else if (strcmp(arg, "--seed") == 0) {
            if (parse_u64(value, &options->seed) != 0) return -1;
        } else if (strcmp(arg, "--output") == 0) options->output = value;
        else {
            fprintf(stderr, "unknown option: %s\n", arg);
            return -1;
        }
        ++i;
    }
    if (strcmp(options->game, "holdem") != 0 ||
        options->big_blind < options->small_blind ||
        options->stack <= options->big_blind)
        return -1;
    return 0;
}

static const char *guarantee_name(pe_guarantee_t guarantee)
{
    switch (guarantee) {
    case PE_GUARANTEE_UNSPECIFIED: return "unspecified";
    case PE_GUARANTEE_NASH: return "nash";
    case PE_GUARANTEE_NO_REGRET_ONLY: return "no-regret-only";
    case PE_GUARANTEE_EMPIRICAL: return "empirical";
    default: return "unspecified";
    }
}

static void write_report(const char *path, const options_t *options,
                         const pe_metrics_t *metrics, pe_progress_t *progress,
                         size_t infosets)
{
    FILE *file = fopen(path, "w");
    if (!file) {
        fprintf(stderr, "cannot write %s: %s\n", path, strerror(errno));
        return;
    }
    fprintf(file,
        "{\"schema\":\"pe-preflop-solve/v1\","
        "\"game\":\"holdem\",\"players\":2,"
        "\"iterations\":%" PRIu64 ",\"showdown_samples\":%d,"
        "\"stack\":%.17g,\"small_blind\":%.17g,\"big_blind\":%.17g,"
        "\"allow_nonallin_call\":%s,\"infosets\":%zu,"
        "\"progress\":{\"iteration\":%" PRIu64 ",\"complete\":%s},"
        "\"metrics\":{\"guarantee\":\"%s\",\"exploitability_raw\":null,"
        "\"exploitability_mbb_per_game\":null,\"big_blind\":%.17g}}\n",
        options->iterations, options->showdown_samples, options->stack,
        options->small_blind, options->big_blind,
        options->allow_nonallin_call ? "true" : "false", infosets,
        progress->iteration, progress->complete ? "true" : "false",
        guarantee_name(metrics->guarantee), options->big_blind);
    fclose(file);
}

int main(int argc, char **argv)
{
    options_t options;
    pe_range_t *ranges[2] = {NULL, NULL};
    pe_preflop_allin_rules_t rules;
    pe_preflop_allin_game_t *game = NULL;
    pe_solver_config_t config;
    pe_solver_deps_t deps;
    pe_solver_t *solver = NULL;
    pe_solver_status_t status;
    pe_progress_t progress = {0};
    pe_metrics_t metrics = {0};
    StdDeck_CardMask dead;

    {
        int option_status = parse_options(argc, argv, &options);
        if (option_status == 1)
            return 0;
        if (option_status != 0) {
        usage(stderr);
        return 2;
        }
    }
    StdDeck_CardMask_RESET(dead);
    for (int player = 0; player < 2; ++player) {
        if (pe_solver_range_parse(game_holdem, options.range[player], dead,
                                  &ranges[player]) != PE_SOLVER_OK ||
            !ranges[player]) {
            fprintf(stderr, "invalid Hold'em range%d: %s\n", player,
                    options.range[player]);
            goto fail;
        }
    }

    memset(&rules, 0, sizeof(rules));
    rules.player_count = 2;
    rules.stacks[0] = rules.stacks[1] = options.stack;
    rules.small_blind = options.small_blind;
    rules.big_blind = options.big_blind;
    rules.min_raise = options.min_raise;
    rules.raise_cap = 0;
    rules.raise_count = options.raise_count;
    rules.allow_nonallin_call = options.allow_nonallin_call;
    rules.showdown_samples = options.showdown_samples;
    rules.showdown_seed = options.seed;
    memcpy(rules.raise_sizes, options.raise_sizes, sizeof(rules.raise_sizes));
    game = pe_preflop_allin_game_create(&rules, ranges);
    if (!game) {
        fprintf(stderr, "could not create preflop game\n");
        goto fail;
    }

    config = pe_solver_config_default();
    config.algorithm.preset = PE_PRESET_EXTERNAL_MCCFR;
    config.execution.backend = PE_COMPUTE_CPU_REF;
    config.execution.stages.traversal = PE_COMPUTE_CPU_REF;
    config.execution.stages.update = PE_COMPUTE_CPU_REF;
    config.execution.stages.terminal_eval = PE_COMPUTE_CPU_REF;
    config.execution.deterministic = 1;
    config.execution.sample_batch_size = 1u;
    config.problem.expected_infosets = 4096u;
    config.problem.expected_actions = 8u;
    config.problem.expected_combos = 1u;
    config.max_iterations = options.iterations;
    config.seed = options.seed;
    deps = pe_solver_deps_default();
    deps.external_game = pe_preflop_allin_external(game);
    solver = pe_solver_create(&config, &deps);
    status = solver ? pe_solver_run(solver) : PE_SOLVER_ERR_OUT_OF_MEMORY;
    if (solver)
        (void)pe_solver_progress(solver, &progress);
    if (solver)
        (void)pe_solver_metrics(solver, &metrics);
    if (status != PE_SOLVER_OK) {
        fprintf(stderr, "preflop solve failed: status=%d\n", (int)status);
        goto fail;
    }
    {
        size_t infosets = pe_preflop_allin_infodesc_count(game);
        printf("preflop_solver=lane-b external-mccfr game=holdem players=2\n");
        printf("iterations=%" PRIu64 " complete=%d infosets=%zu\n",
               progress.iteration, progress.complete, infosets);
        printf("guarantee=%s exploitability=not-available (BR requires a solved policy)\n",
               guarantee_name(metrics.guarantee));
        if (options.output)
            write_report(options.output, &options, &metrics, &progress, infosets);
    }
    pe_solver_destroy(solver);
    pe_preflop_allin_game_destroy(game);
    pe_range_free(ranges[0]);
    pe_range_free(ranges[1]);
    return 0;

fail:
    pe_solver_destroy(solver);
    pe_preflop_allin_game_destroy(game);
    pe_range_free(ranges[0]);
    pe_range_free(ranges[1]);
    return 1;
}
