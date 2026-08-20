/*
 * rake.h - Comprehensive rake and fee calculations for poker games
 *
 * Supports:
 * - Percentage-based rake with cap
 * - No-flop-no-drop rules
 * - Time-based collection (time rake)
 * - Dead button rake adjustment
 * - Promotional rake reductions
 * - Jackpot/bad beat contributions
 * - VIP tier-based rake discounts
 */

#ifndef POKER_EVAL_ECONOMICS_RAKE_H
#define POKER_EVAL_ECONOMICS_RAKE_H

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/utils/pokereval_export.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum rake tiers for progressive/tiered structures */
#define RAKE_MAX_TIERS 10
#define RAKE_MAX_VIP_LEVELS 10

/* Rake calculation methods */
typedef enum {
    RAKE_METHOD_PERCENTAGE = 0,   /* Standard percentage of pot */
    RAKE_METHOD_TIME,             /* Time-based collection */
    RAKE_METHOD_FIXED_PER_HAND,   /* Fixed amount per hand */
    RAKE_METHOD_TIERED            /* Progressive tiers based on pot size */
} rake_method_t;

/* Rake tier for progressive structures */
typedef struct {
    double pot_threshold;         /* Pot size threshold for this tier */
    double percentage;            /* Rake % for this tier */
    double cap;                   /* Cap for this tier */
} rake_tier_t;

/* VIP/loyalty tier for rake discounts */
typedef struct {
    int level;                    /* VIP level (0 = no VIP) */
    double rake_discount;         /* Discount multiplier (0.0-1.0, e.g., 0.1 = 10% off) */
    double rakeback;              /* Rakeback percentage (0.0-1.0) */
} vip_tier_t;

/* Basic rake configuration (backwards compatible) */
typedef struct {
    double percentage;            /* e.g. 0.05 for 5% */
    double cap;                   /* Maximum rake amount */
    double min_pot;               /* Minimum pot size to apply rake */
    int no_flop_no_drop;          /* If 1, rake is 0 if pot doesn't reach flop */
} rake_config_t;

/* Extended rake configuration */
typedef struct {
    /* Basic configuration */
    rake_method_t method;
    double percentage;            /* Base percentage (0.0-1.0) */
    double cap;                   /* Maximum rake per hand */
    double min_pot;               /* Minimum pot for rake */
    
    /* No-flop-no-drop rules */
    bool no_flop_no_drop;         /* No rake if hand ends preflop */
    bool reduced_rake_headsup;    /* Reduced rake for heads-up pots */
    double headsup_cap;           /* Lower cap for heads-up */
    
    /* Tiered rake structure */
    rake_tier_t tiers[RAKE_MAX_TIERS];
    int num_tiers;
    
    /* Time-based rake (alternative to pot rake) */
    double time_collection;       /* Amount per time period */
    int time_period_minutes;      /* Collection period (e.g., 30 min) */
    
    /* Additional fees */
    double jackpot_contribution;  /* % of pot for jackpot/bad beat (0.0-1.0) */
    double promo_contribution;    /* % for promotional fund */
    double fixed_fee;             /* Fixed fee per hand (in addition to rake) */
    
    /* Dead button handling */
    bool dead_button_adjustment;  /* Adjust rake when button is dead */
    double dead_button_reduction; /* Reduction factor for dead button */
    
    /* Currency/denomination */
    double rake_increment;        /* Minimum rake increment (e.g., 0.01) */
    bool round_down;              /* Round rake down to increment */
    
} rake_config_extended_t;

/* Rake calculation result */
typedef struct {
    double total_rake;            /* Total rake taken */
    double jackpot_contribution;  /* Jackpot/bad beat amount */
    double promo_contribution;    /* Promotional fund amount */
    double net_pot;               /* Pot after all deductions */
    double effective_percentage;  /* Actual rake % of original pot */
} rake_result_t;

/* ============================================================================
 * BASIC RAKE FUNCTIONS (backwards compatible)
 * ============================================================================ */

/**
 * Apply rake to a total pot amount (basic version)
 * @param pot Total pot before rake
 * @param config Rake configuration
 * @return Pot after rake deduction
 */
