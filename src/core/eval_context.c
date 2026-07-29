/*
 * eval_context.c - Thread-safe immutable evaluation context implementation
 */

#include <poker_eval/core/eval_context.h>
#include <poker_eval/core/modern_combinations.h>
#include <poker_eval/core/canonical_5card.h>
#include <poker_eval/core/combo_7to5.h>
#include <poker_eval/core/combo_7to5_hilo.h>
#include <poker_eval/core/eval_cache.h>
#include <poker_eval/core/eval_7c_simd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* EvalContext internal structure */
struct EvalContext
{
    EvalConfig config; /* Immutable configuration */

    /* Deck information */
    mask_t full_deck;   /* All cards in this deck type */
    uint32_t deck_size; /* Number of cards in deck */

    /* Lookup tables (immutable after creation) */
    uint32_t *lut_5c;   /* 5-card canonical LUT */
    size_t lut_5c_size; /* Size of 5-card LUT */

    /* Thread-safe statistics (atomic or use locks in real implementation) */
    eval_stats_t stats;

    /* Evaluation cache (thread-local, one per context) */
    eval_cache_t *eval_cache;
};

/* Default configurations */
EvalConfig eval_config_default(void)
{
    EvalConfig config = {
        .deck_type = EVAL_DECK_STANDARD,
        .rules = EVAL_RULES_HIGH,
        .mode = EVAL_MODE_EXACT,
        .monte_carlo_trials = 50000,
        .enable_cache = true,
        .enable_simd = true,
        .cache_size_mb = 16};
    return config;
}

EvalConfig eval_config_holdem(void)
{
    EvalConfig config = eval_config_default();
    config.rules = EVAL_RULES_HIGH;
    return config;
}

EvalConfig eval_config_omaha(void)
{
    EvalConfig config = eval_config_default();
    config.rules = EVAL_RULES_HIGH;
    return config;
}

EvalConfig eval_config_stud(void)
{
    EvalConfig config = eval_config_default();
    config.rules = EVAL_RULES_HIGH;
    return config;
}

EvalConfig eval_config_shortdeck(void)
{
    EvalConfig config = eval_config_default();
    config.deck_type = EVAL_DECK_SHORT;
    config.rules = EVAL_RULES_HIGH;
    return config;
}

/* Helper to get deck size */
static uint32_t get_deck_size(eval_deck_type_t deck_type)
{
    switch (deck_type)
    {
    case EVAL_DECK_STANDARD:
        return 52;
    case EVAL_DECK_JOKER:
        return 53;
    case EVAL_DECK_SHORT:
        return 36;
    case EVAL_DECK_ASTUD:
        return 32;
    case EVAL_DECK_MANILA:
        return 32;
    case EVAL_DECK_MAX:
    default:
        return 52;
    }
}

/* Helper to create full deck mask */
static mask_t create_deck_mask(eval_deck_type_t deck_type)
{
    mask_t deck = MASK_EMPTY;
    uint32_t deck_size = get_deck_size(deck_type);

    switch (deck_type)
    {
    case EVAL_DECK_STANDARD:
    case EVAL_DECK_JOKER:
        for (uint32_t i = 0; i < deck_size; i++)
        {
            deck = mask_set(deck, i);
        }
        break;

    case EVAL_DECK_SHORT:
        /* Short deck: 6-A only (remove 2-5) */
        for (int suit = 0; suit < 4; suit++)
        {
            for (int rank = 4; rank < 13; rank++)
            { /* 6=4, 7=5, ..., A=12 */
                deck = mask_set(deck, rank + 13 * suit);
            }
        }
        break;

    case EVAL_DECK_MANILA:
    case EVAL_DECK_ASTUD:
        /* Manila/Asian stud: 7-A only (remove 2-6) */
        for (int suit = 0; suit < 4; suit++)
        {
            for (int rank = 5; rank < 13; rank++)
            { /* 7=5, 8=6, ..., A=12 */
                deck = mask_set(deck, rank + 13 * suit);
            }
        }
        break;

    case EVAL_DECK_MAX:
    default:
        /* Fallback to standard */
        for (uint32_t i = 0; i < 52; i++)
        {
            deck = mask_set(deck, i);
        }
        break;
    }

    return deck;
}

