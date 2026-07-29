#include <poker_eval/distributions/plo_nomenclature.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h> // Added for bool type
#include <stdlib.h>  // For qsort if needed, general utilities
#include <poker_eval/deck/deck_std.h> // For StdDeck_CardMask, StdDeck_Rank, StdDeck_Suit, StdDeck_CardMask_CLEAR, etc.
#include <poker_eval/core/card.h>     // For Card_print, Card_new

#define RANK_PLACEHOLDER_MARKER ((int)-1) // Special marker for 'x'

// Forward declarations
static int count_set_bits(StdDeck_CardMask mask);

// Helper function to convert rank character to rank value
static int char_to_rank(char r_char) {
    switch (r_char) {
        case 'A': case 'a': return StdDeck_Rank_ACE;
        case 'K': case 'k': return StdDeck_Rank_KING;
        case 'Q': case 'q': return StdDeck_Rank_QUEEN;
        case 'J': case 'j': return StdDeck_Rank_JACK;
        case 'T': case 't': return StdDeck_Rank_TEN;
        case '9': return StdDeck_Rank_9;
        case '8': return StdDeck_Rank_8;
        case '7': return StdDeck_Rank_7;
        case '6': return StdDeck_Rank_6;
        case '5': return StdDeck_Rank_5;
        case '4': return StdDeck_Rank_4;
        case '3': return StdDeck_Rank_3;
        case '2': return StdDeck_Rank_2;
        default: return StdDeck_Rank_COUNT; // Invalid rank indicator
    }
}

// Tries all suit combinations for a given set of 4 concrete ranks to achieve target_suitedness.
// Renamed from generate_plo_mask.
static bool try_suit_combinations_for_ranks(const int ranks[4], PLOSuitedness target_suitedness, StdDeck_CardMask* out_mask) {
    StdDeck_CardMask_RESET(*out_mask);
    int current_suits[4];
    
    // Check if it's impossible to achieve target suitedness with given ranks
    // Count how many times each rank appears
    int rank_counts[StdDeck_Rank_COUNT] = {0};
    for (int i = 0; i < 4; i++) {
        rank_counts[ranks[i]]++;
    }
    
    // If we have 4 of the same rank, we can only have one of each suit
    // This means we must have all 4 different suits (rainbow)
    for (int i = 0; i < StdDeck_Rank_COUNT; i++) {
        if (rank_counts[i] == 4) {
            // With 4 of the same rank, we must use all 4 suits
            // This creates a "rainbow" but it's actually all same rank
            // In PLO, this is considered monotone/quad suited
            if (target_suitedness == PLO_SUIT_RAINBOW) {
                return false; // Can't have true rainbow with 4 same rank
            }
        }
    }

    for (current_suits[0] = StdDeck_Suit_FIRST; current_suits[0] <= StdDeck_Suit_LAST; current_suits[0]++) {
        for (current_suits[1] = StdDeck_Suit_FIRST; current_suits[1] <= StdDeck_Suit_LAST; current_suits[1]++) {
            for (current_suits[2] = StdDeck_Suit_FIRST; current_suits[2] <= StdDeck_Suit_LAST; current_suits[2]++) {
                for (current_suits[3] = StdDeck_Suit_FIRST; current_suits[3] <= StdDeck_Suit_LAST; current_suits[3]++) {
                    StdDeck_CardMask attempt_mask;
                    StdDeck_CardMask_RESET(attempt_mask);
                    
                    int card_values[4];
                    card_values[0] = StdDeck_MAKE_CARD(ranks[0], current_suits[0]);
                    card_values[1] = StdDeck_MAKE_CARD(ranks[1], current_suits[1]);
                    card_values[2] = StdDeck_MAKE_CARD(ranks[2], current_suits[2]);
                    card_values[3] = StdDeck_MAKE_CARD(ranks[3], current_suits[3]);

                    bool unique_cards = true;
                    for(int i=0; i<4; ++i) {
                        for(int j=i+1; j<4; ++j) {
                            if(card_values[i] == card_values[j]) {
                                unique_cards = false;
                                break;
                            }
                        }
                        if(!unique_cards) break;
                    }
                    if (!unique_cards) continue;

                    StdDeck_CardMask_SET(attempt_mask, card_values[0]);
                    StdDeck_CardMask_SET(attempt_mask, card_values[1]);
                    StdDeck_CardMask_SET(attempt_mask, card_values[2]);
                    StdDeck_CardMask_SET(attempt_mask, card_values[3]);
                    
                    if (count_set_bits(attempt_mask) == 4) {
                        if (PLO_GetSuitedness(attempt_mask) == target_suitedness) {
                            *out_mask = attempt_mask;
                            return true;
                        }
                    }
                }
            }
        }
    }
    return false;
}

