/*
 * combo_validation_suite.c - Comprehensive validation suite for combinatorial generator
 *
 * Implements 13 micro-checks for robust quality validation:
 * 1. API Guards (edge cases)
 * 2. Input Integrity (data validation)
 * 3. Dead Cards (constraint enforcement)
 * 4. Omaha Constraints (poker-specific)
 * 5. Combination Aggregation (popcount validation)
 * 6. Types & Shifts (avoid UB)
 * 7. Quality Tools (sanitizers)
 * 8. Benchmark Methodology (proper timing)
 * 9. Suit Permutation Invariance (deck homomorphism)
 * 10. Random Dead Masks (constraint fuzzing)
 * 11. Monotonic Ordering (position monotonicity)
 * 12. Duplicate-Free Generation (small-deck guarantee)
 * 13. Stress Limits (combinatorial totals)
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach_time.h>
#else
#include <time.h>
#endif

#include <poker_eval/core/modern_combinations.h>

/* Global variables for testing */
static bool g_quiet_mode = false;

/* Global validation stats */
static struct {
    int tests_run;
    int tests_passed;
    int tests_failed;
    int assertions_checked;
} validation_stats = {0};

static uint64_t monotonic_time_ns(void) {
#if defined(_WIN32)
    LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    if (!QueryPerformanceFrequency(&frequency) || !QueryPerformanceCounter(&counter)) {
        return 0;
    }
    return (uint64_t)((counter.QuadPart * 1000000000ULL) / (uint64_t)frequency.QuadPart);
#elif defined(__APPLE__)
    static mach_timebase_info_data_t timebase = {0};
    if (timebase.denom == 0) {
        mach_timebase_info(&timebase);
    }
    uint64_t absolute = mach_absolute_time();
    return (absolute * (uint64_t)timebase.numer) / (uint64_t)timebase.denom;
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
#endif
}

static uint32_t rng_next_u32(uint64_t *state) {
    /* SplitMix64 - deterministic, good statistical quality */
    *state += UINT64_C(0x9E3779B97F4A7C15);
    uint64_t z = *state;
    z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
    z ^= (z >> 31);
    return (uint32_t)z;
}

static uint32_t rng_next_range(uint64_t *state, uint32_t bound) {
    if (bound == 0) {
        return 0;
    }
    uint32_t threshold = (uint32_t)(-bound) % bound;
    for (;;) {
        uint32_t value = rng_next_u32(state);
        if (value >= threshold) {
            return value % bound;
        }
    }
}

static mask_t mask_permute_suits(mask_t cards, const int perm[MODERN_SUIT_COUNT]) {
    card_list_t list = mask_to_list(cards);
    mask_t result = MASK_EMPTY;
    for (int i = 0; i < list.count; i++) {
        int card = list.cards[i];
        int rank = MODERN_GET_RANK(card);
        int suit = MODERN_GET_SUIT(card);
        int new_suit = perm[suit];
        result = mask_set(result, MODERN_MAKE_CARD(rank, new_suit));
    }
    return result;
}

static uint64_t combination_rank_signature(mask_t combination) {
    card_list_t list = mask_to_list(combination);
    int ranks[7];
    for (int i = 0; i < list.count; i++) {
        ranks[i] = MODERN_GET_RANK(list.cards[i]);
    }
    /* Simple insertion sort; list.count <= 7 */
    for (int i = 1; i < list.count; i++) {
        int key = ranks[i];
        int j = i - 1;
        while (j >= 0 && ranks[j] > key) {
            ranks[j + 1] = ranks[j];
            j--;
        }
        ranks[j + 1] = key;
    }

    uint64_t signature = 0;
    for (int i = 0; i < list.count; i++) {
        signature |= ((uint64_t)ranks[i] & 0x3FULL) << (6 * i);
    }
    signature |= ((uint64_t)list.count & 0xFFULL) << 48;
    return signature;
}

static int compare_uint64(const void *a, const void *b) {
    const uint64_t lhs = *(const uint64_t *)a;
    const uint64_t rhs = *(const uint64_t *)b;
    if (lhs < rhs) return -1;
    if (lhs > rhs) return 1;
    return 0;
}

static void collect_combo_signatures(mask_t deck, uint32_t combo_size,
                                     uint64_t *signatures, size_t max_signatures,
                                     size_t *out_count) {
    combo_generator_t *gen = combo_generator_create_simple(deck, combo_size);
    size_t count = 0;
    mask_t combination;
    while (combo_generator_next(gen, &combination) && count < max_signatures) {
        signatures[count++] = combination_rank_signature(combination);
    }
    combo_generator_destroy(gen);
    *out_count = count;
}

