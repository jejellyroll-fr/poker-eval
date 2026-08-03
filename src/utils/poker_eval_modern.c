/*
 * Implementation of modern C API for poker-eval library
 * 
 * Copyright (C) 2024 poker-eval contributors
 * Licensed under GPL v3 or later
 */

#include <poker_eval/core/poker_eval_modern.h>
#include <poker_eval/poker_eval.h>
#include <poker_eval/core/poker_defs.h>

/* Include deck definitions */
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/deck/deck_short.h>
#include <poker_eval/deck/deck_joker.h>
#include <poker_eval/deck/deck_astud.h>

/* Include rules definitions */
#include <poker_eval/games/rules_std.h>
#include <poker_eval/games/rules_short.h>
#include <poker_eval/games/rules_joker.h>
#include <poker_eval/games/rules_astud.h>

/* Include evaluation functions */
#include <poker_eval/core/eval.h>
#include <poker_eval/games/eval_short.h>
#include <poker_eval/games/eval_joker.h>
#include <poker_eval/games/eval_astud.h>

/* Include other functionality */
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/eval_cache.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <assert.h>

#include "poker_eval_modern_internal.h"

/* ========================================================================
 * ERROR HANDLING
 * ======================================================================== */

const char* poker_eval_error_string(poker_eval_error_t error) {
    switch (error) {
        case POKER_EVAL_SUCCESS:
            return "Success";
        case POKER_EVAL_ERROR_NULL_POINTER:
            return "Null pointer argument";
        case POKER_EVAL_ERROR_INVALID_ARGUMENT:
            return "Invalid argument";
        case POKER_EVAL_ERROR_INSUFFICIENT_CARDS:
            return "Insufficient cards";
        case POKER_EVAL_ERROR_TOO_MANY_CARDS:
            return "Too many cards";
        case POKER_EVAL_ERROR_DUPLICATE_CARD:
            return "Duplicate card";
        case POKER_EVAL_ERROR_INVALID_CARD:
            return "Invalid card";
        case POKER_EVAL_ERROR_MEMORY_ALLOCATION:
            return "Memory allocation failed";
        case POKER_EVAL_ERROR_NOT_INITIALIZED:
            return "Object not initialized";
        case POKER_EVAL_ERROR_UNSUPPORTED_OPERATION:
            return "Unsupported operation";
        default:
            return "Unknown error";
    }
}

/* ========================================================================
 * UTILITY FUNCTIONS
 * ======================================================================== */

static poker_hand_type_t handval_to_hand_type(uint32_t handval) {
    uint32_t type = HandVal_HANDTYPE(handval);
    
    switch (type) {
        case StdRules_HandType_NOPAIR:
            return POKER_HAND_HIGH_CARD;
        case StdRules_HandType_ONEPAIR:
            return POKER_HAND_PAIR;
        case StdRules_HandType_TWOPAIR:
            return POKER_HAND_TWO_PAIR;
        case StdRules_HandType_TRIPS:
            return POKER_HAND_THREE_OF_A_KIND;
        case StdRules_HandType_STRAIGHT:
            return POKER_HAND_STRAIGHT;
        case StdRules_HandType_FLUSH:
            return POKER_HAND_FLUSH;
        case StdRules_HandType_FULLHOUSE:
            return POKER_HAND_FULL_HOUSE;
        case StdRules_HandType_QUADS:
            return POKER_HAND_FOUR_OF_A_KIND;
        case StdRules_HandType_STFLUSH:
            return POKER_HAND_STRAIGHT_FLUSH;
        default:
            return POKER_HAND_HIGH_CARD;
    }
}

static int card_to_index(poker_rank_t rank, poker_suit_t suit) {
    return (int)rank + (int)suit * 13;
}

static void index_to_card(int index, poker_rank_t* rank, poker_suit_t* suit) {
    *rank = (poker_rank_t)(index % 13);
    *suit = (poker_suit_t)(index / 13);
}

/* ========================================================================
 * CONTEXT MANAGEMENT
 * ======================================================================== */