/* Create canonical 5-card LUT */
static uint32_t *create_5c_lut(eval_deck_type_t deck_type, size_t *lut_size)
{
    return create_canonical_5c_lut(deck_type, lut_size);
}

/* Context creation and management */
EvalContext *eval_context_create(const EvalConfig *config)
{
    if (!config || !eval_context_validate_config(config))
    {
        return NULL;
    }

    EvalContext *ctx = calloc(1, sizeof(EvalContext));
    if (!ctx)
    {
        return NULL;
    }

    /* Copy configuration (immutable) */
    ctx->config = *config;

    /* Initialize deck */
    ctx->full_deck = create_deck_mask(config->deck_type);
    ctx->deck_size = get_deck_size(config->deck_type);

    /* Create 5-card LUT */
    ctx->lut_5c = create_5c_lut(config->deck_type, &ctx->lut_5c_size);
    if (!ctx->lut_5c)
    {
        free(ctx);
        return NULL;
    }

    /* Initialize statistics */
    memset(&ctx->stats, 0, sizeof(eval_stats_t));

    /* Initialize evaluation cache if enabled */
    if (config->enable_cache && config->cache_size_mb > 0)
    {
        size_t cache_entries = (config->cache_size_mb * 1024 * 1024) / sizeof(eval_cache_entry_t);
        // Ensure cache size is power of 2
        if (cache_entries < EVAL_CACHE_MIN_SIZE)
            cache_entries = EVAL_CACHE_MIN_SIZE;
        if (cache_entries > EVAL_CACHE_MAX_SIZE)
            cache_entries = EVAL_CACHE_MAX_SIZE;

        // Round down to nearest power of 2
        cache_entries = 1 << (63 - __builtin_clzll(cache_entries));

        ctx->eval_cache = eval_cache_create((uint32_t)cache_entries);
        if (!ctx->eval_cache)
        {
            free(ctx->lut_5c);
            free(ctx);
            return NULL;
        }
    }
    else
    {
        ctx->eval_cache = NULL;
    }

    return ctx;
}

void eval_context_destroy(EvalContext *ctx)
{
    if (!ctx)
        return;

    if (ctx->lut_5c)
    {
        free(ctx->lut_5c);
    }

    if (ctx->eval_cache)
    {
        eval_cache_destroy(ctx->eval_cache);
    }

    free(ctx);
}

const EvalConfig *eval_context_get_config(const EvalContext *ctx)
{
    return ctx ? &ctx->config : NULL;
}

struct eval_cache *eval_context_get_cache(const EvalContext *ctx)
{
    return ctx ? ctx->eval_cache : NULL;
}

/* Validation */
bool eval_context_validate_config(const EvalConfig *config)
{
    if (!config)
        return false;

    if (config->deck_type >= EVAL_DECK_MAX)
        return false;
    if (config->rules >= EVAL_RULES_MAX)
        return false;
    if (config->mode >= EVAL_MODE_MAX)
        return false;
    if (config->monte_carlo_trials < 1000)
        return false;
    if (config->cache_size_mb > 1024)
        return false; /* Sanity limit */

    return true;
}

bool eval_context_validate_mask(const EvalContext *ctx, mask_t hand)
{
    if (!ctx)
        return false;

    /* Check that hand only contains cards from this deck */
    if (!mask_is_subset(hand, ctx->full_deck))
        return false;

    /* Check minimum cards */
    if (mask_popcount(hand) < 5)
        return false;

    return true;
}

/* Core evaluation functions */

/**
 * Fast 5-card evaluation using canonical evaluator
 */
eval_t pe_eval_5c(const EvalContext *ctx, mask_t five_mask)
{
    if (!ctx || mask_popcount(five_mask) != 5)
    {
        return EVAL_INVALID;
    }

    if (!eval_context_validate_mask(ctx, five_mask))
    {
        return EVAL_INVALID;
    }

    /* Update statistics (would be atomic in thread-safe implementation) */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
    EvalContext *mutable_ctx = (EvalContext *)ctx;
#pragma GCC diagnostic pop
    mutable_ctx->stats.evaluations_5c++;
    mutable_ctx->stats.lut_lookups++;

    /* Use canonical 5-card evaluator */
    return canonical_5card_eval(five_mask, ctx->lut_5c, ctx->lut_5c_size);
}

