#include <poker_eval/distributions/omaha_distributions.h>
#include <string.h>
#include <poker_eval/core/string_compat.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h> // Added for bool type

// --- Memory Management Functions ---

// Initialize an OmahaHandList with a given initial capacity
int OmahaHandList_Init(OmahaHandList* handList, int initial_capacity) {
    if (!handList) {
        TRACE_OHD("OmahaHandList_Init: Null handList pointer.\n");
        return 0;
    }
    
    if (initial_capacity <= 0) {
        initial_capacity = OMAHA_HANDLIST_INITIAL_CAPACITY;
    }
    
    handList->hands = (StdDeck_CardMask*)malloc(initial_capacity * sizeof(StdDeck_CardMask));
    if (!handList->hands) {
        TRACE_OHD("OmahaHandList_Init: Failed to allocate memory for %d hands.\n", initial_capacity);
        return 0;
    }
    
    handList->count = 0;
    handList->capacity = initial_capacity;
    return 1;
}

// Free the memory allocated for an OmahaHandList
void OmahaHandList_Free(OmahaHandList* handList) {
    if (handList && handList->hands) {
        free(handList->hands);
        handList->hands = NULL;
        handList->count = 0;
        handList->capacity = 0;
    }
}

// Resize an OmahaHandList to a new capacity
int OmahaHandList_Resize(OmahaHandList* handList, int new_capacity) {
    if (!handList) {
        TRACE_OHD("OmahaHandList_Resize: Null handList pointer.\n");
        return 0;
    }
    
    if (new_capacity <= handList->count) {
        TRACE_OHD("OmahaHandList_Resize: New capacity %d is less than current count %d.\n", 
                  new_capacity, handList->count);
        return 0;
    }
    
    StdDeck_CardMask* new_hands = (StdDeck_CardMask*)realloc(handList->hands, 
                                                              new_capacity * sizeof(StdDeck_CardMask));
    if (!new_hands) {
        TRACE_OHD("OmahaHandList_Resize: Failed to reallocate memory for %d hands.\n", new_capacity);
        return 0;
    }
    
    handList->hands = new_hands;
    handList->capacity = new_capacity;
    return 1;
}

// Add a hand to the list, resizing if necessary
int OmahaHandList_AddHand(OmahaHandList* handList, StdDeck_CardMask hand) {
    if (!handList) {
        TRACE_OHD("OmahaHandList_AddHand: Null handList pointer.\n");
        return 0;
    }
    
    // Check if we need to resize
    if (handList->count >= handList->capacity) {
        int new_capacity = handList->capacity * OMAHA_HANDLIST_GROWTH_FACTOR;
        if (!OmahaHandList_Resize(handList, new_capacity)) {
            TRACE_OHD("OmahaHandList_AddHand: Failed to resize list.\n");
            return 0;
        }
    }
    
    handList->hands[handList->count++] = hand;
    return 1;
}

// Clear the list without freeing memory
void OmahaHandList_Clear(OmahaHandList* handList) {
    if (handList) {
        handList->count = 0;
    }
}

// --- Helper Functions ---

// Converts a rank character to its integer representation.
// Returns 0-12 (2-A), WILDCARD_RANK for 'x'/'X', or -100 for error.
static int ohr_char_to_rank(char r) {
    r = (char)tolower(r);
    if (r >= '2' && r <= '9') return r - '2'; // 2-9 -> 0-7
    switch (r) {
        case 't': return 8;  // Ten
        case 'j': return 9;  // Jack
        case 'q': return 10; // Queen
        case 'k': return 11; // King
        case 'a': return 12; // Ace
        case 'x': return WILDCARD_RANK;
        default:  return -100; // Error
    }
}

// Converts a suit character to its integer representation.
// Returns 0-3 (c,d,h,s), or -100 for error.
// Assumes StdDeck_Suit_CLUBS = 0, StdDeck_Suit_DIAMONDS = 1, etc.
static int ohr_char_to_suit(char s) {
    s = (char)tolower(s);
    switch (s) {
        case 'c': return 0; // StdDeck_Suit_CLUBS
        case 'd': return 1; // StdDeck_Suit_DIAMONDS
        case 'h': return 2; // StdDeck_Suit_HEARTS
        case 's': return 3; // StdDeck_Suit_SPADES
        default:  return -100; // Error
    }
}

// --- Main Parsing Function ---

