#include <poker_eval/equity/flop_equity.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/eval.h>
#include <poker_eval/deck/deck_std.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* Helper for backdoor analysis */
static void analyze_backdoor_draws(
    StdDeck_CardMask pocket,
    StdDeck_CardMask flop,
    double *prob_bd_flush,
    double *prob_bd_straight)
{
    *prob_bd_flush = 0.0;
    *prob_bd_straight = 0.0;

    StdDeck_CardMask all;
    StdDeck_CardMask_OR(all, pocket, flop);

    /* Flush: Count suits */
    int suit_counts[4];
    /* Using existing utility, need to implement or call it. flop_count_suits works on any mask if we cast/rename it?
       No, flop_count_suits iterates 0..52. It works on any mask. */
    memset(suit_counts, 0, 4 * sizeof(int));
    for (int card = 0; card < 52; card++) {
        if (StdDeck_CardMask_CARD_IS_SET(all, card)) {
            int suit = StdDeck_SUIT(card);
            suit_counts[suit]++;
        }
    }

    for(int s=0; s<4; s++) {
        if (suit_counts[s] == 3) {
            *prob_bd_flush = 1.0; /* Backdoor flush draw */
        }
    }

    /* Straight: Sliding window */
    int unique_ranks[13]; /* 0=Ace ... 12=2? Or 0=2..12=A?
                             deck_std.h: ACE=12, 2=0.
                             Let's use 0=2 ... 12=Ace. */
    memset(unique_ranks, 0, sizeof(unique_ranks));

    for (int c=0; c<52; c++) {
        if (StdDeck_CardMask_CARD_IS_SET(all, c)) {
            int r = StdDeck_RANK(c);
            /* StdDeck_RANK returns 12 for ACE, 0 for 2. */
            unique_ranks[r] = 1;
        }
    }

    /* Check normal windows (length 5) */
    /* Ranks: 0,1,2,3,4 (2,3,4,5,6) ... 8,9,10,11,12 (T,J,Q,K,A) */
    for (int i=0; i<=8; i++) {
        int count = 0;
        for (int j=0; j<5; j++) {
            if (unique_ranks[i+j]) count++;
        }
        if (count >= 3) {
             *prob_bd_straight = 1.0;
        }
    }

    /* Check Wheel window (A,2,3,4,5). A=12, others 0,1,2,3. */
    int wheel_count = 0;
    if (unique_ranks[12]) wheel_count++; // Ace
    for (int j=0; j<4; j++) {
        if (unique_ranks[j]) wheel_count++;
    }
    if (wheel_count >= 3) *prob_bd_straight = 1.0;
}

/* Count cards of each suit on flop */
int flop_count_suits(StdDeck_CardMask flop, int suit_counts[4])
{
    memset(suit_counts, 0, 4 * sizeof(int));

    for (int card = 0; card < 52; card++) {
        if (StdDeck_CardMask_CARD_IS_SET(flop, card)) {
            int suit = StdDeck_SUIT(card);
            suit_counts[suit]++;
        }
    }

    return 0;
}

/* Extract ranks from flop (sorted high to low) */
int flop_get_ranks(StdDeck_CardMask flop, int ranks[3])
{
    int temp_ranks[3];
    int count = 0;

    /* Collect ranks */
    for (int card = 0; card < 52; card++) {
        if (StdDeck_CardMask_CARD_IS_SET(flop, card)) {
            int rank = StdDeck_RANK(card);
            /* Convert to 0=A, 1=K, ..., 12=2 */
            int canonical_rank = (rank == StdDeck_Rank_ACE) ? 0 :
                                 (StdDeck_Rank_KING - rank + 1);
            temp_ranks[count++] = canonical_rank;
        }
    }

    if (count != 3) return -1;

    /* Sort high to low (smaller canonical value is higher rank) */
    for (int i = 0; i < 2; i++) {
        for (int j = i + 1; j < 3; j++) {
            if (temp_ranks[i] > temp_ranks[j]) {
                int tmp = temp_ranks[i];
                temp_ranks[i] = temp_ranks[j];
                temp_ranks[j] = tmp;
            }
        }
    }

    ranks[0] = temp_ranks[0];  /* High */
    ranks[1] = temp_ranks[1];  /* Middle */
    ranks[2] = temp_ranks[2];  /* Low */

    return 0;
}

