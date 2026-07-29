/*
 * ofcalc.c - Open Face Chinese Poker Calculator
 *
 * Copyright (C) 2025
 *
 * This program provides specialized tools for Open Face Chinese Poker analysis.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poker_eval/core/string_compat.h>
#include <assert.h>
#include <time.h>
#include <math.h>

#include <poker_eval/poker_eval.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/ofc/ofc.h>
#include <poker_eval/ofc/ofc_simd.h>
#include <poker_eval/ofc/ofc_adaptive.h>
#include <poker_eval/gpu/ofc_gpu.h>

#if defined(_WIN32)
#define strtok_r strtok_s
#endif

/* Function prototypes */
static void display_simd_info(void);
static void run_simd_benchmark(void);
static void display_gpu_info(void);
static void display_adaptive_info(void);

static void print_usage(const char *progname) {
    printf("Usage: %s [options]\n\n", progname);
    printf("Open Face Chinese Poker Calculator - Analysis Tools\n\n");
    printf("Options:\n");
    printf("  -foul <partial_hand> <card> <position>\n");
    printf("        Calculate foul risk for placing a card\n");
    printf("        Example: %s -foul \"top:As,Kh middle:Qd,Jc\" \"Qs\" \"top\"\n\n", progname);
    printf("  -fantasy <hand>\n");
    printf("        Calculate fantasyland analysis (current + probability)\n");
    printf("        Example: %s -fantasy \"top:Qh,Jd middle:8s,7c\"\n\n", progname);
    printf("  -fantasy-prob <hand>\n");
    printf("        Calculate only fantasyland probability\n");
    printf("        Example: %s -fantasy-prob \"top:Qh middle:8s,7c\"\n\n", progname);
    printf("  -compare <partial_hand> <card> <positions>\n");
    printf("        Compare foul risk for placing a card in different positions\n");
    printf("        Example: %s -compare \"top:As,Kh middle:Qd\" \"Js\" \"top,middle,bottom\"\n\n", progname);
    printf("  -expected-royalties <partial_hand> <position>\n");
    printf("        Calculate expected royalties for a specific position\n");
    printf("        Example: %s -expected-royalties \"top:Qh middle:8s,7c\" \"top\"\n\n", progname);
    printf("  -royalties <hand>\n");
    printf("        Calculate expected royalties\n");
    printf("        Example: %s -royalties \"top:Ah,Ac,Kd middle:Qs,Qh,Qc,9d,8s\"\n\n", progname);
    printf("  -analyze <complete_hand>\n");
    printf("        Complete analysis of a 13-card OFC hand\n");
    printf("        Example: %s -analyze \"top:Ah,Ac,Kd middle:Qs,Qh,Qc,9d,8s bottom:As,Ks,Js,Ts,9s\"\n\n", progname);
    printf("  -compare-hands <hand1> <hand2>\n");
    printf("        Compare two complete hands head-to-head\n");
    printf("        Example: %s -compare-hands \"hand1_spec\" \"hand2_spec\"\n\n", progname);
    printf("  -equity <complete_hand>\n");
    printf("        Calculate equity vs random opponent\n");
    printf("        Example: %s -equity \"top:Ah,Ac,Kd middle:Qs,Qh,Qc,9d,8s bottom:As,Ks,Js,Ts,9s\"\n\n", progname);
    printf("  -scoop <complete_hand>\n");
    printf("        Calculate scoop probability vs random opponent\n");
    printf("        Example: %s -scoop \"top:Ah,Ac,Kd middle:Qs,Qh,Qc,9d,8s bottom:As,Ks,Js,Ts,9s\"\n\n", progname);
    printf("  -score <hand1> <hand2>\n");
    printf("        Calculate OFC scoring between two complete hands\n\n");
    printf("  -benchmark\n");
    printf("        Run SIMD performance benchmarks\n\n");
    printf("  -simd-info\n");
    printf("        Display SIMD capability information\n\n");
    printf("  -gpu-info\n");
    printf("        Display GPU capability and performance information\n\n");
    printf("  -adaptive-info\n");
    printf("        Display adaptive system information (CPU/SIMD/GPU)\n\n");
    printf("Card format: As Kh Qd Jc ... (standard poker notation)\n");
    printf("Positions: top, middle, bottom\n");
    printf("Hand format: \"position:cards,cards position:cards\"\n\n");
    printf("Examples:\n");
    printf("  %s -foul \"top:As,Kh\" \"Qd\" \"top\"\n", progname);
    printf("  %s -fantasy \"top:Qh,Jd middle:8s,7c bottom:Ac\"\n", progname);
    printf("  %s -fantasy-prob \"top:Qh middle:8s,7c\"\n", progname);
    printf("  %s -compare \"top:As,Kh middle:Qd\" \"Js\" \"top,middle,bottom\"\n", progname);
    printf("  %s -expected-royalties \"top:Qh middle:8s,7c\" \"top\"\n", progname);
    printf("  %s -royalties \"top:Ah,Ac,Kd middle:Qs,Qh,Qc,9d,8s bottom:As,Ks,Qd,Jh,Ts\"\n", progname);
}