// Recursive helper to find ranks for placeholders 'x' and then try suit combinations.
static bool find_rank_suit_combination_recursive(
    int current_wildcard_slot_idx,      // Index of the current placeholder slot (0 to 3, not wildcard_idx 0 to num_x-1)
    int current_rank_config[4], // Array to be filled with concrete ranks
    const int initial_pattern_ranks[4], // Pattern with RANK_PLACEHOLDER_MARKER
    PLOSuitedness target_suitedness,
    StdDeck_CardMask* out_mask)
{
    // Find the next placeholder slot to fill
    while (current_wildcard_slot_idx < 4 && initial_pattern_ranks[current_wildcard_slot_idx] != RANK_PLACEHOLDER_MARKER) {
        current_rank_config[current_wildcard_slot_idx] = initial_pattern_ranks[current_wildcard_slot_idx];
        current_wildcard_slot_idx++;
    }

    if (current_wildcard_slot_idx == 4) { // All slots filled (either fixed or with chosen wildcard ranks)
        return try_suit_combinations_for_ranks(current_rank_config, target_suitedness, out_mask);
    }

    // Try assigning ranks to the current placeholder slot initial_pattern_ranks[current_wildcard_slot_idx]
    for (int r_try = StdDeck_Rank_FIRST; r_try <= StdDeck_Rank_LAST; ++r_try) {
        bool conflict = false;
        // Check conflict with fixed ranks in the pattern
        for (int i = 0; i < 4; ++i) {
            if (initial_pattern_ranks[i] != RANK_PLACEHOLDER_MARKER && initial_pattern_ranks[i] == r_try) {
                conflict = true;
                break;
            }
        }
        if (conflict) continue;

        // Check conflict with previously chosen wildcard ranks for this attempt
        for (int i = 0; i < current_wildcard_slot_idx; ++i) {
            if (initial_pattern_ranks[i] == RANK_PLACEHOLDER_MARKER && current_rank_config[i] == r_try) {
                conflict = true;
                break;
            }
        }
        if (conflict) continue;

        current_rank_config[current_wildcard_slot_idx] = r_try;
        if (find_rank_suit_combination_recursive(current_wildcard_slot_idx + 1, current_rank_config, initial_pattern_ranks, target_suitedness, out_mask)) {
            return true;
        }
    }
    return false;
}

// Generates a sample mask for a pattern that might include 'x' placeholders.
static bool generate_sample_mask_for_pattern(
    const int pattern_ranks[4], // Contains actual ranks or RANK_PLACEHOLDER_MARKER
    PLOSuitedness target_suitedness,
    StdDeck_CardMask* out_mask)
{
    int current_rank_config[4]; // To be filled by recursive helper
    // Initialize current_rank_config with fixed ranks from pattern_ranks
    for(int i=0; i<4; ++i) {
        if(pattern_ranks[i] != RANK_PLACEHOLDER_MARKER) {
            current_rank_config[i] = pattern_ranks[i];
        }
        // Placeholders will be filled by the recursive function
    }
    return find_rank_suit_combination_recursive(0, current_rank_config, pattern_ranks, target_suitedness, out_mask);
}

// Comparison function for qsort
static int compare_ranks(const void* a, const void* b) {
    int rank_a = *(const int*)a;
    int rank_b = *(const int*)b;
    if (rank_a < rank_b) return -1;
    if (rank_a > rank_b) return 1;
    return 0;
}

// Helper function to count set bits (cards) in a mask
static int count_set_bits(StdDeck_CardMask mask) {
    int count = 0;
    for (int i = 0; i < StdDeck_N_CARDS; ++i) {
        if (StdDeck_CardMask_CARD_IS_SET(mask, i)) {
            count++;
        }
    }
    return count;
}

// Helper function to get ranks and suits from a card mask
static void get_cards_details(StdDeck_CardMask mask, int ranks[], int suits[], int* count) {
    *count = 0;
    for (int i = 0; i < StdDeck_N_CARDS && *count < 4; ++i) {
        if (StdDeck_CardMask_CARD_IS_SET(mask, i)) {
            ranks[*count] = StdDeck_RANK(i);
            suits[*count] = StdDeck_SUIT(i);
            (*count)++;
        }
    }
}

