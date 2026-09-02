#include <poker_eval/solver/pe_monker.h>

#include <stdio.h>

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); ++failures; } } while (0)

static void check_layout(uint32_t count, enum_game_t game, uint8_t cards)
{
    pe_monker_combo_layout_t layout;
    CHECK(pe_monker_combo_layout_from_count(count, &layout) == PE_MONKER_OK,
          "layout %u was not recognised", count);
    CHECK(layout.game == game && layout.hole_cards == cards &&
              layout.combo_count == count,
          "layout %u decoded as game %d / %u cards / %u combos", count,
          (int)layout.game, layout.hole_cards, layout.combo_count);
}

int main(void)
{
    check_layout(1326u, game_holdem, 2u);
    check_layout(270725u, game_omaha, 4u);
    check_layout(2598960u, game_omaha5, 5u);
    check_layout(20358520u, game_omaha6, 6u);
    CHECK(pe_monker_combo_layout_from_count(1u, NULL) ==
              PE_MONKER_ERR_NULL_ARGUMENT,
          "null layout output was not rejected");
    if (failures)
        return 1;
    puts("test_monker_layout: Hold'em/PLO4/PLO5/PLO6 layouts passed");
    return 0;
}