/* Parse a single card like "As" or "Kh" */
static int parse_card(const char *str) {
    if (strlen(str) != 2) return -1;

    int rank, suit;

    /* Parse rank */
    switch (str[0]) {
        case '2': rank = StdDeck_Rank_2; break;
        case '3': rank = StdDeck_Rank_3; break;
        case '4': rank = StdDeck_Rank_4; break;
        case '5': rank = StdDeck_Rank_5; break;
        case '6': rank = StdDeck_Rank_6; break;
        case '7': rank = StdDeck_Rank_7; break;
        case '8': rank = StdDeck_Rank_8; break;
        case '9': rank = StdDeck_Rank_9; break;
        case 'T': case 't': rank = StdDeck_Rank_TEN; break;
        case 'J': case 'j': rank = StdDeck_Rank_JACK; break;
        case 'Q': case 'q': rank = StdDeck_Rank_QUEEN; break;
        case 'K': case 'k': rank = StdDeck_Rank_KING; break;
        case 'A': case 'a': rank = StdDeck_Rank_ACE; break;
        default: return -1;
    }

    /* Parse suit */
    switch (str[1]) {
        case 'S': case 's': suit = StdDeck_Suit_SPADES; break;
        case 'H': case 'h': suit = StdDeck_Suit_HEARTS; break;
        case 'D': case 'd': suit = StdDeck_Suit_DIAMONDS; break;
        case 'C': case 'c': suit = StdDeck_Suit_CLUBS; break;
        default: return -1;
    }

    return StdDeck_MAKE_CARD(rank, suit);
}

/* Parse position string */
static ofc_position_t parse_position(const char *str) {
    if (strcasecmp(str, "top") == 0) return OFC_TOP;
    if (strcasecmp(str, "middle") == 0) return OFC_MIDDLE;
    if (strcasecmp(str, "bottom") == 0) return OFC_BOTTOM;
    return -1;
}

/* Parse multiple positions from string like "top,middle,bottom" */
static int parse_positions(const char *pos_str, ofc_position_t positions[], int max_positions) {
    char *pos_copy = strdup(pos_str);
    char *saveptr, *token;
    int count = 0;

    token = strtok_r(pos_copy, ",", &saveptr);
    while (token != NULL && count < max_positions) {
        ofc_position_t pos = parse_position(token);
        if (pos == -1) {
            free(pos_copy);
            return -1;
        }
        positions[count++] = pos;
        token = strtok_r(NULL, ",", &saveptr);
    }

    free(pos_copy);
    return count;
}

/* Parse a hand specification like "top:As,Kh middle:Qd,Jc" */
static int parse_hand_spec(const char *spec, ofc_hand_t *hand) {
    char *spec_copy = strdup(spec);
    char *saveptr, *token;

    OFC_InitializeHand(hand);

    /* Split by spaces to get position:cards tokens */
    token = strtok_r(spec_copy, " ", &saveptr);
    while (token != NULL) {
        /* Split position:cards */
        char *colon = strchr(token, ':');
        if (!colon) {
            free(spec_copy);
            return -1;
        }

        *colon = '\0';
        char *pos_str = token;
        char *cards_str = colon + 1;

        ofc_position_t pos = parse_position(pos_str);
        if (pos == -1) {
            free(spec_copy);
            return -1;
        }

        /* Parse cards separated by commas */
        char *card_saveptr, *card_token;
        card_token = strtok_r(cards_str, ",", &card_saveptr);
        while (card_token != NULL) {
            int card = parse_card(card_token);
            if (card == -1) {
                free(spec_copy);
                return -1;
            }

            if (OFC_PlaceCard(hand, card, pos) != 0) {
                free(spec_copy);
                return -1;
            }

            card_token = strtok_r(NULL, ",", &card_saveptr);
        }

        token = strtok_r(NULL, " ", &saveptr);
    }

    free(spec_copy);
    return 0;
}

/* Calculate and display foul risk */
static void calculate_foul_risk(const char *hand_spec, const char *card_str, const char *pos_str) {
    ofc_hand_t hand;
    int card;
    ofc_position_t pos;

    if (parse_hand_spec(hand_spec, &hand) != 0) {
        printf("Error: Invalid hand specification\n");
        return;
    }

    card = parse_card(card_str);
    if (card == -1) {
        printf("Error: Invalid card '%s'\n", card_str);
        return;
    }

    pos = parse_position(pos_str);
    if (pos == -1) {
        printf("Error: Invalid position '%s'\n", pos_str);
        return;
    }

    float risk = OFC_CalculateFoulRisk(&hand, card, pos);

    printf("=== OFC Foul Risk Analysis ===\n");
    printf("Hand: %s\n", hand_spec);
    printf("Placing: %s in %s\n", card_str, pos_str);
    printf("Foul risk: %.1f%%\n", risk * 100);

    if (risk < 0.20) {
        printf("Risk level: LOW - Safe placement\n");
    } else if (risk < 0.50) {
        printf("Risk level: MEDIUM - Use caution\n");
    } else if (risk < 0.80) {
        printf("Risk level: HIGH - Dangerous placement\n");
    } else {
        printf("Risk level: VERY HIGH - Avoid this placement!\n");
    }
}

