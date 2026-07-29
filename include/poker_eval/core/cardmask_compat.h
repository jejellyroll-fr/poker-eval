/*
 * cardmask_compat.h - Compatibility layer for CardMask to mask_t migration
 *
 * This header provides compatibility macros and functions to gradually migrate
 * from the legacy CardMask system to the modern unified mask_t system.
 *
 * Migration strategy:
 * 1. Include this header in legacy files
 * 2. Gradually replace CardMask usage with mask_t
 * 3. Remove compatibility layer once migration is complete
 */

#ifndef CARDMASK_COMPAT_H
#define CARDMASK_COMPAT_H

#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/deck/deck_std.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Migration control - define to enable compatibility layer */
#ifndef CARDMASK_COMPAT_ENABLED
#define CARDMASK_COMPAT_ENABLED 1
#endif

#if CARDMASK_COMPAT_ENABLED

    /*
     * Conversion functions between legacy CardMask and modern mask_t
     */

    /**
     * Convert legacy CardMask to modern mask_t
     * @param cardmask Legacy CardMask structure
     * @return Equivalent mask_t representation
     *
     * Card indexing in mask_t: bit_position = rank + suit * 13
     * where rank=0..12 (2..A), suit=0..3 (clubs, diamonds, hearts, spades)
     */
    static inline mask_t cardmask_to_mask_t(const StdDeck_CardMask cardmask)
    {
        mask_t result = MASK_EMPTY;

        /* Convert each suit from CardMask format to mask_t */
        unsigned short spades = StdDeck_CardMask_SPADES(cardmask);
        unsigned short hearts = StdDeck_CardMask_HEARTS(cardmask);
        unsigned short diamonds = StdDeck_CardMask_DIAMONDS(cardmask);
        unsigned short clubs = StdDeck_CardMask_CLUBS(cardmask);

        /* Map each rank in each suit to mask_t bit position */
        /* mask_t indexing: bit_pos = rank + suit * 13 */
        for (int rank = 0; rank < 13; rank++)
        {
            if (clubs & (1 << rank))
            {
                int bit_pos = rank + MODERN_SUIT_CLUBS * 13;
                result |= ((mask_t)1) << bit_pos;
            }
            if (diamonds & (1 << rank))
            {
                int bit_pos = rank + MODERN_SUIT_DIAMONDS * 13;
                result |= ((mask_t)1) << bit_pos;
            }
            if (hearts & (1 << rank))
            {
                int bit_pos = rank + MODERN_SUIT_HEARTS * 13;
                result |= ((mask_t)1) << bit_pos;
            }
            if (spades & (1 << rank))
            {
                int bit_pos = rank + MODERN_SUIT_SPADES * 13;
                result |= ((mask_t)1) << bit_pos;
            }
        }

        return result;
    }

    /**
     * Convert modern mask_t to legacy CardMask
     * @param mask Modern mask_t
     * @return Equivalent CardMask structure
     *
     * Card indexing in mask_t: bit_position = rank + suit * 13
     * where rank=0..12 (2..A), suit=0..3 (clubs, diamonds, hearts, spades)
     */
    static inline StdDeck_CardMask mask_t_to_cardmask(mask_t mask)
    {
        StdDeck_CardMask result;
        StdDeck_CardMask_RESET(result);

        /* Convert mask_t bits to CardMask structure */
        /* mask_t indexing: bit_pos = rank + suit * 13 */
        for (int suit = 0; suit < 4; suit++)
        {
            for (int rank = 0; rank < 13; rank++)
            {
                int bit_pos = rank + suit * 13;
                if (mask_is_set(mask, bit_pos))
                {
                    int std_suit = StdDeck_Suit_HEARTS;
                    switch (suit)
                    {
                    case MODERN_SUIT_CLUBS:
                        std_suit = StdDeck_Suit_CLUBS;
                        break;
                    case MODERN_SUIT_DIAMONDS:
                        std_suit = StdDeck_Suit_DIAMONDS;
                        break;
                    case MODERN_SUIT_HEARTS:
                        std_suit = StdDeck_Suit_HEARTS;
                        break;
                    case MODERN_SUIT_SPADES:
                        std_suit = StdDeck_Suit_SPADES;
                        break;
                    default:
                        /* Invalid suit - should not happen */
                        continue;
                    }

                    StdDeck_CardMask_SET(result, StdDeck_MAKE_CARD(rank, std_suit));
                }
            }
        }

        return result;
    }

