/*
 * mpf_stack_index.c - Sparse state indexer for multiway asymmetrical stacks
 *                     (FEAT-10, #146).
 */

#include <poker_eval/engine/solvers/cfr/mpf_stack_index.h>

#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef MPF_STACK_INDEX_EPS
#define MPF_STACK_INDEX_EPS 1e-9
#endif

/* FNV-1a 64-bit, standalone so this module has no dependency on the adapter's
 * static helpers. Used both for bucket placement and for deterministic id
 * ordering. */
static uint64_t mpf_si_fnv1a(const void *data, size_t len)
{
    uint64_t h = 1469598103934665603ull;
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < len; ++i)
    {
        h ^= (uint64_t)p[i];
        h *= 1099511628211ull;
    }
    return h;
}

/* Quantize a double to a stable 64-bit key so that 100.0 reached via different
 * float paths still dedups. Mirrors the adapter's mpf_quant intent. */
static uint64_t mpf_si_quant(double v)
{
    if (v < 0.0)
        v = 0.0;
    double q = floor(v / (1.0 + MPF_STACK_INDEX_EPS) + 0.5);
    if (q > 9.2233720368547758e18)
        q = 9.2233720368547758e18;
    return (uint64_t)q;
}

uint64_t mpf_stack_config_hash(const mpf_stack_config_t *cfg)
{
    uint64_t h = mpf_si_fnv1a(&cfg->num_players, sizeof(cfg->num_players));
    h = mpf_si_fnv1a(&cfg->active_mask, sizeof(cfg->active_mask));
    for (int i = 0; i < cfg->num_players; ++i)
    {
        if (!(cfg->active_mask & (1u << (uint32_t)i)))
            continue;
        uint64_t rc = mpf_si_quant(cfg->round_contrib[i]);
        uint64_t rem = mpf_si_quant(cfg->remaining[i]);
        h = mpf_si_fnv1a(&rc, sizeof(rc));
        h = mpf_si_fnv1a(&rem, sizeof(rem));
    }
    return h;
}

typedef struct
{
    uint64_t hash;
    mpf_stack_config_t cfg;
    uint32_t id;
    int used;
} mpf_si_slot_t;

struct mpf_stack_index_t
{
    mpf_si_slot_t *tab;
    size_t cap;
    size_t used;
    uint32_t next_id; /* next id to hand out (starts at 1) */
};

static size_t mpf_si_next_pow2(size_t x)
{
    if (x == 0)
        return 16;
    size_t p = 1;
    while (p < x && p < ((size_t)1 << 62))
        p <<= 1;
    return p;
}

mpf_stack_index_t *mpf_stack_index_create(size_t cap_hint)
{
    mpf_stack_index_t *idx = (mpf_stack_index_t *)malloc(sizeof(*idx));
    if (!idx)
        return NULL;
    idx->cap = mpf_si_next_pow2(cap_hint < 16 ? 16 : cap_hint);
    idx->tab = (mpf_si_slot_t *)calloc(idx->cap, sizeof(mpf_si_slot_t));
    if (!idx->tab)
    {
        free(idx);
        return NULL;
    }
    idx->used = 0;
    idx->next_id = 1; /* 0 reserved for "unindexed" */
    return idx;
}

void mpf_stack_index_destroy(mpf_stack_index_t *idx)
{
    if (!idx)
        return;
    free(idx->tab);
    free(idx);
}

/* Find the slot index for a config, or -1 if absent. */
static size_t mpf_si_find(const mpf_stack_index_t *idx,
                          const mpf_stack_config_t *cfg,
                          uint64_t hash)
{
    size_t mask = idx->cap - 1;
    size_t j = (size_t)(hash & mask);
    size_t start = j;
    while (idx->tab[j].used)
    {
        if (idx->tab[j].hash == hash &&
            idx->tab[j].cfg.num_players == cfg->num_players &&
            idx->tab[j].cfg.active_mask == cfg->active_mask)
        {
            int same = 1;
            for (int i = 0; i < cfg->num_players; ++i)
            {
                if (!(cfg->active_mask & (1u << (uint32_t)i)))
                    continue;
                if (idx->tab[j].cfg.round_contrib[i] != cfg->round_contrib[i] ||
                    idx->tab[j].cfg.remaining[i] != cfg->remaining[i])
                {
                    same = 0;
                    break;
                }
            }
            if (same)
                return j;
        }
        j = (j + 1) & mask;
        if (j == start)
            break;
    }
    return (size_t)-1;
}

/* Grow and re-insert every live entry, preserving deterministic id assignment
 * (ids are stored on the slot and never recomputed). */
static int mpf_si_rehash(mpf_stack_index_t *idx)
{
    size_t new_cap = idx->cap * 2;
    if (new_cap <= idx->cap) /* overflow guard */
        return -1;
    mpf_si_slot_t *new_tab = (mpf_si_slot_t *)calloc(new_cap, sizeof(mpf_si_slot_t));
    if (!new_tab)
        return -1;
    size_t new_mask = new_cap - 1;
    for (size_t i = 0; i < idx->cap; ++i)
    {
        if (!idx->tab[i].used)
            continue;
        size_t j = (size_t)(idx->tab[i].hash & new_mask);
        while (new_tab[j].used)
            j = (j + 1) & new_mask;
        new_tab[j] = idx->tab[i];
    }
    free(idx->tab);
    idx->tab = new_tab;
    idx->cap = new_cap;
    return 0;
}