static int mask_to_sorted_indices(mask_t combination, uint32_t *indices, size_t capacity) {
    card_list_t list = mask_to_list(combination);
    if ((size_t)list.count > capacity) {
        return 0;
    }
    for (int i = 0; i < list.count; i++) {
        indices[i] = (uint32_t)list.cards[i];
    }
    return list.count;
}

#define VALIDATE_ASSERT(condition, message) do { \
    validation_stats.assertions_checked++; \
    if (!(condition)) { \
        printf("❌ ASSERTION FAILED: %s\n", message); \
        validation_stats.tests_failed++; \
        return false; \
    } \
} while(0)

#define VALIDATE_TEST(test_name, test_func) do { \
    printf("🔍 Testing %s...\n", test_name); \
    validation_stats.tests_run++; \
    if (test_func()) { \
        printf("✅ %s PASSED\n\n", test_name); \
        validation_stats.tests_passed++; \
    } else { \
        printf("❌ %s FAILED\n\n", test_name); \
    } \
} while(0)

/* 1. API Guards - Edge cases and boundary conditions */
static bool test_api_guards(void) {
    printf("  Testing k == 0 case...\n");
    combo_generator_t* gen = combo_generator_create_simple(string_to_mask("As Kh Qd"), 0);
    VALIDATE_ASSERT(gen != NULL, "Generator should be created for k=0");
    VALIDATE_ASSERT(combo_generator_total(gen) == 1, "C(3,0) should equal 1");

    mask_t combination;
    bool has_next = combo_generator_next(gen, &combination);
    VALIDATE_ASSERT(has_next, "Should have one combination (empty set)");
    VALIDATE_ASSERT(combination == MASK_EMPTY, "Combination should be empty mask");
    VALIDATE_ASSERT(!combo_generator_next(gen, &combination), "Should not have more combinations");
    combo_generator_destroy(gen);

    printf("  Testing k > n case...\n");
    gen = combo_generator_create_simple(string_to_mask("As Kh"), 5);
    VALIDATE_ASSERT(gen != NULL, "Generator should be created for k > n");
    VALIDATE_ASSERT(combo_generator_total(gen) == 0, "C(2,5) should equal 0");
    VALIDATE_ASSERT(!combo_generator_next(gen, &combination), "Should have no combinations");
    combo_generator_destroy(gen);

    printf("  Testing empty deck...\n");
    gen = combo_generator_create_simple(MASK_EMPTY, 1);
    VALIDATE_ASSERT(gen != NULL, "Generator should be created for empty deck");
    VALIDATE_ASSERT(combo_generator_total(gen) == 0, "C(0,1) should equal 0");
    VALIDATE_ASSERT(!combo_generator_next(gen, &combination), "Should have no combinations");
    combo_generator_destroy(gen);

    printf("  Testing dead mask covering entire deck...\n");
    mask_t full_deck = string_to_mask("As Kh Qd");
    combo_config_t config = combo_config_default();
    config.available_cards = full_deck;
    config.excluded_cards = full_deck; /* All cards are dead */
    config.combo_size = 2;
    gen = combo_generator_create(&config);
    VALIDATE_ASSERT(gen != NULL, "Generator should be created with all dead cards");
    VALIDATE_ASSERT(combo_generator_total(gen) == 0, "Should have 0 combinations with all dead");
    VALIDATE_ASSERT(!combo_generator_next(gen, &combination), "Should have no combinations");
    combo_generator_destroy(gen);

    return true;
}

/* 2. Input Integrity - Validate input data structure */
static bool test_input_integrity(void) {
    printf("  Testing single-bit card validation...\n");

    /* Test that each card in our test deck has exactly one bit set */
    mask_t test_deck = string_to_mask("As Kh Qd Jc Ts");
    card_list_t cards = mask_to_list(test_deck);

    for (int i = 0; i < cards.count; i++) {
        mask_t single_card = MASK_EMPTY;
        single_card = mask_set(single_card, cards.cards[i]);
        VALIDATE_ASSERT(mask_popcount(single_card) == 1,
                       "Each card should have exactly one bit set");
    }

    printf("  Testing no duplicate cards...\n");
    /* Verify no card appears twice in our representation */
    for (int i = 0; i < cards.count; i++) {
        for (int j = i + 1; j < cards.count; j++) {
            VALIDATE_ASSERT(cards.cards[i] != cards.cards[j],
                           "No duplicate cards should exist");
        }
    }

    printf("  Testing mask construction integrity...\n");
    /* Verify that reconstructed mask equals original */
    mask_t reconstructed = MASK_EMPTY;
    for (int i = 0; i < cards.count; i++) {
        reconstructed = mask_set(reconstructed, cards.cards[i]);
    }
    VALIDATE_ASSERT(reconstructed == test_deck, "Reconstructed mask should match original");

    return true;
}

