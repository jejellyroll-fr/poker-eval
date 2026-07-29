#include "unity.h"
#include <poker_eval/core/card.h>
#include <time.h>
#include <stdio.h>

void setUp(void) {}
void tearDown(void) {}

static void test_char_to_rank_performance(void)
{
    clock_t start, end;
    double cpu_time_used;
    const int iterations = 1000000;

    start = clock();

    // Benchmark de CharToRank
    for (int i = 0; i < iterations; i++)
    {
        CharToRank('A');
        CharToRank('K');
        CharToRank('Q');
        CharToRank('J');
        CharToRank('T');
        CharToRank('9');
        CharToRank('8');
        CharToRank('7');
        CharToRank('6');
        CharToRank('5');
        CharToRank('4');
        CharToRank('3');
        CharToRank('2');
    }

    end = clock();
    cpu_time_used = ((double)(end - start)) / CLOCKS_PER_SEC;

    printf("CharToRank benchmark: %d iterations in %f seconds\n",
           iterations * 13, cpu_time_used);

    // Test que le benchmark s'exécute en moins d'une seconde
    TEST_ASSERT_TRUE(cpu_time_used < 1.0);
}

static void test_char_to_suit_performance(void)
{
    clock_t start, end;
    double cpu_time_used;
    const int iterations = 1000000;

    start = clock();

    // Benchmark de CharToSuit
    for (int i = 0; i < iterations; i++)
    {
        CharToSuit('S');
        CharToSuit('C');
        CharToSuit('D');
        CharToSuit('H');
    }

    end = clock();
    cpu_time_used = ((double)(end - start)) / CLOCKS_PER_SEC;

    printf("CharToSuit benchmark: %d iterations in %f seconds\n",
           iterations * 4, cpu_time_used);

    // Test que le benchmark s'exécute en moins d'une seconde
    TEST_ASSERT_TRUE(cpu_time_used < 1.0);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_char_to_rank_performance);
    RUN_TEST(test_char_to_suit_performance);
    return UNITY_END();
}