PLOSuitedness PLO_GetSuitedness(StdDeck_CardMask cards) {
    if (count_set_bits(cards) != 4) return PLO_SUIT_RAINBOW; // Or some error/undefined

    int suit_counts[4] = {0}; // Spades, Hearts, Diamonds, Clubs
    int card_ranks[4];
    int card_suits[4];
    int num_cards;

    get_cards_details(cards, card_ranks, card_suits, &num_cards);

    for (int i = 0; i < num_cards; ++i) {
        suit_counts[card_suits[i]]++;
    }

    int pairs = 0;
    int trips = 0;
    int quads = 0;
    for (int i = 0; i < 4; ++i) {
        if (suit_counts[i] == 2) pairs++;
        if (suit_counts[i] == 3) trips++;
        if (suit_counts[i] == 4) quads++;
    }

    if (quads == 1) return PLO_SUIT_QUAD;
    if (trips == 1) return PLO_SUIT_TRIPLE; // Typically, 3-1-0-0
    if (pairs == 2) return PLO_SUIT_DOUBLE; // 2-2-0-0
    if (pairs == 1) return PLO_SUIT_SINGLE; // 2-1-1-0
    
    return PLO_SUIT_RAINBOW; // 1-1-1-1
}

PLOConnectivity PLO_GetConnectivity(StdDeck_CardMask cards) {
    int ranks[4];
    int suits[4]; // Unused here, but part of get_cards_details
    int num_cards_extracted;
    get_cards_details(cards, ranks, suits, &num_cards_extracted);

    if (num_cards_extracted != 4) {
        return PLO_CONN_NONE; // Should not happen if called after count_set_bits check
    }

    qsort(ranks, 4, sizeof(int), compare_ranks);

    bool is_unique = (ranks[0] < ranks[1] && ranks[1] < ranks[2] && ranks[2] < ranks[3]);

    if (is_unique) {
        // Check for Ace-low straight (wheel: A234, A235, A245, A345)
        // Ranks are sorted: ranks[0]=2, ranks[1]=3, ranks[2]=4, ranks[3]=A for A234
        // StdDeck_Rank_2 = 0, ..., StdDeck_Rank_5 = 3, StdDeck_Rank_ACE = 12
        if (ranks[3] == StdDeck_Rank_ACE) {
            if (ranks[0] == StdDeck_Rank_2 && ranks[1] == StdDeck_Rank_3 && ranks[2] == StdDeck_Rank_4) {
                return PLO_CONN_RUNDOWN; // A,2,3,4
            }
            if (ranks[0] == StdDeck_Rank_2 && ranks[1] == StdDeck_Rank_3 && ranks[2] == StdDeck_Rank_5) {
                return PLO_CONN_1GAP; // A,2,3,5 (gap for 4)
            }
            if (ranks[0] == StdDeck_Rank_2 && ranks[1] == StdDeck_Rank_4 && ranks[2] == StdDeck_Rank_5) {
                return PLO_CONN_1GAP; // A,2,4,5 (gap for 3)
            }
            if (ranks[0] == StdDeck_Rank_3 && ranks[1] == StdDeck_Rank_4 && ranks[2] == StdDeck_Rank_5) {
                return PLO_CONN_1GAP; // A,3,4,5 (gap for 2)
            }
            // Add A2xx for 2-gaps if needed, e.g. A,2,3,6 (gaps for 4,5)
            if (ranks[0] == StdDeck_Rank_2 && ranks[1] == StdDeck_Rank_3 && ranks[2] == StdDeck_Rank_6) { // A236 (gaps 4,5)
                 return PLO_CONN_2GAP;
            }
             if (ranks[0] == StdDeck_Rank_2 && ranks[1] == StdDeck_Rank_4 && ranks[2] == StdDeck_Rank_6) { // A246 (gaps 3,5)
                 return PLO_CONN_2GAP;
            }
            if (ranks[0] == StdDeck_Rank_2 && ranks[1] == StdDeck_Rank_5 && ranks[2] == StdDeck_Rank_6) { // A256 (gaps 3,4)
                 return PLO_CONN_2GAP;
            }
            // Consider if A,K,Q,J is already handled by general logic. Yes, it should be.
        }

        // General case for unique ranks (Ace is high here if not caught by A-5 logic)
        // For JT86: ranks are 6,8,10,11 (sorted)
        // Total span: 11-6 = 5, but we have 4 cards, so gaps = 5-3 = 2
        // But this counts total gaps, not the pattern
        // JT86 has pattern: 6,8(gap),10,11 which is 1 gap in the middle
        
        // Count actual gaps between consecutive cards
        int gap_count = 0;
        for (int i = 0; i < 3; i++) {
            int gap = ranks[i+1] - ranks[i] - 1;
            if (gap > 0) gap_count += gap;
        }
        
        if (gap_count == 0) return PLO_CONN_RUNDOWN;
        if (gap_count == 1) return PLO_CONN_1GAP;
        if (gap_count == 2) return PLO_CONN_2GAP;
        // If gap_count > 2, fall through to partial or none
    }

    // If not unique or too gappy for rundown/1-gap/2-gap
    // Check for any partial connectivity (at least two consecutive ranks)
    // This handles paired hands as well, e.g. KKJT (T,J,K,K -> TJ connected, JK connected)
    // ranks are sorted: r[0] <= r[1] <= r[2] <= r[3]
    if ((ranks[1] - ranks[0] == 1) || 
        (ranks[2] - ranks[1] == 1) || 
        (ranks[3] - ranks[2] == 1)) {
        // Special check for things like KKKQ - ranks Q,K,K,K. QK is connected.
        // Or KKTJ - ranks T,J,K,K. TJ connected, JK connected.
        return PLO_CONN_PARTIAL;
    }
    // Check for inner partial connectivity if outer ranks are same, e.g. KJTJ -> J,J,T,K -> TJ, JK
    // This is covered if we consider unique ranks for partial, but the current logic is simpler.
    // The current partial check is sufficient for now.

    return PLO_CONN_NONE;
}

