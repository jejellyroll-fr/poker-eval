/*
 * test_plo4_turn_oracle.c - exact turn-to-river chance fixtures (issue #285)
 *
 * Every expectation here is computed by an independent brute force written in
 * this file: it enumerates the private-combo pairs, the river cards still in
 * the deck for each pair, and the showdown of every (hands, river) deal with
 * the product Omaha evaluator.  The terminal under test never appears in the
 * oracle, so the two agree only if the chance weights, the board-dependent
 * legality of a combo and the per-pair dead-card exclusion are all right.
 *
 * The conventions checked are the river terminal's: a deal is net of the
 * player's own investment, values are per deal under the declared joint range
 * distribution, and a pair whose hands share a card has no deal at all.
 */

#include <poker_eval/core/eval_context.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/games/eval_omaha.h>
#include <poker_eval/solver/pe_omaha_river.h>
#include <poker_eval/solver/pe_omaha_turn.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#define DECK_MASK ((mask_t)((1ULL << MODERN_DECK_SIZE) - 1u))
#define TOL 1e-12

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); ++failures; } } while (0)

static StdDeck_CardMask std_mask(mask_t cards)
{
    StdDeck_CardMask result;
    int card;

    StdDeck_CardMask_RESET(result);
    for (card = 0; card < MODERN_DECK_SIZE; ++card)
        if (mask_is_set(cards, card))
            StdDeck_CardMask_SET(result, card);
    return result;
}

/* Awards of one showdown: the pot split evenly between the best hands. */
static int deal_awards(const mask_t *holes, uint8_t player_count, mask_t board,
                       double pot, double *awards)
{
    HandVal strength[PE_OMAHA_TURN_PLAYERS_MAX];
    StdDeck_CardMask board_std = std_mask(board);
    HandVal best = HandVal_NOTHING;
    uint8_t winners = 0u;
    uint8_t best_count = 0u;
    uint8_t player;

    for (player = 0u; player < player_count; ++player)
    {
        awards[player] = 0.0;
        if (StdDeck_OmahaHi_EVAL(std_mask(holes[player]), board_std,
                                 &strength[player]) != 0)
            return 1;
        if (best_count == 0u || strength[player] > best)
        {
            best = strength[player];
            winners = (uint8_t)(1u << player);
            best_count = 1u;
        }
        else if (strength[player] == best)
        {
            winners |= (uint8_t)(1u << player);
            ++best_count;
        }
    }
    for (player = 0u; player < player_count; ++player)
        if (winners & (uint8_t)(1u << player))
            awards[player] = pot / (double)best_count;
    return 0;
}

/* Number of rivers a legal pair can still see: the deck minus the board, the
 * declared dead cards and the two hands. */
static int pair_river_count(mask_t board, mask_t dead, const mask_t *holes,
                            uint8_t player_count)
{
    mask_t used = board | dead;
    uint8_t player;

    for (player = 0u; player < player_count; ++player)
        used |= holes[player];
    return mask_popcount(DECK_MASK & ~used);
}

/* Heads-up oracle: for every legal pair, the mean showdown over that pair's
 * rivers, then the pair-mass-weighted mean over pairs. */
static double oracle_heads_up(mask_t board, mask_t dead,
                              const pe_omaha_range_t *ranges,
                              const pe_betting_state_t *state, uint8_t hero)
{
    double numerator = 0.0;
    double mass = 0.0;
    size_t i;
    size_t j;

    for (i = 0u; i < ranges[0].count; ++i)
    {
        for (j = 0u; j < ranges[1].count; ++j)
        {
            mask_t holes[2];
            mask_t free_cards;
            double weight = ranges[0].combos[i].weight *
                            ranges[1].combos[j].weight;
            double mean = 0.0;
            int rivers;

            if (!(weight > 0.0))
                continue;
            holes[0] = ranges[0].combos[i].cards;
            holes[1] = ranges[1].combos[j].cards;
            if ((holes[0] & holes[1]) != 0u)
                continue;
            if (((holes[0] | holes[1]) & (board | dead)) != 0u)
                continue;
            free_cards = DECK_MASK & ~(board | dead | holes[0] | holes[1]);
            rivers = mask_popcount(free_cards);
            if (rivers <= 0)
                continue;
            MASK_FOR_EACH_SET_BIT(free_cards, card)
            {
                double awards[2];
                if (deal_awards(holes, 2u, mask_set(board, card), state->pot,
                                awards) != 0)
                    return NAN;
                mean += awards[hero] - state->invested[hero];
            }
            numerator += weight * mean / (double)rivers;
            mass += weight;
        }
    }
    return mass > 0.0 ? numerator / mass : NAN;
}

