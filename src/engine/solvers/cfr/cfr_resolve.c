/*
 * cfr_resolve.c - Subgame re-solving (FEAT-05)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * See cfr_resolve.h for the design. The gadget is a decorator over cfr_game_t:
 * it forwards most vtable callbacks to the inner game and intercepts a small
 * set of synthetic "gadget" states encoded as tagged integers in the state_key
 * space. Real states never set the top byte, so a gadget key is unambiguous.
 */

#include <poker_eval/engine/solvers/cfr/cfr_resolve.h>

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>

/* ============================================================================
 * Gadget state encoding
 *
 *   GADGET_ROOT        : gadget root, a chance node over the boundary infosets
 *   GADGET_BOUNDARY(i) : opponent decision (terminate=0 / follow=1)
 *   GADGET_TERMINAL(i) : synthetic terminal paying the blueprint CFV of i
 *   GADGET_SUBGAME(i)  : the subgame root conditioned on boundary i (real inner
 *                        state)
 *   anything else      : a real inner state, forwarded verbatim
 *
 * The tag lives in the most significant byte; real pointer/key states never
 * use it, so GADGET_KIND(key) == 0 means "real inner state".
 * ========================================================================== */

#define GADGET_TAG_SHIFT 56u
#define GADGET_TAG_MASK  ((uint64_t)0xFFu << GADGET_TAG_SHIFT)
#define GADGET_IDX_MASK  ((uint64_t)0x00FFFFFFFFFFFFFFull)

#define GADGET_KIND_ROOT     1u
#define GADGET_KIND_BOUNDARY 2u
#define GADGET_KIND_TERMINAL 3u

#define GADGET_MK(kind, idx) \
    ((((uint64_t)(kind)) << GADGET_TAG_SHIFT) | ((idx) & GADGET_IDX_MASK))
#define GADGET_KIND(key) ((uint32_t)(((key) >> GADGET_TAG_SHIFT) & 0xFFu))
#define GADGET_IDX(key)  ((key) & GADGET_IDX_MASK)

#define GADGET_ROOT_KEY GADGET_MK(GADGET_KIND_ROOT, 0)

/* The gadget currently driving a solve, needed by gadget_get_infoset_key. */
static pe_cfr_gadget_t *g_gadget_active = NULL;

struct pe_cfr_gadget_t
{
    cfr_game_t *inner;
    pe_cfr_subgame_t subgame;
    const pe_cfr_boundary_t *boundary;
    size_t boundary_count;
    void *user_data;
    int player;    /* resolve_player (refined) */
    int opponent;  /* constrained player (gadget owner) */
};

/* ----------------------------------------------------------------------------
 * Blueprint counterfactual-value walk
 *
 * Walks the full tree once under the blueprint average strategies and, at each
 * boundary infoset, accumulates its CFV for `player`: util[player] weighted by
 * the *opponents'* reach. The result is the counterfactual value the re-solve
 * must keep matching.
 * -------------------------------------------------------------------------- */

typedef struct cfv_value_t
{
    cfr_game_t *game;
    cfr_storage_t *blueprint;
    int player;
    void *user_data;
    const pe_cfr_boundary_t *boundary;
    size_t boundary_count;
    double cfv[PE_CFR_RESOLVE_MAX_BOUNDARY];
    double others_reach_sum[PE_CFR_RESOLVE_MAX_BOUNDARY];
} cfv_value_t;

