/* ABS-02: the general MPF infoset key uses (strength bucket, texture) when
 * explicitly enabled, while the zero-value configuration remains bit-exact. */

#include <poker_eval/engine/solvers/cfr/multiway_postflop_adapter.h>
#include <poker_eval/solver/pe_abstraction.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static mask_t one_card(int card)
{
    return mask_set(MASK_EMPTY, card);
}

static mask_t two_cards(int a, int b)
{
    return mask_set(one_card(a), b);
}

static int key_compare(const void *lhs, const void *rhs)
{
    uint64_t a = *(const uint64_t *)lhs;
    uint64_t b = *(const uint64_t *)rhs;
    return a < b ? -1 : (a > b ? 1 : 0);
}

static size_t unique_count(uint64_t *keys, size_t count)
{
    if (count == 0)
        return 0;
    qsort(keys, count, sizeof(*keys), key_compare);
    size_t unique = 1;
    for (size_t i = 1; i < count; ++i)
        if (keys[i] != keys[unique - 1])
            keys[unique++] = keys[i];
    return unique;
}

static void base_config(mpf_config_t *cfg, const EvalContext *ctx)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->ctx = ctx;
    cfg->rules = MPF_RULE_HOLDEM;
    cfg->num_players = 2;
    cfg->button_index = 0;
    cfg->start_street = MPF_STREET_FLOP;
    cfg->board_card_count = 3;
    cfg->board_cards[0] = 0;
    cfg->board_cards[1] = 20;
    cfg->board_cards[2] = 38;
    cfg->hole[0] = two_cards(1, 2);
    cfg->hole[1] = two_cards(3, 4);
    cfg->hole_specified[0] = 1;
    cfg->hole_specified[1] = 1;
    cfg->stacks[0] = 100.0;
    cfg->stacks[1] = 100.0;
    cfg->sb = 0.5;
    cfg->bb = 1.0;
}

static int collect_hands(mask_t board, mask_t *out, size_t cap)
{
    int cards[52];
    int count = 0;
    for (int card = 0; card < 52; ++card)
        if (!mask_is_set(board, card))
            cards[count++] = card;

    size_t written = 0;
    for (int i = 0; i < count; ++i)
        for (int j = i + 1; j < count && written < cap; ++j)
            out[written++] = two_cards(cards[i], cards[j]);
    return (int)written;
}

int main(void)
{
    EvalConfig eval_cfg = eval_config_holdem();
    EvalContext *ctx = eval_context_create(&eval_cfg);
    const pe_abstraction_ops_t *ops = pe_abstraction_ops();
    pe_abstraction_model_t *model = NULL;
    mpf_state_t exact_state;
    mpf_state_t abstract_state;
    cfr_game_t exact_game;
    cfr_game_t abstract_game;
    mpf_config_t exact_cfg;
    mpf_config_t abstract_cfg;
    mask_t board = one_card(0);
    board = mask_set(board, 20);
    board = mask_set(board, 38);
    mask_t hands[1326];
    uint64_t exact_keys[1326];
    uint64_t abstract_keys[1326];
    int hand_count;
    size_t exact_unique;
    size_t abstract_unique;
    int failure = 0;

    memset(&exact_state, 0, sizeof(exact_state));
    memset(&abstract_state, 0, sizeof(abstract_state));

    if (!ctx || !ops || !ops->train || !ops->destroy)
        return 1;
    hand_count = collect_hands(board, hands, sizeof(hands) / sizeof(hands[0]));
    pe_abstraction_config_t acfg;
    memset(&acfg, 0, sizeof(acfg));
    acfg.strength.n_buckets = 30;
    acfg.strength.hole_cards = 2;
    acfg.strength.max_iterations = 10;
    acfg.texture_filter = PE_TEXTURE_FILTER_NONE;
    if (ops->train(&model, ctx, board, hands, (size_t)hand_count, &acfg) != 0 ||
        !model)
    {
        fprintf(stderr, "ABS-02: model training failed\n");
        eval_context_destroy(ctx);
        return 1;
    }

    base_config(&exact_cfg, ctx);
    base_config(&abstract_cfg, ctx);
    abstract_cfg.strength_buckets_per_street = 30;
    abstract_cfg.texture_filter_level = PE_TEXTURE_FILTER_NONE;
    abstract_cfg.abstraction_model = model;
    if (mpf_build_game(&exact_cfg, &exact_game, &exact_state) != 0 ||
        mpf_build_game(&abstract_cfg, &abstract_game, &abstract_state) != 0)
    {
        fprintf(stderr, "ABS-02: MPF build failed\n");
        if (abstract_state.ctx)
            mpf_state_cleanup(&abstract_state);
        if (exact_state.ctx)
            mpf_state_cleanup(&exact_state);
        ops->destroy(model);
        eval_context_destroy(ctx);
        return 1;
    }

    /* The disabled branch must remain identical even when compared with a
       state built next to an enabled model. */
    mpf_state_t disabled_probe = abstract_state;
    disabled_probe.strength_buckets_per_street = 0;
    disabled_probe.abstraction_model = NULL;
    if (mpf_state_infoset_key(&disabled_probe) !=
        mpf_state_infoset_key(&exact_state))
    {
        fprintf(stderr, "ABS-02: disabled key changed\n");
        failure = 1;
    }

    for (int i = 0; i < hand_count; ++i)
    {
        mpf_state_t exact_probe = exact_state;
        mpf_state_t abstract_probe = abstract_state;
        exact_probe.to_act = 0;
        abstract_probe.to_act = 0;
        exact_probe.hole[0] = hands[i];
        abstract_probe.hole[0] = hands[i];
        exact_keys[i] = mpf_state_infoset_key(&exact_probe);
        abstract_keys[i] = mpf_state_infoset_key(&abstract_probe);
    }
    exact_unique = unique_count(exact_keys, (size_t)hand_count);
    abstract_unique = unique_count(abstract_keys, (size_t)hand_count);
    if (abstract_unique * 10u > exact_unique)
    {
        fprintf(stderr, "ABS-02: abstraction reduction too small (%zu -> %zu)\n",
                exact_unique, abstract_unique);
        failure = 1;
    }

    mpf_state_cleanup(&abstract_state);
    mpf_state_cleanup(&exact_state);
    ops->destroy(model);
    eval_context_destroy(ctx);
    if (failure)
        return 1;
    printf("test_mpf_abstraction: %zu -> %zu unique infoset keys\n",
           exact_unique, abstract_unique);
    return 0;
}
