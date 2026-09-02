/*
 * monker_classes.c - Four-card hand-class numbering
 */

#include <poker_eval/solver/pe_monker_classes.h>
#include <poker_eval/solver/pe_combinations.h>

#include <stdlib.h>
#include <string.h>

/*
 * The table is indexed by the colexicographic rank of the four-card set,
 * because that is the ranking this project already has. The *values* come
 * from the lexicographic enumeration, which is what defines the numbering —
 * the two orders are different and both are needed: lex to mint the indices,
 * colex to find them again.
 */
struct pe_monker_classes_t
{
    uint16_t *class_of_combo;
    uint8_t *representatives;
};

/*
 * Canonical form of a hand under suit relabelling, packed into 64 bits.
 *
 * Each suit contributes the 13-bit mask of its ranks; the four masks are
 * sorted so that relabelling the suits cannot change the result. Sorting is
 * what makes it canonical, and four elements make an insertion sort the right
 * one to write.
 */
static uint64_t canonical_key(const int *cards)
{
    uint32_t suits[4] = {0u, 0u, 0u, 0u};
    uint64_t key = 0u;
    int i;
    int j;

    for (i = 0; i < 4; ++i)
        suits[cards[i] / 13] |= 1u << (unsigned)(cards[i] % 13);
    for (i = 1; i < 4; ++i)
    {
        uint32_t value = suits[i];
        for (j = i - 1; j >= 0 && suits[j] > value; --j)
            suits[j + 1] = suits[j];
        suits[j + 1] = value;
    }
    for (i = 0; i < 4; ++i)
        key = (key << 13) | (uint64_t)suits[i];
    return key;
}

/* Open-addressed map from canonical key to class index, sized well above the
   16432 keys it holds so probing stays short. Keys are non-zero by
   construction: every hand puts at least one rank bit somewhere. */
#define PE_MONKER_MAP_SIZE 65536u

typedef struct
{
    uint64_t key;
    uint32_t value;
} class_entry_t;

static size_t map_slot(uint64_t key)
{
    /* SplitMix64 finalizer: the low bits of a packed suit key are far from
       uniform, and linear probing needs them to be. */
    key ^= key >> 30;
    key *= 0xbf58476d1ce4e5b9ULL;
    key ^= key >> 27;
    key *= 0x94d049bb133111ebULL;
    key ^= key >> 31;
    return (size_t)(key & (PE_MONKER_MAP_SIZE - 1u));
}