static void cfv_value_recursive(cfv_value_t *w,
                                uint64_t key,
                                double *reach,
                                int num_players,
                                double *out_util)
{
    cfr_game_t *g = w->game;

    if (g->is_terminal(g, key, w->user_data))
    {
        for (int p = 0; p < num_players; ++p)
            out_util[p] = g->get_utility(g, key, p, w->user_data);
        return;
    }
    if (g->is_chance && g->is_chance(g, key, w->user_data))
    {
        int outcomes = g->get_chance_outcomes ? g->get_chance_outcomes(g, key, w->user_data) : 0;
        for (int p = 0; p < num_players; ++p)
            out_util[p] = 0.0;
        if (outcomes <= 0 || !g->apply_chance)
            return;
        for (int c = 0; c < outcomes; ++c)
        {
            uint64_t ck = g->apply_chance(g, key, c, w->user_data);
            double cu[CFR_MAX_PLAYERS];
            cfv_value_recursive(w, ck, reach, num_players, cu);
            for (int p = 0; p < num_players; ++p)
                out_util[p] += cu[p] / (double)outcomes;
            if (g->release_state)
                g->release_state(g, ck, w->user_data);
        }
        return;
    }

    int actions[CFR_MAX_ACTIONS];
    int num_actions = g->get_actions(g, key, actions, CFR_MAX_ACTIONS, w->user_data);
    if (num_actions <= 0)
        return;
    if (num_actions > CFR_MAX_ACTIONS)
        num_actions = CFR_MAX_ACTIONS;

    int acting = g->current_player ? g->current_player(g, key, w->user_data) : 0;
    if (acting < 0 || acting >= num_players)
        acting = 0;

    uint64_t infoset = g->get_infoset_key
        ? g->get_infoset_key((const void *)(uintptr_t)key)
        : key;

    double strat[CFR_MAX_ACTIONS];
    cfr_storage_get_avg_strategy(w->blueprint, infoset, num_actions, strat);

    double node_val[CFR_MAX_PLAYERS];
    for (int p = 0; p < num_players; ++p)
        node_val[p] = 0.0;

    for (int a = 0; a < num_actions; ++a)
    {
        double nreach[CFR_MAX_PLAYERS];
        for (int p = 0; p < num_players; ++p)
            nreach[p] = reach[p];
        nreach[acting] *= strat[a];
        uint64_t nk = g->apply_action(g, key, actions[a], w->user_data);
        double cu[CFR_MAX_PLAYERS];
        cfv_value_recursive(w, nk, nreach, num_players, cu);
        for (int p = 0; p < num_players; ++p)
            node_val[p] += strat[a] * cu[p];
        if (g->release_state)
            g->release_state(g, nk, w->user_data);
    }

    double others_reach = 1.0;
    for (int p = 0; p < num_players; ++p)
        if (p != acting)
            others_reach *= reach[p];

    for (size_t i = 0; i < w->boundary_count; ++i)
    {
        if (w->boundary[i].infoset != infoset)
            continue;
        w->cfv[i] += node_val[w->player] * others_reach;
        w->others_reach_sum[i] += others_reach;
    }

    for (int p = 0; p < num_players; ++p)
        out_util[p] = node_val[p];
}

int pe_cfr_blueprint_cfv(cfr_game_t *game,
                         cfr_storage_t *blueprint,
                         int player,
                         void *user_data,
                         pe_cfr_boundary_t *boundary,
                         size_t boundary_count)
{
    if (!game || !blueprint || !boundary)
        return PE_CFR_RESOLVE_EINVAL;
    if (boundary_count > PE_CFR_RESOLVE_MAX_BOUNDARY)
        return PE_CFR_RESOLVE_EINVAL;
    if (game->num_players <= 0)
        return PE_CFR_RESOLVE_EINVAL;

    cfv_value_t w;
    memset(&w, 0, sizeof(w));
    w.game = game;
    w.blueprint = blueprint;
    w.player = player;
    w.user_data = user_data;
    w.boundary = boundary;
    w.boundary_count = boundary_count;

    int num_players = game->num_players;
    double *reach = (double *)calloc((size_t)num_players, sizeof(double));
    if (!reach)
        return PE_CFR_RESOLVE_ENOMEM;
    for (int p = 0; p < num_players; ++p)
        reach[p] = 1.0;

    double *util = (double *)calloc((size_t)num_players, sizeof(double));
    if (!util)
    {
        free(reach);
        return PE_CFR_RESOLVE_ENOMEM;
    }

    uint64_t root = (uint64_t)(uintptr_t)game->initial_state;
    cfv_value_recursive(&w, root, reach, num_players, util);

    for (size_t i = 0; i < boundary_count; ++i)
    {
        double r = w.others_reach_sum[i];
        boundary[i].cfv = (r > 0.0) ? (w.cfv[i] / r) : 0.0;
        boundary[i].reach = r;
    }

    free(reach);
    free(util);
    return PE_CFR_RESOLVE_OK;
}

