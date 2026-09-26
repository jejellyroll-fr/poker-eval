/* test_plo_mixed_ranges.c - a restricted range mixed with 100% on PLO5/PLO6.
 *
 * A full PLO5/PLO6 range cannot be materialised -- C(52,6) is 20.4M combos --
 * so the direct solver used to require every player to share the same kind of
 * range: all explicit, or all complete.  Mixing "--range0 AAxxx --range1 100%"
 * therefore failed with "invalid plo5 range1: 100%", accusing the player who
 * held the full range even though that range is the one the format cannot
 * spell out.
 *
 * The sampler now draws a complete player from the live deck *after* the
 * list-driven ones, contributing the same 1 / C(live, n) proposal and the same
 * exact importance weight as the all-complete path.  This test pins:
 *   1. the mixed importance ratio equals the enumerated all-combos ratio;
 *   2. PLO5/PLO6 mixed deals are legal (right card count, disjoint, board
 *      dead) with the exact closed-form ratio;
 *   3. a mixed range has no closed-form normalisation (measure refuses it);
 *   4. the all-in preflop game accepts complete_mask and deals mixed spots.
 */

#include <poker_eval/core/cardmask_compat.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/range.h>
#include <poker_eval/solver/pe_preflop_allin_game.h>
#include <poker_eval/solver/pe_preflop_deal_sampler.h>
#include <poker_eval/solver/pe_range.h>
#include <poker_eval/solver/pe_rng.h>

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static mask_t cards(const int *values, size_t count)
{
    mask_t result = MASK_EMPTY;
    for (size_t i = 0u; i < count; ++i)
        result = mask_set(result, values[i]);
    return result;
}

/* Every drawn deal must be a legal n-card subset per player, disjoint from the
   board and from the other players. */
static void check_sample(const pe_preflop_deal_sampler_t *sampler, pe_rng_t *rng,
                         double expected_ratio)
{
    pe_preflop_deal_sample_t sample;
    if (pe_preflop_deal_sampler_sample(sampler, rng, &sample) != 0)
    {
        fprintf(stderr, "test_plo_mixed_ranges: sample failed\n");
        exit(1);
    }
    assert(fabs(sample.importance_ratio - expected_ratio) <= 1e-6);
    mask_t used = sampler->board;
    for (uint8_t p = 0u; p < sampler->player_count; ++p)
    {
        assert(mask_popcount(sample.holes[p]) == sampler->hole_cards);
        assert(!mask_intersects(used, sample.holes[p]));
        used |= sample.holes[p];
    }
}

static void mixed_holdem_agrees_with_enumerated(void)
{
    /* Player 0 holds two explicit hands; player 1 holds the complete range.
       The enumerated path spells the same thing as all 1326 hands, so the two
       must agree exactly: 2 * C(50,2) = 2450. */
    const int h0[] = {0, 13};
    const int h1[] = {1, 14};
    pe_holdem_combo_t combos0[] = {{cards(h0, 2), 1.0}, {cards(h1, 2), 1.0}};
    pe_holdem_range_t mixed_ranges[] = {{combos0, 2u}, {NULL, 0u}};
    pe_holdem_combo_t all[1326];
    pe_holdem_range_t enumerated_ranges[2];
    pe_preflop_deal_sampler_t mixed;
    pe_preflop_deal_sampler_t enumerated;
    pe_rng_t rng;
    size_t n = 0u;
    const double expected = 2.0 * (50.0 * 49.0 / 2.0);
    size_t deal_count;
    double weight_sum;

    for (int a = 0; a < 52; ++a)
        for (int b = a + 1; b < 52; ++b)
        {
            all[n].cards = mask_set(mask_set(MASK_EMPTY, a), b);
            all[n].weight = 1.0;
            ++n;
        }
    assert(n == 1326u);
    enumerated_ranges[0].combos = combos0;
    enumerated_ranges[0].count = 2u;
    enumerated_ranges[1].combos = all;
    enumerated_ranges[1].count = n;

    assert(pe_preflop_deal_sampler_init_holdem(&mixed, MASK_EMPTY, mixed_ranges,
                                               2u) == 0);
    assert(mixed.complete_ranges == 0u);
    assert(pe_preflop_deal_sampler_set_complete(&mixed, 1u) == 0);
    assert(pe_preflop_deal_sampler_set_complete(&mixed, 2u) == -1);

    /* No combo list exists for the complete player: the exact normalisation
       cannot be enumerated and must be refused, not guessed. */
    assert(pe_preflop_deal_sampler_measure(&mixed, &deal_count, &weight_sum) == -1);

    pe_rng_seed(&rng, 0x31CEDu);
    for (int i = 0; i < 200; ++i)
        check_sample(&mixed, &rng, expected);

    assert(pe_preflop_deal_sampler_init_holdem(&enumerated, MASK_EMPTY,
                                               enumerated_ranges, 2u) == 0);
    for (int i = 0; i < 50; ++i)
        check_sample(&enumerated, &rng, expected);

    puts("test_plo_mixed_ranges: mixed Hold'em matches the enumerated path");
}

