#include <poker_eval/solver/pe_variant.h>

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

static void test_short_deck(void)
{
    pe_variant_profile_t profile;
    CHECK(pe_variant_profile(game_sdholdem, &profile) == 0 &&
              profile.family == PE_VARIANT_SHORT_DECK &&
              profile.deck_cards == 36u && profile.board_cards == 5u &&
              profile.max_private_cards == 2u,
          "short-deck profile is incomplete");
}

static void test_hilo_stud_and_draw(void)
{
    pe_variant_profile_t profile;
    CHECK(pe_variant_profile(game_omaha8, &profile) == 0 &&
              profile.family == PE_VARIANT_HI_LO && profile.has_low &&
              profile.low_qualifier == LOW_QUALIFIER_8,
          "Omaha hi/lo profile is incomplete");
    CHECK(pe_variant_profile(game_7stud, &profile) == 0 &&
              profile.family == PE_VARIANT_STUD &&
              profile.max_private_cards == 7u && profile.board_cards == 0u,
          "Stud profile is incomplete");
    CHECK(pe_variant_profile(game_27_triple_draw, &profile) == 0 &&
              profile.family == PE_VARIANT_DRAW && profile.draw_rounds == 3u &&
              (profile.capabilities & PE_CAP_DRAW_CHANCE) != 0,
          "triple-draw profile is incomplete");
}

int main(void)
{
    test_short_deck();
    test_hilo_stud_and_draw();
    if (failures)
        return 1;
    puts("test_pe_variant: short-deck, hi-lo, stud and draw profiles passed");
    return 0;
}