int mpf_stack_index_put(mpf_stack_index_t *idx,
                        const mpf_stack_config_t *cfg,
                        uint32_t *out_id)
{
    if (!idx || !cfg || !out_id)
        return -1;
    if (cfg->num_players < 1 || cfg->num_players > MPF_STACK_INDEX_MAX_PLAYERS)
        return -1;

    uint64_t hash = mpf_stack_config_hash(cfg);
    size_t found = mpf_si_find(idx, cfg, hash);
    if (found != (size_t)-1)
    {
        *out_id = idx->tab[found].id;
        return 0;
    }

    /* Keep load factor <= 0.7 before inserting. */
    if (idx->used + 1 > (idx->cap * 7) / 10)
    {
        if (mpf_si_rehash(idx) != 0)
            return -1;
    }

    size_t mask = idx->cap - 1;
    size_t j = (size_t)(hash & mask);
    while (idx->tab[j].used)
        j = (j + 1) & mask;

    idx->tab[j].hash = hash;
    idx->tab[j].cfg = *cfg;
    idx->tab[j].id = idx->next_id++;
    idx->tab[j].used = 1;
    idx->used++;
    *out_id = idx->tab[j].id;
    return 0;
}

int mpf_stack_index_get(const mpf_stack_index_t *idx,
                        const mpf_stack_config_t *cfg,
                        uint32_t *out_id)
{
    if (!idx || !cfg || !out_id)
        return 0;
    if (cfg->num_players < 1 || cfg->num_players > MPF_STACK_INDEX_MAX_PLAYERS)
        return 0;
    uint64_t hash = mpf_stack_config_hash(cfg);
    size_t found = mpf_si_find(idx, cfg, hash);
    if (found == (size_t)-1)
        return 0;
    *out_id = idx->tab[found].id;
    return 1;
}

size_t mpf_stack_index_count(const mpf_stack_index_t *idx)
{
    return idx ? (size_t)idx->next_id - 1 : 0;
}

size_t mpf_stack_index_capacity(const mpf_stack_index_t *idx)
{
    return idx ? idx->cap : 0;
}

void mpf_stack_config_from_arrays(mpf_stack_config_t *out,
                                  int num_players,
                                  const double *round_contrib,
                                  const double *remaining,
                                  const int *active)
{
    memset(out, 0, sizeof(*out));
    if (num_players < 1)
        num_players = 1;
    if (num_players > MPF_STACK_INDEX_MAX_PLAYERS)
        num_players = MPF_STACK_INDEX_MAX_PLAYERS;
    out->num_players = num_players;
    out->active_mask = 0;
    for (int i = 0; i < num_players; ++i)
    {
        int is_active = active ? (active[i] ? 1 : 0) : 1;
        if (is_active)
            out->active_mask |= (1u << (uint32_t)i);
        out->round_contrib[i] = round_contrib ? round_contrib[i] : 0.0;
        out->remaining[i] = remaining ? remaining[i] : 0.0;
    }
}

/* ---- Compact reach-weight mapping ------------------------------------- */

struct mpf_reach_map_t
{
    double *weights;     /* [cfg_id * num_players + player] */
    size_t config_count; /* number of config slots (id 0 unused) */
    int num_players;
};

mpf_reach_map_t *mpf_reach_map_create(size_t config_count, int num_players)
{
    if (num_players < 1 || num_players > MPF_STACK_INDEX_MAX_PLAYERS)
        return NULL;
    /* id 0 is reserved; allocate one extra slot so cfg_id == config_count-1
     * is the highest valid id. */
    size_t slots = config_count + 1;
    mpf_reach_map_t *map = (mpf_reach_map_t *)malloc(sizeof(*map));
    if (!map)
        return NULL;
    map->weights = (double *)calloc((size_t)slots * (size_t)num_players, sizeof(double));
    if (!map->weights)
    {
        free(map);
        return NULL;
    }
    map->config_count = slots;
    map->num_players = num_players;
    return map;
}

void mpf_reach_map_destroy(mpf_reach_map_t *map)
{
    if (!map)
        return;
    free(map->weights);
    free(map);
}

static int mpf_reach_map_valid(const mpf_reach_map_t *map,
                               uint32_t cfg_id,
                               int player)
{
    if (!map)
        return 0;
    if (player < 0 || player >= map->num_players)
        return 0;
    if (cfg_id == 0 || (size_t)cfg_id >= map->config_count)
        return 0;
    return 1;
}

int mpf_reach_map_set(mpf_reach_map_t *map,
                      uint32_t cfg_id,
                      int player,
                      double weight)
{
    if (!mpf_reach_map_valid(map, cfg_id, player))
        return -1;
    map->weights[(size_t)cfg_id * map->num_players + player] = weight;
    return 0;
}

double mpf_reach_map_get(const mpf_reach_map_t *map,
                         uint32_t cfg_id,
                         int player,
                         int *ok)
{
    if (!mpf_reach_map_valid(map, cfg_id, player))
    {
        if (ok)
            *ok = 0;
        return 0.0;
    }
    if (ok)
        *ok = 1;
    return map->weights[(size_t)cfg_id * map->num_players + player];
}
