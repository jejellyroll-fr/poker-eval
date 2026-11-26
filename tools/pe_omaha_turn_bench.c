/*
 * pe-omaha-turn-bench - the turn terminal's cost in PLO4, PLO5 and PLO6.
 *
 * Issue #285, phase B.  One configuration per invocation, so the peak resident
 * size the OS reports belongs to that configuration alone and not to the
 * largest one that ran before it.  The ranges are drawn deterministically from
 * the deck the board leaves, and the document also carries the exact number of
 * legal private-hand tuples, because a turn pays one river terminal per
 * candidate river: the tuple count is what makes PLO5 and PLO6 expensive.
 *
 * Nothing is sampled and nothing is extrapolated.  The reported seconds are
 * the whole terminal call, medians over `--repetitions`; the deal count is
 * exact - the walk visits every legal tuple and every river card such a tuple
 * leaves in the deck - so a measured cost per deal can be read off directly
 * and turned into a range-size budget in
 * docs/cfr/guides/plo_turn_scalability.md.
 *
 * `--threads N` adds a second measurement: the candidate rivers are independent
 * calls, so they can be fanned out over N lanes, each with its own eval context
 * and compaction buffers.  The per-river results are collected by card and
 * reduced in card order with the serial terminal's arithmetic, which lets the
 * benchmark assert the two agree bit for bit instead of approximately; the lane
 * figure is therefore the same work, not an estimate of it.
 */

#include <poker_eval/core/eval_context.h>
#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/core/pthread_compat.h>
#include <poker_eval/solver/pe_betting_state.h>
#include <poker_eval/solver/pe_omaha_deals.h>
#include <poker_eval/solver/pe_omaha_river.h>
#include <poker_eval/solver/pe_omaha_turn.h>
#include <poker_eval/solver/pe_showdown.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/resource.h>
#include <time.h>
#endif

#if defined(__APPLE__)
#include <mach/mach_time.h>
#endif

#define BENCH_SEED 20261126ULL
#define BENCH_DEFAULT_REPETITIONS 3u
/* Above this, the duplicate check would cost more than it is worth: random
 * draws from a space of at least C(48,4) collide rarely, and one repeated
 * private hand only shifts one point of the range. */
#define BENCH_DEDUPE_LIMIT 4096u
#define BENCH_MAX_COMBOS 262144u
#define BENCH_MAX_THREADS 64u

typedef struct
{
    uint8_t holes;
    uint8_t players;
    uint8_t threads;
    size_t combos;
    size_t repetitions;
    uint64_t seed;
} bench_config_t;

static int g_failures;

static void bench_fail(const char *message)
{
    fprintf(stderr, "pe-omaha-turn-bench: %s\n", message);
    ++g_failures;
}

static uint64_t lcg_next(uint64_t *state)
{
    *state = *state * 6364136223846793005ULL + 1442695040888963407ULL;
    return *state >> 33;
}

/* A turn board: As Kd 7c 2h, the phase A fixture's board. */
static mask_t turn_board_mask(void)
{
    static const int cards[PE_OMAHA_TURN_BOARD_CARDS] = {
        MODERN_MAKE_CARD(MODERN_RANK_A, MODERN_SUIT_SPADES),
        MODERN_MAKE_CARD(MODERN_RANK_K, MODERN_SUIT_DIAMONDS),
        MODERN_MAKE_CARD(MODERN_RANK_7, MODERN_SUIT_CLUBS),
        MODERN_MAKE_CARD(MODERN_RANK_2, MODERN_SUIT_HEARTS),
    };
    mask_t board = MASK_EMPTY;
    size_t index;

    for (index = 0u; index < PE_OMAHA_TURN_BOARD_CARDS; ++index)
        board = mask_set(board, cards[index]);
    return board;
}

/* Deterministic weighted ranges: every eleventh combo carries zero weight, the
 * rest spread over [0.25, 1.0].  A zero-weight combo is still in the range, so
 * the walk has to skip it, which is what a caller's filtered range looks
 * like. */