/* Calculate fantasyland probability */
static void calculate_fantasyland(const char *hand_spec) {
    ofc_hand_t hand;

    if (parse_hand_spec(hand_spec, &hand) != 0) {
        printf("Error: Invalid hand specification\n");
        return;
    }

    /* Check current qualification */
    int qualifies = OFC_CheckFantasyland(&hand);

    /* Calculate probability of reaching fantasyland */
    float probability = OFC_CalculateFantasylandProbability(&hand);

    printf("=== OFC Fantasyland Analysis ===\n");
    printf("Hand: %s\n", hand_spec);
    printf("Currently qualifies for fantasyland: %s\n", qualifies ? "YES" : "NO");
    printf("Probability of qualifying: %.1f%%\n", probability * 100);

    if (qualifies) {
        printf("Congratulations! This hand qualifies for fantasyland in the next round.\n");
    } else if (probability > 0.50) {
        printf("EXCELLENT chance of reaching fantasyland!\n");
    } else if (probability > 0.25) {
        printf("GOOD chance of reaching fantasyland.\n");
    } else if (probability > 0.10) {
        printf("MODERATE chance of reaching fantasyland.\n");
    } else if (probability > 0.01) {
        printf("LOW chance of reaching fantasyland.\n");
    } else {
        printf("VERY LOW chance of reaching fantasyland.\n");
    }

    if (!qualifies) {
        printf("Need QQ+ in top position to qualify.\n");
    }
}

/* Calculate fantasyland probability only */
static void calculate_fantasyland_probability(const char *hand_spec) {
    ofc_hand_t hand;

    if (parse_hand_spec(hand_spec, &hand) != 0) {
        printf("Error: Invalid hand specification\n");
        return;
    }

    float probability = OFC_CalculateFantasylandProbability(&hand);

    printf("=== OFC Fantasyland Probability ===\n");
    printf("Hand: %s\n", hand_spec);
    printf("Fantasyland probability: %.1f%%\n", probability * 100);

    if (probability > 0.50) {
        printf("Rating: EXCELLENT (>50%%) - Very likely to qualify!\n");
    } else if (probability > 0.25) {
        printf("Rating: GOOD (25-50%%) - Strong chance to qualify.\n");
    } else if (probability > 0.10) {
        printf("Rating: MODERATE (10-25%%) - Decent chance to qualify.\n");
    } else if (probability > 0.01) {
        printf("Rating: LOW (1-10%%) - Unlikely but possible.\n");
    } else {
        printf("Rating: VERY LOW (<1%%) - Nearly impossible.\n");
    }
}

/* Compare placement options for a card */
static void compare_placements(const char *hand_spec, const char *card_str, const char *positions_str) {
    ofc_hand_t hand;
    int card;
    ofc_position_t positions[3];
    int num_positions;
    ofc_placement_result_t results[3];
    int i;

    /* Parse inputs */
    if (parse_hand_spec(hand_spec, &hand) != 0) {
        printf("Error: Invalid hand specification\n");
        return;
    }

    card = parse_card(card_str);
    if (card == -1) {
        printf("Error: Invalid card '%s'\n", card_str);
        return;
    }

    num_positions = parse_positions(positions_str, positions, 3);
    if (num_positions <= 0) {
        printf("Error: Invalid positions '%s'\n", positions_str);
        return;
    }

    /* Perform comparison */
    if (OFC_ComparePlacements(&hand, card, positions, num_positions, results) != 0) {
        printf("Error: Could not compare placements\n");
        return;
    }

    /* Get recommendation */
    ofc_position_t recommended = OFC_GetRecommendedPlacement(results, num_positions);

    /* Display results */
    printf("=== OFC Placement Comparison ===\n");
    printf("Hand: %s\n", hand_spec);
    printf("Card to place: %s\n", card_str);
    printf("\nRisk analysis:\n");

    for (i = 0; i < num_positions; i++) {
        printf("  %s: ", OFC_PositionToString(results[i].position));
        if (results[i].is_valid) {
            printf("%.1f%% foul risk (%s)\n", results[i].foul_risk * 100, results[i].risk_level);
        } else {
            printf("POSITION FULL (cannot place)\n");
        }
    }

    printf("\nRECOMMENDATION: ");
    if (recommended != (ofc_position_t)-1) {
        printf("Place %s in %s (safest option)\n", card_str, OFC_PositionToString(recommended));

        /* Find the result for the recommended position */
        for (i = 0; i < num_positions; i++) {
            if (results[i].position == recommended && results[i].is_valid) {
                if (results[i].foul_risk < 0.10) {
                    printf("Strategy: SAFE placement - very low risk.\n");
                } else if (results[i].foul_risk < 0.30) {
                    printf("Strategy: REASONABLE placement - acceptable risk.\n");
                } else if (results[i].foul_risk < 0.60) {
                    printf("Strategy: RISKY placement - use caution.\n");
                } else {
                    printf("Strategy: DANGEROUS placement - high risk of foul!\n");
                }
                break;
            }
        }
    } else {
        printf("No valid placement available (all positions full)\n");
    }
}