/**
 * 7-card evaluation via optimized 7→5 enumeration using combo generator
 */
eval_t pe_eval_7c(const EvalContext *ctx, mask_t seven_mask)
{
    if (!ctx || mask_popcount(seven_mask) != 7)
    {
        return EVAL_INVALID;
    }

    if (!eval_context_validate_mask(ctx, seven_mask))
    {
        return EVAL_INVALID;
    }

    /* Update statistics */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
    EvalContext *mutable_ctx = (EvalContext *)ctx;
#pragma GCC diagnostic pop
    mutable_ctx->stats.evaluations_7c++;

    /* Optional fast-path: if no flush and no straight are possible,
     * compute best hand class from rank counts without enumeration.
     * This targets the common case and gives an immediate speedup.
     */
#define R13_MASK 0x1FFFu /* 13 bits */
    if (ctx->config.enable_simd)
    {
        /* Extract 13-bit rank masks per suit */
        uint32_t s0 = (uint32_t)((seven_mask >> 0) & R13_MASK);
        uint32_t s1 = (uint32_t)((seven_mask >> 13) & R13_MASK);
        uint32_t s2 = (uint32_t)((seven_mask >> 26) & R13_MASK);
        uint32_t s3 = (uint32_t)((seven_mask >> 39) & R13_MASK);

        /* Quick flush check: any suit has 5+ cards */
        int c0 = __builtin_popcount(s0);
        int c1 = __builtin_popcount(s1);
        int c2 = __builtin_popcount(s2);
        int c3 = __builtin_popcount(s3);
        bool possible_flush = (c0 >= 5) || (c1 >= 5) || (c2 >= 5) || (c3 >= 5);

        /* Quick straight check from unique rank mask */
        uint32_t ranks_any = (s0 | s1) | (s2 | s3);
        bool possible_straight = false;
        if (!possible_flush)
        {
            /* A-5 wheel */
            if ((ranks_any & (1u << 12)) && ((ranks_any & 0x0Fu) == 0x0Fu))
            {
                possible_straight = true;
            }
            else
            {
                for (int hi = 12; hi >= 4; --hi)
                {
                    uint32_t window = (ranks_any >> (hi - 4)) & 0x1Fu;
                    if (window == 0x1Fu)
                    {
                        possible_straight = true;
                        break;
                    }
                }
            }
        }

        if (!possible_flush && !possible_straight)
        {
            /* Compute counts per rank (vector-style aggregation across suits) */
            int cnt[13];
            for (int r = 0; r < 13; ++r)
            {
                cnt[r] = ((s0 >> r) & 1) + ((s1 >> r) & 1) + ((s2 >> r) & 1) + ((s3 >> r) & 1);
            }

            /* Derive best 5-card value from multiplicities only */
            int quad_rank = -1;
            int trips_ranks[2] = {-1, -1};
            int ntr = 0;
            int pair_ranks[3] = {-1, -1, -1};
            int np = 0;
            int single_ranks[7];
            int ns = 0;

            for (int r = 12; r >= 0; --r)
            {
                if (cnt[r] == 4)
                {
                    quad_rank = (quad_rank < 0 ? r : quad_rank);
                }
            }
            for (int r = 12; r >= 0; --r)
            {
                if (cnt[r] == 3 && ntr < 2)
                    trips_ranks[ntr++] = r;
            }
            for (int r = 12; r >= 0; --r)
            {
                if (cnt[r] == 2 && np < 3)
                    pair_ranks[np++] = r;
            }
            for (int r = 12; r >= 0; --r)
            {
                if (cnt[r] == 1 && ns < 7)
                    single_ranks[ns++] = r;
            }

            uint32_t base = 0, kick = 0;
            if (quad_rank >= 0)
            {
                base = EVAL_QUADS;
                kick = (uint32_t)quad_rank * 1000u;
                /* best kicker not part of quads */
                int best_k = -1;
                for (int r = 12; r >= 0; --r)
                    if (r != quad_rank && cnt[r] > 0)
                    {
                        best_k = r;
                        break;
                    }
                if (best_k >= 0)
                    kick += (uint32_t)best_k;
            }
            else if ((ntr >= 1 && np >= 1) || (ntr >= 2))
            {
                /* Full house from trip + pair or two trips */
                int trips_rank = trips_ranks[0];
                int pair_rank = (ntr >= 2) ? trips_ranks[1] : pair_ranks[0];
                base = EVAL_FULL_HOUSE;
                kick = (uint32_t)trips_rank * 1000u + (uint32_t)pair_rank;
            }
            else if (ntr >= 1)
            {
                base = EVAL_TRIPS;
                kick = (uint32_t)trips_ranks[0] * 10000u;
                int added = 0;
                for (int i = 0; i < ns && added < 2; ++i)
                {
                    kick += (uint32_t)single_ranks[i] * (added == 0 ? 100u : 1u);
                    ++added;
                }
            }
            else if (np >= 2)
            {
                base = EVAL_TWO_PAIR;
                kick = (uint32_t)pair_ranks[0] * 1000u + (uint32_t)pair_ranks[1] * 100u;
                int best_k = (ns > 0) ? single_ranks[0] : -1;
                if (best_k >= 0)
                    kick += (uint32_t)best_k;
            }
            else if (np == 1)
            {
                base = EVAL_PAIR;
                kick = (uint32_t)pair_ranks[0] * 10000u;
                int added = 0;
                for (int i = 0; i < ns && added < 3; ++i)
                {
                    kick += (uint32_t)single_ranks[i] * (added == 0 ? 1000u : (added == 1 ? 100u : 10u));
                    ++added;
                }
            }
            else
            {
                base = EVAL_HIGH_CARD;
                int added = 0;
                for (int i = 0; i < ns && added < 5; ++i)
                {
                    uint32_t mul = (added == 0 ? 10000u : added == 1 ? 1000u
                                                      : added == 2   ? 100u
                                                      : added == 3   ? 10u
                                                                     : 1u);
                    kick += (uint32_t)single_ranks[i] * mul;
                    ++added;
                }
            }

            return base + kick;
        }
    }

    /* SIMD path (in progress): try AVX2/AVX-512 accelerated 7→5 evaluation */
#if defined(HAS_AVX2) || defined(HAS_AVX512)
    if (ctx->config.enable_simd)
    {
        eval_t simd_eval = pe_eval_7c_simd(ctx, seven_mask);
        if (simd_eval != EVAL_INVALID)
        {
            return simd_eval;
        }
    }
#endif

    /* Use specialized 7→5 generator for optimal performance */
    combo_7to5_t *gen = combo_7to5_create(seven_mask);
    if (!gen)
    {
        return EVAL_INVALID;
    }

    eval_t best_eval = EVAL_INVALID;
    mask_t five_combo;

    /* Process all 21 combinations */
    while (combo_7to5_next(gen, &five_combo))
    {
        eval_t current_eval = pe_eval_5c(ctx, five_combo);

        if (current_eval > best_eval)
        {
            best_eval = current_eval;
        }

        mutable_ctx->stats.combinations_enum++;
    }

    combo_7to5_destroy(gen);
    return best_eval;
}

