/*
 * modern_combinations.c - Implementation of modern combinatorial generator
 */

#include <poker_eval/core/modern_combinations.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* Helper function to calculate binomial coefficient */
uint64_t combo_binomial(uint32_t n, uint32_t k)
{
    if (k > n)
        return 0;
    if (k == 0 || k == n)
        return 1;

    /* Use symmetry: C(n,k) = C(n,n-k) */
    if (k > n - k)
        k = n - k;

    uint64_t result = 1;
    for (uint32_t i = 0; i < k; i++)
    {
        result = result * (n - i) / (i + 1);
    }

    return result;
}

/* Count combinations for a given mask and size */
uint64_t combo_count_combinations(mask_t available_cards, uint32_t combo_size)
{
    uint32_t available_count = mask_popcount(available_cards);
    return combo_binomial(available_count, combo_size);
}

/* Count combinations in a size range */
uint64_t combo_count_combinations_range(mask_t available_cards,
                                        uint32_t min_size, uint32_t max_size)
{
    uint32_t available_count = mask_popcount(available_cards);
    uint64_t total = 0;

    for (uint32_t size = min_size; size <= max_size && size <= available_count; size++)
    {
        total += combo_binomial(available_count, size);
    }

    return total;
}

/* Default configuration */
combo_config_t combo_config_default(void)
{
    combo_config_t config = {0};

    /* Set up standard 52-card deck */
    config.available_cards = MASK_EMPTY;
    for (int i = 0; i < 52; i++)
    {
        config.available_cards = mask_set(config.available_cards, i);
    }

    config.excluded_cards = MASK_EMPTY;
    config.combo_size = 5;
    config.min_size = 5;
    config.max_size = 5;
    config.mode = COMBO_MODE_EXACT;
    config.optimization = COMBO_OPT_BALANCED;
    config.iteration = COMBO_ITER_FORWARD;
    config.max_combinations = UINT64_MAX;
    config.start_offset = 0;
    config.unique_only = true;
    config.sorted_output = false;

    return config;
}

/* Texas Hold'em configuration */
combo_config_t combo_config_holdem(mask_t board, mask_t dead_cards)
{
    combo_config_t config = combo_config_default();

    /* Remove board and dead cards from available cards */
    config.excluded_cards = mask_union(board, dead_cards);
    config.available_cards = mask_subtract(config.available_cards, config.excluded_cards);

    config.combo_size = 2; /* Hole cards */
    config.min_size = 2;
    config.max_size = 2;

    return config;
}

/* Omaha configuration */
combo_config_t combo_config_omaha(mask_t board, mask_t dead_cards, uint32_t hole_cards)
{
    combo_config_t config = combo_config_default();

    config.excluded_cards = mask_union(board, dead_cards);
    config.available_cards = mask_subtract(config.available_cards, config.excluded_cards);

    config.combo_size = hole_cards;
    config.min_size = hole_cards;
    config.max_size = hole_cards;

    return config;
}

/* Seven Card Stud configuration */
combo_config_t combo_config_stud(mask_t dead_cards, uint32_t hand_size)
{
    combo_config_t config = combo_config_default();

    config.excluded_cards = dead_cards;
    config.available_cards = mask_subtract(config.available_cards, config.excluded_cards);

    config.combo_size = hand_size;
    config.min_size = hand_size;
    config.max_size = hand_size;

    return config;
}

/* Initialize generator state */
static void init_generator_state(combo_generator_t *restrict gen)
{
    combo_state_t *restrict state = &gen->state;

    state->current_combo = MASK_EMPTY;
    state->position = 0;
    state->combo_size = gen->config.combo_size;
    state->remaining_cards = mask_popcount(gen->config.available_cards);
    state->total_combos = combo_count_combinations(gen->config.available_cards,
                                                   gen->config.combo_size);
    state->current_level = -1;
    state->exhausted = false;

    /* Initialize indices array */
    memset(state->indices, 0, sizeof(state->indices));
    memset(state->limits, 0, sizeof(state->limits));

    /* Set up limits for each position */
    card_list_t available_list = mask_to_list(gen->config.available_cards);
    for (int i = 0; i < (int)state->combo_size && i < available_list.count; i++)
    {
        /* Last valid index for position i in lexicographic ordering */
        state->limits[i] = available_list.count - state->combo_size + i;
    }
}