static int fill_range(mask_t board, uint8_t holes, size_t count, uint64_t seed,
                      pe_omaha_combo_t *combos)
{
    int live[MODERN_DECK_SIZE];
    int live_count = 0;
    int card;
    uint64_t state = seed;
    size_t index;
    int dedupe = count <= BENCH_DEDUPE_LIMIT;

    for (card = 0; card < MODERN_DECK_SIZE; ++card)
        if (!mask_is_set(board, card))
            live[live_count++] = card;

    for (index = 0u; index < count; ++index)
    {
        mask_t hand = MASK_EMPTY;
        size_t draws = 0u;
        size_t previous;
        int duplicate;

        do
        {
            size_t pick;

            hand = MASK_EMPTY;
            for (pick = 0u; pick < (size_t)holes; ++pick)
            {
                size_t slot =
                    (size_t)(lcg_next(&state) % (uint64_t)live_count);

                hand = mask_set(hand, live[slot]);
            }
            if (++draws > 1000000u)
                return -1;
            duplicate = 0;
            if (dedupe)
                for (previous = 0u; previous < index; ++previous)
                    if (combos[previous].cards == hand)
                    {
                        duplicate = 1;
                        break;
                    }
            /* A repeated pick leaves a short hand, so the card count is checked
             * on every draw whether or not duplicates are rejected. */
        } while (mask_popcount(hand) != (int)holes || duplicate);

        combos[index].cards = hand;
        combos[index].weight =
            (index % 11u == 7u)
                ? 0.0
                : 0.25 + 0.75 * (double)((index * 37u + 13u) % 17u) / 16.0;
    }
    return 0;
}

static double monotonic_seconds(void)
{
#if defined(__APPLE__)
    /* mach_absolute_time reports nanoseconds; clock_gettime is too coarse for
     * the smallest configurations on macOS. */
    static mach_timebase_info_data_t timebase;
    uint64_t ticks;

    if (timebase.denom == 0u)
        mach_timebase_info(&timebase);
    ticks = mach_absolute_time();
    return (double)ticks * (double)timebase.numer /
           ((double)timebase.denom * 1e9);
#elif defined(_WIN32)
    static LARGE_INTEGER frequency;
    LARGE_INTEGER counter;

    if (frequency.QuadPart == 0)
        QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)frequency.QuadPart;
#else
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
#endif
}

/* Peak resident set size in bytes, or 0 when the platform does not expose it.
 * ru_maxrss is bytes on macOS and kilobytes on Linux. */
static uint64_t peak_rss_bytes(void)
{
#if defined(_WIN32)
    return 0u;
#else
    struct rusage usage;

    if (getrusage(RUSAGE_SELF, &usage) != 0)
        return 0u;
#if defined(__APPLE__)
    return (uint64_t)usage.ru_maxrss;
#else
    return (uint64_t)usage.ru_maxrss * 1024u;
#endif
#endif
}

static uint64_t fingerprint_values(const double *values, size_t count)
{
    uint64_t hash = 1469598103934665603ULL;
    size_t index;

    for (index = 0u; index < count; ++index)
    {
        union
        {
            double value;
            uint64_t bits;
        } pun;

        pun.value = values[index];
        hash ^= pun.bits;
        hash *= 1099511628211ULL;
    }
    return hash;
}

static int compare_double(const void *left, const void *right)
{
    double a = *(const double *)left;
    double b = *(const double *)right;

    if (a < b)
        return -1;
    if (a > b)
        return 1;
    return 0;
}

static double median_of(double *values, size_t count)
{
    qsort(values, count, sizeof(*values), compare_double);
    if (count % 2u == 0u && count > 1u)
        return 0.5 * (values[count / 2u - 1u] + values[count / 2u]);
    return values[count / 2u];
}

/* The combos that do not hold `blocked`, copied out.  Returns the count kept,
 * 0 when the card empties the range, or -1 when the copy cannot be made. */
static long compact_range(const pe_omaha_range_t *range, mask_t blocked,
                          pe_omaha_combo_t **out)
{
    size_t kept = 0u;
    size_t index;
    pe_omaha_combo_t *copy;

    for (index = 0u; index < range->count; ++index)
        if ((range->combos[index].cards & blocked) == 0u)
            ++kept;
    if (kept == 0u)
        return 0l;
    copy = (pe_omaha_combo_t *)malloc(kept * sizeof(*copy));
    if (!copy)
        return -1l;
    kept = 0u;
    for (index = 0u; index < range->count; ++index)
        if ((range->combos[index].cards & blocked) == 0u)
            copy[kept++] = range->combos[index];
    *out = copy;
    return (long)kept;
}

