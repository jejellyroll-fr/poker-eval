#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <poker_eval/distributions/omaha_distributions.h>
#include <poker_eval/deck/deck_std.h>

// Helper function to count set bits in a card mask
static int count_set_bits(StdDeck_CardMask mask) {
    int count = 0;
    for (int i = 0; i < StdDeck_N_CARDS; i++) {
        if (StdDeck_CardMask_CARD_IS_SET(mask, i)) {
            count++;
        }
    }
    return count;
}

// Test basic initialization and cleanup
static void test_init_and_free(void) {
    printf("Testing OmahaHandList initialization and cleanup...\n");
    
    OmahaHandList handList;
    memset(&handList, 0, sizeof(OmahaHandList));
    
    // Test initialization with default capacity
    assert(OmahaHandList_Init(&handList, 0) == 1);
    assert(handList.capacity == OMAHA_HANDLIST_INITIAL_CAPACITY);
    assert(handList.count == 0);
    assert(handList.hands != NULL);
    
    // Test cleanup
    OmahaHandList_Free(&handList);
    assert(handList.hands == NULL);
    assert(handList.capacity == 0);
    assert(handList.count == 0);
    
    // Test initialization with custom capacity
    assert(OmahaHandList_Init(&handList, 5000) == 1);
    assert(handList.capacity == 5000);
    assert(handList.count == 0);
    assert(handList.hands != NULL);
    
    OmahaHandList_Free(&handList);
    
    printf("✓ Initialization and cleanup tests passed\n");
}

// Test adding hands and automatic resizing
static void test_add_and_resize(void) {
    printf("Testing hand addition and automatic resizing...\n");
    
    OmahaHandList handList;
    memset(&handList, 0, sizeof(OmahaHandList));
    
    // Initialize with small capacity to test resizing
    assert(OmahaHandList_Init(&handList, 10) == 1);
    assert(handList.capacity == 10);
    
    // Add hands beyond initial capacity
    StdDeck_CardMask testHand;
    StdDeck_CardMask_RESET(testHand);
    
    for (int i = 0; i < 25; i++) {
        assert(OmahaHandList_AddHand(&handList, testHand) == 1);
    (void)testHand; // Suppress unused variable warning
    }
    
    assert(handList.count == 25);
    assert(handList.capacity >= 25); // Should have resized
    
    OmahaHandList_Free(&handList);
    
    printf("✓ Addition and resizing tests passed\n");
}

// Test large range generation (beyond old MAX_OMAHA_COMBOS limit)
static void test_large_range(void) {
    printf("Testing large range generation (>10000 hands)...\n");
    
    OmahaHandList handList;
    OmahaHandQuery query;
    StdDeck_CardMask deadCards;
    
    memset(&handList, 0, sizeof(OmahaHandList));
    StdDeck_CardMask_RESET(deadCards);
    
    // Test "xxxx" which generates many combinations
    assert(OmahaHand_Parse("xxxx", &query) == 1);
    
    // Initialize with reasonable capacity for large range
    assert(OmahaHandList_Init(&handList, 50000) == 1);
    
    int result = OmahaHand_Instantiate(&query, deadCards, &handList);
    (void)result; /* Suppress unused variable warning */
    
    printf("Generated %d hands for 'xxxx' query\n", handList.count);
    assert(result > 0);
    assert(handList.count > 10000); // Should exceed old MAX_OMAHA_COMBOS
    
    // Verify we can access all hands
    for (int i = 0; i < handList.count && i < 100; i++) {
        assert(count_set_bits(handList.hands[i]) == 4);
    }
    
    OmahaHandList_Free(&handList);
    
    printf("✓ Large range generation test passed\n");
}

// Test memory efficiency with clear operation
static void test_clear_operation(void) {
    printf("Testing clear operation...\n");
    
    OmahaHandList handList;
    OmahaHandQuery query;
    StdDeck_CardMask deadCards;
    
    memset(&handList, 0, sizeof(OmahaHandList));
    StdDeck_CardMask_RESET(deadCards);
    
    // Parse and generate hands
    assert(OmahaHand_Parse("AAxx", &query) == 1);
    assert(OmahaHandList_Init(&handList, 1000) == 1);
    
    int result1 = OmahaHand_Instantiate(&query, deadCards, &handList);
    (void)result1; /* Suppress unused variable warning */
    assert(result1 > 0);
    int count1 = handList.count;
    (void)count1; /* Suppress unused variable warning */
    int capacity1 = handList.capacity;
    (void)capacity1; /* Suppress unused variable warning */
    
    // Clear and regenerate
    OmahaHandList_Clear(&handList);
    assert(handList.count == 0);
    assert(handList.capacity == capacity1); // Capacity should remain
    
    int result2 = OmahaHand_Instantiate(&query, deadCards, &handList);
    (void)result2; /* Suppress unused variable warning */
    assert(result2 == count1); // Should generate same number of hands
    
    OmahaHandList_Free(&handList);
    
    printf("✓ Clear operation test passed\n");
}

// Test auto-initialization in OmahaHand_Instantiate
static void test_auto_initialization(void) {
    printf("Testing auto-initialization...\n");
    
    OmahaHandList handList;
    OmahaHandQuery query;
    StdDeck_CardMask deadCards;
    
    // Don't initialize, let OmahaHand_Instantiate do it
    memset(&handList, 0, sizeof(OmahaHandList));
    StdDeck_CardMask_RESET(deadCards);
    
    assert(OmahaHand_Parse("KKxx", &query) == 1);
    
    // Should auto-initialize
    int result = OmahaHand_Instantiate(&query, deadCards, &handList);
    (void)result; /* Suppress unused variable warning */
    assert(result > 0);
    assert(handList.hands != NULL);
    assert(handList.capacity > 0);
    assert(handList.count > 0);
    
    OmahaHandList_Free(&handList);
    
    printf("✓ Auto-initialization test passed\n");
}

int main(void) {
    printf("=== Omaha Dynamic Allocation Tests ===\n\n");
    
    test_init_and_free();
    test_add_and_resize();
    test_large_range();
    test_clear_operation();
    test_auto_initialization();
    
    printf("\n✓ All tests passed!\n");
    return 0;
}