/* Three-way oracle: same construction, three nested pairs, pot split between
 * every tied best hand. */
static double oracle_three_way(mask_t board, mask_t dead,
                               const pe_omaha_range_t *ranges,
                               const pe_betting_state_t *state, uint8_t hero)
{
    double numerator = 0.0;
    double mass = 0.0;
    size_t i;
    size_t j;
    size_t k;

    for (i = 0u; i < ranges[0].count; ++i)
    {
        for (j = 0u; j < ranges[1].count; ++j)
        {
            for (k = 0u; k < ranges[2].count; ++k)
            {
                mask_t holes[3];
                mask_t free_cards;
                double weight = ranges[0].combos[i].weight *
                                ranges[1].combos[j].weight *
                                ranges[2].combos[k].weight;
                double mean = 0.0;
                int rivers;

                if (!(weight > 0.0))
                    continue;
                holes[0] = ranges[0].combos[i].cards;
                holes[1] = ranges[1].combos[j].cards;
                holes[2] = ranges[2].combos[k].cards;
                if ((holes[0] & holes[1]) != 0u ||
                    (holes[0] & holes[2]) != 0u ||
                    (holes[1] & holes[2]) != 0u)
                    continue;
                if (((holes[0] | holes[1] | holes[2]) & (board | dead)) != 0u)
                    continue;
                free_cards = DECK_MASK & ~(board | dead | holes[0] | holes[1] |
                                           holes[2]);
                rivers = mask_popcount(free_cards);
                if (rivers <= 0)
                    continue;
                MASK_FOR_EACH_SET_BIT(free_cards, card)
                {
                    double awards[3];
                    if (deal_awards(holes, 3u, mask_set(board, card),
                                    state->pot, awards) != 0)
                        return NAN;
                    mean += awards[hero] - state->invested[hero];
                }
                numerator += weight * mean / (double)rivers;
                mass += weight;
            }
        }
    }
    return mass > 0.0 ? numerator / mass : NAN;
}

/* Total chance-expanded deal mass: every legal pair contributes its weight once
 * per river it can see. */
static double oracle_mass(mask_t board, mask_t dead,
                          const pe_omaha_range_t *ranges)
{
    double mass = 0.0;
    size_t i;
    size_t j;

    for (i = 0u; i < ranges[0].count; ++i)
    {
        for (j = 0u; j < ranges[1].count; ++j)
        {
            mask_t holes[2];
            double weight = ranges[0].combos[i].weight *
                            ranges[1].combos[j].weight;
            int rivers;

            if (!(weight > 0.0))
                continue;
            holes[0] = ranges[0].combos[i].cards;
            holes[1] = ranges[1].combos[j].cards;
            if ((holes[0] & holes[1]) != 0u)
                continue;
            if (((holes[0] | holes[1]) & (board | dead)) != 0u)
                continue;
            rivers = pair_river_count(board, dead, holes, 2u);
            mass += weight * (double)rivers;
        }
    }
    return mass;
}

/* ------------------------------------------------------------------ */
/* Fixtures                                                           */
/* ------------------------------------------------------------------ */

#define CARD(rank, suit) MODERN_MAKE_CARD(MODERN_RANK_##rank, MODERN_SUIT_##suit)

/* Turn board: A-K-7-2, one card to come. */
static const int TURN_BOARD[4] = {
    CARD(A, SPADES), CARD(K, DIAMONDS), CARD(7, CLUBS), CARD(2, HEARTS)
};

/* Two four-card ranges.  The second player's second combo shares T and 9 with
 * the first player's first combo, so at least one pair is blocked by hand
 * collision on every river; the third combo holds one of the two river cards
 * the dead-card fixture leaves, so that pair also loses a chance outcome. */
