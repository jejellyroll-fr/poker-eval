/*
 * compute_cpu_ref.c - Stable one-thread F64 compute oracle (GPU-01)
 */

#include <poker_eval/solver/pe_compute.h>
#include <poker_eval/solver/pe_regret_dcfr.h>
#include <poker_eval/core/eval.h>
#include <poker_eval/games/eval_omaha.h>

#include "compute_simd.h"

#include <math.h>
#include <stdlib.h>

typedef struct
{
    pe_compute_config_t config;
} pe_cpu_ref_t;

static uint64_t cpu_ref_capabilities(void *self)
{
    (void)self;
    return PE_CAP_DETERMINISTIC;
}

static int cpu_ref_create(void **self, const pe_compute_config_t *cfg)
{
    pe_cpu_ref_t *backend;

    if (self == NULL || cfg == NULL || cfg->cpu_threads < 0 ||
        cfg->cpu_threads > 1 || cfg->deterministic != 1 ||
        ((cfg->storage == NULL) != (cfg->storage_self == NULL)))
        return -1;
    backend = (pe_cpu_ref_t *)calloc(1u, sizeof(*backend));
    if (backend == NULL)
        return -1;
    backend->config = *cfg;
    *self = backend;
    return 0;
}

static void cpu_ref_destroy(void *self)
{
    free(self);
}

static int cpu_ref_update_values(const pe_compute_config_t *config,
                                 const pe_update_batch_t *batch,
                                 const pe_update_t *update,
                                 double old_regret, double old_average,
                                 double *out_regret, double *out_average)
{
    double regret = old_regret;
    double average_delta = update->average_delta;

    if (!config || !batch || !update || !out_regret || !out_average ||
        !isfinite(old_regret) || !isfinite(old_average))
        return -1;

    if (config->regret_mode == PE_REGRET_DCFR) {
        pe_dcfr_params_t params = {
            config->dcfr_alpha, config->dcfr_beta, config->dcfr_gamma
        };
        if (pe_dcfr_discount_regrets(&regret, 1u, batch->iteration,
                                     &params) != 0)
            return -1;
    }
    regret += update->delta;
    if (config->regret_mode == PE_REGRET_PLUS && regret < 0.0)
        regret = 0.0;

    switch (config->averaging_mode) {
    case PE_AVG_LINEAR:
        if (batch->iteration == 0u)
            return -1;
        average_delta *= (double)batch->iteration;
        break;
    case PE_AVG_POWER: {
        double weight;
        if (pe_dcfr_average_weight(batch->iteration, config->dcfr_gamma,
                                   &weight) != 0)
            return -1;
        average_delta *= weight;
        break;
    }
    case PE_AVG_DELAYED_LINEAR:
        if (batch->iteration <= (uint64_t)(config->averaging_delay < 0
                                             ? 0 : config->averaging_delay))
            average_delta = 0.0;
        else
            average_delta *= (double)(batch->iteration -
                                      (uint64_t)config->averaging_delay);
        break;
    case PE_AVG_UNIFORM:
    case PE_AVG_IMPORTANCE:
    case PE_AVG_COUNT:
    default:
        break;
    }
    if (!isfinite(regret) || !isfinite(old_average + average_delta))
        return -1;
    *out_regret = regret;
    *out_average = old_average + average_delta;
    return 0;
}