POKEREVAL_EXPORT double pe_apply_rake(double pot, const rake_config_t *config);

/**
 * Apply rake only to the called portion of a pot.
 *
 * @param pot Total contributions in the pot.
 * @param uncalled Amount of the last unmatched wager to return without rake.
 * @return Net pot including the returned uncalled amount.
 */
POKEREVAL_EXPORT double pe_apply_rake_excluding_uncalled(
    double pot,
    double uncalled,
    const rake_config_t *config);

/**
 * Distribute pot after rake to winners (basic version)
 * @param pot Total pot before rake
 * @param num_winners Number of winners
 * @param winnings Output array for each winner's share
 * @param config Rake configuration
 * @return Total rake taken
 */
POKEREVAL_EXPORT double pe_distribute_pot_with_rake(
    double pot,
    int num_winners,
    double *winnings,
    const rake_config_t *config);

/* ============================================================================
 * EXTENDED RAKE FUNCTIONS
 * ============================================================================ */

/**
 * Initialize extended rake configuration with defaults
 */
POKEREVAL_EXPORT void pe_rake_config_init(rake_config_extended_t *config);

/**
 * Create standard rake configuration for stakes
 * @param config Output configuration
 * @param small_blind Small blind amount (determines rake structure)
 */
POKEREVAL_EXPORT void pe_rake_config_for_stakes(
    rake_config_extended_t *config,
    double small_blind);

/**
 * Add a tier to tiered rake structure
 * @return tier index or -1 on error
 */
POKEREVAL_EXPORT int pe_rake_add_tier(
    rake_config_extended_t *config,
    double pot_threshold,
    double percentage,
    double cap);

/**
 * Calculate rake with extended configuration
 * @param pot Total pot
 * @param config Extended configuration
 * @param num_players Number of players in hand
 * @param saw_flop Whether hand reached flop
 * @param is_headsup Whether pot is heads-up
 * @param result Output result structure
 * @return 0 on success, -1 on error
 */
POKEREVAL_EXPORT int pe_calculate_rake_extended(
    double pot,
    const rake_config_extended_t *config,
    int num_players,
    bool saw_flop,
    bool is_headsup,
    rake_result_t *result);

/**
 * Apply VIP discount to rake
 * @param rake Original rake amount
 * @param vip VIP tier configuration
 * @return Rake after discount
 */
POKEREVAL_EXPORT double pe_apply_vip_discount(
    double rake,
    const vip_tier_t *vip);

/**
 * Calculate rakeback amount
 * @param total_rake Total rake paid
 * @param vip VIP tier configuration
 * @return Rakeback amount
 */
POKEREVAL_EXPORT double pe_calculate_rakeback(
    double total_rake,
    const vip_tier_t *vip);

/**
 * Calculate time-based rake collection
 * @param config Extended configuration
 * @param minutes_played Time played in minutes
 * @return Collection amount due
 */
POKEREVAL_EXPORT double pe_calculate_time_rake(
    const rake_config_extended_t *config,
    int minutes_played);

/**
 * Calculate rake for tournament hand (usually no rake during play)
 * @param pot Pot amount
 * @param is_rebuy_period Whether in rebuy period
 * @return Rake amount (usually 0 for tournaments)
 */
POKEREVAL_EXPORT double pe_calculate_tournament_rake(
    double pot,
    bool is_rebuy_period);

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

/**
 * Round rake to specified increment
 */
POKEREVAL_EXPORT double pe_round_rake(
    double rake,
    double increment,
    bool round_down);

/**
 * Get rake cap for specific stakes
 */
POKEREVAL_EXPORT double pe_get_standard_cap(double big_blind);

/**
 * Get standard rake percentage for stakes
 */
POKEREVAL_EXPORT double pe_get_standard_percentage(double big_blind);

/**
 * Print rake configuration (debug)
 */
POKEREVAL_EXPORT void pe_rake_print_config(const rake_config_extended_t *config);

/**
 * Print rake result (debug)
 */
POKEREVAL_EXPORT void pe_rake_print_result(const rake_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_ECONOMICS_RAKE_H */