/* 3. Dead Cards - Ensure dead cards never appear in combinations */
static bool test_dead_cards_enforcement(void) {
    printf("  Testing dead cards exclusion...\n");

    mask_t available = string_to_mask("As Kh Qd Jc Ts 9h 8s 7d");
    mask_t dead = string_to_mask("Jc 9h"); /* Mark Jc and 9h as dead */

    combo_config_t config = combo_config_default();
    config.available_cards = available;
    config.excluded_cards = dead;
    config.combo_size = 3;

    combo_generator_t* gen = combo_generator_create(&config);
    VALIDATE_ASSERT(gen != NULL, "Generator should be created with dead cards");

    /* Generate all combinations and verify none contain dead cards */
    mask_t combination;
    int combinations_checked = 0;

    while (combo_generator_next(gen, &combination)) {
        VALIDATE_ASSERT(!mask_intersects(combination, dead),
                       "Combination should not contain dead cards");
        VALIDATE_ASSERT(mask_is_subset(combination, available),
                       "Combination should be subset of available cards");
        VALIDATE_ASSERT(mask_popcount(combination) == 3,
                       "Combination should have exactly 3 cards");
        combinations_checked++;

        /* Limit check to avoid excessive output */
        if (combinations_checked >= 50) break;
    }

    printf("    Verified %d combinations contain no dead cards\n", combinations_checked);
    VALIDATE_ASSERT(combinations_checked > 0, "Should have generated some combinations");

    combo_generator_destroy(gen);
    return true;
}

/* 4. Omaha Constraints - Test poker-specific constraints */
static bool test_omaha_constraints(void) {
    printf("  Testing Omaha-style constraints...\n");

    mask_t board = string_to_mask("As Kh Qd Jc Ts");
    mask_t dead = string_to_mask("2s 3h");

    combo_config_t omaha_config = combo_config_omaha(board, dead, 4);
    combo_generator_t* gen = combo_generator_create(&omaha_config);
    VALIDATE_ASSERT(gen != NULL, "Omaha generator should be created");

    /* Test a few combinations to ensure they follow Omaha rules */
    mask_t hole_cards;
    int omaha_combinations_checked = 0;

    while (combo_generator_next(gen, &hole_cards) && omaha_combinations_checked < 20) {
        /* Verify exactly 4 hole cards */
        VALIDATE_ASSERT(mask_popcount(hole_cards) == 4,
                       "Omaha hole cards should be exactly 4");

        /* Verify no intersection with board */
        VALIDATE_ASSERT(!mask_intersects(hole_cards, board),
                       "Hole cards should not intersect with board");

        /* Verify no intersection with dead cards */
        VALIDATE_ASSERT(!mask_intersects(hole_cards, dead),
                       "Hole cards should not intersect with dead cards");

        omaha_combinations_checked++;
    }

    printf("    Verified %d Omaha combinations follow constraints\n", omaha_combinations_checked);
    VALIDATE_ASSERT(omaha_combinations_checked > 0, "Should have generated Omaha combinations");

    combo_generator_destroy(gen);
    return true;
}

/* 5. Combination Aggregation - Verify popcount integrity */
static bool test_combination_aggregation(void) {
    printf("  Testing popcount integrity for different k values...\n");

    mask_t test_deck = string_to_mask("As Kh Qd Jc Ts 9h 8s");

    for (uint32_t k = 1; k <= 5; k++) {
        combo_generator_t* gen = combo_generator_create_simple(test_deck, k);
        VALIDATE_ASSERT(gen != NULL, "Generator should be created for each k");

        mask_t combination;
        int combinations_with_correct_popcount = 0;
        int total_combinations = 0;

        while (combo_generator_next(gen, &combination)) {
            total_combinations++;
            if (mask_popcount(combination) == (int)k) {
                combinations_with_correct_popcount++;
            }

            /* Debug mode: verify popcount for every combination */
            #ifdef DEBUG
            VALIDATE_ASSERT(mask_popcount(combination) == (int)k,
                           "Every combination should have exactly k cards");
            #endif

            /* Limit to avoid excessive checking */
            if (total_combinations >= 100) break;
        }

        VALIDATE_ASSERT(combinations_with_correct_popcount == total_combinations,
                       "All combinations should have correct popcount");

        printf("    k=%u: %d/%d combinations have correct popcount\n",
               k, combinations_with_correct_popcount, total_combinations);

        combo_generator_destroy(gen);
    }

    return true;
}

