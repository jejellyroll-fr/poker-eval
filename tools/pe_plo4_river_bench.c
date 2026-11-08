/*
 * pe-plo4-river-bench - PLO4 river showdown microbenchmark (issue #280).
 *
 * The baseline is intentionally exact: every scenario evaluates the full
 * Cartesian product of the two ranges through pe_showdown_vector(), which for
 * 4-card private hands is the pairwise fallback.  Nothing is sampled and no
 * result is inferred from a solver run.
 *
 * The tool emits one JSON document on stdout (schema
 * pe-plo4-river-bench/v1).  Setup (range construction plus hand strength
 * evaluation) and evaluation (the showdown call) are timed separately, so the
 * cost of the pairwise fallback can be compared with the cost of preparing the
 * ranges.
 *
 * Hand strengths come from the product Omaha evaluator; the showdown path is
 * reported by the API itself, not assumed.  Peak RSS is the OS high-water mark
 * where the platform exposes one.  The allocation figure is a net heap delta
 * measured around one evaluation (blocks on Apple, bytes on glibc); it is not
 * a count of allocation calls, and it is reported as unavailable rather than
 * guessed on platforms without a counter.
 */

#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/games/eval_omaha.h>
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
#include <malloc/malloc.h>
#elif defined(__GLIBC__) &&                                            \
    (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33))
#include <malloc.h>
#define PE_BENCH_HAVE_MALLINFO2 1
#endif

#define PE_BENCH_MAX_COMBOS 512u
#define PE_BENCH_BOARD_CARDS 5u
#define PE_BENCH_HOLE_CARDS 4u
#define PE_BENCH_DEFAULT_REPETITIONS 5u
#define PE_BENCH_SEED 20260906ULL

typedef struct
{
    const char *id;
    size_t combos;
} bench_scenario_t;

static const bench_scenario_t PE_BENCH_SCENARIOS[] = {
    {"small", 16u},
    {"medium", 128u},
    {"larger", 512u},
};

typedef struct
{
    mask_t *hero_masks;
    mask_t *opp_masks;
    int64_t *hero_strength;
    int64_t *opp_strength;
    double *opp_reach;
    size_t hero_n;
    size_t opp_n;
    size_t evaluated_pairs;
    size_t blocked_pairs;
    size_t zero_reach;
} bench_fixture_t;

typedef struct
{
    int available;
    const char *counter;
    const char *unit;
    double before;
    double after;
} bench_heap_t;

static int g_failures;

#define BENCH_FAIL(...)                          \
    do                                           \
    {                                            \
        fprintf(stderr, "pe-plo4-river-bench: "); \
        fprintf(stderr, __VA_ARGS__);            \
        fputc('\n', stderr);                     \
        ++g_failures;                            \
    } while (0)

static uint64_t lcg_next(uint64_t *state)
{
    *state = *state * 6364136223846793005ULL + 1442695040888963407ULL;
    return *state >> 33;
}

static mask_t board_mask(void)
{
    /* Ks9d4c2hTs, the river board of the checked-in PLO4 case. */
    static const int cards[] = {
        MODERN_MAKE_CARD(MODERN_RANK_K, MODERN_SUIT_SPADES),
        MODERN_MAKE_CARD(MODERN_RANK_9, MODERN_SUIT_DIAMONDS),
        MODERN_MAKE_CARD(MODERN_RANK_4, MODERN_SUIT_CLUBS),
        MODERN_MAKE_CARD(MODERN_RANK_2, MODERN_SUIT_HEARTS),
        MODERN_MAKE_CARD(MODERN_RANK_T, MODERN_SUIT_SPADES),
    };
    mask_t board = MASK_EMPTY;
    size_t i;

    for (i = 0u; i < PE_BENCH_BOARD_CARDS; ++i)
        board = mask_set(board, cards[i]);
    return board;
}

static StdDeck_CardMask to_std_mask(mask_t cards)
{
    StdDeck_CardMask result;
    int card;

    StdDeck_CardMask_RESET(result);
    for (card = 0; card < MODERN_DECK_SIZE; ++card)
        if (mask_is_set(cards, card))
            StdDeck_CardMask_SET(result, card);
    return result;
}

static int64_t omaha_strength(mask_t hole, mask_t board)
{
    HandVal value = HandVal_NOTHING;

    if (StdDeck_OmahaHi_EVAL(to_std_mask(hole), to_std_mask(board), &value) != 0)
        return 0;
    return (int64_t)value;
}