/* Check if flop is monotone (all same suit) */
bool flop_is_monotone(StdDeck_CardMask flop)
{
    int suit_counts[4];
    flop_count_suits(flop, suit_counts);

    for (int s = 0; s < 4; s++) {
        if (suit_counts[s] == 3) {
            return true;
        }
    }
    return false;
}

/* Check if flop has a pair */
bool flop_is_paired(StdDeck_CardMask flop)
{
    int ranks[3];
    if (flop_get_ranks(flop, ranks) < 0) return false;

    return (ranks[0] == ranks[1] ||
            ranks[1] == ranks[2] ||
            ranks[0] == ranks[2]);
}

/* Calculate texture wetness score (0=dry, 100=coordinated) */
int flop_texture_score(const flop_analysis_t *analysis)
{
    int score = 0;

    /* Base score by category */
    switch (analysis->texture) {
        case FLOP_TEXTURE_DRY:         score = 10; break;
        case FLOP_TEXTURE_WET:         score = 40; break;
        case FLOP_TEXTURE_PAIRED:      score = 50; break;
        case FLOP_TEXTURE_COORDINATED: score = 70; break;
        case FLOP_TEXTURE_TRIPS:       score = 80; break;
        default:                       score = 0;  break;
    }

    /* Adjustments */
    if (analysis->is_monotone) score += 15;
    if (analysis->is_connected) score += 10;
    if (analysis->n_broadway >= 2) score += 5;

    /* Cap at 100 */
    if (score > 100) score = 100;

    return score;
}

/* Main analysis function */
int analyze_flop_texture(StdDeck_CardMask flop, flop_analysis_t *analysis)
{
    if (!analysis) return -1;

    memset(analysis, 0, sizeof(*analysis));

    /* Verify exactly 3 cards */
    int n_cards = StdDeck_numCards(flop);
    if (n_cards != 3) return -1;

    /* Extract ranks */
    int ranks[3];
    if (flop_get_ranks(flop, ranks) < 0) return -1;

    analysis->high_card_rank = ranks[0];
    analysis->middle_card_rank = ranks[1];
    analysis->low_card_rank = ranks[2];

    /* Suit analysis */
    int suit_counts[4];
    flop_count_suits(flop, suit_counts);

    int max_suit_count = 0;
    for (int s = 0; s < 4; s++) {
        if (suit_counts[s] > max_suit_count) {
            max_suit_count = suit_counts[s];
        }
    }

    analysis->is_monotone = (max_suit_count == 3);
    analysis->is_two_tone = (max_suit_count == 2);
    analysis->is_rainbow = (max_suit_count == 1);

    /* Pair detection */
    if (ranks[0] == ranks[1] && ranks[1] == ranks[2]) {
        analysis->is_trips = true;
        analysis->is_paired = true;
        analysis->paired_rank = ranks[0];
    } else if (ranks[0] == ranks[1] || ranks[1] == ranks[2]) {
        analysis->is_paired = true;
        analysis->paired_rank = (ranks[0] == ranks[1]) ? ranks[0] : ranks[1];
    } else {
        analysis->is_paired = false;
        analysis->paired_rank = -1;
    }

    /* Connectivity */
    int gap1 = ranks[1] - ranks[0];
    int gap2 = ranks[2] - ranks[1];
    analysis->max_gap = (gap1 > gap2) ? gap1 : gap2;

    /* Handle wrap-around for wheel (A-2-3) */
    /* A=0, 2=12, 3=11. Sorted: 0, 11, 12. Gap1=11, Gap2=1. */
    if (ranks[0] == 0 && ranks[2] == 12) {  /* A...2 */
        /* Special case for wheel possibilities */
        if (ranks[1] == 11) /* A-3-2 */
             analysis->max_gap = 1;
        else if (ranks[1] == 10) /* A-4-2 */
             analysis->max_gap = 2;
        else if (ranks[1] == 9) /* A-5-2 */
             analysis->max_gap = 3;
    }

    analysis->is_connected = (analysis->max_gap == 1 && !analysis->is_paired);

    /* Broadway and low cards */
    analysis->n_broadway = 0;
    analysis->n_low_cards = 0;

    for (int i = 0; i < 3; i++) {
        if (ranks[i] <= 4) {  /* A, K, Q, J, T (0-4) */
            analysis->n_broadway++;
        }
        if (ranks[i] >= 7) {  /* 6, 5, 4, 3, 2 (7-12) */
            analysis->n_low_cards++;
        }
    }

    /* Draw potential (simplified) */
    analysis->has_oesd = analysis->is_connected;  /* Connected = potential OESD */
    analysis->has_gutshot = (analysis->max_gap == 2 && !analysis->is_paired);

    analysis->straight_draw_outs = 0;
    if (analysis->has_oesd) analysis->straight_draw_outs += 8;
    else if (analysis->has_gutshot) analysis->straight_draw_outs += 4;

    analysis->flush_draw_outs = 0;
    if (analysis->is_two_tone) analysis->flush_draw_outs = 9;
    if (analysis->is_monotone) analysis->flush_draw_outs = 0;  /* Already made flush */

    /* Categorize texture */
    if (analysis->is_trips) {
        analysis->texture = FLOP_TEXTURE_TRIPS;
    } else if (analysis->is_paired) {
        analysis->texture = FLOP_TEXTURE_PAIRED;
    } else if (analysis->is_monotone ||
               (analysis->is_connected && analysis->n_broadway >= 2)) {
        analysis->texture = FLOP_TEXTURE_COORDINATED;
    } else if (analysis->is_connected || analysis->is_two_tone) {
        analysis->texture = FLOP_TEXTURE_WET;
    } else {
        analysis->texture = FLOP_TEXTURE_DRY;
    }

    /* Calculate texture score (0-100) */
    analysis->texture_score = flop_texture_score(analysis);

    return 0;
}