/**
 * N-card evaluation (adaptive)
 */
eval_t pe_eval_nc(const EvalContext *ctx, mask_t hand_mask)
{
    if (!ctx)
        return EVAL_INVALID;

    uint32_t card_count = mask_popcount(hand_mask);

    if (card_count < 5)
    {
        return EVAL_INVALID;
    }

    /* Update statistics */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
    EvalContext *mutable_ctx = (EvalContext *)ctx;
#pragma GCC diagnostic pop
    mutable_ctx->stats.evaluations_nc++;

    if (card_count == 5)
    {
        return pe_eval_5c(ctx, hand_mask);
    }
    else if (card_count == 7)
    {
        return pe_eval_7c(ctx, hand_mask);
    }
    else if (card_count <= 7)
    {
        /* Use combo generator for 6-card and other cases */
        combo_generator_t *gen = combo_generator_create_simple(hand_mask, 5);
        if (!gen)
        {
            return EVAL_INVALID;
        }

        eval_t best_eval = EVAL_INVALID;
        mask_t five_combo;

        while (combo_generator_next(gen, &five_combo))
        {
            eval_t current_eval = pe_eval_5c(ctx, five_combo);

            if (current_eval > best_eval)
            {
                best_eval = current_eval;
            }

            mutable_ctx->stats.combinations_enum++;
        }

        combo_generator_destroy(gen);
        return best_eval;
    }
    else
    {
        /* For N > 7, could use Monte Carlo if configured */
        if (ctx->config.mode == EVAL_MODE_MONTE_CARLO ||
            ctx->config.mode == EVAL_MODE_HYBRID)
        {

            /* Monte Carlo stub - would sample random 5-card subsets */
            uint32_t trials = ctx->config.monte_carlo_trials;
            eval_t best_eval = EVAL_INVALID;

            /* This is a simplified MC stub */
            for (uint32_t i = 0; i < trials && i < 1000; i++)
            {
                /* Would randomly sample 5 cards from hand_mask */
                /* For now, just use the best 5-card combination we can find */
                combo_generator_t *gen = combo_generator_create_simple(hand_mask, 5);
                if (gen)
                {
                    mask_t five_combo;
                    if (combo_generator_next(gen, &five_combo))
                    {
                        eval_t current_eval = pe_eval_5c(ctx, five_combo);
                        if (current_eval > best_eval)
                        {
                            best_eval = current_eval;
                        }
                    }
                    combo_generator_destroy(gen);
                }
            }

            return best_eval;
        }
        else
        {
            /* For very large hands (>7), use combo generator with first 5000 combinations */
            combo_generator_t *gen = combo_generator_create_simple(hand_mask, 5);
            if (!gen)
            {
                return EVAL_INVALID;
            }

            eval_t best_eval = EVAL_INVALID;
            mask_t five_combo;
            uint32_t max_combinations = 5000; /* Limit for performance */
            uint32_t count = 0;

            while (combo_generator_next(gen, &five_combo) && count < max_combinations)
            {
                eval_t current_eval = pe_eval_5c(ctx, five_combo);

                if (current_eval > best_eval)
                {
                    best_eval = current_eval;
                }

                mutable_ctx->stats.combinations_enum++;
                count++;
            }

            combo_generator_destroy(gen);
            return best_eval;
        }
    }
}

