/*
 * test_plo4_turn_oracle.c - exact turn-to-river chance fixtures (issue #285)
 *
 * Every expectation here is computed by an independent brute force written in
 * this file: it enumerates the private-combo tuples, the river cards still in
 * the deck for each tuple, and the showdown of every (hands, river) deal with
 * the product Omaha evaluator.  The terminal under test never appears in the
 * oracle, so the two agree only if the chance weights, the board-dependent
 * legality of a combo and the per-tuple dead-card exclusion are all right.
 *
 * The conventions checked are the river terminal's: a deal is net of the
 * player's own investment, values are per deal under the declared joint range
 * distribution, and a tuple whose hands share a card has no deal at all.
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

typedef struct
{
    double value;      /* hero's chance-expanded mean net award */
    double pair_mass;  /* pooled weight of the legal combo tuples */
    double deal_mass;  /* pooled weight of the legal (hands, river) deals */
    int ok;            /* 0 when the showdown evaluator rejected a deal */
} oracle_result_t;

/* Independent brute force.  A tuple is legal when no combo reuses a card the
 * board, the dead mask or an earlier hand already holds and every weight is
 * positive; it then contributes once per river card still in the deck.  The
 * pooled value divides by the deal mass, which is exactly the probability
 * normaliser the terminal has to report.
 */
