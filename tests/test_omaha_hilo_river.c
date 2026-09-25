/* test_omaha_hilo_river.c - Omaha Hi/Lo 8-or-better pot distribution.
 *
 * Hi/Lo is not another hand count, it is another way to divide one pot: each
 * slice is cut in half between the best high hand and the best qualifying low
 * hand, and the two halves are decided independently.  These cases pin every
 * outcome the rule can produce -- no low at all, one high winner plus one low
 * winner, a scoop, a tied high, a tied low and heads-up quartering -- on a
 * single river deal, so the numbers are the distribution and nothing else.
 *
 * The high-only terminal is checked on the same deals: it must keep paying the
 * whole pot to the same high winner, which is what makes the split visible
 * rather than a re-labelling.
 */

#include <poker_eval/solver/pe_omaha_deals.h>
#include <poker_eval/solver/pe_omaha_river.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(cond, ...)                                          \
    do                                                            \
    {                                                             \
        if (!(cond))                                              \
        {                                                         \
            fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__);\
            fprintf(stderr, __VA_ARGS__);                         \
            fputc('\n', stderr);                                  \
            ++g_failures;                                         \
        }                                                         \
    } while (0)

/* "Ks" / "Th" -> card index, rank 0 = deuce, suit 0 = spades. */
static int card_of(const char *text)
{
    int rank;
    int suit;
    switch (text[0])
    {
        case 'A': rank = 12; break;
        case 'K': rank = 11; break;
        case 'Q': rank = 10; break;
        case 'J': rank = 9; break;
        case 'T': rank = 8; break;
        default: rank = text[0] - '2'; break;
    }
    switch (text[1])
    {
        case 's': suit = 0; break;
        case 'h': suit = 1; break;
        case 'd': suit = 2; break;
        default: suit = 3; break;
    }
    return suit * 13 + rank;
}

static mask_t mask_of(const char *text)
{
    mask_t result = MASK_EMPTY;
    size_t i;
    for (i = 0u; text[i] != '\0' && text[i + 1u] != '\0'; i += 2u)
        result = mask_set(result, card_of(text + i));
    return result;
}

typedef struct
{
    const char *name;
    const char *board;
    const char *holes[3];
    uint8_t players;
    uint8_t hole_cards;
    int hilo8;
    double expected[3];
} case_t;

static int run_case(const case_t *c)
{
    EvalConfig config = eval_config_holdem();
    EvalContext *context = eval_context_create(&config);
    pe_omaha_combo_t combos[3];
    pe_omaha_range_t ranges[3];
    pe_betting_state_t state = {0};
    double values[3] = {0.0, 0.0, 0.0};
    double sum = 0.0;
    double pot = 0.0;
    size_t deal_count = 0u;
    double weight_sum = 0.0;
    uint8_t player;
    int status;

    CHECK(context != NULL, "%s: Omaha context creation failed", c->name);
    if (!context)
        return 1;

    for (player = 0u; player < c->players; ++player)
    {
        combos[player].cards = mask_of(c->holes[player]);
        combos[player].weight = 1.0;
        ranges[player].combos = &combos[player];
        ranges[player].count = 1u;
        state.active[player] = 1;
        state.invested[player] = 10.0;
        pot += 10.0;
    }
    state.player_count = c->players;
    state.winner = -1;
    state.pot = pot;

    status = c->hilo8
                 ? pe_omaha_hilo8_river_range_values(
                       context, mask_of(c->board), ranges, &state,
                       c->hole_cards, values, c->players, &deal_count,
                       &weight_sum)
                 : pe_omaha_river_range_values(
                       context, mask_of(c->board), ranges, &state,
                       c->hole_cards, values, c->players, &deal_count,
                       &weight_sum);
    CHECK(status == 0, "%s: terminal failed with %d", c->name, status);
    CHECK(deal_count == 1u && fabs(weight_sum - 1.0) <= 1e-12,
          "%s: deal mass is wrong (%zu, %.17g)", c->name, deal_count,
          weight_sum);

    for (player = 0u; player < c->players; ++player)
    {
        CHECK(fabs(values[player] - c->expected[player]) <= 1e-9,
              "%s: player %u got %.17g, want %.17g", c->name,
              (unsigned)player, values[player], c->expected[player]);
        sum += values[player];
    }
    CHECK(fabs(sum) <= 1e-9, "%s: utilities are not zero-sum (%.17g)", c->name,
          sum);

    eval_context_destroy(context);
    return 0;
}

