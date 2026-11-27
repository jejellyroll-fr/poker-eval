/*
 * test_omaha_river_hoisted.c - independent check of the hoisted river walk.
 *
 * The river terminal evaluates each combo once per board and then walks the
 * deals reading those strengths (design A of issue #332).  The frozen-strength
 * probe used to be the independent check of that walk; it now calls the same
 * code, so its `matches_live` flag can only show that the clocks change
 * nothing.  This test is the check that does not trust the terminal: it prices
 * the same river by enumerating the deals with pe_omaha_deals_enumerate() and
 * evaluating every hole of every deal with the product evaluator, summing the
 * deal mass and the payouts in its own callback, with the pot slices written
 * out per case instead of being built by the solver.
 *
 * It covers what the PLO4 oracle cannot: five and six holes, Hi/Lo, whose low
 * strength lives in the second hoisted table, and a three-way pot whose second
 * slice pays only two of the three players.  The deal count, the deal mass and
 * the values are compared bit for bit, because the reference walks in the
 * terminal's order, sums the same products, and splits a slice by the same
 * rule; a comparison that only agrees approximately would not tell the two
 * apart from a re-associated sum.
 */

#include <poker_eval/core/eval_context.h>
#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/games/eval_omaha.h>
#include <poker_eval/solver/pe_betting_state.h>
#include <poker_eval/solver/pe_omaha_deals.h>
#include <poker_eval/solver/pe_omaha_river.h>

#include "support/pe_omaha_hilo_winners.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define RANGE_MAX 8u
#define MAX_PLAYERS 3u
#define MAX_SLICES 3u
#define TOL 1e-12

static int g_failures;

