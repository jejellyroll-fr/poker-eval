#include <assert.h>
#include <stdio.h>

#include <poker_eval/engine/solvers/cfr/draw_abstraction.h>

static mask_t cards(const int *values, int n)
{
    mask_t result = MASK_EMPTY;
    for (int i = 0; i < n; ++i)
        result = mask_set(result, values[i]);
    return result;
}

int main(void)
{
    const int badugi_cards[] = {
        MODERN_MAKE_CARD(MODERN_RANK_A, MODERN_SUIT_CLUBS),
        MODERN_MAKE_CARD(MODERN_RANK_7, MODERN_SUIT_DIAMONDS),
        MODERN_MAKE_CARD(MODERN_RANK_4, MODERN_SUIT_HEARTS),
        MODERN_MAKE_CARD(MODERN_RANK_2, MODERN_SUIT_SPADES)
    };
    const mask_t badugi = cards(badugi_cards, 4);
    pe_draw_features_t f;
    assert(pe_draw_features(PE_DRAW_BADUGI, badugi, badugi_cards[1] ?
                            mask_set(MASK_EMPTY, badugi_cards[1]) : MASK_EMPTY, &f) == 0);
    assert(f.cards_kept == 3);
    assert(f.cards_discarded == 1);
    assert(f.distinct_ranks == 3);
    assert(f.distinct_suits == 3);
    assert(f.rank_sum == 7); /* A=1, 4=4, 2=2, 7 discarded */
    assert(f.low_cards == 3);
    assert(pe_draw_abstraction_key(PE_DRAW_BADUGI, badugi, MASK_EMPTY) != 0);

    pe_draw_features_t ace_features;
    assert(pe_draw_features(PE_DRAW_BADUGI, mask_set(MASK_EMPTY, badugi_cards[0]),
                            MASK_EMPTY, &ace_features) == 0);
    assert(ace_features.rank_sum == 1 && ace_features.low_cards == 1);
    assert(pe_draw_features(PE_DRAW_TRIPLE_DRAW_27,
                            mask_set(MASK_EMPTY, badugi_cards[0]),
                            MASK_EMPTY, &ace_features) == 0);
    assert(ace_features.rank_sum == 14 && ace_features.low_cards == 0);

    const int paired[] = {
        MODERN_MAKE_CARD(MODERN_RANK_2, MODERN_SUIT_CLUBS),
        MODERN_MAKE_CARD(MODERN_RANK_2, MODERN_SUIT_DIAMONDS),
        MODERN_MAKE_CARD(MODERN_RANK_5, MODERN_SUIT_HEARTS),
        MODERN_MAKE_CARD(MODERN_RANK_9, MODERN_SUIT_SPADES),
        MODERN_MAKE_CARD(MODERN_RANK_K, MODERN_SUIT_CLUBS)
    };
    mask_t triple = cards(paired, 5);
    assert(pe_draw_features(PE_DRAW_TRIPLE_DRAW_27, triple, MASK_EMPTY, &f) == 0);
    assert(f.cards_kept == 5 && f.paired_ranks == 1);
    assert(pe_draw_features(PE_DRAW_BADUGI, triple, MASK_EMPTY, &f) != 0);
    assert(pe_draw_abstraction_key(PE_DRAW_BADUGI, triple, MASK_EMPTY) == 0);

    assert(pe_draw_features(PE_DRAW_BADUGI, (mask_t)1ULL << 52,
                            MASK_EMPTY, &f) != 0);
    assert(pe_draw_features(PE_DRAW_BADUGI, badugi, (mask_t)1ULL << 52,
                            &f) != 0);
    assert(pe_draw_abstraction_key(PE_DRAW_BADUGI, (mask_t)1ULL << 52,
                                   MASK_EMPTY) == 0);

    puts("Draw abstraction tests passed");
    return 0;
}