poker_eval_error_t poker_eval_context_create(poker_deck_type_t deck_type, 
                                            poker_eval_context_t** context) {
    if (!context) {
        return POKER_EVAL_ERROR_NULL_POINTER;
    }
    
    poker_eval_context_t* ctx = malloc(sizeof(poker_eval_context_t));
    if (!ctx) {
        return POKER_EVAL_ERROR_MEMORY_ALLOCATION;
    }
    
    ctx->deck_type = deck_type;
    ctx->caching_enabled = false;
    ctx->cache = NULL;
    
    *context = ctx;
    return POKER_EVAL_SUCCESS;
}

void poker_eval_context_destroy(poker_eval_context_t* context) {
    if (context) {
        if (context->cache) {
            eval_cache_destroy(context->cache);
        }
        free(context);
    }
}

poker_eval_error_t poker_eval_context_set_caching(poker_eval_context_t* context, 
                                                 bool enable) {
    if (!context) {
        return POKER_EVAL_ERROR_NULL_POINTER;
    }
    
    if (enable && !context->cache) {
        context->cache = eval_cache_create(1024 * 1024); // 1MB cache
        if (!context->cache) {
            return POKER_EVAL_ERROR_MEMORY_ALLOCATION;
        }
    } else if (!enable && context->cache) {
        eval_cache_destroy(context->cache);
        context->cache = NULL;
    }
    
    context->caching_enabled = enable;
    return POKER_EVAL_SUCCESS;
}

/* ========================================================================
 * HAND MANAGEMENT
 * ======================================================================== */

poker_eval_error_t poker_eval_hand_create(poker_eval_hand_t** hand) {
    if (!hand) {
        return POKER_EVAL_ERROR_NULL_POINTER;
    }
    
    poker_eval_hand_t* h = malloc(sizeof(poker_eval_hand_t));
    if (!h) {
        return POKER_EVAL_ERROR_MEMORY_ALLOCATION;
    }
    
    h->deck_type = POKER_DECK_STANDARD;
    StdDeck_CardMask_RESET(h->cards.std_mask);
    h->card_count = 0;
    
    *hand = h;
    return POKER_EVAL_SUCCESS;
}

void poker_eval_hand_destroy(poker_eval_hand_t* hand) {
    if (hand) {
        free(hand);
    }
}

poker_eval_error_t poker_eval_hand_clear(poker_eval_hand_t* hand) {
    if (!hand) {
        return POKER_EVAL_ERROR_NULL_POINTER;
    }
    
    switch (hand->deck_type) {
        case POKER_DECK_STANDARD:
            StdDeck_CardMask_RESET(hand->cards.std_mask);
            break;
        case POKER_DECK_SHORT:
            ShortDeck_CardMask_RESET(hand->cards.short_mask);
            break;
        case POKER_DECK_JOKER:
            JokerDeck_CardMask_RESET(hand->cards.joker_mask);
            break;
        case POKER_DECK_ASTUD:
            AStudDeck_CardMask_RESET(hand->cards.astud_mask);
            break;
        default:
            return POKER_EVAL_ERROR_INVALID_ARGUMENT;
    }
    
    hand->card_count = 0;
    return POKER_EVAL_SUCCESS;
}

