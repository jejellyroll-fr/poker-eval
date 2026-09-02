#include <poker_eval/solver/pe_holdem_deals.h>
#include <poker_eval/solver/pe_holdem_river.h>

#include <math.h>
#include <stdio.h>

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); ++failures; } } while (0)

static mask_t cards(int a, int b)
{
    return mask_set(mask_set(MASK_EMPTY, a), b);
}

static int count_callback(const mask_t *holes, uint8_t players, double weight,
                          void *user)
{
    double *sum = (double *)user;
    CHECK(players == 3u, "callback player count is %u", players);
    CHECK((holes[0] & holes[1]) == 0 && (holes[0] & holes[2]) == 0 &&
              (holes[1] & holes[2]) == 0,
          "callback received overlapping deal");
    *sum += weight;
    return 0;
}

static void test_combo_enumeration(void)
{
    mask_t out[1225];
    size_t count = 0u;
    CHECK(pe_holdem_combo_count(MASK_EMPTY) == 1326u,
          "full deck combo count is wrong");
    CHECK(pe_holdem_combo_count(cards(0, 1)) == 1225u,
          "dead-card combo count is wrong");
    CHECK(pe_holdem_enumerate_combos(cards(0, 1), out, 1225u, &count) == 0,
          "combo enumeration failed");
    CHECK(count == 1225u, "expected 1225 combos, got %zu", count);
    CHECK(pe_holdem_enumerate_combos(cards(0, 1), out, 10u, &count) == -2 &&
              count == 1225u,
          "short combo buffer was not reported");
}

static void test_correlated_deals(void)
{
    const pe_holdem_combo_t p0[] = {{cards(0, 1), 1.0}, {cards(0, 2), 2.0}};
    const pe_holdem_combo_t p1[] = {{cards(0, 3), 3.0}, {cards(3, 4), 4.0}};
    const pe_holdem_combo_t p2[] = {{cards(5, 6), 5.0}};
    const pe_holdem_range_t ranges[] = {
        {p0, 2u}, {p1, 2u}, {p2, 1u}};
    size_t deal_count = 0u;
    double weight_sum = 0.0;
    double callback_sum = 0.0;
    CHECK(pe_holdem_deals_measure(MASK_EMPTY, ranges, 3u, &deal_count,
                                  &weight_sum) == 0,
          "deal measurement failed");
    CHECK(deal_count == 2u && fabs(weight_sum - 60.0) <= 1e-12,
          "expected 2 legal deals of mass 60, got %zu / %.17g", deal_count,
          weight_sum);
    CHECK(pe_holdem_deals_enumerate(MASK_EMPTY, ranges, 3u, count_callback,
                                    &callback_sum, &deal_count,
                                    &weight_sum) == 0,
          "deal enumeration failed");
    CHECK(deal_count == 2u && fabs(callback_sum - 60.0) <= 1e-12,
          "enumeration mass differs: %zu / %.17g", deal_count, callback_sum);
}

static void test_range_showdown_with_side_pots(void)
{
    EvalConfig config = eval_config_holdem();
    EvalContext *context = eval_context_create(&config);
    const mask_t board = cards(MODERN_MAKE_CARD(MODERN_RANK_2,
                                                 MODERN_SUIT_CLUBS),
                               MODERN_MAKE_CARD(MODERN_RANK_7,
                                                MODERN_SUIT_DIAMONDS)) |
                         mask_set(mask_set(mask_set(MASK_EMPTY,
                                                   MODERN_MAKE_CARD(MODERN_RANK_T,
                                                                    MODERN_SUIT_HEARTS)),
                                            MODERN_MAKE_CARD(MODERN_RANK_J,
                                                             MODERN_SUIT_SPADES)),
                                 MODERN_MAKE_CARD(MODERN_RANK_Q,
                                                  MODERN_SUIT_CLUBS));
    const pe_holdem_combo_t p0[] = {{cards(
        MODERN_MAKE_CARD(MODERN_RANK_A, MODERN_SUIT_SPADES),
        MODERN_MAKE_CARD(MODERN_RANK_K, MODERN_SUIT_SPADES)), 1.0}};
    const pe_holdem_combo_t p1[] = {{cards(
        MODERN_MAKE_CARD(MODERN_RANK_9, MODERN_SUIT_CLUBS),
        MODERN_MAKE_CARD(MODERN_RANK_8, MODERN_SUIT_CLUBS)), 1.0}};
    const pe_holdem_combo_t p2[] = {{cards(
        MODERN_MAKE_CARD(MODERN_RANK_3, MODERN_SUIT_CLUBS),
        MODERN_MAKE_CARD(MODERN_RANK_4, MODERN_SUIT_CLUBS)), 1.0}};
    const pe_holdem_range_t ranges[] = {{p0, 1u}, {p1, 1u}, {p2, 1u}};
    pe_betting_state_t state = {0};
    double stacks[] = {10.0, 20.0, 30.0};
    double values[3];
    size_t deal_count = 0u;
    double weight_sum = 0.0;
    state.player_count = 3u;
    state.active[0] = state.active[1] = state.active[2] = 1;
    state.winner = -1;
    state.invested[0] = 10.0;
    state.invested[1] = 20.0;
    state.invested[2] = 30.0;
    state.pot = 60.0;
    (void)stacks;
    CHECK(context != NULL, "Hold'em context creation failed");
    if (!context)
        return;
    CHECK(pe_holdem_river_range_values(context, board, ranges, &state, values,
                                       3u, &deal_count, &weight_sum) == 0,
          "range showdown failed");
    CHECK(deal_count == 1u && fabs(weight_sum - 1.0) <= 1e-12,
          "unexpected deal mass: %zu / %.17g", deal_count, weight_sum);
    CHECK(fabs(values[0] - 20.0) <= 1e-12 && fabs(values[1]) <= 1e-12 &&
              fabs(values[2] + 20.0) <= 1e-12,
          "unexpected net side-pot EV: %.17g %.17g %.17g", values[0],
          values[1], values[2]);
    eval_context_destroy(context);
}

int main(void)
{
    test_combo_enumeration();
    test_correlated_deals();
    test_range_showdown_with_side_pots();
    if (failures)
        return 1;
    puts("test_pe_holdem_deals: exact combo and correlated card removal passed");
    return 0;
}
