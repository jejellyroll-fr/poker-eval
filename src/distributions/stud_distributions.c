#include <poker_eval/distributions/stud_distributions.h>
#include <string.h>
#include <stdio.h>  // For TRACE_SHD via StudHandDistribution.h
#include <stdlib.h> // Not strictly needed for this parse function yet
#include <ctype.h>  // For isspace, tolower, isalpha, isalnum

// --- Static Helper Functions for Card Parsing ---

// Converts a rank character to its integer representation.
// Returns 0-12 (2-A), WILDCARD_CARD_VAL for 'x'/'X', or -100 for error.
static int stud_char_to_rank(char r) {
    r = (char)tolower(r);
    if (r >= '2' && r <= '9') return r - '2'; // 2-9 -> 0-7
    switch (r) {
        case 't': return 8;  // Ten
        case 'j': return 9;  // Jack
        case 'q': return 10; // Queen
        case 'k': return 11; // King
        case 'a': return 12; // Ace
        // 'x' is handled by the main parser, not by this function directly
        // as 'x' can appear alone. This function expects a rank char.
        default:  return -100; // Error
    }
}

// Converts a suit character to its integer representation.
// Returns 0-3 (c,d,h,s), or -100 for error.
static int stud_char_to_suit(char s) {
    s = (char)tolower(s);
    switch (s) {
        case 'c': return 0; // StdDeck_Suit_CLUBS
        case 'd': return 1; // StdDeck_Suit_DIAMONDS
        case 'h': return 2; // StdDeck_Suit_HEARTS
        case 's': return 3; // StdDeck_Suit_SPADES
        default:  return -100; // Error
    }
}

// --- StudHand_Parse Function ---

int StudHand_Parse(const char* handText, int game_cards, StudHandQuery* query) {
    if (!handText || !query) {
        TRACE_SHD("StudHand_Parse: Null handText or query pointer.\n");
        return 0;
    }
    if (game_cards <= 0 || game_cards > MAX_STUD_CARDS) {
        TRACE_SHD("StudHand_Parse: Invalid game_cards value %d. Must be 1-%d.\n", game_cards, MAX_STUD_CARDS);
        return 0;
    }

    // Initialize query structure
    memset(query, 0, sizeof(StudHandQuery)); // Zero out all members
    query->game_total_cards = game_cards;
    for (int i = 0; i < MAX_STUD_CARDS; ++i) {
        query->patterns[i].rank = WILDCARD_CARD_VAL; // Default to wildcard
        query->patterns[i].suit = WILDCARD_CARD_VAL; // Default to wildcard
        query->patterns[i].is_up_card = true;       // Default, will be set explicitly
    }

    const char* p = handText;
    bool in_hole = false;
    int pattern_idx = 0; // Index for query->patterns array

    while (*p != '\0') {
        // Skip whitespace
        while (*p != '\0' && isspace((unsigned char)*p)) {
            p++;
        }
        if (*p == '\0') break;

        if (*p == '(') {
            if (in_hole) {
                TRACE_SHD("StudHand_Parse: Nested parentheses not allowed: %s\n", handText);
                return 0;
            }
            in_hole = true;
            p++;
            continue;
        } else if (*p == ')') {
            if (!in_hole) {
                TRACE_SHD("StudHand_Parse: Unmatched closing parenthesis: %s\n", handText);
                return 0;
            }
            in_hole = false;
            p++;
            continue;
        }

        if (pattern_idx >= query->game_total_cards) { // Changed from MAX_STUD_CARDS to game_total_cards
            TRACE_SHD("StudHand_Parse: Too many card patterns for game_cards = %d. Pattern string: %s\n", query->game_total_cards, handText);
            return 0;
        }
        
        // Prepare to parse a card pattern
        query->patterns[pattern_idx].is_up_card = !in_hole;

        if (tolower(*p) == 'x') { // Wildcard card 'x' or 'X'
            query->patterns[pattern_idx].rank = WILDCARD_CARD_VAL;
            query->patterns[pattern_idx].suit = WILDCARD_CARD_VAL;
            query->num_wildcards_in_pattern++;
            p++; // Consume 'x'
        } else { // Specific card (e.g., "As", "Td") or rank-only (e.g., "A")
            char rank_char = *p;
            int rank_val = stud_char_to_rank(rank_char);
            if (rank_val == -100) {
                TRACE_SHD("StudHand_Parse: Invalid rank character '%c' in hand string: %s\n", rank_char, handText);
                return 0;
            }
            // A following suit char makes this a specific card; otherwise it is
            // a rank-only card (suit wildcard), e.g. "AA" inside "(AA)xxxxx".
            char next_char = *(p + 1);
            int suit_val = stud_char_to_suit(next_char);
            if (suit_val != -100) {
                p += 2; // Consume rank + suit
                query->patterns[pattern_idx].rank = rank_val;
                query->patterns[pattern_idx].suit = suit_val;
                query->num_known_cards++;
            } else {
                p += 1; // Consume only the rank
                query->patterns[pattern_idx].rank = rank_val;
                query->patterns[pattern_idx].suit = WILDCARD_CARD_VAL;
                query->num_known_cards++;
            }
        }

        if (query->patterns[pattern_idx].is_up_card) {
            query->num_up_cards_specified++;
        } else {
            query->num_down_cards_specified++;
        }
        query->num_pattern_cards++;
        pattern_idx++;
    } // End while (*p != '\0')

    // Post-loop validation
    if (in_hole) {
        TRACE_SHD("StudHand_Parse: Unclosed parenthesis at end of hand string: %s\n", handText);
        return 0;
    }

    if (query->num_pattern_cards == 0 && strlen(handText) > 0) {
        // This could happen if handText only contains whitespace or empty parentheses.
        TRACE_SHD("StudHand_Parse: No valid card patterns found in non-empty string: %s\n", handText);
        return 0;
    }
    
    // If handText is empty, num_pattern_cards will be 0. This is fine, means all cards are wild upcards.
    // The instantiator will handle filling up to game_total_cards.

    // Validate that the sum of known cards and wildcards in the pattern matches the number of patterns parsed.
    if (query->num_known_cards + query->num_wildcards_in_pattern != query->num_pattern_cards) {
        TRACE_SHD("StudHand_Parse: Internal count mismatch. Known: %d, Wild: %d, TotalParsed: %d\n",
                  query->num_known_cards, query->num_wildcards_in_pattern, query->num_pattern_cards);
        return 0; // Should not happen if logic is correct
    }

    // The parser only processes what's in the string. It does not fill up to game_total_cards.
    // The instantiator will use query.num_pattern_cards and query.game_total_cards.

    TRACE_SHD("StudHand_Parse: Success. Parsed %d patterns. Known: %d, WildInPattern: %d. Down: %d, Up: %d. GameCards: %d\n",
              query->num_pattern_cards, query->num_known_cards, query->num_wildcards_in_pattern,
              query->num_down_cards_specified, query->num_up_cards_specified, query->game_total_cards);

    return 1; // Success
}

