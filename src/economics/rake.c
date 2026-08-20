/*
 * rake.c - Comprehensive rake and fee calculations for poker games
 */

#include <poker_eval/economics/rake.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ============================================================================
 * BASIC RAKE FUNCTIONS (backwards compatible)
 * ============================================================================ */

double pe_apply_rake(double pot, const rake_config_t *config)
{
    if (!config) return pot;
    if (pot < config->min_pot) return pot;

    double rake = pot * config->percentage;
    if (config->cap > 0.0 && rake > config->cap) {
        rake = config->cap;
    }

    return pot - rake;
}

double pe_apply_rake_excluding_uncalled(double pot,
                                        double uncalled,
                                        const rake_config_t *config)
{
    if (!isfinite(pot) || pot < 0.0)
        return 0.0;
    if (!isfinite(uncalled) || uncalled < 0.0)
        uncalled = 0.0;
    if (uncalled > pot)
        uncalled = pot;
    return uncalled + pe_apply_rake(pot - uncalled, config);
}

double pe_distribute_pot_with_rake(double pot,
                                   int num_winners,
                                   double *winnings,
                                   const rake_config_t *config)
{
    if (!winnings || num_winners <= 0) return 0.0;

    double raked_pot = pe_apply_rake(pot, config);
    double total_rake = pot - raked_pot;
    double share = raked_pot / num_winners;

    for (int i = 0; i < num_winners; ++i) {
        winnings[i] = share;
    }

    return total_rake;
}

/* ============================================================================
 * EXTENDED RAKE FUNCTIONS
 * ============================================================================ */

void pe_rake_config_init(rake_config_extended_t *config)
{
    if (!config) return;
    memset(config, 0, sizeof(*config));
    
    /* Default values - typical online poker rake */
    config->method = RAKE_METHOD_PERCENTAGE;
    config->percentage = 0.05;      /* 5% */
    config->cap = 3.0;              /* $3 cap */
    config->min_pot = 0.0;
    config->no_flop_no_drop = true;
    config->reduced_rake_headsup = true;
    config->headsup_cap = 1.0;      /* $1 cap HU */
    config->num_tiers = 0;
    config->time_collection = 0.0;
    config->time_period_minutes = 30;
    config->jackpot_contribution = 0.0;
    config->promo_contribution = 0.0;
    config->fixed_fee = 0.0;
    config->dead_button_adjustment = false;
    config->dead_button_reduction = 0.0;
    config->rake_increment = 0.01;
    config->round_down = true;
}

void pe_rake_config_for_stakes(rake_config_extended_t *config, double small_blind)
{
    pe_rake_config_init(config);
    
    /* Adjust based on stakes (typical online structure) */
    if (small_blind <= 0.05) {
        /* Micro stakes: 0.01/0.02 - 0.05/0.10 */
        config->percentage = 0.05;
        config->cap = 0.50;
        config->headsup_cap = 0.25;
    } else if (small_blind <= 0.25) {
        /* Low stakes: 0.10/0.25 - 0.25/0.50 */
        config->percentage = 0.05;
        config->cap = 1.00;
        config->headsup_cap = 0.50;
    } else if (small_blind <= 1.0) {
        /* Medium stakes: 0.50/1.00 - 1.00/2.00 */
        config->percentage = 0.05;
        config->cap = 3.00;
        config->headsup_cap = 1.00;
    } else if (small_blind <= 5.0) {
        /* Mid-high stakes: 2.00/4.00 - 5.00/10.00 */
        config->percentage = 0.045;
        config->cap = 4.00;
        config->headsup_cap = 2.00;
    } else {
        /* High stakes: 10/20+ */
        config->percentage = 0.04;
        config->cap = 5.00;
        config->headsup_cap = 2.50;
    }
}

int pe_rake_add_tier(rake_config_extended_t *config,
                     double pot_threshold,
                     double percentage,
                     double cap)
{
    if (!config || config->num_tiers >= RAKE_MAX_TIERS)
        return -1;
    
    int idx = config->num_tiers;
    config->tiers[idx].pot_threshold = pot_threshold;
    config->tiers[idx].percentage = percentage;
    config->tiers[idx].cap = cap;
    config->num_tiers++;
    
    return idx;
}

