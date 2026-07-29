/*
 * test_engine_sidepots_hilo.c - Side pots and Hi/Lo distribution test
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <poker_eval/engine/game_engine.h>
#include <poker_eval/core/eval_context.h>
#include <poker_eval/core/cardmask_compat.h>

static inline mask_t bit(int rank, int suit) {
    return (mask_t)1ULL << (rank + 13 * suit);
}

static int run_test(void) {
    // Eval context (Stud)
    EvalConfig cfg = eval_config_stud();
    cfg.mode = EVAL_MODE_EXACT;
    EvalContext* ctx = eval_context_create(&cfg);
    if (!ctx) { fprintf(stderr, "Failed to create EvalContext\n"); return 1; }

    // Game rules: Stud8 (Hi/Lo)
    GameRules rules = game_engine_get_default_rules(GAME_SEVEN_STUD_HILO);
    BlindStructure blinds = {0, 0, 0, 0};
    GameEngine* engine = game_engine_create(ctx, &rules);
    if (!engine) { fprintf(stderr, "Failed to create GameEngine\n"); eval_context_destroy(ctx); return 1; }

    GameState* state = game_state_create(&rules, &blinds, 3);
    if (!state) { fprintf(stderr, "Failed to create GameState\n"); game_engine_destroy(engine); eval_context_destroy(ctx); return 1; }

    // Compose 7-card hands (Stud):
    // Player 0: A A A K K 2 3 (high strong full house, no qualifying low)
    //  As, Ah, Ac, Ks, Kh, 2c, 3c
    mask_t p0 = 0;
    p0 |= bit(12, 3); // As
    p0 |= bit(12, 2); // Ah
    p0 |= bit(12, 0); // Ac
    p0 |= bit(11, 3); // Ks
    p0 |= bit(11, 2); // Kh
    p0 |= bit(0, 0);  // 2c
    p0 |= bit(1, 0);  // 3c

    // Player 1: strong low (2♦,3♥,4♦,5♥,6♦,7♥,8♦)
    mask_t p1 = 0;
    p1 |= bit(0, 1);  // 2d
    p1 |= bit(1, 2);  // 3h
    p1 |= bit(2, 1);  // 4d
    p1 |= bit(3, 2);  // 5h
    p1 |= bit(4, 1);  // 6d
    p1 |= bit(5, 2);  // 7h
    p1 |= bit(6, 1);  // 8d

    // Player 2: four queens (high best), plus kickers 9♦, J♦
    mask_t p2 = 0;
    p2 |= bit(10, 3); // Qs
    p2 |= bit(10, 2); // Qh
    p2 |= bit(10, 1); // Qd
    p2 |= bit(10, 0); // Qc
    p2 |= bit(7, 1);  // 9d
    p2 |= bit(9, 1);  // Jd
    p2 |= bit(2, 0);  // 4c (filler, distinct)

    // Convert mask_t to CardMask for GameState (which uses legacy CardMask)
    state->players[0].hole_cards = mask_t_to_cardmask(p0);
    state->players[1].hole_cards = mask_t_to_cardmask(p1);
    state->players[2].hole_cards = mask_t_to_cardmask(p2);

    fprintf(stderr, "TEST: P0 mask=0x%llx ncards=%d\n", (unsigned long long)p0, mask_popcount(p0));
    fprintf(stderr, "TEST: P1 mask=0x%llx ncards=%d\n", (unsigned long long)p1, mask_popcount(p1));
    fprintf(stderr, "TEST: P2 mask=0x%llx ncards=%d\n", (unsigned long long)p2, mask_popcount(p2));

    // All active
    for (int i = 0; i < 3; i++) {
        state->players[i].is_active = true;
        state->players[i].has_folded = false;
        state->players[i].is_all_in = false;
        state->players[i].stack_size = 0; // start at 0, receive winnings
    }

    // Investments to form side pots: P0=100, P1=300, P2=300
    state->players[0].total_investment = 100;
    state->players[1].total_investment = 300;
    state->players[2].total_investment = 300;
    state->pot_size = 700; // informational; distribution recomputes per-level

    HandResult results[3];
    if (!game_engine_evaluate_showdown(engine, state, results)) {
        fprintf(stderr, "Showdown evaluation failed\n");
        game_state_destroy(state); game_engine_destroy(engine); eval_context_destroy(ctx); return 1;
    }

    if (!game_engine_distribute_pot(engine, state, results)) {
        fprintf(stderr, "Distribution failed\n");
        game_state_destroy(state); game_engine_destroy(engine); eval_context_destroy(ctx); return 1;
    }

    // Expected: pot 300 (all three): split 150 hi (P2), 150 lo (P1)
    //           pot 400 (P1 & P2): split 200 hi (P2), 200 lo (P1)
    // Totals: P0=0, P1=350, P2=350
    if (results[0].winnings != 0) {
        fprintf(stderr, "P0 winnings mismatch: got %lld, expected 0\n", (long long)results[0].winnings);
        return 2;
    }
    if (results[1].winnings != 350) {
        fprintf(stderr, "P1 winnings mismatch: got %lld, expected 350\n", (long long)results[1].winnings);
        return 3;
    }
    if (results[2].winnings != 350) {
        fprintf(stderr, "P2 winnings mismatch: got %lld, expected 350\n", (long long)results[2].winnings);
        return 4;
    }
    if (!results[1].is_low_winner) {
        fprintf(stderr, "P1 should be low winner\n");
        return 5;
    }
    if (!state->hand_complete || state->pot_size != 0) {
        fprintf(stderr, "Hand not complete or pot not zero (complete=%d pot=%lld)\n", (int)state->hand_complete, (long long)state->pot_size);
        return 6;
    }

    game_state_destroy(state);
    game_engine_destroy(engine);
    eval_context_destroy(ctx);
    return 0;
}

int main(void) {
    int rc = run_test();
    if (rc == 0) {
        printf("Side pots + Hi/Lo distribution test: OK\n");
    }
    return rc;
}

