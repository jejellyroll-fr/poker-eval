/*
 * test_omaha5_6.c - Unit tests for PLO5 and PLO6 combination generation
 */

#include <poker_eval/core/omaha_combinations.h>
#include <poker_eval/core/modern_cardmask.h>
#include <unity.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

void test_plo5_combinations_count(void)
{
    /* PLO5: 5 hole cards, 5 board cards */
    /* C(5,2) = 10 hole combinations */
    /* C(5,3) = 10 board combinations */
    /* Total = 10 * 10 = 100 combinations */
    
    mask_t hole = string_to_mask("As Ks Qs Js Ts");
    mask_t board = string_to_mask("2c 3c 4c 5c 6c");
    
    omaha_config_t config = omaha_config_plo5(hole, board, MASK_EMPTY);
    omaha_generator_t *gen = omaha_generator_create(&config);
    
    TEST_ASSERT_NOT_NULL(gen);
    TEST_ASSERT_EQUAL_UINT64(100, omaha_generator_total(gen));
    
    uint64_t count = 0;
    mask_t combo;
    while (omaha_generator_next(gen, &combo)) {
        count++;
        TEST_ASSERT_EQUAL_INT(5, mask_popcount(combo));
        
        /* Verify 2 hole cards + 3 board cards */
        mask_t hole_part = mask_intersect(combo, hole);
        mask_t board_part = mask_intersect(combo, board);
        TEST_ASSERT_EQUAL_INT(2, mask_popcount(hole_part));
        TEST_ASSERT_EQUAL_INT(3, mask_popcount(board_part));
    }
    
    TEST_ASSERT_EQUAL_UINT64(100, count);
    omaha_generator_destroy(gen);
}

void test_plo6_combinations_count(void)
{
    /* PLO6: 6 hole cards, 5 board cards */
    /* C(6,2) = 15 hole combinations */
    /* C(5,3) = 10 board combinations */
    /* Total = 15 * 10 = 150 combinations */
    
    mask_t hole = string_to_mask("As Ks Qs Js Ts 9s");
    mask_t board = string_to_mask("2c 3c 4c 5c 6c");
    
    omaha_config_t config = omaha_config_plo6(hole, board, MASK_EMPTY);
    omaha_generator_t *gen = omaha_generator_create(&config);
    
    TEST_ASSERT_NOT_NULL(gen);
    TEST_ASSERT_EQUAL_UINT64(150, omaha_generator_total(gen));
    
    uint64_t count = 0;
    mask_t combo;
    while (omaha_generator_next(gen, &combo)) {
        count++;
        TEST_ASSERT_EQUAL_INT(5, mask_popcount(combo));
        
        /* Verify 2 hole cards + 3 board cards */
        mask_t hole_part = mask_intersect(combo, hole);
        mask_t board_part = mask_intersect(combo, board);
        TEST_ASSERT_EQUAL_INT(2, mask_popcount(hole_part));
        TEST_ASSERT_EQUAL_INT(3, mask_popcount(board_part));
    }
    
    TEST_ASSERT_EQUAL_UINT64(150, count);
    omaha_generator_destroy(gen);
}

void test_plo5_validation(void)
{
    /* Test invalid hole card count */
    mask_t hole4 = string_to_mask("As Ks Qs Js"); /* 4 cards */
    mask_t board = string_to_mask("2c 3c 4c 5c 6c");
    
    /* Should fail validation if we try to pass 4 cards to PLO5 config? 
       Actually the config helper just passes it through, but validation checks count.
       Wait, validation allows 4, 5, or 6. So 4 is valid for the generator, 
       but semantically it's PLO4. */
       
    omaha_config_t config = omaha_config_plo5(hole4, board, MASK_EMPTY);
    TEST_ASSERT_TRUE(omaha_validate_config(&config)); /* 4 is valid count */
    
    /* Test 3 cards (invalid) */
    mask_t hole3 = string_to_mask("As Ks Qs");
    config.constraints.hole_cards = hole3;
    TEST_ASSERT_FALSE(omaha_validate_config(&config));
    
    /* Test 7 cards (invalid) */
    mask_t hole7 = string_to_mask("As Ks Qs Js Ts 9s 8s");
    config.constraints.hole_cards = hole7;
    TEST_ASSERT_FALSE(omaha_validate_config(&config));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_plo5_combinations_count);
    RUN_TEST(test_plo6_combinations_count);
    RUN_TEST(test_plo5_validation);
    return UNITY_END();
}