PLOHandCategory PLO_CategorizeHand(StdDeck_CardMask cards, PLOHand* hand_details_ptr) {
    if (count_set_bits(cards) != 4) {
        if (hand_details_ptr) {
            memset(hand_details_ptr, 0, sizeof(PLOHand));
            hand_details_ptr->category = PLO_CAT_INVALID;
            snprintf(hand_details_ptr->notation, sizeof(hand_details_ptr->notation), "%s", "INVALID");
        }
        return PLO_CAT_INVALID;
    }

    int card_ranks_arr[4];
    int card_suits_arr[4]; // Not strictly needed for categorization logic below, but good for completeness
    int num_cards_parsed;
    get_cards_details(cards, card_ranks_arr, card_suits_arr, &num_cards_parsed);

    // Count occurrences of each rank
    int rank_counts[StdDeck_Rank_COUNT] = {0};
    int highest_rank = -1;
    for(int i=0; i < num_cards_parsed; ++i) {
        rank_counts[card_ranks_arr[i]]++;
        if (card_ranks_arr[i] > highest_rank) {
            highest_rank = card_ranks_arr[i];
        }
    }

    int pair_count = 0;
    int trips_count = 0;
    int ace_present_flag = 0;
    int num_broadway_cards = 0;

    for(int i = 0; i < StdDeck_Rank_COUNT; ++i) {
        if (rank_counts[i] == 2) pair_count++;
        if (rank_counts[i] == 3) trips_count++;
        // Note: rank_counts[i] == 4 (quads) is not a PLO starting hand category,
        // as you only have 4 cards. Quads would mean all 4 cards are same rank.
        // This would be trips_count = 1, and one other card. Or pair_count = 2 if e.g. AAAA.
        // Let's clarify: if AAAA, rank_counts[ACE] = 4. trips_count would be 0, pair_count would be 0 by this loop.
        // This needs to be handled. AAAA is trips A with A kicker, or two pair AA and AA.
        // The nomenclature has "Trips (xxx y)". So AAAA would be Trips.
        // If rank_counts[i] == 4, it's effectively trips with a paired kicker of the same rank.
        // For PLO starting hands, AAAA is trips.
    }
    // Correctly identify trips if all 4 cards are same rank (e.g. AAAA)
    // This is a special case of trips.
    for(int i=0; i < StdDeck_Rank_COUNT; ++i) {
        if (rank_counts[i] == 4) { // e.g. AAAA
            trips_count = 1; // Treat as trips for categorization
            pair_count = 0; // Not two pair in this context
            // The 4th card is the 'kicker' of the same rank as trips.
        }
    }


    if (rank_counts[StdDeck_Rank_ACE] > 0) ace_present_flag = 1;

    for(int i = StdDeck_Rank_TEN; i <= StdDeck_Rank_ACE; ++i) {
        if (rank_counts[i] > 0) {
            num_broadway_cards += rank_counts[i];
        }
    }

    PLOSuitedness current_suitedness = PLO_GetSuitedness(cards);
    PLOConnectivity current_connectivity = PLO_GetConnectivity(cards); // Still a stub

    if (hand_details_ptr) {
        hand_details_ptr->cards = cards;
        hand_details_ptr->suitedness = current_suitedness;
        hand_details_ptr->connectivity = current_connectivity;
        hand_details_ptr->has_ace = ace_present_flag;
        hand_details_ptr->broadway_count = num_broadway_cards;
        hand_details_ptr->pair_count = pair_count; // This will be 0 if trips_count is 1 due to AAAA
        hand_details_ptr->trips = (trips_count > 0);
        // Notation will be set later if needed by PLO_HandToString or by parser
    }

    PLOHandCategory determined_category = PLO_CAT_INVALID;

    // Block E: Aces (AAxy) -- allow extra pair (e.g., AAKK) to remain in AA bucket
    if (rank_counts[StdDeck_Rank_ACE] == 2 && trips_count == 0) {
        if (current_suitedness == PLO_SUIT_DOUBLE) determined_category = PLO_CAT_AA_DS;
        else if (current_suitedness == PLO_SUIT_SINGLE) determined_category = PLO_CAT_AA_SS;
        else determined_category = PLO_CAT_AA_RB;
    }
    // Block D: Trips (xxx y) - including xxxx
    else if (trips_count == 1) {
        if (current_suitedness == PLO_SUIT_DOUBLE) determined_category = PLO_CAT_TRIPS_DS;
        else if (current_suitedness == PLO_SUIT_SINGLE) determined_category = PLO_CAT_TRIPS_SS;
        else determined_category = PLO_CAT_TRIPS_RB;
    }
    // Block C: Two-Pair (xxyy) - ensure not AAxx (handled by Block E)
    else if (pair_count == 2) { // trips_count must be 0 here
        if (current_suitedness == PLO_SUIT_DOUBLE) determined_category = PLO_CAT_TWO_PAIR_DS;
        else if (current_suitedness == PLO_SUIT_SINGLE) determined_category = PLO_CAT_TWO_PAIR_SS;
        else determined_category = PLO_CAT_TWO_PAIR_RB;
    }
    // Block B: One-Pair (xxy z, not AA) - ensure not AAxx (handled by Block E)
    else if (pair_count == 1) { // trips_count must be 0 here, and not AAxx
        if (current_suitedness == PLO_SUIT_DOUBLE) determined_category = PLO_CAT_PAIR_DS;
        else if (current_suitedness == PLO_SUIT_SINGLE) determined_category = PLO_CAT_PAIR_SS;
        else determined_category = PLO_CAT_PAIR_RB;
    }
    // Unpaired hands (Blocks F, G, A)
    else if (pair_count == 0 && trips_count == 0) {
        // Block F: Broadway-heavy (3+ Broadway cards)
        if (num_broadway_cards >= 3) {
            if (current_suitedness == PLO_SUIT_DOUBLE) determined_category = PLO_CAT_BROADWAY_DS;
            else if (current_suitedness == PLO_SUIT_SINGLE) determined_category = PLO_CAT_BROADWAY_SS;
            else determined_category = PLO_CAT_BROADWAY_RB;
        }
        // Block G: Ragged/Low (highest card <= 9, not Broadway-heavy)
        // Broadway-heavy check is implicitly handled as this is an 'else if'
        else if (highest_rank <= StdDeck_Rank_9) {
            if (current_suitedness == PLO_SUIT_DOUBLE) determined_category = PLO_CAT_RAGGED_DS;
            else if (current_suitedness == PLO_SUIT_SINGLE) determined_category = PLO_CAT_RAGGED_SS;
            else determined_category = PLO_CAT_RAGGED_RB;
        }
        // Block A: Unpaired (connected/high cards, not Broadway-heavy, not Ragged/Low)
        else {
            if (current_suitedness == PLO_SUIT_DOUBLE) determined_category = PLO_CAT_UNPAIRED_DS;
            else if (current_suitedness == PLO_SUIT_SINGLE) determined_category = PLO_CAT_UNPAIRED_SS;
            else determined_category = PLO_CAT_UNPAIRED_RB;
        }
    }

    if (hand_details_ptr) {
        hand_details_ptr->category = determined_category;
    }
    return determined_category;
}