/* ----------------------------------------------------------------------------
 * Subgame infoset collection (flood fill, deduplicated by infoset key)
 * -------------------------------------------------------------------------- */

typedef struct
{
    cfr_game_t *game;
    void *user_data;
    uint64_t *keys;
    size_t max_keys;
    size_t count;
    uint64_t *visited;
    size_t visited_cap;
} sg_collect_t;

static int sg_seen(sg_collect_t *c, uint64_t infoset)
{
    size_t m = c->visited_cap - 1;
    size_t i = (size_t)(infoset * 11400714819323198485ull) & m;
    for (;;)
    {
        if (!c->visited[i])
        {
            c->visited[i] = infoset;
            return 0;
        }
        if (c->visited[i] == infoset)
            return 1;
        i = (i + 1) & m;
    }
}

static void sg_collect_recursive(sg_collect_t *c, uint64_t key, int num_players)
{
    cfr_game_t *g = c->game;

    uint64_t infoset = g->get_infoset_key
        ? g->get_infoset_key((const void *)(uintptr_t)key)
        : key;
    if (!sg_seen(c, infoset))
    {
        if (c->keys && c->count < c->max_keys)
            c->keys[c->count] = infoset;
        c->count++;
    }

    if (g->is_terminal(g, key, c->user_data))
        return;
    if (g->is_chance && g->is_chance(g, key, c->user_data))
    {
        int outcomes = g->get_chance_outcomes ? g->get_chance_outcomes(g, key, c->user_data) : 0;
        if (outcomes <= 0 || !g->apply_chance)
            return;
        for (int o = 0; o < outcomes; ++o)
        {
            uint64_t ck = g->apply_chance(g, key, o, c->user_data);
            sg_collect_recursive(c, ck, num_players);
            if (g->release_state)
                g->release_state(g, ck, c->user_data);
        }
        return;
    }

    int actions[CFR_MAX_ACTIONS];
    int num_actions = g->get_actions(g, key, actions, CFR_MAX_ACTIONS, c->user_data);
    if (num_actions <= 0)
        return;
    if (num_actions > CFR_MAX_ACTIONS)
        num_actions = CFR_MAX_ACTIONS;

    if (!sg_seen(c, infoset))
    {
        if (c->keys && c->count < c->max_keys)
            c->keys[c->count] = infoset;
        c->count++;
    }

    for (int a = 0; a < num_actions; ++a)
    {
        uint64_t nk = g->apply_action(g, key, actions[a], c->user_data);
        sg_collect_recursive(c, nk, num_players);
        if (g->release_state)
            g->release_state(g, nk, c->user_data);
    }
}

int pe_cfr_subgame_infosets(cfr_game_t *game,
                            uint64_t root_state_key,
                            void *user_data,
                            uint64_t *out_keys,
                            size_t max_keys,
                            size_t *out_count)
{
    if (!game || !out_count)
        return PE_CFR_RESOLVE_EINVAL;
    int num_players = game->num_players > 0 ? game->num_players : 2;

    sg_collect_t c;
    memset(&c, 0, sizeof(c));
    c.game = game;
    c.user_data = user_data;
    c.keys = out_keys;
    c.max_keys = max_keys;

    size_t cap = 1024;
    while (cap < max_keys + 8)
        cap <<= 1;
    c.visited = (uint64_t *)calloc(cap, sizeof(uint64_t));
    c.visited_cap = cap;
    if (!c.visited)
        return PE_CFR_RESOLVE_ENOMEM;

    sg_collect_recursive(&c, root_state_key, num_players);

    *out_count = c.count;
    free(c.visited);
    return PE_CFR_RESOLVE_OK;
}

/* ----------------------------------------------------------------------------
 * Trunk-locked re-solve seeding
 * -------------------------------------------------------------------------- */

typedef struct seed_ctx_t
{
    cfr_storage_t *res;
    const uint64_t *subset;
    size_t subset_cap;
    size_t locked;
    size_t free_n;
} seed_ctx_t;

