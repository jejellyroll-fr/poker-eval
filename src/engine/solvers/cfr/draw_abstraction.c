/* draw_abstraction.c - deterministic structural buckets for draw games */
#include <poker_eval/engine/solvers/cfr/draw_abstraction.h>

#include <string.h>

static int pe_draw_max_cards(pe_draw_variant_t variant)
{
    return variant == PE_DRAW_BADUGI ? 4 :
           variant == PE_DRAW_TRIPLE_DRAW_27 ? 5 : 0;
}

int pe_draw_features(pe_draw_variant_t variant,
                     mask_t hand,
                     mask_t discard,
                     pe_draw_features_t *out)
{
    const int max_cards = pe_draw_max_cards(variant);
    if (!out || max_cards == 0 || hand == MASK_EMPTY ||
        !mask_is_valid(hand) || !mask_is_valid(discard) ||
        (discard & ~hand) != MASK_EMPTY ||
        mask_popcount(hand) > max_cards)
        return -1;

    memset(out, 0, sizeof(*out));
    mask_t kept = hand & ~discard;
    out->cards_kept = (uint8_t)mask_popcount(kept);
    out->cards_discarded = (uint8_t)mask_popcount(discard);

    unsigned rank_counts[MODERN_RANK_COUNT] = {0};
    unsigned suit_counts[MODERN_SUIT_COUNT] = {0};
    for (int card = 0; card < MODERN_DECK_SIZE; ++card)
    {
        if (!mask_is_set(kept, card))
            continue;
        const int rank = MODERN_GET_RANK(card);
        const int suit = MODERN_GET_SUIT(card);
        rank_counts[rank]++;
        suit_counts[suit]++;
        /* Badugi uses ace-to-five lowball ordering; 2-7 Triple Draw
         * treats aces as high, like the rest of the 2-7 evaluator. */
        const int draw_rank = (variant == PE_DRAW_BADUGI &&
                               rank == MODERN_RANK_A) ? 1 : rank + 2;
        out->rank_sum = (uint16_t)(out->rank_sum + (uint16_t)draw_rank);
        if (draw_rank <= 8)
            out->low_cards++;
    }
    for (int rank = 0; rank < MODERN_RANK_COUNT; ++rank)
    {
        if (rank_counts[rank] != 0)
            out->distinct_ranks++;
        if (rank_counts[rank] > 1)
            out->paired_ranks += (uint8_t)(rank_counts[rank] - 1);
    }
    for (int suit = 0; suit < MODERN_SUIT_COUNT; ++suit)
        if (suit_counts[suit] != 0)
            out->distinct_suits++;
    return 0;
}

uint32_t pe_draw_abstraction_key(pe_draw_variant_t variant,
                                 mask_t hand,
                                 mask_t discard)
{
    pe_draw_features_t f;
    if (pe_draw_features(variant, hand, discard, &f) != 0)
        return 0;

    /* Keep the layout explicit: it is part of the stable abstraction ABI. */
    uint32_t key = 1u | ((uint32_t)variant << 1);
    key |= (uint32_t)(f.cards_discarded & 7u) << 3;
    key |= (uint32_t)(f.cards_kept & 7u) << 6;
    key |= (uint32_t)(f.distinct_ranks & 15u) << 9;
    key |= (uint32_t)(f.distinct_suits & 7u) << 13;
    key |= (uint32_t)(f.paired_ranks & 7u) << 16;
    key |= (uint32_t)(f.low_cards & 7u) << 19;
    key |= (uint32_t)(f.rank_sum & 0x3Fu) << 22;
    return key;
}