static const char *path_name(pe_showdown_path_t path)
{
    switch (path)
    {
    case PE_SHOWDOWN_PATH_PLO4:
        return "plo4_sweep";
    case PE_SHOWDOWN_PATH_SORTED:
        return "sorted";
    case PE_SHOWDOWN_PATH_PAIRWISE:
        return "pairwise";
    case PE_SHOWDOWN_PATH_MULTIWAY:
        return "multiway";
    case PE_SHOWDOWN_PATH_NONE:
    default:
        return "none";
    }
}

/* One lane of the river fan-out.  A lane owns its eval context and its
 * compaction buffers, and it writes only the cards it prices; the results are
 * collected by card so the reduction can run in card order, which is the order
 * the serial terminal uses.  That is what lets a parallel run be compared with
 * the serial one bit for bit instead of approximately. */
typedef struct
{
    const pe_omaha_range_t *ranges;
    const pe_betting_state_t *state;
    EvalContext *context;
    pe_omaha_combo_t *filtered[PE_OMAHA_TURN_PLAYERS_MAX];
    double (*values)[PE_OMAHA_TURN_PLAYERS_MAX];
    double *mass;
    signed char *priced;
    mask_t board;
    mask_t dead;
    uint8_t hole_cards;
    uint8_t players;
    uint8_t lane;
    uint8_t lanes;
} turn_lane_t;

/* The turn's own loop, restricted to the cards `card % lanes == lane`: compact
 * every range for the river, skip the river when any range empties, and let a
 * -1 from the river terminal mean "no deal on this river" exactly as it does
 * in pe_omaha_turn_range_values(). */
static void *turn_lane_run(void *argument)
{
    turn_lane_t *lane = (turn_lane_t *)argument;
    int card;

    for (card = (int)lane->lane; card < MODERN_DECK_SIZE;
         card += (int)lane->lanes)
    {
        pe_omaha_range_t compacted[PE_OMAHA_TURN_PLAYERS_MAX];
        double river_values[PE_OMAHA_TURN_PLAYERS_MAX];
        size_t deal_count = 0u;
        double river_mass = 0.0;
        int usable = 1;
        uint8_t player;
        int status;

        lane->priced[card] = 0;
        if (mask_is_set(lane->board, card) || mask_is_set(lane->dead, card))
            continue;
        for (player = 0u; player < lane->players; ++player)
        {
            size_t kept = 0u;
            size_t index;

            for (index = 0u; index < lane->ranges[player].count; ++index)
                if ((lane->ranges[player].combos[index].cards &
                     ((mask_t)1u << card)) == 0u)
                    lane->filtered[player][kept++] =
                        lane->ranges[player].combos[index];
            compacted[player].combos = lane->filtered[player];
            compacted[player].count = kept;
            if (kept == 0u)
            {
                usable = 0;
                break;
            }
        }
        if (!usable)
            continue;
        status = pe_omaha_river_range_values(
            lane->context, mask_set(lane->board, card), compacted, lane->state,
            lane->hole_cards, river_values, lane->players, &deal_count,
            &river_mass);
        if (status == -1)
            continue;
        if (status != 0)
        {
            lane->priced[card] = -1;
            continue;
        }
        lane->priced[card] = 1;
        lane->mass[card] = river_mass;
        for (player = 0u; player < lane->players; ++player)
            lane->values[card][player] = river_values[player];
    }
    return NULL;
}

/* Combine the lanes in card order, with the serial terminal's own arithmetic
 * and its own status codes: 0 with values in chips per deal, 1 when no river
 * carried mass, -1 when a lane reported a failure. */
static int reduce_rivers(const signed char *priced, const double *mass,
                         const double (*values)[PE_OMAHA_TURN_PLAYERS_MAX],
                         uint8_t players, double *out_values,
                         size_t *out_rivers, double *out_mass)
{
    double accumulated[PE_OMAHA_TURN_PLAYERS_MAX] = {0.0};
    double total = 0.0;
    size_t rivers = 0u;
    uint8_t player;
    int card;

    for (card = 0; card < MODERN_DECK_SIZE; ++card)
    {
        if (priced[card] < 0)
            return -1;
        if (priced[card] == 0)
            continue;
        ++rivers;
        for (player = 0u; player < players; ++player)
            accumulated[player] += values[card][player] * mass[card];
        total += mass[card];
    }
    if (!(total > 0.0))
        return 1;
    for (player = 0u; player < players; ++player)
        out_values[player] = accumulated[player] / total;
    *out_rivers = rivers;
    *out_mass = total;
    return 0;
}