static double calculate_tiered_rake(double pot, const rake_config_extended_t *config)
{
    if (config->num_tiers == 0)
        return 0.0;
    
    double total_rake = 0.0;
    double prev_threshold = 0.0;
    
    for (int i = 0; i < config->num_tiers; i++) {
        const rake_tier_t *tier = &config->tiers[i];
        
        if (pot <= prev_threshold)
            break;
        
        double tier_amount;
        if (pot > tier->pot_threshold && tier->pot_threshold > 0) {
            tier_amount = tier->pot_threshold - prev_threshold;
        } else {
            tier_amount = pot - prev_threshold;
        }
        
        double tier_rake = tier_amount * tier->percentage;
        if (tier->cap > 0 && tier_rake > tier->cap) {
            tier_rake = tier->cap;
        }
        
        total_rake += tier_rake;
        prev_threshold = tier->pot_threshold;
        
        if (pot <= tier->pot_threshold)
            break;
    }
    
    /* Apply overall cap if set */
    if (config->cap > 0 && total_rake > config->cap) {
        total_rake = config->cap;
    }
    
    return total_rake;
}

int pe_calculate_rake_extended(double pot,
                               const rake_config_extended_t *config,
                               int num_players,
                               bool saw_flop,
                               bool is_headsup,
                               rake_result_t *result)
{
    if (!config || !result || pot < 0)
        return -1;
    
    memset(result, 0, sizeof(*result));
    
    /* No-flop-no-drop check */
    if (config->no_flop_no_drop && !saw_flop) {
        result->net_pot = pot;
        return 0;
    }
    
    /* Minimum pot check */
    if (pot < config->min_pot) {
        result->net_pot = pot;
        return 0;
    }
    
    double rake = 0.0;
    double cap = config->cap;
    
    /* Adjust cap for heads-up */
    if (config->reduced_rake_headsup && is_headsup && config->headsup_cap > 0) {
        cap = config->headsup_cap;
    }
    
    /* Calculate base rake based on method */
    switch (config->method) {
        case RAKE_METHOD_TIERED:
            rake = calculate_tiered_rake(pot, config);
            break;
            
        case RAKE_METHOD_FIXED_PER_HAND:
            rake = config->fixed_fee;
            break;
            
        case RAKE_METHOD_TIME:
            /* Time rake is calculated separately */
            rake = 0.0;
            break;
            
        case RAKE_METHOD_PERCENTAGE:
        default:
            rake = pot * config->percentage;
            break;
    }
    
    /* Apply cap */
    if (cap > 0 && rake > cap) {
        rake = cap;
    }
    
    /* Apply dead button adjustment */
    if (config->dead_button_adjustment && config->dead_button_reduction > 0) {
        /* This would typically be signaled by caller */
        /* rake *= (1.0 - config->dead_button_reduction); */
    }
    
    /* Round rake to increment */
    rake = pe_round_rake(rake, config->rake_increment, config->round_down);
    
    /* Calculate additional contributions */
    double jackpot = 0.0;
    double promo = 0.0;
    
    if (config->jackpot_contribution > 0 && saw_flop) {
        jackpot = pot * config->jackpot_contribution;
        jackpot = pe_round_rake(jackpot, config->rake_increment, config->round_down);
    }
    
    if (config->promo_contribution > 0 && saw_flop) {
        promo = pot * config->promo_contribution;
        promo = pe_round_rake(promo, config->rake_increment, config->round_down);
    }
    
    /* Populate result */
    result->total_rake = rake;
    result->jackpot_contribution = jackpot;
    result->promo_contribution = promo;
    result->net_pot = pot - rake - jackpot - promo;
    result->effective_percentage = pot > 0 ? (rake / pot) : 0.0;
    
    /* Ensure net pot is not negative */
    if (result->net_pot < 0) {
        result->net_pot = 0;
    }
    
    (void)num_players; /* May be used for player-count based adjustments */
    
    return 0;
}