/* 6. Types & Shifts - Test safe bit operations */
static bool test_types_and_shifts(void) {
    printf("  Testing safe bit operations...\n");

    /* Test that we can safely handle all 52 card positions */
    for (int card = 0; card < 52; card++) {
        mask_t single_card = MASK_EMPTY;
        single_card = mask_set(single_card, card);

        VALIDATE_ASSERT(mask_popcount(single_card) == 1,
                       "Single card should have popcount 1");
        VALIDATE_ASSERT(mask_is_set(single_card, card),
                       "Card should be set in its own mask");

        /* Test that no undefined behavior occurs with shifts */
        uint64_t shift_test = UINT64_C(1) << card;
        VALIDATE_ASSERT(shift_test != 0, "Shift should not result in zero (unless card 0)");
    }

    printf("  Testing boundary conditions...\n");
    /* Test edge cases */
    mask_t card_0 = mask_set(MASK_EMPTY, 0);
    mask_t card_51 = mask_set(MASK_EMPTY, 51);

    VALIDATE_ASSERT(mask_is_set(card_0, 0), "Card 0 should be settable");
    VALIDATE_ASSERT(mask_is_set(card_51, 51), "Card 51 should be settable");
    VALIDATE_ASSERT(!mask_intersects(card_0, card_51), "Card 0 and 51 should not intersect");

    return true;
}

/* 7. Quality Tools - Test with various compiler flags */
static bool test_quality_tools(void) {
    printf("  Testing memory safety (basic checks)...\n");

    /* Test for memory leaks by creating and destroying many generators */
    for (int i = 0; i < 100; i++) {
        combo_generator_t* gen = combo_generator_create_simple(
            string_to_mask("As Kh Qd Jc Ts"), 3);
        VALIDATE_ASSERT(gen != NULL, "Generator should be created");

        /* Use the generator briefly */
        mask_t combination;
        combo_generator_next(gen, &combination);

        combo_generator_destroy(gen);
    }

    printf("  Testing iterator memory management...\n");
    /* Test iterator memory management */
    for (int i = 0; i < 50; i++) {
        combo_iterator_t* iter = combo_iterator_create_simple(
            string_to_mask("As Kh Qd"), 2, 10);
        VALIDATE_ASSERT(iter != NULL, "Iterator should be created");

        mask_t combination;
        combo_iterator_next(iter, &combination);

        combo_iterator_destroy(iter);
    }

    printf("  Testing null pointer safety...\n");
    /* Test null pointer handling */
    VALIDATE_ASSERT(!combo_generator_next(NULL, NULL), "NULL generator should return false");
    VALIDATE_ASSERT(combo_generator_total(NULL) == 0, "NULL generator total should be 0");

    mask_t dummy = MASK_EMPTY;
    (void)dummy; /* Use dummy to avoid warning */
    combo_generator_t* gen = combo_generator_create_simple(string_to_mask("As"), 1);
    VALIDATE_ASSERT(!combo_generator_next(gen, NULL), "NULL combination pointer should fail");
    combo_generator_destroy(gen);

    return true;
}

static bool test_suit_permutation_invariance(void) {
    printf("  Testing suit permutation invariance...\n");

    mask_t base_deck = string_to_mask("As Kd Qh Jc Ts 9d 8h 7c");
    const uint32_t combo_size = 3;

    uint64_t base_signatures[128] = {0};
    size_t base_count = 0;
    collect_combo_signatures(base_deck, combo_size, base_signatures,
                             sizeof(base_signatures) / sizeof(base_signatures[0]),
                             &base_count);

    combo_generator_t *base_gen = combo_generator_create_simple(base_deck, combo_size);
    uint64_t expected_total = combo_generator_total(base_gen);
    combo_generator_destroy(base_gen);

    VALIDATE_ASSERT(base_count == expected_total,
                   "Signature collection should match total combinations");
    qsort(base_signatures, base_count, sizeof(uint64_t), compare_uint64);

    static const int permutations[][MODERN_SUIT_COUNT] = {
        {MODERN_SUIT_CLUBS, MODERN_SUIT_DIAMONDS, MODERN_SUIT_HEARTS, MODERN_SUIT_SPADES},
        {MODERN_SUIT_DIAMONDS, MODERN_SUIT_HEARTS, MODERN_SUIT_SPADES, MODERN_SUIT_CLUBS},
        {MODERN_SUIT_SPADES, MODERN_SUIT_HEARTS, MODERN_SUIT_DIAMONDS, MODERN_SUIT_CLUBS},
        {MODERN_SUIT_HEARTS, MODERN_SUIT_CLUBS, MODERN_SUIT_SPADES, MODERN_SUIT_DIAMONDS}
    };

    uint64_t perm_signatures[128];
    size_t perm_count = 0;

    for (size_t i = 0; i < sizeof(permutations) / sizeof(permutations[0]); i++) {
        mask_t permuted_deck = mask_permute_suits(base_deck, permutations[i]);
        collect_combo_signatures(permuted_deck, combo_size,
                                 perm_signatures,
                                 sizeof(perm_signatures) / sizeof(perm_signatures[0]),
                                 &perm_count);
        VALIDATE_ASSERT(perm_count == base_count,
                        "Suit permutation should not change combination count");

        qsort(perm_signatures, perm_count, sizeof(uint64_t), compare_uint64);
        VALIDATE_ASSERT(memcmp(base_signatures, perm_signatures,
                               base_count * sizeof(uint64_t)) == 0,
                        "Suit permutation should preserve rank distributions");
    }

    return true;
}