int main(void)
{
    /* The board has three nines: no 8-or-better low can exist, whatever the
     * hole cards are.  Aces full of nines beats kings full of nines, so the
     * whole pot goes high, exactly as it would high-only. */
    static const case_t no_low = {
        "no qualifying low",
        "9s9h9dKcAh",
        {"AsAd2s2d" /* aces */, "KsKd2h2c"},
        2u, 4u, 1,
        {10.0, -10.0, 0.0}};

    /* P0 has trips (the board king), P1 the 6-low.  Neither half is contested
     * and neither player is in the other half, so both end flat: 10 out, 10
     * back. */
    static const case_t high_and_low = {
        "one high winner, one low winner",
        "2s4h6d8cKs",
        {"KhKd9s9c", "As3s9d9h"},
        2u, 4u, 1,
        {0.0, 0.0, 0.0}};

    /* Same board shape, but P0 holds the nut flush *and* the nut low: scoop,
     * and the low half is not even contested. */
    static const case_t scoop = {
        "scoop",
        "2s4s6s8cKh",
        {"As3s8h8d", "KsKd7d7h"},
        2u, 4u, 1,
        {10.0, -10.0, 0.0}};

    /* Heads-up quartering: both players make A-2 + board 4-6-8, so they tie
     * the low, and P1's kings beat P0's threes for the high.  75% / 25%. */
    static const case_t quarter = {
        "heads-up quartering",
        "2h4c6d8cKs",
        {"As3s9d9h", "Ad3dTsTh"},
        2u, 4u, 1,
        {-5.0, 5.0, 0.0}};

    /* Three players: P0 and P1 tie the high with pocket aces and neither can
     * make a low; P2 has the only qualifying low.  The high half is halved
     * again between the two tied winners. */
    static const case_t high_tie = {
        "tied high, one low winner",
        "9s9h2s4h6d",
        {"KsJsQh8h", "KdJdQc8c", "3h7hTsJc"},
        3u, 4u, 1,
        {-2.5, -2.5, 5.0}};

    /* Three players: P0 has the only high hand, P1 and P2 make the same
     * 6-low.  The low half is split between them. */
    static const case_t low_tie = {
        "one high winner, tied low",
        "2s4h6d8cKs",
        {"KhKd9s9c", "As3s9d9h", "Ad3dTsTh"},
        3u, 4u, 1,
        {5.0, -2.5, -2.5}};

    /* PLO5 and PLO6 on the split case: the same two extra cards are inert for
     * both halves, so the split is unchanged by the wider hand. */
    static const case_t plo5 = {
        "PLO5 split",
        "2s4h6d8cKs",
        {"KhKd9s9h9c", "As3sThTdJc"},
        2u, 5u, 1,
        {0.0, 0.0, 0.0}};

    static const case_t plo6 = {
        "PLO6 split",
        "2s4h6d8cKs",
        {"KhKd9s9h9c5c", "As3sThTdJcQc"},
        2u, 6u, 1,
        {0.0, 0.0, 0.0}};

    /* High-only on the split and quarter deals: the same high winner takes the
     * whole pot, which is what the split changes. */
    static const case_t high_only_split = {
        "high-only, split deal",
        "2s4h6d8cKs",
        {"KhKd9s9c", "As3s9d9h"},
        2u, 4u, 0,
        {10.0, -10.0, 0.0}};

    static const case_t high_only_quarter = {
        "high-only, quarter deal",
        "2h4c6d8cKs",
        {"As3s9d9h", "Ad3dTsTh"},
        2u, 4u, 0,
        {-10.0, 10.0, 0.0}};

    static const case_t *cases[] = {
        &no_low,       &high_and_low,      &scoop,
        &quarter,      &high_tie,          &low_tie,
        &plo5,         &plo6,              &high_only_split,
        &high_only_quarter,
    };

    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        printf("  %-28s %s\n", cases[i]->name, cases[i]->hilo8 ? "hi/lo" : "high");
        run_case(cases[i]);
    }

    if (g_failures)
    {
        fprintf(stderr, "test_omaha_hilo_river: %d failure(s)\n", g_failures);
        return 1;
    }
    puts("test_omaha_hilo_river: Omaha Hi/Lo 8-or-better distribution passed");
    return 0;
}