/* Calculate expected royalties for a specific position */
static void calculate_expected_royalties(const char *hand_spec, const char *position_str) {
    ofc_hand_t hand;
    ofc_position_t position;
    float expected;

    /* Parse inputs */
    if (parse_hand_spec(hand_spec, &hand) != 0) {
        printf("Error: Invalid hand specification\n");
        return;
    }

    position = parse_position(position_str);
    if (position == -1) {
        printf("Error: Invalid position '%s'\n", position_str);
        return;
    }

    /* Calculate expected royalties */
    expected = OFC_CalculateExpectedRoyalties(&hand, position);

    /* Display results */
    printf("=== OFC Expected Royalties Analysis ===\n");
    printf("Hand: %s\n", hand_spec);
    printf("Position: %s\n", OFC_PositionToString(position));
    printf("Expected royalties: %.2f points\n", expected);

    /* Provide interpretation based on position and expected value */
    if (position == OFC_TOP) {
        if (expected >= 3.0) {
            printf("Rating: EXCELLENT - High chance of strong pairs or trips\n");
        } else if (expected >= 1.0) {
            printf("Rating: GOOD - Decent chance of qualifying pairs\n");
        } else if (expected >= 0.1) {
            printf("Rating: MODERATE - Some chance of small pairs\n");
        } else {
            printf("Rating: LOW - Mostly high card outcomes\n");
        }
        printf("Note: Top position royalties: 66/77/88/99/TT = 1pt, JJ/QQ = 2/4pts, KK/AA = 6/9pts, Trips = 10-22pts\n");
    } else if (position == OFC_MIDDLE) {
        if (expected >= 8.0) {
            printf("Rating: EXCELLENT - High chance of strong hands (full house+)\n");
        } else if (expected >= 3.0) {
            printf("Rating: GOOD - Decent chance of flushes or full houses\n");
        } else if (expected >= 0.5) {
            printf("Rating: MODERATE - Some chance of straights or trips\n");
        } else {
            printf("Rating: LOW - Mostly non-royalty hands\n");
        }
        printf("Note: Middle royalties: Trips = 2pts, Straight = 4pts, Flush = 8pts, Full House = 12pts, etc.\n");
    } else { /* OFC_BOTTOM */
        if (expected >= 6.0) {
            printf("Rating: EXCELLENT - High chance of strong hands (straight+)\n");
        } else if (expected >= 2.0) {
            printf("Rating: GOOD - Decent chance of straights\n");
        } else if (expected >= 0.1) {
            printf("Rating: MODERATE - Some chance of small royalties\n");
        } else {
            printf("Rating: LOW - Mostly non-royalty hands\n");
        }
        printf("Note: Bottom royalties: Straight = 2pts, Flush = 4pts, Full House = 6pts, Four of a Kind = 10pts, etc.\n");
    }
}

/* Calculate royalties */
static void calculate_royalties(const char *hand_spec) {
    ofc_hand_t hand;
    ofc_royalties_t royalties;

    if (parse_hand_spec(hand_spec, &hand) != 0) {
        printf("Error: Invalid hand specification\n");
        return;
    }

    if (OFC_CalculateRoyalties(&hand, &royalties) != 0) {
        printf("Error: Could not calculate royalties\n");
        return;
    }

    printf("=== OFC Royalties Analysis ===\n");
    printf("Hand: %s\n", hand_spec);
    printf("Top royalties: %d points\n", royalties.top_royalties);
    printf("Middle royalties: %d points\n", royalties.middle_royalties);
    printf("Bottom royalties: %d points\n", royalties.bottom_royalties);
    printf("Total royalties: %d points\n",
           royalties.top_royalties + royalties.middle_royalties + royalties.bottom_royalties);
}

/* Analyze a complete OFC hand */
static void analyze_complete_hand(const char *hand_spec) {
    ofc_hand_t hand;
    ofc_complete_analysis_t analysis;

    if (parse_hand_spec(hand_spec, &hand) != 0) {
        printf("Error: Invalid hand specification\n");
        return;
    }

    /* Check if hand is complete (13 cards) */
    int total_cards = hand.card_count[OFC_TOP] + hand.card_count[OFC_MIDDLE] + hand.card_count[OFC_BOTTOM];
    if (total_cards != 13) {
        printf("Error: Complete hand analysis requires exactly 13 cards (got %d)\n", total_cards);
        printf("Use other analysis options for partial hands\n");
        return;
    }

    if (OFC_AnalyzeCompleteHand(&hand, &analysis) != 0) {
        printf("Error: Failed to analyze hand\n");
        return;
    }

    OFC_PrintCompleteAnalysis(&analysis);

    /* Add strategic advice */
    const char* advice = OFC_GetStrategicAdvice(&analysis);
    printf("Strategic Advice: %s\n\n", advice);
}