/* Create combination generator */
combo_generator_t *combo_generator_create(const combo_config_t *config)
{
    if (!config || !combo_generator_validate_config(config))
    {
        return NULL;
    }

    combo_generator_t *gen = calloc(1, sizeof(combo_generator_t));
    if (!gen)
        return NULL;

    gen->config = *config;

    /* Apply excluded cards */
    gen->config.available_cards = mask_subtract(gen->config.available_cards,
                                                gen->config.excluded_cards);

    init_generator_state(gen);

    /* Initialize performance tracking */
    gen->combinations_generated = 0;
    gen->total_iterations = 0;
    gen->cache_hits = 0;
    gen->cache_misses = 0;

    /* Initialize cache */
    gen->cache_size = 0;
    memset(gen->cached_masks, 0, sizeof(gen->cached_masks));

    return gen;
}

/* Create simple generator */
combo_generator_t *combo_generator_create_simple(mask_t available_cards,
                                                 uint32_t combo_size)
{
    combo_config_t config = combo_config_default();
    config.available_cards = available_cards;
    config.combo_size = combo_size;
    config.min_size = combo_size;
    config.max_size = combo_size;

    return combo_generator_create(&config);
}

/* Destroy generator */
void combo_generator_destroy(combo_generator_t *gen)
{
    if (!gen)
        return;

    if (gen->internal_buffer)
    {
        free(gen->internal_buffer);
    }

    free(gen);
}

/* Generate next combination using lexicographic ordering with caching */
bool combo_generator_next(combo_generator_t *gen, mask_t *restrict combination)
{
    if (!gen || !combination || gen->state.exhausted)
    {
        return false;
    }

    combo_state_t *restrict state = &gen->state;
    gen->total_iterations++;

    /* Special case for k=0: exactly one combination (empty set) */
    if (state->combo_size == 0)
    {
        if (state->position == 0)
        {
            *combination = MASK_EMPTY;
            state->position++;
            gen->combinations_generated++;
            return true;
        }
        else
        {
            state->exhausted = true;
            return false;
        }
    }

    /* Special case for k>n: no combinations possible */
    uint32_t available_count = mask_popcount(gen->config.available_cards);
    if (state->combo_size > available_count)
    {
        state->exhausted = true;
        return false;
    }

    /* Cache available list to avoid repeated mask_to_list calls */
    static card_list_t cached_list = {0};
    static mask_t cached_mask = 0;
    card_list_t *available_list;

    if (cached_mask != gen->config.available_cards)
    {
        cached_list = mask_to_list(gen->config.available_cards);
        cached_mask = gen->config.available_cards;
        gen->cache_misses++;
    }
    else
    {
        gen->cache_hits++;
    }
    available_list = &cached_list;

    /* First combination */
    if (state->current_level < 0)
    {
        if (available_list->count < (int)state->combo_size)
        {
            state->exhausted = true;
            return false;
        }

        /* Initialize first combination using incremental mask building */
        state->current_combo = MASK_EMPTY;
        for (uint32_t i = 0; i < state->combo_size; i++)
        {
            state->indices[i] = i;
            state->current_combo |= (1ULL << available_list->cards[i]);
        }

        state->current_level = 0;
        *combination = state->current_combo;
        state->position++;
        gen->combinations_generated++;
        return true;
    }

    /* Find rightmost position that can be incremented */
    int pos = (int)state->combo_size - 1;
    while (pos >= 0 && state->indices[pos] >= state->limits[pos])
    {
        pos--;
    }

    if (pos < 0)
    {
        /* All combinations generated */
        state->exhausted = true;
        return false;
    }

    /* Incremental update: remove old card, add new card */
    int old_card = available_list->cards[state->indices[pos]];
    state->current_combo &= ~(1ULL << old_card);

    /* Increment position and reset all positions to the right */
    state->indices[pos]++;
    int new_card = available_list->cards[state->indices[pos]];
    state->current_combo |= (1ULL << new_card);

    for (int i = pos + 1; i < (int)state->combo_size; i++)
    {
        /* Remove old card */
        int prev_card = available_list->cards[state->indices[i]];
        state->current_combo &= ~(1ULL << prev_card);

        /* Set new index and add new card */
        state->indices[i] = state->indices[i - 1] + 1;
        int curr_card = available_list->cards[state->indices[i]];
        state->current_combo |= (1ULL << curr_card);
    }

    *combination = state->current_combo;
    state->position++;
    gen->combinations_generated++;
    return true;
}

