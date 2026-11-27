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
 * `--profile` decomposes the serial call instead of timing it as a whole: the
 * terminal reports the per-river filter and the river terminal separately, with
 * the counters each phase visited, so the two shares can be read against the
 * same call's wall clock and the range size at which the terminal overtakes
 * the filter can be solved from the two measured costs.
 *
 * `--threads N` adds a second measurement: the candidate rivers are independent
 * calls, so they can be fanned out over N lanes, each with its own eval context
 * and compaction buffers.  The per-river results are collected by card and
 * reduced in card order with the serial terminal's arithmetic, which lets the
 * benchmark assert the two agree bit for bit instead of approximately; the lane
 * figure is therefore the same work, not an estimate of it.
 *
 * `--hilo` prices every river with the river terminal's Hi/Lo payoff instead of
 * the high-only one.  The chance expansion, the per-river filter and the
 * reduction are unchanged, so the two runs differ only in the payoff and in the
 * probe's extra low-table pass; `--frozen-strength --hilo` reports that pass
 * apart and still has to reproduce the live Hi/Lo walk bit for bit.  A Hi/Lo
 * turn reaches the deal walk at every range size, so `--hilo` enumerates where
 * a high-only run of the same shape would take the four-card sweep; the tuple
 * count is still only measured while its square stays affordable, and
 * `legal_pairs_measured` says which of the two the document carries.
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
/* Counting the legal private-hand tuples is quadratic in the range size.  It is
 * worth paying next to a turn that costs tens of seconds, and not next to a
 * range whose own square is larger than this. */
#define BENCH_PAIR_COUNT_LIMIT 4.0e7

