/*
 * test_omaha_river_hoisted.c - independent check of the hoisted river walk.
 *
 * The river terminal evaluates each combo once per board and then walks the
 * deals reading those strengths (design A of issue #332).  The frozen-strength
 * probe used to be the independent check of that walk; it now calls the same
 * code, so its `matches_live` flag can only show that the clocks change
 * nothing.  This test is the check that does not trust the terminal: it prices
 * the same river by enumerating the deals with pe_omaha_deals_enumerate() and
 * evaluating every hole of every deal with the product evaluator, distributing
 * the pot with arithmetic written here.
 *
 * It covers what the PLO4 oracle cannot: five and six holes, and Hi/Lo, whose
 * low strength lives in the second hoisted table.  The deal count and the deal
 * mass are compared bit for bit, because the reference walks in the terminal's
 * order and sums the same products; the values are compared at 1e-12, since the
 * reference's payoff is its own.
 */

#include <poker_eval/core/eval_context.h>
#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/games/eval_omaha.h>
#include <poker_eval/solver/pe_betting_state.h>
#include <poker_eval/solver/pe_omaha_deals.h>
#include <poker_eval/solver/pe_omaha_river.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#define HOLE_CARDS_MAX 6u
#define RANGE_MAX 8u
#define PLAYERS 2u
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

static uint8_t count_bits(uint8_t bits)
{
    uint8_t count = 0u;

    while (bits != 0u)
    {
        count = (uint8_t)(count + (bits & 1u));
        bits = (uint8_t)(bits >> 1);
    }
    return count;
}

typedef struct
{
    mask_t board;
    int hilo8;
    const pe_betting_state_t *state;
    double values[PLAYERS];
    int failed;
} reference_t;

/* Winners of the high half: the strongest hand, ties included. */
static uint8_t high_winners(const HandVal *strength)
{
    uint8_t winners = 0u;
    HandVal best = HandVal_NOTHING;
    uint8_t player;

    for (player = 0u; player < PLAYERS; ++player)
    {
        if (winners == 0u || strength[player] > best)
        {
            best = strength[player];
            winners = (uint8_t)(1u << player);
        }
        else if (strength[player] == best)
            winners |= (uint8_t)(1u << player);
    }
    return winners;
}

/* Winners of the low half: the lowest qualifying eight-or-better hand, ties
 * included, and 0 when nobody qualifies. */
static uint8_t low_winners(const LowHandVal *strength)
{
    uint8_t winners = 0u;
    LowHandVal best = LowHandVal_NOTHING;
    uint8_t player;

    for (player = 0u; player < PLAYERS; ++player)
    {
        if (strength[player] == LowHandVal_NOTHING)
            continue;
        if (winners == 0u || strength[player] < best)
        {
            best = strength[player];
            winners = (uint8_t)(1u << player);
        }
        else if (strength[player] == best)
            winners |= (uint8_t)(1u << player);
    }
    return winners;
}

/* One deal of the reference: evaluate every hole on this board, pay the pot the
 * way the terminal's convention does for a single slice, accumulate net of the
 * investment. */
static int reference_deal(const mask_t *holes, uint8_t player_count,
                          double weight, void *user)
{
    reference_t *ref = (reference_t *)user;
    StdDeck_CardMask board = to_std_mask(ref->board);
    HandVal strength[PLAYERS];
    LowHandVal low[PLAYERS];
    double awards[PLAYERS];
    uint8_t player;

    if (player_count != PLAYERS || ref->failed)
        return 1;
    for (player = 0u; player < PLAYERS; ++player)
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

    if (ref->hilo8)
    {
        double high_half = ref->state->pot * 0.5;
        double low_half = ref->state->pot - high_half;
        uint8_t high = high_winners(strength);
        uint8_t low_masks = low_winners(low);
        uint8_t high_count = count_bits(high);
        uint8_t low_count = count_bits(low_masks);

        if (high_count == 0u)
        {
            ref->failed = 1;
            return 1;
        }
        for (player = 0u; player < PLAYERS; ++player)
            if (high & (uint8_t)(1u << player))
                awards[player] += high_half / (double)high_count;
        /* No qualifying low sends its half back to the high winners, which is
         * the terminal's "no low, 100 % high" rule. */
        if (low_count == 0u)
        {
            low_masks = high;
            low_count = high_count;
        }
        for (player = 0u; player < PLAYERS; ++player)
            if (low_masks & (uint8_t)(1u << player))
                awards[player] += low_half / (double)low_count;
    }
    else
    {
        uint8_t high = high_winners(strength);
        uint8_t high_count = count_bits(high);

        if (high_count == 0u)
        {
            ref->failed = 1;
            return 1;
        }
        for (player = 0u; player < PLAYERS; ++player)
            if (high & (uint8_t)(1u << player))
                awards[player] = ref->state->pot / (double)high_count;
    }
    for (player = 0u; player < PLAYERS; ++player)
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
    const entry_t *hero;
    size_t hero_count;
    const entry_t *opp;
    size_t opp_count;
} river_case_t;