/* Check if generator has more combinations */
bool combo_generator_has_next(const combo_generator_t *gen)
{
    return gen && !gen->state.exhausted &&
           gen->state.position < gen->state.total_combos;
}

/* Get remaining combinations count */
uint64_t combo_generator_remaining(const combo_generator_t *gen)
{
    if (!gen || gen->state.exhausted)
        return 0;
    return gen->state.total_combos - gen->state.position;
}

/* Get total combinations count */
uint64_t combo_generator_total(const combo_generator_t *gen)
{
    return gen ? gen->state.total_combos : 0;
}

/* Generate batch of combinations */
size_t combo_generator_next_batch(combo_generator_t *gen, mask_t *restrict combinations,
                                  size_t max_count)
{
    if (!gen || !combinations || max_count == 0)
        return 0;

    size_t generated = 0;
    for (size_t i = 0; i < max_count; i++)
    {
        if (!combo_generator_next(gen, &combinations[i]))
        {
            break;
        }
        generated++;
    }

    return generated;
}

/* Reset generator to beginning */
bool combo_generator_reset(combo_generator_t *gen)
{
    if (!gen)
        return false;

    init_generator_state(gen);
    return true;
}

/* Save current state */
combo_state_t combo_generator_save_state(const combo_generator_t *gen)
{
    combo_state_t empty_state = {0};
    return gen ? gen->state : empty_state;
}

/* Restore state */
bool combo_generator_restore_state(combo_generator_t *gen,
                                   const combo_state_t *state)
{
    if (!gen || !state)
        return false;
    gen->state = *state;
    return true;
}

/* Validate configuration */
bool combo_generator_validate_config(const combo_config_t *config)
{
    if (!config)
        return false;

    /* Check basic constraints - k=0 and k>n are mathematically valid */
    if (config->combo_size > 52)
        return false;

    /* Check range mode constraints */
    if (config->mode == COMBO_MODE_RANGE)
    {
        if (config->min_size > config->max_size)
            return false;
        /* Allow max_size > available_count for mathematical completeness */
    }

    return true;
}

/* Get generator statistics */
void combo_generator_get_stats(const combo_generator_t *gen,
                               uint64_t *generated, uint64_t *iterations,
                               uint64_t *cache_hits, uint64_t *cache_misses)
{
    if (!gen)
        return;

    if (generated)
        *generated = gen->combinations_generated;
    if (iterations)
        *iterations = gen->total_iterations;
    if (cache_hits)
        *cache_hits = gen->cache_hits;
    if (cache_misses)
        *cache_misses = gen->cache_misses;
}

/* Reset statistics */
void combo_generator_reset_stats(combo_generator_t *gen)
{
    if (!gen)
        return;

    gen->combinations_generated = 0;
    gen->total_iterations = 0;
    gen->cache_hits = 0;
    gen->cache_misses = 0;
}

/* Calculate cache hit rate */
double combo_generator_cache_hit_rate(const combo_generator_t *gen)
{
    if (!gen || (gen->cache_hits + gen->cache_misses) == 0)
    {
        return 0.0;
    }

    return (double)gen->cache_hits / (double)(gen->cache_hits + gen->cache_misses);
}