poker_eval_error_t poker_eval_hand_add_card(poker_eval_hand_t* hand, 
                                           poker_rank_t rank, 
                                           poker_suit_t suit) {
    if (!hand) {
        return POKER_EVAL_ERROR_NULL_POINTER;
    }
    
    if (rank < POKER_RANK_TWO || rank > POKER_RANK_ACE ||
        suit < POKER_SUIT_CLUBS || suit > POKER_SUIT_SPADES) {
        return POKER_EVAL_ERROR_INVALID_CARD;
    }
    
    int card_index = card_to_index(rank, suit);
    
    switch (hand->deck_type) {
        case POKER_DECK_STANDARD: {
            if (hand->card_count >= 7) {
                return POKER_EVAL_ERROR_TOO_MANY_CARDS;
            }
            if (StdDeck_CardMask_CARD_IS_SET(hand->cards.std_mask, card_index)) {
                return POKER_EVAL_ERROR_DUPLICATE_CARD;
            }
            StdDeck_CardMask_SET(hand->cards.std_mask, card_index);
            break;
        }
        case POKER_DECK_SHORT: {
            if (rank < POKER_RANK_SIX) {
                return POKER_EVAL_ERROR_INVALID_CARD;
            }
            if (hand->card_count >= 7) {
                return POKER_EVAL_ERROR_TOO_MANY_CARDS;
            }
            // Convert to short deck index
            int short_index = (rank - POKER_RANK_SIX) + suit * 9;
            if (ShortDeck_CardMask_CARD_IS_SET(hand->cards.short_mask, short_index)) {
                return POKER_EVAL_ERROR_DUPLICATE_CARD;
            }
            ShortDeck_CardMask_SET(hand->cards.short_mask, short_index);
            break;
        }
        case POKER_DECK_JOKER: {
            if (hand->card_count >= 7) {
                return POKER_EVAL_ERROR_TOO_MANY_CARDS;
            }
            if (JokerDeck_CardMask_CARD_IS_SET(hand->cards.joker_mask, card_index)) {
                return POKER_EVAL_ERROR_DUPLICATE_CARD;
            }
            JokerDeck_CardMask_SET(hand->cards.joker_mask, card_index);
            break;
        }
        case POKER_DECK_ASTUD: {
            if (hand->card_count >= 7) {
                return POKER_EVAL_ERROR_TOO_MANY_CARDS;
            }
            if (AStudDeck_CardMask_CARD_IS_SET(hand->cards.astud_mask, card_index)) {
                return POKER_EVAL_ERROR_DUPLICATE_CARD;
            }
            AStudDeck_CardMask_SET(hand->cards.astud_mask, card_index);
            break;
        }
        default:
            return POKER_EVAL_ERROR_INVALID_ARGUMENT;
    }
    
    hand->card_count++;
    return POKER_EVAL_SUCCESS;
}

poker_eval_error_t poker_eval_hand_add_card_string(poker_eval_hand_t* hand, 
                                                  const char* card_str) {
    if (!hand || !card_str) {
        return POKER_EVAL_ERROR_NULL_POINTER;
    }
    
    poker_rank_t rank;
    poker_suit_t suit;
    
    poker_eval_error_t err = poker_eval_parse_card(card_str, &rank, &suit);
    if (err != POKER_EVAL_SUCCESS) {
        return err;
    }
    
    return poker_eval_hand_add_card(hand, rank, suit);
}

poker_eval_error_t poker_eval_hand_remove_card(poker_eval_hand_t* hand, 
                                              poker_rank_t rank, 
                                              poker_suit_t suit) {
    if (!hand) {
        return POKER_EVAL_ERROR_NULL_POINTER;
    }
    
    if (rank < POKER_RANK_TWO || rank > POKER_RANK_ACE ||
        suit < POKER_SUIT_CLUBS || suit > POKER_SUIT_SPADES) {
        return POKER_EVAL_ERROR_INVALID_CARD;
    }
    
    int card_index = card_to_index(rank, suit);
    
    switch (hand->deck_type) {
        case POKER_DECK_STANDARD: {
            if (!StdDeck_CardMask_CARD_IS_SET(hand->cards.std_mask, card_index)) {
                return POKER_EVAL_ERROR_INVALID_CARD;
            }
            StdDeck_CardMask_UNSET(hand->cards.std_mask, card_index);
            break;
        }
        case POKER_DECK_SHORT: {
            if (rank < POKER_RANK_SIX) {
                return POKER_EVAL_ERROR_INVALID_CARD;
            }
            int short_index = (rank - POKER_RANK_SIX) + suit * 9;
            if (!ShortDeck_CardMask_CARD_IS_SET(hand->cards.short_mask, short_index)) {
                return POKER_EVAL_ERROR_INVALID_CARD;
            }
            ShortDeck_CardMask_UNSET(hand->cards.short_mask, short_index);
            break;
        }
        case POKER_DECK_JOKER: {
            if (!JokerDeck_CardMask_CARD_IS_SET(hand->cards.joker_mask, card_index)) {
                return POKER_EVAL_ERROR_INVALID_CARD;
            }
            JokerDeck_CardMask_UNSET(hand->cards.joker_mask, card_index);
            break;
        }
        case POKER_DECK_ASTUD: {
            if (!AStudDeck_CardMask_CARD_IS_SET(hand->cards.astud_mask, card_index)) {
                return POKER_EVAL_ERROR_INVALID_CARD;
            }
            AStudDeck_CardMask_UNSET(hand->cards.astud_mask, card_index);
            break;
        }
        default:
            return POKER_EVAL_ERROR_INVALID_ARGUMENT;
    }
    
    hand->card_count--;
    return POKER_EVAL_SUCCESS;
}