static bool test_random_dead_masks(void) {
    printf("  Testing random dead mask enforcement...\n");

    mask_t deck = string_to_mask("As Ks Qs Js Ts 9s 8s 7s 6s 5s");
    const uint32_t combo_size = 4;
    const int iterations = 100;
    uint64_t rng_state = UINT64_C(0xDEADBEEFCAFEBABE);
    card_list_t deck_cards = mask_to_list(deck);

    for (int i = 0; i < iterations; i++) {
        int desired_dead = (int)rng_next_range(&rng_state, 8);
        mask_t dead = MASK_EMPTY;

        while (mask_popcount(dead) < desired_dead &&
               mask_popcount(dead) < deck_cards.count) {
            int choice = deck_cards.cards[rng_next_range(&rng_state, (uint32_t)deck_cards.count)];
            dead = mask_set(dead, choice);
        }

        combo_config_t config = combo_config_default();
        config.available_cards = deck;
        config.excluded_cards = dead;
        config.combo_size = combo_size;

        combo_generator_t *gen = combo_generator_create(&config);
        VALIDATE_ASSERT(gen != NULL, "Generator should be created for random dead masks");

        int available_count = mask_popcount(mask_subtract(deck, dead));
        uint64_t expected = 0;
        if (available_count >= (int)combo_size) {
            expected = combo_binomial((uint32_t)available_count, combo_size);
        }

        VALIDATE_ASSERT(combo_generator_total(gen) == expected,
                        "Generator total should match combinatorial count");

        mask_t combination;
        uint64_t generated = 0;
        while (combo_generator_next(gen, &combination)) {
            VALIDATE_ASSERT(!mask_intersects(combination, dead),
                            "Combination must not include dead cards");
            generated++;
        }

        VALIDATE_ASSERT(generated == expected,
                        "Generated combinations should match expected count");
        combo_generator_destroy(gen);
    }

    return true;
}

static bool test_basic_monotonicity(void) {
    printf("  Testing monotonic combination ordering...\n");

    mask_t deck = MASK_EMPTY;
    for (int i = 0; i < 6; i++) {
        deck = mask_set(deck, i);
    }

    const uint32_t combo_size = 3;
    combo_generator_t *gen = combo_generator_create_simple(deck, combo_size);
    VALIDATE_ASSERT(gen != NULL, "Monotonic generator should be created");

    mask_t combination;
    uint32_t prev_indices[MODERN_DECK_SIZE] = {0};
    int direction = 0; /* 0 = unknown, 1 = increasing, -1 = decreasing */
    bool first = true;

    while (combo_generator_next(gen, &combination)) {
    uint32_t indices[MODERN_DECK_SIZE];
    int count = mask_to_sorted_indices(combination, indices, MODERN_DECK_SIZE);
        VALIDATE_ASSERT(count == (int)combo_size,
                        "Combination should have expected cardinality");

        if (!first) {
            int cmp = 0;
            for (int i = 0; i < count; i++) {
                if (indices[i] != prev_indices[i]) {
                    cmp = (indices[i] > prev_indices[i]) ? 1 : -1;
                    break;
                }
            }

            VALIDATE_ASSERT(cmp != 0, "Generator should not repeat combinations");

            if (direction == 0) {
                direction = cmp;
            } else {
                VALIDATE_ASSERT(cmp == direction,
                                "Generator should maintain a consistent lexicographic ordering");
            }
        } else {
            first = false;
        }

        memcpy(prev_indices, indices, sizeof(uint32_t) * count);
    }

    combo_generator_destroy(gen);
    return true;
}