static int cpu_ref_strategy_batch(void *self, const pe_infoset_batch_t *in,
                                  pe_strategy_batch_t *out)
{
    const pe_cpu_ref_t *backend = (const pe_cpu_ref_t *)self;
    size_t infoset;

    if (!backend || !in || !out || (in->count != 0u &&
                       (!in->offsets || !in->action_counts || !in->regrets)) ||
        (out->capacity != 0u && !out->strategies) ||
        out->capacity < (in->count != 0u ? in->offsets[in->count] : 0u))
        return -1;
    if (backend->config.policy_mode == PE_POLICY_EXPONENTIAL &&
        (!isfinite(backend->config.exponential_lambda) ||
         backend->config.exponential_lambda <= 0.0))
        return -1;
    if (in->count == 0u)
    {
        out->count = 0u;
        out->offsets = in->offsets;
        return 0;
    }
    if (!out->strategies)
        return -1;
    if (!out->offsets)
        out->offsets = in->offsets;
    for (infoset = 0u; infoset < in->count; ++infoset)
    {
        uint32_t begin = in->offsets[infoset];
        uint32_t end = in->offsets[infoset + 1u];
        uint16_t actions = in->action_counts[infoset];
        float positive = 0.0f;
        uint16_t action;

        if (end < begin || actions == 0u ||
            (uint32_t)actions > end - begin)
            return -1;
        if (backend->config.policy_mode == PE_POLICY_EXPONENTIAL)
        {
            float maximum = -INFINITY;
            double total = 0.0;

            for (action = 0u; action < actions; ++action)
            {
                float regret = in->regrets[begin + action];
                if (!isfinite(regret))
                    return -1;
                if (regret > maximum)
                    maximum = regret;
            }
            for (action = 0u; action < actions; ++action)
            {
                double weight = exp(backend->config.exponential_lambda *
                                    ((double)in->regrets[begin + action] -
                                     (double)maximum));
                if (!isfinite(weight))
                    return -1;
                total += weight;
            }
            if (!isfinite(total) || total <= 0.0)
                return -1;
            for (action = 0u; action < actions; ++action)
            {
                double weight = exp(backend->config.exponential_lambda *
                                    ((double)in->regrets[begin + action] -
                                     (double)maximum));
                out->strategies[begin + action] = (float)(weight / total);
            }
            for (uint32_t slot = begin + actions; slot < end; ++slot)
                out->strategies[slot] = 0.0f;
            continue;
        }
        for (action = 0u; action < actions; ++action)
            if (!isfinite(in->regrets[begin + action]))
                return -1;
        positive = pe_compute_simd_positive_sum(in->regrets + begin, actions);
        if (!isfinite(positive))
            return -1;
        for (action = 0u; action < actions; ++action)
        {
            float regret = in->regrets[begin + action];
            out->strategies[begin + action] = positive > 0.0f
                ? (regret > 0.0f ? regret / positive : 0.0f)
                : 1.0f / (float)actions;
        }
        for (uint32_t slot = begin + actions; slot < end; ++slot)
            out->strategies[slot] = 0.0f;
    }
    out->count = in->count;
    return 0;
}

static int cpu_ref_apply_update_batch(void *self,
                                      const pe_update_batch_t *batch)
{
    const pe_cpu_ref_t *backend = (const pe_cpu_ref_t *)self;
    size_t index;

    if (backend == NULL || batch == NULL ||
        (batch->count != 0u && batch->items == NULL))
        return -1;
    for (index = 0u; index < batch->count; ++index) {
        const pe_update_t *update = &batch->items[index];
        if (!isfinite(update->delta) || !isfinite(update->average_delta))
            return -1;
        if (backend->config.storage != NULL) {
            uint16_t actions;
            uint16_t combos;
            size_t regret_length;
            size_t average_length;
            size_t slot;
            double *regrets;
            double *average;

            if (!backend->config.storage->shape ||
                !backend->config.storage->values ||
                !pe_storage_serves(backend->config.storage, PE_VALUES_REGRET) ||
                !pe_storage_serves(backend->config.storage, PE_VALUES_AVERAGE) ||
                backend->config.storage->shape(backend->config.storage_self,
                                               update->infoset, &actions, &combos,
                                               NULL) != 0 ||
                update->action >= actions || update->combo >= combos)
                return -1;
            slot = pe_storage_slot_at(combos, update->action, update->combo);
            regrets = backend->config.storage->values(
                backend->config.storage_self, update->infoset,
                PE_VALUES_REGRET, &regret_length);
            average = backend->config.storage->values(
                backend->config.storage_self, update->infoset,
                PE_VALUES_AVERAGE, &average_length);
            if (regrets == NULL || average == NULL || slot >= regret_length ||
                slot >= average_length)
                return -1;
            {
                double new_regret;
                double new_average;
                if (cpu_ref_update_values(&backend->config, batch, update,
                                          regrets[slot], average[slot],
                                          &new_regret, &new_average) != 0)
                    return -1;
                regrets[slot] = new_regret;
                average[slot] = new_average;
            }
        }
    }
    return 0;
}

