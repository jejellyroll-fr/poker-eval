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
#include <poker_eval/solver/pe_pots.h>

#include "support/pe_omaha_hilo_winners.h"

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

typedef struct
{
    double value;      /* hero's chance-expanded mean net award */
    double pair_mass;  /* pooled weight of the legal combo tuples */
    double deal_mass;  /* pooled weight of the legal (hands, river) deals */
    int ok;            /* 0 when the showdown evaluator rejected a deal */
} oracle_result_t;

/* Independent brute force over the deals of a turn.  A tuple is legal when no
 * combo reuses a card the board, the dead mask or an earlier hand already holds
 * and every weight is positive; it then contributes once per river card still
 * in the deck.  Every river is paid with the slices the caller wrote: a slice
 * goes to its best eligible high hand, and on a Hi/Lo board its other half to
 * the best qualifying low, with no low sending that half back to the high
 * winners.  Values are per deal, net of each player's investment, normalised by
 * the deal mass.  Returns 0, or 1 when the evaluator rejected a deal or nothing
 * was dealt.
 */
static int oracle_deals(mask_t board, mask_t dead,
                        const pe_omaha_range_t *ranges, uint8_t player_count,
                        const pe_pot_slice_t *slices, uint8_t slice_count,
                        int hilo8, const double *invested, double *out_values,
                        double *out_pair_mass, double *out_deal_mass)
{
    double values[PE_OMAHA_TURN_PLAYERS_MAX] = {0.0};
    double pair_mass = 0.0;
    double deal_mass = 0.0;
    size_t index[PE_OMAHA_TURN_PLAYERS_MAX] = {0u};
    uint8_t player;

    for (;;)
    {
        mask_t holes[PE_OMAHA_TURN_PLAYERS_MAX];
        mask_t used = board | dead;
        double weight = 1.0;
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
                StdDeck_CardMask board_five =
                    std_mask(mask_set(board, card));
                HandVal high[PE_OMAHA_TURN_PLAYERS_MAX];
                LowHandVal low[PE_OMAHA_TURN_PLAYERS_MAX];
                double awards[PE_OMAHA_TURN_PLAYERS_MAX] = {0.0};
                uint8_t slice;

                for (player = 0u; player < player_count; ++player)
                {
                    high[player] = HandVal_NOTHING;
                    low[player] = LowHandVal_NOTHING;
                    if (hilo8)
                    {
                        if (StdDeck_OmahaHiLow8_EVAL(std_mask(holes[player]),
                                                     board_five, &high[player],
                                                     &low[player]) != 0)
                            return 1;
                    }
                    else if (StdDeck_OmahaHi_EVAL(std_mask(holes[player]),
                                                  board_five,
                                                  &high[player]) != 0)
                        return 1;
                }
                for (slice = 0u; slice < slice_count; ++slice)
                {
                    double amount = slices[slice].amount;
                    uint8_t eligible = slices[slice].eligible_mask;
                    uint8_t high_mask =
                        pe_test_best_high(high, eligible, player_count);

                    if (high_mask == 0u)
                        continue;
                    if (!hilo8)
                    {
                        uint8_t high_count = pe_test_count_bits(high_mask);

                        for (player = 0u; player < player_count; ++player)
                            if (high_mask & (uint8_t)(1u << player))
                                awards[player] += amount / (double)high_count;
                        continue;
                    }
                    {
                        double half = amount * 0.5;
                        uint8_t low_mask =
                            pe_test_best_low(low, eligible, player_count);
                        uint8_t high_count = pe_test_count_bits(high_mask);
                        uint8_t low_count;

                        if (low_mask == 0u)
                            low_mask = high_mask;
                        low_count = pe_test_count_bits(low_mask);
                        for (player = 0u; player < player_count; ++player)
                        {
                            if (high_mask & (uint8_t)(1u << player))
                                awards[player] += half / (double)high_count;
                            if (low_mask & (uint8_t)(1u << player))
                                awards[player] += half / (double)low_count;
                        }
                    }
                }
                for (player = 0u; player < player_count; ++player)
                    values[player] +=
                        weight * (awards[player] - invested[player]);
            }
            pair_mass += weight;
            deal_mass += weight * (double)rivers;
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
    if (!(deal_mass > 0.0))
        return 1;
    for (player = 0u; player < player_count; ++player)
        out_values[player] = values[player] / deal_mass;
    *out_pair_mass = pair_mass;
    *out_deal_mass = deal_mass;
    return 0;
}

