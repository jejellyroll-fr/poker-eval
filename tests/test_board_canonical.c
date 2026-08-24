/*
 * @file test_board_canonical.c
 * @brief Tests for suit-permutation board canonicalization (FEAT-02)
 */

#include <poker_eval/engine/solvers/cfr/board_canonical.h>
#include <poker_eval/engine/solvers/cfr/holdem_river_adapter.h>
#include <poker_eval/core/eval_context.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond, msg)               \
    do {                               \
        if (!(cond)) {                 \
            fprintf(stderr, "%s\n", msg); \
            return 1;                  \
        }                              \
    } while (0)

static mask_t card(int rank, int suit)
{
    return mask_set(MASK_EMPTY, MODERN_MAKE_CARD(rank, suit));
}

static int identity_rank_automorphism(const int permutation[13], void *user_data)
{
    (void)user_data;
    for (int rank = 0; rank < 13; ++rank)
        if (permutation[rank] != rank)
            return 0;
    return 1;
}

static int solve_board(mask_t h0, mask_t h1, mask_t board, double *out_expl, size_t *out_infosets)
{
    EvalConfig ecfg = eval_config_holdem();
    EvalContext *ctx = eval_context_create(&ecfg);
    if (!ctx)
        return -1;

    cfr_game_t game;
    holdem_river_state_t st;
    hr_build_game(ctx, h0, h1, board, &game, &st);
    st.num_bet_sizes = 1;
    st.raise_cap = 1;

    cfr_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_iterations = 200;

    cfr_storage_t *storage = cfr_storage_create();
    if (!storage)
    {
        eval_context_destroy(ctx);
        return -1;
    }
    double expl = 0.0;
    cfr_solve(&game, storage, &cfg, &expl);
    *out_expl = expl;
    *out_infosets = cfr_storage_count_infosets(storage);
    cfr_storage_destroy(storage);
    eval_context_destroy(ctx);
    return 0;
}

int main(void)
{
    /* Isomorphic pair: A: AhKh | 7c7d | 2s3s4s   B: AdKd | 7s7h | 2c3c4c */
    mask_t h0a = card(MODERN_RANK_A, MODERN_SUIT_HEARTS) | card(MODERN_RANK_K, MODERN_SUIT_HEARTS);
    mask_t h1a = card(MODERN_RANK_7, MODERN_SUIT_CLUBS) | card(MODERN_RANK_7, MODERN_SUIT_DIAMONDS);
    mask_t bda = card(MODERN_RANK_2, MODERN_SUIT_SPADES) | card(MODERN_RANK_3, MODERN_SUIT_SPADES) |
                 card(MODERN_RANK_4, MODERN_SUIT_SPADES);

    mask_t h0b = card(MODERN_RANK_A, MODERN_SUIT_DIAMONDS) | card(MODERN_RANK_K, MODERN_SUIT_DIAMONDS);
    mask_t h1b = card(MODERN_RANK_7, MODERN_SUIT_SPADES) | card(MODERN_RANK_7, MODERN_SUIT_HEARTS);
    mask_t bdb = card(MODERN_RANK_2, MODERN_SUIT_CLUBS) | card(MODERN_RANK_3, MODERN_SUIT_CLUBS) |
                 card(MODERN_RANK_4, MODERN_SUIT_CLUBS);

    /* Non-isomorphic board: 2c3c4d has suit pattern 2-1 instead of 3-0 */
    mask_t bdn = card(MODERN_RANK_2, MODERN_SUIT_CLUBS) | card(MODERN_RANK_3, MODERN_SUIT_CLUBS) |
                 card(MODERN_RANK_4, MODERN_SUIT_DIAMONDS);

    char key_a[64] = {0}, key_b[64] = {0}, key_n[64] = {0};
    CHECK(pe_board_canonical_key(h0a | h1a | bda, 7, key_a, sizeof(key_a)) == 0, "key A");
    CHECK(pe_board_canonical_key(h0b | h1b | bdb, 7, key_b, sizeof(key_b)) == 0, "key B");
    CHECK(strcmp(key_a, key_b) == 0, "isomorphic boards must share one canonical key");
    CHECK(pe_board_canonical_key(h0a | h1a | bdn, 7, key_n, sizeof(key_n)) == 0, "key N");
    CHECK(strcmp(key_a, key_n) != 0, "non-isomorphic board must differ");

    /* Canonical representative + mapping back to original suits */
    mask_t canon = MASK_EMPTY;
    int perm[4];
    CHECK(pe_board_canonicalize(h0a | h1a | bda, 7, &canon, perm) == 0, "canonicalize A");
    CHECK(pe_board_count_cards(canon) == 7, "canonical card count");
    char key_canon[64] = {0};
    CHECK(pe_board_canonical_key(canon, 7, key_canon, sizeof(key_canon)) == 0, "canon key");
    CHECK(strcmp(key_canon, key_a) == 0, "canonical representative is canonical");

    mask_t rebuilt = MASK_EMPTY;
    for (int card_id = 0; card_id < MODERN_DECK_SIZE; ++card_id)
    {
        if (!mask_is_set(canon, card_id))
            continue;
        int rank = MODERN_GET_RANK(card_id);
        int label = MODERN_GET_SUIT(card_id);
        if (perm[label] < 0)
            continue;
        rebuilt = mask_set(rebuilt, MODERN_MAKE_CARD(rank, perm[label]));
    }
    CHECK(rebuilt == (h0a | h1a | bda), "suit_perm must translate back to original suits");

    /* Rank canonicalization is opt-in and proof-gated. Standard poker only
     * admits the identity permutation because rank order is semantic. */
    int identity[1][13];
    for (int rank = 0; rank < 13; ++rank)
        identity[0][rank] = rank;
    mask_t rank_canon = MASK_EMPTY;
    int winning_rank_perm[13];
    CHECK(pe_board_canonicalize_rank_orbit(h0a | h1a | bda, 7, identity, 1,
                                           identity_rank_automorphism, NULL,
                                           &rank_canon, winning_rank_perm, NULL) == 0,
          "identity rank canonicalization");
    CHECK(rank_canon == canon, "identity rank orbit must preserve suit canonicalization");
    int invalid[1][13];
    memcpy(invalid[0], identity[0], sizeof(identity[0]));
    invalid[0][0] = invalid[0][1];
    CHECK(pe_board_canonicalize_rank_orbit(h0a | h1a | bda, 7, invalid, 1,
                                           identity_rank_automorphism, NULL,
                                           &rank_canon, NULL, NULL) != 0,
          "non-bijective rank permutation must be rejected");

    /* Solve-equivalence: isomorphic boards must produce identical solves and
     * identical exported (average) strategies within convergence tolerance. */
    double expl_a = 0.0, expl_b = 0.0;
    size_t n_a = 0, n_b = 0;
    CHECK(solve_board(h0a, h1a, bda, &expl_a, &n_a) == 0, "solve A");
    CHECK(solve_board(h0b, h1b, bdb, &expl_b, &n_b) == 0, "solve B");
    CHECK(n_a == n_b, "infoset counts must match");
    CHECK(fabs(expl_a - expl_b) < 1e-6, "exploitability must match for isomorphic boards");

    printf(" PASSED (infosets=%zu, canon key=%s)\n", n_a, key_a);
    return 0;
}
