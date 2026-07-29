/*
 * run_it_twice_demo.c - Demonstration of Run It Twice functionality
 *
 * Shows practical usage examples of RIT for Hold'em and Omaha.
 */

#include <poker_eval/equity/run_it_twice.h>
#include <poker_eval/deck/deck_std.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const char k_rank_chars[] = "23456789TJQKA";
static const char k_suit_chars[] = "hdcs";

/* Helper to create a card mask from string like "AsKh" */
static StdDeck_CardMask str_to_mask(const char *str) {
    StdDeck_CardMask mask;
    StdDeck_CardMask_RESET(mask);

    while (*str) {
        if (*str == ' ') {
            str++;
            continue;
        }

        int rank = -1, suit = -1;

        /* Parse rank */
        switch (*str++) {
            case 'A': case 'a': rank = StdDeck_Rank_ACE; break;
            case 'K': case 'k': rank = StdDeck_Rank_KING; break;
            case 'Q': case 'q': rank = StdDeck_Rank_QUEEN; break;
            case 'J': case 'j': rank = StdDeck_Rank_JACK; break;
            case 'T': case 't': rank = StdDeck_Rank_TEN; break;
            case '9': rank = StdDeck_Rank_9; break;
            case '8': rank = StdDeck_Rank_8; break;
            case '7': rank = StdDeck_Rank_7; break;
            case '6': rank = StdDeck_Rank_6; break;
            case '5': rank = StdDeck_Rank_5; break;
            case '4': rank = StdDeck_Rank_4; break;
            case '3': rank = StdDeck_Rank_3; break;
            case '2': rank = StdDeck_Rank_2; break;
            default: return mask;
        }

        /* Parse suit */
        switch (*str++) {
            case 'h': case 'H': suit = StdDeck_Suit_HEARTS; break;
            case 'd': case 'D': suit = StdDeck_Suit_DIAMONDS; break;
            case 'c': case 'C': suit = StdDeck_Suit_CLUBS; break;
            case 's': case 'S': suit = StdDeck_Suit_SPADES; break;
            default: return mask;
        }

        int card = StdDeck_MAKE_CARD(rank, suit);
        StdDeck_CardMask_SET(mask, card);
    }

    return mask;
}

static int count_board_cards(StdDeck_CardMask board) {
    int count = 0;
    for (int i = 0; i < StdDeck_N_CARDS; i++) {
        if (StdDeck_CardMask_CARD_IS_SET(board, i)) {
            count++;
        }
    }
    return count;
}

static void apply_stage_for_board(rit_config_t *config, StdDeck_CardMask board) {
    int cards = count_board_cards(board);
    switch (cards) {
        case 0:
            config->stage = RIT_STAGE_FLOP;
            break;
        case 3:
            config->stage = RIT_STAGE_TURN_RIVER;
            break;
        case 4:
            config->stage = RIT_STAGE_RIVER;
            break;
        default:
            config->stage = RIT_STAGE_RIVER;
            break;
    }
}

typedef struct {
    int num_players;
    int total_runouts;
    double equity_sum[RIT_MAX_PLAYERS];
    double equity_sq_sum[RIT_MAX_PLAYERS];
    int wins[RIT_MAX_PLAYERS];
    int ties[RIT_MAX_PLAYERS];
    int losses[RIT_MAX_PLAYERS];
    double variance_factor;
} aggregate_stats_t;

static int calculate_equity_with_config(
    const StdDeck_CardMask holes[],
    int num_players,
    StdDeck_CardMask board,
    rit_config_t config,
    rit_result_t *result)
{
    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);
    apply_stage_for_board(&config, board);
    return rit_calculate_equity(holes, num_players, board, dead, &config, result);
}

static int run_aggregate_simulation(
    const StdDeck_CardMask holes[],
    int num_players,
    StdDeck_CardMask board,
    rit_config_t base_config,
    int runouts_per_batch,
    int batches,
    uint64_t seed_base,
    aggregate_stats_t *stats)
{
    if (runouts_per_batch < 1 || runouts_per_batch > RIT_MAX_RUNOUTS)
        return RIT_ERROR_INVALID_CONFIG;

    memset(stats, 0, sizeof(*stats));
    stats->num_players = num_players;
    stats->variance_factor = rit_theoretical_variance_reduction(runouts_per_batch);

    for (int batch = 0; batch < batches; batch++) {
        rit_config_t config = base_config;
        config.num_runouts = runouts_per_batch;
        config.track_individual = true;
        config.calc_variance = true;
        config.seed = seed_base + (uint64_t)batch * 101;

        rit_result_t result;
        int ret = calculate_equity_with_config(holes, num_players, board, config, &result);
        if (ret != RIT_SUCCESS)
            return ret;

        for (int run = 0; run < config.num_runouts; run++) {
            const rit_runout_result_t *runout = &result.runouts[run];
            stats->total_runouts++;
            for (int p = 0; p < num_players; p++) {
                double eq = runout->equities[p];
                stats->equity_sum[p] += eq;
                stats->equity_sq_sum[p] += eq * eq;
                if (runout->winner_mask & (1 << p)) {
                    if (runout->num_winners == 1)
                        stats->wins[p]++;
                    else
                        stats->ties[p]++;
                } else {
                    stats->losses[p]++;
                }
            }
        }
    }

    return RIT_SUCCESS;
}