pe_monker_status_t pe_monker_classes_create(pe_monker_classes_t **out)
{
    pe_monker_classes_t *classes;
    class_entry_t *map;
    unsigned c0;
    unsigned c1;
    unsigned c2;
    unsigned c3;
    uint32_t next_class = 0u;

    if (out == NULL)
        return PE_MONKER_ERR_NULL_ARGUMENT;
    *out = NULL;

    classes = (pe_monker_classes_t *)calloc(1u, sizeof(*classes));
    if (classes == NULL)
        return PE_MONKER_ERR_IO;
    classes->class_of_combo = (uint16_t *)malloc(
        (size_t)PE_MONKER_COMBO_COUNT * sizeof(*classes->class_of_combo));
    classes->representatives = (uint8_t *)calloc(
        (size_t)PE_MONKER_CLASS_COUNT * 4u,
        sizeof(*classes->representatives));
    map = (class_entry_t *)calloc(PE_MONKER_MAP_SIZE, sizeof(*map));
    if (classes->class_of_combo == NULL || classes->representatives == NULL ||
        map == NULL)
    {
        free(map);
        pe_monker_classes_destroy(classes);
        return PE_MONKER_ERR_IO;
    }

    /* Lexicographic, because the numbering is the order of first appearance
       in exactly this loop. */
    for (c0 = 0u; c0 < 52u; ++c0)
        for (c1 = c0 + 1u; c1 < 52u; ++c1)
            for (c2 = c1 + 1u; c2 < 52u; ++c2)
                for (c3 = c2 + 1u; c3 < 52u; ++c3)
                {
                    int cards[4];
                    unsigned values[4];
                    uint64_t key;
                    uint64_t rank;
                    size_t slot;

                    cards[0] = (int)c0;
                    cards[1] = (int)c1;
                    cards[2] = (int)c2;
                    cards[3] = (int)c3;
                    size_t probes = 0u;

                    key = canonical_key(cards);
                    slot = map_slot(key);
                    /* Bounded, so a map that fills fails instead of spinning.
                       It cannot fill while the canonical form is right — 16432
                       keys in 65536 slots — which is exactly why the bound has
                       to be here rather than left to that assumption. */
                    while (map[slot].key != 0u && map[slot].key != key)
                    {
                        slot = (slot + 1u) & (PE_MONKER_MAP_SIZE - 1u);
                        if (++probes >= PE_MONKER_MAP_SIZE)
                        {
                            free(map);
                            pe_monker_classes_destroy(classes);
                            return PE_MONKER_ERR_INVALID_HEADER;
                        }
                    }
                    if (map[slot].key == 0u)
                    {
                        if (next_class >= PE_MONKER_CLASS_COUNT)
                        {
                            free(map);
                            pe_monker_classes_destroy(classes);
                            return PE_MONKER_ERR_INVALID_HEADER;
                        }
                        map[slot].key = key;
                        map[slot].value = next_class++;
                        classes->representatives[map[slot].value * 4u] =
                            (uint8_t)c0;
                        classes->representatives[map[slot].value * 4u + 1u] =
                            (uint8_t)c1;
                        classes->representatives[map[slot].value * 4u + 2u] =
                            (uint8_t)c2;
                        classes->representatives[map[slot].value * 4u + 3u] =
                            (uint8_t)c3;
                    }
                    values[0] = c0;
                    values[1] = c1;
                    values[2] = c2;
                    values[3] = c3;
                    if (pe_comb_rank(52u, 4u, values, &rank) != PE_SOLVER_OK ||
                        rank >= PE_MONKER_COMBO_COUNT)
                    {
                        free(map);
                        pe_monker_classes_destroy(classes);
                        return PE_MONKER_ERR_IO;
                    }
                    classes->class_of_combo[rank] =
                        (uint16_t)map[slot].value;
                }
    free(map);
    if (next_class != PE_MONKER_CLASS_COUNT)
    {
        /* The enumeration did not produce the numbering this header
           documents, so nothing downstream should be indexed by it. */
        pe_monker_classes_destroy(classes);
        return PE_MONKER_ERR_INVALID_HEADER;
    }
    *out = classes;
    return PE_MONKER_OK;
}

void pe_monker_classes_destroy(pe_monker_classes_t *classes)
{
    if (classes == NULL)
        return;
    free(classes->class_of_combo);
    free(classes->representatives);
    free(classes);
}

pe_monker_status_t pe_monker_class_of(const pe_monker_classes_t *classes,
                                      const int *cards,
                                      uint32_t *out_class)
{
    unsigned values[4];
    uint64_t rank;
    int i;
    int j;

    if (classes == NULL || cards == NULL || out_class == NULL)
        return PE_MONKER_ERR_NULL_ARGUMENT;
    for (i = 0; i < 4; ++i)
    {
        if (cards[i] < 0 || cards[i] >= 52)
            return PE_MONKER_ERR_INVALID_HEADER;
        values[i] = (unsigned)cards[i];
    }
    for (i = 1; i < 4; ++i)
    {
        unsigned value = values[i];
        for (j = i - 1; j >= 0 && values[j] > value; --j)
            values[j + 1] = values[j];
        values[j + 1] = value;
    }
    for (i = 1; i < 4; ++i)
        if (values[i] == values[i - 1])
            return PE_MONKER_ERR_INVALID_HEADER;
    if (pe_comb_rank(52u, 4u, values, &rank) != PE_SOLVER_OK ||
        rank >= PE_MONKER_COMBO_COUNT)
        return PE_MONKER_ERR_INVALID_HEADER;
    *out_class = classes->class_of_combo[rank];
    return PE_MONKER_OK;
}

pe_monker_status_t pe_monker_class_representative(
    const pe_monker_classes_t *classes,
    uint32_t class_index,
    int out_cards[4])
{
    unsigned card;

    if (classes == NULL || out_cards == NULL)
        return PE_MONKER_ERR_NULL_ARGUMENT;
    if (class_index >= PE_MONKER_CLASS_COUNT)
        return PE_MONKER_ERR_INVALID_HEADER;
    for (card = 0u; card < 4u; ++card)
        out_cards[card] = classes->representatives[class_index * 4u + card];
    return PE_MONKER_OK;
}