static void fill_range(mask_t board, size_t count, uint64_t seed,
                       mask_t *masks, int64_t *strength, double *reach)
{
    int live[MODERN_DECK_SIZE];
    int live_count = 0;
    int card;
    uint64_t state = seed;
    size_t index;

    for (card = 0; card < MODERN_DECK_SIZE; ++card)
        if (!mask_is_set(board, card))
            live[live_count++] = card;
    for (index = 0u; index < count; ++index)
    {
        mask_t hand = MASK_EMPTY;

        while (mask_popcount(hand) < (int)PE_BENCH_HOLE_CARDS)
        {
            int pick = (int)(lcg_next(&state) % (uint64_t)live_count);
            hand = mask_set(hand, live[pick]);
        }
        masks[index] = hand;
        strength[index] = omaha_strength(hand, board);
        /* Every eleventh combo has zero reach, so the fallback is measured on
         * a range that is not uniformly live.  The hero range passes NULL:
         * only the opponent range carries reach. */
        if (reach)
            reach[index] = (index % 11u == 7u)
                               ? 0.0
                               : 0.25 + 0.75 *
                                            (double)((index * 37u + 13u) % 17u) /
                                                16.0;
    }
}

static void count_pairs(bench_fixture_t *fixture, mask_t dead)
{
    size_t hero;
    size_t opponent;

    fixture->evaluated_pairs = 0u;
    fixture->blocked_pairs = 0u;
    for (hero = 0u; hero < fixture->hero_n; ++hero)
    {
        if ((fixture->hero_masks[hero] & dead) != 0)
        {
            fixture->blocked_pairs += fixture->opp_n;
            continue;
        }
        for (opponent = 0u; opponent < fixture->opp_n; ++opponent)
        {
            if ((fixture->opp_masks[opponent] & dead) != 0 ||
                (fixture->opp_masks[opponent] & fixture->hero_masks[hero]) != 0)
                ++fixture->blocked_pairs;
            else
                ++fixture->evaluated_pairs;
        }
    }
}

static bench_heap_t heap_measure(void)
{
    bench_heap_t heap;

    memset(&heap, 0, sizeof(heap));
#if defined(__APPLE__)
    heap.counter = "malloc_default_zone.blocks_in_use";
    heap.unit = "blocks";
    heap.available = 1;
#elif defined(PE_BENCH_HAVE_MALLINFO2)
    heap.counter = "mallinfo2.uordblks";
    heap.unit = "bytes";
    heap.available = 1;
#else
    heap.counter = NULL;
    heap.unit = NULL;
    heap.available = 0;
#endif
    return heap;
}

static double heap_now(const bench_heap_t *heap)
{
#if defined(__APPLE__)
    malloc_statistics_t stats;
    memset(&stats, 0, sizeof(stats));
    malloc_zone_statistics(malloc_default_zone(), &stats);
    return (double)stats.blocks_in_use;
#elif defined(PE_BENCH_HAVE_MALLINFO2)
    struct mallinfo2 info = mallinfo2();
    return (double)info.uordblks;
#else
    (void)heap;
    return 0.0;
#endif
}

static double monotonic_seconds(void)
{
#if defined(__APPLE__)
    /* clock_gettime(CLOCK_MONOTONIC) is too coarse for the smallest
     * scenario on macOS; mach_absolute_time reports nanoseconds. */
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
        uint64_t bits;

        memcpy(&bits, &values[index], sizeof(bits));
        hash ^= bits;
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

static int parse_repetitions(const char *text, size_t *out)
{
    char *end = NULL;
    unsigned long value;

    if (!text || !out)
        return -1;
    value = strtoul(text, &end, 10);
    if (!end || *end != '\0' || value == 0ul || value > 1000ul)
        return -1;
    *out = (size_t)value;
    return 0;
}

static void usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s [--repetitions N] [--scenario small|medium|larger]...\n"
            "          [--help]\n"
            "\n"
            "Emits a pe-plo4-river-bench/v1 JSON document on stdout.\n"
            "Every scenario is exact (no sampling).\n",
            program);
}

