/*
 * test_game_names.c - the shared game-name table (#316 follow-up)
 *
 * Front ends used to keep their own spelling tables, which is how Sökö could
 * be registered in the core and still be rejected by one entry point.  These
 * tests pin the aliases both front ends rely on.
 */

#include "unity.h"
#include <string.h>

#include <poker_eval/core/game_names.h>

void setUp(void) {}
void tearDown(void) {}

static void expect_game(const char *name, enum_game_t expected) {
    enum_game_t game = game_NUMGAMES;

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, pe_game_from_name(name, &game), name);
    TEST_ASSERT_EQUAL_INT(expected, game);
}

/* Sökö is spelled several ways; all of them are the same game. */
static void test_soko_spellings(void) {
    expect_game("soko", game_soko);
    expect_game("Soko", game_soko);
    expect_game("SOKO", game_soko);
    /* Explicit UTF-8 bytes: a narrow \u escape uses the compiler's execution
     * charset, which is an ANSI code page on MSVC, not UTF-8. */
    expect_game("s\xc3\xb6k\xc3\xb6", game_soko); /* "sökö" */
    expect_game("S\xc3\x96K\xc3\x96", game_soko); /* "SÖKÖ" */
    expect_game("canadian_stud", game_soko);
    expect_game("canadianstud", game_soko);
    expect_game("Canadian Stud", game_soko);
    expect_game("scandinavianstud", game_soko);
    expect_game("-soko", game_soko);
}

/* The historical pokenum flags stay valid now that they go through the table. */
static void test_pokenum_flags(void) {
    expect_game("-h", game_holdem);
    expect_game("-h8", game_holdem8);
    expect_game("-o", game_omaha);
    expect_game("-o5", game_omaha5);
    expect_game("-o6", game_omaha6);
    expect_game("-o8", game_omaha8);
    expect_game("-o85", game_omaha85);
    expect_game("-7s", game_7stud);
    expect_game("-7s8", game_7stud8);
    expect_game("-7snsq", game_7studnsq);
    expect_game("-r", game_razz);
    expect_game("-5d", game_5draw);
    expect_game("-5d8", game_5draw8);
    expect_game("-5dnsq", game_5drawnsq);
    expect_game("-l", game_lowball);
    expect_game("-l27", game_lowball27);
    expect_game("-sd", game_sdholdem);
    expect_game("-dh", game_doubleflop_holdem);
    expect_game("-pa", game_pineapple);
    expect_game("-pa8", game_pineapple8);
    expect_game("-pacrazy", game_pineapple_crazy);
    expect_game("-palazy", game_pineapple_lazy);
    expect_game("-27td", game_27_triple_draw);
    expect_game("-a5td", game_a5_triple_draw);
    expect_game("-ba", game_badacey);
    expect_game("-be", game_badeucy);
    expect_game("-badugi", game_badugi);
    expect_game("-dm", game_drawmaha);
    expect_game("-fusion", game_fusion);
    expect_game("-cv", game_courchevel);
    expect_game("-cv8", game_courchevel8);
    expect_game("-ir", game_irish);
    expect_game("-mn", game_manila);
    expect_game("-manila", game_manila);
}

/* The Python spellings, including the underscore forms. */
static void test_python_spellings(void) {
    expect_game("5draw_std", game_5draw_std);
    expect_game("5draw8_std", game_5draw8_std);
    expect_game("5drawnsq_std", game_5drawnsq_std);
    expect_game("27_triple_draw", game_27_triple_draw);
    expect_game("a5_triple_draw", game_a5_triple_draw);
    expect_game("badugi_hilo", game_badugi_hilo);
    expect_game("drawmaha_zero", game_drawmaha_zero);
    expect_game("archie", game_archie);
    expect_game("astud", game_astud);
}

/* Every registered game is nameable, and names are unique. */
static void test_every_game_has_one_canonical_name(void) {
    int g, h;

    for (g = 0; g < game_NUMGAMES; g++) {
        const char *canonical = pe_game_canonical_name((enum_game_t)g);
        int count = 0;

        TEST_ASSERT_NOT_NULL_MESSAGE(canonical,
                                     "game without a canonical name");
        for (h = 0; h < game_NUMGAMES; h++) {
            const char *other = pe_game_canonical_name((enum_game_t)h);
            if (other != NULL && strcmp(canonical, other) == 0)
                count++;
        }
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, count, canonical);
    }
}

/* Unknown spellings fail instead of silently picking a game. */
static void test_unknown_names_fail(void) {
    enum_game_t game = game_NUMGAMES;

    TEST_ASSERT_EQUAL_INT(-1, pe_game_from_name("notagame", &game));
    TEST_ASSERT_EQUAL_INT(-1, pe_game_from_name("", &game));
    TEST_ASSERT_EQUAL_INT(-1, pe_game_from_name("-t", &game));
    TEST_ASSERT_EQUAL_INT(-1, pe_game_from_name(NULL, &game));
    TEST_ASSERT_EQUAL_INT(-1, pe_game_from_name("soko", NULL));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_soko_spellings);
    RUN_TEST(test_pokenum_flags);
    RUN_TEST(test_python_spellings);
    RUN_TEST(test_every_game_has_one_canonical_name);
    RUN_TEST(test_unknown_names_fail);
    return UNITY_END();
}
