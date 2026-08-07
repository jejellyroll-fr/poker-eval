/*
 * test_mpf_sidepots.c - verifies SOLVER-01: multiway CFR awards must
 * split the pot into side pots based on how much each player could cover.
 */

#include <poker_eval/engine/solvers/cfr/cfr_core.h>
#include <poker_eval/engine/solvers/cfr/multiway_postflop_adapter.h>
#include <poker_eval/core/eval_context.h>
#include <poker_eval/core/modern_cardmask.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond, msg)               \
    do {                               \
        if (!(cond)) {                 \
            fprintf(stderr, "FAIL: %s\n", msg); \
            return 1;                  \
        }                              \
    } while (0)

#define CHECK_CLOSE(a, b, msg)                                                 \
    do {                                                                       \
        if (fabs((a) - (b)) > 1e-6) {                                          \
            fprintf(stderr, "FAIL: %s (got %f, expected %f)\n", msg, (double)(a), (double)(b)); \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static mask_t mk2(int c0, int c1)
{
    mask_t m = mask_set(MASK_EMPTY, c0);
    m = mask_set(m, c1);
    return m;
}

/* Board: 2c 3c 4d 7h Jh (no straight/flush draws resolve below top pair) */
static void set_board(mpf_state_t *st)
{
    static const int board[5] = {
        MODERN_MAKE_CARD(MODERN_RANK_2, MODERN_SUIT_CLUBS),
        MODERN_MAKE_CARD(MODERN_RANK_3, MODERN_SUIT_CLUBS),
        MODERN_MAKE_CARD(MODERN_RANK_4, MODERN_SUIT_DIAMONDS),
        MODERN_MAKE_CARD(MODERN_RANK_7, MODERN_SUIT_HEARTS),
        MODERN_MAKE_CARD(MODERN_RANK_J, MODERN_SUIT_HEARTS)};
    for (int i = 0; i < 5; ++i)
        st->board_cards[i] = board[i];
    st->board_revealed = 5;
}

static void setup_state(mpf_state_t *st, int num_players,
                        const double *invested, const double *pot,
                        mask_t *holes)
{
    memset(st, 0, sizeof(*st));
    st->num_players = num_players;
    st->street = MPF_STREET_SHOWDOWN;
    st->total_hole_cards = 2;
    st->ctx = NULL; /* set by caller */
    st->util_ready = 0;
    for (int i = 0; i < num_players; ++i)
    {
        st->invested[i] = invested[i];
        st->active[i] = 1;
        st->hole[i] = holes[i];
        st->stacks[i] = invested[i];
    }
    if (pot)
        st->pot = *pot;
    else
    {
        st->pot = 0.0;
        for (int i = 0; i < num_players; ++i)
            st->pot += invested[i];
    }
    set_board(st);
}

static int build_cfr_game(const EvalContext *ctx, cfr_game_t *game,
                          mpf_state_t *init_state, const double *invested)
{
    mpf_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.ctx = ctx;
    cfg.rules = MPF_RULE_HOLDEM;
    cfg.num_players = init_state->num_players;
    cfg.button_index = 0;
    cfg.start_street = MPF_STREET_RIVER;
    cfg.sb = 0.5;
    cfg.bb = 1.0;
    for (int i = 0; i < init_state->num_players; ++i)
        cfg.stacks[i] = invested[i];
    memcpy(cfg.hole, init_state->hole, sizeof(init_state->hole[0]) * init_state->num_players);
    for (int i = 0; i < init_state->num_players; ++i)
        cfg.hole_specified[i] = 1;
    cfg.board_card_count = 5;
    for (int i = 0; i < 5; ++i)
        cfg.board_cards[i] = init_state->board_cards[i];

    mpf_state_t built_state;
    if (mpf_build_game(&cfg, game, &built_state) != 0)
        return -1;

    /* Keep the same synthetic showdown state, but make sure the game
       wrapper vtable and EvalContext from the built game are used. */
    double hole0[MPF_MAX_PLAYERS];
    mask_t hole_masks[MPF_MAX_PLAYERS];
    double round_contrib[MPF_MAX_PLAYERS];
    for (int i = 0; i < init_state->num_players; ++i)
    {
        hole_masks[i] = init_state->hole[i];
        hole0[i] = init_state->invested[i];
        round_contrib[i] = init_state->round_contrib[i];
    }
    *init_state = built_state;
    init_state->street = MPF_STREET_SHOWDOWN;
    init_state->util_ready = 0;
    for (int i = 0; i < init_state->num_players; ++i)
    {
        init_state->hole[i] = hole_masks[i];
        init_state->invested[i] = hole0[i];
        init_state->round_contrib[i] = round_contrib[i];
        init_state->active[i] = 1;
        init_state->stacks[i] = init_state->invested[i];
    }
    init_state->pot = 0.0;
    for (int i = 0; i < init_state->num_players; ++i)
        init_state->pot += invested[i];

    return 0;
}

/* Player 0 is the short stack (20) vs two deep stacks (100, 100). */
static int test_short_stack_capped_win(void)
{
    printf("  test_short_stack_capped_win...");

    EvalConfig cfg = eval_config_holdem();
    EvalContext *ctx = eval_context_create(&cfg);
    CHECK(ctx != NULL, "EvalContext create");

    /* P0 (short): AA > P1: KK > P2: QQ. Short stack wins showdown but only
       the main pot (3 x 20 = 60). Deep stacks split the side pot of 160. */
    double invested[3] = {20.0, 100.0, 100.0};
    mask_t holes[3] = {
        mk2(MODERN_MAKE_CARD(MODERN_RANK_A, MODERN_SUIT_SPADES), MODERN_MAKE_CARD(MODERN_RANK_A, MODERN_SUIT_HEARTS)) /* AA */,
        mk2(MODERN_MAKE_CARD(MODERN_RANK_K, MODERN_SUIT_SPADES), MODERN_MAKE_CARD(MODERN_RANK_K, MODERN_SUIT_HEARTS)) /* KK */,
        mk2(MODERN_MAKE_CARD(MODERN_RANK_Q, MODERN_SUIT_SPADES), MODERN_MAKE_CARD(MODERN_RANK_Q, MODERN_SUIT_HEARTS)) /* QQ */};

    mpf_state_t st;
    setup_state(&st, 3, invested, NULL, holes);
    st.hole[0] = holes[0];
    st.hole[1] = holes[1];
    st.hole[2] = holes[2];

    cfr_game_t game;
    CHECK(build_cfr_game(ctx, &game, &st, invested) == 0, "build game");

    double u0 = game.get_utility(&game, (uint64_t)(uintptr_t)&st, 0, NULL);
    double u1 = game.get_utility(&game, (uint64_t)(uintptr_t)&st, 1, NULL);
    double u2 = game.get_utility(&game, (uint64_t)(uintptr_t)&st, 2, NULL);

    /* Zero-sum. */
    CHECK_CLOSE(u0 + u1 + u2, 0.0, "utilities sum to zero");

    /* Short stack: paid 20, can win at most the main pot 60 -> net +40. */
    CHECK_CLOSE(u0, 40.0, "short stack net +40 (main pot 60 - 20)");
    CHECK(u0 <= 40.0, "short stack never wins more than 3x20");

    /* Deep stacks: P1 (KK) takes the 160 side pot -> +60; P2 -> -100. */
    CHECK_CLOSE(u1, 60.0, "P1 side pot net");
    CHECK_CLOSE(u2, -100.0, "P2 folds value");

    mpf_state_cleanup(&st);
    eval_context_destroy(ctx);
    printf(" PASSED\n");
    return 0;
}

/* Equal stacks path must keep the old behavior: whole pot split among winners. */
static int test_equal_stacks_whole_pot(void)
{
    printf("  test_equal_stacks_whole_pot...");

    EvalConfig cfg = eval_config_holdem();
    EvalContext *ctx = eval_context_create(&cfg);
    CHECK(ctx != NULL, "EvalContext create");

    double invested[3] = {100.0, 100.0, 100.0};
    mask_t holes[3] = {
        mk2(MODERN_MAKE_CARD(MODERN_RANK_A, MODERN_SUIT_SPADES), MODERN_MAKE_CARD(MODERN_RANK_A, MODERN_SUIT_HEARTS)) /* AA */,
        mk2(MODERN_MAKE_CARD(MODERN_RANK_K, MODERN_SUIT_SPADES), MODERN_MAKE_CARD(MODERN_RANK_K, MODERN_SUIT_HEARTS)) /* KK */,
        mk2(MODERN_MAKE_CARD(MODERN_RANK_Q, MODERN_SUIT_SPADES), MODERN_MAKE_CARD(MODERN_RANK_Q, MODERN_SUIT_HEARTS)) /* QQ */};

    mpf_state_t st;
    setup_state(&st, 3, invested, NULL, holes);

    cfr_game_t game;
    CHECK(build_cfr_game(ctx, &game, &st, invested) == 0, "build game");

    double u0 = game.get_utility(&game, (uint64_t)(uintptr_t)&st, 0, NULL);
    double u1 = game.get_utility(&game, (uint64_t)(uintptr_t)&st, 1, NULL);
    double u2 = game.get_utility(&game, (uint64_t)(uintptr_t)&st, 2, NULL);

    CHECK_CLOSE(u0 + u1 + u2, 0.0, "zero sum");
    /* One pot of 300: best hand anywhere still wins it all. */
    CHECK_CLOSE(u0, 200.0, "AA wins whole equal pot");
    CHECK_CLOSE(u1, -100.0, "KK loses");
    CHECK_CLOSE(u2, -100.0, "QQ loses");

    mpf_state_cleanup(&st);
    eval_context_destroy(ctx);
    printf(" PASSED\n");
    return 0;
}

/* Folding leaves one active player: they take the whole pot (all side pots). */
static int test_fold_takes_all(void)
{
    printf("  test_fold_takes_all...");

    EvalConfig cfg = eval_config_holdem();
    EvalContext *ctx = eval_context_create(&cfg);
    CHECK(ctx != NULL, "EvalContext create");

    double invested[3] = {20.0, 100.0, 100.0};
    mask_t holes[3] = {
        mk2(MODERN_MAKE_CARD(MODERN_RANK_A, MODERN_SUIT_SPADES), MODERN_MAKE_CARD(MODERN_RANK_A, MODERN_SUIT_HEARTS)),
        mk2(MODERN_MAKE_CARD(MODERN_RANK_K, MODERN_SUIT_SPADES), MODERN_MAKE_CARD(MODERN_RANK_K, MODERN_SUIT_HEARTS)),
        mk2(MODERN_MAKE_CARD(MODERN_RANK_Q, MODERN_SUIT_SPADES), MODERN_MAKE_CARD(MODERN_RANK_Q, MODERN_SUIT_HEARTS))};

    mpf_state_t st;
    setup_state(&st, 3, invested, NULL, holes);

    cfr_game_t game;
    CHECK(build_cfr_game(ctx, &game, &st, invested) == 0, "build game");

    /* Fold: mark players 1 and 2 inactive. The lone survivor takes all. */
    st.active[1] = 0;
    st.active[2] = 0;
    st.util_ready = 0;
    st.street = MPF_STREET_SHOWDOWN;

    double u0 = game.get_utility(&game, (uint64_t)(uintptr_t)&st, 0, NULL);
    double u1 = game.get_utility(&game, (uint64_t)(uintptr_t)&st, 1, NULL);
    double u2 = game.get_utility(&game, (uint64_t)(uintptr_t)&st, 2, NULL);

    /* Survivor gets their stack back plus both opponents' stacks? No: with
       unequal stacks the whole pot (220) goes to the survivor. */
    CHECK_CLOSE(u0 + u1 + u2, 0.0, "zero sum");
    CHECK_CLOSE(u0, 200.0, "survivor takes whole pot");

    mpf_state_cleanup(&st);
    eval_context_destroy(ctx);
    printf(" PASSED\n");
    return 0;
}

int main(void)
{
    printf("Running MPF side-pot tests...\n");
    int failures = 0;
    failures += test_short_stack_capped_win();
    failures += test_equal_stacks_whole_pot();
    failures += test_fold_takes_all();
    printf("\n");
    if (failures == 0)
    {
        printf("All MPF side-pot tests PASSED\n");
        return 0;
    }
    printf("%d test(s) FAILED\n", failures);
    return 1;
}