#define CHECK(cond, ...)                                                   \
    do                                                                     \
    {                                                                      \
        if (!(cond))                                                       \
        {                                                                  \
            fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__);         \
            fprintf(stderr, __VA_ARGS__);                                  \
            fputc('\n', stderr);                                           \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

/* "Ks" -> modern card index, suit order clubs, diamonds, hearts, spades. */
static int card_of(const char *text)
{
    static const char ranks[] = "23456789TJQKA";
    const char *found;
    int rank;
    int suit;

    found = strchr(ranks, text[0]);
    if (!found)
        return -1;
    rank = (int)(found - ranks);
    switch (text[1])
    {
        case 'c': suit = MODERN_SUIT_CLUBS; break;
        case 'd': suit = MODERN_SUIT_DIAMONDS; break;
        case 'h': suit = MODERN_SUIT_HEARTS; break;
        case 's': suit = MODERN_SUIT_SPADES; break;
        default: return -1;
    }
    return MODERN_MAKE_CARD(rank, suit);
}

static mask_t mask_of(const char *text)
{
    mask_t result = MASK_EMPTY;
    size_t i;

    for (i = 0u; text[i] != '\0' && text[i + 1u] != '\0'; i += 2u)
    {
        int card = card_of(text + i);

        if (card < 0)
            return MASK_EMPTY;
        result = mask_set(result, card);
    }
    return result;
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

static int same_bits(double a, double b)
{
    return memcmp(&a, &b, sizeof(a)) == 0;
}

typedef struct
{
    double amount;
    uint8_t eligible;
} slice_spec_t;

/* The pot every heads-up case plays for: 20 in the middle, ten each. */
static const slice_spec_t heads_up_slices[] = {{20.0, 0x03u}};

typedef struct
{
    mask_t board;
    int hilo8;
    uint8_t player_count;
    uint8_t slice_count;
    const slice_spec_t *slices;
    const pe_betting_state_t *state;
    double mass;
    double values[MAX_PLAYERS];
    int failed;
} reference_t;

/* One deal of the reference: sum its weight, evaluate every hole on this board,
 * pay the declared slices, accumulate net of the investment.  The mass is
 * summed here, in the walk's order, rather than taken from the enumeration's
 * own total. */
static int reference_deal(const mask_t *holes, uint8_t player_count,
                          double weight, void *user)
{
    reference_t *ref = (reference_t *)user;
    StdDeck_CardMask board = to_std_mask(ref->board);
    HandVal strength[MAX_PLAYERS];
    LowHandVal low[MAX_PLAYERS];
    double awards[MAX_PLAYERS];
    uint8_t player;
    uint8_t index;

    ref->mass += weight;
    if (player_count != ref->player_count || ref->failed)
        return 1;
    for (player = 0u; player < player_count; ++player)
    {
        StdDeck_CardMask hole = to_std_mask(holes[player]);

        awards[player] = 0.0;
        if (ref->hilo8)
        {
            if (StdDeck_OmahaHiLow8_EVAL(hole, board, &strength[player],
                                         &low[player]) != 0)
            {
                ref->failed = 1;
                return 1;
            }
        }
        else if (StdDeck_OmahaHi_EVAL(hole, board, &strength[player]) != 0)
        {
            ref->failed = 1;
            return 1;
        }
    }

    for (index = 0u; index < ref->slice_count; ++index)
    {
        const slice_spec_t *slice = &ref->slices[index];
        uint8_t high =
            pe_test_best_high(strength, slice->eligible, player_count);
        uint8_t high_count = pe_test_count_bits(high);

        /* A slice nobody eligible can win pays nobody, exactly as the
         * distribution routine leaves it. */
        if (high_count == 0u)
            continue;
        if (!ref->hilo8)
        {
            for (player = 0u; player < player_count; ++player)
                if (high & (uint8_t)(1u << player))
                    awards[player] += slice->amount / (double)high_count;
            continue;
        }
        {
            double high_half = slice->amount * 0.5;
            double low_half = slice->amount * 0.5;
            uint8_t low_masks =
                pe_test_best_low(low, slice->eligible, player_count);
            uint8_t low_count = pe_test_count_bits(low_masks);

            for (player = 0u; player < player_count; ++player)
                if (high & (uint8_t)(1u << player))
                    awards[player] += high_half / (double)high_count;
            /* No qualifying low sends its half back to the high winners,
             * which is the terminal's "no low, 100 % high" rule. */
            if (low_count == 0u)
            {
                low_masks = high;
                low_count = high_count;
            }
            for (player = 0u; player < player_count; ++player)
                if (low_masks & (uint8_t)(1u << player))
                    awards[player] += low_half / (double)low_count;
        }
    }
    for (player = 0u; player < player_count; ++player)
        ref->values[player] +=
            weight * (awards[player] - ref->state->invested[player]);
    return 0;
}

typedef struct
{
    const char *cards;
    double weight;
} entry_t;

typedef struct
{
    const char *label;
    uint8_t hole_cards;
    int hilo8;
    const char *board;
    uint8_t player_count;
    const entry_t *hands[MAX_PLAYERS];
    size_t hand_counts[MAX_PLAYERS];
    double pot;
    double invested[MAX_PLAYERS];
    const slice_spec_t *slices;
    uint8_t slice_count;
    /* Hand-computed values, so a case can pin its own distribution instead of
     * only agreeing with the terminal.  NULL leaves it unchecked. */
    const double *expected;
    size_t expected_deals;
} river_case_t;

static void run_case(const river_case_t *test)
{
    pe_omaha_combo_t combos[MAX_PLAYERS][RANGE_MAX];
    pe_omaha_range_t ranges[MAX_PLAYERS];
    pe_betting_state_t state;
    reference_t reference;
    EvalConfig config = eval_config_holdem();
    EvalContext *context;
    double values[MAX_PLAYERS] = {0.0, 0.0, 0.0};
    /* The enumeration insists on somewhere to put its own total; this case
     * never reads it, because the reference sums the mass itself. */
    double enumeration_mass = 0.0;
    double terminal_mass = 0.0;
    double sum = 0.0;
    size_t reference_deals = 0u;
    size_t terminal_deals = 0u;
    int status;
    uint8_t player;
    size_t i;

    memset(&reference, 0, sizeof(reference));
    memset(&state, 0, sizeof(state));
    reference.board = mask_of(test->board);
    CHECK(mask_popcount(reference.board) == 5,
          "%s: board %s is not five cards", test->label, test->board);
    CHECK(test->player_count >= 2u && test->player_count <= MAX_PLAYERS,
          "%s: unsupported player count", test->label);
    CHECK(test->slice_count <= MAX_SLICES,
          "%s: too many slices", test->label);
    if (mask_popcount(reference.board) != 5 ||
        test->player_count < 2u || test->player_count > MAX_PLAYERS ||
        test->slice_count > MAX_SLICES)
        return;
    for (player = 0u; player < test->player_count; ++player)
    {
        CHECK(test->hand_counts[player] <= RANGE_MAX,
              "%s: range %u is larger than this test supports", test->label,
              (unsigned)player);
        if (test->hand_counts[player] > RANGE_MAX)
            return;
        for (i = 0u; i < test->hand_counts[player]; ++i)
        {
            combos[player][i].cards = mask_of(test->hands[player][i].cards);
            combos[player][i].weight = test->hands[player][i].weight;
            CHECK(mask_popcount(combos[player][i].cards) == test->hole_cards,
                  "%s: player %u combo %zu is not %u cards", test->label,
                  (unsigned)player, i, (unsigned)test->hole_cards);
        }
        ranges[player].combos = combos[player];
        ranges[player].count = test->hand_counts[player];
        state.invested[player] = test->invested[player];
        state.active[player] = 1;
    }
    state.player_count = test->player_count;
    state.winner = -1;
    state.pot = test->pot;
    reference.hilo8 = test->hilo8;
    reference.player_count = test->player_count;
    reference.slice_count = test->slice_count;
    reference.slices = test->slices;
    reference.state = &state;

    status = pe_omaha_deals_enumerate(reference.board, ranges,
                                      test->player_count, test->hole_cards,
                                      reference_deal, &reference,
                                      &reference_deals, &enumeration_mass);
    CHECK(status == 0 && !reference.failed,
          "%s: the per-deal reference failed", test->label);
    CHECK(reference_deals > 0u && reference.mass > 0.0,
          "%s: the reference found no legal deal", test->label);
    if (status != 0 || reference.failed || reference_deals == 0u ||
        !(reference.mass > 0.0))
        return;
    for (player = 0u; player < test->player_count; ++player)
        reference.values[player] /= reference.mass;
    if (test->expected_deals != 0u)
        CHECK(reference_deals == test->expected_deals,
              "%s: the reference priced %zu deals, the hand count says %zu",
              test->label, reference_deals, test->expected_deals);
    if (test->expected)
    {
        for (player = 0u; player < test->player_count; ++player)
            CHECK(fabs(reference.values[player] - test->expected[player]) <=
                      TOL,
                  "%s: the reference pays player %u %.17g, the hand count "
                  "%.17g",
                  test->label, (unsigned)player, reference.values[player],
                  test->expected[player]);
    }

    context = eval_context_create(&config);
    CHECK(context != NULL, "%s: context creation failed", test->label);
    if (!context)
        return;
    status = test->hilo8
                 ? pe_omaha_hilo8_river_range_values(
                       context, reference.board, ranges, &state,
                       test->hole_cards, values, test->player_count,
                       &terminal_deals, &terminal_mass)
                 : pe_omaha_river_range_values(
                       context, reference.board, ranges, &state,
                       test->hole_cards, values, test->player_count,
                       &terminal_deals, &terminal_mass);
    CHECK(status == 0, "%s: the terminal failed", test->label);
    CHECK(terminal_deals == reference_deals,
          "%s: terminal counted %zu deals, the reference %zu", test->label,
          terminal_deals, reference_deals);
    CHECK(same_bits(terminal_mass, reference.mass),
          "%s: terminal mass %.17g, the reference %.17g (not bit-identical)",
          test->label, terminal_mass, reference.mass);
    for (player = 0u; player < test->player_count; ++player)
    {
        CHECK(same_bits(values[player], reference.values[player]),
              "%s: player %u %.17g, the reference %.17g (not bit-identical)",
              test->label, (unsigned)player, values[player],
              reference.values[player]);
        sum += values[player];
    }
    CHECK(fabs(sum) <= TOL,
          "%s: the terminal is not zero-sum (%.17g)", test->label, sum);
    eval_context_destroy(context);
}

/* Five holes, high only: the width the PLO4 oracle cannot reach. */
static void test_plo5_matches_per_deal(void)
{
    static const entry_t p0[] = {
        {"AsKs3d4d5h", 0.500},
        {"9c8c6d6hAc", 0.250},
        {"KdQd9s8s7h", 0.125},
        {"AhKh5c4c3c", 0.125},
    };
    static const entry_t p1[] = {
        {"AdKc3s4s6c", 0.500},
        {"9d8d5d5s6s", 0.250},
        {"QhJh9h8h7s", 0.125},
        {"AcKd2d3h4h", 0.125},
    };
    static const river_case_t test = {
        "PLO5 high-only", 5u, 0, "2c7dThJsQc", 2u,
        {p0, p1}, {4u, 4u}, 20.0, {10.0, 10.0}, heads_up_slices, 1u,
        NULL, 0u,
    };

    run_case(&test);
}

/* Six holes, high only. */
static void test_plo6_matches_per_deal(void)
{
    static const entry_t p0[] = {
        {"AsKs3d4d5h6h", 0.500},
        {"9c8c6d6hAc7c", 0.250},
        {"KdQd9s8s7h5s", 0.125},
        {"AhKh5c4c3c2d", 0.125},
    };
    static const entry_t p1[] = {
        {"AdKc3s4s6c8d", 0.500},
        {"9d8d5d5s6s7s", 0.250},
        {"QhJh9h8h7s6c", 0.125},
        {"AcKd2d3h4h5d", 0.125},
    };
    static const river_case_t test = {
        "PLO6 high-only", 6u, 0, "2c7dThJsQc", 2u,
        {p0, p1}, {4u, 4u}, 20.0, {10.0, 10.0}, heads_up_slices, 1u,
        NULL, 0u,
    };

    run_case(&test);
}

/* Three players with unequal investments: the short stack can win the main pot
 * and nothing from the side pot, which only the two covering players contest.
 * A payoff that ignored the eligible mask would pay it out three ways.
 *
 * The values are counted by hand, so this case does not share the terminal's
 * distribution rule.  Board `2c7dThJsQc`, unit weights: P0 holds A-K and a
 * Q-J-T-9-8 straight, P1 trips jacks and a pair of nines, P2 trips tens and a
 * pair of sixes.  The short stack's two hands are the two best in the game, so
 * P0 takes the 30 main pot on all 8 deals; the 20 side pot goes to P1 on the 6
 * deals where P1's hand beats P2's and to P2 on the 2 deals where P2 plays
 * trips against the pair of nines.  Net of the 10 / 20 / 20 invested, that is
 * 8 x 20 = 160 for P0, 2 x -20 = -40 for P1 and 6 x -20 = -120 for P2, over a
 * mass of 8: 20, -5 and -15. */
static void test_three_way_side_pot_matches_per_deal(void)
{
    static const entry_t p0[] = {
        {"AsKs3d4d", 1.0},
        {"9d8d6c6h", 1.0},
    };
    static const entry_t p1[] = {
        {"JdJh4s4h", 1.0},
        {"9s9h3s3h", 1.0},
    };
    static const entry_t p2[] = {
        {"TsTc5s5c", 1.0},
        {"6d6s5c5d", 1.0},
    };
    /* Main pot 3 x 10, side pot the 2 x 10 the two covering players put in
     * above the short stack. */
    static const slice_spec_t slices[] = {{30.0, 0x07u}, {20.0, 0x06u}};
    static const double expected[] = {20.0, -5.0, -15.0};
    static const river_case_t test = {
        "three-way side pot", 4u, 0, "2c7dThJsQc", 3u,
        {p0, p1, p2}, {2u, 2u, 2u}, 50.0, {10.0, 20.0, 20.0}, slices, 2u,
        expected, 8u,
    };

    run_case(&test);
}

/* Hi/Lo where one player takes the high half and the other the low: both
 * hoisted tables have to come into play on the same deal. */
static void test_hilo_split_matches_per_deal(void)
{
    static const entry_t p0[] = {
        {"As3cKdKh", 0.750},
        {"Ah5cQdQs", 0.250},
    };
    static const entry_t p1[] = {
        {"Ad7cJdJh", 0.600},
        {"KcKs5d5h", 0.400},
    };
    static const river_case_t test = {
        "PLO4 Hi/Lo split", 4u, 1, "2c4d8hJsQc", 2u,
        {p0, p1}, {2u, 2u}, 20.0, {10.0, 10.0}, heads_up_slices, 1u,
        NULL, 0u,
    };

    run_case(&test);
}

/* Hi/Lo on a board with no qualifying low: the low half must fall back to the
 * high winners instead of paying nobody. */
static void test_hilo_no_low_matches_per_deal(void)
{
    static const entry_t p0[] = {
        {"AsKs3d4d5h6h", 0.500},
        {"9c8c6d6hAc7c", 0.250},
        {"KdQd9s8s7h5s", 0.250},
    };
    static const entry_t p1[] = {
        {"AdKc3s4s6c8d", 0.500},
        {"9d8d5d5s6s7s", 0.250},
        {"QhJh9h8h7s6c", 0.250},
    };
    static const river_case_t test = {
        "PLO6 Hi/Lo without a low", 6u, 1, "2c7dThJsQc", 2u,
        {p0, p1}, {3u, 3u}, 20.0, {10.0, 10.0}, heads_up_slices, 1u,
        NULL, 0u,
    };

    run_case(&test);
}

int main(void)
{
    test_plo5_matches_per_deal();
    test_plo6_matches_per_deal();
    test_three_way_side_pot_matches_per_deal();
    test_hilo_split_matches_per_deal();
    test_hilo_no_low_matches_per_deal();
    if (g_failures != 0)
    {
        fprintf(stderr, "test_omaha_river_hoisted: %d failure(s)\n",
                g_failures);
        return 1;
    }
    puts("test_omaha_river_hoisted: the hoisted walk matches per-deal pricing");
    return 0;
}
