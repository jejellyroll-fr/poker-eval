/*
 * @file test_pe_isomorphism.c
 * @brief ISO-01: exact suit-isomorphism classes for the complete flop space.
 */

#include <poker_eval/engine/solvers/cfr/board_canonical.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ISO_SLOTS 4096u
#define ISO_KEY_MAX 16

typedef struct
{
    char key[ISO_KEY_MAX];
    unsigned cardinal;
    int used;
} iso_slot_t;

static iso_slot_t g_slots[ISO_SLOTS];

static uint32_t iso_hash(const char *key)
{
    uint32_t h = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)key; *p; ++p)
        h = (uint32_t)((h ^ *p) * 16777619u);
    return h & (ISO_SLOTS - 1u);
}

static iso_slot_t *iso_find_or_add(const char *key)
{
    uint32_t base = iso_hash(key);
    for (uint32_t probe = 0; probe < ISO_SLOTS; ++probe)
    {
        iso_slot_t *slot = &g_slots[(base + probe) & (ISO_SLOTS - 1u)];
        if (!slot->used)
        {
            slot->used = 1;
            strncpy(slot->key, key, ISO_KEY_MAX - 1);
            slot->key[ISO_KEY_MAX - 1] = '\0';
            return slot;
        }
        if (strcmp(slot->key, key) == 0)
            return slot;
    }
    return NULL;
}

static mask_t flop_mask(unsigned a, unsigned b, unsigned c)
{
    mask_t mask = MASK_EMPTY;
    mask = mask_set(mask, (int)a);
    mask = mask_set(mask, (int)b);
    mask = mask_set(mask, (int)c);
    return mask;
}

int main(void)
{
    unsigned total = 0;
    unsigned classes = 0;

    memset(g_slots, 0, sizeof(g_slots));
    for (unsigned a = 0; a < MODERN_DECK_SIZE; ++a)
        for (unsigned b = a + 1; b < MODERN_DECK_SIZE; ++b)
            for (unsigned c = b + 1; c < MODERN_DECK_SIZE; ++c)
            {
                char key[ISO_KEY_MAX];
                iso_slot_t *slot;

                if (pe_board_canonical_key(flop_mask(a, b, c), 3,
                                           key, sizeof(key)) != 0)
                {
                    fprintf(stderr, "FAILED: canonical key for flop %u/%u/%u\n",
                            a, b, c);
                    return 1;
                }
                slot = iso_find_or_add(key);
                if (!slot)
                {
                    fprintf(stderr, "FAILED: isomorphism table is full\n");
                    return 1;
                }
                if (slot->cardinal == 0)
                    classes++;
                slot->cardinal++;
                total++;
            }

    if (total != 22100u)
    {
        fprintf(stderr, "FAILED: expected 22100 flops, got %u\n", total);
        return 1;
    }
    if (classes != 1755u)
    {
        fprintf(stderr, "FAILED: expected 1755 isomorphism classes, got %u\n",
                classes);
        return 1;
    }

    printf(" PASSED (flops=%u, classes=%u)\n", total, classes);
    return 0;
}
