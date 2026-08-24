#include <assert.h>
#include <stdio.h>

#include <poker_eval/engine/solvers/cfr/draw_chance.h>

static mask_t cards(const int *values, int count)
{
    mask_t result = MASK_EMPTY;
    for (int i = 0; i < count; ++i) result = mask_set(result, values[i]);
    return result;
}

int main(void)
{
    const int values[] = {
        MODERN_MAKE_CARD(MODERN_RANK_A, MODERN_SUIT_CLUBS),
        MODERN_MAKE_CARD(MODERN_RANK_7, MODERN_SUIT_DIAMONDS),
        MODERN_MAKE_CARD(MODERN_RANK_4, MODERN_SUIT_HEARTS),
        MODERN_MAKE_CARD(MODERN_RANK_2, MODERN_SUIT_SPADES)
    };
    pe_draw_chance_t chance;
    mask_t discard = mask_set(MASK_EMPTY, values[1]);
    assert(pe_draw_chance_init(&chance, PE_DRAW_BADUGI, cards(values, 4), discard) == 0);
    assert(pe_draw_chance_kind(&chance) == PE_CHANCE_DRAW_N);
    assert(pe_draw_chance_outcome_count(&chance) == 49u);
    for (uint64_t i = 0; i < pe_draw_chance_outcome_count(&chance); ++i) {
        mask_t replacement, new_hand;
        assert(pe_draw_chance_outcome(&chance, i, &replacement) == 0);
        assert(mask_popcount(replacement) == 1);
        assert((replacement & (chance.hand & ~chance.discard)) == MASK_EMPTY);
        assert(pe_draw_chance_apply(&chance, i, &new_hand) == 0);
        assert(mask_popcount(new_hand) == 4);
    }
    assert(pe_draw_chance_init(&chance, PE_DRAW_BADUGI, cards(values, 4), MASK_EMPTY) == 0);
    assert(pe_draw_chance_outcome_count(&chance) == 1u);
    {
        mask_t new_hand = MASK_EMPTY;
        assert(pe_draw_chance_apply(&chance, 0u, &new_hand) == 0);
        assert(new_hand == chance.hand);
    }
    puts("Draw chance tests passed");
    return 0;
}
