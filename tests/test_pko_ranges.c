#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <poker_eval/economics/pko.h>

static int winner_is_player_zero(const pe_pko_range_profile_t *profile,
                                 int players,
                                 double out[ICM_MAX_PLAYERS][ICM_MAX_PLAYERS],
                                 void *user)
{
    (void)profile; (void)user;
    memset(out, 0, sizeof(double) * ICM_MAX_PLAYERS * ICM_MAX_PLAYERS);
    assert(players == 2);
    out[0][1] = 1.0;
    return 0;
}

int main(void)
{
    pe_combo_t combos[2];
    pe_pko_range_input_t input;
    pe_pko_range_result_t result;
    memset(&input, 0, sizeof(input));
    StdDeck_CardMask_OR(combos[0].hand, StdDeck_MASK(12), StdDeck_MASK(25));
    StdDeck_CardMask_OR(combos[1].hand, StdDeck_MASK(0), StdDeck_MASK(13));
    combos[0].weight = combos[1].weight = 1.0;
    input.ranges[0].combos = &combos[0]; input.ranges[0].count = 1;
    input.ranges[1].combos = &combos[1]; input.ranges[1].count = 1;
    input.base.icm.num_players = 2; input.base.icm.num_payouts = 2;
    input.base.icm.stacks[0] = input.base.icm.stacks[1] = 100.0;
    input.base.icm.payouts[0][0] = input.base.icm.payouts[1][0] = 70.0;
    input.base.icm.payouts[0][1] = input.base.icm.payouts[1][1] = 30.0;
    input.base.bounties[1] = 25.0; input.base.bounty_multiplier = 1.0;
    input.max_profiles = 4;
    input.outcome = winner_is_player_zero;
    assert(pe_pko_calculate_from_ranges(&input, &result) == 0);
    assert(result.profile_count == 1);
    assert(fabs(result.elimination_probability[0][1] - 1.0) < 1e-9);
    assert(fabs(result.pko.bounty_ev[0] - 25.0) < 1e-9);
    input.max_profiles = 1;
    assert(pe_pko_calculate_from_ranges(&input, &result) == 0);
    assert(result.profile_count == 1);
    puts("PKO range/card-removal tests passed");
    return 0;
}