static const int P0_HANDS[3][4] = {
    {CARD(Q, SPADES), CARD(J, SPADES), CARD(T, SPADES), CARD(9, SPADES)},
    {CARD(A, HEARTS), CARD(K, HEARTS), CARD(Q, HEARTS), CARD(J, HEARTS)},
    {CARD(7, DIAMONDS), CARD(7, HEARTS), CARD(2, CLUBS), CARD(2, DIAMONDS)},
};
static const int P1_HANDS[3][4] = {
    {CARD(A, DIAMONDS), CARD(A, CLUBS), CARD(3, SPADES), CARD(4, SPADES)},
    {CARD(K, CLUBS), CARD(K, SPADES), CARD(T, SPADES), CARD(9, SPADES)},
    {CARD(6, SPADES), CARD(6, HEARTS), CARD(8, SPADES), CARD(5, DIAMONDS)},
};

static mask_t mask_of(const int *cards, size_t count)
{
    mask_t result = MASK_EMPTY;
    size_t index;

    for (index = 0u; index < count; ++index)
        result = mask_set(result, cards[index]);
    return result;
}

static mask_t board_mask(void)
{
    return mask_of(TURN_BOARD, 4u);
}

static void build_ranges(pe_omaha_combo_t *p0, pe_omaha_range_t *range0,
                         size_t n0, pe_omaha_combo_t *p1,
                         pe_omaha_range_t *range1, size_t n1)
{
    size_t index;

    for (index = 0u; index < n0; ++index)
    {
        p0[index].cards = mask_of(P0_HANDS[index], 4u);
        p0[index].weight = 1.0;
    }
    for (index = 0u; index < n1; ++index)
    {
        p1[index].cards = mask_of(P1_HANDS[index], 4u);
        p1[index].weight = 1.0;
    }
    range0->combos = p0;
    range0->count = n0;
    range1->combos = p1;
    range1->count = n1;
}

static void init_state(pe_betting_state_t *state, uint8_t players, double pot,
                       double invested_each)
{
    uint8_t player;

    memset(state, 0, sizeof(*state));
    state->player_count = players;
    state->winner = -1;
    state->pot = pot;
    for (player = 0u; player < players; ++player)
    {
        state->active[player] = 1;
        state->invested[player] = invested_each;
    }
}

/* Pooled range mass over the legal pairs: the normaliser of every value. */
static double oracle_pair_mass(mask_t board, mask_t dead,
                               const pe_omaha_range_t *ranges)
{
    double mass = 0.0;
    size_t i;
    size_t j;

    for (i = 0u; i < ranges[0].count; ++i)
    {
        for (j = 0u; j < ranges[1].count; ++j)
        {
            double weight = ranges[0].combos[i].weight *
                            ranges[1].combos[j].weight;

            if (!(weight > 0.0))
                continue;
            if ((ranges[0].combos[i].cards & ranges[1].combos[j].cards) != 0u)
                continue;
            if (((ranges[0].combos[i].cards | ranges[1].combos[j].cards) &
                 (board | dead)) != 0u)
                continue;
            mass += weight;
        }
    }
    return mass;
}

/* ------------------------------------------------------------------ */
/* Tests                                                              */
/* ------------------------------------------------------------------ */

static void test_full_turn_matches_oracle(EvalContext *context)
{
    const mask_t board = board_mask();
    pe_omaha_combo_t p0[3];
    pe_omaha_combo_t p1[3];
    pe_omaha_range_t ranges[2];
    pe_betting_state_t state;
    double values[2] = {0.0, 0.0};
    size_t rivers = 0u;
    double mass = 0.0;
    double expected;
    int status;

    build_ranges(p0, &ranges[0], 3u, p1, &ranges[1], 3u);
    init_state(&state, 2u, 63.0, 31.5);
    status = pe_omaha_turn_range_values(context, board, MASK_EMPTY, ranges,
                                        &state, 4u, values, 2u, &rivers, &mass);
    CHECK(status == 0, "full turn terminal returned %d", status);
    CHECK(rivers == 48u, "a four-card board leaves 48 rivers, got %zu", rivers);
    expected = oracle_mass(board, MASK_EMPTY, ranges);
    CHECK(fabs(mass - expected) <= TOL * expected,
          "deal mass %.17g differs from the oracle %.17g", mass, expected);
    CHECK(fabs(values[0] + values[1]) <= 1e-12,
          "heads-up values do not sum to zero: %.17g %.17g", values[0],
          values[1]);
    expected = oracle_heads_up(board, MASK_EMPTY, ranges, &state, 0u);
    CHECK(fabs(values[0] - expected) <= 1e-12,
          "player 0 turn value %.17g differs from the oracle %.17g", values[0],
          expected);
    expected = oracle_heads_up(board, MASK_EMPTY, ranges, &state, 1u);
    CHECK(fabs(values[1] - expected) <= 1e-12,
          "player 1 turn value %.17g differs from the oracle %.17g", values[1],
          expected);
}

