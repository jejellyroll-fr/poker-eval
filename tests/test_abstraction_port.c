/* ABS-01: strength/texture abstraction port and .pe_sbk round-trip. */

#include <poker_eval/solver/pe_abstraction.h>

#include <stdio.h>
#include <string.h>

static mask_t card(int rank, int suit)
{
    return mask_set(MASK_EMPTY, MODERN_MAKE_CARD(rank, suit));
}

static mask_t hand(int r0, int s0, int r1, int s1)
{
    return mask_set(card(r0, s0), MODERN_MAKE_CARD(r1, s1));
}

int main(void)
{
    const pe_abstraction_ops_t *ops = pe_abstraction_ops();
    const char *path = "/tmp/poker_eval_abs01.pe_sbk";
    EvalConfig eval_cfg = eval_config_holdem();
    EvalContext *ctx = eval_context_create(&eval_cfg);
    pe_abstraction_config_t config = {0};
    pe_abstraction_model_t *model = NULL;
    pe_abstraction_model_t *loaded = NULL;
    mask_t board = card(1, 0);
    mask_t board2 = mask_set(mask_set(board, MODERN_MAKE_CARD(2, 1)),
                             MODERN_MAKE_CARD(12, 2));
    mask_t hands[] = {
        hand(0, 0, 0, 1), hand(3, 0, 3, 1), hand(4, 0, 4, 1),
        hand(5, 0, 5, 1), hand(6, 0, 6, 1), hand(7, 0, 7, 1),
        hand(8, 0, 8, 1), hand(9, 0, 9, 1)
    };
    size_t i;

    if (ops == NULL || ctx == NULL)
        return 1;
    config.strength.n_buckets = 4;
    config.strength.max_iterations = 10;
    config.texture_filter = PE_TEXTURE_FILTER_MEDIUM;
    if (ops->train(&model, ctx, board2, hands,
                   sizeof(hands) / sizeof(hands[0]), &config) != 0 ||
        model == NULL || ops->save(model, path) != 0 ||
        ops->load(&loaded, path) != 0 || loaded == NULL) {
        fprintf(stderr, "abstraction train/save/load failed\n");
        ops->destroy(model);
        ops->destroy(loaded);
        eval_context_destroy(ctx);
        return 1;
    }

    /* Exercise repeated lookups through the table cache, as the solver hot
       path does, and require exact bucket identity after reload. */
    for (i = 0u; i < 10000u; ++i) {
        size_t index = i % (sizeof(hands) / sizeof(hands[0]));
        int before = ops->bucket_of(model, ctx, hands[index], board2, 0);
        int after = ops->bucket_of(loaded, ctx, hands[index], board2, 0);
        if (before < 0 || before != after) {
            fprintf(stderr, "bucket round-trip mismatch at %zu\n", i);
            ops->destroy(model);
            ops->destroy(loaded);
            eval_context_destroy(ctx);
            return 1;
        }
    }
    if (ops->texture_of(model, board2, 0) !=
        pe_board_texture_id(board2, PE_TEXTURE_FILTER_MEDIUM)) {
        fprintf(stderr, "texture adapter mismatch\n");
        ops->destroy(model);
        ops->destroy(loaded);
        eval_context_destroy(ctx);
        return 1;
    }

    ops->destroy(model);
    ops->destroy(loaded);
    eval_context_destroy(ctx);
    remove(path);
    puts("test_abstraction_port: strength/texture round-trip passed");
    return 0;
}
