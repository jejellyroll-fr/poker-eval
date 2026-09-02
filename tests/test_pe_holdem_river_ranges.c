/*
 * test_pe_holdem_river_ranges.c - VEC-10 Hold'em river range validation.
 *
 * The solver lifecycle is not the consumer of this port yet, so this test
 * validates the exported river policy value at the domain boundary: a wide
 * two-card range is evaluated by the sorted vector showdown and by an
 * independent exhaustive enumeration. Both values must agree at the root.
 */

#include <poker_eval/core/eval_context.h>
#include <poker_eval/solver/pe_showdown.h>
#include <poker_eval/solver/pe_solver_plan.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#define RIVER_RANGE_SIZE 132u

static int failures;

#define CHECK(cond, ...)                                                   \
    do                                                                     \
    {                                                                      \
        if (!(cond))                                                       \
        {                                                                  \
            fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__);       \
            fprintf(stderr, __VA_ARGS__);                                 \
            fputc('\n', stderr);                                           \
            failures++;                                                    \
        }                                                                  \
    } while (0)

static mask_t card(int rank, int suit)
{
    return mask_set(MASK_EMPTY, MODERN_MAKE_CARD(rank, suit));
}

static mask_t river_board(void)
{
    return card(MODERN_RANK_2, MODERN_SUIT_CLUBS) |
           card(MODERN_RANK_7, MODERN_SUIT_DIAMONDS) |
           card(MODERN_RANK_T, MODERN_SUIT_HEARTS) |
           card(MODERN_RANK_J, MODERN_SUIT_SPADES) |
           card(MODERN_RANK_Q, MODERN_SUIT_CLUBS);
}

static size_t build_range(const EvalContext *ctx, mask_t board,
                          mask_t *masks, int64_t *strengths)
{
    size_t count = 0;
    int first;

    for (first = 0; first < MODERN_DECK_SIZE && count < RIVER_RANGE_SIZE;
         ++first)
    {
        int second;
        if (mask_is_set(board, first))
            continue;
        for (second = first + 1;
             second < MODERN_DECK_SIZE && count < RIVER_RANGE_SIZE;
             ++second)
        {
            mask_t hand;
            if (mask_is_set(board, second))
                continue;
            hand = mask_set(mask_set(MASK_EMPTY, first), second);
            masks[count] = hand;
            strengths[count] = (int64_t)pe_eval_7c(ctx, board | hand);
            ++count;
        }
    }
    return count;
}

static double independent_value(mask_t hero, int64_t hero_strength,
                                const mask_t *opponents,
                                const int64_t *opponent_strength,
                                size_t opponent_count, double pot)
{
    double value = 0.0;
    size_t opponent;

    for (opponent = 0; opponent < opponent_count; ++opponent)
    {
        double share;
        if ((hero & opponents[opponent]) != 0)
            continue;
        if (hero_strength > opponent_strength[opponent])
            share = 1.0;
        else if (hero_strength == opponent_strength[opponent])
            share = 0.5;
        else
            share = 0.0;
        value += share / (double)opponent_count;
    }
    return value * pot;
}

static void test_cfr_vector_preset(void)
{
    pe_algorithm_config_t algorithm = {0};

    CHECK(pe_preset_expand(PE_PRESET_CFR_VECTOR, &algorithm) == 0,
          "cfr-vector preset did not expand");
    CHECK(algorithm.traversal == PE_TRAVERSAL_FULL_VECTOR,
          "cfr-vector traversal is not FULL_VECTOR");
    CHECK(algorithm.regret == PE_REGRET_VANILLA,
          "cfr-vector regret mode is not VANILLA");
    CHECK(algorithm.policy == PE_POLICY_REGRET_MATCHING,
          "cfr-vector policy is not regret matching");
    CHECK(algorithm.averaging == PE_AVG_UNIFORM,
          "cfr-vector averaging is not UNIFORM");
}

static void test_wide_river_ranges_match_enumeration(void)
{
    EvalConfig config = eval_config_holdem();
    EvalContext *ctx = eval_context_create(&config);
    const mask_t board = river_board();
    mask_t hero_masks[RIVER_RANGE_SIZE];
    mask_t opponent_masks[RIVER_RANGE_SIZE];
    int64_t hero_strength[RIVER_RANGE_SIZE];
    int64_t opponent_strength[RIVER_RANGE_SIZE];
    double opponent_reach[RIVER_RANGE_SIZE];
    pe_value_vec_t vector_values = {0};
    pe_showdown_path_t path = PE_SHOWDOWN_PATH_NONE;
    const double pot = 3.0;
    double vector_root = 0.0;
    double independent_root = 0.0;
    size_t hero_count;
    size_t opponent_count;
    size_t hero;

    CHECK(ctx != NULL, "Hold'em evaluation context creation failed");
    if (!ctx)
        return;
    hero_count = build_range(ctx, board, hero_masks, hero_strength);
    opponent_count = build_range(ctx, board, opponent_masks,
                                 opponent_strength);
    CHECK(hero_count == RIVER_RANGE_SIZE &&
              opponent_count == RIVER_RANGE_SIZE,
          "expected two ranges of %u combos, got %zu and %zu",
          RIVER_RANGE_SIZE, hero_count, opponent_count);
    if (hero_count != RIVER_RANGE_SIZE || opponent_count != RIVER_RANGE_SIZE)
    {
        eval_context_destroy(ctx);
        return;
    }
    for (hero = 0; hero < opponent_count; ++hero)
        opponent_reach[hero] = 1.0 / (double)opponent_count;

    CHECK(pe_vec_alloc(&vector_values, hero_count) == PE_SOLVER_OK,
          "vector output allocation failed");
    if (!vector_values.v)
    {
        eval_context_destroy(ctx);
        return;
    }
    CHECK(pe_showdown_vector(hero_masks, hero_strength, hero_count,
                             opponent_masks, opponent_strength,
                             opponent_reach, opponent_count, board, pot,
                             &vector_values, &path) == PE_SOLVER_OK,
          "vector river showdown failed");
    CHECK(path == PE_SHOWDOWN_PATH_SORTED,
          "wide two-card ranges did not use the sorted vector path");

    for (hero = 0; hero < hero_count; ++hero)
    {
        double expected = independent_value(
            hero_masks[hero], hero_strength[hero], opponent_masks,
            opponent_strength, opponent_count, pot);
        vector_root += vector_values.v[hero];
        independent_root += expected;
        CHECK(fabs(vector_values.v[hero] - expected) <= 1e-6,
              "hero combo %zu value %.17g differs from enumeration %.17g",
              hero, vector_values.v[hero], expected);
    }
    vector_root /= (double)hero_count;
    independent_root /= (double)hero_count;
    CHECK(fabs(vector_root - independent_root) <= 1e-6,
          "root value %.17g differs from enumeration %.17g", vector_root,
          independent_root);

    pe_vec_free(&vector_values);
    eval_context_destroy(ctx);
}

int main(void)
{
    test_cfr_vector_preset();
    test_wide_river_ranges_match_enumeration();
    if (failures != 0)
    {
        fprintf(stderr, "test_pe_holdem_river_ranges: %d failure(s)\n",
                failures);
        return 1;
    }
    printf("test_pe_holdem_river_ranges: %u-combo vector EV matches enumeration\n",
           RIVER_RANGE_SIZE);
    return 0;
}
