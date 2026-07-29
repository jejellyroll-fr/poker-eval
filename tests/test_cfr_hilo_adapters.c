#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <poker_eval/core/eval_context.h>
#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/core/low_eval.h>
#include <poker_eval/core/low_qualifier.h>
#include <poker_eval/engine/solvers/cfr/omaha8_river_adapter.h>
#include <poker_eval/engine/solvers/cfr/razz_river_adapter.h>

static int nearly_equal(double a, double b, double tol)
{
    return fabs(a - b) <= tol;
}

static mask_t make_mask(const int *cards, int count)
{
    mask_t m = MASK_EMPTY;
    for (int i = 0; i < count; ++i)
    {
        m = mask_set(m, cards[i]);
    }
    return m;
}

static int test_omaha8_quarter(void)
{
    EvalConfig cfg = eval_config_omaha();
    EvalContext *ctx = eval_context_create(&cfg);
    if (!ctx)
        return 1;

    /* Board: Ah 2d 5h Kh Qc */
    int board_cards[] = {
        StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_DIAMONDS),
        StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS)};
    mask_t board = make_mask(board_cards, 5);

    /* Player 0: Jh Th 3c 4c -> flush + wheel */
    int p0_cards[] = {
        StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_CLUBS),
        StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_CLUBS)};
    mask_t h0 = make_mask(p0_cards, 4);

    /* Player 1: As Ks 3d 4d -> no flush, wheel low */
    int p1_cards[] = {
        StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES),
        StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES),
        StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_DIAMONDS),
        StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_DIAMONDS)};
    mask_t h1 = make_mask(p1_cards, 4);

    cfr_game_t game;
    o8_state_t state;
    o8_build_game(ctx, h0, h1, board, &game, &state);

    /* Force showdown */
    state.hist = 0x3u; /* check-check */
    state.to_call = 0.0;
    state.pot = 100.0;

    if (!game.is_terminal(&game, (uint64_t)&state, NULL))
    {
        eval_context_destroy(ctx);
        return 2;
    }
    double u0 = game.get_utility(&game, (uint64_t)&state, 0, NULL);
    double u1 = game.get_utility(&game, (uint64_t)&state, 1, NULL);

    /* Quartering: player 0 gets 75, player 1 gets 25 => utilities +/-50 */
    if (!nearly_equal(u0, 50.0, 1e-6) || !nearly_equal(u1, -50.0, 1e-6))
    {
        fprintf(stderr, "Quartering util mismatch: u0=%.6f u1=%.6f\n", u0, u1);
        eval_context_destroy(ctx);
        return 3;
    }

    eval_context_destroy(ctx);
    return 0;
}

static int test_omaha8_no_low(void)
{
    EvalConfig cfg = eval_config_omaha();
    EvalContext *ctx = eval_context_create(&cfg);
    if (!ctx)
        return 1;

    /* Board: Ks Qs Jd 9c 9d */
    int board_cards[] = {
        StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES),
        StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES),
        StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_DIAMONDS),
        StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_CLUBS),
        StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_DIAMONDS)};
    mask_t board = make_mask(board_cards, 5);

    /* Player 0: As Ts 3c 4c -> top flush */
    int p0_cards[] = {
        StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES),
        StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_SPADES),
        StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_CLUBS),
        StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_CLUBS)};
    mask_t h0 = make_mask(p0_cards, 4);

    /* Player 1: Ah Kh 2d 2c -> no flush, pair low but no 8-or-better */
    int p1_cards[] = {
        StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_DIAMONDS),
        StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_CLUBS)};
    mask_t h1 = make_mask(p1_cards, 4);

    cfr_game_t game;
    o8_state_t state;
    o8_build_game(ctx, h0, h1, board, &game, &state);

    state.hist = 0x3u;
    state.to_call = 0.0;
    state.pot = 80.0;

    if (!game.is_terminal(&game, (uint64_t)&state, NULL))
    {
        eval_context_destroy(ctx);
        return 2;
    }
    double u0 = game.get_utility(&game, (uint64_t)&state, 0, NULL);
    double u1 = game.get_utility(&game, (uint64_t)&state, 1, NULL);

    /* Entire pot to player 0 */
    if (!nearly_equal(u0, 80.0, 1e-6) || !nearly_equal(u1, -80.0, 1e-6))
    {
        fprintf(stderr, "No-low util mismatch: u0=%.6f u1=%.6f\n", u0, u1);
        eval_context_destroy(ctx);
        return 3;
    }

    eval_context_destroy(ctx);
    return 0;
}

static int test_razz_showdown(void)
{
    EvalConfig cfg = eval_config_stud();
    cfg.rules = EVAL_RULES_RAZZ;
    EvalContext *ctx = eval_context_create(&cfg);
    if (!ctx)
        return 1;

    /* Player 0: A♣ 2♦ 3♠ 4♥ 5♣ 9♦ K♠ */
    int p0_cards[] = {
        StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_CLUBS),
        StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_DIAMONDS),
        StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_SPADES),
        StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_CLUBS),
        StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_DIAMONDS),
        StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES)};
    mask_t seven0 = make_mask(p0_cards, 7);

    /* Player 1: A♦ 2♣ 3♥ 4♠ 6♣ 9♠ K♣ -> six-low */
    int p1_cards[] = {
        StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS),
        StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_CLUBS),
        StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_SPADES),
        StdDeck_MAKE_CARD(StdDeck_Rank_6, StdDeck_Suit_CLUBS),
        StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_SPADES),
        StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_CLUBS)};
    mask_t seven1 = make_mask(p1_cards, 7);

    cfr_game_t game;
    razz_state_t state;
    razz_build_game(ctx, seven0, seven1, &game, &state);

    state.hist = 0x3u;
    state.to_call = 0.0;
    state.pot = 60.0;

    if (!game.is_terminal(&game, (uint64_t)&state, NULL))
    {
        eval_context_destroy(ctx);
        return 2;
    }
    double u0 = game.get_utility(&game, (uint64_t)&state, 0, NULL);
    double u1 = game.get_utility(&game, (uint64_t)&state, 1, NULL);

    if (!nearly_equal(u0, 60.0, 1e-6) || !nearly_equal(u1, -60.0, 1e-6))
    {
        fprintf(stderr, "Razz util mismatch: u0=%.6f u1=%.6f\n", u0, u1);
        eval_context_destroy(ctx);
        return 3;
    }

    eval_context_destroy(ctx);
    return 0;
}

int main(void)
{
    if (test_omaha8_quarter() != 0)
        return 1;
    if (test_omaha8_no_low() != 0)
        return 2;
    if (test_razz_showdown() != 0)
        return 3;
    printf("CFR Hi/Lo adapter tests passed.\n");
    return 0;
}
