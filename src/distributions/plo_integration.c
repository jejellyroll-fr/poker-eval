#include <poker_eval/distributions/plo_integration.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

// Helper function to convert PLO rank to Omaha rank
static int plo_rank_to_omaha_rank(char c) {
    c = (char)tolower(c);
    switch(c) {
        case '2': return 0;
        case '3': return 1;
        case '4': return 2;
        case '5': return 3;
        case '6': return 4;
        case '7': return 5;
        case '8': return 6;
        case '9': return 7;
        case 't': return 8;
        case 'j': return 9;
        case 'q': return 10;
        case 'k': return 11;
        case 'a': return 12;
        case 'x': return WILDCARD_RANK;
        default: return -100;
    }
}

// Helper function to convert PLO suit to Omaha suit
static int plo_suit_to_omaha_suit(char c) {
    c = (char)tolower(c);
    switch(c) {
        case 'c': return 0; // Clubs
        case 'd': return 1; // Diamonds
        case 'h': return 2; // Hearts
        case 's': return 3; // Spades
        default: return -100;
    }
}

int PLO_PatternToOmahaQuery(const char* pattern, OmahaHandQuery* query) {
    if (!pattern || !query) return 0;
    
    // Initialize query
    query->suit_property = OMAHA_SUIT_PROPERTY_NONE;
    query->num_specific_cards = 0;
    query->num_rank_wildcards = 0;
    query->num_required_ranks = 0;
    for (int i = 0; i < 4; ++i) {
        query->patterns[i].rank = WILDCARD_RANK;
        query->patterns[i].suit = WILDCARD_SUIT;
        query->required_ranks[i] = -1;
    }
    
    int len = (int)strlen(pattern);
    
    // Check if it's a specific hand (8 chars like AsKdQhJc)
    if (len == 8) {
        for (int i = 0; i < 4; i++) {
            int rank = plo_rank_to_omaha_rank(pattern[i*2]);
            int suit = plo_suit_to_omaha_suit(pattern[i*2+1]);
            
            if (rank == -100 || suit == -100) return 0;
            
            query->patterns[i].rank = rank;
            query->patterns[i].suit = suit;
            query->num_specific_cards++;
            
            // Add to required ranks
            int found = 0;
            for (int j = 0; j < query->num_required_ranks; j++) {
                if (query->required_ranks[j] == rank) {
                    found = 1;
                    break;
                }
            }
            if (!found && query->num_required_ranks < 4) {
                query->required_ranks[query->num_required_ranks++] = rank;
            }
        }
        return 1;
    }
    
    // Parse pattern with potential wildcards and suffix
    // First, check for suffix
    if (len >= 2) {
        const char* suffix = pattern + len - 2;
        if (strcmp(suffix, "ds") == 0) {
            query->suit_property = OMAHA_SUIT_PROPERTY_DS;
            len -= 2;
        } else if (strcmp(suffix, "ss") == 0) {
            query->suit_property = OMAHA_SUIT_PROPERTY_SS;
            len -= 2;
        } else if (len >= 1 && (pattern[len-1] == 'r' || pattern[len-1] == 'R')) {
            query->suit_property = OMAHA_SUIT_PROPERTY_RAINBOW;
            len -= 1;
        }
    }
    
    // Parse ranks
    if (len != 4) return 0; // Must have exactly 4 ranks
    
    for (int i = 0; i < 4; i++) {
        int rank = plo_rank_to_omaha_rank(pattern[i]);
        if (rank == -100) return 0;
        
        query->patterns[i].rank = rank;
        if (rank == WILDCARD_RANK) {
            query->num_rank_wildcards++;
        } else {
            query->patterns[i].suit = SUIT_DETERMINED_BY_PROPERTY;
            
            // Add to required ranks
            int found = 0;
            for (int j = 0; j < query->num_required_ranks; j++) {
                if (query->required_ranks[j] == rank) {
                    found = 1;
                    break;
                }
            }
            if (!found && query->num_required_ranks < 4) {
                query->required_ranks[query->num_required_ranks++] = rank;
            }
        }
    }
    
    return 1;
}

int PLO_GenerateHands(const char* pattern, StdDeck_CardMask deadCards, OmahaHandList* handList) {
    if (!pattern || !handList) return -1;
    
    OmahaHandQuery query;
    if (!PLO_PatternToOmahaQuery(pattern, &query)) {
        return -1;
    }
    
    return OmahaHand_Instantiate(&query, deadCards, handList);
}

int PLO_CreateRange(const char* pattern, StdDeck_CardMask deadCards, PlayerRange* range) {
    if (!pattern || !range) return 0;
    
    OmahaHandList handList;
    if (!OmahaHandList_Init(&handList, OMAHA_HANDLIST_INITIAL_CAPACITY)) {
        return 0;
    }
    
    int count = PLO_GenerateHands(pattern, deadCards, &handList);
    if (count <= 0) {
        OmahaHandList_Free(&handList);
        return 0;
    }
    
    // Transfer ownership of the hands array to the range
    range->hand_masks = handList.hands;
    range->weights = NULL;
    range->count = handList.count;
    range->total_weight = (double)handList.count;
    
    // Clear the handList without freeing the hands array
    handList.hands = NULL;
    handList.count = 0;
    handList.capacity = 0;
    
    return 1;
}

void PLO_FreeRange(PlayerRange* range) {
    if (range && range->hand_masks) {
        // We need to free the memory, but the field is const
        // This is a common pattern when the API needs to maintain const correctness
        // but the implementation needs to manage memory
        free((void*)(uintptr_t)range->hand_masks);
        range->hand_masks = NULL;
        range->count = 0;
        range->weights = NULL;
        range->total_weight = 0.0;
    }
}