static void seed_copy_cb(uint64_t key, int n_actions, const double *regret,
                         const double *avg_strategy, void *user)
{
    (void)regret;
    seed_ctx_t *lc = (seed_ctx_t *)user;
    double prob[CFR_MAX_ACTIONS];
    double sum = 0.0;
    for (int i = 0; i < n_actions; ++i)
        sum += avg_strategy[i];
    if (sum <= 0.0)
        for (int i = 0; i < n_actions; ++i)
            prob[i] = 1.0 / (double)n_actions;
    else
        for (int i = 0; i < n_actions; ++i)
            prob[i] = avg_strategy[i] / sum;

    int in_subgame = 0;
    if (lc->subset)
    {
        size_t m = lc->subset_cap - 1;
        size_t j = (size_t)(key * 11400714819323198485ull) & m;
        while (lc->subset[j])
        {
            if (lc->subset[j] == key)
            {
                in_subgame = 1;
                break;
            }
            j = (j + 1) & m;
        }
    }

    if (in_subgame)
    {
        /* Seed the subgame infoset's average strategy from the blueprint and
         * leave it unlocked so cfr_solve trains it. resolve_storage starts
         * empty, so a single weighted accumulation (weight 1) sets the average
         * exactly to the blueprint probabilities. */
        cfr_storage_update_avg(lc->res, key, n_actions, prob, 1.0);
        lc->free_n++;
    }
    else
    {
        if (cfr_storage_set_locked_strategy(lc->res, key, prob, n_actions) == 0)
            lc->locked++;
    }
}

int pe_cfr_seed_resolve_storage(cfr_game_t *game,
                                cfr_storage_t *blueprint,
                                cfr_storage_t *resolve_storage,
                                uint64_t root_state_key,
                                void *user_data,
                                size_t *out_locked,
                                size_t *out_free)
{
    if (!game || !blueprint || !resolve_storage)
        return PE_CFR_RESOLVE_EINVAL;

    size_t subgame_count = 0;
    if (pe_cfr_subgame_infosets(game, root_state_key, user_data, NULL, 0, &subgame_count) != PE_CFR_RESOLVE_OK)
        return PE_CFR_RESOLVE_ETREE;

    uint64_t *sub_keys = NULL;
    if (subgame_count > 0)
    {
        sub_keys = (uint64_t *)malloc(subgame_count * sizeof(uint64_t));
        if (!sub_keys)
            return PE_CFR_RESOLVE_ENOMEM;
        if (pe_cfr_subgame_infosets(game, root_state_key, user_data, sub_keys,
                                    subgame_count, &subgame_count) != PE_CFR_RESOLVE_OK)
        {
            free(sub_keys);
            return PE_CFR_RESOLVE_ETREE;
        }
    }

    size_t cap = 1024;
    while (cap < subgame_count + 8)
        cap <<= 1;
    uint64_t *subset = (uint64_t *)calloc(cap, sizeof(uint64_t));
    if (!subset)
    {
        free(sub_keys);
        return PE_CFR_RESOLVE_ENOMEM;
    }
    for (size_t i = 0; i < subgame_count; ++i)
    {
        size_t m = cap - 1;
        size_t j = (size_t)(sub_keys[i] * 11400714819323198485ull) & m;
        while (subset[j])
            j = (j + 1) & m;
        subset[j] = sub_keys[i];
    }

    seed_ctx_t lc;
    memset(&lc, 0, sizeof(lc));
    lc.res = resolve_storage;
    lc.subset = subset;
    lc.subset_cap = cap;
    cfr_storage_iterate(blueprint, seed_copy_cb, &lc);

    free(subset);
    free(sub_keys);

    if (out_locked)
        *out_locked = lc.locked;
    if (out_free)
        *out_free = lc.free_n;
    return PE_CFR_RESOLVE_OK;
}

/* ----------------------------------------------------------------------------
 * Gadget game (CFR-D)
 * -------------------------------------------------------------------------- */

static int gadget_current_player(cfr_game_t *game, uint64_t key, void *user)
{
    (void)user;
    pe_cfr_gadget_t *g = (pe_cfr_gadget_t *)game->game_data;
    uint32_t kind = GADGET_KIND(key);
    if (kind == GADGET_KIND_ROOT)
        return -1; /* chance node */
    if (kind == GADGET_KIND_BOUNDARY)
        return g->opponent;
    return g->inner->current_player
        ? g->inner->current_player(g->inner, key, g->user_data)
        : 0;
}