static oracle_result_t oracle_brute(mask_t board, mask_t dead,
                                    const pe_omaha_range_t *ranges,
                                    const pe_betting_state_t *state,
                                    uint8_t player_count, uint8_t hero)
{
    oracle_result_t result = {0.0, 0.0, 0.0, 1};
    size_t index[PE_OMAHA_TURN_PLAYERS_MAX] = {0u};
    uint8_t player;

    for (;;)
    {
        mask_t holes[PE_OMAHA_TURN_PLAYERS_MAX];
        mask_t used = board | dead;
        double weight = 1.0;
        double total = 0.0;
        int legal = 1;

        for (player = 0u; player < player_count; ++player)
        {
            const pe_omaha_combo_t *combo =
                &ranges[player].combos[index[player]];

            holes[player] = combo->cards;
            if (!(combo->weight > 0.0) || (combo->cards & used) != 0u)
                legal = 0;
            weight *= combo->weight;
            used |= combo->cards;
        }
        if (legal)
        {
            mask_t free_cards = DECK_MASK & ~used;
            int rivers = mask_popcount(free_cards);

            MASK_FOR_EACH_SET_BIT(free_cards, card)
            {
                double awards[PE_OMAHA_TURN_PLAYERS_MAX];

                if (deal_awards(holes, player_count, mask_set(board, card),
                                state->pot, awards) != 0)
                {
                    result.ok = 0;
                    return result;
                }
                total += awards[hero] - state->invested[hero];
            }
            result.value += weight * total;
            result.pair_mass += weight;
            result.deal_mass += weight * (double)rivers;
        }
        for (player = 0u; player < player_count; ++player)
        {
            ++index[player];
            if (index[player] < ranges[player].count)
                break;
            index[player] = 0u;
        }
        if (player == player_count)
            break;
    }
    if (result.deal_mass > 0.0)
        result.value /= result.deal_mass;
    else
        result.ok = 0;
    return result;
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
    oracle_result_t oracle;
    double values[2] = {0.0, 0.0};
    size_t rivers = 0u;
    double mass = 0.0;
    int status;

    build_ranges(p0, &ranges[0], 3u, p1, &ranges[1], 3u);
    init_state(&state, 2u, 63.0, 31.5);
    status = pe_omaha_turn_range_values(context, board, MASK_EMPTY, ranges,
                                        &state, 4u, values, 2u, &rivers, &mass);
    CHECK(status == 0, "full turn terminal returned %d", status);
    CHECK(rivers == 48u, "a four-card board leaves 48 rivers, got %zu", rivers);
    CHECK(fabs(values[0] + values[1]) <= TOL,
          "heads-up values do not sum to zero: %.17g %.17g", values[0],
          values[1]);

    oracle = oracle_brute(board, MASK_EMPTY, ranges, &state, 2u, 0u);
    CHECK(oracle.ok, "the oracle failed on the fixture");
    CHECK(fabs(mass - oracle.deal_mass) <= TOL * oracle.deal_mass,
          "deal mass %.17g differs from the oracle %.17g", mass,
          oracle.deal_mass);
    CHECK(fabs(values[0] - oracle.value) <= TOL,
          "player 0 turn value %.17g differs from the oracle %.17g", values[0],
          oracle.value);

    oracle = oracle_brute(board, MASK_EMPTY, ranges, &state, 2u, 1u);
    CHECK(fabs(values[1] - oracle.value) <= TOL,
          "player 1 turn value %.17g differs from the oracle %.17g", values[1],
          oracle.value);
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
    oracle_result_t plain_oracle;
    oracle_result_t dead_oracle;
    double plain[2] = {0.0, 0.0};
    double masked[2] = {0.0, 0.0};
    size_t rivers = 0u;
    double mass = 0.0;
    int status;

    build_ranges(p0, &ranges[0], 3u, p1, &ranges[1], 3u);
    init_state(&state, 2u, 63.0, 31.5);
    plain_oracle = oracle_brute(board, MASK_EMPTY, ranges, &state, 2u, 0u);
    CHECK(plain_oracle.pair_mass > 0.0,
          "the fixture needs at least one legal pair");

    status = pe_omaha_turn_range_values(context, board, MASK_EMPTY, ranges,
                                        &state, 4u, plain, 2u, &rivers, &mass);
    CHECK(status == 0, "turn terminal returned %d", status);
    CHECK(rivers == 48u, "a four-card board leaves 48 candidate rivers, got %zu",
          rivers);
    CHECK(fabs(mass - 40.0 * plain_oracle.pair_mass) <=
              TOL * 40.0 * plain_oracle.pair_mass,
          "deal mass %.17g is not 40 rivers per legal pair (%.17g)", mass,
          40.0 * plain_oracle.pair_mass);

    dead_oracle = oracle_brute(board, dead, ranges, &state, 2u, 0u);
    status = pe_omaha_turn_range_values(context, board, dead, ranges, &state,
                                        4u, masked, 2u, &rivers, &mass);
    CHECK(status == 0, "turn terminal with dead cards returned %d", status);
    CHECK(rivers == 46u, "two dead cards leave 46 candidate rivers, got %zu",
          rivers);
    CHECK(fabs(mass - 38.0 * dead_oracle.pair_mass) <=
              TOL * 38.0 * dead_oracle.pair_mass,
          "deal mass %.17g is not 38 rivers per legal pair (%.17g)", mass,
          38.0 * dead_oracle.pair_mass);
    /* Dead cards remove two rivers per pair, so a pair's conditional value can
     * move even though the ranges are untouched.  The oracle prices the same
     * smaller chance space, and that is the claim. */
    CHECK(fabs(masked[0] - dead_oracle.value) <= TOL,
          "dead-card turn value %.17g differs from the oracle %.17g", masked[0],
          dead_oracle.value);
    dead_oracle = oracle_brute(board, dead, ranges, &state, 2u, 1u);
    CHECK(fabs(masked[1] - dead_oracle.value) <= TOL,
          "dead-card turn value %.17g differs from the oracle %.17g", masked[1],
          dead_oracle.value);
}

/* The chance node is not one arbitrary river: no combo of the fixture holds
 * either watched river, so the river terminal can price them with the ranges
 * untouched, and the turn figure has to differ from both. */