/* A four-card board leaves 40 rivers per legal pair, one fewer per declared
 * dead card, so the pooled deal mass is that count times the pooled range mass.
 * Dead cards the ranges do not use change the count and nothing else. */
static void test_chance_mass_normalization(EvalContext *context)
{
    const mask_t board = board_mask();
    const mask_t dead = (mask_t)((1ULL << CARD(3, CLUBS)) |
                                 (1ULL << CARD(4, CLUBS)));
    pe_omaha_combo_t p0[3];
    pe_omaha_combo_t p1[3];
    pe_omaha_range_t ranges[2];
    pe_betting_state_t state;
    double plain[2] = {0.0, 0.0};
    double masked[2] = {0.0, 0.0};
    double pair_mass;
    size_t rivers = 0u;
    double mass = 0.0;
    int status;

    build_ranges(p0, &ranges[0], 3u, p1, &ranges[1], 3u);
    init_state(&state, 2u, 63.0, 31.5);
    pair_mass = oracle_pair_mass(board, MASK_EMPTY, ranges);
    CHECK(pair_mass > 0.0, "the fixture needs at least one legal pair");

    status = pe_omaha_turn_range_values(context, board, MASK_EMPTY, ranges,
                                        &state, 4u, plain, 2u, &rivers, &mass);
    CHECK(status == 0, "turn terminal returned %d", status);
    CHECK(rivers == 48u, "a four-card board leaves 48 candidate rivers, got %zu",
          rivers);
    CHECK(fabs(mass - 40.0 * pair_mass) <= TOL * 40.0 * pair_mass,
          "deal mass %.17g is not 40 rivers per legal pair (%.17g)", mass,
          40.0 * pair_mass);

    status = pe_omaha_turn_range_values(context, board, dead, ranges, &state,
                                        4u, masked, 2u, &rivers, &mass);
    CHECK(status == 0, "turn terminal with dead cards returned %d", status);
    CHECK(rivers == 46u, "two dead cards leave 46 candidate rivers, got %zu",
          rivers);
    CHECK(fabs(mass - 38.0 * pair_mass) <= TOL * 38.0 * pair_mass,
          "deal mass %.17g is not 38 rivers per legal pair (%.17g)", mass,
          38.0 * pair_mass);
    /* Dead cards remove two rivers per pair, so a pair's conditional value can
     * move even though the ranges are untouched.  The oracle prices the same
     * smaller chance space, and that is the claim. */
    CHECK(fabs(masked[0] - oracle_heads_up(board, dead, ranges, &state, 0u)) <=
              1e-12,
          "dead-card turn value %.17g differs from the oracle %.17g", masked[0],
          oracle_heads_up(board, dead, ranges, &state, 0u));
    CHECK(fabs(masked[1] - oracle_heads_up(board, dead, ranges, &state, 1u)) <=
              1e-12,
          "dead-card turn value %.17g differs from the oracle %.17g", masked[1],
          oracle_heads_up(board, dead, ranges, &state, 1u));
}

/* The turn value is exactly the deal-mass-weighted mean of the per-river
 * values, and the river card really moves them: the chance node adds the river
 * terminal's numbers, it does not redefine them. */
