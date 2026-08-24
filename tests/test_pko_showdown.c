#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <poker_eval/economics/pko.h>

/* AA vs 72o all-in: AA eliminates the 72o player about 88% of the time.
 * Fixed seed keeps the Monte Carlo matrix reproducible. */
static void fill_aa_vs_72o(pe_pko_range_profile_t *profile)
{
    memset(profile, 0, sizeof(*profile));
    StdDeck_CardMask_RESET(profile->hand[0]);
    StdDeck_CardMask_OR(profile->hand[0],
                        StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES)),
                        StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS)));
    StdDeck_CardMask_RESET(profile->hand[1]);
    StdDeck_CardMask_OR(profile->hand[1],
                        StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_CLUBS)),
                        StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_DIAMONDS)));
    profile->weight = 1.0;
}

int main(void)
{
    pe_pko_showdown_config_t config;
    pe_pko_range_profile_t profile;
    double first[ICM_MAX_PLAYERS][ICM_MAX_PLAYERS];
    double second[ICM_MAX_PLAYERS][ICM_MAX_PLAYERS];

    memset(&config, 0, sizeof(config));
    config.seed = 42;
    config.board_samples = 1024;

    fill_aa_vs_72o(&profile);
    assert(pe_pko_outcome_showdown(&profile, 2, first, &config) == 0);
    assert(first[0][0] == 0.0 && first[1][1] == 0.0);
    assert(first[0][1] > 0.80 && first[0][1] < 0.95);
    assert(first[1][0] > 0.03 && first[1][0] < 0.18);

    /* Same seed, same profile: the matrix must reproduce exactly. */
    assert(pe_pko_outcome_showdown(&profile, 2, second, &config) == 0);
    assert(memcmp(first, second, sizeof(first)) == 0);

    /* Symmetric matchup (AKs vs AKo, disjoint suits): eliminations are
     * close to 50/50 up to Monte Carlo noise. */
    {
        pe_pko_range_profile_t mirror;
        double mirror_out[ICM_MAX_PLAYERS][ICM_MAX_PLAYERS];
        memset(&mirror, 0, sizeof(mirror));
        StdDeck_CardMask_RESET(mirror.hand[0]);
        StdDeck_CardMask_OR(mirror.hand[0],
                            StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_CLUBS)),
                            StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_CLUBS)));
        StdDeck_CardMask_RESET(mirror.hand[1]);
        StdDeck_CardMask_OR(mirror.hand[1],
                            StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS)),
                            StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS)));
        mirror.weight = 1.0;
        config.board_samples = 2048;
        assert(pe_pko_outcome_showdown(&mirror, 2, mirror_out, &config) == 0);
        assert(fabs(mirror_out[0][1] - mirror_out[1][0]) < 0.08);
    }

    /* End-to-end through the range enumerator: the showdown callback must
     * plug into pe_pko_calculate_from_ranges() unchanged. */
    {
        pe_combo_t combos[2];
        pe_pko_range_input_t input;
        pe_pko_range_result_t result;
        memset(&input, 0, sizeof(input));
        combos[0] = ((pe_combo_t){0});
        combos[1] = ((pe_combo_t){0});
        StdDeck_CardMask_OR(combos[0].hand,
                            StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES)),
                            StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS)));
        StdDeck_CardMask_OR(combos[1].hand,
                            StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_CLUBS)),
                            StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_DIAMONDS)));
        combos[0].weight = combos[1].weight = 1.0;
        input.ranges[0].combos = &combos[0];
        input.ranges[0].count = 1;
        input.ranges[1].combos = &combos[1];
        input.ranges[1].count = 1;
        input.base.icm.num_players = 2;
        input.base.icm.num_payouts = 2;
        input.base.icm.stacks[0] = input.base.icm.stacks[1] = 100.0;
        input.base.icm.payouts[0][0] = input.base.icm.payouts[1][0] = 70.0;
        input.base.icm.payouts[0][1] = input.base.icm.payouts[1][1] = 30.0;
        input.base.bounties[1] = 25.0;
        input.base.bounty_multiplier = 1.0;
        input.max_profiles = 4;
        input.outcome = pe_pko_outcome_showdown;
        input.user_data = &config;
        config.board_samples = 1024;
        assert(pe_pko_calculate_from_ranges(&input, &result) == 0);
        assert(result.profile_count == 1);
        assert(result.elimination_probability[0][1] > 0.75);
        assert(result.elimination_probability[1][0] < 0.25);
        assert(result.pko.bounty_ev[0] > result.pko.bounty_ev[1]);
    }
    puts("PKO showdown outcome tests passed");
    return 0;
}
