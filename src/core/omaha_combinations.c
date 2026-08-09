/*
 * omaha_combinations.c - Constrained combination generator for Omaha poker
 *
 * Implements efficient Omaha hand generation with native 2+3 constraints:
 * - Exactly 2 cards from hole (4, 5, or 6 cards)
 * - Exactly 3 cards from board (5 cards)
 * - Direct generation without post-filtering
 * - Integration with modern EvalContext
 */

#include <poker_eval/core/omaha_combinations.h>
#include <poker_eval/core/modern_combinations.h>
#include <poker_eval/core/eval_context.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Default configurations */
omaha_config_t omaha_config_default(void)
{
    omaha_config_t config = {
        .constraints = {
            .hole_cards = MASK_EMPTY,
            .board_cards = MASK_EMPTY,
            .dead_cards = MASK_EMPTY,
            .validate_constraints = true,
            .unique_only = true},
        .enable_cache = false,
        .cache_size_mb = 0,
        .enable_stats = true,
        .iteration = COMBO_ITER_FORWARD,
        .max_combinations = UINT64_MAX,
        .start_offset = 0};
    return config;
}

omaha_config_t omaha_config_plo(mask_t hole, mask_t board, mask_t dead)
{
    omaha_config_t config = omaha_config_default();
    config.constraints.hole_cards = hole;
    config.constraints.board_cards = board;
    config.constraints.dead_cards = dead;
    config.constraints.validate_constraints = true;
    return config;
}

omaha_config_t omaha_config_plo5(mask_t hole, mask_t board, mask_t dead)
{
    /* Same as PLO but will be validated for 5 cards */
    return omaha_config_plo(hole, board, dead);
}

omaha_config_t omaha_config_plo6(mask_t hole, mask_t board, mask_t dead)
{
    /* Same as PLO but will be validated for 6 cards */
    return omaha_config_plo(hole, board, dead);
}

omaha_config_t omaha_config_plo8(mask_t hole, mask_t board, mask_t dead)
{
    omaha_config_t config = omaha_config_plo(hole, board, dead);
    /* PLO8 uses same constraints as PLO - Hi-Lo evaluation is handled in EvalContext */
    return config;
}

/* Validation functions */
bool omaha_validate_constraints(const omaha_constraints_t *constraints)
{
    if (!constraints)
        return false;

    /* Check hole cards count - allow 4, 5, or 6 cards */
    uint32_t hole_count = mask_popcount(constraints->hole_cards);
    if (hole_count != 4 && hole_count != 5 && hole_count != 6)
        return false;

    /* Check board cards count */
    uint32_t board_count = mask_popcount(constraints->board_cards);
    if (board_count != 5)
        return false;

    /* CRITICAL: Check for overlap between hole and board (Omaha rule) */
    if (mask_intersect(constraints->hole_cards, constraints->board_cards) != MASK_EMPTY)
    {
        /* This violates fundamental Omaha rule: hole and board must be disjoint */
        return false;
    }

    /* Check for overlap with dead cards */
    if (mask_intersect(constraints->hole_cards, constraints->dead_cards) != MASK_EMPTY)
    {
        /* Dead cards cannot overlap with hole cards */
        return false;
    }
    if (mask_intersect(constraints->board_cards, constraints->dead_cards) != MASK_EMPTY)
    {
        /* Dead cards cannot overlap with board cards */
        return false;
    }

    /* Ensure all cards are valid (within 52-card deck bounds) */
    if (constraints->hole_cards >= (1ULL << 52) ||
        constraints->board_cards >= (1ULL << 52) ||
        constraints->dead_cards >= (1ULL << 52))
    {
        return false;
    }

    return true;
}

bool omaha_validate_config(const omaha_config_t *config)
{
    if (!config)
        return false;

    if (!omaha_validate_constraints(&config->constraints))
        return false;
    if (config->cache_size_mb > 1024)
        return false; /* Sanity limit */
    if (config->iteration > COMBO_ITER_BALANCED)
        return false;

    return true;
}