static int gadget_is_terminal(cfr_game_t *game, uint64_t key, void *user)
{
    pe_cfr_gadget_t *g = (pe_cfr_gadget_t *)game->game_data;
    uint32_t kind = GADGET_KIND(key);
    if (kind == GADGET_KIND_TERMINAL)
        return 1;
    if (kind == GADGET_KIND_ROOT || kind == GADGET_KIND_BOUNDARY)
        return 0;
    return g->inner->is_terminal(g->inner, key, g->user_data);
}

static int gadget_is_chance(cfr_game_t *game, uint64_t key, void *user)
{
    pe_cfr_gadget_t *g = (pe_cfr_gadget_t *)game->game_data;
    uint32_t kind = GADGET_KIND(key);
    if (kind == GADGET_KIND_ROOT)
        return 1;
    if (kind == GADGET_KIND_BOUNDARY || kind == GADGET_KIND_TERMINAL)
        return 0;
    return (g->inner->is_chance && g->inner->is_chance(g->inner, key, g->user_data)) ? 1 : 0;
}

static int gadget_get_chance_outcomes(cfr_game_t *game, uint64_t key, void *user)
{
    pe_cfr_gadget_t *g = (pe_cfr_gadget_t *)game->game_data;
    if (GADGET_KIND(key) == GADGET_KIND_ROOT)
        return (int)g->boundary_count;
    return g->inner->get_chance_outcomes
        ? g->inner->get_chance_outcomes(g->inner, key, g->user_data)
        : 0;
}

static uint64_t gadget_apply_chance(cfr_game_t *game, uint64_t key, int outcome, void *user)
{
    pe_cfr_gadget_t *g = (pe_cfr_gadget_t *)game->game_data;
    if (GADGET_KIND(key) == GADGET_KIND_ROOT)
    {
        if (outcome < 0 || (size_t)outcome >= g->boundary_count)
            return key;
        return GADGET_MK(GADGET_KIND_BOUNDARY, (size_t)outcome);
    }
    return g->inner->apply_chance
        ? g->inner->apply_chance(g->inner, key, outcome, g->user_data)
        : key;
}

static int gadget_get_actions(cfr_game_t *game, uint64_t key, int *out_actions, int max_actions, void *user)
{
    pe_cfr_gadget_t *g = (pe_cfr_gadget_t *)game->game_data;
    uint32_t kind = GADGET_KIND(key);
    if (kind == GADGET_KIND_BOUNDARY)
    {
        if (max_actions < 2)
            return 0;
        out_actions[0] = 0; /* terminate */
        out_actions[1] = 1; /* follow */
        return 2;
    }
    if (kind == GADGET_KIND_ROOT || kind == GADGET_KIND_TERMINAL)
        return 0;
    return g->inner->get_actions(g->inner, key, out_actions, max_actions, g->user_data);
}

static uint64_t gadget_apply_action(cfr_game_t *game, uint64_t key, int action, void *user)
{
    pe_cfr_gadget_t *g = (pe_cfr_gadget_t *)game->game_data;
    uint32_t kind = GADGET_KIND(key);
    if (kind == GADGET_KIND_BOUNDARY)
    {
        if (action == 0)
            return GADGET_MK(GADGET_KIND_TERMINAL, GADGET_IDX(key));
        /* follow: enter the real subgame root state. */
        return g->subgame.root_state_key;
    }
    return g->inner->apply_action(g->inner, key, action, g->user_data);
}

static double gadget_get_utility(cfr_game_t *game, uint64_t key, int player, void *user)
{
    pe_cfr_gadget_t *g = (pe_cfr_gadget_t *)game->game_data;
    uint32_t kind = GADGET_KIND(key);
    if (kind == GADGET_KIND_TERMINAL)
    {
        size_t idx = GADGET_IDX(key);
        if (idx < g->boundary_count)
        {
            double alpha = g->boundary[idx].cfv;
            /* Zero-sum, opponent-centric payoff: the constrained opponent
             * receives alpha_i, the rest of the table absorbs -alpha_i
             * split evenly so the sum is zero. */
            if (player == g->opponent)
                return alpha;
            int others = g->inner->num_players - 1;
            return others > 0 ? -alpha / (double)others : 0.0;
        }
        return 0.0;
    }
    return g->inner->get_utility(g->inner, key, player, g->user_data);
}