static void run_case(const river_case_t *test)
{
    pe_omaha_combo_t hero[RANGE_MAX];
    pe_omaha_combo_t opp[RANGE_MAX];
    pe_omaha_range_t ranges[PLAYERS];
    pe_betting_state_t state;
    reference_t reference;
    EvalConfig config = eval_config_holdem();
    EvalContext *context;
    double values[PLAYERS] = {0.0, 0.0};
    double reference_mass = 0.0;
    double terminal_mass = 0.0;
    size_t reference_deals = 0u;
    size_t terminal_deals = 0u;
    int status;
    size_t i;

    memset(&reference, 0, sizeof(reference));
    CHECK(test->hero_count <= RANGE_MAX && test->opp_count <= RANGE_MAX,
          "%s: range is larger than this test supports", test->label);
    if (test->hero_count > RANGE_MAX || test->opp_count > RANGE_MAX)
        return;
    reference.board = mask_of(test->board);
    CHECK(mask_popcount(reference.board) == 5,
          "%s: board %s is not five cards", test->label, test->board);
    for (i = 0u; i < test->hero_count; ++i)
    {
        hero[i].cards = mask_of(test->hero[i].cards);
        hero[i].weight = test->hero[i].weight;
        CHECK(mask_popcount(hero[i].cards) == test->hole_cards,
              "%s: hero %zu is not %u cards", test->label, i,
              (unsigned)test->hole_cards);
    }
    for (i = 0u; i < test->opp_count; ++i)
    {
        opp[i].cards = mask_of(test->opp[i].cards);
        opp[i].weight = test->opp[i].weight;
        CHECK(mask_popcount(opp[i].cards) == test->hole_cards,
              "%s: opponent %zu is not %u cards", test->label, i,
              (unsigned)test->hole_cards);
    }
    ranges[0].combos = hero;
    ranges[0].count = test->hero_count;
    ranges[1].combos = opp;
    ranges[1].count = test->opp_count;

    memset(&state, 0, sizeof(state));
    state.player_count = PLAYERS;
    state.winner = -1;
    state.active[0] = 1;
    state.active[1] = 1;
    state.invested[0] = 10.0;
    state.invested[1] = 10.0;
    state.pot = 20.0;
    reference.hilo8 = test->hilo8;
    reference.state = &state;

    status = pe_omaha_deals_enumerate(reference.board, ranges, PLAYERS,
                                      test->hole_cards, reference_deal,
                                      &reference, &reference_deals,
                                      &reference_mass);
    CHECK(status == 0 && !reference.failed,
          "%s: the per-deal reference failed", test->label);
    CHECK(reference_deals > 0u && reference_mass > 0.0,
          "%s: the reference found no legal deal", test->label);
    if (status != 0 || reference.failed || reference_deals == 0u ||
        !(reference_mass > 0.0))
        return;
    for (i = 0u; i < PLAYERS; ++i)
        reference.values[i] /= reference_mass;

    context = eval_context_create(&config);
    CHECK(context != NULL, "%s: context creation failed", test->label);
    if (!context)
        return;
    status = test->hilo8
                 ? pe_omaha_hilo8_river_range_values(
                       context, reference.board, ranges, &state,
                       test->hole_cards, values, PLAYERS, &terminal_deals,
                       &terminal_mass)
                 : pe_omaha_river_range_values(
                       context, reference.board, ranges, &state,
                       test->hole_cards, values, PLAYERS, &terminal_deals,
                       &terminal_mass);
    CHECK(status == 0, "%s: the terminal failed", test->label);
    CHECK(terminal_deals == reference_deals,
          "%s: terminal counted %zu deals, the reference %zu", test->label,
          terminal_deals, reference_deals);
    CHECK(same_bits(terminal_mass, reference_mass),
          "%s: terminal mass %.17g, the reference %.17g (not bit-identical)",
          test->label, terminal_mass, reference_mass);
    CHECK(same_bits(values[0], reference.values[0]),
          "%s: player 0 %.17g, the reference %.17g (not bit-identical)",
          test->label, values[0], reference.values[0]);
    CHECK(same_bits(values[1], reference.values[1]),
          "%s: player 1 %.17g, the reference %.17g (not bit-identical)",
          test->label, values[1], reference.values[1]);
    CHECK(fabs(values[0] + values[1]) <= TOL,
          "%s: the terminal is not zero-sum (%.17g)", test->label,
          values[0] + values[1]);
    eval_context_destroy(context);
}