/*
 * Compatibility macros - gradually replace these with mask_t operations
 */

/* Modern CardMask operations using mask_t backend */
#define MODERN_CardMask_OR(result, op1, op2)    \
    do                                          \
    {                                           \
        mask_t _m1 = cardmask_to_mask_t(op1);   \
        mask_t _m2 = cardmask_to_mask_t(op2);   \
        result = mask_t_to_cardmask(_m1 | _m2); \
    } while (0)

#define MODERN_CardMask_AND(result, op1, op2)   \
    do                                          \
    {                                           \
        mask_t _m1 = cardmask_to_mask_t(op1);   \
        mask_t _m2 = cardmask_to_mask_t(op2);   \
        result = mask_t_to_cardmask(_m1 & _m2); \
    } while (0)

#define MODERN_CardMask_XOR(result, op1, op2)   \
    do                                          \
    {                                           \
        mask_t _m1 = cardmask_to_mask_t(op1);   \
        mask_t _m2 = cardmask_to_mask_t(op2);   \
        result = mask_t_to_cardmask(_m1 ^ _m2); \
    } while (0)

#define MODERN_CardMask_NOT(result, op1)                        \
    do                                                          \
    {                                                           \
        mask_t _m1 = cardmask_to_mask_t(op1);                   \
        result = mask_t_to_cardmask(~_m1 & ((1ULL << 52) - 1)); \
    } while (0)

#define MODERN_CardMask_IS_EMPTY(mask) \
    (cardmask_to_mask_t(mask) == MASK_EMPTY)

#define MODERN_CardMask_EQUAL(mask1, mask2) \
    (cardmask_to_mask_t(mask1) == cardmask_to_mask_t(mask2))

#define MODERN_CardMask_CARD_IS_SET(mask, index) \
    mask_is_set(cardmask_to_mask_t(mask), index)

    /*
     * Migration helpers - use these in new code
     */

    /**
     * Extract card count from CardMask
     */
    static inline int cardmask_popcount(const StdDeck_CardMask cardmask)
    {
        return mask_popcount(cardmask_to_mask_t(cardmask));
    }

    /**
     * Check if CardMask represents a valid poker hand
     */
    static inline bool cardmask_is_valid_hand(const StdDeck_CardMask cardmask, int expected_cards)
    {
        return cardmask_popcount(cardmask) == expected_cards;
    }

    /**
     * Convert CardMask to string representation using modern formatter
     */
    const char *cardmask_to_string(const StdDeck_CardMask cardmask, char *buffer, size_t bufsize);

    /**
     * Parse string to CardMask using modern parser
     */
    bool string_to_cardmask(const char *str, StdDeck_CardMask *cardmask);

/*
 * Debugging and validation helpers
 */
#ifdef DEBUG
#define CARDMASK_ASSERT_VALID(mask, expected_cards) \
    assert(cardmask_is_valid_hand(mask, expected_cards))
#else
#define CARDMASK_ASSERT_VALID(mask, expected_cards) ((void)0)
#endif

/* Enable modern operations by default for new code */
#ifdef CARDMASK_USE_MODERN_OPS
#undef CardMask_OR
#undef CardMask_AND
#undef CardMask_XOR
#undef CardMask_NOT
#undef CardMask_IS_EMPTY
#undef CardMask_EQUAL
#undef CardMask_CARD_IS_SET

#define CardMask_OR MODERN_CardMask_OR
#define CardMask_AND MODERN_CardMask_AND
#define CardMask_XOR MODERN_CardMask_XOR
#define CardMask_NOT MODERN_CardMask_NOT
#define CardMask_IS_EMPTY MODERN_CardMask_IS_EMPTY
#define CardMask_EQUAL MODERN_CardMask_EQUAL
#define CardMask_CARD_IS_SET MODERN_CardMask_CARD_IS_SET
#endif

#endif /* CARDMASK_COMPAT_ENABLED */

#ifdef __cplusplus
}
#endif

#endif /* CARDMASK_COMPAT_H */