/* Convert texture to string */
void flop_texture_to_string(flop_texture_category_t texture, char *out, size_t out_size)
{
    const char *label;
    switch (texture) {
        case FLOP_TEXTURE_DRY:         label = "Dry"; break;
        case FLOP_TEXTURE_WET:         label = "Wet"; break;
        case FLOP_TEXTURE_COORDINATED: label = "Coordinated"; break;
        case FLOP_TEXTURE_PAIRED:      label = "Paired"; break;
        case FLOP_TEXTURE_TRIPS:       label = "Trips"; break;
        default:                       label = "Unknown"; break;
    }
    snprintf(out, out_size, "%s", label);
}

static int flop_calc_equity_monte_carlo(
    const flop_equity_input_t *input,
    flop_equity_result_t *result);

/* Helper: Analyze current hand state */
static void analyze_current_hand_draws(
    StdDeck_CardMask pocket,
    StdDeck_CardMask flop,
    flop_equity_result_t *result)
{
    /* Count outs */
    int flush_outs, straight_outs, pair_outs;
    flop_count_outs(pocket, flop, &flush_outs, &straight_outs, &pair_outs);

    result->made_hand_outs = pair_outs;
    result->draw_outs = flush_outs + straight_outs;

    /* Populate draws based on outs */
    if (flush_outs > 0) result->prob_flush_draw = 1.0;

    /* OESD vs Gutshot heuristics */
    if (straight_outs >= 8) {
        result->prob_oesd = 1.0;
        result->prob_gutshot = 0.0;
    }
    else if (straight_outs >= 3) {
        result->prob_oesd = 0.0;
        result->prob_gutshot = 1.0;
    }

    /* Backdoor draws */
    analyze_backdoor_draws(pocket, flop,
                           &result->prob_backdoor_flush,
                           &result->prob_backdoor_straight);
}

static void accumulate_showdown_hand(
    int hand_type,
    flop_equity_result_t *result,
    double weight)
{
    switch(hand_type) {
        case StdRules_HandType_NOPAIR: result->prob_high_card += weight; break;
        case StdRules_HandType_ONEPAIR: result->prob_pair += weight; break;
        case StdRules_HandType_TWOPAIR: result->prob_two_pair += weight; break;
        case StdRules_HandType_TRIPS: result->prob_trips += weight; break;
        case StdRules_HandType_STRAIGHT: result->prob_straight += weight; break;
        case StdRules_HandType_FLUSH: result->prob_flush += weight; break;
        case StdRules_HandType_FULLHOUSE: result->prob_full_house += weight; break;
        case StdRules_HandType_QUADS: result->prob_quads += weight; break;
        case StdRules_HandType_STFLUSH:
            result->prob_straight += weight;
            result->prob_flush += weight; /* Technically both */
            break;
    }
}