static int cpu_ref_terminal_eval_batch(void *self,
                                       const pe_terminal_batch_t *in,
                                       pe_value_batch_t *out)
{
    size_t index;

    (void)self;
    if (in == NULL || out == NULL || in->count == 0u || out->values == NULL ||
        out->capacity < in->count)
        return -1;

    for (index = 0u; index < in->count; ++index) {
        StdDeck_CardMask cards;
        size_t card_index;

        StdDeck_CardMask_RESET(cards);
        if (in->game == game_holdem || in->game == game_7stud) {
            if (in->cards == NULL)
                return -1;
            for (card_index = 0u; card_index < 7u; ++card_index) {
                const uint8_t card = in->cards[index * 7u + card_index];
                if (card >= StdDeck_N_CARDS ||
                    StdDeck_CardMask_CARD_IS_SET(cards, card))
                    return -1;
                StdDeck_CardMask_SET(cards, card);
            }
            out->values[index] = (uint32_t)StdDeck_StdRules_EVAL_N(cards, 7);
        } else if (in->game == game_omaha || in->game == game_omaha8 ||
                   in->game == game_omaha5 || in->game == game_omaha6) {
            StdDeck_CardMask hole;
            StdDeck_CardMask board;
            HandVal value = HandVal_NOTHING;
            size_t hole_cards = 4u;
            size_t hole_index;

            if (in->hole == NULL || in->board == NULL)
                return -1;
            if (in->game == game_omaha5)
                hole_cards = 5u;
            else if (in->game == game_omaha6)
                hole_cards = 6u;
            StdDeck_CardMask_RESET(hole);
            StdDeck_CardMask_RESET(board);
            for (hole_index = 0u; hole_index < hole_cards; ++hole_index) {
                const uint8_t card = in->hole[index * hole_cards + hole_index];
                if (card >= StdDeck_N_CARDS ||
                    StdDeck_CardMask_CARD_IS_SET(hole, card))
                    return -1;
                StdDeck_CardMask_SET(hole, card);
            }
            for (card_index = 0u; card_index < 5u; ++card_index) {
                const uint8_t card = in->board[index * 5u + card_index];
                if (card >= StdDeck_N_CARDS ||
                    StdDeck_CardMask_CARD_IS_SET(hole, card) ||
                    StdDeck_CardMask_CARD_IS_SET(board, card))
                    return -1;
                StdDeck_CardMask_SET(board, card);
            }
            if (StdDeck_OmahaHi_EVAL(hole, board, &value) != 0)
                return -1;
            out->values[index] = (uint32_t)value;
        } else {
            return -1;
        }
    }
    out->count = in->count;
    return 0;
}

static int cpu_ref_vector_showdown(void *self, const pe_showdown_job_t *job,
                                   pe_value_vec_t *out)
{
    (void)self;
    (void)job;
    (void)out;
    return -1;
}

static int cpu_ref_sync(void *self)
{
    return self == NULL ? -1 : 0;
}

const pe_compute_ops_t *pe_compute_cpu_ref_ops(void)
{
    static const pe_compute_ops_t ops = {
        "cpu_ref",
        cpu_ref_capabilities,
        cpu_ref_create,
        cpu_ref_destroy,
        cpu_ref_strategy_batch,
        cpu_ref_apply_update_batch,
        cpu_ref_terminal_eval_batch,
        cpu_ref_vector_showdown,
        cpu_ref_sync
    };
    return &ops;
}
