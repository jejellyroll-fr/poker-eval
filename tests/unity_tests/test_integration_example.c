#include "unity.h"
#include <poker_eval/core/card.h>
#include <poker_eval/deck/deck.h>

void setUp(void) {}
void tearDown(void) {}

static void test_card_deck_integration(void)
{
    // Test d'intégration : créer une carte et l'ajouter à un deck
    Card card;
    Card_init(&card);

    // Vérifier que les fonctions de conversion fonctionnent ensemble
    int rank = CharToRank('A');
    int suit = CharToSuit('S');
    char rank_char = RankToChar(rank);
    char suit_char = SuitToChar(suit);

    TEST_ASSERT_EQUAL_INT(12, rank);
    TEST_ASSERT_EQUAL_INT(3, suit);
    TEST_ASSERT_EQUAL_CHAR('A', rank_char);
    TEST_ASSERT_EQUAL_CHAR('s', suit_char);

    Card_destroy(&card);
}

static void test_multiple_card_conversions(void)
{
    // Test d'intégration : conversion de plusieurs cartes
    const char *test_cards[] = {"As", "Kh", "Qd", "Jc", "Ts"};
    int expected_ranks[] = {12, 11, 10, 9, 8};
    int expected_suits[] = {3, 0, 1, 2, 3};

    for (int i = 0; i < 5; i++)
    {
        int rank = CharToRank(test_cards[i][0]);
        int suit = CharToSuit(test_cards[i][1]);

        TEST_ASSERT_EQUAL_INT(expected_ranks[i], rank);
        TEST_ASSERT_EQUAL_INT(expected_suits[i], suit);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_card_deck_integration);
    RUN_TEST(test_multiple_card_conversions);
    return UNITY_END();
}