/* Generator lifecycle */
omaha_generator_t *omaha_generator_create(const omaha_config_t *config)
{
    if (!config || !omaha_validate_config(config))
    {
        return NULL;
    }

    omaha_generator_t *gen = calloc(1, sizeof(omaha_generator_t));
    if (!gen)
    {
        return NULL;
    }

    /* Copy configuration */
    gen->config = *config;

    /* Create hole generator: C(N,2) where N is 4, 5, or 6 */
    /* Note: combo_generator_create_simple handles the N automatically based on the input mask */
    gen->hole_gen = combo_generator_create_simple(config->constraints.hole_cards, 2);
    if (!gen->hole_gen)
    {
        free(gen);
        return NULL;
    }

    /* Create board generator: C(5,3) = 10 combinations */
    gen->board_gen = combo_generator_create_simple(config->constraints.board_cards, 3);
    if (!gen->board_gen)
    {
        combo_generator_destroy(gen->hole_gen);
        free(gen);
        return NULL;
    }

    /* Initialize state */
    gen->current_hole_combo = MASK_EMPTY;
    gen->current_board_combo = MASK_EMPTY;
    gen->hole_exhausted = false;
    gen->generator_exhausted = false;

    /* Initialize statistics */
    memset(&gen->stats, 0, sizeof(omaha_stats_t));

    /* Initialize cache if enabled */
    if (config->enable_cache && config->cache_size_mb > 0)
    {
        size_t cache_size = config->cache_size_mb * 1024 * 1024;
        gen->cache_data = calloc(1, cache_size);
        if (!gen->cache_data) {
            combo_generator_destroy(gen->hole_gen);
            combo_generator_destroy(gen->board_gen);
            free(gen);
            return NULL;
        }
        /* Cache implementation would go here */
    }

    /* Generate first hole combination */
    if (!combo_generator_next(gen->hole_gen, &gen->current_hole_combo))
    {
        /* Should never happen with valid hole cards */
        gen->hole_exhausted = true;
        gen->generator_exhausted = true;
    }
    else
    {
        /* Count the first hole combination */
        if (config->enable_stats)
        {
            gen->stats.hole_combinations++;
            /* First hole will also generate exactly C(5,3) = 10 board combinations */
            gen->stats.board_combinations += 10;
        }
    }

    return gen;
}

void omaha_generator_destroy(omaha_generator_t *gen)
{
    if (!gen)
        return;

    if (gen->hole_gen)
    {
        combo_generator_destroy(gen->hole_gen);
    }

    if (gen->board_gen)
    {
        combo_generator_destroy(gen->board_gen);
    }

    if (gen->cache_data)
    {
        free(gen->cache_data);
    }

    free(gen);
}

/* Core generation functions */
bool omaha_generator_next(omaha_generator_t *gen, mask_t *combination)
{
    if (!gen || !combination || gen->generator_exhausted)
    {
        return false;
    }

    for (;;)
    {
        mask_t board_combo;

        /* Try to get next board combination with current hole */
        if (combo_generator_next(gen->board_gen, &board_combo))
        {
            /* Combine current hole + this board */
            *combination = mask_union(gen->current_hole_combo, board_combo);

            if (gen->config.enable_stats)
            {
                gen->stats.total_combinations++;
            }

            return true;
        }

        /* Board generator exhausted for current hole - advance to next hole */
        if (!combo_generator_next(gen->hole_gen, &gen->current_hole_combo))
        {
            /* No more hole combinations - completely exhausted */
            gen->hole_exhausted = true;
            gen->generator_exhausted = true;
            return false;
        }

        /* Reset board generator for new hole combination */
        if (!combo_generator_reset(gen->board_gen))
        {
            gen->generator_exhausted = true;
            return false;
        }

        if (gen->config.enable_stats)
        {
            gen->stats.hole_combinations++;
            /* Each hole will generate exactly C(5,3) = 10 board combinations */
            gen->stats.board_combinations += 10;
        }

        /* Loop back to try board_gen again - this ensures we get the first board
         * of the new hole on the next iteration of the for loop */
    }
}

bool omaha_generator_has_next(const omaha_generator_t *gen)
{
    return gen && !gen->generator_exhausted;
}

uint64_t omaha_generator_total(const omaha_generator_t *gen)
{
    if (!gen)
        return 0;

    /* 
     * Total combinations depends on hole card count:
     * PLO4: C(4,2) × C(5,3) = 6 × 10 = 60
     * PLO5: C(5,2) × C(5,3) = 10 × 10 = 100
     * PLO6: C(6,2) × C(5,3) = 15 × 10 = 150
     */
    uint32_t hole_count = mask_popcount(gen->config.constraints.hole_cards);
    uint64_t hole_combos;
    
    switch (hole_count) {
        case 4: hole_combos = 6; break;
        case 5: hole_combos = 10; break;
        case 6: hole_combos = 15; break;
        default: return 0; // Should be caught by validation
    }
    
    return hole_combos * 10;
}

