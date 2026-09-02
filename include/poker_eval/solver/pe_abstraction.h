/*
 * pe_abstraction.h - Backend-independent strength/texture abstraction port
 * (ABS-01)
 */

#ifndef POKER_EVAL_PE_ABSTRACTION_H
#define POKER_EVAL_PE_ABSTRACTION_H

#include <poker_eval/core/eval_context.h>
#include <poker_eval/engine/solvers/cfr/board_texture.h>
#include <poker_eval/engine/solvers/cfr/strength_bucketing.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    pe_strength_cluster_opts_t strength;
    pe_texture_filter_level_t texture_filter;
} pe_abstraction_config_t;

typedef struct pe_abstraction_model_t pe_abstraction_model_t;

typedef struct
{
    const char *name;
    int (*train)(pe_abstraction_model_t **out,
                 const EvalContext *ctx,
                 mask_t board,
                 const mask_t *hands,
                 size_t hand_count,
                 const pe_abstraction_config_t *config);
    int (*save)(const pe_abstraction_model_t *model, const char *path);
    int (*load)(pe_abstraction_model_t **out, const char *path);
    void (*destroy)(pe_abstraction_model_t *model);
    int (*bucket_of)(const pe_abstraction_model_t *model,
                     const EvalContext *ctx,
                     mask_t hole,
                     mask_t board,
                     int street);
    uint64_t (*texture_of)(const pe_abstraction_model_t *model,
                           mask_t board,
                           int street);
} pe_abstraction_ops_t;

/** Strength-bucket plus board-texture adapter backed by the existing engine. */
const pe_abstraction_ops_t *pe_abstraction_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_ABSTRACTION_H */
