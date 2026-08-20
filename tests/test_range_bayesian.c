#include <assert.h>
#include <math.h>
#include <stdio.h>

#include <poker_eval/range.h>

typedef struct
{
    int action;
} likelihood_ctx_t;

static int has_card(StdDeck_CardMask hand, int card)
{
    return StdDeck_CardMask_CARD_IS_SET(hand, card) != 0;
}

static double four_bet_likelihood(size_t index,
                                  StdDeck_CardMask hand,
                                  int action,
                                  void *user_data)
{
    (void)index;
    likelihood_ctx_t *ctx = (likelihood_ctx_t *)user_data;
    assert(action == ctx->action);
    const int ace_spades = StdDeck_MAKE_CARD(StdDeck_Rank_ACE,
                                             StdDeck_Suit_SPADES);
    return has_card(hand, ace_spades) ? 1.0 : 0.0;
}

static double impossible_action(size_t index,
                                StdDeck_CardMask hand,
                                int action,
                                void *user_data)
{
    (void)index;
    (void)hand;
    (void)action;
    (void)user_data;
    return 0.0;
}

static double invalid_likelihood(size_t index,
                                 StdDeck_CardMask hand,
                                 int action,
                                 void *user_data)
{
    (void)index;
    (void)hand;
    (void)action;
    (void)user_data;
    return 1.5;
}

static double tiny_likelihood(size_t index,
                              StdDeck_CardMask hand,
                              int action,
                              void *user_data)
{
    (void)index;
    (void)hand;
    (void)action;
    (void)user_data;
    return ldexp(1.0, -1074);
}

static void test_bayesian_update(void)
{
    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);
    pe_range_t *range = NULL;
    assert(pe_range_parse(game_holdem, "AsAh,AsAd,KsKh,KsKd", dead,
                          NULL, &range) == PE_STATUS_OK);
    assert(range->count == 4);
    assert(fabs(range->total_weight - 4.0) < 1e-12);

    likelihood_ctx_t ctx = { 4 };
    assert(pe_range_bayesian_update(range, ctx.action,
                                    four_bet_likelihood, &ctx) == PE_STATUS_OK);
    assert(fabs(range->total_weight - 4.0) < 1e-12);
    assert(fabs(range->combos[0].weight - 2.0) < 1e-12);
    assert(fabs(range->combos[1].weight - 2.0) < 1e-12);
    assert(range->combos[2].weight == 0.0);
    assert(range->combos[3].weight == 0.0);
    pe_range_free(range);
}

static void test_invalid_or_zero_evidence(void)
{
    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);
    pe_range_t *range = NULL;
    assert(pe_range_parse(game_holdem, "AsAh,KsKh", dead, NULL, &range) ==
           PE_STATUS_OK);
    assert(pe_range_bayesian_update(range, 1, impossible_action, NULL) ==
           PE_STATUS_ERROR);
    assert(pe_range_bayesian_update(range, 1, invalid_likelihood, NULL) ==
           PE_STATUS_INVALID_ARG);
    pe_range_free(range);
}

static void test_subnormal_evidence_is_safe(void)
{
    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);
    pe_range_t *range = NULL;
    assert(pe_range_parse(game_holdem, "AsAh,KsKh", dead, NULL, &range) ==
           PE_STATUS_OK);
    assert(pe_range_bayesian_update(range, 1, tiny_likelihood, NULL) ==
           PE_STATUS_OK);
    assert(isfinite(range->combos[0].weight));
    assert(isfinite(range->combos[1].weight));
    assert(fabs(range->combos[0].weight - 1.0) < 1e-12);
    assert(fabs(range->combos[1].weight - 1.0) < 1e-12);
    pe_range_free(range);
}

int main(void)
{
    test_bayesian_update();
    test_invalid_or_zero_evidence();
    test_subnormal_evidence_is_safe();
    puts("range bayesian tests passed");
    return 0;
}
