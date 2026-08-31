#include <poker_eval/solver/pe_best_response.h>
#include <poker_eval/solver/pe_holdem_chance.h>

#include <math.h>
#include <stdio.h>

static int failures;

#define CHECK(condition, message)                                      \
    do                                                                 \
    {                                                                  \
        if (!(condition))                                              \
        {                                                              \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,  \
                    message);                                         \
            failures++;                                                \
        }                                                              \
    } while (0)

static mask_t card(int rank, int suit)
{
    return mask_set(MASK_EMPTY, MODERN_MAKE_CARD(rank, suit));
}

static void test_exact_river_chance(void)
{
    EvalConfig config = eval_config_holdem();
    EvalContext *context = eval_context_create(&config);
    const mask_t board = card(MODERN_RANK_2, MODERN_SUIT_CLUBS) |
                         card(MODERN_RANK_7, MODERN_SUIT_DIAMONDS) |
                         card(MODERN_RANK_T, MODERN_SUIT_HEARTS) |
                         card(MODERN_RANK_J, MODERN_SUIT_SPADES);
    const mask_t hero = card(MODERN_RANK_A, MODERN_SUIT_SPADES) |
                        card(MODERN_RANK_K, MODERN_SUIT_SPADES);
    const mask_t villain = card(MODERN_RANK_2, MODERN_SUIT_HEARTS) |
                           card(MODERN_RANK_2, MODERN_SUIT_SPADES);
    pe_holdem_chance_game_t game;
    pe_best_response_vector_config_t br_config;
    pe_exploitability_vector_result_t result;
    int unseen = 0;
    int card_index;

    CHECK(context != NULL, "Hold'em context creation");
    if (!context)
        return;
    CHECK(pe_holdem_chance_game_init(&game, context, board, hero, villain,
                                     10.0, 5.0) == 0,
          "chance game initialization");
    if (failures)
    {
        eval_context_destroy(context);
        return;
    }
    for (card_index = 0; card_index < MODERN_DECK_SIZE; ++card_index)
        if (!mask_is_set(board | hero | villain, card_index))
            unseen++;
    CHECK(game.vector.chance_outcome_count(&game.root, game.vector.user) ==
              (uint16_t)unseen && unseen == 44,
          "river chance count is exactly the unseen-card count");

    br_config = pe_best_response_vector_config_default();
    {
        pe_solver_status_t status = pe_exploitability_vector(
            &game.vector, &br_config, &result);
        CHECK(status ==
              PE_SOLVER_OK && result.converged &&
              isfinite(result.policy_value[0]) &&
              isfinite(result.policy_value[1]) &&
              fabs(result.policy_value[0] + result.policy_value[1]) < 1e-9,
          "exact river chance produces finite zero-sum EV");
    }
    CHECK(game.child_count == 0u,
          "river children are released after the traversal");
    pe_holdem_chance_game_destroy(&game);
    eval_context_destroy(context);
}

int main(void)
{
    test_exact_river_chance();
    if (failures)
        fprintf(stderr, "test_pe_holdem_chance: %d failure(s)\n", failures);
    else
        puts("test_pe_holdem_chance: exact river chance passed");
    return failures ? 1 : 0;
}