static void test_turn_is_the_mass_weighted_river_mean(EvalContext *context)
{
    const mask_t board = board_mask();
    pe_omaha_combo_t p0[3];
    pe_omaha_combo_t p1[3];
    pe_omaha_range_t ranges[2];
    pe_betting_state_t state;
    double turn_values[2] = {0.0, 0.0};
    double accumulated[2] = {0.0, 0.0};
    double interesting[2][2];
    double interesting_mass[2];
    double expected[2];
    size_t rivers = 0u;
    double total = 0.0;
    double mass = 0.0;
    int card;
    int status;

    build_ranges(p0, &ranges[0], 3u, p1, &ranges[1], 3u);
    init_state(&state, 2u, 63.0, 31.5);
    status = pe_omaha_turn_range_values(context, board, MASK_EMPTY, ranges,
                                        &state, 4u, turn_values, 2u, &rivers,
                                        &mass);
    CHECK(status == 0, "turn terminal returned %d", status);

    for (card = 0; card < MODERN_DECK_SIZE; ++card)
    {
        const int watched[2] = {CARD(8, SPADES), CARD(9, SPADES)};
        mask_t blocked = (mask_t)1u << card;
        pe_omaha_combo_t river_p0[3];
        pe_omaha_combo_t river_p1[3];
        pe_omaha_range_t river_ranges[2];
        pe_betting_state_t river_state = state;
        double values[2] = {0.0, 0.0};
        double river_mass = 0.0;
        size_t deals = 0u;
        size_t kept0 = 0u;
        size_t kept1 = 0u;
        size_t combo;
        int watch;

        if (mask_is_set(board, card))
            continue;
        for (combo = 0u; combo < 3u; ++combo)
        {
            if ((p0[combo].cards & blocked) == 0u)
                river_p0[kept0++] = p0[combo];
            if ((p1[combo].cards & blocked) == 0u)
                river_p1[kept1++] = p1[combo];
        }
        if (kept0 == 0u || kept1 == 0u)
            continue;
        river_ranges[0].combos = river_p0;
        river_ranges[0].count = kept0;
        river_ranges[1].combos = river_p1;
        river_ranges[1].count = kept1;
        status = pe_omaha_river_range_values(
            context, mask_set(board, card), river_ranges, &river_state, 4u,
            values, 2u, &deals, &river_mass);
        CHECK(status == 0, "river terminal for card %d returned %d", card,
              status);
        if (!(river_mass > 0.0))
            continue;
        accumulated[0] += values[0] * river_mass;
        accumulated[1] += values[1] * river_mass;
        total += river_mass;
        for (watch = 0; watch < 2; ++watch)
        {
            if (card == watched[watch])
            {
                interesting[watch][0] = values[0];
                interesting[watch][1] = values[1];
                interesting_mass[watch] = river_mass;
            }
        }
    }
    CHECK(total > 0.0, "no river produced a deal");
    for (card = 0; card < 2; ++card)
    {
        expected[card] = accumulated[card] / total;
        CHECK(fabs(turn_values[card] - expected[card]) <= 1e-12,
              "turn value %.17g is not the chance mean %.17g",
              turn_values[card], expected[card]);
    }
    CHECK(interesting_mass[0] > 0.0 && interesting_mass[1] > 0.0,
          "the watched rivers produced no deal");
    CHECK(fabs(interesting[0][0] - interesting[1][0]) > 1e-6,
          "the river card made no difference: %.17g %.17g", interesting[0][0],
          interesting[1][0]);
}

/* Three-way turn, which takes the river terminal's deal-enumeration path. */
static void test_three_way_turn_matches_oracle(EvalContext *context)
{
    static const int P2_HANDS[2][4] = {
        {CARD(5, CLUBS), CARD(5, HEARTS), CARD(4, CLUBS), CARD(3, CLUBS)},
        {CARD(J, CLUBS), CARD(T, CLUBS), CARD(9, CLUBS), CARD(8, CLUBS)},
    };
    const mask_t board = board_mask();
    pe_omaha_combo_t p0[2];
    pe_omaha_combo_t p1[2];
    pe_omaha_combo_t p2[2];
    pe_omaha_range_t ranges[3];
    pe_betting_state_t state;
    double values[3] = {0.0, 0.0, 0.0};
    size_t rivers = 0u;
    double mass = 0.0;
    double expected;
    uint8_t player;
    int status;

    build_ranges(p0, &ranges[0], 2u, p1, &ranges[1], 2u);
    for (player = 0u; player < 2u; ++player)
    {
        p2[player].cards = mask_of(P2_HANDS[player], 4u);
        p2[player].weight = 1.0;
    }
    ranges[2].combos = p2;
    ranges[2].count = 2u;
    init_state(&state, 3u, 90.0, 30.0);
    status = pe_omaha_turn_range_values(context, board, MASK_EMPTY, ranges,
                                        &state, 4u, values, 3u, &rivers, &mass);
    CHECK(status == 0, "three-way turn terminal returned %d", status);
    CHECK(rivers == 48u, "three-way turn should visit 48 rivers, got %zu",
          rivers);
    CHECK(fabs(values[0] + values[1] + values[2]) <= 1e-12,
          "three-way values do not sum to zero: %.17g %.17g %.17g", values[0],
          values[1], values[2]);
    for (player = 0u; player < 3u; ++player)
    {
        expected = oracle_three_way(board, MASK_EMPTY, ranges, &state, player);
        CHECK(fabs(values[player] - expected) <= 1e-12,
              "player %u turn value %.17g differs from the oracle %.17g",
              (unsigned)player, values[player], expected);
    }
}