/* Calculate flop equity using exhaustive enumeration */
static int flop_calc_equity_exhaustive(
    const flop_equity_input_t *input,
    flop_equity_result_t *result)
{
    memset(result, 0, sizeof(*result));
    analyze_current_hand_draws(input->pocket, input->flop, result);

    /* Fallback to MC for multi-way if requested exhaustively */
    if (input->n_opponents > 1) {
        flop_equity_input_t mc_input = *input;
        mc_input.n_samples = 10000;
        return flop_calc_equity_monte_carlo(&mc_input, result);
    }

    uint64_t total = 0;

    /* Variance calculation: accumulate sum of squared outcomes (1.0 for win, 0.5 tie) */
    double sum_sq_outcomes = 0.0;
    double sum_outcomes = 0.0;

    double turn_improved_count = 0;
    double river_improved_count = 0;
    /* We also track showdown probabilities in the loop */
    double total_hero_evals = 0.0;

    StdDeck_CardMask dead_init;
    StdDeck_CardMask_OR(dead_init, input->pocket, input->flop);

    /* Pre-calculate current hand value for improvement check */
    StdDeck_CardMask hand5;
    StdDeck_CardMask_OR(hand5, input->pocket, input->flop);
    HandVal current_val = StdDeck_StdRules_EVAL_N(hand5, 5);
    int current_type = HandVal_HANDTYPE(current_val);

    /* Iterate Turn (1 card) */
    for (int t=0; t<52; t++) {
        if (StdDeck_CardMask_CARD_IS_SET(dead_init, t)) continue;

        /* Check Turn improvement */
        StdDeck_CardMask hand6 = hand5;
        StdDeck_CardMask_SET(hand6, t);
        HandVal val6 = StdDeck_StdRules_EVAL_N(hand6, 6);
        if (HandVal_HANDTYPE(val6) > current_type) {
             turn_improved_count += 1.0;
        }

        StdDeck_CardMask dead_turn = dead_init;
        StdDeck_CardMask_SET(dead_turn, t);

        /* Iterate River (1 card) */
        for (int r=t+1; r<52; r++) {
            if (StdDeck_CardMask_CARD_IS_SET(dead_turn, r)) continue;

            /* Check River Improvement */
            StdDeck_CardMask hand7 = hand6;
            StdDeck_CardMask_SET(hand7, r);
            HandVal val7 = StdDeck_StdRules_EVAL_N(hand7, 7);

            int type7 = HandVal_HANDTYPE(val7);
            if (type7 > current_type) {
                 river_improved_count += 1.0;
            }

            /* Accumulate Showdown Probability (Hero Only) */
            /* We count this once per board, not per opponent iteration */
            accumulate_showdown_hand(type7, result, 1.0);
            total_hero_evals += 1.0;

            /* Opponent Loop (2 cards) */
            StdDeck_CardMask board_full = input->flop;
            StdDeck_CardMask_SET(board_full, t);
            StdDeck_CardMask_SET(board_full, r);

            StdDeck_CardMask dead_river = dead_turn;
            StdDeck_CardMask_SET(dead_river, r);

            for (int o1=0; o1<52; o1++) {
                if (StdDeck_CardMask_CARD_IS_SET(dead_river, o1)) continue;
                for (int o2=o1+1; o2<52; o2++) {
                    if (StdDeck_CardMask_CARD_IS_SET(dead_river, o2)) continue;

                    StdDeck_CardMask opp_hand = board_full;
                    StdDeck_CardMask_SET(opp_hand, o1);
                    StdDeck_CardMask_SET(opp_hand, o2);

                    HandVal opp_val = StdDeck_StdRules_EVAL_N(opp_hand, 7);

                    double outcome = 0.0;
                    if (val7 > opp_val) outcome = 1.0;
                    else if (val7 == opp_val) outcome = 0.5;

                    sum_outcomes += outcome;
                    sum_sq_outcomes += outcome * outcome;
                    total++;
                }
            }
        }
    }

    if (total > 0) {
        double total_d = (double)total;
        result->equity = sum_outcomes / total_d;

        /* Variance = E[x^2] - (E[x])^2 */
        double mean = result->equity;
        double mean_sq = sum_sq_outcomes / total_d;
        result->variance = mean_sq - (mean * mean);
    }

    /* Normalize improvements and hand types */
    /* Denominator is C(47, 2) = 1081 */
    double denom = 1081.0;

    result->turn_improvement = turn_improved_count / 47.0; /* Correct? turn loop is 47, but we sum 1.0 per turn card */
    result->river_improvement = river_improved_count / denom;

    if (total_hero_evals > 0) {
        result->prob_high_card /= total_hero_evals;
        result->prob_pair /= total_hero_evals;
        result->prob_two_pair /= total_hero_evals;
        result->prob_trips /= total_hero_evals;
        result->prob_straight /= total_hero_evals;
        result->prob_flush /= total_hero_evals;
        result->prob_full_house /= total_hero_evals;
        result->prob_quads /= total_hero_evals;
    }

    return 0;
}