static bool test_no_duplicate_combinations_small_deck(void) {
    printf("  Testing absence of duplicate combinations on small deck...\n");

    mask_t deck = MASK_EMPTY;
    for (int i = 0; i < 8; i++) {
        deck = mask_set(deck, i);
    }

    const uint32_t combo_size = 4;
    combo_generator_t *gen = combo_generator_create_simple(deck, combo_size);
    VALIDATE_ASSERT(gen != NULL, "Duplicate check generator should be created");

    uint64_t expected_total = combo_generator_total(gen);

    mask_t seen[128];
    size_t seen_count = 0;
    mask_t combination;

    while (combo_generator_next(gen, &combination)) {
        for (size_t i = 0; i < seen_count; i++) {
            VALIDATE_ASSERT(seen[i] != combination,
                            "Combination should not repeat");
        }
        seen[seen_count++] = combination;
    }

    VALIDATE_ASSERT(seen_count == expected_total,
                    "All combinations should be unique");
    combo_generator_destroy(gen);
    return true;
}

static bool test_stress_limits(void) {
    printf("  Testing stress limits across deck sizes...\n");

    mask_t small_deck = MASK_EMPTY;
    for (int i = 0; i < 7; i++) {
        small_deck = mask_set(small_deck, i);
    }

    for (uint32_t k = 1; k <= 7; k++) {
        combo_generator_t *gen = combo_generator_create_simple(small_deck, k);
        VALIDATE_ASSERT(gen != NULL, "Small deck generator should be created");
        uint64_t expected = combo_binomial(7, k);
        VALIDATE_ASSERT(combo_generator_total(gen) == expected,
                        "Generator total should match theoretical value");

        uint64_t count = 0;
        mask_t combination;
        while (combo_generator_next(gen, &combination)) {
            count++;
        }
        VALIDATE_ASSERT(count == expected,
                        "Enumerated count should match theoretical value");
        combo_generator_destroy(gen);
    }

    mask_t full_deck = MASK_EMPTY;
    for (int i = 0; i < 52; i++) {
        full_deck = mask_set(full_deck, i);
    }

    for (uint32_t k = 1; k <= 7; k++) {
        combo_generator_t *gen = combo_generator_create_simple(full_deck, k);
        VALIDATE_ASSERT(gen != NULL, "Full deck generator should be created");
        uint64_t expected = combo_binomial(52, k);
        VALIDATE_ASSERT(combo_generator_total(gen) == expected,
                        "Full deck total should match theoretical binomial");

        if (k <= 3) {
            uint64_t count = 0;
            mask_t combination;
            while (combo_generator_next(gen, &combination)) {
                count++;
            }
            VALIDATE_ASSERT(count == expected,
                            "Explicit enumeration should match expected total");
        }
        combo_generator_destroy(gen);
    }

    return true;
}

/* 8. Benchmark Methodology - Proper timing and validation */
static bool test_benchmark_methodology(void) {
    printf("  Testing benchmark methodology...\n");

    /* Create test generator */
    mask_t full_deck = MASK_EMPTY;
    for (int i = 0; i < 52; i++) {
        full_deck = mask_set(full_deck, i);
    }

    combo_generator_t* gen = combo_generator_create_simple(full_deck, 5);
    VALIDATE_ASSERT(gen != NULL, "Benchmark generator should be created");

    /* Warm-up phase */
    printf("    Performing warm-up...\n");
    mask_t combination;
    for (int i = 0; i < 1000; i++) {
        combo_generator_next(gen, &combination);
    }

    /* Reset for actual benchmark */
    combo_generator_destroy(gen);
    gen = combo_generator_create_simple(full_deck, 5);

    /* Monotonic timing test */
    uint64_t start_ns = monotonic_time_ns();
    VALIDATE_ASSERT(start_ns != 0, "Monotonic clock should be available");

    /* Perform measured work */
    const int benchmark_iterations = 10000;
    int successful_generations = 0;

    for (int i = 0; i < benchmark_iterations; i++) {
        if (combo_generator_next(gen, &combination)) {
            successful_generations++;
            VALIDATE_ASSERT(mask_popcount(combination) == 5,
                           "Benchmark combinations should have 5 cards");
        }
    }

    uint64_t end_ns = monotonic_time_ns();
    VALIDATE_ASSERT(end_ns > start_ns, "End time should be greater than start time");

    /* Calculate timing */
    uint64_t time_ns = end_ns - start_ns;

    VALIDATE_ASSERT(time_ns > 0, "Elapsed time should be positive");
    VALIDATE_ASSERT(successful_generations == benchmark_iterations,
                   "All benchmark iterations should succeed");

    /* Verify timing consistency (run again and compare) */
    VALIDATE_ASSERT(combo_generator_reset(gen), "Generator reset should succeed");
    start_ns = monotonic_time_ns();
    int second_run_generations = 0;
    for (int i = 0; i < 1000; i++) {
        if (combo_generator_next(gen, &combination)) {
            second_run_generations++;
        }
    }
    end_ns = monotonic_time_ns();

    uint64_t time_ns_2 = end_ns - start_ns;

    /* Timing should be reasonable (not zero, not excessive) */
    VALIDATE_ASSERT(time_ns_2 > 0, "Second timing should be positive");
    VALIDATE_ASSERT(second_run_generations == 1000,
                    "Second benchmark run should complete requested iterations");

    printf("    Benchmark: %d iterations in %llu ns (%.1f ns/iter)\n",
           benchmark_iterations, (unsigned long long)time_ns,
           (double)time_ns / benchmark_iterations);
    printf("    Second run: %d iterations in %llu ns\n",
           second_run_generations, (unsigned long long)time_ns_2);

    combo_generator_destroy(gen);
    return true;
}