int OmahaHand_Parse(const char* handText, OmahaHandQuery* query) {
    if (!handText || !query) {
        TRACE_OHD("OmahaHand_Parse: Null handText or query pointer.\n");
        return 0;
    }

    // 1. Initialize query
    query->suit_property = OMAHA_SUIT_PROPERTY_NONE;
    query->num_specific_cards = 0;
    query->num_rank_wildcards = 0;
    query->num_required_ranks = 0;
    for (int i = 0; i < 4; ++i) {
        query->patterns[i].rank = WILDCARD_RANK;
        query->patterns[i].suit = WILDCARD_SUIT;
        query->required_ranks[i] = -1; // Sentinel for not set
    }

    // 2. Make a mutable copy of handText
    char* hand_str_copy = strdup(handText);
    if (!hand_str_copy) {
        TRACE_OHD("OmahaHand_Parse: Failed to duplicate handText.\n");
        return 0;
    }
    // char* current_pos = hand_str_copy; // current_pos is not used
    int len = (int)strlen(hand_str_copy);

    // 3. Identify and parse a potential suit suffix
    const char* suffixes[] = {"ds", "ss", "ts", "qs", "r", "rb"};
    OmahaSuitProperty props[] = {
        OMAHA_SUIT_PROPERTY_DS, OMAHA_SUIT_PROPERTY_SS, OMAHA_SUIT_PROPERTY_TS,
        OMAHA_SUIT_PROPERTY_QS, OMAHA_SUIT_PROPERTY_RAINBOW, OMAHA_SUIT_PROPERTY_RAINBOW
    };
    int num_suffixes = sizeof(suffixes) / sizeof(suffixes[0]);

    for (int i = 0; i < num_suffixes; ++i) {
        int suffix_len = (int)strlen(suffixes[i]);
        if (len >= suffix_len && strcasecmp(hand_str_copy + len - suffix_len, suffixes[i]) == 0) {
            query->suit_property = props[i];
            hand_str_copy[len - suffix_len] = '\0'; // Remove suffix
            TRACE_OHD("OmahaHand_Parse: Found suit property: %s\n", suffixes[i]);
            break;
        }
    }
    
    // Update len after potential suffix removal
    len = (int)strlen(hand_str_copy);

    // 4. Process the remaining card string part
    int card_idx = 0;
    int char_idx = 0;

    while (char_idx < len && card_idx < 4) {
        if (!isgraph(hand_str_copy[char_idx])) { // skip whitespace, should not be there
            char_idx++;
            continue;
        }

        char rank_char = hand_str_copy[char_idx];
        int rank_val = ohr_char_to_rank(rank_char);

        if (rank_val == -100) {
            TRACE_OHD("OmahaHand_Parse: Invalid rank character '%c' at position %d.\n", rank_char, char_idx);
            free(hand_str_copy);
            return 0;
        }
        query->patterns[card_idx].rank = rank_val;
        char_idx++;

        if (rank_val != WILDCARD_RANK) {
            // Add to required_ranks if it's a specific rank and not already present
            int found = 0;
            for (int i = 0; i < query->num_required_ranks; ++i) {
                if (query->required_ranks[i] == rank_val) {
                    found = 1;
                    break;
                }
            }
            if (!found && query->num_required_ranks < 4) {
                query->required_ranks[query->num_required_ranks++] = rank_val;
            }
        } else {
            query->num_rank_wildcards++;
        }

        // Check for suit character
        if (char_idx < len && isalpha(hand_str_copy[char_idx])) {
            char suit_char = hand_str_copy[char_idx];
            int suit_val = ohr_char_to_suit(suit_char);

            if (suit_val != -100) { // It's a suit char
                if (rank_val == WILDCARD_RANK) {
                     TRACE_OHD("OmahaHand_Parse: Suit '%c' specified for wildcard rank at card %d.\n", suit_char, card_idx + 1);
                     free(hand_str_copy);
                     return 0;
                }
                query->patterns[card_idx].suit = suit_val;
                query->num_specific_cards++;
                char_idx++;
            } else { 
                if (query->suit_property != OMAHA_SUIT_PROPERTY_NONE) {
                    query->patterns[card_idx].suit = SUIT_DETERMINED_BY_PROPERTY;
                }
            }
        } else {
            if (query->suit_property != OMAHA_SUIT_PROPERTY_NONE) {
                query->patterns[card_idx].suit = SUIT_DETERMINED_BY_PROPERTY;
            }
        }
        card_idx++;
    }

    // 5. Perform basic validation
    if (card_idx != 4) {
        TRACE_OHD("OmahaHand_Parse: Incorrect number of card patterns. Expected 4, found %d.\n", card_idx);
        free(hand_str_copy);
        return 0;
    }

    if (char_idx != len) {
        TRACE_OHD("OmahaHand_Parse: Did not consume entire hand string. Remainder: '%s'\n", hand_str_copy + char_idx);
        free(hand_str_copy);
        return 0;
    }
    
    int specified_ranks_count = 0;
    for(int i=0; i<4; ++i) {
        if(query->patterns[i].rank != WILDCARD_RANK) {
            specified_ranks_count++;
        }
    }
    if (specified_ranks_count + query->num_rank_wildcards != 4) {
         TRACE_OHD("OmahaHand_Parse: Mismatch in parsed card components. Specified ranks: %d, Wildcards: %d. Should sum to 4.\n", specified_ranks_count, query->num_rank_wildcards);
         int actual_wildcards = 0;
         for(int i=0; i<4; ++i) if(query->patterns[i].rank == WILDCARD_RANK) actual_wildcards++;
         if (specified_ranks_count + actual_wildcards != 4) {
            TRACE_OHD("OmahaHand_Parse: Re-verified mismatch. Specified ranks: %d, Actual pattern wildcards: %d.\n", specified_ranks_count, actual_wildcards);
            free(hand_str_copy);
            return 0;
         }
         query->num_rank_wildcards = actual_wildcards; 
    }

    free(hand_str_copy);
    TRACE_OHD("OmahaHand_Parse: Successfully parsed hand. Specific: %d, Wild Ranks: %d, Suit Prop: %d, Req Ranks: %d\n",
           query->num_specific_cards, query->num_rank_wildcards, query->suit_property, query->num_required_ranks);

    return 1;
}

