/*
 * test_plo4_river_oracle.c - independent PLO4 river oracle (issue #280).
 *
 * Before the PLO4 showdown fast path is optimized, it needs a trusted
 * reference.  This test builds small, deterministic river fixtures with
 * 4-card private hands and checks three separate things:
 *
 * 1. Omaha ranking uses exactly two hole cards and three board cards.  The
 *    rigged fixtures below are chosen so that the unrestricted "best five of
 *    nine" answer is strictly better than the legal answer; the product
 *    evaluator must produce the legal one.
 *
 * 2. pe_showdown_vector() takes the documented pairwise fallback for 4-card
 *    private hands and its per-combo values match an independent enumeration
 *    of the opponent range (weighted, duplicated, dead and overlapping
 *    entries included).
 *
 * 3. pe_omaha_river_range_values() matches an independent deal-by-deal
 *    enumeration on a tractable heads-up fixture: legal reach mass, blockers,
 *    win/tie/loss payouts and per-player EV.
 *
 * The fixtures are small enough to enumerate exhaustively, so every value in
 * this test is exact; nothing here is sampled.
 */

#include <poker_eval/core/eval_context.h>
#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/games/eval_omaha.h>
#include <poker_eval/solver/pe_omaha_deals.h>
#include <poker_eval/solver/pe_omaha_river.h>
#include <poker_eval/solver/pe_showdown.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PLO4_HOLE_CARDS 4
#define PLO4_BOARD_CARDS 5
#define FIXTURE_MAX_COMBOS 8u

static int g_failures;