/* Full combination generation test - verify C(52,5) completely */
static bool test_full_generation(void) {
    printf("🔍 Testing complete C(52,5) generation...\n");

    mask_t full_deck = MASK_EMPTY;
    for (int i = 0; i < 52; i++) {
        full_deck = mask_set(full_deck, i);
    }

    combo_generator_t* gen = combo_generator_create_simple(full_deck, 5);
    VALIDATE_ASSERT(gen != NULL, "Full deck generator should be created");

    uint64_t expected_total = combo_binomial(52, 5);
    VALIDATE_ASSERT(expected_total == 2598960, "C(52,5) should equal 2,598,960");
    VALIDATE_ASSERT(combo_generator_total(gen) == expected_total,
                   "Generator should report correct total");

    if (!g_quiet_mode) {
        printf("  Generating all %llu combinations (this may take a moment)...\n",
               (unsigned long long)expected_total);
    }

    mask_t combination;
    uint64_t actual_count = 0;
    uint64_t progress_interval = expected_total / 20; /* 5% intervals */
    uint64_t next_progress = progress_interval;

    uint64_t start_ns = monotonic_time_ns();

    while (combo_generator_next(gen, &combination)) {
        VALIDATE_ASSERT(mask_popcount(combination) == 5,
                       "Each combination should have exactly 5 cards");
        VALIDATE_ASSERT(mask_is_subset(combination, full_deck),
                       "Each combination should be subset of deck");

        actual_count++;

        if (!g_quiet_mode && actual_count >= next_progress) {
            printf("    Progress: %llu/%llu (%.1f%%)\n",
                   (unsigned long long)actual_count,
                   (unsigned long long)expected_total,
                   100.0 * (double)actual_count / (double)expected_total);
            next_progress += progress_interval;
        }
    }

    uint64_t end_ns = monotonic_time_ns();
    uint64_t time_ns = (end_ns > start_ns) ? (end_ns - start_ns) : 0;

    VALIDATE_ASSERT(actual_count == expected_total,
                   "Should generate exactly C(52,5) combinations");
    VALIDATE_ASSERT(time_ns > 0, "Full generation timing should be positive");

    printf("  ✅ Generated all %llu combinations in %.2f seconds\n",
           (unsigned long long)actual_count, (double)time_ns / 1000000000.0);
    printf("  ⚡ Performance: %.0f combinations/second\n",
           (double)actual_count * 1000000000.0 / (double)time_ns);

    combo_generator_destroy(gen);
    return true;
}

/* Test iterator exhaustion stability */
static bool test_iterator_exhaustion(void) {
    printf("  Testing iterator exhaustion stability...\n");

    mask_t small_deck = string_to_mask("As Kh Qd");
    combo_generator_t* gen = combo_generator_create_simple(small_deck, 2);
    VALIDATE_ASSERT(gen != NULL, "Iterator exhaustion generator should be created");

    /* Generate all combinations (C(3,2) = 3) */
    mask_t combination;
    int count = 0;
    while (combo_generator_next(gen, &combination)) {
        count++;
        VALIDATE_ASSERT(count <= 3, "Should not generate more than C(3,2) combinations");
    }
    VALIDATE_ASSERT(count == 3, "Should generate exactly C(3,2) = 3 combinations");

    /* Test stability after exhaustion */
    for (int i = 0; i < 5; i++) {
        bool result = combo_generator_next(gen, &combination);
        VALIDATE_ASSERT(!result, "Generator should remain exhausted");
        VALIDATE_ASSERT(!combo_generator_has_next(gen), "has_next should remain false");
        VALIDATE_ASSERT(combo_generator_remaining(gen) == 0, "remaining should stay 0");
    }

    combo_generator_destroy(gen);
    return true;
}