static void test_turn_is_not_one_river(EvalContext *context)
{
    static const int WATCHED[2] = {CARD(8, HEARTS), CARD(5, HEARTS)};
    const mask_t board = board_mask();
    pe_omaha_combo_t p0[3];
    pe_omaha_combo_t p1[3];
    pe_omaha_range_t ranges[2];
    pe_betting_state_t state;
    double turn[2] = {0.0, 0.0};
    size_t rivers = 0u;
    double mass = 0.0;
    size_t watch;

    build_ranges(p0, &ranges[0], 3u, p1, &ranges[1], 3u);
    init_state(&state, 2u, 63.0, 31.5);
    CHECK(pe_omaha_turn_range_values(context, board, MASK_EMPTY, ranges, &state,
                                     4u, turn, 2u, &rivers, &mass) == 0,
          "turn terminal rejected the fixture");

    for (watch = 0u; watch < 2u; ++watch)
    {
        double values[2] = {0.0, 0.0};
        double river_mass = 0.0;
        size_t deals = 0u;
        int status = pe_omaha_river_range_values(
            context, mask_set(board, WATCHED[watch]), ranges, &state, 4u,
            values, 2u, &deals, &river_mass);

        CHECK(status == 0, "river terminal for the watched river returned %d",
              status);
        CHECK(fabs(values[0] - turn[0]) > 1e-6,
              "the turn value %.17g is the value of a single river", turn[0]);
    }
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
    oracle_result_t oracle;
    double values[3] = {0.0, 0.0, 0.0};
    size_t rivers = 0u;
    double mass = 0.0;
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
    CHECK(fabs(values[0] + values[1] + values[2]) <= TOL,
          "three-way values do not sum to zero: %.17g %.17g %.17g", values[0],
          values[1], values[2]);
    for (player = 0u; player < 3u; ++player)
    {
        oracle = oracle_brute(board, MASK_EMPTY, ranges, &state, 3u, player);
        CHECK(oracle.ok, "the three-way oracle failed");
        CHECK(fabs(values[player] - oracle.value) <= TOL,
              "player %u turn value %.17g differs from the oracle %.17g",
              (unsigned)player, values[player], oracle.value);
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
    oracle_result_t oracle;
    double values[2] = {0.0, 0.0};
    size_t rivers = 0u;
    double mass = 0.0;
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
    CHECK(fabs(values[0] + values[1]) <= TOL,
          "PLO%u heads-up values do not sum to zero: %.17g %.17g",
          (unsigned)hole_cards, values[0], values[1]);
    oracle = oracle_brute(board, MASK_EMPTY, ranges, &state, 2u, 0u);
    CHECK(oracle.ok, "the PLO%u oracle failed", (unsigned)hole_cards);
    CHECK(fabs(values[0] - oracle.value) <= TOL,
          "PLO%u player 0 value %.17g differs from the oracle %.17g",
          (unsigned)hole_cards, values[0], oracle.value);
    oracle = oracle_brute(board, MASK_EMPTY, ranges, &state, 2u, 1u);
    CHECK(fabs(values[1] - oracle.value) <= TOL,
          "PLO%u player 1 value %.17g differs from the oracle %.17g",
          (unsigned)hole_cards, values[1], oracle.value);
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
    /* Dead card that sits on the board. */
    CHECK(pe_omaha_turn_range_values(context, board,
                                     (mask_t)(1ULL << TURN_BOARD[0]), ranges,
                                     &state, 4u, values, 2u, &rivers,
                                     &mass) == -1,
          "dead cards must be disjoint from the board");
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
    /* Missing outputs. */
    CHECK(pe_omaha_turn_range_values(context, board, MASK_EMPTY, ranges, &state,
                                     4u, NULL, 2u, &rivers, &mass) == -1,
          "a missing value array is rejected");
    CHECK(pe_omaha_turn_range_values(context, board, MASK_EMPTY, ranges, &state,
                                     4u, values, 2u, NULL, &mass) == -1,
          "a missing river count is rejected");
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
    test_full_turn_matches_oracle(context);
    test_chance_mass_normalization(context);
    test_turn_is_not_one_river(context);
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