poker_eval_error_t poker_eval_hand_get_count(const poker_eval_hand_t* hand, 
                                            size_t* count) {
    if (!hand || !count) {
        return POKER_EVAL_ERROR_NULL_POINTER;
    }
    
    *count = hand->card_count;
    return POKER_EVAL_SUCCESS;
}

poker_eval_error_t poker_eval_hand_contains_card(const poker_eval_hand_t* hand, 
                                                poker_rank_t rank, 
                                                poker_suit_t suit, 
                                                bool* contains) {
    if (!hand || !contains) {
        return POKER_EVAL_ERROR_NULL_POINTER;
    }
    
    if (rank < POKER_RANK_TWO || rank > POKER_RANK_ACE ||
        suit < POKER_SUIT_CLUBS || suit > POKER_SUIT_SPADES) {
        return POKER_EVAL_ERROR_INVALID_CARD;
    }
    
    int card_index = card_to_index(rank, suit);
    
    switch (hand->deck_type) {
        case POKER_DECK_STANDARD:
            *contains = StdDeck_CardMask_CARD_IS_SET(hand->cards.std_mask, card_index);
            break;
        case POKER_DECK_SHORT: {
            if (rank < POKER_RANK_SIX) {
                *contains = false;
            } else {
                int short_index = (rank - POKER_RANK_SIX) + suit * 9;
                *contains = ShortDeck_CardMask_CARD_IS_SET(hand->cards.short_mask, short_index);
            }
            break;
        }
        case POKER_DECK_JOKER:
            *contains = JokerDeck_CardMask_CARD_IS_SET(hand->cards.joker_mask, card_index);
            break;
        case POKER_DECK_ASTUD:
            *contains = AStudDeck_CardMask_CARD_IS_SET(hand->cards.astud_mask, card_index);
            break;
        default:
            return POKER_EVAL_ERROR_INVALID_ARGUMENT;
    }
    
    return POKER_EVAL_SUCCESS;
}

poker_eval_error_t poker_eval_hand_copy(poker_eval_hand_t* dest, 
                                       const poker_eval_hand_t* src) {
    if (!dest || !src) {
        return POKER_EVAL_ERROR_NULL_POINTER;
    }
    
    dest->deck_type = src->deck_type;
    dest->card_count = src->card_count;
    
    switch (src->deck_type) {
        case POKER_DECK_STANDARD:
            dest->cards.std_mask = src->cards.std_mask;
            break;
        case POKER_DECK_SHORT:
            dest->cards.short_mask = src->cards.short_mask;
            break;
        case POKER_DECK_JOKER:
            dest->cards.joker_mask = src->cards.joker_mask;
            break;
        case POKER_DECK_ASTUD:
            dest->cards.astud_mask = src->cards.astud_mask;
            break;
        default:
            return POKER_EVAL_ERROR_INVALID_ARGUMENT;
    }
    
    return POKER_EVAL_SUCCESS;
}

/* ========================================================================
 * HAND EVALUATION
 * ======================================================================== */

poker_eval_error_t poker_eval_result_create(poker_eval_result_t** result) {
    if (!result) {
        return POKER_EVAL_ERROR_NULL_POINTER;
    }
    
    poker_eval_result_t* r = malloc(sizeof(poker_eval_result_t));
    if (!r) {
        return POKER_EVAL_ERROR_MEMORY_ALLOCATION;
    }
    
    r->deck_type = POKER_DECK_STANDARD;
    r->raw_value = 0;
    r->hand_type = POKER_HAND_HIGH_CARD;
    r->is_valid = false;
    
    *result = r;
    return POKER_EVAL_SUCCESS;
}

void poker_eval_result_destroy(poker_eval_result_t* result) {
    if (result) {
        free(result);
    }
}