/* PLO5 and PLO6 turn deals: same chance rule, wider private hands. */
static void test_wider_holes(EvalContext *context, uint8_t hole_cards)
{
    const mask_t board = board_mask();
    static const int WIDE_P0[2][6] = {
        {CARD(Q, SPADES), CARD(J, SPADES), CARD(T, SPADES),
         CARD(9, SPADES), CARD(3, HEARTS), CARD(4, HEARTS)},
        {CARD(A, HEARTS), CARD(6, HEARTS), CARD(5, HEARTS),
         CARD(4, SPADES), CARD(3, DIAMONDS), CARD(6, CLUBS)},
    };
    static const int WIDE_P1[2][6] = {
        {CARD(A, DIAMONDS), CARD(K, SPADES), CARD(Q, DIAMONDS),
         CARD(J, HEARTS), CARD(9, DIAMONDS), CARD(8, CLUBS)},
        {CARD(7, SPADES), CARD(6, DIAMONDS), CARD(5, CLUBS),
         CARD(4, CLUBS), CARD(3, CLUBS), CARD(2, SPADES)},
    };
    pe_omaha_combo_t p0[2];
    pe_omaha_combo_t p1[2];
    pe_omaha_range_t ranges[2];
    pe_betting_state_t state;
    double values[2] = {0.0, 0.0};
    size_t rivers = 0u;
    double mass = 0.0;
    double expected;
    size_t index;
    int status;

    for (index = 0u; index < 2u; ++index)
    {
        p0[index].cards = mask_of(WIDE_P0[index], hole_cards);
        p0[index].weight = 1.0;
        p1[index].cards = mask_of(WIDE_P1[index], hole_cards);
        p1[index].weight = 1.0;
    }
    ranges[0].combos = p0;
    ranges[0].count = 2u;
    ranges[1].combos = p1;
    ranges[1].count = 2u;
    init_state(&state, 2u, 63.0, 31.5);
    status = pe_omaha_turn_range_values(context, board, MASK_EMPTY, ranges,
                                        &state, hole_cards, values, 2u, &rivers,
                                        &mass);
    CHECK(status == 0, "PLO%u turn terminal returned %d", (unsigned)hole_cards,
          status);
    /* Candidate rivers are the deck minus the board; the private hands remove
     * rivers per pair inside the terminal, not from the candidate list. */
    CHECK(rivers == 48u, "PLO%u turn should visit 48 rivers, got %zu",
          (unsigned)hole_cards, rivers);
    CHECK(fabs(values[0] + values[1]) <= 1e-12,
          "PLO%u heads-up values do not sum to zero: %.17g %.17g",
          (unsigned)hole_cards, values[0], values[1]);
    expected = oracle_heads_up(board, MASK_EMPTY, ranges, &state, 0u);
    CHECK(fabs(values[0] - expected) <= 1e-12,
          "PLO%u player 0 value %.17g differs from the oracle %.17g",
          (unsigned)hole_cards, values[0], expected);
    expected = oracle_heads_up(board, MASK_EMPTY, ranges, &state, 1u);
    CHECK(fabs(values[1] - expected) <= 1e-12,
          "PLO%u player 1 value %.17g differs from the oracle %.17g",
          (unsigned)hole_cards, values[1], expected);
}