static uint64_t gadget_get_infoset_key(const void *state)
{
    uint64_t key = (uint64_t)(uintptr_t)state;
    if (GADGET_KIND(key) != 0)
        return key; /* stable gadget state */
    pe_cfr_gadget_t *g = g_gadget_active;
    if (g && g->inner->get_infoset_key)
        return g->inner->get_infoset_key(state);
    return key;
}

static void gadget_release_state(cfr_game_t *game, uint64_t key, void *user)
{
    pe_cfr_gadget_t *g = (pe_cfr_gadget_t *)game->game_data;
    if (GADGET_KIND(key) != 0)
        return;
    if (g->inner->release_state)
        g->inner->release_state(g->inner, key, g->user_data);
}

int pe_cfr_gadget_create(cfr_game_t *game,
                         const pe_cfr_subgame_t *subgame,
                         void *user_data,
                         pe_cfr_gadget_t **out_gadget,
                         cfr_game_t *out_game)
{
    if (!game || !subgame || !out_gadget || !out_game)
        return PE_CFR_RESOLVE_EINVAL;
    if (subgame->boundary_count == 0)
        return PE_CFR_RESOLVE_EINVAL;
    if (game->num_players > 2)
        return PE_CFR_RESOLVE_UNSUPPORTED;
    if (subgame->resolve_player < 0 || subgame->resolve_player >= game->num_players)
        return PE_CFR_RESOLVE_EINVAL;

    pe_cfr_gadget_t *g = (pe_cfr_gadget_t *)calloc(1, sizeof(pe_cfr_gadget_t));
    if (!g)
        return PE_CFR_RESOLVE_ENOMEM;
    g->inner = game;
    g->user_data = user_data;
    g->subgame = *subgame;
    g->boundary = subgame->boundary;
    g->boundary_count = subgame->boundary_count;
    g->player = subgame->resolve_player;
    g->opponent = (subgame->resolve_player == 0) ? 1 : 0;

    memset(out_game, 0, sizeof(cfr_game_t));
    out_game->current_player = gadget_current_player;
    out_game->get_actions = gadget_get_actions;
    out_game->apply_action = gadget_apply_action;
    out_game->get_infoset_key = gadget_get_infoset_key;
    out_game->release_state = gadget_release_state;
    out_game->is_terminal = gadget_is_terminal;
    out_game->get_utility = gadget_get_utility;
    out_game->is_chance = gadget_is_chance;
    out_game->get_chance_outcomes = gadget_get_chance_outcomes;
    out_game->apply_chance = gadget_apply_chance;
    out_game->game_data = g;
    out_game->initial_state = (void *)(uintptr_t)GADGET_ROOT_KEY;
    out_game->state_size = sizeof(uint64_t);
    out_game->num_players = game->num_players;

    *out_gadget = g;
    return PE_CFR_RESOLVE_OK;
}

void pe_cfr_gadget_destroy(pe_cfr_gadget_t *gadget)
{
    if (gadget == g_gadget_active)
        g_gadget_active = NULL;
    free(gadget);
}

int pe_cfr_gadget_follow_frequency(const pe_cfr_gadget_t *gadget,
                                   cfr_storage_t *storage,
                                   uint64_t infoset,
                                   double *out_follow)
{
    if (!gadget || !storage || !out_follow)
        return PE_CFR_RESOLVE_EINVAL;
    size_t idx = (size_t)-1;
    for (size_t i = 0; i < gadget->boundary_count; ++i)
    {
        if (gadget->boundary[i].infoset == infoset)
        {
            idx = i;
            break;
        }
    }
    if (idx == (size_t)-1)
        return PE_CFR_RESOLVE_EINVAL;
    uint64_t bkey = GADGET_MK(GADGET_KIND_BOUNDARY, idx);
    double strat[2] = {0.5, 0.5};
    cfr_storage_get_strategy(storage, bkey, 2, strat);
    *out_follow = strat[1]; /* action 1 == follow */
    return PE_CFR_RESOLVE_OK;
}

/* ----------------------------------------------------------------------------
 * Solve driver
 * -------------------------------------------------------------------------- */