poker_eval_error_t poker_eval_hand_evaluate(poker_eval_context_t* context, 
                                           const poker_eval_hand_t* hand, 
                                           poker_eval_result_t* result) {
    if (!context || !hand || !result) {
        return POKER_EVAL_ERROR_NULL_POINTER;
    }
    
    if (hand->card_count < 1) {
        return POKER_EVAL_ERROR_INSUFFICIENT_CARDS;
    }
    
    uint32_t handval = 0;
    
    switch (hand->deck_type) {
        case POKER_DECK_STANDARD: {
            if (context->caching_enabled && context->cache) {
                handval = StdDeck_StdRules_EVAL_N_CachedEx(context->cache, 
                                                          hand->cards.std_mask, 
                                                          (int)hand->card_count);
            } else {
                handval = StdDeck_StdRules_EVAL_N(hand->cards.std_mask, (int)hand->card_count);
            }
            break;
        }
        case POKER_DECK_SHORT:
            handval = ShortDeck_ShortRules_EVAL_N(hand->cards.short_mask, (int)hand->card_count);
            break;
        case POKER_DECK_JOKER:
            handval = JokerDeck_JokerRules_EVAL_N(hand->cards.joker_mask, (int)hand->card_count);
            break;
        case POKER_DECK_ASTUD:
            handval = AStudDeck_AStudRules_EVAL_N(hand->cards.astud_mask, (int)hand->card_count);
            break;
        default:
            return POKER_EVAL_ERROR_INVALID_ARGUMENT;
    }
    
    result->deck_type = hand->deck_type;
    result->raw_value = handval;
    result->hand_type = handval_to_hand_type(handval);
    result->is_valid = true;
    
    return POKER_EVAL_SUCCESS;
}

poker_eval_error_t poker_eval_result_compare(const poker_eval_result_t* result1, 
                                            const poker_eval_result_t* result2, 
                                            int* comparison) {
    if (!result1 || !result2 || !comparison) {
        return POKER_EVAL_ERROR_NULL_POINTER;
    }
    
    if (!result1->is_valid || !result2->is_valid) {
        return POKER_EVAL_ERROR_NOT_INITIALIZED;
    }
    
    if (result1->deck_type != result2->deck_type) {
        return POKER_EVAL_ERROR_INVALID_ARGUMENT;
    }
    
    if (result1->raw_value > result2->raw_value) {
        *comparison = 1;
    } else if (result1->raw_value < result2->raw_value) {
        *comparison = -1;
    } else {
        *comparison = 0;
    }
    
    return POKER_EVAL_SUCCESS;
}

poker_eval_error_t poker_eval_result_get_hand_type(const poker_eval_result_t* result, 
                                                  poker_hand_type_t* hand_type) {
    if (!result || !hand_type) {
        return POKER_EVAL_ERROR_NULL_POINTER;
    }
    
    if (!result->is_valid) {
        return POKER_EVAL_ERROR_NOT_INITIALIZED;
    }
    
    *hand_type = result->hand_type;
    return POKER_EVAL_SUCCESS;
}

poker_eval_error_t poker_eval_result_get_value(const poker_eval_result_t* result, 
                                              uint32_t* value) {
    if (!result || !value) {
        return POKER_EVAL_ERROR_NULL_POINTER;
    }
    
    if (!result->is_valid) {
        return POKER_EVAL_ERROR_NOT_INITIALIZED;
    }
    
    *value = result->raw_value;
    return POKER_EVAL_SUCCESS;
}

poker_eval_error_t poker_eval_result_get_description(const poker_eval_result_t* result, 
                                                    char* buffer, 
                                                    size_t buffer_size) {
    if (!result || !buffer) {
        return POKER_EVAL_ERROR_NULL_POINTER;
    }
    
    if (!result->is_valid) {
        return POKER_EVAL_ERROR_NOT_INITIALIZED;
    }
    
    const char* type_str = poker_eval_hand_type_string(result->hand_type);
    
    if (strnlen(type_str, buffer_size) >= buffer_size) {
        return POKER_EVAL_ERROR_INVALID_ARGUMENT;
    }

    snprintf(buffer, buffer_size, "%s", type_str);
    return POKER_EVAL_SUCCESS;
}

/* ========================================================================
 * UTILITY FUNCTIONS
 * ======================================================================== */

