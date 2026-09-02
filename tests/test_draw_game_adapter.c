#include <assert.h>
#include <stdio.h>

#include <poker_eval/engine/solvers/cfr/draw_game_adapter.h>

static double utility(mask_t p0, mask_t p1, int player, void *user)
{
    (void)user;
    int p0_cards = mask_popcount(p0);
    int p1_cards = mask_popcount(p1);
    if (player == 0) return (double)(p0_cards >= p1_cards ? 1 : -1);
    return (double)(p1_cards >= p0_cards ? 1 : -1);
}

int main(void)
{
    const int p0_cards[] = {
        MODERN_MAKE_CARD(MODERN_RANK_A, MODERN_SUIT_CLUBS),
        MODERN_MAKE_CARD(MODERN_RANK_7, MODERN_SUIT_DIAMONDS),
        MODERN_MAKE_CARD(MODERN_RANK_4, MODERN_SUIT_HEARTS),
        MODERN_MAKE_CARD(MODERN_RANK_2, MODERN_SUIT_SPADES)
    };
    const int p1_cards[] = {
        MODERN_MAKE_CARD(MODERN_RANK_K, MODERN_SUIT_CLUBS),
        MODERN_MAKE_CARD(MODERN_RANK_8, MODERN_SUIT_DIAMONDS),
        MODERN_MAKE_CARD(MODERN_RANK_5, MODERN_SUIT_HEARTS),
        MODERN_MAKE_CARD(MODERN_RANK_3, MODERN_SUIT_SPADES)
    };
    pe_draw_cfr_config_t config = {0};
    cfr_game_t game;
    pe_draw_cfr_state_t state;
    cfr_config_t solve_config = {0};
    cfr_storage_t *storage;
    double exploitability;
    for (int i = 0; i < 4; ++i) {
        config.player0_hand = mask_set(config.player0_hand, p0_cards[i]);
        config.player1_hand = mask_set(config.player1_hand, p1_cards[i]);
    }
    config.variant = PE_DRAW_BADUGI;
    config.action_count[0] = 2;
    config.action_count[1] = 1;
    config.discard_actions[0][0] = MASK_EMPTY;
    /* Keep the integration test small; replacement enumeration itself is
     * covered exhaustively by test_draw_chance. */
    config.discard_actions[0][1] = MASK_EMPTY;
    config.discard_actions[1][0] = MASK_EMPTY;
    config.terminal_value = utility;
    assert(pe_draw_cfr_build_game(&config, &game, &state) == 0);
    assert(game.is_chance(&game, (uint64_t)(uintptr_t)&state, game.game_data) == 0);
    solve_config.max_iterations = 2;
    solve_config.max_depth = 16;
    storage = cfr_storage_create();
    assert(storage != NULL);
    exploitability = cfr_solve(&game, storage, &solve_config, NULL);
    assert(exploitability >= 0.0);
    cfr_storage_destroy(storage);
    puts("Draw CFR adapter tests passed");
    return 0;
}
