/*
 * compute_cpu_ref.c - Stable one-thread F64 compute oracle (GPU-01)
 */

#include <poker_eval/solver/pe_compute.h>
#include <poker_eval/core/eval.h>
#include <poker_eval/games/eval_omaha.h>

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

static int cpu_ref_strategy_batch(void *self, const pe_infoset_batch_t *in,
                                  pe_strategy_batch_t *out)
{
    size_t infoset;

    (void)self;
    if (!in || !out || (in->count != 0u &&
                       (!in->offsets || !in->action_counts || !in->regrets)) ||
        (out->capacity != 0u && !out->strategies) ||
        out->capacity < (in->count != 0u ? in->offsets[in->count] : 0u))
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

        if (end < begin || (uint32_t)actions > end - begin)
            return -1;
        for (action = 0u; action < actions; ++action)
        {
            float regret = in->regrets[begin + action];
            if (!isfinite(regret))
                return -1;
            if (regret > 0.0f)
                positive += regret;
        }
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
                slot >= average_length ||
                !isfinite(regrets[slot] + update->delta) ||
                !isfinite(average[slot] + update->average_delta))
                return -1;
            regrets[slot] += update->delta;
            average[slot] += update->average_delta;
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
