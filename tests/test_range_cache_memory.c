
static int test_memory_efficiency(void) {
    printf("\n--- Testing Memory Efficiency ---\n");

    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);

    /* Test small range */
    arp_range_t small_range;
    TEST_ASSERT(ARP_GetTopPercentage(0.005f, game_holdem, dead, &small_range),
                "Should parse AA");

    printf("   AA (approx): count=%zu, capacity=%zu, efficiency=%.1f%%\n",
           small_range.count, small_range.capacity,
           100.0 * small_range.count / small_range.capacity);

    /* Efficiency should be decent, at least not wildly allocated */
    // With dynamic allocation, we expect capacity to be reasonably close to count
    // Default was 256, for AA (6 combos) that was 2.3% efficiency
    // Now it should be better if estimation works, or at least start smaller (16)

    ARP_FreeRange(&small_range);

    /* Test large range */
    arp_range_t large_range;
    TEST_ASSERT(ARP_GetTopPercentage(0.50f, game_holdem, dead, &large_range),
                "Should parse Top 50%");

    printf("   Top 50%%: count=%zu, capacity=%zu, efficiency=%.1f%%\n",
           large_range.count, large_range.capacity,
           100.0 * large_range.count / large_range.capacity);

    TEST_ASSERT(large_range.count * 2 >= large_range.capacity,
                "Large range should have good memory efficiency");

    ARP_FreeRange(&large_range);

    TEST_PASS("Memory efficiency tests");
    return 1;
}