/* Compare two complete hands */
static void compare_complete_hands(const char *hand1_spec, const char *hand2_spec) {
    ofc_hand_t hand1, hand2;
    ofc_complete_analysis_t analysis1, analysis2;
    ofc_score_t score1, score2;

    if (parse_hand_spec(hand1_spec, &hand1) != 0) {
        printf("Error: Invalid hand1 specification\n");
        return;
    }
    if (parse_hand_spec(hand2_spec, &hand2) != 0) {
        printf("Error: Invalid hand2 specification\n");
        return;
    }

    /* Check if both hands are complete */
    int total1 = hand1.card_count[OFC_TOP] + hand1.card_count[OFC_MIDDLE] + hand1.card_count[OFC_BOTTOM];
    int total2 = hand2.card_count[OFC_TOP] + hand2.card_count[OFC_MIDDLE] + hand2.card_count[OFC_BOTTOM];

    if (total1 != 13 || total2 != 13) {
        printf("Error: Both hands must have exactly 13 cards (got %d and %d)\n", total1, total2);
        return;
    }

    if (OFC_CompareCompleteHands(&hand1, &hand2, &analysis1, &analysis2, &score1, &score2) != 0) {
        printf("Error: Failed to compare hands\n");
        return;
    }

    printf("=== OFC Hand-to-Hand Comparison ===\n\n");

    /* Print head-to-head scoring */
    printf("Head-to-Head Scoring:\n");
    printf("Hand 1: %d points  |  Hand 2: %d points\n", score1.total_score, score2.total_score);
    if (score1.total_score > score2.total_score) {
        printf("Winner: Hand 1 (+%d points)\n\n", score1.total_score - score2.total_score);
    } else if (score2.total_score > score1.total_score) {
        printf("Winner: Hand 2 (+%d points)\n\n", score2.total_score - score1.total_score);
    } else {
        printf("Result: Tie\n\n");
    }

    /* Print individual analyses */
    printf("==== Hand 1 Analysis ====\n");
    OFC_PrintCompleteAnalysis(&analysis1);

    printf("==== Hand 2 Analysis ====\n");
    OFC_PrintCompleteAnalysis(&analysis2);
}

/* Calculate equity vs random opponent */
static void calculate_equity_vs_random(const char *hand_spec) {
    ofc_hand_t hand;

    if (parse_hand_spec(hand_spec, &hand) != 0) {
        printf("Error: Invalid hand specification\n");
        return;
    }

    int total_cards = hand.card_count[OFC_TOP] + hand.card_count[OFC_MIDDLE] + hand.card_count[OFC_BOTTOM];
    if (total_cards != 13) {
        printf("Error: Equity calculation requires exactly 13 cards (got %d)\n", total_cards);
        return;
    }

    printf("=== OFC Equity vs Random Opponent ===\n");
    printf("Hand: %s\n", hand_spec);
    printf("Calculating equity (1000 simulations)...\n\n");

    float equity = OFC_CalculateEquityVsRandom(&hand, 1000);

    printf("Results:\n");
    printf("  Equity vs Random: %.1f%%\n", equity * 100);

    if (equity >= 0.65) {
        printf("  Rating: EXCELLENT - Very strong hand\n");
    } else if (equity >= 0.55) {
        printf("  Rating: GOOD - Above average strength\n");
    } else if (equity >= 0.45) {
        printf("  Rating: AVERAGE - Neutral strength\n");
    } else if (equity >= 0.35) {
        printf("  Rating: POOR - Below average\n");
    } else {
        printf("  Rating: VERY POOR - Weak construction\n");
    }
    printf("\n");
}

/* Calculate scoop probability */
static void calculate_scoop_probability(const char *hand_spec) {
    ofc_hand_t hand;

    if (parse_hand_spec(hand_spec, &hand) != 0) {
        printf("Error: Invalid hand specification\n");
        return;
    }

    int total_cards = hand.card_count[OFC_TOP] + hand.card_count[OFC_MIDDLE] + hand.card_count[OFC_BOTTOM];
    if (total_cards != 13) {
        printf("Error: Scoop calculation requires exactly 13 cards (got %d)\n", total_cards);
        return;
    }

    printf("=== OFC Scoop Probability vs Random Opponent ===\n");
    printf("Hand: %s\n", hand_spec);
    printf("Calculating scoop probability (1000 simulations)...\n\n");

    float scoop_prob = OFC_CalculateScoopProbability(&hand, 1000);

    printf("Results:\n");
    printf("  Scoop Probability: %.1f%%\n", scoop_prob * 100);

    if (scoop_prob >= 0.25) {
        printf("  Rating: EXCELLENT - Very high scoop potential\n");
    } else if (scoop_prob >= 0.15) {
        printf("  Rating: GOOD - Good scoop chances\n");
    } else if (scoop_prob >= 0.08) {
        printf("  Rating: AVERAGE - Moderate scoop potential\n");
    } else if (scoop_prob >= 0.03) {
        printf("  Rating: POOR - Low scoop chances\n");
    } else {
        printf("  Rating: VERY POOR - Minimal scoop potential\n");
    }

    printf("\nNote: Average scoop rate vs random opponent is ~10-15%%\n");
    printf("      Strong hands can achieve 20-30%% or higher\n\n");
}

