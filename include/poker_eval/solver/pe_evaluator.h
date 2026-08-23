/*
 * pe_evaluator.h - Backend-independent terminal evaluator port (GPU-02)
 */

#ifndef POKER_EVAL_PE_EVALUATOR_H
#define POKER_EVAL_PE_EVALUATOR_H

#include <poker_eval/deck/deck_std.h>
#include <poker_eval/range.h>
#include <poker_eval/solver/pe_capabilities.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PE_EVALUATOR_MAX_PLAYERS 10u

typedef enum
{
    PE_EVALUATOR_OK = 0,
    PE_EVALUATOR_ERR_NULL_ARGUMENT,
    PE_EVALUATOR_ERR_INVALID_REQUEST,
    PE_EVALUATOR_ERR_UNSUPPORTED,
    PE_EVALUATOR_ERR_BACKEND
} pe_evaluator_status_t;

typedef struct
{
    enum_game_t game;
    const pe_range_t **ranges;
    uint32_t player_count;
    StdDeck_CardMask board;
    StdDeck_CardMask dead_cards;
    int force_monte_carlo;
    long iterations;
} pe_evaluator_request_t;

typedef struct
{
    uint32_t player_count;
    double equity[PE_EVALUATOR_MAX_PLAYERS];
    double win_prob[PE_EVALUATOR_MAX_PLAYERS];
    double tie_prob[PE_EVALUATOR_MAX_PLAYERS];
    double ev[PE_EVALUATOR_MAX_PLAYERS];
    long samples;
    int exact;
} pe_evaluator_result_t;

typedef struct
{
    const char *name;
    uint64_t (*capabilities)(void *self);
    int (*create)(void **self);
    void (*destroy)(void *self);
    pe_evaluator_status_t (*evaluate)(void *self,
                                      const pe_evaluator_request_t *request,
                                      pe_evaluator_result_t *result);
    int (*sync)(void *self);
} pe_evaluator_ops_t;

const pe_evaluator_ops_t *pe_evaluator_cpu_ops(void);
const pe_evaluator_ops_t *pe_evaluator_gpu_ops(void);

const char *pe_evaluator_status_string(pe_evaluator_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_EVALUATOR_H */