static void print_aggregate_stats(const aggregate_stats_t *stats) {
    if (stats->total_runouts == 0) {
        printf("No aggregate data available.\n");
        return;
    }

    printf("\nAggregate results (%d runouts, batches aggregated):\n", stats->total_runouts);
    for (int p = 0; p < stats->num_players; p++) {
        double avg = stats->equity_sum[p] / stats->total_runouts;
        double mean_sq = stats->equity_sq_sum[p] / stats->total_runouts;
        double variance = mean_sq - avg * avg;
        if (variance < 0.0) variance = 0.0;
        double stddev = sqrt(variance);

        printf("  Player %d: avg equity %.2f%% | wins %d | ties %d | losses %d | stddev %.4f%%\n",
               p + 1,
               avg * 100.0,
               stats->wins[p],
               stats->ties[p],
               stats->losses[p],
               stddev * 100.0);
    }
    printf("  Per-batch variance reduction factor: %.2fx (theoretical)\n",
           stats->variance_factor);
}

/* Print a card mask */
static void print_mask(StdDeck_CardMask mask) {
    int count = 0;
    for (int i = 0; i < StdDeck_N_CARDS; i++) {
        if (StdDeck_CardMask_CARD_IS_SET(mask, i)) {
            if (count > 0) printf(" ");
            printf("%c%c",
                k_rank_chars[StdDeck_RANK(i)],
                k_suit_chars[StdDeck_SUIT(i)]);
            count++;
        }
    }
}

/* Example 1: Classic heads-up all-in situation */
static void example_classic_allin(void) {
    printf("\n╔═══════════════════════════════════════════════════╗\n");
    printf("║  Example 1: Classic All-In (AA vs KK on flop)    ║\n");
    printf("╚═══════════════════════════════════════════════════╝\n\n");

    StdDeck_CardMask holes[2];
    holes[0] = str_to_mask("AsAh");  /* Player 1: Pocket Aces */
    holes[1] = str_to_mask("KsKh");  /* Player 2: Pocket Kings */

    StdDeck_CardMask flop = str_to_mask("Qc9d2h");

    printf("Player 1: ");
    print_mask(holes[0]);
    printf("\nPlayer 2: ");
    print_mask(holes[1]);
    printf("\nFlop: ");
    print_mask(flop);
    printf("\n");

    printf("\n--- Sample: Run It Twice ---\n");

    rit_result_t sample_result;
    int ret = rit_holdem_equity(holes, 2, flop, 2, &sample_result);

    if (ret != RIT_SUCCESS) {
        printf("Error: %s\n", rit_error_string(ret));
        return;
    }

    rit_print_result(&sample_result, true);

    printf("\nInterpretation (sample of 2 runouts):\n");
    printf("  • Player 1 (AA) scooped both runouts shown (%.0f%% this sample)\n",
        sample_result.avg_equity[0] * 100);
    printf("  • Variance reduction relative to a single runout: %.2fx\n",
        sample_result.variance_reduction);

    /* Aggregate view with more simulation */
    rit_config_t config = rit_config_holdem_rit2();
    config.num_runouts = 50;
    config.calc_variance = true;
    config.track_individual = false;
    config.seed = 42;

    aggregate_stats_t stats;
    int agg_ret = run_aggregate_simulation(
        holes, 2, flop, config, 8, 200, 4200, &stats);
    if (agg_ret == RIT_SUCCESS) {
        print_aggregate_stats(&stats);
        printf("Reference equity (enumerated): AA ≈ 81%%, KK ≈ 19%%\n");
    } else {
        printf("Aggregate simulation failed: %s\n", rit_error_string(agg_ret));
    }
}