/* Hand strength classification */
hand_class_t eval_get_hand_class(eval_t eval_value)
{
    if (eval_value == EVAL_INVALID)
        return HAND_MAX;

    if (eval_value >= EVAL_STRAIGHT_FLUSH)
        return HAND_STRAIGHT_FLUSH;
    if (eval_value >= EVAL_QUADS)
        return HAND_FOUR_KIND;
    if (eval_value >= EVAL_FULL_HOUSE)
        return HAND_FULL_HOUSE;
    if (eval_value >= EVAL_FLUSH)
        return HAND_FLUSH;
    if (eval_value >= EVAL_STRAIGHT)
        return HAND_STRAIGHT;
    if (eval_value >= EVAL_TRIPS)
        return HAND_THREE_KIND;
    if (eval_value >= EVAL_TWO_PAIR)
        return HAND_TWO_PAIR;
    if (eval_value >= EVAL_PAIR)
        return HAND_PAIR;
    if (eval_value >= EVAL_HIGH_CARD)
        return HAND_HIGH_CARD;

    return HAND_MAX;
}

const char *eval_hand_class_name(hand_class_t hand_class)
{
    switch (hand_class)
    {
    case HAND_HIGH_CARD:
        return "High Card";
    case HAND_PAIR:
        return "Pair";
    case HAND_TWO_PAIR:
        return "Two Pair";
    case HAND_THREE_KIND:
        return "Three of a Kind";
    case HAND_STRAIGHT:
        return "Straight";
    case HAND_FLUSH:
        return "Flush";
    case HAND_FULL_HOUSE:
        return "Full House";
    case HAND_FOUR_KIND:
        return "Four of a Kind";
    case HAND_STRAIGHT_FLUSH:
        return "Straight Flush";
    case HAND_MAX:
    default:
        return "Unknown";
    }
}

/* Utility functions */
int eval_compare(eval_t a, eval_t b)
{
    if (a > b)
        return 1;
    if (a < b)
        return -1;
    return 0;
}

bool eval_is_valid(eval_t eval_value)
{
    return eval_value != EVAL_INVALID;
}

/* Statistics and debugging */
void eval_context_get_stats(const EvalContext *ctx, eval_stats_t *stats)
{
    if (!ctx || !stats)
        return;
    *stats = ctx->stats;
}