int PLO_ParseHand(const char* hand_str, PLOHand* hand) {
    if (!hand_str || !hand) return 0;

    memset(hand, 0, sizeof(PLOHand));
    StdDeck_CardMask_RESET(hand->cards);
    hand->category = PLO_CAT_INVALID;
    strncpy(hand->notation, "INVALID_INPUT", sizeof(hand->notation) - 1); // Default error message
    hand->notation[sizeof(hand->notation) - 1] = '\0';

    size_t len = strnlen(hand_str, 32);

    // 1. Attempt to parse 4 specific cards (e.g., "AsKdQhJc")
    if (len == 8) {
        StdDeck_CardMask temp_mask;
        StdDeck_CardMask_RESET(temp_mask);
        int specific_cards_parsed = 0;
        for (int i = 0; i < 4; ++i) {
            char card_str_segment[3] = {hand_str[i * 2], hand_str[i * 2 + 1], '\0'};
            int card_val;
            if (StdDeck_stringToCard(card_str_segment, &card_val) == 2) {
                if (StdDeck_CardMask_CARD_IS_SET(temp_mask, card_val)) { // Duplicate card
                    specific_cards_parsed = 0; break;
                }
                StdDeck_CardMask_SET(temp_mask, card_val);
                specific_cards_parsed++;
            } else {
                specific_cards_parsed = 0; break; // Invalid card in string
            }
        }
        if (specific_cards_parsed == 4) {
            hand->cards = temp_mask;
            PLO_CategorizeHand(hand->cards, hand); // Fills details including hand->suitedness
            PLO_HandToString(hand, hand->notation, sizeof(hand->notation)); // Generate canonical notation
            return 1; // Success
        }
    }

    // 2. Attempt to parse descriptive notation (e.g., "AAKKds", "AAxxds", "AKxxd", "xxxxr")
    // Valid lengths: 4 ranks + 'r' (5), 4 ranks + "ss"/"ds" (6)
    if (len == 5 || len == 6) {
        int pattern_ranks[4];
        // int num_x = 0; // Not directly used after parsing ranks into pattern_ranks
        bool parse_ranks_ok = true;
        for (int i = 0; i < 4; ++i) {
            char rank_char = hand_str[i];
            if (rank_char == 'x' || rank_char == 'X') {
                pattern_ranks[i] = RANK_PLACEHOLDER_MARKER;
                // num_x++;
            } else {
                pattern_ranks[i] = char_to_rank(rank_char);
                if (pattern_ranks[i] == StdDeck_Rank_COUNT) {
                    parse_ranks_ok = false; break;
                }
            }
        }

        if (!parse_ranks_ok) {
            snprintf(hand->notation, sizeof(hand->notation), "%s", "INVALID_RANK_IN_PATTERN");
            return 0;
        }

        PLOSuitedness target_suitedness;
        const char* suffix_ptr = &hand_str[4];
        size_t suffix_len = len - 4;

        if (suffix_len == 1 && (suffix_ptr[0] == 'r' || suffix_ptr[0] == 'R')) {
            target_suitedness = PLO_SUIT_RAINBOW;
        } else if (suffix_len == 2 && ((suffix_ptr[0] == 's' || suffix_ptr[0] == 'S') && (suffix_ptr[1] == 's' || suffix_ptr[1] == 'S'))) {
            target_suitedness = PLO_SUIT_SINGLE;
        } else if (suffix_len == 2 && ((suffix_ptr[0] == 'd' || suffix_ptr[0] == 'D') && (suffix_ptr[1] == 's' || suffix_ptr[1] == 'S'))) {
            target_suitedness = PLO_SUIT_DOUBLE;
        } else {
            snprintf(hand->notation, sizeof(hand->notation), "%s", "INVALID_SUFFIX_IN_PATTERN");
            return 0;
        }
        
        StdDeck_CardMask generated_mask;
        if (generate_sample_mask_for_pattern(pattern_ranks, target_suitedness, &generated_mask)) {
            hand->cards = generated_mask;
            PLO_CategorizeHand(hand->cards, hand); 
            // hand->suitedness will be set by PLO_CategorizeHand based on the generated_mask.
            // This should match target_suitedness due to generate_sample_mask_for_pattern's logic.
            
            strncpy(hand->notation, hand_str, sizeof(hand->notation) - 1); // Store original pattern
            hand->notation[sizeof(hand->notation) - 1] = '\0';
            return 1; // Success
        } else {
            snprintf(hand->notation, sizeof(hand->notation), "%s", "IMPOSSIBLE_PATTERN");
            return 0;
        }
    }
    
    // If no parsing method worked, hand->notation is already "INVALID_INPUT" or a more specific error.
    return 0;
}