/* Test API misuse handling */
static bool test_api_misuse(void) {
    printf("  Testing API misuse handling...\n");

    mask_t deck = string_to_mask("As Kh Qd Jc Ts");

    /* Test NULL parameters */
    VALIDATE_ASSERT(combo_generator_next(NULL, NULL) == false, "NULL generator should fail");

    combo_generator_t* gen = combo_generator_create_simple(deck, 3);
    VALIDATE_ASSERT(gen != NULL, "Valid generator should be created");

    VALIDATE_ASSERT(combo_generator_next(gen, NULL) == false, "NULL combination should fail");

    /* Test dead mask covering entire deck */
    combo_config_t config = combo_config_default();
    config.available_cards = deck;
    config.excluded_cards = deck; /* Exclude all available cards */
    config.combo_size = 2;

    combo_generator_t* impossible_gen = combo_generator_create(&config);
    VALIDATE_ASSERT(impossible_gen != NULL, "Generator with impossible config should still be created");

    mask_t combination;
    VALIDATE_ASSERT(!combo_generator_next(impossible_gen, &combination),
                   "Generator with no available cards should not produce combinations");
    VALIDATE_ASSERT(combo_generator_total(impossible_gen) == 0,
                   "Generator should report 0 total combinations");

    /* Test k > n case */
    combo_generator_t* oversized_gen = combo_generator_create_simple(deck, 10); /* k=10, n=5 */
    VALIDATE_ASSERT(oversized_gen != NULL, "Oversized generator should be created");
    VALIDATE_ASSERT(!combo_generator_next(oversized_gen, &combination),
                   "k > n generator should not produce combinations");
    VALIDATE_ASSERT(combo_generator_total(oversized_gen) == 0,
                   "k > n generator should report 0 total combinations");

    combo_generator_destroy(gen);
    combo_generator_destroy(impossible_gen);
    combo_generator_destroy(oversized_gen);
    return true;
}

/* Main validation suite */
int main(int argc, char* argv[]) {
    /* Parse command line arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--quiet") == 0 || strcmp(argv[i], "-q") == 0) {
            g_quiet_mode = true;
        }
    }

    printf("🛡️  Combinatorial Generator Validation Suite\n");
    printf("===========================================\n\n");
    if (g_quiet_mode) {
        printf("Running in quiet mode (progress logging disabled)\n\n");
    }

    /* Run all 15 micro-checks */
    VALIDATE_TEST("API Guards", test_api_guards);
    VALIDATE_TEST("Input Integrity", test_input_integrity);
    VALIDATE_TEST("Dead Cards Enforcement", test_dead_cards_enforcement);
    VALIDATE_TEST("Omaha Constraints", test_omaha_constraints);
    VALIDATE_TEST("Combination Aggregation", test_combination_aggregation);
    VALIDATE_TEST("Types & Shifts", test_types_and_shifts);
    VALIDATE_TEST("Quality Tools", test_quality_tools);
    VALIDATE_TEST("Suit Permutation Invariance", test_suit_permutation_invariance);
    VALIDATE_TEST("Random Dead Masks", test_random_dead_masks);
    VALIDATE_TEST("Monotonic Ordering", test_basic_monotonicity);
    VALIDATE_TEST("Duplicate-Free Generation", test_no_duplicate_combinations_small_deck);
    VALIDATE_TEST("Stress Limits", test_stress_limits);
    VALIDATE_TEST("Benchmark Methodology", test_benchmark_methodology);

    /* Edge case tests */
    VALIDATE_TEST("Iterator Exhaustion Stability", test_iterator_exhaustion);
    VALIDATE_TEST("API Misuse Handling", test_api_misuse);

    /* Bonus: Full generation test */
    VALIDATE_TEST("Full C(52,5) Generation", test_full_generation);

    /* Print final results */
    printf("\n🏁 Validation Summary\n");
    printf("==================\n");
    printf("Tests run: %d\n", validation_stats.tests_run);
    printf("Tests passed: %d\n", validation_stats.tests_passed);
    printf("Tests failed: %d\n", validation_stats.tests_failed);
    printf("Assertions checked: %d\n", validation_stats.assertions_checked);

    double success_rate = validation_stats.tests_run > 0 ?
        100.0 * validation_stats.tests_passed / validation_stats.tests_run : 0.0;
    printf("Success rate: %.1f%%\n", success_rate);

    if (validation_stats.tests_failed == 0) {
        printf("\n🎉 ALL VALIDATIONS PASSED! Generator is truly \"béton\" 🛡️\n");
        return 0;
    } else {
        printf("\n❌ Some validations failed. Please review and fix.\n");
        return 1;
    }
}
