/*
 * modern_cardmask.c - Implementation of modern card mask utilities
 */

#include <poker_eval/core/modern_cardmask.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

/* Rank characters for string conversion. Sized from the initializer: the
 * explicit [13]/[4] bounds left no room for the terminating NUL, which Apple
 * clang and GCC 15 reject. Only ever indexed, never read as a string. */
static const char rank_chars[] = "23456789TJQKA";
static const char suit_chars[] = "cdhs";

/* Convert mask to string representation */
const char *mask_to_string(mask_t mask, char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size < 1)
        return NULL;

    buffer[0] = '\0';
    size_t pos = 0;

    for (int card = 0; card < 52 && pos < buffer_size - 3; card++)
    {
        if (mask_is_set(mask, card))
        {
            int rank = MODERN_GET_RANK(card);
            int suit = MODERN_GET_SUIT(card);

            if (pos > 0 && pos < buffer_size - 1)
            {
                buffer[pos++] = ' ';
            }

            if (pos < buffer_size - 1)
            {
                buffer[pos++] = rank_chars[rank];
            }
            if (pos < buffer_size - 1)
            {
                buffer[pos++] = suit_chars[suit];
            }
        }
    }

    buffer[pos] = '\0';
    return buffer;
}

/* Convert string to mask */
mask_t string_to_mask(const char *str)
{
    if (!str)
        return MASK_EMPTY;

    mask_t mask = MASK_EMPTY;
    const char *p = str;

    while (*p)
    {
        /* Skip whitespace */
        while (isspace(*p))
            p++;
        if (!*p)
            break;

        /* Read rank */
        char rank_ch = (char)toupper(*p++);
        if (!*p)
            break;

        /* Read suit */
        char suit_ch = (char)tolower(*p++);

        /* Convert rank character to index */
        int rank = -1;
        for (int i = 0; i < 13; i++)
        {
            if (toupper(rank_chars[i]) == rank_ch)
            {
                rank = i;
                break;
            }
        }

        /* Convert suit character to index */
        int suit = -1;
        for (int i = 0; i < 4; i++)
        {
            if (suit_chars[i] == suit_ch)
            {
                suit = i;
                break;
            }
        }

        /* Add card to mask if valid */
        if (rank >= 0 && suit >= 0)
        {
            int card = MODERN_MAKE_CARD(rank, suit);
            mask = mask_set(mask, card);
        }
    }

    return mask;
}

/* Debug and testing utilities */
void mask_print(mask_t mask)
{
    char buffer[256];
    printf("Mask: %s (%d cards)\n",
           mask_to_string(mask, buffer, sizeof(buffer)),
           mask_popcount(mask));
}

void mask_print_binary(mask_t mask)
{
    printf("Binary: ");
    for (int i = 51; i >= 0; i--)
    {
        printf("%c", mask_is_set(mask, i) ? '1' : '0');
        if (i % 13 == 0 && i > 0)
            printf(" ");
    }
    printf("\n");
}

/* Comprehensive validation */
bool mask_validate_hand(mask_t mask, int expected_cards)
{
    if (!mask_is_valid(mask))
    {
        return false;
    }

    int actual_cards = mask_popcount(mask);
    if (expected_cards >= 0 && actual_cards != expected_cards)
    {
        return false;
    }

    /* Check for duplicate cards (shouldn't happen with proper usage) */
    card_list_t list = mask_to_list(mask);
    for (int i = 0; i < list.count; i++)
    {
        for (int j = i + 1; j < list.count; j++)
        {
            if (list.cards[i] == list.cards[j])
            {
                return false;
            }
        }
    }

    return true;
}

/* Performance testing utilities */

mask_performance_t mask_benchmark(int iterations)
{
    mask_performance_t perf = {0};
    clock_t start, end;

    /* Benchmark set operations */
    start = clock();
    mask_t mask = MASK_EMPTY;
    for (int i = 0; i < iterations; i++)
    {
        for (int card = 0; card < 52; card++)
        {
            mask = mask_set(mask, card);
            mask = mask_unset(mask, card);
        }
    }
    end = clock();
    perf.set_ops_per_sec = (double)(iterations * 52 * 2) /
                           ((double)(end - start) / CLOCKS_PER_SEC);

    /* Benchmark popcount */
    mask = string_to_mask("As Kh Qd Jc Ts");
    start = clock();
    volatile int count = 0;
    for (int i = 0; i < iterations * 1000; i++)
    {
        count += mask_popcount(mask);
    }
    end = clock();
    perf.popcount_ops_per_sec = (double)(iterations * 1000) /
                                ((double)(end - start) / CLOCKS_PER_SEC);

    /* Benchmark iteration */
    start = clock();
    volatile int sum = 0;
    for (int i = 0; i < iterations; i++)
    {
        MASK_FOR_EACH_SET_BIT(mask, card)
        {
            sum += card;
        }
    }
    end = clock();
    perf.iteration_ops_per_sec = (double)iterations /
                                 ((double)(end - start) / CLOCKS_PER_SEC);

    (void)count; /* Suppress unused variable warning */
    (void)sum;   /* Suppress unused variable warning */

    return perf;
}