/* Iterator creation */
combo_iterator_t *combo_iterator_create(combo_generator_t *gen)
{
    if (!gen)
        return NULL;

    combo_iterator_t *iter = calloc(1, sizeof(combo_iterator_t));
    if (!iter)
        return NULL;

    iter->generator = gen;
    iter->private_state = gen->state;
    iter->owns_generator = false;
    iter->batch_buffer = NULL;
    iter->batch_size = 0;
    iter->batch_count = 0;
    iter->batch_index = 0;

    return iter;
}

/* Create simple iterator */
combo_iterator_t *combo_iterator_create_simple(mask_t available_cards,
                                               uint32_t combo_size,
                                               size_t batch_size)
{
    combo_generator_t *gen = combo_generator_create_simple(available_cards, combo_size);
    if (!gen)
        return NULL;

    combo_iterator_t *iter = combo_iterator_create(gen);
    if (!iter)
    {
        combo_generator_destroy(gen);
        return NULL;
    }

    iter->owns_generator = true;

    if (batch_size > 0)
    {
        iter->batch_buffer = malloc(batch_size * sizeof(mask_t));
        if (iter->batch_buffer)
        {
            iter->batch_size = batch_size;
        }
    }

    return iter;
}

/* Destroy iterator */
void combo_iterator_destroy(combo_iterator_t *iter)
{
    if (!iter)
        return;

    if (iter->batch_buffer)
    {
        free(iter->batch_buffer);
    }

    if (iter->owns_generator)
    {
        combo_generator_destroy(iter->generator);
    }

    free(iter);
}

/* Iterator next */
bool combo_iterator_next(combo_iterator_t *iter, mask_t *combination)
{
    if (!iter || !combination)
        return false;

    return combo_generator_next(iter->generator, combination);
}

/* Iterator has next */
bool combo_iterator_has_next(const combo_iterator_t *iter)
{
    return iter && combo_generator_has_next(iter->generator);
}

/* Reset iterator */
void combo_iterator_reset(combo_iterator_t *iter)
{
    if (!iter)
        return;
    combo_generator_reset(iter->generator);
}

/* Utility string functions */
char *combo_config_to_string(const combo_config_t *config, char *buffer, size_t size)
{
    if (!config || !buffer || size < 100)
        return NULL;

    snprintf(buffer, size,
             "Combo Config: size=%u, available=%d cards, mode=%d, optimization=%d",
             config->combo_size,
             mask_popcount(config->available_cards),
             (int)config->mode,
             (int)config->optimization);

    return buffer;
}

/* Print generator statistics */
void combo_generator_print_stats(const combo_generator_t *gen)
{
    if (!gen)
        return;

    printf("Combination Generator Statistics:\n");
    printf("  Generated: %llu combinations\n", (unsigned long long)gen->combinations_generated);
    printf("  Iterations: %llu\n", (unsigned long long)gen->total_iterations);
    printf("  Cache hits: %llu\n", (unsigned long long)gen->cache_hits);
    printf("  Cache misses: %llu\n", (unsigned long long)gen->cache_misses);
    printf("  Hit rate: %.2f%%\n", combo_generator_cache_hit_rate(gen) * 100.0);
    printf("  Total combinations: %llu\n", (unsigned long long)gen->state.total_combos);
    printf("  Remaining: %llu\n", (unsigned long long)combo_generator_remaining(gen));
}

/* Self-test function */
bool combo_generator_self_test(void)
{
    /* Test basic functionality */
    mask_t test_cards = string_to_mask("As Kh Qd Jc Ts");
    combo_generator_t *gen = combo_generator_create_simple(test_cards, 3);

    if (!gen)
        return false;

    /* Should generate exactly C(5,3) = 10 combinations */
    uint64_t expected = combo_binomial(5, 3);
    if (combo_generator_total(gen) != expected)
    {
        combo_generator_destroy(gen);
        return false;
    }

    /* Generate all combinations and verify count */
    uint64_t count = 0;
    mask_t combo;
    while (combo_generator_next(gen, &combo))
    {
        if (mask_popcount(combo) != 3)
        {
            combo_generator_destroy(gen);
            return false;
        }
        count++;
    }

    combo_generator_destroy(gen);
    return count == expected;
}