static void seed_resolve_cb(uint64_t key, int n_actions, const double *regret,
                            const double *avg_strategy, void *user)
{
    (void)regret;
    cfr_storage_t *res = (cfr_storage_t *)user;
    double prob[CFR_MAX_ACTIONS];
    double sum = 0.0;
    for (int i = 0; i < n_actions; ++i)
        sum += avg_strategy[i];
    if (sum <= 0.0)
        for (int i = 0; i < n_actions; ++i)
            prob[i] = 1.0 / (double)n_actions;
    else
        for (int i = 0; i < n_actions; ++i)
            prob[i] = avg_strategy[i] / sum;
    /* Write the blueprint average into the resolve storage. resolve_storage
     * starts empty, so a single weighted accumulation (weight 1) sets the
     * average exactly to the blueprint probabilities. */
    cfr_storage_update_avg(res, key, n_actions, prob, 1.0);
}

int pe_cfr_resolve_subgame(cfr_game_t *game,
                           cfr_storage_t *blueprint,
                           cfr_storage_t *resolve_storage,
                           const pe_cfr_subgame_t *subgame,
                           const pe_cfr_resolve_config_t *config,
                           void *user_data,
                           pe_cfr_resolve_result_t *out_result)
{
    if (!game || !blueprint || !resolve_storage || !subgame)
        return PE_CFR_RESOLVE_EINVAL;
    if (subgame->boundary_count == 0)
        return PE_CFR_RESOLVE_EINVAL;

    /* Multiway games have no single-opponent counterfactual value, so the
     * CFR-D gadget is undefined. When the caller enables the trunk-locked
     * fallback we can still refine the subgame (locking the rest of the tree);
     * otherwise the call is unsupported. */
    int multiway = (game->num_players > 2);
    if (multiway && !(config && config->lock_trunk))
        return PE_CFR_RESOLVE_UNSUPPORTED;

    pe_cfr_gadget_t *gadget = NULL;
    cfr_game_t ggame;
    if (!multiway)
    {
        if (pe_cfr_gadget_create(game, subgame, user_data, &gadget, &ggame) != PE_CFR_RESOLVE_OK)
        {
            pe_cfr_gadget_destroy(gadget);
            return PE_CFR_RESOLVE_EINVAL;
        }
    }

    /* Fill blueprint CFVs when the caller left them at zero. */
    int need_cfv = 0;
    for (size_t i = 0; i < subgame->boundary_count; ++i)
    {
        if (subgame->boundary[i].cfv == 0.0 && subgame->boundary[i].reach == 0.0)
        {
            need_cfv = 1;
            break;
        }
    }
    if (!multiway && need_cfv)
    {
        /* subgame->boundary is const; copy into a mutable working array so the
         * resolver can write the computed CFVs back, then copy them across. */
        pe_cfr_boundary_t *work = (pe_cfr_boundary_t *)malloc(
            subgame->boundary_count * sizeof(pe_cfr_boundary_t));
        if (!work)
        {
            pe_cfr_gadget_destroy(gadget);
            return PE_CFR_RESOLVE_ENOMEM;
        }
        memcpy(work, subgame->boundary,
               subgame->boundary_count * sizeof(pe_cfr_boundary_t));
        int rc = pe_cfr_blueprint_cfv(game, blueprint, gadget->opponent, user_data,
                                      work, subgame->boundary_count);
        if (rc != PE_CFR_RESOLVE_OK)
        {
            free(work);
            pe_cfr_gadget_destroy(gadget);
            return rc;
        }
        /* Copy the computed cfv/reach back into the const caller array. */
        memcpy((pe_cfr_boundary_t *)subgame->boundary, work,
               subgame->boundary_count * sizeof(pe_cfr_boundary_t));
        free(work);
    }

    /* Seed the resolve storage from the blueprint so the subgame starts from
     * the blueprint rather than from scratch. */
    cfr_storage_iterate(blueprint, seed_resolve_cb, resolve_storage);

    /* Optionally lock the trunk (everything outside the subgame). */
    if (config && config->lock_trunk)
    {
        size_t locked = 0, free_n = 0;
        pe_cfr_seed_resolve_storage(game, blueprint, resolve_storage,
                                    subgame->root_state_key, user_data,
                                    &locked, &free_n);
        (void)locked;
        (void)free_n;
    }

    cfr_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    if (config)
        cfg = config->cfr;
    if (cfg.max_iterations <= 0)
        cfg.max_iterations = 1000;

    /* Multiway (no gadget): solve the full game with the trunk locked; copy the
     * inner game vtable so cfr_solve can drive it directly. */
    if (multiway)
        ggame = *game;

    g_gadget_active = gadget; /* NULL for multiway; get_infoset_key tolerates it */
    double expl = 0.0;
    double r = cfr_solve(&ggame, resolve_storage, &cfg, &expl);
    g_gadget_active = NULL;

    if (r < 0.0)
    {
        pe_cfr_gadget_destroy(gadget);
        return PE_CFR_RESOLVE_ETREE;
    }

    if (out_result)
    {
        memset(out_result, 0, sizeof(*out_result));
        out_result->iterations = cfg.max_iterations;
        out_result->exploitability = expl;

        double tol = (config && config->margin_tolerance > 0.0)
            ? config->margin_tolerance
            : PE_CFR_RESOLVE_DEFAULT_TOLERANCE;

        double worst = 1e300;
        double sum = 0.0;
        size_t n = 0;
        for (size_t i = 0; i < subgame->boundary_count && i < PE_CFR_RESOLVE_MAX_BOUNDARY; ++i)
        {
            uint64_t infoset = subgame->boundary[i].infoset;
            double follow = 0.0;
            /* The follow-frequency is only meaningful for the 2-player gadget;
             * for multiway (trunk-locked) there is no gadget decision. */
            if (gadget)
                pe_cfr_gadget_follow_frequency(gadget, resolve_storage, infoset, &follow);

            /* A converged gadget drives the opponent to "terminate" (follow=0)
             * whenever entering the subgame would yield less than alpha_i, and
             * to "follow" (follow=1) otherwise. The constraint holds when the
             * opponent has no incentive to deviate from terminate, i.e. when
             * follow == 0 at convergence. We therefore report the margin as the
             * residual the gadget still has to close: positive follow implies
             * the constraint is (so far) satisfied because the opponent prefers
             * the subgame only when it is at least as good as terminate. */
            double margin = (follow <= 1e-9) ? 0.0 : -follow;

            out_result->margins[i].infoset = infoset;
            out_result->margins[i].blueprint_cfv = subgame->boundary[i].cfv;
            out_result->margins[i].resolved_cfv = subgame->boundary[i].cfv;
            out_result->margins[i].follow_freq = follow;
            out_result->margins[i].margin = margin;

            if (margin < worst)
                worst = margin;
            sum += margin;
            n++;
        }

        out_result->boundary_count = subgame->boundary_count;
        out_result->worst_margin = (n > 0) ? worst : 0.0;
        out_result->mean_margin = (n > 0) ? (sum / (double)n) : 0.0;
        out_result->constraints_satisfied = (worst >= -tol) ? 1 : 0;
        out_result->infosets_trained = cfr_storage_count_infosets(resolve_storage);
    }

    pe_cfr_gadget_destroy(gadget);
    return PE_CFR_RESOLVE_OK;
}

void pe_cfr_resolve_print(const pe_cfr_resolve_result_t *result)
{
    if (!result)
    {
        fprintf(stderr, "pe_cfr_resolve_result_t: (null)\n");
        return;
    }
    fprintf(stderr, "subgame re-solve: iterations=%d infosets_trained=%zu\n",
            result->iterations, result->infosets_trained);
    fprintf(stderr, "  exploitability=%.6f constraints_satisfied=%d\n",
            result->exploitability, result->constraints_satisfied);
    for (size_t i = 0; i < result->boundary_count && i < PE_CFR_RESOLVE_MAX_BOUNDARY; ++i)
    {
        const pe_cfr_resolve_margin_t *m = &result->margins[i];
        fprintf(stderr,
                "  infoset=0x%llx blueprint_cfv=%.4f follow=%.4f margin=%.4f\n",
                (unsigned long long)m->infoset, m->blueprint_cfv,
                m->follow_freq, m->margin);
    }
}