/* Bitwise equality: -Wfloat-equal forbids the == a comparison here would use,
 * and "the same answer" is meant literally. */
static int same_double(double left, double right)
{
    return memcmp(&left, &right, sizeof(left)) == 0;
}

static int parse_size(const char *text, size_t minimum, size_t maximum,
                      size_t *out)
{
    char *end = NULL;
    unsigned long value;

    if (!text || !out)
        return -1;
    value = strtoul(text, &end, 10);
    if (!end || *end != '\0' || value < (unsigned long)minimum ||
        value > (unsigned long)maximum)
        return -1;
    *out = (size_t)value;
    return 0;
}

static void usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s --holes 4|5|6 --combos N [--players N] "
            "[--threads N] [--repetitions N] [--seed N]\n"
            "\n"
            "Emits a pe-omaha-turn-bench/v1 JSON document on stdout.\n"
            "N is the combo count of every range; one configuration per run, so\n"
            "the reported peak RSS belongs to this configuration alone.\n"
            "--threads fans the candidate rivers out over N lanes, each with its\n"
            "own eval context, and reports that time next to the serial terminal.\n",
            program);
}

int main(int argc, char **argv)
{
    bench_config_t config;
    EvalConfig eval_config = eval_config_holdem();
    EvalContext *context = NULL;
    pe_omaha_combo_t *storage[PE_OMAHA_TURN_PLAYERS_MAX] = {0};
    pe_omaha_combo_t *filtered_storage[PE_OMAHA_TURN_PLAYERS_MAX] = {0};
    pe_omaha_range_t ranges[PE_OMAHA_TURN_PLAYERS_MAX];
    pe_omaha_range_t filtered[PE_OMAHA_TURN_PLAYERS_MAX];
    pe_betting_state_t state;
    double *durations = NULL;
    double *sorted = NULL;
    double *parallel_durations = NULL;
    double *parallel_sorted = NULL;
    double values[PE_OMAHA_TURN_PLAYERS_MAX] = {0.0};
    double river_values[PE_OMAHA_TURN_PLAYERS_MAX] = {0.0};
    mask_t board = turn_board_mask();
    size_t rivers = 0u;
    double mass = 0.0;
    size_t legal_pairs = 0u;
    double pair_mass = 0.0;
    size_t river_deals = 0u;
    double river_mass = 0.0;
    double river_seconds = 0.0;
    pe_showdown_path_t river_path = PE_SHOWDOWN_PATH_NONE;
    double turn_min;
    double turn_max;
    double turn_median;
    size_t deals_per_pair;
    double deals_total;
    uint64_t fingerprint = 0u;
    int deterministic = 1;
    size_t repeat;
    uint8_t player;
    turn_lane_t *lanes = NULL;
    pthread_t *handles = NULL;
    double(*lane_values)[PE_OMAHA_TURN_PLAYERS_MAX] = NULL;
    double *lane_mass = NULL;
    signed char *lane_priced = NULL;
    double parallel_values[PE_OMAHA_TURN_PLAYERS_MAX] = {0.0};
    size_t parallel_rivers = 0u;
    double parallel_mass = 0.0;
    double parallel_min = 0.0;
    double parallel_max = 0.0;
    double parallel_median = 0.0;
    uint64_t parallel_fingerprint = 0u;
    int parallel_ran = 0;
    int parallel_matches = 0;
    int parallel_status = 0;
    int river_card = -1;
    int enumerates;
    int status;
    int arg;
    int have_holes = 0;
    int have_combos = 0;

    memset(&config, 0, sizeof(config));
    config.holes = 4u;
    config.players = 2u;
    config.threads = 1u;
    config.combos = 1000u;
    config.repetitions = BENCH_DEFAULT_REPETITIONS;
    config.seed = BENCH_SEED;
    memset(ranges, 0, sizeof(ranges));

    for (arg = 1; arg < argc; ++arg)
    {
        size_t parsed;

        if (strcmp(argv[arg], "--help") == 0 || strcmp(argv[arg], "-h") == 0)
        {
            usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[arg], "--holes") == 0 && arg + 1 < argc)
        {
            if (parse_size(argv[++arg], 4u, 6u, &parsed) != 0)
            {
                usage(argv[0]);
                return 2;
            }
            config.holes = (uint8_t)parsed;
            have_holes = 1;
            continue;
        }
        if (strcmp(argv[arg], "--combos") == 0 && arg + 1 < argc)
        {
            if (parse_size(argv[++arg], 1u, BENCH_MAX_COMBOS, &parsed) != 0)
            {
                usage(argv[0]);
                return 2;
            }
            config.combos = parsed;
            have_combos = 1;
            continue;
        }
        if (strcmp(argv[arg], "--players") == 0 && arg + 1 < argc)
        {
            if (parse_size(argv[++arg], 2u, PE_OMAHA_TURN_PLAYERS_MAX,
                           &parsed) != 0)
            {
                usage(argv[0]);
                return 2;
            }
            config.players = (uint8_t)parsed;
            continue;
        }
        if (strcmp(argv[arg], "--threads") == 0 && arg + 1 < argc)
        {
            if (parse_size(argv[++arg], 1u, BENCH_MAX_THREADS, &parsed) != 0)
            {
                usage(argv[0]);
                return 2;
            }
            config.threads = (uint8_t)parsed;
            continue;
        }
        if (strcmp(argv[arg], "--repetitions") == 0 && arg + 1 < argc)
        {
            if (parse_size(argv[++arg], 1u, 1000u, &parsed) != 0)
            {
                usage(argv[0]);
                return 2;
            }
            config.repetitions = parsed;
            continue;
        }
        if (strcmp(argv[arg], "--seed") == 0 && arg + 1 < argc)
        {
            if (parse_size(argv[++arg], 1u, 0x7fffffffu, &parsed) != 0)
            {
                usage(argv[0]);
                return 2;
            }
            config.seed = (uint64_t)parsed;
            continue;
        }
        usage(argv[0]);
        return 2;
    }

    if (!have_holes || !have_combos)
    {
        usage(argv[0]);
        return 2;
    }

    durations = (double *)calloc(config.repetitions, sizeof(*durations));
    sorted = (double *)calloc(config.repetitions, sizeof(*sorted));
    if (!durations || !sorted)
    {
        bench_fail("allocation failed");
        goto cleanup;
    }
    context = eval_context_create(&eval_config);
    if (!context)
    {
        bench_fail("could not create the eval context");
        goto cleanup;
    }

    for (player = 0u; player < config.players; ++player)
    {
        storage[player] = (pe_omaha_combo_t *)calloc(
            config.combos, sizeof(*storage[player]));
        if (!storage[player])
        {
            bench_fail("range allocation failed");
            goto cleanup;
        }
        if (fill_range(board, config.holes, config.combos,
                       config.seed ^ ((uint64_t)player + 1u) * 0x9e3779b97f4a7c15ULL,
                       storage[player]) != 0)
        {
            bench_fail("could not draw distinct combos");
            goto cleanup;
        }
        ranges[player].combos = storage[player];
        ranges[player].count = config.combos;
    }

    memset(&state, 0, sizeof(state));
    state.player_count = config.players;
    state.pot = 63.0;
    state.winner = -1;
    /* Equal contributions that add up to the pot: 31.5 heads-up, 21 three-way. */
    for (player = 0u; player < config.players; ++player)
    {
        state.invested[player] = state.pot / (double)config.players;
        state.active[player] = 1;
    }

    /* The turn reaches PLO5 and PLO6 through the river terminal's deal walk,
     * which visits every legal tuple; four-card heads-up ranges of at least
     * PE_SHOWDOWN_PLO4_MIN_OPPONENTS combos take the vector sweep instead and
     * visit no tuple at all.  Counting tuples is quadratic, so it is only done
     * for the configurations whose cost is per tuple. */
    enumerates = !(config.holes == 4u && config.players == 2u &&
                   config.combos >= PE_SHOWDOWN_PLO4_MIN_OPPONENTS);
    if (enumerates &&
        pe_omaha_deals_measure(board, ranges, config.players, config.holes,
                              &legal_pairs, &pair_mass) != 0)
    {
        bench_fail("could not measure the legal private-hand tuples");
        goto cleanup;
    }

    /* Single-river reference: one candidate river, with the combos that card
     * blocks removed exactly as the turn removes them.  This is what shows
     * which showdown path the wider private hands take, and it makes the turn's
     * cost checkable as a multiple of one river. */
    memset(filtered, 0, sizeof(filtered));
    for (river_card = 0; river_card < MODERN_DECK_SIZE; ++river_card)
    {
        uint8_t player_loop;
        int usable = !mask_is_set(board, river_card);
        double start;

        for (player_loop = 0u; usable && player_loop < config.players;
             ++player_loop)
        {
            long kept = compact_range(&ranges[player_loop],
                                      (mask_t)1u << river_card,
                                      &filtered_storage[player_loop]);

            if (kept < 0l)
            {
                bench_fail("allocation failed");
                goto cleanup;
            }
            if (kept == 0l)
                usable = 0;
            else
            {
                filtered[player_loop].combos = filtered_storage[player_loop];
                filtered[player_loop].count = (size_t)kept;
            }
        }
        if (!usable)
        {
            for (player_loop = 0u; player_loop < config.players; ++player_loop)
            {
                free(filtered_storage[player_loop]);
                filtered_storage[player_loop] = NULL;
            }
            continue;
        }
        start = monotonic_seconds();
        status = pe_omaha_river_range_values_with_path(
            context, mask_set(board, river_card), filtered, &state, config.holes,
            river_values, config.players, &river_deals, &river_mass,
            &river_path);
        river_seconds = monotonic_seconds() - start;
        for (player_loop = 0u; player_loop < config.players; ++player_loop)
        {
            free(filtered_storage[player_loop]);
            filtered_storage[player_loop] = NULL;
        }
        if (status != 0)
        {
            bench_fail("the single-river reference failed");
            goto cleanup;
        }
        break;
    }

    for (repeat = 0u; repeat < config.repetitions; ++repeat)
    {
        uint64_t run_fingerprint;
        double start = monotonic_seconds();

        status = pe_omaha_turn_range_values(
            context, board, MASK_EMPTY, ranges, &state, config.holes, values,
            config.players, &rivers, &mass);
        durations[repeat] = monotonic_seconds() - start;
        if (status != 0)
        {
            bench_fail("the turn terminal failed");
            goto cleanup;
        }
        run_fingerprint = fingerprint_values(values, config.players);
        if (repeat == 0u)
            fingerprint = run_fingerprint;
        else if (run_fingerprint != fingerprint)
            deterministic = 0;
    }

    /* The candidate rivers are independent calls with no shared state, so the
     * same work can be fanned out over N lanes.  The contexts are built before
     * the clock starts, because a caller would pool them too. */
    if (config.threads > 1u)
    {
        size_t lane_count = (size_t)config.threads;
        size_t lane_index;
        int setup_ok = 1;

        lanes = (turn_lane_t *)calloc(lane_count, sizeof(*lanes));
        handles = (pthread_t *)calloc(lane_count, sizeof(*handles));
        parallel_durations =
            (double *)calloc(config.repetitions, sizeof(*parallel_durations));
        parallel_sorted =
            (double *)calloc(config.repetitions, sizeof(*parallel_sorted));
        lane_values = (double(*)[PE_OMAHA_TURN_PLAYERS_MAX])calloc(
            MODERN_DECK_SIZE, sizeof(*lane_values));
        lane_mass = (double *)calloc(MODERN_DECK_SIZE, sizeof(*lane_mass));
        lane_priced =
            (signed char *)calloc(MODERN_DECK_SIZE, sizeof(*lane_priced));
        if (!lanes || !handles || !parallel_durations || !parallel_sorted ||
            !lane_values || !lane_mass || !lane_priced)
        {
            bench_fail("parallel allocation failed");
            goto cleanup;
        }

        for (lane_index = 0u; lane_index < lane_count; ++lane_index)
        {
            turn_lane_t *lane = &lanes[lane_index];
            uint8_t lead;

            lane->ranges = ranges;
            lane->state = &state;
            lane->board = board;
            lane->dead = MASK_EMPTY;
            lane->hole_cards = config.holes;
            lane->players = config.players;
            lane->lane = (uint8_t)lane_index;
            lane->lanes = config.threads;
            lane->values = lane_values;
            lane->mass = lane_mass;
            lane->priced = lane_priced;
            lane->context = eval_context_create(&eval_config);
            if (!lane->context)
            {
                bench_fail("could not create an eval context per lane");
                setup_ok = 0;
                break;
            }
            for (lead = 0u; lead < config.players; ++lead)
            {
                lane->filtered[lead] = (pe_omaha_combo_t *)malloc(
                    config.combos * sizeof(*lane->filtered[lead]));
                if (!lane->filtered[lead])
                {
                    bench_fail("lane range allocation failed");
                    setup_ok = 0;
                    break;
                }
            }
            if (!setup_ok)
                break;
        }
        if (!setup_ok)
            goto cleanup;

        for (repeat = 0u; repeat < config.repetitions && setup_ok; ++repeat)
        {
            size_t index_loop;
            size_t started = 0u;
            double start;

            for (index_loop = 0u; index_loop < MODERN_DECK_SIZE; ++index_loop)
                lane_priced[index_loop] = 0;
            start = monotonic_seconds();
            for (index_loop = 0u; index_loop < lane_count; ++index_loop)
            {
                if (pthread_create(&handles[index_loop], NULL, turn_lane_run,
                                   &lanes[index_loop]) != 0)
                {
                    bench_fail("could not start a river lane");
                    setup_ok = 0;
                    break;
                }
                ++started;
            }
            for (index_loop = 0u; index_loop < started; ++index_loop)
                pthread_join(handles[index_loop], NULL);
            if (!setup_ok)
                goto cleanup;
            parallel_durations[repeat] = monotonic_seconds() - start;
            parallel_status = reduce_rivers(
                (const signed char *)lane_priced, (const double *)lane_mass,
                (const double(*)[PE_OMAHA_TURN_PLAYERS_MAX])lane_values,
                config.players, parallel_values, &parallel_rivers,
                &parallel_mass);
            parallel_fingerprint =
                fingerprint_values(parallel_values, config.players);
            parallel_ran = 1;
        }

        if (parallel_ran)
        {
            for (repeat = 0u; repeat < config.repetitions; ++repeat)
                parallel_sorted[repeat] = parallel_durations[repeat];
            parallel_median = median_of(parallel_sorted, config.repetitions);
            parallel_min = parallel_sorted[0];
            parallel_max = parallel_sorted[config.repetitions - 1u];
            parallel_matches =
                parallel_status == status && parallel_rivers == rivers &&
                same_double(parallel_mass, mass) &&
                parallel_fingerprint == fingerprint ? 1 : 0;
            if (!parallel_matches)
                bench_fail("the parallel lanes disagree with the serial terminal");
        }
    }

    for (repeat = 0u; repeat < config.repetitions; ++repeat)
        sorted[repeat] = durations[repeat];
    turn_median = median_of(sorted, config.repetitions);
    turn_min = sorted[0];
    turn_max = sorted[config.repetitions - 1u];

    /* Every legal tuple sees the same number of candidate rivers, so the turn's
     * exact deal count is the tuple count times the cards left by the board
     * and the hands. */
    deals_per_pair = (size_t)(MODERN_DECK_SIZE - PE_OMAHA_TURN_BOARD_CARDS) -
                     (size_t)config.holes * (size_t)config.players;
    deals_total = (double)legal_pairs * (double)deals_per_pair;

    printf("{\n");
    printf("  \"schema\": \"pe-omaha-turn-bench/v1\",\n");
    printf("  \"generated_by\": \"pe-omaha-turn-bench\",\n");
    printf("  \"issue\": 285,\n");
    printf("  \"phase\": \"B\",\n");
    printf("  \"board\": \"AsKd7c2h\",\n");
    printf("  \"dead_cards\": 0,\n");
    printf("  \"holes\": %u,\n", (unsigned)config.holes);
    printf("  \"players\": %u,\n", (unsigned)config.players);
    printf("  \"threads\": %u,\n", (unsigned)config.threads);
    printf("  \"combos_per_player\": %zu,\n", config.combos);
    printf("  \"repetitions\": %zu,\n", config.repetitions);
    printf("  \"seed\": %llu,\n", (unsigned long long)config.seed);
    printf("  \"exact\": true,\n");
    printf("  \"sampled\": false,\n");
    printf("  \"pot\": 63,\n");
    printf("  \"turn_seconds\": {\"min\": %.9f, \"median\": %.9f, \"max\": %.9f},\n",
           turn_min, turn_median, turn_max);
    printf("  \"rivers_priced\": %zu,\n", rivers);
    printf("  \"weight_sum\": %.9f,\n", mass);
    if (enumerates)
    {
        printf("  \"legal_pairs\": %zu,\n", legal_pairs);
        printf("  \"pair_density\": %.9f,\n",
               config.combos > 0u
                   ? (double)legal_pairs /
                         ((double)config.combos * (double)config.combos)
                   : 0.0);
        printf("  \"rivers_per_pair\": %zu,\n", deals_per_pair);
        printf("  \"deals_total\": %.0f,\n", deals_total);
        printf("  \"deals_per_second\": %.3f,\n",
               turn_median > 0.0 ? deals_total / turn_median : 0.0);
        printf("  \"seconds_per_deal\": %.12f,\n",
               deals_total > 0.0 ? turn_median / deals_total : 0.0);
    }
    else
        printf("  \"legal_pairs\": null,\n  \"pair_density\": null,\n"
               "  \"rivers_per_pair\": null,\n  \"deals_total\": null,\n"
               "  \"deals_per_second\": null,\n"
               "  \"seconds_per_deal\": null,\n");
    printf("  \"seconds_per_combo\": %.12f,\n",
           config.combos > 0u
               ? turn_median /
                     ((double)config.combos * (double)config.players)
               : 0.0);
    if (river_seconds > 0.0)
    {
        printf("  \"river_reference\": {\"card\": %d, \"seconds\": %.9f, "
               "\"deals\": %zu, \"weight_sum\": %.9f, \"path\": \"%s\", "
               "\"seconds_per_deal\": ",
               river_card, river_seconds, river_deals, river_mass,
               path_name(river_path));
        /* The vector sweep reports no deal count: it evaluates combos, not
         * tuples, so there is nothing to divide by. */
        if (river_deals > 0u)
            printf("%.12f},\n", river_seconds / (double)river_deals);
        else
            printf("null},\n");
    }
    else
        printf("  \"river_reference\": null,\n");
    printf("  \"turn_over_river\": %.3f,\n",
           river_seconds > 0.0 ? turn_median / river_seconds : 0.0);
    printf("  \"combo_bytes\": %zu,\n", sizeof(pe_omaha_combo_t));
    printf("  \"range_bytes\": %zu,\n",
           (size_t)config.players * config.combos * sizeof(pe_omaha_combo_t));
    printf("  \"peak_rss_bytes\": %llu,\n",
           (unsigned long long)peak_rss_bytes());
    printf("  \"values\": [");
    for (player = 0u; player < config.players; ++player)
        printf("%s%.12f", player == 0u ? "" : ", ", values[player]);
    printf("],\n");
    printf("  \"values_fingerprint\": \"%016llx\",\n",
           (unsigned long long)fingerprint);
    printf("  \"deterministic_across_repetitions\": %s,\n",
           deterministic ? "true" : "false");
    if (parallel_ran)
        printf("  \"parallel_rivers\": {\"threads\": %u, \"seconds\": "
               "{\"min\": %.9f, \"median\": %.9f, \"max\": %.9f}, "
               "\"speedup\": %.3f, \"matches_serial\": %s, "
               "\"values_fingerprint\": \"%016llx\", \"rivers_priced\": %zu, "
               "\"weight_sum\": %.9f}\n",
               (unsigned)config.threads, parallel_min, parallel_median,
               parallel_max,
               parallel_median > 0.0 ? turn_median / parallel_median : 0.0,
               parallel_matches ? "true" : "false",
               (unsigned long long)parallel_fingerprint, parallel_rivers,
               parallel_mass);
    else
        printf("  \"parallel_rivers\": null\n");
    printf("}\n");

cleanup:
    if (lanes)
    {
        size_t lane_index;

        for (lane_index = 0u; lane_index < (size_t)config.threads;
             ++lane_index)
        {
            uint8_t lead;

            for (lead = 0u; lead < PE_OMAHA_TURN_PLAYERS_MAX; ++lead)
                free(lanes[lane_index].filtered[lead]);
            if (lanes[lane_index].context)
                eval_context_destroy(lanes[lane_index].context);
        }
    }
    free(lane_priced);
    free(lane_mass);
    free(lane_values);
    free(parallel_sorted);
    free(parallel_durations);
    free(handles);
    free(lanes);
    for (player = 0u; player < PE_OMAHA_TURN_PLAYERS_MAX; ++player)
    {
        free(filtered_storage[player]);
        free(storage[player]);
    }
    free(sorted);
    free(durations);
    if (context)
        eval_context_destroy(context);
    return g_failures == 0 ? 0 : 1;
}