/* The high-only view of the same reference: one pot, every player eligible, and
 * the hero's value, which is what the PLO4 fixtures below compare against. */
static oracle_result_t oracle_brute(mask_t board, mask_t dead,
                                    const pe_omaha_range_t *ranges,
                                    const pe_betting_state_t *state,
                                    uint8_t player_count, uint8_t hero)
{
    pe_pot_slice_t slice;
    double invested[PE_OMAHA_TURN_PLAYERS_MAX] = {0.0};
    double values[PE_OMAHA_TURN_PLAYERS_MAX] = {0.0};
    oracle_result_t result = {0.0, 0.0, 0.0, 1};
    uint8_t player;

    slice.amount = state->pot;
    slice.eligible_mask = (uint8_t)((1u << player_count) - 1u);
    for (player = 0u; player < player_count; ++player)
        invested[player] = state->invested[player];
    if (oracle_deals(board, dead, ranges, player_count, &slice, 1u, 0, invested,
                     values, &result.pair_mass, &result.deal_mass) != 0)
    {
        result.ok = 0;
        return result;
    }
    result.value = values[hero];
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

/* The profiled entry point is the same terminal, so it must return the same
 * bits, and the counters must be exact: the filter looks at every combo of
 * every range on each of the 48 candidate rivers, keeps the combos that do not
 * hold the river card, and the river terminal then counts the legal deals,
 * which with unit weights is the oracle's own deal mass. */
static void test_profile_counters(EvalContext *context)
{
    const mask_t board = board_mask();
    pe_omaha_combo_t p0[3];
    pe_omaha_combo_t p1[3];
    pe_omaha_range_t ranges[2];
    pe_betting_state_t state;
    pe_omaha_turn_stats_t stats;
    oracle_result_t oracle;
    double plain[2] = {0.0, 0.0};
    double profiled[2] = {0.0, 0.0};
    size_t plain_rivers = 0u;
    size_t profiled_rivers = 0u;
    double plain_mass = 0.0;
    double profiled_mass = 0.0;
    size_t expected_kept = 0u;
    size_t index;
    int card;

    build_ranges(p0, &ranges[0], 3u, p1, &ranges[1], 3u);
    init_state(&state, 2u, 63.0, 31.5);
    CHECK(pe_omaha_turn_range_values(context, board, MASK_EMPTY, ranges, &state,
                                     4u, plain, 2u, &plain_rivers,
                                     &plain_mass) == 0,
          "the plain turn terminal rejected the fixture");

    /* Poison the struct: a profiled call that forgets a counter would leave the
     * poison visible instead of a plausible zero. */
    memset(&stats, 0xff, sizeof(stats));
    CHECK(pe_omaha_turn_range_values_profiled(
              context, board, MASK_EMPTY, ranges, &state, 4u, profiled, 2u,
              &profiled_rivers, &profiled_mass, &stats) == 0,
          "the profiled turn terminal rejected the fixture");
    CHECK(memcmp(plain, profiled, sizeof(plain)) == 0,
          "the profiled path changed the values");
    CHECK(plain_rivers == profiled_rivers,
          "the profiled path changed the river count");
    CHECK(memcmp(&plain_mass, &profiled_mass, sizeof(plain_mass)) == 0,
          "the profiled path changed the deal mass");

    CHECK(stats.combos_examined == 48u * 2u * 3u,
          "the filter examined %zu combos, expected %u", stats.combos_examined,
          48u * 2u * 3u);
    for (card = 0; card < MODERN_DECK_SIZE; ++card)
    {
        if (mask_is_set(board, card))
            continue;
        for (index = 0u; index < 3u; ++index)
        {
            if ((p0[index].cards & ((mask_t)1u << card)) == 0u)
                ++expected_kept;
            if ((p1[index].cards & ((mask_t)1u << card)) == 0u)
                ++expected_kept;
        }
    }
    CHECK(stats.combos_kept == expected_kept,
          "the filter kept %zu combos, expected %zu", stats.combos_kept,
          expected_kept);
    CHECK(stats.rivers_to_terminal == 48u,
          "expected 48 rivers to reach the terminal, got %zu",
          stats.rivers_to_terminal);

    oracle = oracle_brute(board, MASK_EMPTY, ranges, &state, 2u, 0u);
    CHECK(oracle.ok, "the oracle failed on the fixture");
    CHECK(fabs((double)stats.deals - oracle.deal_mass) <=
              TOL * oracle.deal_mass,
          "the terminal counted %.17g deals, the oracle %.17g",
          (double)stats.deals, oracle.deal_mass);
    CHECK(stats.filter_seconds >= 0.0 && stats.terminal_seconds >= 0.0,
          "the phase clocks must not run backwards: %.17g %.17g",
          stats.filter_seconds, stats.terminal_seconds);
}

/* The frozen-strength probe is the same walk with the evaluation hoisted out of
 * it, so it has to reproduce the terminal bit for bit: same values, same deal
 * mass, same river count.  It is kept behind a check rather than trusted,
 * because a probe that drifted would still produce plausible per-visit numbers
 * for a walk nobody runs. */
static void test_frozen_strength_matches_live(EvalContext *context)
{
    const mask_t board = board_mask();
    pe_omaha_combo_t p0[3];
    pe_omaha_combo_t p1[3];
    pe_omaha_range_t ranges[2];
    pe_betting_state_t state;
    pe_omaha_turn_probe_t probe;
    double live[2] = {0.0, 0.0};
    double frozen[2] = {0.0, 0.0};
    size_t live_rivers = 0u;
    size_t frozen_rivers = 0u;
    double live_mass = 0.0;
    double frozen_mass = 0.0;
    size_t expected_combos = 0u;
    size_t index;
    int card;

    build_ranges(p0, &ranges[0], 3u, p1, &ranges[1], 3u);
    init_state(&state, 2u, 63.0, 31.5);
    CHECK(pe_omaha_turn_range_values(context, board, MASK_EMPTY, ranges, &state,
                                     4u, live, 2u, &live_rivers,
                                     &live_mass) == 0,
          "the live turn terminal rejected the fixture");
    CHECK(live_rivers == 48u,
          "the fixture must reach all 48 rivers, got %zu", live_rivers);

    /* Poison the struct: a probe that forgets a field would leave the poison
     * visible instead of a plausible zero. */
    memset(&probe, 0xff, sizeof(probe));
    CHECK(pe_omaha_turn_range_values_frozen(
              context, board, MASK_EMPTY, ranges, &state, 4u, frozen, 2u,
              &frozen_rivers, &frozen_mass, &probe) == 0,
          "the frozen turn terminal rejected the fixture");
    CHECK(memcmp(live, frozen, sizeof(live)) == 0,
          "the frozen walk changed the values");
    CHECK(live_rivers == frozen_rivers,
          "the frozen walk changed the river count");
    CHECK(memcmp(&live_mass, &frozen_mass, sizeof(live_mass)) == 0,
          "the frozen walk changed the deal mass");
    CHECK(probe.precompute_seconds >= 0.0 && probe.walk_seconds >= 0.0,
          "the probe clocks must not run backwards: %.17g %.17g",
          probe.precompute_seconds, probe.walk_seconds);

    /* One evaluation per combo of every range that survives the per-river
     * filter, so the count is the filter's kept combos over the 48 rivers. */
    for (card = 0; card < MODERN_DECK_SIZE; ++card)
    {
        if (mask_is_set(board, card))
            continue;
        for (index = 0u; index < 3u; ++index)
        {
            if ((p0[index].cards & ((mask_t)1u << card)) == 0u)
                ++expected_combos;
            if ((p1[index].cards & ((mask_t)1u << card)) == 0u)
                ++expected_combos;
        }
    }
    CHECK(probe.combos == expected_combos,
          "the precompute evaluated %zu combos, expected %zu", probe.combos,
          expected_combos);
    /* Unit weights, so the live deal mass is the deal count. */
    CHECK(fabs((double)probe.deals - live_mass) <= TOL * live_mass,
          "the frozen walk counted %.17g deals against a mass of %.17g",
          (double)probe.deals, live_mass);
}

/* The Hi/Lo turn prices every candidate river with the river terminal's Hi/Lo
 * payoff and combines them with the high-only chance expansion.  The oracle
 * here composes exactly that from the river terminal called directly: it
 * filters each range for the river card, prices the five-card board and
 * weights the result by the deal mass the river terminal reports.  The turn
 * agrees only if its filter, its chance weights and its reduction are the
 * high-only ones it claims, and the probe has to reproduce it bit for bit.
 *
 * The hero holds three cards below eight, so it has a low on almost every
 * river; the villain holds none, so the split moves the values away from the
 * high-only ones and a Hi/Lo run cannot pass by ignoring the low half. */
static void test_hilo_turn_matches_composition(EvalContext *context)
{
    static const int hero_cards[4] = {
        CARD(A, CLUBS), CARD(3, DIAMONDS), CARD(4, HEARTS), CARD(5, SPADES)
    };
    static const int villain_cards[4] = {
        CARD(K, CLUBS), CARD(Q, DIAMONDS), CARD(J, HEARTS), CARD(T, SPADES)
    };
    const mask_t board = board_mask();
    pe_omaha_combo_t p0[1];
    pe_omaha_combo_t p1[1];
    pe_omaha_combo_t kept[2][1];
    pe_omaha_range_t ranges[2];
    pe_omaha_range_t filtered[2];
    pe_betting_state_t state;
    pe_omaha_turn_stats_t stats;
    pe_omaha_turn_probe_t probe;
    double values[2] = {0.0, 0.0};
    double instrumented[2] = {0.0, 0.0};
    double high_only[2] = {0.0, 0.0};
    double expected[2] = {0.0, 0.0};
    double total = 0.0;
    size_t rivers = 0u;
    size_t instrumented_rivers = 0u;
    size_t high_rivers = 0u;
    double mass = 0.0;
    double instrumented_mass = 0.0;
    double high_mass = 0.0;
    int card;
    uint8_t player;
    int status;

    p0[0].cards = mask_of(hero_cards, 4u);
    p0[0].weight = 1.0;
    p1[0].cards = mask_of(villain_cards, 4u);
    p1[0].weight = 1.0;
    ranges[0].combos = p0;
    ranges[0].count = 1u;
    ranges[1].combos = p1;
    ranges[1].count = 1u;
    init_state(&state, 2u, 63.0, 31.5);

    status = pe_omaha_hilo8_turn_range_values(
        context, board, MASK_EMPTY, ranges, &state, 4u, values, 2u, &rivers,
        &mass, NULL, NULL);
    CHECK(status == 0, "the Hi/Lo turn returned %d", status);
    CHECK(rivers == 40u, "expected 40 rivers, got %zu", rivers);

    for (card = 0; card < MODERN_DECK_SIZE; ++card)
    {
        double river_values[2] = {0.0, 0.0};
        double river_mass = 0.0;
        size_t deals = 0u;
        int usable = 1;

        if (mask_is_set(board, card))
            continue;
        for (player = 0u; player < 2u; ++player)
        {
            size_t kept_count = 0u;
            size_t index;

            for (index = 0u; index < ranges[player].count; ++index)
                if ((ranges[player].combos[index].cards &
                     ((mask_t)1u << card)) == 0u)
                    kept[player][kept_count++] = ranges[player].combos[index];
            filtered[player].combos = kept[player];
            filtered[player].count = kept_count;
            if (kept_count == 0u)
                usable = 0;
        }
        if (!usable)
            continue;
        status = pe_omaha_hilo8_river_range_values(
            context, mask_set(board, card), filtered, &state, 4u, river_values,
            2u, &deals, &river_mass);
        CHECK(status == 0, "the Hi/Lo river terminal returned %d on card %d",
              status, card);
        if (status != 0)
            continue;
        expected[0] += river_values[0] * river_mass;
        expected[1] += river_values[1] * river_mass;
        total += river_mass;
    }
    CHECK(total > 0.0, "the composition found no deal mass");
    expected[0] /= total;
    expected[1] /= total;
    CHECK(fabs(values[0] - expected[0]) <= TOL * (fabs(expected[0]) + 1.0),
          "hero %.17g against the composition %.17g", values[0],
          expected[0]);
    CHECK(fabs(values[1] - expected[1]) <= TOL * (fabs(expected[1]) + 1.0),
          "villain %.17g against the composition %.17g", values[1],
          expected[1]);
    CHECK(fabs(values[0] + values[1]) <= TOL,
          "the Hi/Lo values do not sum to zero");

    /* The instrumented entry is the same walk: the counters and the low pass
     * must not move the values, and the low pass has to be timed. */
    memset(&stats, 0xff, sizeof(stats));
    memset(&probe, 0xff, sizeof(probe));
    status = pe_omaha_hilo8_turn_range_values(
        context, board, MASK_EMPTY, ranges, &state, 4u, instrumented, 2u,
        &instrumented_rivers, &instrumented_mass, &stats, &probe);
    CHECK(status == 0, "the instrumented Hi/Lo turn returned %d", status);
    CHECK(memcmp(values, instrumented, sizeof(values)) == 0,
          "the instrumented Hi/Lo turn changed the values");
    CHECK(rivers == instrumented_rivers,
          "the instrumented Hi/Lo turn changed the river count");
    CHECK(memcmp(&mass, &instrumented_mass, sizeof(mass)) == 0,
          "the instrumented Hi/Lo turn changed the deal mass");
    CHECK(stats.rivers_to_terminal == 40u,
          "the Hi/Lo filter reached %zu rivers, want 40",
          stats.rivers_to_terminal);
    CHECK(probe.combos == 80u,
          "the Hi/Lo precompute visited %zu combos, want 80", probe.combos);
    CHECK(probe.precompute_seconds > 0.0,
          "the Hi/Lo probe did not time the high pass");
    CHECK(probe.low_precompute_seconds > 0.0,
          "the Hi/Lo probe did not time the low pass");

    /* The low half has to move the values: the hero takes half the pot on the
     * rivers that give it a low, so a Hi/Lo run cannot return the high-only
     * numbers. */
    status = pe_omaha_turn_range_values(context, board, MASK_EMPTY, ranges,
                                        &state, 4u, high_only, 2u, &high_rivers,
                                        &high_mass);
    CHECK(status == 0, "the high-only turn returned %d", status);
    CHECK(memcmp(values, high_only, sizeof(values)) != 0,
          "the Hi/Lo turn returned the high-only values");
}

/* Three-way Hi/Lo with a side pot: the short stack has paid 10 into a pot of 50
 * and the two deep stacks 20 each, so the main 30 is open to all three and the
 * 20 side pot to the two that matched it.  The short stack holds the only low
 * draw, so it can never win more than the main pot while the two deep stacks
 * own the whole side pot; a terminal that paid one pot to everybody, or let a
 * player who never paid into the side pot win a share of it, would not match
 * the oracle.  The oracle writes those slices out itself and pays them with its
 * own high/low winners, so it does not share the terminal's slice builder. */
static void test_hilo_three_way_side_pot(EvalContext *context)
{
    static const int SHORT_HAND[4] = {
        CARD(A, CLUBS), CARD(3, DIAMONDS), CARD(4, HEARTS), CARD(5, SPADES)
    };
    static const int KINGS_HAND[4] = {
        CARD(K, CLUBS), CARD(K, HEARTS), CARD(8, DIAMONDS), CARD(8, CLUBS)
    };
    static const int ACES_HAND[4] = {
        CARD(A, DIAMONDS), CARD(A, HEARTS), CARD(6, CLUBS), CARD(6, DIAMONDS)
    };
    const mask_t board = board_mask();
    pe_omaha_combo_t p0[1];
    pe_omaha_combo_t p1[1];
    pe_omaha_combo_t p2[1];
    pe_omaha_range_t ranges[3];
    pe_betting_state_t state;
    /* The main pot is the 10 each of the three paid, the side pot the second 10
     * of the two deep stacks. */
    pe_pot_slice_t slices[2] = {{30.0, 0x07u}, {20.0, 0x06u}};
    double invested[3] = {10.0, 20.0, 20.0};
    double oracle_values[3] = {0.0, 0.0, 0.0};
    double values[3] = {0.0, 0.0, 0.0};
    double oracle_mass = 0.0;
    double pair_mass = 0.0;
    size_t rivers = 0u;
    double mass = 0.0;
    double sum = 0.0;
    uint8_t player;
    int status;

    p0[0].cards = mask_of(SHORT_HAND, 4u);
    p0[0].weight = 1.0;
    p1[0].cards = mask_of(KINGS_HAND, 4u);
    p1[0].weight = 1.0;
    p2[0].cards = mask_of(ACES_HAND, 4u);
    p2[0].weight = 1.0;
    ranges[0].combos = p0;
    ranges[0].count = 1u;
    ranges[1].combos = p1;
    ranges[1].count = 1u;
    ranges[2].combos = p2;
    ranges[2].count = 1u;
    init_state(&state, 3u, 50.0, 10.0);
    state.invested[1] = 20.0;
    state.invested[2] = 20.0;

    CHECK(oracle_deals(board, MASK_EMPTY, ranges, 3u, slices, 2u, 1, invested,
                       oracle_values, &pair_mass, &oracle_mass) == 0,
          "the side-pot oracle rejected a deal");
    CHECK(pair_mass > 0.0, "the side-pot fixture has no legal tuple");

    status = pe_omaha_hilo8_turn_range_values(
        context, board, MASK_EMPTY, ranges, &state, 4u, values, 3u, &rivers,
        &mass, NULL, NULL);
    CHECK(status == 0, "the three-way Hi/Lo turn returned %d", status);
    CHECK(rivers == 36u,
          "the fixture leaves 36 rivers, got %zu", rivers);
    for (player = 0u; player < 3u; ++player)
    {
        CHECK(fabs(values[player] - oracle_values[player]) <=
                  TOL * (fabs(oracle_values[player]) + 1.0),
              "player %u %.17g against the oracle %.17g", (unsigned)player,
              values[player], oracle_values[player]);
        sum += values[player];
        /* Nobody loses more than it put in, and the short stack cannot win
         * more than the main pot it paid into. */
        CHECK(values[player] >= -invested[player] - TOL,
              "player %u lost more than its investment: %.17g",
              (unsigned)player, values[player]);
    }
    CHECK(fabs(sum) <= TOL, "the Hi/Lo values sum to %.17g", sum);
    CHECK(values[0] <= 20.0 + TOL,
          "the short stack won %.17g out of a 30 main pot", values[0]);
    CHECK(values[1] <= 30.0 + TOL && values[2] <= 30.0 + TOL,
          "a deep stack won more than the 50 it could take");
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

/* A river that holds every combo of some range leaves that range empty, so
 * there is no deal on that river.  That is not an input error: the river is
 * skipped and the call still returns 0 with the remaining rivers priced.  One
 * combo per range, disjoint, so the eight cards the two hands hold are exactly
 * the eight rivers that empty a range and nothing else is skipped. */
static void test_river_that_empties_a_range_is_skipped(EvalContext *context)
{
    static const int P0_ONLY[4] = {
        CARD(Q, SPADES), CARD(J, SPADES), CARD(T, SPADES), CARD(9, SPADES)
    };
    static const int P1_ONLY[4] = {
        CARD(6, SPADES), CARD(6, HEARTS), CARD(8, SPADES), CARD(5, DIAMONDS)
    };
    const mask_t board = board_mask();
    pe_omaha_combo_t p0[1];
    pe_omaha_combo_t p1[1];
    pe_omaha_range_t ranges[2];
    pe_betting_state_t state;
    oracle_result_t oracle;
    double values[2] = {0.0, 0.0};
    size_t rivers = 0u;
    double mass = 0.0;
    int status;

    p0[0].cards = mask_of(P0_ONLY, 4u);
    p0[0].weight = 1.0;
    p1[0].cards = mask_of(P1_ONLY, 4u);
    p1[0].weight = 1.0;
    ranges[0].combos = p0;
    ranges[0].count = 1u;
    ranges[1].combos = p1;
    ranges[1].count = 1u;
    init_state(&state, 2u, 63.0, 31.5);

    status = pe_omaha_turn_range_values(context, board, MASK_EMPTY, ranges,
                                        &state, 4u, values, 2u, &rivers, &mass);
    CHECK(status == 0, "an empty range on some river is not an input error, got %d",
          status);
    CHECK(rivers == 40u, "expected the 40 playable of 48 rivers, got %zu",
          rivers);

    oracle = oracle_brute(board, MASK_EMPTY, ranges, &state, 2u, 0u);
    CHECK(oracle.ok && oracle.pair_mass == 1.0,
          "the fixture needs exactly one legal pair");
    CHECK(fabs(mass - oracle.deal_mass) <= TOL * oracle.deal_mass,
          "deal mass %.17g differs from the oracle %.17g", mass,
          oracle.deal_mass);
    CHECK(fabs(values[0] - oracle.value) <= TOL,
          "turn value %.17g differs from the oracle %.17g", values[0],
          oracle.value);
}

/* Every pair blocked: the two ranges hold the same single combo, so no tuple
 * is legal on any river.  That is "no deal" - code 1 with nothing priced and
 * zeroed outputs - and not an invalid input. */
static void test_no_legal_deal_returns_one(EvalContext *context)
{
    const mask_t board = board_mask();
    pe_omaha_combo_t p0[1];
    pe_omaha_combo_t p1[1];
    pe_omaha_range_t ranges[2];
    pe_betting_state_t state;
    double values[2] = {42.0, -42.0};
    size_t rivers = 99u;
    double mass = 99.0;
    int status;

    p0[0].cards = mask_of(P0_HANDS[0], 4u);
    p0[0].weight = 1.0;
    p1[0].cards = p0[0].cards;
    p1[0].weight = 1.0;
    ranges[0].combos = p0;
    ranges[0].count = 1u;
    ranges[1].combos = p1;
    ranges[1].count = 1u;
    init_state(&state, 2u, 63.0, 31.5);

    status = pe_omaha_turn_range_values(context, board, MASK_EMPTY, ranges,
                                        &state, 4u, values, 2u, &rivers, &mass);
    CHECK(status == 1, "no legal pair must report 1, got %d", status);
    CHECK(rivers == 0u, "no river was priced, but the count says %zu", rivers);
    CHECK(mass == 0.0, "no deal was priced, but the mass is %.17g", mass);
    CHECK(values[0] == 0.0 && values[1] == 0.0,
          "the values must be zeroed, got %.17g %.17g", values[0], values[1]);
}

/* The third way to have no deal: the ranges are not empty and every combo is a
 * legal private hand, but no combo carries weight, so no tuple has a positive
 * weight.  That is "no deal" as well - code 1, nothing priced, zeroed outputs -
 * and not an invalid input, which is why zero weights are accepted by the
 * validity check. */
static void test_zero_weight_ranges_report_no_deal(EvalContext *context)
{
    const mask_t board = board_mask();
    pe_omaha_combo_t p0[2];
    pe_omaha_combo_t p1[2];
    pe_omaha_range_t ranges[2];
    pe_betting_state_t state;
    oracle_result_t oracle;
    double values[2] = {42.0, -42.0};
    size_t rivers = 99u;
    double mass = 99.0;
    int status;

    build_ranges(p0, &ranges[0], 2u, p1, &ranges[1], 2u);
    /* The two combos per player are alternatives, so they may overlap each
     * other; what matters is that each is legal against the board. */
    p1[1].cards = mask_of(P1_HANDS[2], 4u);
    p0[0].weight = 0.0;
    p0[1].weight = 0.0;
    p1[0].weight = 0.0;
    p1[1].weight = 0.0;
    init_state(&state, 2u, 63.0, 31.5);

    oracle = oracle_brute(board, MASK_EMPTY, ranges, &state, 2u, 0u);
    CHECK(!oracle.ok && oracle.deal_mass == 0.0,
          "the oracle must agree that weightless ranges have no deal");

    status = pe_omaha_turn_range_values(context, board, MASK_EMPTY, ranges,
                                        &state, 4u, values, 2u, &rivers, &mass);
    CHECK(status == 1, "a weightless range must report 1, got %d", status);
    CHECK(rivers == 0u, "no river was priced, but the count says %zu", rivers);
    CHECK(mass == 0.0, "no deal was priced, but the mass is %.17g", mass);
    CHECK(values[0] == 0.0 && values[1] == 0.0,
          "the values must be zeroed, got %.17g %.17g", values[0], values[1]);
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
    test_profile_counters(context);
    test_frozen_strength_matches_live(context);
    test_hilo_turn_matches_composition(context);
    test_hilo_three_way_side_pot(context);
    test_chance_mass_normalization(context);
    test_turn_is_not_one_river(context);
    test_river_that_empties_a_range_is_skipped(context);
    test_no_legal_deal_returns_one(context);
    test_zero_weight_ranges_report_no_deal(context);
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
