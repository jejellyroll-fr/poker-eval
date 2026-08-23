/* pruning_rbp.c - bounded regret-based pruning (RBP-01). */

#include <poker_eval/solver/pe_pruning.h>

#include <float.h>
#include <math.h>
#include <stdlib.h>

typedef struct
{
    pe_infoset_id_t infoset;
    uint16_t action;
    uint32_t cooldown;
    uint8_t pruned;
} pe_rbp_entry_t;

typedef struct
{
    pe_pruning_config_t config;
    pe_rbp_entry_t *entries;
    size_t count;
    size_t capacity;
    uint64_t iteration;
} pe_rbp_t;

static int rbp_create(void **self, const pe_pruning_config_t *config)
{
    pe_rbp_t *ctx;

    if (self == NULL || config == NULL || !isfinite(config->regret_threshold) ||
        config->regret_threshold >= 0.0 || config->revisit_interval == 0u)
        return -1;
    ctx = (pe_rbp_t *)calloc(1u, sizeof(*ctx));
    if (ctx == NULL)
        return -1;
    ctx->config = *config;
    *self = ctx;
    return 0;
}

static void rbp_destroy(void *self)
{
    pe_rbp_t *ctx = (pe_rbp_t *)self;
    if (ctx == NULL)
        return;
    free(ctx->entries);
    free(ctx);
}

static int rbp_begin_iteration(void *self, uint64_t iteration)
{
    pe_rbp_t *ctx = (pe_rbp_t *)self;
    if (ctx == NULL || (ctx->count != 0u && iteration < ctx->iteration))
        return -1;
    ctx->iteration = iteration;
    return 0;
}

static pe_rbp_entry_t *rbp_find(pe_rbp_t *ctx, pe_infoset_id_t infoset,
                                uint16_t action, int create)
{
    size_t i;
    pe_rbp_entry_t *grown;
    size_t capacity;

    for (i = 0u; i < ctx->count; ++i)
        if (ctx->entries[i].infoset == infoset &&
            ctx->entries[i].action == action)
            return &ctx->entries[i];
    if (!create)
        return NULL;
    if (ctx->count == ctx->capacity)
    {
        capacity = ctx->capacity == 0u ? 16u : ctx->capacity * 2u;
        if (capacity < ctx->capacity || capacity > SIZE_MAX / sizeof(*grown))
            return NULL;
        grown = (pe_rbp_entry_t *)realloc(ctx->entries,
                                          capacity * sizeof(*grown));
        if (grown == NULL)
            return NULL;
        ctx->entries = grown;
        ctx->capacity = capacity;
    }
    ctx->entries[ctx->count].infoset = infoset;
    ctx->entries[ctx->count].action = action;
    ctx->entries[ctx->count].cooldown = 0u;
    ctx->entries[ctx->count].pruned = 0u;
    return &ctx->entries[ctx->count++];
}

static int rbp_evaluate(void *self, const pe_pruning_span_t *span)
{
    pe_rbp_t *ctx = (pe_rbp_t *)self;
    uint16_t best_action = 0u;
    double best_regret = -DBL_MAX;
    int live = 0;
    uint16_t action;

    if (ctx == NULL || span == NULL || span->action_count == 0u ||
        span->cumulative_regrets == NULL || span->pruned == NULL)
        return -1;
    for (action = 0u; action < span->action_count; ++action)
    {
        pe_rbp_entry_t *entry;
        double regret = span->cumulative_regrets[action];
        if (!isfinite(regret))
            return -1;
        if (regret > best_regret)
        {
            best_regret = regret;
            best_action = action;
        }
        entry = rbp_find(ctx, span->infoset, action, 1);
        if (entry == NULL)
            return -1;
        if (entry->cooldown > 1u)
        {
            --entry->cooldown;
            entry->pruned = 1u;
        }
        else if (entry->cooldown == 1u)
        {
            entry->cooldown = 0u;
            if (regret <= ctx->config.regret_threshold)
            {
                entry->cooldown = ctx->config.revisit_interval;
                entry->pruned = 1u;
            }
            else
                entry->pruned = 0u;
        }
        else if (regret <= ctx->config.regret_threshold)
        {
            entry->cooldown = ctx->config.revisit_interval;
            entry->pruned = 1u;
        }
        else
            entry->pruned = 0u;
        span->pruned[action] = entry->pruned;
        if (!entry->pruned)
            live = 1;
    }
    if (!live)
    {
        pe_rbp_entry_t *entry = rbp_find(ctx, span->infoset, best_action, 0);
        if (entry == NULL)
            return -1;
        entry->cooldown = 0u;
        entry->pruned = 0u;
        span->pruned[best_action] = 0u;
    }
    return 0;
}

static int rbp_is_pruned(void *self, pe_infoset_id_t infoset,
                         uint16_t action, int *out_pruned)
{
    pe_rbp_t *ctx = (pe_rbp_t *)self;
    pe_rbp_entry_t *entry;
    if (ctx == NULL || out_pruned == NULL)
        return -1;
    entry = rbp_find(ctx, infoset, action, 0);
    if (entry == NULL)
        return -1;
    *out_pruned = entry->pruned != 0u;
    return 0;
}

static int rbp_end_iteration(void *self, uint64_t iteration)
{
    pe_rbp_t *ctx = (pe_rbp_t *)self;
    if (ctx == NULL || iteration < ctx->iteration)
        return -1;
    ctx->iteration = iteration;
    return 0;
}

const pe_pruning_ops_t *pe_pruning_rbp_ops(void)
{
    static const pe_pruning_ops_t ops = {
        "rbp",
        rbp_create,
        rbp_destroy,
        rbp_begin_iteration,
        rbp_evaluate,
        rbp_is_pruned,
        rbp_end_iteration
    };
    return &ops;
}