const char* PLO_SuitednessSuffix(PLOSuitedness suitedness) {
    switch (suitedness) {
        case PLO_SUIT_DOUBLE: return "ds";
        case PLO_SUIT_SINGLE: return "ss";
        case PLO_SUIT_RAINBOW: return "r";
        case PLO_SUIT_TRIPLE: return "ts"; // Or some other convention for 3-suited
        case PLO_SUIT_QUAD: return "qs";   // Or monotone
        default: return "";
    }
}

char* PLO_HandToString(const PLOHand* hand, char* buffer, size_t bufsize) {
    if (!hand || !buffer || bufsize == 0) return NULL;

    // Convert each card in the mask to string
    char compact_cards[9] = {0};
    int card_count = 0;
    
    for (int i = 0; i < StdDeck_N_CARDS && card_count < 4; ++i) {
        if (StdDeck_CardMask_CARD_IS_SET(hand->cards, i)) {
            char card_str[3];
            StdDeck_cardToString(i, card_str);
            compact_cards[card_count * 2] = card_str[0];
            compact_cards[card_count * 2 + 1] = card_str[1];
            card_count++;
        }
    }
    compact_cards[8] = '\0';

    snprintf(buffer, bufsize, "%s%s", compact_cards, PLO_SuitednessSuffix(hand->suitedness));
    return buffer;
}

