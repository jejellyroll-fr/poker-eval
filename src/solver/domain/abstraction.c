/*
 * abstraction.c - Adapter for the existing strength/texture implementations
 * (ABS-01)
 */

#include <poker_eval/solver/pe_abstraction.h>

#include <stdlib.h>

struct pe_abstraction_model_t
{
    pe_strength_table_t *strength;
    pe_texture_filter_level_t texture_filter;
};

static int abstraction_train(pe_abstraction_model_t **out,
                             const EvalContext *ctx,
                             mask_t board,
                             const mask_t *hands,
                             size_t hand_count,
                             const pe_abstraction_config_t *config)
{
    pe_abstraction_model_t *model;
    pe_strength_cluster_opts_t options = {0};
    pe_texture_filter_level_t filter = PE_TEXTURE_FILTER_NONE;

    if (out == NULL || ctx == NULL || hands == NULL || hand_count == 0u)
        return -1;
    *out = NULL;
    if (config != NULL) {
        options = config->strength;
        filter = config->texture_filter;
    }
    if (filter < PE_TEXTURE_FILTER_NONE || filter >= PE_TEXTURE_FILTER_COUNT)
        return -1;

    model = (pe_abstraction_model_t *)calloc(1u, sizeof(*model));
    if (model == NULL)
        return -1;
    model->strength = pe_strength_table_train(ctx, board, hands, hand_count,
                                              &options, NULL);
    if (model->strength == NULL) {
        free(model);
        return -1;
    }
    model->texture_filter = filter;
    *out = model;
    return 0;
}

static int abstraction_save(const pe_abstraction_model_t *model,
                            const char *path)
{
    if (model == NULL || model->strength == NULL || path == NULL)
        return -1;
    return pe_strength_table_save(model->strength, path);
}

static int abstraction_load(pe_abstraction_model_t **out, const char *path)
{
    pe_abstraction_model_t *model;

    if (out == NULL || path == NULL)
        return -1;
    *out = NULL;
    model = (pe_abstraction_model_t *)calloc(1u, sizeof(*model));
    if (model == NULL)
        return -1;
    model->strength = pe_strength_table_load(path);
    if (model->strength == NULL) {
        free(model);
        return -1;
    }
    model->texture_filter = PE_TEXTURE_FILTER_NONE;
    *out = model;
    return 0;
}

static void abstraction_destroy(pe_abstraction_model_t *model)
{
    if (model == NULL)
        return;
    pe_strength_table_free(model->strength);
    free(model);
}

static int abstraction_bucket_of(const pe_abstraction_model_t *model,
                                 const EvalContext *ctx,
                                 mask_t hole,
                                 mask_t board,
                                 int street)
{
    (void)street;
    if (model == NULL || model->strength == NULL || ctx == NULL)
        return -1;
    return pe_strength_table_assign_cached(model->strength, ctx, hole, board);
}

static uint64_t abstraction_texture_of(const pe_abstraction_model_t *model,
                                       mask_t board,
                                       int street)
{
    (void)street;
    if (model == NULL)
        return 0u;
    return pe_board_texture_id(board, model->texture_filter);
}

const pe_abstraction_ops_t *pe_abstraction_ops(void)
{
    static const pe_abstraction_ops_t ops = {
        "strength_texture",
        abstraction_train,
        abstraction_save,
        abstraction_load,
        abstraction_destroy,
        abstraction_bucket_of,
        abstraction_texture_of
    };
    return &ops;
}