// --- Instantiation Helper Functions ---

// Helper function to count cards in a mask
// Assumes StdDeck_CardMask_CARD_IS_SET and StdDeck_N_CARDS are available from deck_std.h
static int count_set_bits(StdDeck_CardMask mask) {
    int count = 0;
    for (int i = 0; i < StdDeck_N_CARDS; i++) { // StdDeck_N_CARDS is typically 52
        if (StdDeck_CardMask_CARD_IS_SET(mask, i)) {
            count++;
        }
    }
    return count;
}

// Helper function to get suits of cards in a 4-card mask
// Fills an array 'suits_counts' of size 4 (one for each suit)
// Returns 0 on error (e.g. not a 4-card hand), 1 on success
// Assumes StdDeck_SUIT is available from deck_std.h
static int get_suit_counts_omaha(StdDeck_CardMask hand, int* suit_counts) {
    if (count_set_bits(hand) != 4) {
        TRACE_OHD("Error: Not a 4-card hand for suit count.\n");
        return 0;
    }
    for (int i = 0; i < 4; i++) suit_counts[i] = 0;

    for (int card_idx = 0; card_idx < StdDeck_N_CARDS; card_idx++) {
        if (StdDeck_CardMask_CARD_IS_SET(hand, card_idx)) {
            suit_counts[StdDeck_SUIT(card_idx)]++;
        }
    }
    return 1;
}

static bool is_double_suited(StdDeck_CardMask hand) {
    int suit_counts[4];
    if (!get_suit_counts_omaha(hand, suit_counts)) return false;
    // Pattern for double suited: 2-2-0-0 (two suits have 2 cards, two suits have 0)
    int pairs = 0;
    for (int i = 0; i < 4; i++) {
        if (suit_counts[i] == 2) pairs++;
    }
    return pairs == 2;
}

static bool is_single_suited(StdDeck_CardMask hand) {
    int suit_counts[4];
    if (!get_suit_counts_omaha(hand, suit_counts)) return false;
    // Pattern for single suited: 2-1-1-0 (one suit has 2 cards, two suits have 1, one has 0)
    bool one_pair = false;
    int singles = 0;
    for (int i = 0; i < 4; i++) {
        if (suit_counts[i] == 2) one_pair = true;
        else if (suit_counts[i] == 1) singles++;
    }
    return one_pair && singles == 2;
}

static bool is_triple_suited(StdDeck_CardMask hand) {
    int suit_counts[4];
    if (!get_suit_counts_omaha(hand, suit_counts)) return false;
    // Pattern for triple suited: 3-1-0-0 (one suit has 3 cards, one suit has 1)
    bool three_of_suit = false;
    bool one_of_suit = false;
    for (int i = 0; i < 4; i++) {
        if (suit_counts[i] == 3) three_of_suit = true;
        else if (suit_counts[i] == 1) one_of_suit = true;
    }
    return three_of_suit && one_of_suit;
}

static bool is_quad_suited(StdDeck_CardMask hand) {
    int suit_counts[4];
    if (!get_suit_counts_omaha(hand, suit_counts)) return false;
    // Pattern for quad suited (monotone): 4-0-0-0 (one suit has all 4 cards)
    for (int i = 0; i < 4; i++) {
        if (suit_counts[i] == 4) return true;
    }
    return false;
}

static bool is_rainbow(StdDeck_CardMask hand) {
    int suit_counts[4];
    if (!get_suit_counts_omaha(hand, suit_counts)) return false;
    // Pattern for rainbow: 1-1-1-1 (each suit has 1 card)
    int singles = 0;
    for (int i = 0; i < 4; i++) {
        if (suit_counts[i] == 1) singles++;
    }
    return singles == 4;
}

// --- Main Instantiation Function ---