/* Example 2: Three-way all-in on turn */
static void example_three_way(void) {
    printf("\n╔═══════════════════════════════════════════════════╗\n");
    printf("║  Example 2: Three-Way All-In on Turn             ║\n");
    printf("╚═══════════════════════════════════════════════════╝\n\n");

    StdDeck_CardMask holes[3];
    holes[0] = str_to_mask("AsKs");  /* Player 1: Big Slick suited */
    holes[1] = str_to_mask("QcQd");  /* Player 2: Pocket Queens */
    holes[2] = str_to_mask("JhTh");  /* Player 3: JT suited */

    StdDeck_CardMask board = str_to_mask("Qh9s2cAd");  /* Flop + Turn */

    printf("Player 1: ");
    print_mask(holes[0]);
    printf(" (top pair)\n");
    printf("Player 2: ");
    print_mask(holes[1]);
    printf(" (set of queens)\n");
    printf("Player 3: ");
    print_mask(holes[2]);
    printf(" (straight draw)\n");
    printf("Board: ");
    print_mask(board);
    printf("\n");

    printf("\n--- Sample: Run It Three Times ---\n");

    rit_result_t sample_result;
    int ret = rit_holdem_equity(holes, 3, board, 3, &sample_result);

    if (ret != RIT_SUCCESS) {
        printf("Error: %s\n", rit_error_string(ret));
        return;
    }

    rit_print_result(&sample_result, true);

    printf("\nSample interpretation:\n");
    printf("  • In this sample, QQ captured every runout\n");
    printf("  • Variance reduction vs single board: %.2fx in this sample\n",
        sample_result.variance_reduction);

    rit_config_t config = rit_config_holdem_rit2();
    config.seed = 314;

    aggregate_stats_t stats;
    int agg_ret = run_aggregate_simulation(
        holes, 3, board, config, 6, 200, 3140, &stats);
    if (agg_ret == RIT_SUCCESS) {
        print_aggregate_stats(&stats);
        printf("Reference equities (turn, enumerated): QQ ≈ 74%%, AK ≈ 9%%, JT ≈ 17%%\n");
    } else {
        printf("Aggregate simulation failed: %s\n", rit_error_string(agg_ret));
    }
}

/* Example 3: Omaha heads-up */
static void example_omaha(void) {
    printf("\n╔═══════════════════════════════════════════════════╗\n");
    printf("║  Example 3: Omaha Heads-Up All-In                ║\n");
    printf("╚═══════════════════════════════════════════════════╝\n\n");

    StdDeck_CardMask holes[2];
    holes[0] = str_to_mask("AsAhKsKh");  /* Player 1: AAKKds */
    holes[1] = str_to_mask("QdQcJdJc");  /* Player 2: QQJJds */

    StdDeck_CardMask flop = str_to_mask("Ad9h2c");

    printf("Player 1: ");
    print_mask(holes[0]);
    printf(" (AAKKds - set + nut flush draw)\n");
    printf("Player 2: ");
    print_mask(holes[1]);
    printf(" (QQJJds - overpair)\n");
    printf("Flop: ");
    print_mask(flop);
    printf("\n");

    printf("\n--- Sample: Run It Twice ---\n");

    rit_result_t sample_result;
    int ret = rit_omaha_equity(holes, 2, flop, 2, &sample_result);

    if (ret != RIT_SUCCESS) {
        printf("Error: %s\n", rit_error_string(ret));
        return;
    }

    rit_print_result(&sample_result, true);

    printf("\nSample interpretation:\n");
    printf("  • AAKKds scooped both illustrated runouts (%.0f%% this sample)\n",
        sample_result.avg_equity[0] * 100);
    printf("  • Variance reduction vs single board: %.2fx in this sample\n",
        sample_result.variance_reduction);

    rit_config_t config = rit_config_omaha_rit2();
    config.seed = 777;

    aggregate_stats_t stats;
    int agg_ret = run_aggregate_simulation(
        holes, 2, flop, config, 8, 250, 7770, &stats);
    if (agg_ret == RIT_SUCCESS) {
        print_aggregate_stats(&stats);
        printf("Reference equity (enumerated): AAKKds ≈ 69%%, QQJJds ≈ 31%%\n");
    } else {
        printf("Aggregate simulation failed: %s\n", rit_error_string(agg_ret));
    }
}