static void test_river_count_helper(void)
{
    CHECK(pe_omaha_turn_river_count(board_mask(), MASK_EMPTY) == 48,
          "an empty dead mask leaves 48 rivers");
    CHECK(pe_omaha_turn_river_count(board_mask(),
                                    (mask_t)((1ULL << CARD(3, CLUBS)) |
                                             (1ULL << CARD(4, CLUBS)))) == 46,
          "two dead cards leave 46 rivers");
    CHECK(pe_omaha_turn_river_count(board_mask() | (mask_t)(1ULL << 0),
                                    MASK_EMPTY) == -1,
          "a five-card board is not a turn board");
    CHECK(pe_omaha_turn_river_count(board_mask(),
                                    (mask_t)(1ULL << TURN_BOARD[0])) == -1,
          "a dead card on the board is rejected");
}

static void test_invalid_inputs(EvalContext *context)
{
    const mask_t board = board_mask();
    pe_omaha_combo_t p0[1];
    pe_omaha_combo_t p1[1];
    pe_omaha_range_t ranges[2];
    pe_betting_state_t state;
    double values[2] = {0.0, 0.0};
    size_t rivers = 0u;
    double mass = 0.0;

    p0[0].cards = mask_of(P0_HANDS[0], 4u);
    p0[0].weight = 1.0;
    p1[0].cards = mask_of(P1_HANDS[0], 4u);
    p1[0].weight = 1.0;
    ranges[0].combos = p0;
    ranges[0].count = 1u;
    ranges[1].combos = p1;
    ranges[1].count = 1u;

    init_state(&state, 2u, 63.0, 31.5);
    CHECK(pe_omaha_turn_range_values(context, board, MASK_EMPTY, ranges, &state,
                                     4u, values, 2u, &rivers, &mass) == 0,
          "the reference fixture must be valid");

    /* Five-card board. */
    CHECK(pe_omaha_turn_range_values(context, board | (mask_t)(1ULL << 0),
                                     MASK_EMPTY, ranges, &state, 4u, values,
                                     2u, &rivers, &mass) == -1,
          "a full board is not a turn board");
    /* Dead card that a combo also holds. */
    CHECK(pe_omaha_turn_range_values(context, board,
                                     (mask_t)(1ULL << P0_HANDS[0][0]), ranges,
                                     &state, 4u, values, 2u, &rivers,
                                     &mass) == -1,
          "dead cards must be disjoint from the ranges");
    /* A fold is priced by the betting tree, not by this terminal. */
    state.winner = 0;
    CHECK(pe_omaha_turn_range_values(context, board, MASK_EMPTY, ranges, &state,
                                     4u, values, 2u, &rivers, &mass) == -1,
          "a folded-out state has no showdown");
    state.winner = -1;
    /* A player who is not active has no showdown value. */
    state.active[1] = 0;
    CHECK(pe_omaha_turn_range_values(context, board, MASK_EMPTY, ranges, &state,
                                     4u, values, 2u, &rivers, &mass) == -1,
          "an inactive player is rejected");
    state.active[1] = 1;
    /* Hole count outside the supported 4..6. */
    CHECK(pe_omaha_turn_range_values(context, board, MASK_EMPTY, ranges, &state,
                                     7u, values, 2u, &rivers, &mass) == -1,
          "seven-card private hands are rejected");
    /* Outputs are zeroed even on a rejected call. */
    CHECK(values[0] == 0.0 && values[1] == 0.0 && rivers == 0u && mass == 0.0,
          "a rejected call left the outputs untouched");
}

int main(void)
{
    EvalConfig config = eval_config_holdem();
    EvalContext *context = eval_context_create(&config);

    if (!context)
    {
        fprintf(stderr, "FAILED: could not create an eval context\n");
        return 1;
    }
    test_river_count_helper();
    test_full_turn_matches_oracle(context);
    test_chance_mass_normalization(context);
    test_turn_is_the_mass_weighted_river_mean(context);
    test_three_way_turn_matches_oracle(context);
    test_wider_holes(context, 5u);
    test_wider_holes(context, 6u);
    test_invalid_inputs(context);
    eval_context_destroy(context);
    if (failures)
        return 1;
    puts("test_plo4_turn_oracle: turn-to-river chance fixtures passed");
    return 0;
}