/* Calculate flop equity using Monte Carlo */
static int flop_calc_equity_monte_carlo(
    const flop_equity_input_t *input,
    flop_equity_result_t *result)
{
    int n_samples = input->n_samples;
    if (n_samples <= 0) n_samples = 10000;

    memset(result, 0, sizeof(*result));
    analyze_current_hand_draws(input->pocket, input->flop, result);

    uint64_t total = 0;

    double sum_sq_outcomes = 0.0;
    double sum_outcomes = 0.0;

    StdDeck_CardMask dead_init;
    StdDeck_CardMask_OR(dead_init, input->pocket, input->flop);

    /* Pre-calculate current type for improvement stats */
    StdDeck_CardMask hand5;
    StdDeck_CardMask_OR(hand5, input->pocket, input->flop);
    HandVal current_val = StdDeck_StdRules_EVAL_N(hand5, 5);
    int current_type = HandVal_HANDTYPE(current_val);

    double turn_improved_count = 0;
    double river_improved_count = 0;

    /* MC Loop */
    for (int i=0; i<n_samples; i++) {
        StdDeck_CardMask dead = dead_init;
        StdDeck_CardMask board_full = input->flop;

        /* Sample Turn */
        int t_card = -1, r_card = -1;
        while (1) {
             t_card = rand() % 52;
             if (!StdDeck_CardMask_CARD_IS_SET(dead, t_card)) break;
        }
        StdDeck_CardMask_SET(board_full, t_card);
        StdDeck_CardMask_SET(dead, t_card);

        /* Check Turn improvement (approximate via sampling) */
        StdDeck_CardMask hand6 = hand5;
        StdDeck_CardMask_SET(hand6, t_card);
        if (HandVal_HANDTYPE(StdDeck_StdRules_EVAL_N(hand6, 6)) > current_type) {
            turn_improved_count += 1.0;
        }

        /* Sample River */
        while (1) {
             r_card = rand() % 52;
             if (!StdDeck_CardMask_CARD_IS_SET(dead, r_card)) break;
        }
        StdDeck_CardMask_SET(board_full, r_card);
        StdDeck_CardMask_SET(dead, r_card);

        StdDeck_CardMask hero_hand;
        StdDeck_CardMask_OR(hero_hand, input->pocket, board_full);
        HandVal hero_val = StdDeck_StdRules_EVAL_N(hero_hand, 7);
        int hero_type = HandVal_HANDTYPE(hero_val);

        if (hero_type > current_type) river_improved_count += 1.0;
        accumulate_showdown_hand(hero_type, result, 1.0);

        /* Sample Opponents */
        HandVal best_opp_val = HandVal_NOTHING;

        for (int opp=0; opp < input->n_opponents; opp++) {
             StdDeck_CardMask opp_pocket;
             StdDeck_CardMask_RESET(opp_pocket);
             int opp_cards_needed = 2;
             while (opp_cards_needed > 0) {
                 int c = rand() % 52;
                 if (!StdDeck_CardMask_CARD_IS_SET(dead, c)) {
                     StdDeck_CardMask_SET(opp_pocket, c);
                     StdDeck_CardMask_SET(dead, c);
                     opp_cards_needed--;
                 }
             }

             StdDeck_CardMask opp_hand;
             StdDeck_CardMask_OR(opp_hand, opp_pocket, board_full);
             HandVal opp_val = StdDeck_StdRules_EVAL_N(opp_hand, 7);
             if (opp_val > best_opp_val) best_opp_val = opp_val;
        }

        double outcome = 0.0;
        if (hero_val > best_opp_val) outcome = 1.0;
        else if (hero_val == best_opp_val) outcome = 0.5;

        sum_outcomes += outcome;
        sum_sq_outcomes += outcome * outcome;
        total++;
    }

    if (total > 0) {
        double total_d = (double)total;
        result->equity = sum_outcomes / total_d;

        double mean = result->equity;
        double mean_sq = sum_sq_outcomes / total_d;
        result->variance = mean_sq - (mean * mean);

        result->turn_improvement = turn_improved_count / total_d;
        result->river_improvement = river_improved_count / total_d;

        result->prob_high_card /= total_d;
        result->prob_pair /= total_d;
        result->prob_two_pair /= total_d;
        result->prob_trips /= total_d;
        result->prob_straight /= total_d;
        result->prob_flush /= total_d;
        result->prob_full_house /= total_d;
        result->prob_quads /= total_d;
    }

    return 0;
}

