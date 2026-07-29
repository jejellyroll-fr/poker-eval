/*
 * test_engine_omaha8_hilo.c - Omaha8 (Hi/Lo) side pots and exactly-two validation
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <poker_eval/engine/game_engine.h>
#include <poker_eval/core/eval_context.h>
#include <poker_eval/core/cardmask_compat.h>
#include <poker_eval/core/low_eval.h>
#include <poker_eval/core/low_qualifier.h>
#include <poker_eval/games/eval_omaha.h>

static inline mask_t bit(int rank, int suit) {
    return (mask_t)1ULL << (rank + 13 * suit);
}

static int run_test(void) {
    EvalConfig cfg = eval_config_omaha();
    cfg.mode = EVAL_MODE_EXACT;
    EvalContext* ctx = eval_context_create(&cfg);
    if (!ctx) { fprintf(stderr, "EvalContext fail\n"); return 1; }

    GameRules rules = game_engine_get_default_rules(GAME_OMAHA_HILO);
    BlindStructure blinds = {0,0,0,0};
    GameEngine* engine = game_engine_create(ctx, &rules);
    if (!engine) { eval_context_destroy(ctx); fprintf(stderr, "Engine fail\n"); return 1; }

    GameState* state = game_state_create(&rules, &blinds, 3);
    if (!state) { game_engine_destroy(engine); eval_context_destroy(ctx); fprintf(stderr, "State fail\n"); return 1; }

    // Board: Ac, 2d, 5h, Kc, Kd (A 2 5 K K)
    mask_t board = 0;
    board |= bit(12,0); // Ac
    board |= bit(0,1);  // 2d
    board |= bit(3,2);  // 5h (rank 5 -> index 3? ranks are 0:2,1:3,2:4,3:5 yes OK)
    board |= bit(11,0); // Kc
    board |= bit(11,1); // Kd
    state->community_cards = mask_t_to_cardmask(board);  // Convert to CardMask
    state->num_community_cards_dealt = 5;

    // P2 (High): As, Kh, Qc, Jd -> can make full house KKKAA using A and K from hole
    mask_t p2 = 0;
    p2 |= bit(12,3); // As
    p2 |= bit(11,2); // Kh
    p2 |= bit(10,0); // Qc
    p2 |= bit(9,1);  // Jd

    // P1 (Low): 3c, 4c, 7s, 8s -> low wheel using 3,4 from hole and A,2,5 from board
    mask_t p1 = 0;
    p1 |= bit(1,0); // 3c
    p1 |= bit(2,0); // 4c
    p1 |= bit(5,3); // 7s
    p1 |= bit(6,3); // 8s

    // P0 (busted): T♣, T♦, 9♣, 9♦ (no relevant high/low better than others)
    mask_t p0 = 0;
    p0 |= bit(8,0); // Tc
    p0 |= bit(8,1); // Td
    p0 |= bit(7,0); // 9c
    p0 |= bit(7,1); // 9d

    // Convert mask_t to CardMask for GameState (which uses legacy CardMask)
    state->players[0].hole_cards = mask_t_to_cardmask(p0);
    state->players[1].hole_cards = mask_t_to_cardmask(p1);
    state->players[2].hole_cards = mask_t_to_cardmask(p2);

    fprintf(stderr, "TEST: board mask=0x%llx ncards=%d\n", (unsigned long long)board, mask_popcount(board));
    fprintf(stderr, "TEST: P0 mask=0x%llx ncards=%d\n", (unsigned long long)p0, mask_popcount(p0));
    fprintf(stderr, "TEST: P1 mask=0x%llx ncards=%d\n", (unsigned long long)p1, mask_popcount(p1));
    fprintf(stderr, "TEST: P2 mask=0x%llx ncards=%d\n", (unsigned long long)p2, mask_popcount(p2));

    // Verify round-trip conversion
    mask_t p1_rt = cardmask_to_mask_t(state->players[1].hole_cards);
    fprintf(stderr, "TEST: P1 round-trip mask=0x%llx ncards=%d (should match above)\n",
            (unsigned long long)p1_rt, mask_popcount(p1_rt));

    // Test StdDeck_Lowball8_EVAL directly on P1's cards
    StdDeck_CardMask p1_all;
    StdDeck_CardMask_OR(p1_all, state->players[1].hole_cards, state->community_cards);
    LowHandVal p1_low_test = pe_eval_low_a5(p1_all);
    bool p1_low_qualifies = pe_low_qualify5(p1_low_test, LOW_QUALIFIER_8);
    fprintf(stderr, "TEST: P1 Low A-5 direct test: lo_val=%u qual=%d (NOTHING=%u)\n",
            (unsigned)p1_low_test, (int)p1_low_qualifies, (unsigned)LowHandVal_NOTHING);

    // Test StdDeck_OmahaHiLow8_EVAL directly
    // Print CardMask contents
    fprintf(stderr, "TEST: P1 hole CardMask: spades=%u hearts=%u diamonds=%u clubs=%u\n",
            (unsigned)StdDeck_CardMask_SPADES(state->players[1].hole_cards),
            (unsigned)StdDeck_CardMask_HEARTS(state->players[1].hole_cards),
            (unsigned)StdDeck_CardMask_DIAMONDS(state->players[1].hole_cards),
            (unsigned)StdDeck_CardMask_CLUBS(state->players[1].hole_cards));
    fprintf(stderr, "TEST: Board CardMask: spades=%u hearts=%u diamonds=%u clubs=%u\n",
            (unsigned)StdDeck_CardMask_SPADES(state->community_cards),
            (unsigned)StdDeck_CardMask_HEARTS(state->community_cards),
            (unsigned)StdDeck_CardMask_DIAMONDS(state->community_cards),
            (unsigned)StdDeck_CardMask_CLUBS(state->community_cards));

    LowHandVal p1_omaha_lo = LowHandVal_NOTHING;
    int ret = StdDeck_OmahaHiLow8_EVAL(state->players[1].hole_cards, state->community_cards, NULL, &p1_omaha_lo);
    bool p1_qual_omaha = (ret == 0) && pe_low_qualify5(p1_omaha_lo, LOW_QUALIFIER_8);
    fprintf(stderr, "TEST: P1 OmahaHiLow8 direct test: ret=%d lo_val=%u qual=%d (NOTHING=%u)\n",
            ret, (unsigned)p1_omaha_lo, (int)p1_qual_omaha, (unsigned)LowHandVal_NOTHING);
    for (int i=0;i<3;i++){ state->players[i].is_active=true; state->players[i].has_folded=false; state->players[i].stack_size=0; }

    // Investments: 100 / 300 / 300 => total 700
    state->players[0].total_investment = 100;
    state->players[1].total_investment = 300;
    state->players[2].total_investment = 300;
    state->pot_size = 700;

    HandResult results[3];
    if (!game_engine_evaluate_showdown(engine, state, results)) {
        fprintf(stderr, "Showdown failed\n");
        return 2;
    }
    if (!game_engine_distribute_pot(engine, state, results)) {
        fprintf(stderr, "Distribute failed\n");
        return 3;
    }

    // Expect split 350/350 between P1 (low) and P2 (high)
    if (results[0].winnings != 0) { fprintf(stderr, "P0 win %lld != 0\n", (long long)results[0].winnings); return 4; }
    if (results[1].winnings != 350) { fprintf(stderr, "P1 win %lld != 350\n", (long long)results[1].winnings); return 5; }
    if (results[2].winnings != 350) { fprintf(stderr, "P2 win %lld != 350\n", (long long)results[2].winnings); return 6; }
    if (!results[1].is_low_winner) { fprintf(stderr, "P1 should be low winner\n"); return 7; }
    if (!state->hand_complete || state->pot_size != 0) { fprintf(stderr, "hand not complete or pot !=0\n"); return 8; }

    game_state_destroy(state);
    game_engine_destroy(engine);
    eval_context_destroy(ctx);
    return 0;
}

int main(void) {
    int rc = run_test();
    if (rc == 0) printf("Omaha8 Hi/Lo side pots test: OK\n");
    return rc;
}