void eval_context_reset_stats(EvalContext *ctx)
{
    if (!ctx)
        return;
    memset(&ctx->stats, 0, sizeof(eval_stats_t));
}

double eval_context_cache_hit_rate(const EvalContext *ctx)
{
    if (!ctx || (ctx->stats.cache_hits + ctx->stats.cache_misses) == 0)
    {
        return 0.0;
    }

    return (double)ctx->stats.cache_hits /
           (double)(ctx->stats.cache_hits + ctx->stats.cache_misses);
}

/* Performance testing */
bool eval_context_self_test(const EvalContext *ctx)
{
    if (!ctx)
        return false;

    /* Test basic functionality */
    mask_t test_hand = string_to_mask("As Kh Qd Jc Ts");
    eval_t result = pe_eval_5c(ctx, test_hand);

    return eval_is_valid(result);
}

uint64_t eval_context_benchmark_5c(const EvalContext *ctx, uint32_t iterations)
{
    if (!ctx || iterations == 0)
        return 0;

    mask_t test_hand = string_to_mask("As Kh Qd Jc Ts");

    /* Simple timing measurement */
    uint64_t start_time = 0; /* Would use proper timing */

    for (uint32_t i = 0; i < iterations; i++)
    {
        pe_eval_5c(ctx, test_hand);
    }

    uint64_t end_time = start_time + iterations; /* Stub timing */
    return end_time - start_time;
}

uint64_t eval_context_benchmark_7c(const EvalContext *ctx, uint32_t iterations)
{
    if (!ctx || iterations == 0)
        return 0;

    mask_t test_hand = string_to_mask("As Kh Qd Jc Ts 9h 8s");

    uint64_t start_time = 0; /* Would use proper timing */

    for (uint32_t i = 0; i < iterations; i++)
    {
        pe_eval_7c(ctx, test_hand);
    }

    uint64_t end_time = start_time + iterations; /* Stub timing */
    return end_time - start_time;
}

/* Memory management */
size_t eval_context_memory_usage(const EvalContext *ctx)
{
    if (!ctx)
        return 0;

    size_t total = sizeof(EvalContext);
    total += ctx->lut_5c_size * sizeof(uint32_t);

    // Add cache memory usage if cache exists
    if (ctx->eval_cache)
    {
        // Approximate cache memory usage - eval_cache structure + entries
        total += sizeof(eval_cache_t) + ctx->config.cache_size_mb * 1024 * 1024;
    }

    return total;
}

bool eval_context_optimize_memory(EvalContext *ctx)
{
    /* Stub for memory optimization */
    (void)ctx;
    return true;
}

void eval_context_add_combinations(const EvalContext *ctx, uint32_t n)
{
    if (!ctx || n == 0)
        return;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
    EvalContext *mctx = (EvalContext *)ctx;
#pragma GCC diagnostic pop
    mctx->stats.combinations_enum += n;
}
/* Public API declared in include/poker_eval/core/eval_context.h */
bool pe_eval_7c_hilo(const EvalContext *ctx, mask_t seven_mask, eval_t *hi_out, eval_t *lo_out, bool *lo_qual_out)
{
    if (!ctx || mask_popcount(seven_mask) != 7 || !hi_out || !lo_out || !lo_qual_out)
        return false;
    if (!eval_context_validate_mask(ctx, seven_mask))
        return false;
    combo_7to5_hilo_t *gen = combo_7to5_hilo_create(seven_mask, false);
    if (!gen)
        return false;
    eval_t best_hi = EVAL_INVALID;
    bool any_low = false;
    eval_t best_lo = EVAL_INVALID;
    mask_t five;
    hilo_result_t res;
    while (combo_7to5_hilo_next(gen, &five, &res))
    {
        if (res.hi_value > best_hi)
            best_hi = res.hi_value;
        if (res.has_low)
        {
            if (!any_low || res.lo_value < best_lo)
            {
                best_lo = res.lo_value;
                any_low = true;
            }
        }
    }
    combo_7to5_hilo_destroy(gen);
    *hi_out = best_hi;
    *lo_out = any_low ? best_lo : EVAL_INVALID;
    *lo_qual_out = any_low;
    return (best_hi != EVAL_INVALID);
}