// --- Instantiation Logic ---

// Recursive helper to fill stud hand slots. Each pattern slot is one of:
//   - exact card (rank and suit known)
//   - rank-only card (rank known, suit wildcard)
//   - fully wild card (rank and suit wildcard)
// `used` tracks cards already placed (including dead cards) so hands have no duplicates.
static void stud_fill_slots(
    int slot,
    StdDeck_CardMask used,
    StdDeck_CardMask acc,
    const StudHandQuery* query,
    StudHandList* handList
) {
    if (handList->count >= MAX_STUD_COMBOS) {
        return;
    }
    if (slot == query->num_pattern_cards) {
        handList->hands[handList->count++] = acc;
        return;
    }

    const StudCardPattern* pat = &query->patterns[slot];

    if (pat->rank != WILDCARD_CARD_VAL && pat->suit != WILDCARD_CARD_VAL) {
        // Exact card
        StdDeck_CardMask c = StdDeck_MASK(StdDeck_MAKE_CARD(pat->rank, pat->suit));
        if (StdDeck_CardMask_ANY_SET(used, c)) {
            return;
        }
        StdDeck_CardMask nused, nacc;
        StdDeck_CardMask_OR(nused, used, c);
        StdDeck_CardMask_OR(nacc, acc, c);
        stud_fill_slots(slot + 1, nused, nacc, query, handList);
    } else if (pat->rank != WILDCARD_CARD_VAL && pat->suit == WILDCARD_CARD_VAL) {
        // Rank-only card: try every suit
        for (int s = 0; s < 4; s++) {
            StdDeck_CardMask c = StdDeck_MASK(StdDeck_MAKE_CARD(pat->rank, s));
            if (StdDeck_CardMask_ANY_SET(used, c)) {
                continue;
            }
            StdDeck_CardMask nused, nacc;
            StdDeck_CardMask_OR(nused, used, c);
            StdDeck_CardMask_OR(nacc, acc, c);
            stud_fill_slots(slot + 1, nused, nacc, query, handList);
            if (handList->count >= MAX_STUD_COMBOS) {
                return;
            }
        }
    } else {
        // Fully wild card: try every remaining deck card
        for (int card = 0; card < StdDeck_N_CARDS; card++) {
            StdDeck_CardMask c = StdDeck_MASK(card);
            if (StdDeck_CardMask_ANY_SET(used, c)) {
                continue;
            }
            StdDeck_CardMask nused, nacc;
            StdDeck_CardMask_OR(nused, used, c);
            StdDeck_CardMask_OR(nacc, acc, c);
            stud_fill_slots(slot + 1, nused, nacc, query, handList);
            if (handList->count >= MAX_STUD_COMBOS) {
                return;
            }
        }
    }
}

int StudHand_Instantiate(const StudHandQuery* query, StdDeck_CardMask deadCards, StudHandList* handList) {
    if (!query || !handList) {
        TRACE_SHD("StudHand_Instantiate: Null query or handList pointer.\n");
        return -1; // Indicate error
    }

    handList->count = 0;

    StdDeck_CardMask used, acc;
    StdDeck_CardMask_RESET(used);
    StdDeck_CardMask_RESET(acc);
    StdDeck_CardMask_OR(used, used, deadCards);

    stud_fill_slots(0, used, acc, query, handList);

    return handList->count;
}