static void mixed_plo_ratios(void)
{
    /* PLO5: one explicit 5-card hand + one complete player.
       ratio = C(52 - 5, 5) = 1,533,939. */
    {
        int hole[5] = {0, 1, 2, 3, 4};
        pe_omaha_combo_t combo = {cards(hole, 5u), 1.0};
        pe_omaha_range_t ranges[] = {{&combo, 1u}, {NULL, 0u}};
        pe_preflop_deal_sampler_t sampler;
        pe_rng_t rng;

        assert(pe_preflop_deal_sampler_init_omaha(&sampler, MASK_EMPTY, ranges,
                                                  2u, 5u) == 0);
        assert(pe_preflop_deal_sampler_set_complete(&sampler, 1u) == 0);
        pe_rng_seed(&rng, 0x50505050u);
        for (int i = 0; i < 200; ++i)
            check_sample(&sampler, &rng, 1533939.0);
    }

    /* PLO6: ratio = C(46, 6) = 9,366,819. */
    {
        int hole[6] = {0, 1, 2, 3, 4, 5};
        pe_omaha_combo_t combo = {cards(hole, 6u), 1.0};
        pe_omaha_range_t ranges[] = {{&combo, 1u}, {NULL, 0u}};
        pe_preflop_deal_sampler_t sampler;
        pe_rng_t rng;

        assert(pe_preflop_deal_sampler_init_omaha(&sampler, MASK_EMPTY, ranges,
                                                  2u, 6u) == 0);
        assert(pe_preflop_deal_sampler_set_complete(&sampler, 1u) == 0);
        pe_rng_seed(&rng, 0x60606060u);
        for (int i = 0; i < 200; ++i)
            check_sample(&sampler, &rng, 9366819.0);
    }

    /* PLO5 on a five-card board: the board and the explicit hand are dead, so
       ratio = C(52 - 5 - 5, 5) = C(42, 5) = 850,668. */
    {
        int board[5] = {0, 1, 2, 3, 4};
        int hole[5] = {5, 6, 7, 8, 9};
        pe_omaha_combo_t combo = {cards(hole, 5u), 1.0};
        pe_omaha_range_t ranges[] = {{&combo, 1u}, {NULL, 0u}};
        pe_preflop_deal_sampler_t sampler;
        pe_rng_t rng;

        assert(pe_preflop_deal_sampler_init_omaha(&sampler, cards(board, 5u),
                                                  ranges, 2u, 5u) == 0);
        assert(pe_preflop_deal_sampler_set_complete(&sampler, 1u) == 0);
        pe_rng_seed(&rng, 0x42424242u);
        for (int i = 0; i < 200; ++i)
            check_sample(&sampler, &rng, 850668.0);
    }

    /* Orientation does not change the weight: the restricted player is placed
       first whether it sits at seat 0 or seat 1. */
    {
        const int h0[] = {0, 13};
        const int h1[] = {1, 14};
        pe_holdem_combo_t combos[] = {{cards(h0, 2), 1.0}, {cards(h1, 2), 1.0}};
        pe_holdem_range_t ranges[] = {{NULL, 0u}, {combos, 2u}};
        pe_preflop_deal_sampler_t sampler;
        pe_rng_t rng;
        const double expected = 2.0 * (50.0 * 49.0 / 2.0);

        assert(pe_preflop_deal_sampler_init_holdem(&sampler, MASK_EMPTY, ranges,
                                                  2u) == 0);
        assert(pe_preflop_deal_sampler_set_complete(&sampler, 0u) == 0);
        pe_rng_seed(&rng, 0x12344321u);
        for (int i = 0; i < 100; ++i)
            check_sample(&sampler, &rng, expected);
    }

    puts("test_plo_mixed_ranges: PLO5/PLO6 mixed ratios are exact");
}

static void allin_game_accepts_mixed_rules(void)
{
    pe_preflop_allin_rules_t rules;
    pe_range_t *ranges[PE_PREFLOP_ALLIN_MAX_PLAYERS] = {NULL};
    pe_preflop_allin_game_t *game;
    StdDeck_CardMask dead;
    const pe_external_game_t *external;
    pe_rng_t rng;
    pe_chance_sample_t sample;
    const pe_preflop_betting_state_t *dealt;

    StdDeck_CardMask_RESET(dead);
    assert(pe_solver_range_parse(game_omaha5, "AAxxx", dead, &ranges[0]) ==
           PE_SOLVER_OK);
    assert(ranges[0] != NULL);

    memset(&rules, 0, sizeof(rules));
    rules.variant = PE_PREFLOP_PLO5;
    rules.player_count = 2;
    rules.stacks[0] = 100.0;
    rules.stacks[1] = 100.0;
    rules.small_blind = 0.5;
    rules.big_blind = 1.0;
    rules.min_raise = 1.0;
    rules.raise_count = 1;
    rules.allow_nonallin_call = 1;
    rules.showdown_samples = 16;
    rules.showdown_seed = 0x31CEDu;
    /* Player 1 holds the full range: no list to build. */
    rules.complete_mask = 2u;

    game = pe_preflop_allin_game_create(&rules, ranges);
    assert(game != NULL);

    external = pe_preflop_allin_external(game);
    assert(external != NULL);
    rng = pe_solver_rng_root(0xA11CEu);

    for (int i = 0; i < 32; ++i)
    {
        mask_t used;
        sample.outcome = -1;
        sample.importance_ratio = 0.0;
        dealt = external->sample_chance_child(external->root, &rng, &sample,
                                              external->user);
        assert(dealt != NULL);
        assert(sample.outcome == 0);
        assert(sample.importance_ratio > 0.0);
        assert(isfinite(sample.importance_ratio));
        used = MASK_EMPTY;
        for (int p = 0; p < rules.player_count; ++p)
        {
            assert(mask_popcount(dealt->holes[p]) == 5u);
            assert(!mask_intersects(used, dealt->holes[p]));
            used |= dealt->holes[p];
        }
        external->release_state(dealt, external->user);
    }

    pe_preflop_allin_game_destroy(game);
    pe_range_free(ranges[0]);
    puts("test_plo_mixed_ranges: all-in game deals mixed PLO5 spots");
}

int main(void)
{
    mixed_holdem_agrees_with_enumerated();
    mixed_plo_ratios();
    allin_game_accepts_mixed_rules();
    puts("test_plo_mixed_ranges: passed");
    return 0;
}