const char* PLO_CategoryName(PLOHandCategory category) {
    switch (category) {
        case PLO_CAT_UNPAIRED_DS: return "Unpaired Double-Suited";
        case PLO_CAT_UNPAIRED_SS: return "Unpaired Single-Suited";
        case PLO_CAT_UNPAIRED_RB: return "Unpaired Rainbow";
        case PLO_CAT_PAIR_DS: return "Pair Double-Suited";
        case PLO_CAT_PAIR_SS: return "Pair Single-Suited";
        case PLO_CAT_PAIR_RB: return "Pair Rainbow";
        case PLO_CAT_TWO_PAIR_DS: return "Two-Pair Double-Suited";
        case PLO_CAT_TWO_PAIR_SS: return "Two-Pair Single-Suited";
        case PLO_CAT_TWO_PAIR_RB: return "Two-Pair Rainbow";
        case PLO_CAT_TRIPS_DS: return "Trips Double-Suited";
        case PLO_CAT_TRIPS_SS: return "Trips Single-Suited";
        case PLO_CAT_TRIPS_RB: return "Trips Rainbow";
        case PLO_CAT_AA_DS: return "Aces Double-Suited";
        case PLO_CAT_AA_SS: return "Aces Single-Suited";
        case PLO_CAT_AA_RB: return "Aces Rainbow";
        case PLO_CAT_BROADWAY_DS: return "3+ Broadway Double-Suited";
        case PLO_CAT_BROADWAY_SS: return "3+ Broadway Single-Suited";
        case PLO_CAT_BROADWAY_RB: return "3+ Broadway Rainbow";
        case PLO_CAT_RAGGED_DS: return "Ragged/Low Double-Suited";
        case PLO_CAT_RAGGED_SS: return "Ragged/Low Single-Suited";
        case PLO_CAT_RAGGED_RB: return "Ragged/Low Rainbow";
        case PLO_CAT_INVALID: return "Invalid PLO Hand";
        default: return "Unknown Category";
    }
}

