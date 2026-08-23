/*
 * evaluator_equity.c - CPU evaluator adapter for the solver port (GPU-02)
 */

#include <poker_eval/solver/pe_evaluator.h>
#include <poker_eval/equity.h>

#include <string.h>

static uint64_t evaluator_cpu_capabilities(void *self)
{
    (void)self;
    return PE_CAP_DETERMINISTIC;
}

static int evaluator_cpu_create(void **self)
{
    if (self == NULL)
        return -1;
    *self = (void *)1;
    return 0;
}

static void evaluator_cpu_destroy(void *self)
{
    (void)self;
}

static pe_evaluator_status_t evaluator_cpu_evaluate(
    void *self, const pe_evaluator_request_t *request,
    pe_evaluator_result_t *result)
{
    const pe_equity_result_multi_t *source;
    pe_equity_result_multi_t equity;
    pe_equity_opts_t options;
    pe_status_t status;
    uint32_t player;

    if (self == NULL || request == NULL || result == NULL ||
        request->ranges == NULL || request->player_count < 2u ||
        request->player_count > PE_EVALUATOR_MAX_PLAYERS)
        return PE_EVALUATOR_ERR_INVALID_REQUEST;
    if (request->force_monte_carlo != 0 && request->force_monte_carlo != 1)
        return PE_EVALUATOR_ERR_INVALID_REQUEST;
    memset(&options, 0, sizeof(options));
    options.is_monte_carlo = request->force_monte_carlo;
    options.iterations = request->iterations;
    status = pe_equity_multiway(NULL, request->game, request->ranges,
                                (int)request->player_count, request->board,
                                request->dead_cards, &options, &equity);
    if (status != PE_STATUS_OK)
        return PE_EVALUATOR_ERR_BACKEND;
    memset(result, 0, sizeof(*result));
    result->player_count = request->player_count;
    for (player = 0u; player < request->player_count; ++player) {
        source = &equity;
        result->equity[player] = source->results[player].equity;
        result->win_prob[player] = source->results[player].win_prob;
        result->tie_prob[player] = source->results[player].tie_prob;
        result->ev[player] = source->results[player].ev;
    }
    result->samples = equity.samples;
    result->exact = equity.exact;
    return PE_EVALUATOR_OK;
}

static int evaluator_cpu_sync(void *self)
{
    return self == NULL ? -1 : 0;
}

const pe_evaluator_ops_t *pe_evaluator_cpu_ops(void)
{
    static const pe_evaluator_ops_t ops = {
        "cpu_equity",
        evaluator_cpu_capabilities,
        evaluator_cpu_create,
        evaluator_cpu_destroy,
        evaluator_cpu_evaluate,
        evaluator_cpu_sync
    };
    return &ops;
}

const char *pe_evaluator_status_string(pe_evaluator_status_t status)
{
    switch (status) {
    case PE_EVALUATOR_OK: return "ok";
    case PE_EVALUATOR_ERR_NULL_ARGUMENT: return "null argument";
    case PE_EVALUATOR_ERR_INVALID_REQUEST: return "invalid evaluator request";
    case PE_EVALUATOR_ERR_UNSUPPORTED: return "unsupported evaluator backend";
    case PE_EVALUATOR_ERR_BACKEND: return "evaluator backend failure";
    default: return "unknown evaluator status";
    }
}
