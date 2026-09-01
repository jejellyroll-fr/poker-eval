#include <poker_eval/solver/pe_pots.h>

#include <math.h>
#include <stdio.h>

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); ++failures; } } while (0)

static void test_three_way_side_pots(void)
{
    pe_betting_state_t state = {0};
    pe_pot_slice_t slices[3] = {{0}};
    uint8_t winners[] = {0x01u, 0x02u, 0x06u};
    double awards[3];
    uint8_t count = 0u;
    state.player_count = 3u;
    state.active[0] = state.active[1] = state.active[2] = 1;
    state.invested[0] = 10.0;
    state.invested[1] = 20.0;
    state.invested[2] = 30.0;
    state.pot = 60.0;
    CHECK(pe_pot_slices_build(&state, slices, 3u, &count) == 0 && count == 3u,
          "three-way side pots were not built");
    CHECK(fabs(slices[0].amount - 30.0) < 1e-12 &&
              fabs(slices[1].amount - 20.0) < 1e-12 &&
              fabs(slices[2].amount - 10.0) < 1e-12,
          "unexpected side-pot amounts: %.17g %.17g %.17g", slices[0].amount,
          slices[1].amount, slices[2].amount);
    CHECK(slices[0].eligible_mask == 0x07u && slices[1].eligible_mask == 0x06u &&
              slices[2].eligible_mask == 0x04u,
          "unexpected eligibility masks: %u %u %u", slices[0].eligible_mask,
          slices[1].eligible_mask, slices[2].eligible_mask);
    CHECK(pe_pot_distribute(slices, count, winners, 3u, awards) == 0,
          "side-pot distribution failed");
    CHECK(fabs(awards[0] - 30.0) < 1e-12 && fabs(awards[1] - 20.0) < 1e-12 &&
              fabs(awards[2] - 10.0) < 1e-12,
          "unexpected awards: %.17g %.17g %.17g", awards[0], awards[1],
          awards[2]);
}

static void test_folded_dead_money(void)
{
    pe_betting_state_t state = {0};
    pe_pot_slice_t slices[2] = {{0}};
    uint8_t winners[] = {0x04u, 0x04u};
    double awards[3];
    uint8_t count = 0u;
    state.player_count = 3u;
    state.active[0] = 0;
    state.active[1] = state.active[2] = 1;
    state.invested[0] = 10.0;
    state.invested[1] = 10.0;
    state.invested[2] = 20.0;
    state.pot = 40.0;
    CHECK(pe_pot_slices_build(&state, slices, 2u, &count) == 0 && count == 2u,
          "folded-player side pots were not built");
    CHECK(slices[0].eligible_mask == 0x06u && slices[1].eligible_mask == 0x04u,
          "folded dead money eligibility is wrong");
    CHECK(pe_pot_distribute(slices, count, winners, 3u, awards) == 0 &&
              fabs(awards[2] - 40.0) < 1e-12,
          "folded dead money was not awarded to the live winner");
}

static void test_existing_pot_without_contributions(void)
{
    pe_betting_state_t state = {0};
    pe_pot_slice_t slices[1] = {{0}};
    uint8_t winners[] = {0x02u};
    double awards[3];
    uint8_t count = 0u;
    state.player_count = 3u;
    state.active[0] = state.active[1] = state.active[2] = 1;
    state.pot = 12.0;
    CHECK(pe_pot_slices_build(&state, slices, 1u, &count) == 0 &&
              count == 1u && slices[0].eligible_mask == 0x07u,
          "existing pot was not made eligible for active players");
    CHECK(pe_pot_distribute(slices, count, winners, 3u, awards) == 0 &&
              fabs(awards[1] - 12.0) < 1e-12,
          "existing pot was not distributed to the active winner");
}

int main(void)
{
    test_three_way_side_pots();
    test_folded_dead_money();
    test_existing_pot_without_contributions();
    if (failures)
        return 1;
    puts("test_pe_pots: multiway side-pot construction passed");
    return 0;
}
