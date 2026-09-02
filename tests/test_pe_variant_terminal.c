#include <poker_eval/solver/pe_variant_terminal.h>

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
            ++failures;                                                \
        }                                                              \
    } while (0)

static mask_t cards(const int *values, size_t count)
{
    mask_t result = MASK_EMPTY;
    size_t index;
    for (index = 0u; index < count; ++index)
        result = mask_set(result, values[index]);
    return result;
}

static void check_conservation(enum_game_t game, const mask_t *hands,
                               uint8_t players, mask_t board)
{
    double values[8] = {0.0};
    double total = 0.0;
    uint8_t player;
    CHECK(pe_variant_terminal_fixed(game, hands, players, board, 100.0,
                                   values) == 0,
          "variant terminal dispatch failed");
    for (player = 0u; player < players; ++player)
        total += values[player];
    CHECK(fabs(total - 100.0) < 1e-7,
          "terminal values do not conserve the pot");
}

static void test_short_deck(void)
{
    const int first[] = {12 + 3 * 13, 11 + 3 * 13};
    const int second[] = {7, 6 + 13};
    const int board_cards[] = {4, 5 + 13, 8 + 26, 9 + 39, 10};
    const mask_t hands[] = {cards(first, 2u), cards(second, 2u)};
    check_conservation(game_sdholdem, hands, 2u, cards(board_cards, 5u));
}

static void test_hilo_stud_and_draw(void)
{
    const int omaha0[] = {12, 11 + 13, 10 + 26, 9 + 39};
    const int omaha1[] = {8, 7 + 13, 6 + 26, 5 + 39};
    const int board[] = {0, 1 + 13, 2 + 26, 3 + 39, 4};
    const mask_t omaha[] = {cards(omaha0, 4u), cards(omaha1, 4u)};
    const int stud0[] = {12, 11 + 13, 10 + 26, 9 + 39, 8, 7 + 13, 6 + 26};
    const int stud1[] = {5, 4 + 13, 3 + 26, 2 + 39, 1, 0 + 13, 12 + 26};
    const mask_t stud[] = {cards(stud0, 7u), cards(stud1, 7u)};
    const int draw0[] = {12, 11 + 13, 10 + 26, 9 + 39, 8};
    const int draw1[] = {7, 6 + 13, 5 + 26, 4 + 39, 3};
    const mask_t draw[] = {cards(draw0, 5u), cards(draw1, 5u)};

    check_conservation(game_omaha8, omaha, 2u, cards(board, 5u));
    check_conservation(game_7stud, stud, 2u, MASK_EMPTY);
    check_conservation(game_27_triple_draw, draw, 2u, MASK_EMPTY);
}

int main(void)
{
    test_short_deck();
    test_hilo_stud_and_draw();
    if (failures)
        return 1;
    puts("test_pe_variant_terminal: short-deck, hi-lo, stud and draw passed");
    return 0;
}