int PLO_CalculateRangeEquity(
    const char* patterns[],
    int num_players,
    StdDeck_CardMask board,
    StdDeck_CardMask dead_cards,
    int nboard_cards_to_deal,
    bool use_montecarlo,
    int iterations,
    enum_result_t* results
) {
    if (!patterns || !results || num_players < 2) return -1;
    
    PlayerRange* ranges = (PlayerRange*)calloc(num_players, sizeof(PlayerRange));
    if (!ranges) return -1;
    
    // Create ranges for each player
    for (int i = 0; i < num_players; i++) {
        if (!PLO_CreateRange(patterns[i], dead_cards, &ranges[i])) {
            // Clean up previously created ranges
            for (int j = 0; j < i; j++) {
                PLO_FreeRange(&ranges[j]);
            }
            free(ranges);
            return -1;
        }
    }
    
    // Calculate equity
    int result = CalculateEquityForRanges(
        game_omaha,
        ranges,
        num_players,
        board,
        dead_cards,
        nboard_cards_to_deal,
        use_montecarlo,
        iterations,
        0, // orderflag
        results
    );
    
    // Clean up ranges
    for (int i = 0; i < num_players; i++) {
        PLO_FreeRange(&ranges[i]);
    }
    free(ranges);
    
    return result;
}

int PLO_GetCategoryHands(PLOHandCategory category, StdDeck_CardMask deadCards, OmahaHandList* handList) {
    if (!handList || category == PLO_CAT_INVALID) {
        return -1;
    }

    OmahaHandList_Clear(handList);

    PLOHand hand_details;
    StdDeck_CardMask combo_mask;

    for (int c1 = 0; c1 < StdDeck_N_CARDS; ++c1) {
        if (StdDeck_CardMask_CARD_IS_SET(deadCards, c1)) continue;
        for (int c2 = c1 + 1; c2 < StdDeck_N_CARDS; ++c2) {
            if (StdDeck_CardMask_CARD_IS_SET(deadCards, c2)) continue;
            for (int c3 = c2 + 1; c3 < StdDeck_N_CARDS; ++c3) {
                if (StdDeck_CardMask_CARD_IS_SET(deadCards, c3)) continue;
                for (int c4 = c3 + 1; c4 < StdDeck_N_CARDS; ++c4) {
                    if (StdDeck_CardMask_CARD_IS_SET(deadCards, c4)) continue;

                    StdDeck_CardMask_RESET(combo_mask);
                    StdDeck_CardMask_SET(combo_mask, c1);
                    StdDeck_CardMask_SET(combo_mask, c2);
                    StdDeck_CardMask_SET(combo_mask, c3);
                    StdDeck_CardMask_SET(combo_mask, c4);

                    PLOHandCategory hand_cat = PLO_CategorizeHand(combo_mask, &hand_details);
                    if (hand_cat == category) {
                        if (!OmahaHandList_AddHand(handList, combo_mask)) {
                            return -1;
                        }
                    }
                }
            }
        }
    }

    return handList->count;
}

int PLO_FilterByCategory(const OmahaHandList* inputList, PLOHandCategory category, OmahaHandList* outputList) {
    if (!inputList || !outputList || category == PLO_CAT_INVALID) return 0;
    
    OmahaHandList_Clear(outputList);
    
    for (int i = 0; i < inputList->count; i++) {
        PLOHand hand;
        hand.cards = inputList->hands[i];
        PLOHandCategory cat = PLO_CategorizeHand(hand.cards, &hand);
        
        if (cat == category) {
            if (!OmahaHandList_AddHand(outputList, hand.cards)) {
                return -1;
            }
        }
    }
    
    return outputList->count;
}

char* PLO_GetRangeDescription(const char* pattern, char* buffer, size_t bufsize) {
    if (!pattern || !buffer || bufsize == 0) return NULL;
    
    PLOHand hand;
    if (PLO_ParseHand(pattern, &hand)) {
        snprintf(buffer, bufsize, "%s (%s, %d combos)", 
                 pattern,
                 PLO_CategoryName(hand.category),
                 1); // Would need to calculate actual combos
    } else {
        // Try as a pattern
        OmahaHandQuery query;
        if (PLO_PatternToOmahaQuery(pattern, &query)) {
            OmahaHandList handList;
            if (OmahaHandList_Init(&handList, 100)) {
                StdDeck_CardMask dead;
                StdDeck_CardMask_RESET(dead);
                int count = OmahaHand_Instantiate(&query, dead, &handList);
                
                if (count > 0 && handList.count > 0) {
                    // Get category of first hand as representative
                    PLOHand firstHand;
                    firstHand.cards = handList.hands[0];
                    PLO_CategorizeHand(firstHand.cards, &firstHand);
                    
                    snprintf(buffer, bufsize, "%s (%s, %d combos)", 
                             pattern,
                             PLO_CategoryName(firstHand.category),
                             count);
                } else {
                    snprintf(buffer, bufsize, "%s (0 combos)", pattern);
                }
                
                OmahaHandList_Free(&handList);
            } else {
                strncpy(buffer, pattern, bufsize - 1);
                buffer[bufsize - 1] = '\0';
            }
        } else {
            strncpy(buffer, pattern, bufsize - 1);
            buffer[bufsize - 1] = '\0';
        }
    }
    
    return buffer;
}