#define CHECK(cond, ...)                                                   \
    do                                                                     \
    {                                                                      \
        if (!(cond))                                                       \
        {                                                                  \
            fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__);        \
            fprintf(stderr, __VA_ARGS__);                                  \
            fputc('\n', stderr);                                           \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

/* "Ks" -> modern card index.  Suit order is the one in
 * modern_cardmask.h: clubs, diamonds, hearts, spades. */
static int card_of(const char *text)
{
    static const char ranks[] = "23456789TJQKA";
    const char *found;
    int rank;
    int suit;

    found = strchr(ranks, text[0]);
    if (!found || text[0] == '\0')
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

static void cards_of(mask_t mask, int *out, int *count)
{
    int card;

    *count = 0;
    for (card = 0; card < MODERN_DECK_SIZE; ++card)
        if (mask_is_set(mask, card))
            out[(*count)++] = card;
}

static HandVal five_card_value(const int *cards)
{
    StdDeck_CardMask hand;
    int index;

    StdDeck_CardMask_RESET(hand);
    for (index = 0; index < 5; ++index)
        StdDeck_CardMask_SET(hand, cards[index]);
    return StdDeck_StdRules_EVAL_N(hand, 5);
}

/* The reference ranking: the maximum over every legal 2-hole / 3-board
 * combination.  This is the definition Omaha is checked against, written out
 * explicitly instead of borrowing the evaluator's own loops. */
static HandVal independent_omaha_value(mask_t hole, mask_t board)
{
    int holes[PLO4_HOLE_CARDS];
    int boards[PLO4_BOARD_CARDS];
    int hole_count;
    int board_count;
    int h1, h2, b1, b2, b3;
    HandVal best = HandVal_NOTHING;

    cards_of(hole, holes, &hole_count);
    cards_of(board, boards, &board_count);
    if (hole_count != PLO4_HOLE_CARDS || board_count != PLO4_BOARD_CARDS)
        return HandVal_NOTHING;
    for (h1 = 0; h1 < hole_count - 1; ++h1)
        for (h2 = h1 + 1; h2 < hole_count; ++h2)
            for (b1 = 0; b1 < board_count - 2; ++b1)
                for (b2 = b1 + 1; b2 < board_count - 1; ++b2)
                    for (b3 = b2 + 1; b3 < board_count; ++b3)
                    {
                        int picked[5];
                        HandVal value;

                        picked[0] = holes[h1];
                        picked[1] = holes[h2];
                        picked[2] = boards[b1];
                        picked[3] = boards[b2];
                        picked[4] = boards[b3];
                        value = five_card_value(picked);
                        if (best == HandVal_NOTHING || value > best)
                            best = value;
                    }
    return best;
}

/* Deliberately illegal control: best five of all nine cards, which is what a
 * missing 2-hole/3-board restriction would compute. */
static HandVal unrestricted_five_of_nine(mask_t hole, mask_t board)
{
    int all[PLO4_HOLE_CARDS + PLO4_BOARD_CARDS];
    int count = 0;
    int card;
    int i, j, k, l, m;
    HandVal best = HandVal_NOTHING;

    for (card = 0; card < MODERN_DECK_SIZE; ++card)
        if (mask_is_set(hole, card) || mask_is_set(board, card))
            all[count++] = card;
    if (count != PLO4_HOLE_CARDS + PLO4_BOARD_CARDS)
        return HandVal_NOTHING;
    for (i = 0; i < count - 4; ++i)
        for (j = i + 1; j < count - 3; ++j)
            for (k = j + 1; k < count - 2; ++k)
                for (l = k + 1; l < count - 1; ++l)
                    for (m = l + 1; m < count; ++m)
                    {
                        int picked[5];
                        HandVal value;

                        picked[0] = all[i];
                        picked[1] = all[j];
                        picked[2] = all[k];
                        picked[3] = all[l];
                        picked[4] = all[m];
                        value = five_card_value(picked);
                        if (best == HandVal_NOTHING || value > best)
                            best = value;
                    }
    return best;
}

static HandVal product_omaha_value(mask_t hole, mask_t board, int *ok)
{
    StdDeck_CardMask hole_mask = to_std_mask(hole);
    StdDeck_CardMask board_mask = to_std_mask(board);
    HandVal value = HandVal_NOTHING;

    *ok = StdDeck_OmahaHi_EVAL(hole_mask, board_mask, &value) == 0;
    return value;
}

/* ------------------------------------------------------------------ *
 * Omaha ranking is exactly 2 hole + 3 board
 * ------------------------------------------------------------------ */

static void check_rule_fixture(const char *label, const char *hole_text,
                               const char *board_text, int expected_hand_type,
                               int naive_must_differ)
{
    mask_t hole = mask_of(hole_text);
    mask_t board = mask_of(board_text);
    HandVal legal;
    HandVal naive;
    HandVal product;
    int ok = 0;

    CHECK(mask_popcount(hole) == 4u, "%s: hole %s is not four cards", label,
          hole_text);
    CHECK(mask_popcount(board) == 5u, "%s: board %s is not five cards", label,
          board_text);
    legal = independent_omaha_value(hole, board);
    naive = unrestricted_five_of_nine(hole, board);
    product = product_omaha_value(hole, board, &ok);

    CHECK(ok, "%s: product Omaha evaluator rejected a legal river hand",
          label);
    CHECK(legal != HandVal_NOTHING, "%s: reference evaluator produced nothing",
          label);
    CHECK(HandVal_HANDTYPE(legal) == expected_hand_type,
          "%s: reference hand type is %d, expected %d", label,
          (int)HandVal_HANDTYPE(legal), expected_hand_type);
    CHECK(product == legal,
          "%s: product hand value differs from the explicit 2-hole/3-board "
          "reference", label);
    if (naive_must_differ)
    {
        /* The fixture is only meaningful if the illegal answer is strictly
         * better: otherwise it cannot tell the two rules apart. */
        CHECK(naive > legal,
              "%s: fixture does not separate the 2+3 rule from best-of-nine",
              label);
        CHECK(product != naive,
              "%s: product ranking used more than two hole cards", label);
    }
    else
    {
        CHECK(naive == legal,
              "%s: control should agree with best-of-nine", label);
    }
}

static void test_omaha_ranking_uses_two_hole_cards(void)
{
    /* Four hole spades, three board spades: the legal flush is A-K-8-3-2
     * (As Ks + 8s 2s 3s), while best-of-nine would use all four hole spades
     * and make A-K-Q-J-8.  The two must not be confused. */
    check_rule_fixture("four hole spades", "AsKsQsJs", "8s8h8d2s3s",
                       StdRules_HandType_FLUSH, 1);

    /* Three hole aces on a board of trips: the legal hand is nines full of
     * aces (AA + 999), not aces full of nines (AAA + 99). */
    check_rule_fixture("three hole aces", "AsAdAhAc", "9s9h9dKcQh",
                       StdRules_HandType_FULLHOUSE, 1);

    /* Control: no third hole card of the same suit, so both rules agree.
     * This pins that the previous checks fail because of the restriction and
     * not because the reference evaluator is simply weaker.  The board has a
     * pair of eights rather than trips, otherwise best-of-nine could find a
     * full house that the legal flush outranks in the first fixture but not
     * here. */
    check_rule_fixture("two hole spades", "AsKs2c3c", "8s8h4d2s3s",
                       StdRules_HandType_FLUSH, 0);
}

/* ------------------------------------------------------------------ *
 * Showdown vector matches an independent enumeration
 * ------------------------------------------------------------------ */

typedef struct
{
    const char *cards;
    double weight;
} range_entry_t;

typedef struct
{
    const char *name;
    const char *board;
    const char *extra_dead;       /* NULL when the board is the only dead set */
    const range_entry_t *hero;
    size_t hero_n;
    const range_entry_t *opp;
    size_t opp_n;
    const double *expected_ev;    /* NULL when only the oracle is checked */
    double pot;
} showdown_fixture_t;

static double independent_ev(mask_t hero, int64_t hero_strength,
                             const mask_t *opp_masks,
                             const int64_t *opp_strength,
                             const double *opp_reach, size_t opp_n,
                             mask_t dead, double pot, double *out_mass)
{
    double payout = 0.0;
    size_t opponent;

    *out_mass = 0.0;
    for (opponent = 0; opponent < opp_n; ++opponent)
    {
        if ((opp_masks[opponent] & dead) != 0)
            continue;
        if ((opp_masks[opponent] & hero) != 0)
            continue;
        *out_mass += opp_reach[opponent];
        if (hero_strength > opp_strength[opponent])
            payout += opp_reach[opponent];
        else if (hero_strength == opp_strength[opponent])
            payout += 0.5 * opp_reach[opponent];
    }
    return payout * pot;
}

static void run_showdown_fixture(const showdown_fixture_t *fixture)
{
    mask_t board = mask_of(fixture->board);
    mask_t dead = board;
    mask_t hero_masks[FIXTURE_MAX_COMBOS];
    mask_t opp_masks[FIXTURE_MAX_COMBOS];
    int64_t hero_strength[FIXTURE_MAX_COMBOS];
    int64_t opp_strength[FIXTURE_MAX_COMBOS];
    double opp_reach[FIXTURE_MAX_COMBOS];
    double mass[FIXTURE_MAX_COMBOS];
    pe_value_vec_t sorted = {0};
    pe_value_vec_t pairwise = {0};
    pe_showdown_path_t path = PE_SHOWDOWN_PATH_NONE;
    size_t i;

    CHECK(fixture->hero_n <= FIXTURE_MAX_COMBOS &&
              fixture->opp_n <= FIXTURE_MAX_COMBOS,
          "%s: fixture is larger than this test supports", fixture->name);
    if (fixture->hero_n > FIXTURE_MAX_COMBOS ||
        fixture->opp_n > FIXTURE_MAX_COMBOS)
        return;
    if (fixture->extra_dead)
        dead |= mask_of(fixture->extra_dead);

    for (i = 0; i < fixture->hero_n; ++i)
    {
        hero_masks[i] = mask_of(fixture->hero[i].cards);
        hero_strength[i] = (hero_masks[i] & dead) == 0
                               ? (int64_t)independent_omaha_value(hero_masks[i],
                                                                  board)
                               : 0;
    }
    for (i = 0; i < fixture->opp_n; ++i)
    {
        opp_masks[i] = mask_of(fixture->opp[i].cards);
        opp_strength[i] = (opp_masks[i] & dead) == 0
                              ? (int64_t)independent_omaha_value(opp_masks[i],
                                                                 board)
                              : 0;
        opp_reach[i] = fixture->opp[i].weight;
    }

    CHECK(pe_vec_alloc(&sorted, fixture->hero_n) == PE_SOLVER_OK,
          "%s: sorted output allocation failed", fixture->name);
    CHECK(pe_vec_alloc(&pairwise, fixture->hero_n) == PE_SOLVER_OK,
          "%s: pairwise output allocation failed", fixture->name);
    if (!sorted.v || !pairwise.v)
        goto cleanup;

    CHECK(pe_showdown_vector(hero_masks, hero_strength, fixture->hero_n,
                             opp_masks, opp_strength, opp_reach,
                             fixture->opp_n, dead, fixture->pot, &sorted,
                             &path) == PE_SOLVER_OK,
          "%s: vector showdown failed", fixture->name);
    CHECK(path == PE_SHOWDOWN_PATH_PAIRWISE,
          "%s: four-card hands did not use the pairwise fallback", fixture->name);
    CHECK(pe_showdown_vector_pairwise(hero_masks, hero_strength,
                                      fixture->hero_n, opp_masks,
                                      opp_strength, opp_reach, fixture->opp_n,
                                      dead, fixture->pot, &pairwise)
              == PE_SOLVER_OK,
          "%s: pairwise oracle failed", fixture->name);

    for (i = 0; i < fixture->hero_n; ++i)
    {
        double oracle = independent_ev(hero_masks[i], hero_strength[i],
                                       opp_masks, opp_strength, opp_reach,
                                       fixture->opp_n, dead, fixture->pot,
                                       &mass[i]);

        CHECK(fabs(sorted.v[i] - oracle) <= 1e-12,
              "%s: combo %zu vector value %.17g differs from enumeration "
              "%.17g", fixture->name, i, sorted.v[i], oracle);
        CHECK(fabs(pairwise.v[i] - oracle) <= 1e-12,
              "%s: combo %zu pairwise value %.17g differs from enumeration "
              "%.17g", fixture->name, i, pairwise.v[i], oracle);
        CHECK(fabs(sorted.v[i] - pairwise.v[i]) <= 1e-12,
              "%s: combo %zu vector %.17g and pairwise %.17g disagree",
              fixture->name, i, sorted.v[i], pairwise.v[i]);
        if (fixture->expected_ev)
            CHECK(fabs(oracle - fixture->expected_ev[i]) <= 1e-12,
                  "%s: combo %zu oracle EV %.17g does not match the "
                  "hand-checked %.17g (strength %lld, legal mass %.17g)",
                  fixture->name, i, oracle, fixture->expected_ev[i],
                  (long long)hero_strength[i], mass[i]);
        /* Zero-reach opponents must not move the value at all. */
        CHECK((hero_masks[i] & dead) != 0 || mass[i] > 0.0 ||
                  fabs(sorted.v[i]) <= 1e-12,
              "%s: combo %zu has no legal reach but a non-zero value",
              fixture->name, i);
    }

cleanup:
    pe_vec_free(&sorted);
    pe_vec_free(&pairwise);
}

static void test_showdown_vector_matches_enumeration(void)
{
    /* Hand-checked fixture.  Board 2c7dThJsQc, pot 4.
     *   hero 0 = AsKs3d4d -> broadway A-K-Q-J-T
     *   hero 1 = 9d8d6c6h -> Q-J-T-9-8
     *   hero 2 = QsQh3c3s -> trips queens (Q-Q-Q-J-T)
     *   hero 3 = 2c...     -> contains a board card (dead)
     *   opp 0  = 9c8c...   weight 0.50 -> Q-J-T-9-8
     *   opp 1  = same hand, weight 0.25 -> duplicate opponent entry
     *   opp 2  = AcKc...   weight 0.125 -> broadway (tie with hero 0)
     *   opp 3  = QdQh...   weight 0.125 -> trips queens
     *   opp 4  = KdKc...   weight 0.000 -> zero reach
     * opp 3 = QdQh...   weight 0.125 -> trips queens
     * opp 4 = KdKc...   weight 0.000 -> zero reach
     * Straights outrank trips, so hero 2 (trips) loses to opp 0, opp 1 and
     * opp 2, while opp 3 holds Qh and therefore blocks it entirely.
     * hero 0: 0.75 win + 0.125 tie * 0.5 + 0.125 win = 0.9375 -> 3.75
     * hero 1: (0.5 + 0.25) tie * 0.5 + 0.125 win = 0.5 -> 2.0
     * hero 2: 0.75 loss mass + 0.125 blocked -> 0.0
     * hero 3: dead -> 0 */
    static const range_entry_t hero[] = {
        {"AsKs3d4d", 0.0},
        {"9d8d6c6h", 0.0},
        {"QsQh3c3s", 0.0},
        {"2cAhKdQd", 0.0},
    };
    static const range_entry_t opp[] = {
        {"9c8c5d5h", 0.500},
        {"9c8c5d5h", 0.250},
        {"AcKc4c4s", 0.125},
        {"QdQh9s9h", 0.125},
        {"KdKc7h6h", 0.000},
    };
    static const double expected[] = {3.75, 2.0, 0.0, 0.0};
    static const showdown_fixture_t fixture = {
        "weighted, duplicated, tied, zero-reach",
        "2c7dThJsQc",
        NULL,
        hero,
        4u,
        opp,
        5u,
        expected,
        4.0,
    };

    run_showdown_fixture(&fixture);
}

static void test_showdown_vector_honours_extra_dead_cards(void)
{
    /* An extra dead card outside the board must remove both a hero combo and
     * the opponent entries that hold it.  Values are checked against the same
     * independent enumeration, which is where the blocking rule lives. */
    static const range_entry_t hero[] = {
        {"AsKs3d4d", 0.0},
        {"KdQd9c8c", 0.0},   /* blocked by the extra dead Kd */
        {"9d8d6c6h", 0.0},
    };
    static const range_entry_t opp[] = {
        {"AcKc4c4s", 0.250},
        {"KdKc7h6h", 0.375},  /* blocked by the extra dead Kd */
        {"9c8c5d5h", 0.250},
        {"QdQh9s9h", 0.125},
    };
    static const showdown_fixture_t fixture = {
        "extra dead card",
        "2c7dThJsQc",
        "Kd",
        hero,
        3u,
        opp,
        4u,
        NULL,
        2.0,
    };

    run_showdown_fixture(&fixture);
}

/* ------------------------------------------------------------------ *
 * PLO4 river terminal matches an independent deal enumeration
 * ------------------------------------------------------------------ */

static void test_river_terminal_matches_independent_deals(void)
{
    static const pe_omaha_combo_t hero_combos[] = {
        {0, 0.50},
        {0, 0.30},
        {0, 0.20},
    };
    static const pe_omaha_combo_t opp_combos[] = {
        {0, 0.60},
        {0, 0.30},
        {0, 0.10},
    };
    mask_t board = mask_of("2c7dThJsQc");
    pe_omaha_combo_t hero[3];
    pe_omaha_combo_t opp[3];
    pe_omaha_range_t ranges[2];
    pe_betting_state_t state = {0};
    double values[2] = {0.0, 0.0};
    double independent[2] = {0.0, 0.0};
    double weight_sum = 0.0;
    size_t deal_count = 0u;
    EvalConfig config = eval_config_holdem();
    EvalContext *context;
    size_t i;
    size_t j;

    memcpy(hero, hero_combos, sizeof(hero));
    memcpy(opp, opp_combos, sizeof(opp));
    hero[0].cards = mask_of("AsKs3d4d");
    hero[1].cards = mask_of("9d8d6c6h");
    hero[2].cards = mask_of("QsQh3c3s");
    opp[0].cards = mask_of("9c8c5d5h");
    opp[1].cards = mask_of("AcKc4c4s");
    opp[2].cards = mask_of("QdQh9s9h");
    ranges[0].combos = hero;
    ranges[0].count = 3u;
    ranges[1].combos = opp;
    ranges[1].count = 3u;
    state.player_count = 2u;
    state.winner = -1;
    state.active[0] = 1;
    state.active[1] = 1;
    state.invested[0] = 10.0;
    state.invested[1] = 10.0;
    state.pot = 20.0;

    /* Independent enumeration: every legal deal, weighted, with the same
     * pot-slice rule (one slice; ties split it). */
    for (i = 0; i < 3u; ++i)
    {
        if ((hero[i].cards & board) != 0)
            continue;
        for (j = 0; j < 3u; ++j)
        {
            double weight;
            HandVal hero_value;
            HandVal opp_value;
            double award;

            if ((opp[j].cards & board) != 0 || opp[j].weight <= 0.0)
                continue;
            if ((hero[i].cards & opp[j].cards) != 0)
                continue;
            hero_value = independent_omaha_value(hero[i].cards, board);
            opp_value = independent_omaha_value(opp[j].cards, board);
            weight = hero[i].weight * opp[j].weight;
            if (hero_value > opp_value)
                award = 20.0;
            else if (hero_value == opp_value)
                award = 10.0;
            else
                award = 0.0;
            independent[0] += weight * (award - 10.0);
            independent[1] += weight * (20.0 - award - 10.0);
            weight_sum += weight;
            ++deal_count;
        }
    }
    CHECK(deal_count > 0u && weight_sum > 0.0,
          "terminal fixture produced no legal deals");
    if (deal_count == 0u || weight_sum <= 0.0)
        return;
    independent[0] /= weight_sum;
    independent[1] /= weight_sum;

    context = eval_context_create(&config);
    CHECK(context != NULL, "PLO4 river context creation failed");
    if (!context)
        return;
    {
        size_t product_deals = 0u;
        double product_mass = 0.0;

        CHECK(pe_omaha_river_range_values(context, board, ranges, &state, 4u,
                                          values, 2u, &product_deals,
                                          &product_mass) == 0,
              "PLO4 river terminal failed");
        CHECK(product_deals == deal_count,
              "terminal counted %zu deals, enumeration counted %zu",
              product_deals, deal_count);
        CHECK(fabs(product_mass - weight_sum) <= 1e-12,
              "terminal reach mass %.17g, enumeration %.17g", product_mass,
              weight_sum);
    }
    CHECK(fabs(values[0] - independent[0]) <= 1e-9,
          "player 0 EV %.17g, enumeration %.17g", values[0], independent[0]);
    CHECK(fabs(values[1] - independent[1]) <= 1e-9,
          "player 1 EV %.17g, enumeration %.17g", values[1], independent[1]);
    CHECK(fabs(values[0] + values[1]) <= 1e-9,
          "river terminal is not zero-sum (%.17g)", values[0] + values[1]);
    eval_context_destroy(context);
}

int main(void)
{
    test_omaha_ranking_uses_two_hole_cards();
    test_showdown_vector_matches_enumeration();
    test_showdown_vector_honours_extra_dead_cards();
    test_river_terminal_matches_independent_deals();
    if (g_failures != 0)
    {
        fprintf(stderr, "test_plo4_river_oracle: %d failure(s)\n", g_failures);
        return 1;
    }
    puts("test_plo4_river_oracle: PLO4 river matches independent oracles");
    return 0;
}