double pe_apply_vip_discount(double rake, const vip_tier_t *vip)
{
    if (!vip || vip->level <= 0)
        return rake;
    
    double discount = rake * vip->rake_discount;
    return rake - discount;
}

double pe_calculate_rakeback(double total_rake, const vip_tier_t *vip)
{
    if (!vip || vip->rakeback <= 0)
        return 0.0;
    
    return total_rake * vip->rakeback;
}

double pe_calculate_time_rake(const rake_config_extended_t *config, int minutes_played)
{
    if (!config || config->method != RAKE_METHOD_TIME)
        return 0.0;
    
    if (config->time_period_minutes <= 0 || config->time_collection <= 0)
        return 0.0;
    
    int periods = minutes_played / config->time_period_minutes;
    return periods * config->time_collection;
}

double pe_calculate_tournament_rake(double pot, bool is_rebuy_period)
{
    (void)pot;
    (void)is_rebuy_period;
    
    /* Tournaments typically don't rake individual pots */
    /* Rake is taken from buy-ins instead */
    return 0.0;
}

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

double pe_round_rake(double rake, double increment, bool round_down)
{
    if (increment <= 0)
        return rake;
    
    if (round_down) {
        return floor(rake / increment) * increment;
    } else {
        return round(rake / increment) * increment;
    }
}

double pe_get_standard_cap(double big_blind)
{
    /* Standard online rake caps by stakes */
    if (big_blind <= 0.04) return 0.50;
    if (big_blind <= 0.10) return 0.50;
    if (big_blind <= 0.25) return 1.00;
    if (big_blind <= 0.50) return 1.50;
    if (big_blind <= 1.00) return 2.00;
    if (big_blind <= 2.00) return 3.00;
    if (big_blind <= 5.00) return 4.00;
    return 5.00;
}

double pe_get_standard_percentage(double big_blind)
{
    /* Standard percentages - higher stakes get slightly better rates */
    if (big_blind <= 1.00) return 0.05;   /* 5% */
    if (big_blind <= 5.00) return 0.045;  /* 4.5% */
    return 0.04;                          /* 4% */
}

void pe_rake_print_config(const rake_config_extended_t *config)
{
    if (!config) {
        printf("Rake config: NULL\n");
        return;
    }
    
    printf("=== Rake Configuration ===\n");
    printf("Method: %d\n", config->method);
    printf("Percentage: %.2f%%\n", config->percentage * 100);
    printf("Cap: %.2f\n", config->cap);
    printf("Min pot: %.2f\n", config->min_pot);
    printf("No-flop-no-drop: %s\n", config->no_flop_no_drop ? "Yes" : "No");
    printf("Reduced rake HU: %s (cap: %.2f)\n", 
           config->reduced_rake_headsup ? "Yes" : "No",
           config->headsup_cap);
    
    if (config->num_tiers > 0) {
        printf("Tiers (%d):\n", config->num_tiers);
        for (int i = 0; i < config->num_tiers; i++) {
            printf("  Tier %d: threshold=%.2f, rate=%.2f%%, cap=%.2f\n",
                   i + 1,
                   config->tiers[i].pot_threshold,
                   config->tiers[i].percentage * 100,
                   config->tiers[i].cap);
        }
    }
    
    if (config->jackpot_contribution > 0) {
        printf("Jackpot contribution: %.2f%%\n", config->jackpot_contribution * 100);
    }
    if (config->promo_contribution > 0) {
        printf("Promo contribution: %.2f%%\n", config->promo_contribution * 100);
    }
}

void pe_rake_print_result(const rake_result_t *result)
{
    if (!result) {
        printf("Rake result: NULL\n");
        return;
    }
    
    printf("=== Rake Result ===\n");
    printf("Total rake: %.2f\n", result->total_rake);
    printf("Jackpot contribution: %.2f\n", result->jackpot_contribution);
    printf("Promo contribution: %.2f\n", result->promo_contribution);
    printf("Net pot: %.2f\n", result->net_pot);
    printf("Effective rate: %.2f%%\n", result->effective_percentage * 100);
}