// Main instantiation function
int OmahaHand_Instantiate(const OmahaHandQuery* query, StdDeck_CardMask deadCards, OmahaHandList* handList) {
    if (!query || !handList) {
        TRACE_OHD("Error: Null query or handList passed to OmahaHand_Instantiate.\n");
        return -1; // Return negative for error
    }

    // Check if handList is initialized
    if (!handList->hands && handList->capacity > 0) {
        TRACE_OHD("Error: OmahaHandList not properly initialized. Call OmahaHandList_Init first.\n");
        return -1;
    }
    
    // Auto-initialize if not initialized
    if (!handList->hands) {
        if (!OmahaHandList_Init(handList, OMAHA_HANDLIST_INITIAL_CAPACITY)) {
            TRACE_OHD("Error: Failed to auto-initialize OmahaHandList.\n");
            return -1;
        }
    }

    handList->count = 0;

    if (query->num_specific_cards == 4) {
        StdDeck_CardMask current_hand;
        StdDeck_CardMask_RESET(current_hand);
        bool possible = true;
        for (int i = 0; i < 4; i++) {
            // This path should only be taken if all cards are fully specified (rank and suit).
            // The parser should ensure patterns[i].rank and patterns[i].suit are valid.
            if (query->patterns[i].rank == WILDCARD_RANK || query->patterns[i].suit == WILDCARD_SUIT || query->patterns[i].suit == SUIT_DETERMINED_BY_PROPERTY) {
                possible = false; 
                TRACE_OHD("Instantiate Error (Specifics): Pattern %d not fully specific despite num_specific_cards=4.\n", i);
                break;
            }
            int card_index = StdDeck_MAKE_CARD(query->patterns[i].rank, query->patterns[i].suit);
            StdDeck_CardMask card_m = StdDeck_MASK(card_index);
            
            if (StdDeck_CardMask_ANY_SET(current_hand, card_m) || StdDeck_CardMask_ANY_SET(deadCards, card_m)) {
                possible = false;
                TRACE_OHD("Instantiate Error (Specifics): Card conflict for %d%d.\n", query->patterns[i].rank, query->patterns[i].suit);
                break;
            }
            StdDeck_CardMask_OR(current_hand, current_hand, card_m);
        }

        if (possible && count_set_bits(current_hand) == 4) {
            bool suit_property_met = false;
            switch (query->suit_property) {
                case OMAHA_SUIT_PROPERTY_NONE: suit_property_met = true; break;
                case OMAHA_SUIT_PROPERTY_DS: suit_property_met = is_double_suited(current_hand); break;
                case OMAHA_SUIT_PROPERTY_SS: suit_property_met = is_single_suited(current_hand); break;
                case OMAHA_SUIT_PROPERTY_TS: suit_property_met = is_triple_suited(current_hand); break;
                case OMAHA_SUIT_PROPERTY_QS: suit_property_met = is_quad_suited(current_hand); break;
                case OMAHA_SUIT_PROPERTY_RAINBOW: suit_property_met = is_rainbow(current_hand); break;
                default: TRACE_OHD("Error: Unknown suit property in instantiate (Specifics): %d\n", query->suit_property); suit_property_met = false; break;
            }
            if (suit_property_met) {
                if (!OmahaHandList_AddHand(handList, current_hand)) {
                    TRACE_OHD("Error: Failed to add hand (Specifics).\n");
                    return -1; 
                }
            } else {
                 TRACE_OHD("Instantiate Info (Specifics): Hand does not match suit property %d.\n", query->suit_property);
            }
        }
        return handList->count;
    } 
    // If not num_specific_cards == 4, then proceed with rank-based logic combined with suit properties
    else if (query->num_required_ranks == 1 && query->num_rank_wildcards == 1 &&
             (query->patterns[0].rank != WILDCARD_RANK && 
              query->patterns[0].rank == query->patterns[1].rank &&
              query->patterns[0].rank == query->patterns[2].rank &&
              query->patterns[3].rank == WILDCARD_RANK)
        ) { // RRRx type (e.g. AAAx)
        int specific_rank = query->patterns[0].rank; // The rank for trips

        StdDeck_CardMask card_options[4]; // Cards of the specific rank
        for (int s = 0; s < 4; ++s) {
            card_options[s] = StdDeck_MASK(StdDeck_MAKE_CARD(specific_rank, s));
        }

        // Iterate for the first card of the trips
        for (int i = 0; i < 4; ++i) {
            if (StdDeck_CardMask_ANY_SET(deadCards, card_options[i])) continue;
            
            // Iterate for the second card of the trips
            for (int j = i + 1; j < 4; ++j) {
                if (StdDeck_CardMask_ANY_SET(deadCards, card_options[j])) continue;

                // Iterate for the third card of the trips
                for (int k = j + 1; k < 4; ++k) {
                    if (StdDeck_CardMask_ANY_SET(deadCards, card_options[k])) continue;

                    StdDeck_CardMask trips_hand;
                    StdDeck_CardMask_RESET(trips_hand);
                    StdDeck_CardMask_OR(trips_hand, card_options[i], card_options[j]);
                    StdDeck_CardMask_OR(trips_hand, trips_hand, card_options[k]);

                    // Now choose 1 wildcard card (kicker)
                    for (int c4_idx = 0; c4_idx < StdDeck_N_CARDS; ++c4_idx) {
                        StdDeck_CardMask kicker_mask = StdDeck_MASK(c4_idx);
                        if (StdDeck_RANK(c4_idx) == specific_rank) continue; // Kicker cannot be same rank as trips
                        if (StdDeck_CardMask_ANY_SET(deadCards, kicker_mask)) continue;
                        if (StdDeck_CardMask_ANY_SET(trips_hand, kicker_mask)) continue; 

                        StdDeck_CardMask current_hand;
                        StdDeck_CardMask_OR(current_hand, trips_hand, kicker_mask);
                                
                        if (count_set_bits(current_hand) == 4) {
                            // Apply universal suit property filter
                            bool suit_property_met = false;
                            switch (query->suit_property) {
                                case OMAHA_SUIT_PROPERTY_NONE: suit_property_met = true; break;
                                case OMAHA_SUIT_PROPERTY_DS: suit_property_met = is_double_suited(current_hand); break;
                                case OMAHA_SUIT_PROPERTY_SS: suit_property_met = is_single_suited(current_hand); break;
                                case OMAHA_SUIT_PROPERTY_TS: suit_property_met = is_triple_suited(current_hand); break;
                                case OMAHA_SUIT_PROPERTY_QS: suit_property_met = is_quad_suited(current_hand); break;
                                case OMAHA_SUIT_PROPERTY_RAINBOW: suit_property_met = is_rainbow(current_hand); break;
                                default: TRACE_OHD("Error: Unknown suit property (RRRx): %d\n", query->suit_property); suit_property_met = false; break;
                            }

                            if (suit_property_met) {
                                if (!OmahaHandList_AddHand(handList, current_hand)) {
                                    TRACE_OHD("Error: Failed to add hand for RRRx type.\n");
                                    return -1; 
                                }
                            }
                        }
                    }
                }
            }
        }
        return handList->count;
    } else if (query->num_required_ranks == 1 && query->num_rank_wildcards == 2) { // AAxx type
        int specific_rank = query->required_ranks[0];
        StdDeck_CardMask card_options[4]; 
        int num_card_options = 0;
        for (int s = 0; s < 4; ++s) { 
            card_options[num_card_options++] = StdDeck_MASK(StdDeck_MAKE_CARD(specific_rank, s));
        }
        for (int i = 0; i < num_card_options; ++i) {
            if (StdDeck_CardMask_ANY_SET(deadCards, card_options[i])) continue;
            for (int j = i + 1; j < num_card_options; ++j) {
                if (StdDeck_CardMask_ANY_SET(deadCards, card_options[j])) continue;
                StdDeck_CardMask pair_hand;
                StdDeck_CardMask_RESET(pair_hand); 
                StdDeck_CardMask_OR(pair_hand, card_options[i], card_options[j]);
                for (int c3_idx = 0; c3_idx < StdDeck_N_CARDS; ++c3_idx) {
                    StdDeck_CardMask card3_mask = StdDeck_MASK(c3_idx);
                    if (StdDeck_RANK(c3_idx) == specific_rank) continue; 
                    if (StdDeck_CardMask_ANY_SET(deadCards, card3_mask)) continue;
                    if (StdDeck_CardMask_ANY_SET(pair_hand, card3_mask)) continue;
                    for (int c4_idx = c3_idx + 1; c4_idx < StdDeck_N_CARDS; ++c4_idx) {
                        StdDeck_CardMask card4_mask = StdDeck_MASK(c4_idx);
                        if (StdDeck_RANK(c4_idx) == specific_rank) continue; 
                        if (StdDeck_CardMask_ANY_SET(deadCards, card4_mask)) continue;
                        if (StdDeck_CardMask_ANY_SET(pair_hand, card4_mask)) continue; 
                        StdDeck_CardMask current_hand;
                        StdDeck_CardMask_OR(current_hand, pair_hand, card3_mask); 
                        StdDeck_CardMask_OR(current_hand, current_hand, card4_mask);
                        if (count_set_bits(current_hand) == 4) {
                            bool suit_property_met = false;
                            switch (query->suit_property) {
                                case OMAHA_SUIT_PROPERTY_NONE: suit_property_met = true; break;
                                case OMAHA_SUIT_PROPERTY_DS: suit_property_met = is_double_suited(current_hand); break;
                                case OMAHA_SUIT_PROPERTY_SS: suit_property_met = is_single_suited(current_hand); break;
                                case OMAHA_SUIT_PROPERTY_TS: suit_property_met = is_triple_suited(current_hand); break;
                                case OMAHA_SUIT_PROPERTY_QS: suit_property_met = is_quad_suited(current_hand); break;
                                case OMAHA_SUIT_PROPERTY_RAINBOW: suit_property_met = is_rainbow(current_hand); break;
                                default: TRACE_OHD("Error: Unknown suit property (AAxx): %d\n", query->suit_property); suit_property_met = false; break;
                            }
                            if (suit_property_met) {
                                if (!OmahaHandList_AddHand(handList, current_hand)) {
                                    TRACE_OHD("Error: Failed to add hand (AAxx).\n");
                                    return -1; 
                                }
                            }
                        }
                    }
                }
            }
        }
        return handList->count;
    } else if (query->num_required_ranks == 1 && query->num_rank_wildcards == 3) { // Axxx type
        int specific_rank = query->required_ranks[0];
        for (int s = 0; s < 4; ++s) { 
            int c1_card_idx = StdDeck_MAKE_CARD(specific_rank, s);
            StdDeck_CardMask card1_mask = StdDeck_MASK(c1_card_idx);
            if (StdDeck_CardMask_ANY_SET(deadCards, card1_mask)) continue;
            for (int c2_idx = 0; c2_idx < StdDeck_N_CARDS; ++c2_idx) {
                if (StdDeck_RANK(c2_idx) == specific_rank) continue;
                StdDeck_CardMask card2_mask = StdDeck_MASK(c2_idx);
                if (StdDeck_CardMask_CARD_IS_SET(card1_mask, c2_idx)) continue; 
                if (StdDeck_CardMask_ANY_SET(deadCards, card2_mask)) continue;
                for (int c3_idx = c2_idx + 1; c3_idx < StdDeck_N_CARDS; ++c3_idx) {
                    if (StdDeck_RANK(c3_idx) == specific_rank) continue;
                    StdDeck_CardMask card3_mask = StdDeck_MASK(c3_idx);
                    if (StdDeck_CardMask_CARD_IS_SET(card1_mask, c3_idx)) continue; 
                    if (StdDeck_CardMask_ANY_SET(deadCards, card3_mask)) continue;
                    for (int c4_idx = c3_idx + 1; c4_idx < StdDeck_N_CARDS; ++c4_idx) {
                        if (StdDeck_RANK(c4_idx) == specific_rank) continue;
                        StdDeck_CardMask card4_mask = StdDeck_MASK(c4_idx);
                        if (StdDeck_CardMask_CARD_IS_SET(card1_mask, c4_idx)) continue; 
                        if (StdDeck_CardMask_ANY_SET(deadCards, card4_mask)) continue;
                        StdDeck_CardMask current_hand;
                        StdDeck_CardMask_RESET(current_hand);
                        StdDeck_CardMask_OR(current_hand, current_hand, card1_mask);
                        StdDeck_CardMask_OR(current_hand, current_hand, card2_mask);
                        StdDeck_CardMask_OR(current_hand, current_hand, card3_mask);
                        StdDeck_CardMask_OR(current_hand, current_hand, card4_mask);
                        if (count_set_bits(current_hand) == 4) {
                             bool suit_property_met = false;
                            switch (query->suit_property) {
                                case OMAHA_SUIT_PROPERTY_NONE: suit_property_met = true; break;
                                case OMAHA_SUIT_PROPERTY_DS: suit_property_met = is_double_suited(current_hand); break;
                                case OMAHA_SUIT_PROPERTY_SS: suit_property_met = is_single_suited(current_hand); break;
                                case OMAHA_SUIT_PROPERTY_TS: suit_property_met = is_triple_suited(current_hand); break;
                                case OMAHA_SUIT_PROPERTY_QS: suit_property_met = is_quad_suited(current_hand); break;
                                case OMAHA_SUIT_PROPERTY_RAINBOW: suit_property_met = is_rainbow(current_hand); break;
                                default: TRACE_OHD("Error: Unknown suit property (Axxx): %d\n", query->suit_property); suit_property_met = false; break;
                            }
                            if (suit_property_met) {
                                if (!OmahaHandList_AddHand(handList, current_hand)) {
                                    TRACE_OHD("Error: Failed to add hand (Axxx).\n");
                                    return -1; 
                                }
                            }
                        }
                    }
                }
            }
        }
        return handList->count;
    } else if (query->num_required_ranks == 2 && query->num_rank_wildcards == 2) { // AKxx type
        int rank1 = query->required_ranks[0]; 
        int rank2 = query->required_ranks[1]; 
        StdDeck_CardMask r1_cs[4], r2_cs[4];
        for(int s=0;s<4;++s){ r1_cs[s]=StdDeck_MASK(StdDeck_MAKE_CARD(rank1,s)); r2_cs[s]=StdDeck_MASK(StdDeck_MAKE_CARD(rank2,s)); }
        for (int i = 0; i < 4; ++i) {
            if (StdDeck_CardMask_ANY_SET(deadCards, r1_cs[i])) continue;
            for (int j = 0; j < 4; ++j) {
                if (StdDeck_CardMask_ANY_SET(deadCards, r2_cs[j])) continue;
                StdDeck_CardMask base_hand; StdDeck_CardMask_RESET(base_hand);
                StdDeck_CardMask_OR(base_hand, r1_cs[i], r2_cs[j]);
                if (count_set_bits(base_hand) != 2) continue;
                for (int c3_idx = 0; c3_idx < StdDeck_N_CARDS; ++c3_idx) {
                    StdDeck_CardMask c3_m = StdDeck_MASK(c3_idx);
                    if (StdDeck_RANK(c3_idx)==rank1 || StdDeck_RANK(c3_idx)==rank2) continue;
                    if (StdDeck_CardMask_ANY_SET(deadCards,c3_m) || StdDeck_CardMask_ANY_SET(base_hand,c3_m)) continue;
                    for (int c4_idx = c3_idx + 1; c4_idx < StdDeck_N_CARDS; ++c4_idx) {
                        StdDeck_CardMask c4_m = StdDeck_MASK(c4_idx);
                        if (StdDeck_RANK(c4_idx)==rank1 || StdDeck_RANK(c4_idx)==rank2) continue;
                        if (StdDeck_CardMask_ANY_SET(deadCards,c4_m) || StdDeck_CardMask_ANY_SET(base_hand,c4_m)) continue;
                        StdDeck_CardMask current_hand; StdDeck_CardMask_OR(current_hand,base_hand,c3_m); StdDeck_CardMask_OR(current_hand,current_hand,c4_m);
                        if (count_set_bits(current_hand) == 4) {
                            bool suit_property_met = false;
                            switch (query->suit_property) {
                                case OMAHA_SUIT_PROPERTY_NONE: suit_property_met = true; break;
                                case OMAHA_SUIT_PROPERTY_DS: suit_property_met = is_double_suited(current_hand); break;
                                case OMAHA_SUIT_PROPERTY_SS: suit_property_met = is_single_suited(current_hand); break;
                                case OMAHA_SUIT_PROPERTY_TS: suit_property_met = is_triple_suited(current_hand); break;
                                case OMAHA_SUIT_PROPERTY_QS: suit_property_met = is_quad_suited(current_hand); break;
                                case OMAHA_SUIT_PROPERTY_RAINBOW: suit_property_met = is_rainbow(current_hand); break;
                                default: TRACE_OHD("Error: Unknown suit property (AKxx): %d\n", query->suit_property); suit_property_met = false; break;
                            }
                            if (suit_property_met) {
                                if (!OmahaHandList_AddHand(handList, current_hand)) {
                                    TRACE_OHD("Error: Failed to add hand (AKxx).\n");
                                    return -1; 
                                }
                            }
                        }
                    }
                }
            }
        }
        return handList->count;
    } else if (query->num_required_ranks == 3 && query->num_rank_wildcards == 1) { // AKQx type
        int rk1=query->required_ranks[0], rk2=query->required_ranks[1], rk3=query->required_ranks[2];
        StdDeck_CardMask r1cs[4],r2cs[4],r3cs[4];
        for(int s=0;s<4;++s){r1cs[s]=StdDeck_MASK(StdDeck_MAKE_CARD(rk1,s)); r2cs[s]=StdDeck_MASK(StdDeck_MAKE_CARD(rk2,s)); r3cs[s]=StdDeck_MASK(StdDeck_MAKE_CARD(rk3,s));}
        for(int i=0;i<4;++i){ if(StdDeck_CardMask_ANY_SET(deadCards,r1cs[i]))continue;
            for(int j=0;j<4;++j){ if(StdDeck_CardMask_ANY_SET(deadCards,r2cs[j]))continue; if(StdDeck_CardMask_EQUAL(r1cs[i],r2cs[j]))continue;
                for(int k=0;k<4;++k){ if(StdDeck_CardMask_ANY_SET(deadCards,r3cs[k]))continue; if(StdDeck_CardMask_EQUAL(r1cs[i],r3cs[k]))continue; if(StdDeck_CardMask_EQUAL(r2cs[j],r3cs[k]))continue;
                    StdDeck_CardMask base_h; StdDeck_CardMask_RESET(base_h); StdDeck_CardMask_OR(base_h,r1cs[i],r2cs[j]); StdDeck_CardMask_OR(base_h,base_h,r3cs[k]);
                    if(count_set_bits(base_h)!=3)continue;
                    for(int c4_idx=0;c4_idx<StdDeck_N_CARDS;++c4_idx){
                        StdDeck_CardMask c4_m=StdDeck_MASK(c4_idx); int c4_r=StdDeck_RANK(c4_idx);
                        if(c4_r==rk1||c4_r==rk2||c4_r==rk3)continue;
                        if(StdDeck_CardMask_ANY_SET(deadCards,c4_m)||StdDeck_CardMask_ANY_SET(base_h,c4_m))continue;
                        StdDeck_CardMask curr_h; StdDeck_CardMask_OR(curr_h,base_h,c4_m);
                        if(count_set_bits(curr_h)==4){
                            bool suit_property_met = false;
                            switch (query->suit_property) {
                                case OMAHA_SUIT_PROPERTY_NONE: suit_property_met = true; break;
                                case OMAHA_SUIT_PROPERTY_DS: suit_property_met = is_double_suited(curr_h); break;
                                case OMAHA_SUIT_PROPERTY_SS: suit_property_met = is_single_suited(curr_h); break;
                                case OMAHA_SUIT_PROPERTY_TS: suit_property_met = is_triple_suited(curr_h); break;
                                case OMAHA_SUIT_PROPERTY_QS: suit_property_met = is_quad_suited(curr_h); break;
                                case OMAHA_SUIT_PROPERTY_RAINBOW: suit_property_met = is_rainbow(curr_h); break;
                                default: TRACE_OHD("Error: Unknown suit property (AKQx): %d\n", query->suit_property); suit_property_met = false; break;
                            }
                            if (suit_property_met) {
                                if (!OmahaHandList_AddHand(handList, curr_h)) {
                                    TRACE_OHD("Error: Failed to add hand (AKQx).\n");
                                    return -1; 
                                }
                            }
                        }
                    }
                }
            }
        }
        return handList->count;
    } else if (query->num_required_ranks == 4 && query->num_rank_wildcards == 0) { // AKQJ type
        int rk1=query->required_ranks[0], rk2=query->required_ranks[1], rk3=query->required_ranks[2], rk4=query->required_ranks[3];
        StdDeck_CardMask r1o[4],r2o[4],r3o[4],r4o[4];
        for(int s=0;s<4;++s){r1o[s]=StdDeck_MASK(StdDeck_MAKE_CARD(rk1,s)); r2o[s]=StdDeck_MASK(StdDeck_MAKE_CARD(rk2,s)); r3o[s]=StdDeck_MASK(StdDeck_MAKE_CARD(rk3,s)); r4o[s]=StdDeck_MASK(StdDeck_MAKE_CARD(rk4,s));}
        for(int i=0;i<4;++i){ if(StdDeck_CardMask_ANY_SET(deadCards,r1o[i]))continue;
            for(int j=0;j<4;++j){ if(StdDeck_CardMask_ANY_SET(deadCards,r2o[j]))continue;
                for(int k=0;k<4;++k){ if(StdDeck_CardMask_ANY_SET(deadCards,r3o[k]))continue;
                    for(int l=0;l<4;++l){ if(StdDeck_CardMask_ANY_SET(deadCards,r4o[l]))continue;
                        StdDeck_CardMask curr_h; StdDeck_CardMask_RESET(curr_h);
                        StdDeck_CardMask_OR(curr_h,r1o[i],r2o[j]); StdDeck_CardMask_OR(curr_h,curr_h,r3o[k]); StdDeck_CardMask_OR(curr_h,curr_h,r4o[l]);
                        if(count_set_bits(curr_h)==4){
                            bool suit_property_met = false;
                            switch (query->suit_property) {
                                case OMAHA_SUIT_PROPERTY_NONE: suit_property_met = true; break;
                                case OMAHA_SUIT_PROPERTY_DS: suit_property_met = is_double_suited(curr_h); break;
                                case OMAHA_SUIT_PROPERTY_SS: suit_property_met = is_single_suited(curr_h); break;
                                case OMAHA_SUIT_PROPERTY_TS: suit_property_met = is_triple_suited(curr_h); break;
                                case OMAHA_SUIT_PROPERTY_QS: suit_property_met = is_quad_suited(curr_h); break;
                                case OMAHA_SUIT_PROPERTY_RAINBOW: suit_property_met = is_rainbow(curr_h); break;
                                default: TRACE_OHD("Error: Unknown suit property (AKQJ): %d\n", query->suit_property); suit_property_met = false; break;
                            }
                            if (suit_property_met) {
                                if (!OmahaHandList_AddHand(handList, curr_h)) {
                                    TRACE_OHD("Error: Failed to add hand (AKQJ).\n");
                                    return -1; 
                                }
                            }
                        }
                    }
                }
            }
        }
        return handList->count;
    } else if (query->num_required_ranks == 0 && query->num_rank_wildcards == 4) { // "xxxx" - four wildcards
        // Iterate through all C(StdDeck_N_CARDS, 4) card combinations.
        for (int c1_idx = 0; c1_idx < StdDeck_N_CARDS; ++c1_idx) {
            StdDeck_CardMask card1_mask = StdDeck_MASK(c1_idx);
            if (StdDeck_CardMask_ANY_SET(deadCards, card1_mask)) continue;

            for (int c2_idx = c1_idx + 1; c2_idx < StdDeck_N_CARDS; ++c2_idx) {
                StdDeck_CardMask card2_mask = StdDeck_MASK(c2_idx);
                if (StdDeck_CardMask_ANY_SET(deadCards, card2_mask)) continue;

                for (int c3_idx = c2_idx + 1; c3_idx < StdDeck_N_CARDS; ++c3_idx) {
                    StdDeck_CardMask card3_mask = StdDeck_MASK(c3_idx);
                    if (StdDeck_CardMask_ANY_SET(deadCards, card3_mask)) continue;

                    for (int c4_idx = c3_idx + 1; c4_idx < StdDeck_N_CARDS; ++c4_idx) {
                        StdDeck_CardMask card4_mask = StdDeck_MASK(c4_idx);
                        if (StdDeck_CardMask_ANY_SET(deadCards, card4_mask)) continue;

                        StdDeck_CardMask current_hand;
                        StdDeck_CardMask_RESET(current_hand);
                        StdDeck_CardMask_OR(current_hand, card1_mask, card2_mask);
                        StdDeck_CardMask_OR(current_hand, current_hand, card3_mask);
                        StdDeck_CardMask_OR(current_hand, current_hand, card4_mask);
                        
                        // count_set_bits(current_hand) == 4 is true by construction here

                        // Apply universal suit property filter
                        bool suit_property_met = false;
                        switch (query->suit_property) {
                            case OMAHA_SUIT_PROPERTY_NONE: suit_property_met = true; break;
                            case OMAHA_SUIT_PROPERTY_DS: suit_property_met = is_double_suited(current_hand); break;
                            case OMAHA_SUIT_PROPERTY_SS: suit_property_met = is_single_suited(current_hand); break;
                            case OMAHA_SUIT_PROPERTY_TS: suit_property_met = is_triple_suited(current_hand); break;
                            case OMAHA_SUIT_PROPERTY_QS: suit_property_met = is_quad_suited(current_hand); break;
                            case OMAHA_SUIT_PROPERTY_RAINBOW: suit_property_met = is_rainbow(current_hand); break;
                            default: TRACE_OHD("Error: Unknown suit property (xxxx): %d\n", query->suit_property); suit_property_met = false; break;
                        }

                        if (suit_property_met) {
                            if (!OmahaHandList_AddHand(handList, current_hand)) {
                                TRACE_OHD("Error: Failed to add hand for xxxx type.\n");
                                return -1; 
                            }
                        }
                    }
                }
            }
        }
        return handList->count; // After processing all "xxxx" combinations
    } else {
        // This case handles patterns not covered by any specific rank/wildcard counts
        // and is not an "xxxx" pattern.
        TRACE_OHD("OmahaHand_Instantiate: Query pattern (ranks/wildcards combination) not explicitly handled by current logic.\n");
    }
    
    return handList->count; 
}
