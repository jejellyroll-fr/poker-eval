#include <poker_eval/engine/solvers/cfr/draw_chance.h>
#include <poker_eval/solver/pe_combinations.h>

static int valid_variant(pe_draw_variant_t variant, unsigned *max_cards)
{
    if (!max_cards) return 0;
    if (variant == PE_DRAW_BADUGI) *max_cards = 4u;
    else if (variant == PE_DRAW_TRIPLE_DRAW_27) *max_cards = 5u;
    else return 0;
    return 1;
}

int pe_draw_chance_init(pe_draw_chance_t *chance,
                        pe_draw_variant_t variant,
                        mask_t hand,
                        mask_t discard)
{
    unsigned max_cards = 0u;
    if (!chance || !valid_variant(variant, &max_cards) ||
        hand == MASK_EMPTY || !mask_is_valid(hand) ||
        !mask_is_valid(discard) || (discard & ~hand) != MASK_EMPTY ||
        mask_popcount(hand) > (int)max_cards)
        return -1;
    chance->variant = variant;
    chance->hand = hand;
    chance->discard = discard;
    chance->draw_count = (unsigned)mask_popcount(discard);
    return 0;
}

pe_chance_kind_t pe_draw_chance_kind(const pe_draw_chance_t *chance)
{
    return chance ? PE_CHANCE_DRAW_N : PE_CHANCE_NONE;
}

uint64_t pe_draw_chance_outcome_count(const pe_draw_chance_t *chance)
{
    if (!chance || chance->draw_count > PE_COMB_MAX_K) return 0u;
    return pe_comb_count((unsigned)(MODERN_DECK_SIZE -
                                    mask_popcount(chance->hand & ~chance->discard)),
                         chance->draw_count);
}

int pe_draw_chance_outcome(const pe_draw_chance_t *chance,
                           uint64_t outcome,
                           mask_t *replacement)
{
    int available[MODERN_DECK_SIZE];
    unsigned picked[PE_COMB_MAX_K];
    int count = 0;
    uint64_t total;
    if (!chance || !replacement) return -1;
    *replacement = MASK_EMPTY;
    total = pe_draw_chance_outcome_count(chance);
    if (outcome >= total) return -1;
    if (chance->draw_count == 0u) return 0;
    for (int card = 0; card < MODERN_DECK_SIZE; ++card)
        if (!mask_is_set(chance->hand & ~chance->discard, card))
            available[count++] = card;
    if (pe_comb_unrank((unsigned)count, chance->draw_count, outcome, picked) != PE_SOLVER_OK)
        return -1;
    for (unsigned i = 0u; i < chance->draw_count; ++i)
        *replacement = mask_set(*replacement, available[picked[i]]);
    return 0;
}

int pe_draw_chance_apply(const pe_draw_chance_t *chance,
                         uint64_t outcome,
                         mask_t *new_hand)
{
    mask_t replacement;
    if (!chance || !new_hand || pe_draw_chance_outcome(chance, outcome, &replacement) != 0)
        return -1;
    *new_hand = (chance->hand & ~chance->discard) | replacement;
    return 0;
}
