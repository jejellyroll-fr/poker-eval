#include <poker_eval/solver/pe_omaha_deals.h>
#include <poker_eval/solver/pe_omaha_river.h>

#include <math.h>
#include <stdio.h>

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); ++failures; } } while (0)

static mask_t hand(const int *cards, size_t count)
{
    mask_t result = MASK_EMPTY;
    size_t index;
    for (index = 0u; index < count; ++index)
        result = mask_set(result, cards[index]);
    return result;
}

static void test_combo_counts(void)
{
    mask_t out[10];
    size_t count = 0u;
    CHECK(pe_omaha_combo_count(MASK_EMPTY, 4u) == 270725u,
          "PLO4 combo count is wrong");
    CHECK(pe_omaha_combo_count(MASK_EMPTY, 5u) == 2598960u,
          "PLO5 combo count is wrong");
    CHECK(pe_omaha_combo_count(MASK_EMPTY, 6u) == 20358520u,
          "PLO6 combo count is wrong");
    CHECK(pe_omaha_enumerate_combos(mask_set(MASK_EMPTY, 0), 5u, out, 10u,
                                    &count) == -2 && count == 2349060u,
          "PLO5 dead-card count is wrong");
}

static void test_plo_river(uint8_t hole_cards)
{
    EvalConfig config = eval_config_holdem();
    EvalContext *context = eval_context_create(&config);
    const mask_t board = hand((int[]){2, 18, 34, 48, 10}, 5u);
    int p0_cards[] = {12, 11, 24, 1, 14, 13};
    int p1_cards[] = {7, 19, 3, 4, 5, 6};
    pe_omaha_combo_t p0 = {hand(p0_cards, hole_cards), 1.0};
    pe_omaha_combo_t p1 = {hand(p1_cards, hole_cards), 1.0};
    const pe_omaha_range_t ranges[] = {{&p0, 1u}, {&p1, 1u}};
    pe_betting_state_t state = {0};
    double values[2];
    size_t deal_count = 0u;
    double weight_sum = 0.0;
    CHECK(context != NULL, "Omaha context creation failed");
    if (!context)
        return;
    state.player_count = 2u;
    state.active[0] = state.active[1] = 1;
    state.winner = -1;
    state.invested[0] = state.invested[1] = 10.0;
    state.pot = 20.0;
    CHECK(pe_omaha_river_range_values(context, board, ranges, &state,
                                      hole_cards, values, 2u, &deal_count,
                                      &weight_sum) == 0,
          "PLO%u river terminal failed", hole_cards);
    CHECK(deal_count == 1u && fabs(weight_sum - 1.0) <= 1e-12,
          "PLO%u deal mass is wrong", hole_cards);
    CHECK(fabs(values[0] - 10.0) <= 1e-12 &&
              fabs(values[1] + 10.0) <= 1e-12,
          "PLO%u showdown result is wrong: %.17g %.17g", hole_cards,
          values[0], values[1]);
    eval_context_destroy(context);
}

int main(void)
{
    test_combo_counts();
    test_plo_river(5u);
    test_plo_river(6u);
    if (failures)
        return 1;
    puts("test_pe_omaha_deals: PLO4/PLO5/PLO6 combos and river passed");
    return 0;
}