typedef struct
{
    uint8_t holes;
    uint8_t players;
    uint8_t threads;
    size_t combos;
    size_t repetitions;
    uint64_t seed;
    int profile;
    int frozen_strength;
    int hilo8;
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

/* Copy the combos that do not hold a card of `blocked` into `out` and return
 * how many survived: the terminal's own rule, which drops a combo holding the
 * river card instead of leaving it in the range with a zero weight. */
static size_t compact_for_river(const pe_omaha_range_t *range, mask_t blocked,
                                pe_omaha_combo_t *out)
{
    size_t kept = 0u;
    size_t index;

    for (index = 0u; index < range->count; ++index)
        if ((range->combos[index].cards & blocked) == 0u)
            out[kept++] = range->combos[index];
    return kept;
}

/* The single-river reference keeps its compaction in a buffer of its own.
 * Returns the count kept, 0 when the card empties the range, or -1 when the
 * copy cannot be made. */
static long compact_range(const pe_omaha_range_t *range, mask_t blocked,
                          pe_omaha_combo_t **out)
{
    pe_omaha_combo_t *copy;
    size_t kept = 0u;
    size_t index;

    for (index = 0u; index < range->count; ++index)
        if ((range->combos[index].cards & blocked) == 0u)
            ++kept;
    if (kept == 0u)
        return 0l;
    copy = (pe_omaha_combo_t *)malloc(kept * sizeof(*copy));
    if (!copy)
        return -1l;
    *out = copy;
    return (long)compact_for_river(range, blocked, copy);
}

static const char *path_name(pe_showdown_path_t path)
{
    switch (path)
    {
    case PE_SHOWDOWN_PATH_PLO4:
        return "plo4_sweep";
    case PE_SHOWDOWN_PATH_PLO_WIDE:
        return "plo_wide_sweep";
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
    uint8_t hole_cards;
    uint8_t players;
    uint8_t lane;
    uint8_t lanes;
    int hilo8;
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
        if (mask_is_set(lane->board, card))
            continue;
        for (player = 0u; player < lane->players; ++player)
        {
            compacted[player].combos = lane->filtered[player];
            compacted[player].count = compact_for_river(
                &lane->ranges[player], (mask_t)1u << card,
                lane->filtered[player]);
            if (compacted[player].count == 0u)
            {
                usable = 0;
                break;
            }
        }
        if (!usable)
            continue;
        status = lane->hilo8
                     ? pe_omaha_hilo8_river_range_values(
                           lane->context, mask_set(lane->board, card),
                           compacted, lane->state, lane->hole_cards,
                           river_values, lane->players, &deal_count,
                           &river_mass)
                     : pe_omaha_river_range_values(
                           lane->context, mask_set(lane->board, card),
                           compacted, lane->state, lane->hole_cards,
                           river_values, lane->players, &deal_count,
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

/* Clear the card table, start every lane, wait for all of them and report the
 * elapsed seconds.  Setup happens once, outside the repetitions, so the figure
 * is the sweep and not the pools a caller would keep. */
static int run_lanes(turn_lane_t *lanes, pthread_t *handles, size_t lane_count,
                     double *seconds)
{
    size_t index;
    size_t started = 0u;
    double start;

    for (index = 0u; index < MODERN_DECK_SIZE; ++index)
        lanes[0].priced[index] = 0;
    start = monotonic_seconds();
    for (index = 0u; index < lane_count; ++index)
    {
        if (pthread_create(&handles[index], NULL, turn_lane_run,
                           &lanes[index]) != 0)
            break;
        ++started;
    }
    for (index = 0u; index < started; ++index)
        pthread_join(handles[index], NULL);
    *seconds = monotonic_seconds() - start;
    if (started < lane_count)
    {
        bench_fail("could not start a river lane");
        return -1;
    }
    return 0;
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
            "       [--profile] [--frozen-strength] [--hilo]\n"
            "\n"
            "Emits a pe-omaha-turn-bench/v1 JSON document on stdout.\n"
            "N is the combo count of every range; one configuration per run, so\n"
            "the reported peak RSS belongs to this configuration alone.\n"
            "--threads fans the candidate rivers out over N lanes, each with its\n"
            "own eval context, and reports that time next to the serial terminal.\n"
            "--profile splits the serial call into the per-river filter and\n"
            "the river terminal, with the counters each phase visited, and the\n"
            "range size at which the terminal overtakes the filter.\n"
            "--frozen-strength runs the same turn with every combo evaluated\n"
            "once per river and reports the precompute and the walk apart, so\n"
            "a visit can be priced with the evaluation taken out of it.\n"
            "--profile and --frozen-strength are two runs, not one split: with\n"
            "the probe, the terminal clock times the frozen river call, its\n"
            "precompute included, so the profile's terminal and the probe's\n"
            "walk are not subtracted from each other.  Price the walk against\n"
            "the precompute, and read the live terminal from a profile run.\n"
            "--sparse-blockers prices a heads-up high-only river through the\n"
            "wide inclusion-exclusion sweep instead of the deal walk, for 5 and 6\n"
            "hole cards; exact in real arithmetic, not bit-identical.\n"
            "--hilo prices every river with the Hi/Lo payoff; the expansion,\n"
            "the filter and the reduction are the high-only ones.  With\n"
            "--frozen-strength the probe fills the low table in a pass of its\n"
            "own and reports it as low_precompute_seconds.\n",
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
    pe_omaha_combo_t *lane_combos = NULL;
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
    int parallel_matches = 0;
    int parallel_status = 0;
    int river_card = -1;
    int enumerates;
    int counts_pairs;
    int status;
    int arg;
    int have_holes = 0;
    int have_combos = 0;
    int sparse_blockers = 0;
    pe_omaha_turn_stats_t *profile_runs = NULL;
    double *filter_durations = NULL;
    double *terminal_durations = NULL;
    double *filter_sorted = NULL;
    double *terminal_sorted = NULL;
    double filter_median = 0.0;
    double terminal_median = 0.0;
    double unaccounted_seconds = 0.0;
    double nanoseconds_per_visit = 0.0;
    double nanoseconds_per_deal = 0.0;
    int counts_stable = 1;
    double frozen_values[PE_OMAHA_TURN_PLAYERS_MAX] = {0.0};
    size_t frozen_rivers = 0u;
    double frozen_mass = 0.0;
    double *frozen_durations = NULL;
    double *frozen_sorted = NULL;
    double *walk_sorted = NULL;
    double *precompute_sorted = NULL;
    double *low_precompute_sorted = NULL;
    double probe_low_precompute = 0.0;
    pe_omaha_turn_probe_t *probe_runs = NULL;
    double frozen_median = 0.0;
    double probe_walk = 0.0;
    double probe_precompute = 0.0;
    uint64_t probe_fingerprint = 0u;
    int probe_matches = 0;

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
        if (strcmp(argv[arg], "--profile") == 0)
        {
            config.profile = 1;
            continue;
        }
        if (strcmp(argv[arg], "--frozen-strength") == 0)
        {
            config.frozen_strength = 1;
            continue;
        }
        if (strcmp(argv[arg], "--hilo") == 0)
        {
            config.hilo8 = 1;
            continue;
        }
        /* Diagnostic path of issue #332: the wide inclusion-exclusion sweep
         * replaces the deal walk for heads-up PLO5/PLO6. Off unless asked. */
        if (strcmp(argv[arg], "--sparse-blockers") == 0)
        {
            sparse_blockers = 1;
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
    pe_omaha_river_set_sparse_blockers(sparse_blockers);

    durations = (double *)calloc(config.repetitions, sizeof(*durations));
    sorted = (double *)calloc(config.repetitions, sizeof(*sorted));
    if (!durations || !sorted)
    {
        bench_fail("allocation failed");
        goto cleanup;
    }
    if (config.profile)
    {
        profile_runs = (pe_omaha_turn_stats_t *)calloc(
            config.repetitions, sizeof(*profile_runs));
        filter_durations =
            (double *)calloc(config.repetitions, sizeof(*filter_durations));
        terminal_durations =
            (double *)calloc(config.repetitions, sizeof(*terminal_durations));
        filter_sorted =
            (double *)calloc(config.repetitions, sizeof(*filter_sorted));
        terminal_sorted =
            (double *)calloc(config.repetitions, sizeof(*terminal_sorted));
        if (!profile_runs || !filter_durations || !terminal_durations ||
            !filter_sorted || !terminal_sorted)
        {
            bench_fail("profile allocation failed");
            goto cleanup;
        }
    }
    if (config.frozen_strength)
    {
        frozen_durations =
            (double *)calloc(config.repetitions, sizeof(*frozen_durations));
        frozen_sorted =
            (double *)calloc(config.repetitions, sizeof(*frozen_sorted));
        walk_sorted =
            (double *)calloc(config.repetitions, sizeof(*walk_sorted));
        precompute_sorted =
            (double *)calloc(config.repetitions, sizeof(*precompute_sorted));
        low_precompute_sorted = (double *)calloc(
            config.repetitions, sizeof(*low_precompute_sorted));
        probe_runs = (pe_omaha_turn_probe_t *)calloc(
            config.repetitions, sizeof(*probe_runs));
        if (!frozen_durations || !frozen_sorted || !walk_sorted ||
            !precompute_sorted || !low_precompute_sorted || !probe_runs)
        {
            bench_fail("probe allocation failed");
            goto cleanup;
        }
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
     * visit no tuple at all.  A Hi/Lo turn has no sweep -- every Hi/Lo river is
     * priced by the deal walk -- so a Hi/Lo run enumerates at every range
     * size. */
    enumerates = config.hilo8 || !(config.holes == 4u && config.players == 2u &&
                                   config.combos >=
                                       PE_SHOWDOWN_PLO4_MIN_OPPONENTS);
    /* The count itself is quadratic, so it is measured only while the square is
     * affordable; past that the JSON reports that it was not measured rather
     * than implying the walk visits no tuple. */
    counts_pairs = enumerates && (double)config.combos * (double)config.combos <=
                                      BENCH_PAIR_COUNT_LIMIT;
    if (counts_pairs &&
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
        /* The Hi/Lo entry reports no showdown path: it prices the deal walk
         * only, which is what a wider board takes. */
        if (config.hilo8)
            status = pe_omaha_hilo8_river_range_values(
                context, mask_set(board, river_card), filtered, &state,
                config.holes, river_values, config.players, &river_deals,
                &river_mass);
        else
            status = pe_omaha_river_range_values_with_path(
                context, mask_set(board, river_card), filtered, &state,
                config.holes, river_values, config.players, &river_deals,
                &river_mass, &river_path);
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

        if (config.hilo8)
            status = pe_omaha_hilo8_turn_range_values(
                context, board, MASK_EMPTY, ranges, &state, config.holes,
                values, config.players, &rivers, &mass,
                config.profile ? &profile_runs[repeat] : NULL, NULL);
        else
            status = config.profile
                         ? pe_omaha_turn_range_values_profiled(
                               context, board, MASK_EMPTY, ranges, &state,
                               config.holes, values, config.players, &rivers,
                               &mass, &profile_runs[repeat])
                         : pe_omaha_turn_range_values(
                               context, board, MASK_EMPTY, ranges, &state,
                               config.holes, values, config.players, &rivers,
                               &mass);
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
        size_t buffer_count =
            lane_count * (size_t)config.players * config.combos;
        size_t lane_index;

        lanes = (turn_lane_t *)calloc(lane_count, sizeof(*lanes));
        handles = (pthread_t *)calloc(lane_count, sizeof(*handles));
        parallel_durations =
            (double *)calloc(config.repetitions, sizeof(*parallel_durations));
        parallel_sorted =
            (double *)calloc(config.repetitions, sizeof(*parallel_sorted));
        lane_combos =
            (pe_omaha_combo_t *)calloc(buffer_count, sizeof(*lane_combos));
        lane_values = (double(*)[PE_OMAHA_TURN_PLAYERS_MAX])calloc(
            MODERN_DECK_SIZE, sizeof(*lane_values));
        lane_mass = (double *)calloc(MODERN_DECK_SIZE, sizeof(*lane_mass));
        lane_priced =
            (signed char *)calloc(MODERN_DECK_SIZE, sizeof(*lane_priced));
        if (!lanes || !handles || !parallel_durations || !parallel_sorted ||
            !lane_combos || !lane_values || !lane_mass || !lane_priced)
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
            lane->hole_cards = config.holes;
            lane->players = config.players;
            lane->lane = (uint8_t)lane_index;
            lane->lanes = config.threads;
            lane->hilo8 = config.hilo8;
            lane->values = lane_values;
            lane->mass = lane_mass;
            lane->priced = lane_priced;
            for (lead = 0u; lead < config.players; ++lead)
                lane->filtered[lead] =
                    lane_combos +
                    (lane_index * (size_t)config.players + (size_t)lead) *
                        config.combos;
            lane->context = eval_context_create(&eval_config);
            if (!lane->context)
            {
                bench_fail("could not create an eval context per lane");
                goto cleanup;
            }
        }

        for (repeat = 0u; repeat < config.repetitions; ++repeat)
        {
            if (run_lanes(lanes, handles, lane_count,
                          &parallel_durations[repeat]) != 0)
                goto cleanup;
            parallel_status = reduce_rivers(
                (const signed char *)lane_priced, (const double *)lane_mass,
                (const double(*)[PE_OMAHA_TURN_PLAYERS_MAX])lane_values,
                config.players, parallel_values, &parallel_rivers,
                &parallel_mass);
            parallel_fingerprint =
                fingerprint_values(parallel_values, config.players);
        }

        for (repeat = 0u; repeat < config.repetitions; ++repeat)
            parallel_sorted[repeat] = parallel_durations[repeat];
        parallel_median = median_of(parallel_sorted, config.repetitions);
        parallel_min = parallel_sorted[0];
        parallel_max = parallel_sorted[config.repetitions - 1u];
        parallel_matches = parallel_status == status &&
                           parallel_rivers == rivers &&
                           same_double(parallel_mass, mass) &&
                           parallel_fingerprint == fingerprint ? 1 : 0;
        if (!parallel_matches)
            bench_fail("the parallel lanes disagree with the serial terminal");
    }

    /* The frozen-strength probe: the same turn with every combo evaluated once
     * per river, then the walks reading those strengths.  It has to reproduce
     * the live values bit for bit, or the split it reports belongs to a walk
     * nobody runs. */
    if (config.frozen_strength)
    {
        for (repeat = 0u; repeat < config.repetitions; ++repeat)
        {
            double probe_start = monotonic_seconds();

            status = config.hilo8
                         ? pe_omaha_hilo8_turn_range_values(
                               context, board, MASK_EMPTY, ranges, &state,
                               config.holes, frozen_values, config.players,
                               &frozen_rivers, &frozen_mass, NULL,
                               &probe_runs[repeat])
                         : pe_omaha_turn_range_values_frozen(
                               context, board, MASK_EMPTY, ranges, &state,
                               config.holes, frozen_values, config.players,
                               &frozen_rivers, &frozen_mass,
                               &probe_runs[repeat]);
            frozen_durations[repeat] = monotonic_seconds() - probe_start;
            if (status != 0)
            {
                bench_fail("the frozen-strength turn terminal failed");
                goto cleanup;
            }
            frozen_sorted[repeat] = frozen_durations[repeat];
            walk_sorted[repeat] = probe_runs[repeat].walk_seconds;
            precompute_sorted[repeat] = probe_runs[repeat].precompute_seconds;
            low_precompute_sorted[repeat] =
                probe_runs[repeat].low_precompute_seconds;
        }
        frozen_median = median_of(frozen_sorted, config.repetitions);
        probe_walk = median_of(walk_sorted, config.repetitions);
        probe_precompute = median_of(precompute_sorted, config.repetitions);
        probe_low_precompute =
            median_of(low_precompute_sorted, config.repetitions);
        probe_fingerprint = fingerprint_values(frozen_values, config.players);
        probe_matches = probe_fingerprint == fingerprint &&
                        frozen_rivers == rivers &&
                        same_double(frozen_mass, mass) ? 1 : 0;
        if (!probe_matches)
            bench_fail("the frozen-strength walk disagrees with the live one");
    }

    for (repeat = 0u; repeat < config.repetitions; ++repeat)
        sorted[repeat] = durations[repeat];
    turn_median = median_of(sorted, config.repetitions);
    turn_min = sorted[0];
    turn_max = sorted[config.repetitions - 1u];

    /* The two phases are measured inside each call, so their medians come from
     * the same repetitions as the wall clock above.  What the sum leaves is
     * validation, allocation and the final reduction. */
    if (config.profile)
    {
        size_t profile_index;

        for (profile_index = 0u; profile_index < config.repetitions;
             ++profile_index)
        {
            const pe_omaha_turn_stats_t *run = &profile_runs[profile_index];

            filter_durations[profile_index] = run->filter_seconds;
            terminal_durations[profile_index] = run->terminal_seconds;
            if (run->combos_examined != profile_runs[0].combos_examined ||
                run->combos_kept != profile_runs[0].combos_kept ||
                run->rivers_to_terminal != profile_runs[0].rivers_to_terminal ||
                run->deals != profile_runs[0].deals)
                counts_stable = 0;
        }
        for (profile_index = 0u; profile_index < config.repetitions;
             ++profile_index)
        {
            filter_sorted[profile_index] = filter_durations[profile_index];
            terminal_sorted[profile_index] = terminal_durations[profile_index];
        }
        filter_median = median_of(filter_sorted, config.repetitions);
        terminal_median = median_of(terminal_sorted, config.repetitions);
        unaccounted_seconds = turn_median - filter_median - terminal_median;
        if (profile_runs[0].combos_examined > 0u)
            nanoseconds_per_visit = filter_median * 1e9 /
                                    (double)profile_runs[0].combos_examined;
        if (profile_runs[0].deals > 0u)
            nanoseconds_per_deal =
                terminal_median * 1e9 / (double)profile_runs[0].deals;
    }

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
    printf("  \"hilo8\": %s,\n", config.hilo8 ? "true" : "false");
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
    printf("  \"legal_pairs_measured\": %s,\n",
           counts_pairs ? "true" : "false");
    if (counts_pairs)
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
    {
        printf("  \"legal_pairs\": null,\n  \"pair_density\": null,\n");
        /* The rivers per tuple follow from the deck and the hands, so they are
         * known whenever the walk runs, counted or not. */
        if (enumerates)
            printf("  \"rivers_per_pair\": %zu,\n", deals_per_pair);
        else
            printf("  \"rivers_per_pair\": null,\n");
        printf("  \"deals_total\": null,\n  \"deals_per_second\": null,\n"
               "  \"seconds_per_deal\": null,\n");
    }
    printf("  \"seconds_per_combo\": %.12f,\n",
           config.combos > 0u
               ? turn_median /
                     ((double)config.combos * (double)config.players)
               : 0.0);
    if (config.profile)
    {
        double filter_share =
            turn_median > 0.0 ? filter_median / turn_median : 0.0;
        double terminal_share =
            turn_median > 0.0 ? terminal_median / turn_median : 0.0;
        double unaccounted_share =
            turn_median > 0.0 ? unaccounted_seconds / turn_median : 0.0;
        double density = config.combos > 0u
                             ? (double)legal_pairs /
                                   ((double)config.combos *
                                    (double)config.combos)
                             : 0.0;
        double crossover = 0.0;
        int have_crossover = 0;

        /* Heads-up only: there the filter is linear in the combos while the
         * terminal walks a quadratic tuple space, so the two costs cross once.
         * Filter work is one visit per combo of each range on each of the
         * 48 - 4 candidate rivers, terminal work is `density * n^2` tuples per
         * candidate river, so the crossing is
         *   n* = rivers * players * a / (density * rivers_per_pair * b)
         * with a the cost of a combo visit and b the cost of a deal.  With a
         * third player the terminal is cubic and there is no single crossing
         * worth naming. */
        if (config.players == 2u && enumerates && density > 0.0 &&
            deals_per_pair > 0u && nanoseconds_per_visit > 0.0 &&
            nanoseconds_per_deal > 0.0)
        {
            crossover = (double)(MODERN_DECK_SIZE - PE_OMAHA_TURN_BOARD_CARDS) *
                        (double)config.players * nanoseconds_per_visit /
                        (density * (double)deals_per_pair *
                         nanoseconds_per_deal);
            have_crossover = 1;
        }

        printf("  \"profile\": {\"combos_examined\": %zu, "
               "\"combos_kept\": %zu, \"rivers_to_terminal\": %zu, "
               "\"deals\": %zu, \"counts_stable_across_repetitions\": %s,\n",
               profile_runs[0].combos_examined, profile_runs[0].combos_kept,
               profile_runs[0].rivers_to_terminal, profile_runs[0].deals,
               counts_stable ? "true" : "false");
        printf("    \"seconds\": {\"filter\": %.9f, \"terminal\": %.9f, "
               "\"unaccounted\": %.9f},\n",
               filter_median, terminal_median, unaccounted_seconds);
        printf("    \"shares\": {\"filter\": %.6f, \"terminal\": %.6f, "
               "\"unaccounted\": %.6f},\n",
               filter_share, terminal_share, unaccounted_share);
        printf("    \"filter_bytes_copied\": %zu, "
               "\"nanoseconds_per_combo_visit\": %.6f,\n",
               profile_runs[0].combos_kept * sizeof(pe_omaha_combo_t),
               nanoseconds_per_visit);
        printf("    \"nanoseconds_per_deal\": ");
        if (profile_runs[0].deals > 0u)
            printf("%.6f,\n", nanoseconds_per_deal);
        else
            printf("null,\n");
        printf("    \"crossover_combos\": ");
        if (have_crossover)
            printf("%.4f},\n", crossover);
        else
            printf("null},\n");
    }
    else
        printf("  \"profile\": null,\n");
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
    if (config.frozen_strength)
    {
        printf("  \"probe\": {\"precompute_seconds\": %.9f, "
               "\"low_precompute_seconds\": %.9f, "
               "\"walk_seconds\": %.9f, \"deals\": %zu, \"combos\": %zu, "
               "\"nanoseconds_per_visit\": ",
               probe_precompute, probe_low_precompute, probe_walk,
               probe_runs[0].deals, probe_runs[0].combos);
        if (probe_runs[0].deals > 0u && probe_walk > 0.0)
            printf("%.6f,\n", probe_walk * 1e9 / (double)probe_runs[0].deals);
        else
            printf("null,\n");
        printf("    \"frozen_call_seconds\": %.9f, \"matches_live\": %s},\n",
               frozen_median, probe_matches ? "true" : "false");
    }
    else
        printf("  \"probe\": null,\n");
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
    if (config.threads > 1u)
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
            if (lanes[lane_index].context)
                eval_context_destroy(lanes[lane_index].context);
    }
    free(lane_combos);
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
    free(terminal_sorted);
    free(filter_sorted);
    free(terminal_durations);
    free(filter_durations);
    free(profile_runs);
    free(precompute_sorted);
    free(walk_sorted);
    free(low_precompute_sorted);
    free(frozen_sorted);
    free(frozen_durations);
    free(probe_runs);
    if (context)
        eval_context_destroy(context);
    return g_failures == 0 ? 0 : 1;
}