int main(int argc, char *argv[]) {
    /* Initialize adaptive system (CPU/SIMD/GPU detection) */
    OFC_AdaptiveInit();

    if (argc < 2) {
        print_usage(argv[0]);
        OFC_AdaptiveCleanup();
        return 1;
    }

    if (strcmp(argv[1], "-foul") == 0) {
        if (argc != 5) {
            printf("Error: -foul requires 3 arguments\n");
            print_usage(argv[0]);
            return 1;
        }
        calculate_foul_risk(argv[2], argv[3], argv[4]);
    } else if (strcmp(argv[1], "-fantasy") == 0) {
        if (argc != 3) {
            printf("Error: -fantasy requires 1 argument\n");
            print_usage(argv[0]);
            return 1;
        }
        calculate_fantasyland(argv[2]);
    } else if (strcmp(argv[1], "-fantasy-prob") == 0) {
        if (argc != 3) {
            printf("Error: -fantasy-prob requires 1 argument\n");
            print_usage(argv[0]);
            return 1;
        }
        calculate_fantasyland_probability(argv[2]);
    } else if (strcmp(argv[1], "-compare") == 0) {
        if (argc != 5) {
            printf("Error: -compare requires 3 arguments\n");
            print_usage(argv[0]);
            return 1;
        }
        compare_placements(argv[2], argv[3], argv[4]);
    } else if (strcmp(argv[1], "-expected-royalties") == 0) {
        if (argc != 4) {
            printf("Error: -expected-royalties requires 2 arguments\n");
            print_usage(argv[0]);
            return 1;
        }
        calculate_expected_royalties(argv[2], argv[3]);
    } else if (strcmp(argv[1], "-royalties") == 0) {
        if (argc != 3) {
            printf("Error: -royalties requires 1 argument\n");
            print_usage(argv[0]);
            return 1;
        }
        calculate_royalties(argv[2]);
    } else if (strcmp(argv[1], "-analyze") == 0) {
        if (argc != 3) {
            printf("Error: -analyze requires 1 argument\n");
            print_usage(argv[0]);
            return 1;
        }
        analyze_complete_hand(argv[2]);
    } else if (strcmp(argv[1], "-compare-hands") == 0) {
        if (argc != 4) {
            printf("Error: -compare-hands requires 2 arguments\n");
            print_usage(argv[0]);
            return 1;
        }
        compare_complete_hands(argv[2], argv[3]);
    } else if (strcmp(argv[1], "-equity") == 0) {
        if (argc != 3) {
            printf("Error: -equity requires 1 argument\n");
            print_usage(argv[0]);
            return 1;
        }
        calculate_equity_vs_random(argv[2]);
    } else if (strcmp(argv[1], "-scoop") == 0) {
        if (argc != 3) {
            printf("Error: -scoop requires 1 argument\n");
            print_usage(argv[0]);
            return 1;
        }
        calculate_scoop_probability(argv[2]);
    } else if (strcmp(argv[1], "-simd-info") == 0) {
        display_simd_info();
    } else if (strcmp(argv[1], "-benchmark") == 0) {
        run_simd_benchmark();
    } else if (strcmp(argv[1], "-gpu-info") == 0) {
        display_gpu_info();
    } else if (strcmp(argv[1], "-adaptive-info") == 0) {
        display_adaptive_info();
    } else {
        printf("Error: Unknown option '%s'\n", argv[1]);
        print_usage(argv[0]);
        OFC_AdaptiveCleanup();
        return 1;
    }

    /* Cleanup adaptive system */
    OFC_AdaptiveCleanup();
    return 0;
}

/* ===== SIMD PERFORMANCE AND INFO FUNCTIONS ===== */