uint64_t omaha_generator_remaining(const omaha_generator_t *gen)
{
    if (!gen || gen->generator_exhausted)
        return 0;

    uint64_t total = omaha_generator_total(gen);
    return total > gen->stats.total_combinations ? total - gen->stats.total_combinations : 0;
}

bool omaha_generator_reset(omaha_generator_t *gen)
{
    if (!gen)
        return false;

    /* Reset both sub-generators */
    if (!combo_generator_reset(gen->hole_gen) ||
        !combo_generator_reset(gen->board_gen))
    {
        return false;
    }

    /* Reset state */
    gen->current_hole_combo = MASK_EMPTY;
    gen->current_board_combo = MASK_EMPTY;
    gen->hole_exhausted = false;
    gen->generator_exhausted = false;

    /* Reset statistics */
    if (gen->config.enable_stats)
    {
        gen->stats.hole_combinations = 0;
        gen->stats.board_combinations = 0;
        gen->stats.total_combinations = 0;
    }

    /* Generate first hole combination */
    if (!combo_generator_next(gen->hole_gen, &gen->current_hole_combo))
    {
        gen->hole_exhausted = true;
        gen->generator_exhausted = true;
        return false;
    }
    else
    {
        /* Count the first hole combination */
        if (gen->config.enable_stats)
        {
            gen->stats.hole_combinations++;
            /* First hole will also generate exactly C(5,3) = 10 board combinations */
            gen->stats.board_combinations += 10;
        }
    }

    return true;
}

/* Batch generation */
size_t omaha_generator_next_batch(omaha_generator_t *gen, mask_t *combinations,
                                  size_t max_count)
{
    if (!gen || !combinations || max_count == 0)
    {
        return 0;
    }

    size_t generated = 0;
    for (size_t i = 0; i < max_count; i++)
    {
        if (!omaha_generator_next(gen, &combinations[i]))
        {
            break;
        }
        generated++;
    }

    return generated;
}

/* Statistics and debugging */
void omaha_generator_get_stats(const omaha_generator_t *gen, omaha_stats_t *stats)
{
    if (!gen || !stats)
        return;
    *stats = gen->stats;
}

void omaha_generator_reset_stats(omaha_generator_t *gen)
{
    if (!gen)
        return;
    memset(&gen->stats, 0, sizeof(omaha_stats_t));
}

double omaha_generator_cache_hit_rate(const omaha_generator_t *gen)
{
    if (!gen || (gen->stats.cache_hits + gen->stats.cache_misses) == 0)
    {
        return 0.0;
    }

    return (double)gen->stats.cache_hits /
           (double)(gen->stats.cache_hits + gen->stats.cache_misses);
}

/* Performance testing */
bool omaha_generator_self_test(void)
{
    /* Test PLO4 */
    mask_t hole4 = string_to_mask("As Ah Kh Qh");     /* 4 hole cards */
    mask_t board = string_to_mask("Js Tc 9d 8s 7c"); /* 5 board cards */
    omaha_config_t config4 = omaha_config_plo(hole4, board, MASK_EMPTY);
    
    omaha_generator_t *gen4 = omaha_generator_create(&config4);
    if (!gen4) return false;
    
    if (omaha_generator_total(gen4) != 60) {
        omaha_generator_destroy(gen4);
        return false;
    }
    omaha_generator_destroy(gen4);

    /* Test PLO5 */
    mask_t hole5 = string_to_mask("As Ah Kh Qh 2c");  /* 5 hole cards */
    omaha_config_t config5 = omaha_config_plo5(hole5, board, MASK_EMPTY);
    
    omaha_generator_t *gen5 = omaha_generator_create(&config5);
    if (!gen5) return false;
    
    if (omaha_generator_total(gen5) != 100) {
        omaha_generator_destroy(gen5);
        return false;
    }
    omaha_generator_destroy(gen5);

    /* Test PLO6 */
    mask_t hole6 = string_to_mask("As Ah Kh Qh 2c 3d"); /* 6 hole cards */
    omaha_config_t config6 = omaha_config_plo6(hole6, board, MASK_EMPTY);
    
    omaha_generator_t *gen6 = omaha_generator_create(&config6);
    if (!gen6) return false;
    
    if (omaha_generator_total(gen6) != 150) {
        omaha_generator_destroy(gen6);
        return false;
    }
    omaha_generator_destroy(gen6);

    return true;
}