static int run_scenario(const char *id, size_t combos, size_t repetitions,
                        size_t scenario_index)
{
    mask_t board = board_mask();
    mask_t dead = board;
    bench_fixture_t fixture;
    bench_heap_t heap;
    double *durations;
    double *sorted_durations;
    double setup_seconds;
    double eval_median;
    double eval_min;
    double eval_max;
    double eval_total;
    double heap_before;
    double heap_after;
    double setup_start;
    pe_value_vec_t values = {0};
    pe_showdown_path_t path = PE_SHOWDOWN_PATH_NONE;
    uint64_t fingerprint = 0u;
    size_t repeat;
    size_t index;
    int deterministic = 1;

    memset(&fixture, 0, sizeof(fixture));
    fixture.hero_n = combos;
    fixture.opp_n = combos;
    fixture.hero_masks = (mask_t *)calloc(combos, sizeof(mask_t));
    fixture.opp_masks = (mask_t *)calloc(combos, sizeof(mask_t));
    fixture.hero_strength = (int64_t *)calloc(combos, sizeof(int64_t));
    fixture.opp_strength = (int64_t *)calloc(combos, sizeof(int64_t));
    fixture.opp_reach = (double *)calloc(combos, sizeof(double));
    durations = (double *)calloc(repetitions, sizeof(double));
    sorted_durations = (double *)calloc(repetitions, sizeof(double));
    if (!fixture.hero_masks || !fixture.opp_masks ||
        !fixture.hero_strength || !fixture.opp_strength || !fixture.opp_reach ||
        !durations || !sorted_durations)
    {
        BENCH_FAIL("%s: allocation failed", id);
        goto cleanup;
    }
    if (pe_vec_alloc(&values, combos) != PE_SOLVER_OK)
    {
        BENCH_FAIL("%s: output allocation failed", id);
        goto cleanup;
    }

    setup_start = monotonic_seconds();
    fill_range(board, combos, PE_BENCH_SEED ^ (uint64_t)(scenario_index + 1u),
               fixture.hero_masks, fixture.hero_strength, NULL);
    fill_range(board, combos,
               PE_BENCH_SEED ^ (uint64_t)(0x9e37u + scenario_index),
               fixture.opp_masks, fixture.opp_strength, fixture.opp_reach);
    count_pairs(&fixture, dead);
    setup_seconds = monotonic_seconds() - setup_start;

    for (index = 0u; index < combos; ++index)
        if (fixture.opp_reach[index] == 0.0)
            ++fixture.zero_reach;

    heap = heap_measure();
    heap_before = heap_now(&heap);
    if (pe_showdown_vector(fixture.hero_masks, fixture.hero_strength,
                           fixture.hero_n, fixture.opp_masks,
                           fixture.opp_strength, fixture.opp_reach,
                           fixture.opp_n, dead, 63.0, &values, &path) !=
        PE_SOLVER_OK)
    {
        BENCH_FAIL("%s: showdown failed", id);
        goto cleanup;
    }
    heap_after = heap_now(&heap);

    for (repeat = 0u; repeat < repetitions; ++repeat)
    {
        uint64_t run_fingerprint;
        double start = monotonic_seconds();

        if (pe_showdown_vector(fixture.hero_masks, fixture.hero_strength,
                               fixture.hero_n, fixture.opp_masks,
                               fixture.opp_strength, fixture.opp_reach,
                               fixture.opp_n, dead, 63.0, &values, &path) !=
            PE_SOLVER_OK)
        {
            BENCH_FAIL("%s: showdown failed on repetition %zu", id, repeat);
            goto cleanup;
        }
        durations[repeat] = monotonic_seconds() - start;
        run_fingerprint = fingerprint_values(values.v, values.n);
        if (repeat == 0u)
            fingerprint = run_fingerprint;
        else if (run_fingerprint != fingerprint)
            deterministic = 0;
    }

    memcpy(sorted_durations, durations, repetitions * sizeof(double));
    eval_median = median_of(sorted_durations, repetitions);
    eval_min = sorted_durations[0];
    eval_max = sorted_durations[repetitions - 1u];
    eval_total = 0.0;
    for (repeat = 0u; repeat < repetitions; ++repeat)
        eval_total += durations[repeat];

    printf("    {\n");
    printf("      \"id\": \"%s\",\n", id);
    printf("      \"exact\": true,\n");
    printf("      \"hero_combos\": %zu,\n", fixture.hero_n);
    printf("      \"opp_combos\": %zu,\n", fixture.opp_n);
    printf("      \"zero_reach_opp_combos\": %zu,\n", fixture.zero_reach);
    printf("      \"evaluated_pairs\": %zu,\n", fixture.evaluated_pairs);
    printf("      \"blocked_pairs\": %zu,\n", fixture.blocked_pairs);
    printf("      \"showdown_path\": \"%s\",\n",
           path == PE_SHOWDOWN_PATH_PAIRWISE
               ? "pairwise"
               : (path == PE_SHOWDOWN_PATH_SORTED ? "sorted" : "other"));
    printf("      \"setup_seconds\": %.9f,\n", setup_seconds);
    printf("      \"eval_seconds\": {\"min\": %.9f, \"median\": %.9f, "
           "\"max\": %.9f, \"total\": %.9f},\n",
           eval_min, eval_median, eval_max, eval_total);
    /* Same precision as eval_seconds.median so the two cannot disagree just
     * because one was printed with more digits than the other. */
    printf("      \"eval_seconds_per_call\": %.9f,\n", eval_median);
    printf("      \"pair_evaluations_per_second\": %.3f,\n",
           eval_median > 0.0
               ? (double)fixture.evaluated_pairs / eval_median
               : 0.0);
    printf("      \"values_fingerprint\": \"%016llx\",\n",
           (unsigned long long)fingerprint);
    printf("      \"deterministic_across_repetitions\": %s,\n",
           deterministic ? "true" : "false");
    if (peak_rss_bytes() != 0u)
        printf("      \"peak_rss_bytes\": %llu,\n",
               (unsigned long long)peak_rss_bytes());
    else
        printf("      \"peak_rss_bytes\": null,\n");
    printf("      \"allocations\": {\"available\": %s",
           heap.available ? "true" : "false");
    if (heap.available)
        printf(", \"counter\": \"%s\", \"unit\": \"%s\", \"before\": %.0f, "
               "\"after\": %.0f, \"net\": %.0f, \"per_evaluation\": %.6f",
               heap.counter, heap.unit, heap_before, heap_after,
               heap_after - heap_before,
               repetitions > 0u
                   ? ((heap_after - heap_before) / (double)repetitions)
                   : 0.0);
    else
        printf(", \"reason\": \"no platform heap counter\"");
    printf("}\n");
    printf("    }");

cleanup:
    free(sorted_durations);
    free(durations);
    pe_vec_free(&values);
    free(fixture.opp_reach);
    free(fixture.opp_strength);
    free(fixture.hero_strength);
    free(fixture.opp_masks);
    free(fixture.hero_masks);
    return g_failures == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    size_t repetitions = PE_BENCH_DEFAULT_REPETITIONS;
    const char *wanted[sizeof(PE_BENCH_SCENARIOS) /
                     sizeof(PE_BENCH_SCENARIOS[0])];
    size_t wanted_count = 0u;
    size_t index;
    int arg;
    int first = 1;

    for (arg = 1; arg < argc; ++arg)
    {
        if (strcmp(argv[arg], "--help") == 0 || strcmp(argv[arg], "-h") == 0)
        {
            usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[arg], "--repetitions") == 0)
        {
            if (arg + 1 >= argc || parse_repetitions(argv[++arg], &repetitions))
            {
                usage(argv[0]);
                return 2;
            }
            continue;
        }
        if (strcmp(argv[arg], "--scenario") == 0)
        {
            if (arg + 1 >= argc || wanted_count >=
                                       sizeof(wanted) / sizeof(wanted[0]))
            {
                usage(argv[0]);
                return 2;
            }
            wanted[wanted_count++] = argv[++arg];
            continue;
        }
        usage(argv[0]);
        return 2;
    }

    for (index = 0u; index < wanted_count; ++index)
    {
        size_t known;
        int found = 0;

        for (known = 0u; known < sizeof(PE_BENCH_SCENARIOS) /
                                  sizeof(PE_BENCH_SCENARIOS[0]);
             ++known)
            if (strcmp(wanted[index], PE_BENCH_SCENARIOS[known].id) == 0)
                found = 1;
        if (!found)
        {
            fprintf(stderr, "pe-plo4-river-bench: unknown scenario '%s'\n",
                    wanted[index]);
            return 2;
        }
    }

    printf("{\n");
    printf("  \"schema\": \"pe-plo4-river-bench/v1\",\n");
    printf("  \"generated_by\": \"pe-plo4-river-bench\",\n");
    printf("  \"issue\": 280,\n");
    printf("  \"board\": \"Ks9d4c2hTs\",\n");
    printf("  \"pot\": 63,\n");
    printf("  \"seed\": %llu,\n", (unsigned long long)PE_BENCH_SEED);
    printf("  \"repetitions\": %zu,\n", repetitions);
    printf("  \"exact\": true,\n");
    printf("  \"sampled\": false,\n");
    printf("  \"results\": [\n");

    for (index = 0u; index < sizeof(PE_BENCH_SCENARIOS) /
                                 sizeof(PE_BENCH_SCENARIOS[0]);
         ++index)
    {
        size_t wanted_index;
        int selected = wanted_count == 0u;

        for (wanted_index = 0u; wanted_index < wanted_count; ++wanted_index)
            if (strcmp(wanted[wanted_index], PE_BENCH_SCENARIOS[index].id) == 0)
                selected = 1;
        if (!selected)
            continue;
        if (!first)
            printf(",\n");
        first = 0;
        if (run_scenario(PE_BENCH_SCENARIOS[index].id,
                         PE_BENCH_SCENARIOS[index].combos, repetitions,
                         index) != 0)
            return 1;
    }

    printf("\n  ]\n}\n");
    return g_failures == 0 ? 0 : 1;
}