/* Stub implementations for remaining functions */
combo_generator_t *combo_generator_clone(const combo_generator_t *gen)
{
    if (!gen)
        return NULL;
    return combo_generator_create(&gen->config);
}

size_t combo_generator_generate_all(combo_generator_t *gen, mask_t **combinations)
{
    if (!gen || !combinations)
        return 0;

    uint64_t total = combo_generator_total(gen);
    if (total == 0 || total > SIZE_MAX)
        return 0;

    *combinations = malloc(total * sizeof(mask_t));
    if (!*combinations)
        return 0;

    return combo_generator_next_batch(gen, *combinations, (size_t)total);
}

bool combo_generator_seek(combo_generator_t *gen, uint64_t position)
{
    if (!gen || position >= gen->state.total_combos)
        return false;

    /* Simple implementation: reset and advance */
    combo_generator_reset(gen);
    mask_t dummy;
    for (uint64_t i = 0; i < position; i++)
    {
        if (!combo_generator_next(gen, &dummy))
        {
            return false;
        }
    }

    return true;
}

size_t combo_iterator_next_batch(combo_iterator_t *iter, mask_t *combinations,
                                 size_t max_count)
{
    if (!iter)
        return 0;
    return combo_generator_next_batch(iter->generator, combinations, max_count);
}

bool combo_is_valid_combination(mask_t combination, const combo_config_t *config)
{
    if (!config)
        return false;

    /* Check size */
    uint32_t size = mask_popcount(combination);
    if (size != config->combo_size)
        return false;

    /* Check that all cards are available */
    if (!mask_is_subset(combination, config->available_cards))
        return false;

    /* Check no excluded cards */
    if (mask_intersects(combination, config->excluded_cards))
        return false;

    return true;
}

uint64_t combo_position_to_combination(uint64_t position, uint32_t n, uint32_t k,
                                       uint32_t *indices)
{
    /* Stub implementation */
    (void)position;
    (void)n;
    (void)k;
    (void)indices;
    return 0;
}

uint64_t combo_combination_to_position(const uint32_t *indices, uint32_t n, uint32_t k)
{
    /* Stub implementation */
    (void)indices;
    (void)n;
    (void)k;
    return 0;
}

bool combo_generator_optimize_memory(combo_generator_t *gen)
{
    (void)gen;
    return true; /* Stub */
}

bool combo_generator_set_cache_size(combo_generator_t *gen, size_t cache_size)
{
    if (!gen)
        return false;
    gen->cache_size = (cache_size > 256) ? 256 : (uint32_t)cache_size;
    return true;
}

size_t combo_generator_memory_usage(const combo_generator_t *gen)
{
    return gen ? sizeof(combo_generator_t) + gen->buffer_size : 0;
}

bool combo_generator_skip(combo_generator_t *gen, uint64_t count)
{
    if (!gen)
        return false;

    mask_t dummy;
    for (uint64_t i = 0; i < count; i++)
    {
        if (!combo_generator_next(gen, &dummy))
        {
            return false;
        }
    }

    return true;
}

bool combo_generator_filter(combo_generator_t *gen,
                            bool (*filter_fn)(mask_t combination, void *user_data),
                            void *user_data)
{
    /* Stub implementation */
    (void)gen;
    (void)filter_fn;
    (void)user_data;
    return false;
}

char *combo_state_to_string(const combo_state_t *state, char *buffer, size_t size)
{
    if (!state || !buffer || size < 100)
        return NULL;

    snprintf(buffer, size,
             "State: pos=%llu/%llu, size=%u, exhausted=%s",
             (unsigned long long)state->position,
             (unsigned long long)state->total_combos,
             state->combo_size,
             state->exhausted ? "yes" : "no");

    return buffer;
}