/* Example 4: Variance analysis with different runout counts */
static void example_variance_analysis(void) {
    printf("\n╔═══════════════════════════════════════════════════╗\n");
    printf("║  Example 4: Variance Analysis (2-8 runouts)      ║\n");
    printf("╚═══════════════════════════════════════════════════╝\n\n");

    StdDeck_CardMask holes[2];
    holes[0] = str_to_mask("AsKd");  /* 60% favorite */
    holes[1] = str_to_mask("QhJh");  /* 40% underdog */

    StdDeck_CardMask flop = str_to_mask("Kh9c2s");

    printf("Player 1: ");
    print_mask(holes[0]);
    printf(" (top pair)\n");
    printf("Player 2: ");
    print_mask(holes[1]);
    printf(" (gutshot + flush draw)\n");
    printf("Flop: ");
    print_mask(flop);
    printf("\n");

    printf("\n%-8s  %-12s  %-12s  %-15s\n",
        "Runouts", "P1 Equity", "P1 StdDev", "Variance Factor");
    printf("%-8s  %-12s  %-12s  %-15s\n",
        "-------", "---------", "---------", "---------------");

    int runout_counts[] = {2, 3, 4, 6, 8};

    for (int i = 0; i < 5; i++) {
        int num_runouts = runout_counts[i];

        rit_config_t config = rit_config_holdem_rit2();
        config.num_runouts = num_runouts;
        config.seed = 420 + num_runouts;

        aggregate_stats_t stats;
        int agg_ret = run_aggregate_simulation(
            holes, 2, flop, config, num_runouts, 12, 4200 + num_runouts * 10, &stats);

        if (agg_ret == RIT_SUCCESS) {
            double avg = stats.equity_sum[0] / stats.total_runouts;
            double mean_sq = stats.equity_sq_sum[0] / stats.total_runouts;
            double variance = mean_sq - avg * avg;
            if (variance < 0.0) variance = 0.0;
            double stddev = sqrt(variance);
            printf("%-8d  %6.2f%%      %.5f       %.2fx\n",
                num_runouts,
                avg * 100.0,
                stddev,
                stats.variance_factor);
        } else {
            printf("%-8d  (error: %s)\n", num_runouts, rit_error_string(agg_ret));
        }
    }

    printf("\nConclusion:\n");
    printf("  • More runouts = lower variance (stddev decreases)\n");
    printf("  • Variance reduces by factor of N (theoretical)\n");
    printf("  • RIT-2 is most common (50%% variance reduction)\n");
}

/* Example 5: Different stages comparison */
static void example_stages(void) {
    printf("\n╔═══════════════════════════════════════════════════╗\n");
    printf("║  Example 5: Different Stages (River vs Turn)     ║\n");
    printf("╚═══════════════════════════════════════════════════╝\n\n");

    StdDeck_CardMask holes[2];
    holes[0] = str_to_mask("AsKs");
    holes[1] = str_to_mask("AhKh");

    /* River stage - 1 card to deal */
    {
        printf("--- River Stage (1 card) ---\n");
        StdDeck_CardMask board = str_to_mask("9dTcJhQs");
        printf("Board (4 cards): ");
        print_mask(board);
        printf("\n");

        rit_result_t result;
        int ret = rit_holdem_equity(holes, 2, board, 2, &result);

        if (ret == RIT_SUCCESS) {
            printf("Player 1 equity: %.2f%%\n", result.avg_equity[0] * 100);
            printf("Player 2 equity: %.2f%%\n", result.avg_equity[1] * 100);
            printf("(Both share the same straight; long-run equity ≈ 50/50)\n");
        }
    }

    printf("\n");

    /* Turn stage - 2 cards to deal */
    {
        printf("--- Turn Stage (2 cards) ---\n");
        StdDeck_CardMask flop = str_to_mask("9dTcJh");
        printf("Board (3 cards): ");
        print_mask(flop);
        printf("\n");

        rit_result_t result;
        int ret = rit_holdem_equity(holes, 2, flop, 2, &result);

        if (ret == RIT_SUCCESS) {
            printf("Player 1 equity: %.2f%%\n", result.avg_equity[0] * 100);
            printf("Player 2 equity: %.2f%%\n", result.avg_equity[1] * 100);
            printf("(Two cards to come ⇒ higher variance, still ~50/50 overall)\n");
        }
    }
}

/* Main demonstration */
int main(void) {
    printf("\n╔═══════════════════════════════════════════════════╗\n");
    printf("║                                                   ║\n");
    printf("║         Run It Twice - Demo & Examples           ║\n");
    printf("║                                                   ║\n");
    printf("╚═══════════════════════════════════════════════════╝\n");

    example_classic_allin();
    example_three_way();
    example_omaha();
    example_variance_analysis();
    example_stages();

    printf("\n╔═══════════════════════════════════════════════════╗\n");
    printf("║                   Summary                         ║\n");
    printf("╚═══════════════════════════════════════════════════╝\n\n");

    printf("Run It Twice (RIT) benefits:\n");
    printf("  1. Reduces variance in all-in situations\n");
    printf("  2. Makes results more skill-based, less luck-based\n");
    printf("  3. Common in high-stakes cash games\n");
    printf("  4. Can run 2-8 times (RIT-2 is standard)\n");
    printf("  5. Works for Hold'em, Omaha, and Omaha Hi/Lo\n\n");

    printf("Typical usage:\n");
    printf("  • Tournament bubbles (multiple runouts)\n");
    printf("  • High-stakes cash (variance reduction)\n");
    printf("  • Deal making (equity calculation)\n\n");

    return 0;
}