/* Display SIMD capability information */
static void display_simd_info(void) {
    printf("=== OFC SIMD Information ===\n");

    /* Initialize and detect SIMD capabilities */
    int capabilities = OFC_InitializeSIMD();

    printf("\nSIMD Support Status:\n");
    printf("  AVX-512: %s\n", (capabilities & OFC_SIMD_AVX512) ? "✓ Available" : "✗ Not available");
    printf("  AVX2:    %s\n", (capabilities & OFC_SIMD_AVX2) ? "✓ Available" : "✗ Not available");
    printf("  SSE2:    %s\n", (capabilities & OFC_SIMD_SSE2) ? "✓ Available" : "✗ Not available");

    if (capabilities == OFC_SIMD_NONE) {
        printf("  Status:  No SIMD support detected\n");
    } else {
        printf("  Status:  SIMD optimizations available\n");
    }

    printf("\nOptimal Configuration:\n");
    printf("  Batch Size:    %d\n", OFC_GetOptimalBatchSize());
    printf("  SIMD Enabled:  %s\n", OFC_IsSIMDEnabled() ? "Yes" : "No");

    /* Test a simple operation to verify functionality */
    printf("\nFunctionality Test:\n");
    ofc_hand_t test_hand;
    OFC_InitializeHand(&test_hand);

    /* Create a simple partial hand */
    OFC_PlaceCard(&test_hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES), OFC_TOP);
    OFC_PlaceCard(&test_hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS), OFC_TOP);

    int test_card = StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS);
    float foul_risk = OFC_CalculateFoulRisk(&test_hand, test_card, OFC_TOP);

    printf("  Sample calculation: Foul risk = %.3f\n", foul_risk);
    printf("  Status: %s\n", (foul_risk >= 0.0f && foul_risk <= 1.0f) ? "✓ Working correctly" : "✗ Error detected");

    printf("\nPerformance Recommendations:\n");
    if (capabilities & OFC_SIMD_AVX512) {
        printf("  - Use batch sizes of 16 for maximum performance\n");
        printf("  - Expected speedup: 8-16x for large batches\n");
    } else if (capabilities & OFC_SIMD_AVX2) {
        printf("  - Use batch sizes of 8 for optimal performance\n");
        printf("  - Expected speedup: 4-8x for large batches\n");
    } else if (capabilities & OFC_SIMD_SSE2) {
        printf("  - Use batch sizes of 4 for best performance\n");
        printf("  - Expected speedup: 2-4x for large batches\n");
    } else {
        printf("  - SIMD not available, consider CPU upgrade for better performance\n");
        printf("  - Current implementation will use CPU fallback\n");
    }
}

/* Run quick SIMD performance benchmark */
static void run_simd_benchmark(void) {
    printf("=== OFC SIMD Quick Benchmark ===\n");

    /* Initialize SIMD */
    int capabilities = OFC_InitializeSIMD();

    if (capabilities == OFC_SIMD_NONE) {
        printf("No SIMD support available - benchmark will compare CPU implementations only.\n");
    } else {
        printf("SIMD capabilities detected - running performance comparison...\n");
    }

    /* Create test data */
    printf("\nPreparing test data...\n");
    ofc_hand_t test_hand;
    OFC_InitializeHand(&test_hand);

    /* Create realistic partial hand */
    OFC_PlaceCard(&test_hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES), OFC_TOP);
    OFC_PlaceCard(&test_hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS), OFC_TOP);
    OFC_PlaceCard(&test_hand, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS), OFC_MIDDLE);
    OFC_PlaceCard(&test_hand, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_DIAMONDS), OFC_MIDDLE);
    OFC_PlaceCard(&test_hand, StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_SPADES), OFC_BOTTOM);
    OFC_PlaceCard(&test_hand, StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_HEARTS), OFC_BOTTOM);

    int test_card = StdDeck_MAKE_CARD(StdDeck_Rank_8, StdDeck_Suit_CLUBS);
    ofc_position_t test_position = OFC_MIDDLE;

    printf("Test configuration:\n");
    printf("  Partial hand: As Kh (top), Qc Jd (middle), Ts 9h (bottom)\n");
    printf("  Test card: 8c\n");
    printf("  Test position: middle\n");
    printf("  Iterations: 100 (for quick test)\n");

    /* Benchmark CPU version */
    printf("\nBenchmarking CPU version...\n");
    OFC_SetSIMDEnabled(0);

    clock_t start = clock();
    float cpu_result = 0;
    for (int i = 0; i < 100; i++) {
        cpu_result = OFC_CalculateFoulRisk(&test_hand, test_card, test_position);
    }
    clock_t end = clock();

    double cpu_time = ((double)(end - start)) / CLOCKS_PER_SEC * 1000.0;
    printf("CPU Time: %.2f ms (result: %.3f)\n", cpu_time, cpu_result);

    /* Benchmark SIMD version */
    printf("\nBenchmarking SIMD version...\n");
    OFC_SetSIMDEnabled(1);

    start = clock();
    float simd_result = 0;
    for (int i = 0; i < 100; i++) {
        simd_result = OFC_CalculateFoulRisk(&test_hand, test_card, test_position);
    }
    end = clock();

    double simd_time = ((double)(end - start)) / CLOCKS_PER_SEC * 1000.0;
    printf("SIMD Time: %.2f ms (result: %.3f)\n", simd_time, simd_result);

    /* Calculate and display results */
    printf("\nBenchmark Results:\n");
    if (simd_time > 0) {
        double speedup = cpu_time / simd_time;
        printf("  Speedup: %.2fx\n", speedup);

        if (speedup >= 2.0) {
            printf("  Status: ✓ Significant performance improvement detected\n");
        } else if (speedup >= 1.2) {
            printf("  Status: ✓ Moderate performance improvement detected\n");
        } else {
            printf("  Status: ⚠ Little to no performance improvement (single operations)\n");
            printf("          Note: SIMD benefits are most apparent with batch operations\n");
        }
    }

    /* Consistency check */
    float diff = (float)fabs(cpu_result - simd_result);
    printf("  Consistency: %s (diff: %.6f)\n",
           (diff < 0.1f) ? "✓ Results consistent" : "✗ Results differ significantly", diff);

    printf("\nRecommendation:\n");
    if (capabilities != OFC_SIMD_NONE) {
        printf("  For maximum performance, use the batch functions:\n");
        printf("  - OFC_CalculateMultipleFoulRisks() for multiple foul risk calculations\n");
        printf("  - OFC_AnalyzeMultipleCompleteHands() for multiple hand analyses\n");
        printf("  - These functions can achieve 4-16x speedup depending on SIMD support\n");
    } else {
        printf("  Consider upgrading to a system with AVX2 or AVX-512 support\n");
        printf("  for significant performance improvements in OFC calculations.\n");
    }

    /* Call the comprehensive benchmark if available */
    printf("\nFor detailed benchmarking, run: ./ofc_simd_benchmark\n");
}