uint64_t omaha_generator_benchmark(uint32_t iterations)
{
    if (iterations == 0)
        return 0;

    mask_t hole = string_to_mask("As Ah Kh Qh");
    mask_t board = string_to_mask("Js Tc 9d 8s 7c");
    omaha_config_t config = omaha_config_plo(hole, board, MASK_EMPTY);

    /* Simple timing stub */
    uint64_t start_time = 0; /* Would use proper timing */

    for (uint32_t i = 0; i < iterations; i++)
    {
        omaha_generator_t *gen = omaha_generator_create(&config);
        if (gen)
        {
            mask_t combination;
            while (omaha_generator_next(gen, &combination))
            {
                /* Generate all combinations */
            }
            omaha_generator_destroy(gen);
        }
    }

    uint64_t end_time = start_time + iterations; /* Stub timing */
    return end_time - start_time;
}

/* Utility functions */
const char *omaha_config_to_string(const omaha_config_t *config, char *buffer, size_t size)
{
    if (!config || !buffer || size == 0)
    {
        return NULL;
    }

    snprintf(buffer, size,
             "OmahaConfig{hole=%llu, board=%llu, dead=%llu, cache=%s, stats=%s}",
             (unsigned long long)config->constraints.hole_cards,
             (unsigned long long)config->constraints.board_cards,
             (unsigned long long)config->constraints.dead_cards,
             config->enable_cache ? "ON" : "OFF",
             config->enable_stats ? "ON" : "OFF");

    return buffer;
}

void omaha_generator_print_stats(const omaha_generator_t *gen)
{
    if (!gen)
    {
        printf("NULL generator\n");
        return;
    }

    printf("Omaha Generator Statistics:\n");
    printf("  Hole combinations: %llu\n", (unsigned long long)gen->stats.hole_combinations);
    printf("  Board combinations: %llu\n", (unsigned long long)gen->stats.board_combinations);
    printf("  Total combinations: %llu\n", (unsigned long long)gen->stats.total_combinations);
    printf("  Cache hits: %llu\n", (unsigned long long)gen->stats.cache_hits);
    printf("  Cache misses: %llu\n", (unsigned long long)gen->stats.cache_misses);
    printf("  Validation failures: %llu\n", (unsigned long long)gen->stats.validation_failures);
    printf("  Cache hit rate: %.2f%%\n", omaha_generator_cache_hit_rate(gen) * 100.0);
}

/* Memory management */
size_t omaha_generator_memory_usage(const omaha_generator_t *gen)
{
    if (!gen)
        return 0;

    size_t total = sizeof(omaha_generator_t);
    total += combo_generator_memory_usage(gen->hole_gen);
    total += combo_generator_memory_usage(gen->board_gen);
    total += gen->cache_used;

    return total;
}

bool omaha_generator_optimize_memory(omaha_generator_t *gen)
{
    /* Stub for memory optimization */
    (void)gen;
    return true;
}

/* Integration with EvalContext */
size_t omaha_evaluate_all(omaha_generator_t *gen, const struct EvalContext *ctx,
                          uint32_t *results)
{
    if (!gen || !ctx || !results)
    {
        return 0;
    }

    if (!omaha_generator_reset(gen))
    {
        return 0;
    }

    size_t count = 0;
    mask_t combination;
    uint64_t max_combos = omaha_generator_total(gen);

    while (omaha_generator_next(gen, &combination))
    {
        results[count] = pe_eval_5c(ctx, combination);
        count++;

        /* Safety check */
        if (count >= max_combos)
        {
            break;
        }
    }

    return count;
}

uint32_t omaha_find_best(omaha_generator_t *gen, const struct EvalContext *ctx,
                         mask_t *best_combo)
{
    if (!gen || !ctx)
    {
        return EVAL_INVALID;
    }

    if (!omaha_generator_reset(gen))
    {
        return EVAL_INVALID;
    }

    uint32_t best_eval = EVAL_INVALID;
    mask_t best_combination = MASK_EMPTY;
    mask_t combination;

    while (omaha_generator_next(gen, &combination))
    {
        uint32_t eval_result = pe_eval_5c(ctx, combination);

        if (eval_result > best_eval)
        {
            best_eval = eval_result;
            best_combination = combination;
        }
    }

    if (best_combo)
    {
        *best_combo = best_combination;
    }

    return best_eval;
}