const char* poker_eval_hand_type_string(poker_hand_type_t hand_type) {
    switch (hand_type) {
        case POKER_HAND_HIGH_CARD:
            return "High Card";
        case POKER_HAND_PAIR:
            return "Pair";
        case POKER_HAND_TWO_PAIR:
            return "Two Pair";
        case POKER_HAND_THREE_OF_A_KIND:
            return "Three of a Kind";
        case POKER_HAND_STRAIGHT:
            return "Straight";
        case POKER_HAND_FLUSH:
            return "Flush";
        case POKER_HAND_FULL_HOUSE:
            return "Full House";
        case POKER_HAND_FOUR_OF_A_KIND:
            return "Four of a Kind";
        case POKER_HAND_STRAIGHT_FLUSH:
            return "Straight Flush";
        case POKER_HAND_ROYAL_FLUSH:
            return "Royal Flush";
        default:
            return "Unknown";
    }
}

const char* poker_eval_rank_string(poker_rank_t rank) {
    switch (rank) {
        case POKER_RANK_TWO:   return "2";
        case POKER_RANK_THREE: return "3";
        case POKER_RANK_FOUR:  return "4";
        case POKER_RANK_FIVE:  return "5";
        case POKER_RANK_SIX:   return "6";
        case POKER_RANK_SEVEN: return "7";
        case POKER_RANK_EIGHT: return "8";
        case POKER_RANK_NINE:  return "9";
        case POKER_RANK_TEN:   return "T";
        case POKER_RANK_JACK:  return "J";
        case POKER_RANK_QUEEN: return "Q";
        case POKER_RANK_KING:  return "K";
        case POKER_RANK_ACE:   return "A";
        default:               return "?";
    }
}

const char* poker_eval_suit_string(poker_suit_t suit) {
    switch (suit) {
        case POKER_SUIT_CLUBS:    return "c";
        case POKER_SUIT_DIAMONDS: return "d";
        case POKER_SUIT_HEARTS:   return "h";
        case POKER_SUIT_SPADES:   return "s";
        default:                  return "?";
    }
}

poker_eval_error_t poker_eval_parse_card(const char* card_str, 
                                        poker_rank_t* rank, 
                                        poker_suit_t* suit) {
    if (!card_str || !rank || !suit) {
        return POKER_EVAL_ERROR_NULL_POINTER;
    }
    
    if (strnlen(card_str, 3) != 2) {
        return POKER_EVAL_ERROR_INVALID_CARD;
    }
    
    int rank_char = toupper(card_str[0]);
    int suit_char = tolower(card_str[1]);
    
    // Parse rank
    switch (rank_char) {
        case '2': *rank = POKER_RANK_TWO; break;
        case '3': *rank = POKER_RANK_THREE; break;
        case '4': *rank = POKER_RANK_FOUR; break;
        case '5': *rank = POKER_RANK_FIVE; break;
        case '6': *rank = POKER_RANK_SIX; break;
        case '7': *rank = POKER_RANK_SEVEN; break;
        case '8': *rank = POKER_RANK_EIGHT; break;
        case '9': *rank = POKER_RANK_NINE; break;
        case 'T': *rank = POKER_RANK_TEN; break;
        case 'J': *rank = POKER_RANK_JACK; break;
        case 'Q': *rank = POKER_RANK_QUEEN; break;
        case 'K': *rank = POKER_RANK_KING; break;
        case 'A': *rank = POKER_RANK_ACE; break;
        default:
            return POKER_EVAL_ERROR_INVALID_CARD;
    }
    
    // Parse suit
    switch (suit_char) {
        case 'c': *suit = POKER_SUIT_CLUBS; break;
        case 'd': *suit = POKER_SUIT_DIAMONDS; break;
        case 'h': *suit = POKER_SUIT_HEARTS; break;
        case 's': *suit = POKER_SUIT_SPADES; break;
        default:
            return POKER_EVAL_ERROR_INVALID_CARD;
    }
    
    return POKER_EVAL_SUCCESS;
}

poker_eval_error_t poker_eval_get_version(int* major, int* minor, int* patch) {
    if (!major || !minor || !patch) {
        return POKER_EVAL_ERROR_NULL_POINTER;
    }
    
    *major = POKER_EVAL_API_VERSION_MAJOR;
    *minor = POKER_EVAL_API_VERSION_MINOR;
    *patch = POKER_EVAL_API_VERSION_PATCH;
    
    return POKER_EVAL_SUCCESS;
}

/* ========================================================================
 * EQUITY CALCULATION (experimental)
 * ======================================================================== */