/* Main equity calculation function */
int flop_calc_equity(
    const flop_equity_input_t *input,
    flop_equity_result_t *result)
{
    if (!input || !result) return -1;

    /* Validate inputs */
    StdDeck_CardMask pocket = input->pocket;
    StdDeck_CardMask flop = input->flop;
    if (StdDeck_numCards(pocket) != 2) return -1;
    if (StdDeck_numCards(flop) != 3) return -1;

    /* Choose calculation method */
    if (input->n_samples == 0) {
        return flop_calc_equity_exhaustive(input, result);
    } else {
        return flop_calc_equity_monte_carlo(input, result);
    }
}

/* Calculate made hand probabilities */
double flop_calc_made_hand_prob(
    StdDeck_CardMask pocket,
    StdDeck_CardMask flop,
    int hand_type)
{
    StdDeck_CardMask hand;
    StdDeck_CardMask_OR(hand, pocket, flop);
    HandVal val = StdDeck_StdRules_EVAL_N(hand, 5);
    int current_type = HandVal_HANDTYPE(val);
    return (current_type >= hand_type) ? 1.0 : 0.0;
}

/* Count outs */
int flop_count_outs(
    StdDeck_CardMask pocket,
    StdDeck_CardMask flop,
    int *flush_outs,
    int *straight_outs,
    int *pair_outs)
{
    *flush_outs = 0;
    *straight_outs = 0;
    *pair_outs = 0;

    StdDeck_CardMask used;
    StdDeck_CardMask_OR(used, pocket, flop);

    /* Evaluate current hand */
    StdDeck_CardMask current_hand;
    StdDeck_CardMask_OR(current_hand, pocket, flop);
    HandVal current_val = StdDeck_StdRules_EVAL_N(current_hand, 5);
    int current_type = HandVal_HANDTYPE(current_val);

    /* Check improvements by iterating all 52 cards */
    for (int c = 0; c < 52; c++) {
        if (StdDeck_CardMask_CARD_IS_SET(used, c)) continue;

        /* Form new hand with card c */
        StdDeck_CardMask test_hand = current_hand;
        StdDeck_CardMask_SET(test_hand, c);

        HandVal new_val = StdDeck_StdRules_EVAL_N(test_hand, 6);
        int new_type = HandVal_HANDTYPE(new_val);

        /* Flush outs */
        /* If we don't have a flush yet, count cards that give us one */
        if (current_type < StdRules_HandType_FLUSH) {
            if (new_type >= StdRules_HandType_FLUSH) {
                (*flush_outs)++;
            }
        }

        /* Straight outs */
        /* If we don't have a straight yet, count cards that give us one */
        if (current_type < StdRules_HandType_STRAIGHT) {
            /* Check specifically for straight (or straight flush) */
            if (new_type == StdRules_HandType_STRAIGHT || new_type == StdRules_HandType_STFLUSH) {
                (*straight_outs)++;
            }
        }

        /* Pair/Set/Trips/TwoPair outs */
        /* This captures "improving the hand rank" in general terms of pairing */
        /* If we have High Card, we want Pair. If Pair, we want Trips or Two Pair. */
        if (current_type == StdRules_HandType_NOPAIR) {
             if (new_type >= StdRules_HandType_ONEPAIR) (*pair_outs)++;
        } else if (current_type == StdRules_HandType_ONEPAIR) {
             if (new_type > StdRules_HandType_ONEPAIR) (*pair_outs)++;
        } else if (current_type == StdRules_HandType_TWOPAIR) {
             if (new_type > StdRules_HandType_TWOPAIR) (*pair_outs)++;
        } else if (current_type == StdRules_HandType_TRIPS) {
             if (new_type > StdRules_HandType_TRIPS) (*pair_outs)++;
        }
    }

    return (*flush_outs + *straight_outs + *pair_outs);
}
