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
#include <string.h>

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

/*
 * Every group is computed into a staging buffer seeded with the values the
 * storage currently holds, and the staging buffer is copied back only once
 * the whole batch has succeeded. A refused batch therefore leaves storage
 * byte-identical, which the AoS path gets for free from its target array and
 * which the caller is entitled to on the reference backend above all: it is
 * the oracle every other backend is compared against.
 */
static int cpu_ref_apply_soa(const pe_cpu_ref_t *backend,
                             const pe_update_batch_t *batch)
{
    size_t group_index;
    double *scratch_regrets = NULL;
    double *scratch_average = NULL;
    double **dest_regrets = NULL;
    double **dest_average = NULL;
    int status = -1;

    if (backend->config.storage == NULL || backend->config.storage_self == NULL)
        return 0;
    if (!backend->config.storage->shape || !backend->config.storage->values ||
        !pe_storage_serves(backend->config.storage, PE_VALUES_REGRET) ||
        !pe_storage_serves(backend->config.storage, PE_VALUES_AVERAGE))
        return -1;
    if (batch->soa.value_count > SIZE_MAX / sizeof(double) ||
        batch->soa.group_count > SIZE_MAX / sizeof(double *))
        return -1;
    scratch_regrets = (double *)malloc(
        batch->soa.value_count * sizeof(double));
    scratch_average = (double *)malloc(
        batch->soa.value_count * sizeof(double));
    dest_regrets = (double **)malloc(
        batch->soa.group_count * sizeof(double *));
    dest_average = (double **)malloc(
        batch->soa.group_count * sizeof(double *));
    if (scratch_regrets == NULL || scratch_average == NULL ||
        dest_regrets == NULL || dest_average == NULL)
        goto done;

    for (group_index = 0u; group_index < batch->soa.group_count; ++group_index)
    {
        const pe_update_group_t *group = &batch->soa.groups[group_index];
        uint16_t actions;
        uint16_t combos;
        size_t regret_length;
        size_t average_length;
        double *regrets;
        double *average;
        size_t value_index;

        if (group->actions == 0u || group->combos == 0u ||
            group->offset > batch->soa.value_count ||
            (size_t)group->actions > SIZE_MAX / (size_t)group->combos ||
            (size_t)group->actions * (size_t)group->combos >
                batch->soa.value_count - group->offset)
            goto done;
        if (backend->config.storage->shape(
                backend->config.storage_self, group->infoset,
                &actions, &combos, NULL) != 0 || actions != group->actions ||
            combos != group->combos)
            goto done;
        regrets = backend->config.storage->values(
            backend->config.storage_self, group->infoset,
            PE_VALUES_REGRET, &regret_length);
        average = backend->config.storage->values(
            backend->config.storage_self, group->infoset,
            PE_VALUES_AVERAGE, &average_length);
        if (regrets == NULL || average == NULL ||
            regret_length < (size_t)actions * combos ||
            average_length < (size_t)actions * combos)
            goto done;
        /* From here on the group works on its staging span, seeded with what
           storage holds today, so the kernels and the scalar path below need
           no change to become fail-before-write. */
        dest_regrets[group_index] = regrets;
        dest_average[group_index] = average;
        memcpy(scratch_regrets + group->offset, regrets,
               (size_t)actions * combos * sizeof(double));
        memcpy(scratch_average + group->offset, average,
               (size_t)actions * combos * sizeof(double));
        regrets = scratch_regrets + group->offset;
        average = scratch_average + group->offset;

        {
            size_t values = (size_t)actions * (size_t)combos;
            double positive_factor = 1.0;
            double negative_factor = 1.0;
            double average_scale = 1.0;
            int weighted_mode =
                backend->config.regret_mode == PE_REGRET_VANILLA ||
                backend->config.regret_mode == PE_REGRET_PLUS ||
                backend->config.regret_mode == PE_REGRET_DCFR;
            int weighted_update =
                backend->config.regret_mode == PE_REGRET_DCFR ||
                backend->config.averaging_mode == PE_AVG_LINEAR ||
                backend->config.averaging_mode == PE_AVG_POWER ||
                backend->config.averaging_mode == PE_AVG_DELAYED_LINEAR;
            int fast_safe = weighted_mode;
            size_t check_index;

            if (backend->config.regret_mode == PE_REGRET_DCFR)
            {
                double factors[2] = {1.0, -1.0};
                pe_dcfr_params_t params = {
                    backend->config.dcfr_alpha,
                    backend->config.dcfr_beta,
                    backend->config.dcfr_gamma
                };
                if (pe_dcfr_discount_regrets(
                        factors, 2u, batch->iteration, &params) != 0)
                    fast_safe = 0;
                else
                {
                    positive_factor = factors[0];
                    negative_factor = -factors[1];
                }
            }

            switch (backend->config.averaging_mode)
            {
            case PE_AVG_LINEAR:
                if (batch->iteration == 0u)
                    fast_safe = 0;
                else
                    average_scale = (double)batch->iteration;
                break;
            case PE_AVG_POWER:
                if (pe_dcfr_average_weight(
                        batch->iteration, backend->config.dcfr_gamma,
                        &average_scale) != 0)
                    fast_safe = 0;
                break;
            case PE_AVG_DELAYED_LINEAR:
                if (batch->iteration <= (uint64_t)(
                        backend->config.averaging_delay < 0
                            ? 0 : backend->config.averaging_delay))
                    average_scale = 0.0;
                else
                    average_scale = (double)(batch->iteration -
                                             (uint64_t)backend->config.averaging_delay);
                break;
            case PE_AVG_UNIFORM:
            case PE_AVG_IMPORTANCE:
            case PE_AVG_COUNT:
            default:
                break;
            }

            /* Validate before entering the SIMD kernel. This preserves the
               scalar API's fail-before-write behavior and excludes negative
               zero, whose sign is not preserved by max(+0, -0). */
            for (check_index = 0u; check_index < values; ++check_index)
            {
                double old_regret = regrets[check_index];
                double delta = batch->soa.deltas[group->offset + check_index];
                double old_average = average[check_index];
                double average_delta =
                    batch->soa.average_deltas[group->offset + check_index];
                if (!isfinite(old_regret) || !isfinite(delta) ||
                    !isfinite(old_average) || !isfinite(average_delta))
                    goto done;
                if (backend->config.regret_mode == PE_REGRET_PLUS &&
                    signbit(old_regret + delta) && old_regret + delta == 0.0)
                    fast_safe = 0;
            }
            if (fast_safe && (weighted_update
                    ? pe_compute_simd_apply_weighted(
                          regrets, average,
                          batch->soa.deltas + group->offset,
                          batch->soa.average_deltas + group->offset,
                          values, positive_factor, negative_factor,
                          average_scale,
                          backend->config.regret_mode == PE_REGRET_PLUS)
                    : pe_compute_simd_apply_uniform(
                          regrets, average,
                          batch->soa.deltas + group->offset,
                          batch->soa.average_deltas + group->offset,
                          values,
                          backend->config.regret_mode == PE_REGRET_PLUS)))
            {
                for (check_index = 0u; check_index < values; ++check_index)
                    if (!isfinite(regrets[check_index]) ||
                        !isfinite(average[check_index]))
                        goto done;
                continue;
            }
        }

        for (value_index = 0u;
             value_index < (size_t)actions * (size_t)combos; ++value_index)
        {
            pe_update_t update = {
                group->infoset,
                (uint16_t)(value_index / combos),
                (uint16_t)(value_index % combos),
                batch->soa.deltas[group->offset + value_index],
                batch->soa.average_deltas[group->offset + value_index]
            };
            double new_regret;
            double new_average;
            size_t slot = pe_storage_slot_at(combos, update.action,
                                              update.combo);
            if (!isfinite(update.delta) || !isfinite(update.average_delta) ||
                slot >= regret_length || slot >= average_length ||
                cpu_ref_update_values(&backend->config, batch, &update,
                                      regrets[slot], average[slot],
                                      &new_regret, &new_average) != 0)
                goto done;
            regrets[slot] = new_regret;
            average[slot] = new_average;
        }
    }

    for (group_index = 0u; group_index < batch->soa.group_count; ++group_index)
    {
        const pe_update_group_t *group = &batch->soa.groups[group_index];
        size_t values = (size_t)group->actions * (size_t)group->combos;
        memcpy(dest_regrets[group_index], scratch_regrets + group->offset,
               values * sizeof(double));
        memcpy(dest_average[group_index], scratch_average + group->offset,
               values * sizeof(double));
    }
    status = 0;

done:
    free(scratch_regrets);
    free(scratch_average);
    free(dest_regrets);
    free(dest_average);
    return status;
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
        if (positive > 0.0f && pe_compute_simd_regret_match(
                in->regrets + begin, out->strategies + begin, actions,
                positive))
        {
            for (uint32_t slot = begin + actions; slot < end; ++slot)
                out->strategies[slot] = 0.0f;
            continue;
        }
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
        (batch->count != 0u && batch->items == NULL) ||
        (batch->soa.group_count != 0u &&
         (batch->soa.groups == NULL || batch->soa.deltas == NULL ||
          batch->soa.average_deltas == NULL)))
        return -1;
    if (batch->soa.group_count != 0u)
        return cpu_ref_apply_soa(backend, batch);
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