/* Five holes, high only: the width the PLO4 oracle cannot reach. */
static void test_plo5_matches_per_deal(void)
{
    static const entry_t hero[] = {
        {"AsKs3d4d5h", 0.500},
        {"9c8c6d6hAc", 0.250},
        {"KdQd9s8s7h", 0.125},
        {"AhKh5c4c3c", 0.125},
    };
    static const entry_t opp[] = {
        {"AdKc3s4s6c", 0.500},
        {"9d8d5d5s6s", 0.250},
        {"QhJh9h8h7s", 0.125},
        {"AcKd2d3h4h", 0.125},
    };
    static const river_case_t test = {
        "PLO5 high-only", 5u, 0, "2c7dThJsQc", hero, 4u, opp, 4u,
    };

    run_case(&test);
}

/* Six holes, high only. */
static void test_plo6_matches_per_deal(void)
{
    static const entry_t hero[] = {
        {"AsKs3d4d5h6h", 0.500},
        {"9c8c6d6hAc7c", 0.250},
        {"KdQd9s8s7h5s", 0.125},
        {"AhKh5c4c3c2d", 0.125},
    };
    static const entry_t opp[] = {
        {"AdKc3s4s6c8d", 0.500},
        {"9d8d5d5s6s7s", 0.250},
        {"QhJh9h8h7s6c", 0.125},
        {"AcKd2d3h4h5d", 0.125},
    };
    static const river_case_t test = {
        "PLO6 high-only", 6u, 0, "2c7dThJsQc", hero, 4u, opp, 4u,
    };

    run_case(&test);
}

/* Hi/Lo where one player takes the high half and the other the low: both
 * hoisted tables have to come into play on the same deal. */
static void test_hilo_split_matches_per_deal(void)
{
    static const entry_t hero[] = {
        {"As3cKdKh", 0.750},
        {"Ah5cQdQs", 0.250},
    };
    static const entry_t opp[] = {
        {"Ad7cJdJh", 0.600},
        {"KcKs5d5h", 0.400},
    };
    static const river_case_t test = {
        "PLO4 Hi/Lo split", 4u, 1, "2c4d8hJsQc", hero, 2u, opp, 2u,
    };

    run_case(&test);
}

/* Hi/Lo on a board with no qualifying low: the low half must fall back to the
 * high winners instead of paying nobody. */
static void test_hilo_no_low_matches_per_deal(void)
{
    static const entry_t hero[] = {
        {"AsKs3d4d5h6h", 0.500},
        {"9c8c6d6hAc7c", 0.250},
        {"KdQd9s8s7h5s", 0.250},
    };
    static const entry_t opp[] = {
        {"AdKc3s4s6c8d", 0.500},
        {"9d8d5d5s6s7s", 0.250},
        {"QhJh9h8h7s6c", 0.250},
    };
    static const river_case_t test = {
        "PLO6 Hi/Lo without a low", 6u, 1, "2c7dThJsQc", hero, 3u, opp, 3u,
    };

    run_case(&test);
}

int main(void)
{
    test_plo5_matches_per_deal();
    test_plo6_matches_per_deal();
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