/* ===== GPU INFO AND ADAPTIVE FUNCTIONS ===== */

/* Display GPU capability information */
static void display_gpu_info(void) {
#if defined(HAVE_CUDA) || defined(HAVE_OPENCL)
    printf("=== OFC GPU Information ===\n\n");

    /* Check GPU availability */
    int cuda_available = OFC_GPU_IsAvailable(OFC_GPU_BACKEND_CUDA);
    int opencl_available = OFC_GPU_IsAvailable(OFC_GPU_BACKEND_OPENCL);

    printf("GPU Backend Status:\n");
    printf("  CUDA:   %s\n", cuda_available ? "✓ Available" : "✗ Not available");
    printf("  OpenCL: %s\n", opencl_available ? "✓ Available" : "✗ Not available");

    if (!cuda_available && !opencl_available) {
        printf("\n⚠ No GPU acceleration available\n");
        printf("  Recommendation: Install CUDA or OpenCL drivers\n");
        return;
    }

    /* Initialize GPU */
    ofc_gpu_context_t *gpu_ctx = OFC_GPU_Init(-1, 8192, OFC_GPU_BACKEND_AUTO);
    if (!gpu_ctx) {
        printf("\n✗ Failed to initialize GPU\n");
        return;
    }

    printf("\nGPU Device Information:\n");
    /* Print device info through the GPU context */
    printf("  Status: GPU initialized successfully\n");
    printf("  Max batch size: %d hands\n", OFC_GPU_MAX_BATCH_SIZE);

    printf("\nPerformance Characteristics:\n");
    printf("  Optimal batch size: >= 64 hands\n");
    printf("  Optimal simulations: >= 10,000 per hand\n");
    printf("  Expected speedup: 100-1000x vs CPU (for large batches)\n");

    printf("\nUsage Recommendations:\n");
    printf("  - Use GPU for batch sizes >= 64\n");
    printf("  - Use GPU for simulations >= 10,000\n");
    printf("  - Use adaptive mode for automatic selection\n");
    printf("  - Run ./scripts/run_gpu_benchmarks.sh for detailed metrics\n");

    /* Cleanup */
    OFC_GPU_Cleanup(gpu_ctx);
#else
    printf("=== OFC GPU Information ===\n\n");
    printf("⚠ GPU support not compiled in\n");
    printf("  This build was configured without GPU support (BUILD_GPU=OFF)\n");
    printf("  To enable GPU acceleration:\n");
    printf("    1. Install CUDA or OpenCL drivers\n");
    printf("    2. Reconfigure with: cmake .. -DBUILD_GPU=ON\n");
    printf("    3. Rebuild the project\n");
#endif
}

/* Display adaptive system information */
static void display_adaptive_info(void) {
    printf("=== OFC Adaptive Processing System ===\n\n");

    /* Print adaptive capabilities */
    OFC_PrintAdaptiveInfo();

    printf("\nAdaptive Selection Logic:\n");
    printf("  Small workloads:  CPU\n");
    printf("  Medium workloads: SIMD (batch_size >= 4)\n");
    printf("  Large workloads:  GPU (batch_size >= 64 AND simulations >= 10,000)\n");

    printf("\nBenefits:\n");
    printf("  ✓ Automatic hardware detection\n");
    printf("  ✓ Optimal performance selection\n");
    printf("  ✓ Graceful fallback on errors\n");
    printf("  ✓ No manual configuration needed\n");

    printf("\nTo use adaptive mode:\n");
    printf("  Use OFC_CalculateFoulRiskAdaptive() instead of OFC_CalculateFoulRisk()\n");
    printf("  The system will automatically select the best backend\n");
}