int PLO_MatchesPattern(const PLOHand* hand, const char* pattern) {
    if (!hand || !pattern) return 0;
    size_t len = strnlen(pattern, 32);

    // 1. Extraire les rangs du pattern et le suffixe
    int pattern_ranks[4];
    for (int i = 0; i < 4; ++i) {
        char c = pattern[i];
        if (c == 'x' || c == 'X') {
            pattern_ranks[i] = RANK_PLACEHOLDER_MARKER;
        } else {
            pattern_ranks[i] = char_to_rank(c);
            if (pattern_ranks[i] == StdDeck_Rank_COUNT) return 0;
        }
    }
    const char* suffix_ptr = &pattern[4];
    size_t suffix_len = len - 4;
    PLOSuitedness target_suitedness;
    if (suffix_len == 1 && (suffix_ptr[0] == 'r' || suffix_ptr[0] == 'R')) {
        target_suitedness = PLO_SUIT_RAINBOW;
    } else if (suffix_len == 2 && ((suffix_ptr[0] == 's' || suffix_ptr[0] == 'S') && (suffix_ptr[1] == 's' || suffix_ptr[1] == 'S'))) {
        target_suitedness = PLO_SUIT_SINGLE;
    } else if (suffix_len == 2 && ((suffix_ptr[0] == 'd' || suffix_ptr[0] == 'D') && (suffix_ptr[1] == 's' || suffix_ptr[1] == 'S'))) {
        target_suitedness = PLO_SUIT_DOUBLE;
    } else {
        return 0;
    }

    // 2. Vérifier la suitedness
    if (hand->suitedness != target_suitedness) return 0;

    // 3. Vérifier les rangs
    int hand_ranks[4];
    int hand_suits[4];
    int num_cards;
    get_cards_details(hand->cards, hand_ranks, hand_suits, &num_cards);
    if (num_cards != 4) return 0;

    // Marquer les rangs de la main déjà utilisés
    int used[4] = {0,0,0,0};
    // Pour chaque rang du pattern
    for (int i = 0; i < 4; ++i) {
        if (pattern_ranks[i] == RANK_PLACEHOLDER_MARKER) continue;
        int found = 0;
        for (int j = 0; j < 4; ++j) {
            if (!used[j] && hand_ranks[j] == pattern_ranks[i]) {
                used[j] = 1;
                found = 1;
                break;
            }
        }
        if (!found) return 0; // Rang fixe non trouvé
    }
    // Les 'x' doivent matcher n'importe quel rang restant, mais tous différents des rangs fixes déjà utilisés
    int x_needed = 0;
    for (int i = 0; i < 4; ++i) if (pattern_ranks[i] == RANK_PLACEHOLDER_MARKER) x_needed++;
    int x_found = 0;
    for (int j = 0; j < 4; ++j) {
        if (!used[j]) x_found++;
    }
    if (x_found != x_needed) return 0;
    // Tous les rangs de la main doivent être différents pour les 'x' (pas de doublon avec les rangs fixes déjà utilisés)
    // (déjà garanti par la logique ci-dessus)
    return 1;
}

float PLO_CategoryPercentage(PLOHandCategory category) {
    // Approximate percentages from the provided nomenclature
    switch (category) {
        case PLO_CAT_INVALID: return 0.0f;
        case PLO_CAT_UNPAIRED_DS: return 9.5f;
        case PLO_CAT_UNPAIRED_SS: return 26.9f;
        case PLO_CAT_UNPAIRED_RB: return 6.3f;
        case PLO_CAT_PAIR_DS: return 3.6f;
        case PLO_CAT_PAIR_SS: return 20.6f;
        case PLO_CAT_PAIR_RB: return 23.8f; // Note: Original doc has xxyy for one-pair, but table has xxyy for two-pair. Assuming one-pair.
        case PLO_CAT_TWO_PAIR_DS: return 1.1f;
        case PLO_CAT_TWO_PAIR_SS: return 10.0f;
        case PLO_CAT_TWO_PAIR_RB: return 0.8f;
        case PLO_CAT_TRIPS_DS: return 0.18f;
        case PLO_CAT_TRIPS_SS: return 1.6f;
        case PLO_CAT_TRIPS_RB: return 0.13f;
        case PLO_CAT_AA_DS: return 0.45f;
        case PLO_CAT_AA_SS: return 3.9f;
        case PLO_CAT_AA_RB: return 0.3f;
        case PLO_CAT_BROADWAY_DS: return 2.2f;
        case PLO_CAT_BROADWAY_SS: return 7.7f;
        case PLO_CAT_BROADWAY_RB: return 0.7f;
        case PLO_CAT_RAGGED_DS: return 2.6f;
        case PLO_CAT_RAGGED_SS: return 7.0f;
        case PLO_CAT_RAGGED_RB: return 2.0f;
        default: return 0.0f;
    }
}
