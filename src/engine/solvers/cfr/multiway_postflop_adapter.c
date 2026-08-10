#include <poker_eval/engine/solvers/cfr/multiway_postflop_adapter.h>
#include <poker_eval/engine/solvers/cfr/mpf_tree.h>
#include <poker_eval/deck/deck_std.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>

#define MPF_EPS 1e-9

static int mpf_adapter_debug_enabled(void)
{
    static int cached = -1;
    if (cached == -1)
    {
        const char *env = getenv("POKER_DEBUG_MPF");
        cached = (env && *env) ? 1 : 0;
    }
    return cached;
}

#define MPF_ADAPTER_DEBUG(...)                       \
    do                                               \
    {                                                \
        if (mpf_adapter_debug_enabled())             \
            fprintf(stderr, __VA_ARGS__);            \
    } while (0)

#if defined(_MSC_VER)
#define MPF_PERF_INC_STATS(stats_ptr, field)     \
    do                                           \
    {                                            \
        if ((stats_ptr))                         \
            (stats_ptr)->field++;                \
    } while (0)
#else
#define MPF_PERF_INC_STATS(stats_ptr, field)                             \
    do                                                                  \
    {                                                                   \
        if ((stats_ptr))                                                \
            __atomic_fetch_add(&(stats_ptr)->field, 1ULL, __ATOMIC_RELAXED); \
    } while (0)
#endif

#define MPF_PERF_INC_STATE(state_ptr, field)        \
    MPF_PERF_INC_STATS(((state_ptr) ? (state_ptr)->perf_stats : NULL), field)

struct mpf_perf_stats_pool_t
{
#if !defined(_WIN32)
    pthread_mutex_t lock;
    pthread_t *owners;
    mpf_perf_stats_t *shards;
    int capacity;
    int count;
#else
    mpf_perf_stats_t shard;
#endif
};

void mpf_perf_stats_reset(mpf_perf_stats_t *stats)
{
    if (stats)
    {
        memset(stats, 0, sizeof(*stats));
    }
}

struct mpf_perf_stats_pool_t *mpf_perf_stats_pool_create(int max_threads_hint)
{
    int cap = (max_threads_hint > 0) ? max_threads_hint : 8;
    struct mpf_perf_stats_pool_t *pool = (struct mpf_perf_stats_pool_t *)calloc(1, sizeof(*pool));
    if (!pool)
        return NULL;
#if !defined(_WIN32)
    if (pthread_mutex_init(&pool->lock, NULL) != 0)
    {
        free(pool);
        return NULL;
    }
    pool->capacity = (cap > 0) ? cap : 8;
    pool->owners = NULL;
    pool->shards = NULL;
    if (pool->capacity > 0)
    {
        pool->owners = (pthread_t *)calloc((size_t)pool->capacity, sizeof(pthread_t));
        pool->shards = (mpf_perf_stats_t *)calloc((size_t)pool->capacity, sizeof(mpf_perf_stats_t));
        if (!pool->owners || !pool->shards)
        {
            free(pool->owners);
            free(pool->shards);
            pthread_mutex_destroy(&pool->lock);
            free(pool);
            return NULL;
        }
    }
    pool->count = 0;
#else
    (void)cap;
    memset(&pool->shard, 0, sizeof(pool->shard));
#endif
    return pool;
}

void mpf_perf_stats_pool_destroy(struct mpf_perf_stats_pool_t *pool)
{
    if (!pool)
        return;
#if !defined(_WIN32)
    pthread_mutex_destroy(&pool->lock);
    free(pool->owners);
    free(pool->shards);
#endif
    free(pool);
}

void mpf_perf_stats_pool_reset(struct mpf_perf_stats_pool_t *pool)
{
    if (!pool)
        return;
#if !defined(_WIN32)
    pthread_mutex_lock(&pool->lock);
    pool->count = 0;
    if (pool->owners)
        memset(pool->owners, 0, sizeof(pthread_t) * (size_t)pool->capacity);
    if (pool->shards)
        memset(pool->shards, 0, sizeof(mpf_perf_stats_t) * (size_t)pool->capacity);
    pthread_mutex_unlock(&pool->lock);
#else
    memset(&pool->shard, 0, sizeof(pool->shard));
#endif
}

static mpf_perf_stats_t *mpf_perf_stats_pool_acquire_locked(struct mpf_perf_stats_pool_t *pool)
{
#if !defined(_WIN32)
    pthread_t self = pthread_self();
    for (int i = 0; i < pool->count; ++i)
    {
        if (pthread_equal(pool->owners[i], self))
            return &pool->shards[i];
    }
    if (pool->count >= pool->capacity)
    {
        int old_cap = pool->capacity;
        int new_cap = (old_cap > 0) ? (old_cap * 2) : 8;
        pthread_t *owners = (pthread_t *)realloc(pool->owners, (size_t)new_cap * sizeof(pthread_t));
        mpf_perf_stats_t *shards = (mpf_perf_stats_t *)realloc(pool->shards, (size_t)new_cap * sizeof(mpf_perf_stats_t));
        if (!owners || !shards)
        {
            if (owners)
                pool->owners = owners;
            if (shards)
                pool->shards = shards;
            return NULL;
        }
        memset(owners + old_cap, 0, (size_t)(new_cap - old_cap) * sizeof(pthread_t));
        memset(shards + old_cap, 0, (size_t)(new_cap - old_cap) * sizeof(mpf_perf_stats_t));
        pool->owners = owners;
        pool->shards = shards;
        pool->capacity = new_cap;
    }
    int idx = pool->count++;
    pool->owners[idx] = self;
    memset(&pool->shards[idx], 0, sizeof(mpf_perf_stats_t));
    return &pool->shards[idx];
#else
    (void)pool;
    return NULL;
#endif
}

mpf_perf_stats_t *mpf_perf_stats_pool_acquire(struct mpf_perf_stats_pool_t *pool)
{
    if (!pool)
        return NULL;
#if !defined(_WIN32)
    mpf_perf_stats_t *stats = NULL;
    pthread_mutex_lock(&pool->lock);
    stats = mpf_perf_stats_pool_acquire_locked(pool);
    pthread_mutex_unlock(&pool->lock);
    return stats;
#else
    return &pool->shard;
#endif
}

void mpf_perf_stats_pool_collect(struct mpf_perf_stats_pool_t *pool, mpf_perf_stats_t *out_total)
{
    if (!pool || !out_total)
        return;
    memset(out_total, 0, sizeof(*out_total));
#if !defined(_WIN32)
    pthread_mutex_lock(&pool->lock);
    for (int i = 0; i < pool->count; ++i)
    {
        const mpf_perf_stats_t *src = &pool->shards[i];
        out_total->apply_action_calls += src->apply_action_calls;
        out_total->state_clone_ops += src->state_clone_ops;
        out_total->state_heap_allocs += src->state_heap_allocs;
        out_total->state_cache_hits += src->state_cache_hits;
        out_total->state_cache_misses += src->state_cache_misses;
        out_total->street_transitions += src->street_transitions;
        out_total->round_resets += src->round_resets;
        out_total->board_updates += src->board_updates;
        out_total->utility_computations += src->utility_computations;
        out_total->tree_snapshot_applies += src->tree_snapshot_applies;
    }
    pthread_mutex_unlock(&pool->lock);
#else
    *out_total = pool->shard;
#endif
}

static int mpf_action_cache_index(int action)
{
    if (action == MPF_ACTION_FOLD)
        return 0;
    if (action == MPF_ACTION_CALL)
        return 1;
    if (action >= MPF_ACTION_RAISE_BASE)
    {
        int idx = action - MPF_ACTION_RAISE_BASE;
        if (idx >= 0 && idx < (MPF_TREE_ACTION_MAX - 2))
            return 2 + idx;
    }
    return -1;
}

static void mpf_state_cleanup_internal(mpf_state_t *state);
static void mpf_tree_release_cache(mpf_tree_def_t *tree, const mpf_state_t *root);

static int mpf_is_terminal(const mpf_state_t *st);
static void mpf_compute_utilities(mpf_state_t *st);
static int mpf_round_complete(const mpf_state_t *st);
static void mpf_advance_street(mpf_state_t *st);
static int mpf_next_active(const mpf_state_t *st, int idx);
static int mpf_first_player_after(const mpf_state_t *st, int idx);
static int mpf_active_count(const mpf_state_t *st);
static void mpf_reset_round(mpf_state_t *st, mpf_street_t new_street);
static void mpf_mark_winner_fold(mpf_state_t *st, int winner);
static void mpf_update_board(mpf_state_t *st, int revealed);
static void mpf_init_round_flags(mpf_state_t *st);
static void mpf_apply_preconfig(const mpf_config_t *cfg, mpf_state_t *st);
static void mpf_restore_base_bets(mpf_state_t *st);
static void mpf_apply_tree_node(mpf_state_t *st, int node_idx);
static int mpf_tree_find_next(const mpf_state_t *st, int action);
static int mpf_tree_collect_actions(const mpf_state_t *st, int *out_actions, int max_actions);

static int mpf_current_player_wrapper(cfr_game_t *game, uint64_t key, void *user)
{
    const mpf_state_t *st = (const mpf_state_t *)(uintptr_t)key;
    return st->to_act;
}

static int mpf_is_terminal_wrapper(cfr_game_t *game, uint64_t key, void *user)
{
    const mpf_state_t *st = (const mpf_state_t *)(uintptr_t)key;
    return mpf_is_terminal(st);
}

static double mpf_get_utility_wrapper(cfr_game_t *game, uint64_t key, int player, void *user)
{
    mpf_state_t *st = (mpf_state_t *)(uintptr_t)key;
    if (!st->util_ready)
        mpf_compute_utilities(st);
    if (player < 0 || player >= st->num_players)
        return 0.0;
    return st->utilities[player];
}

static int mpf_get_actions_wrapper(cfr_game_t *game, uint64_t key, int *out_actions, int max_actions, void *user)
{
    const mpf_state_t *st = (const mpf_state_t *)(uintptr_t)key;
    if (mpf_is_terminal(st))
        return 0;
    int player = st->to_act;
    if (player < 0 || player >= st->num_players)
        return 0;
    if (!st->active[player])
        return 0;
    if (st->stacks[player] <= MPF_EPS)
        return 0;

    if (st->tree_enabled && st->tree && st->tree_node_idx >= 0)
    {
        int used = mpf_tree_collect_actions(st, out_actions, max_actions);
        if (used > 0)
            return used;
    }

    int count = 0;
    double need = st->to_call - st->round_contrib[player];
    if (need < MPF_EPS)
        need = 0.0;

    if (need > MPF_EPS)
    {
        if (count < max_actions)
            out_actions[count] = MPF_ACTION_FOLD;
        count++;
    }

    if (count < max_actions)
        out_actions[count] = MPF_ACTION_CALL;
    count++;

    if (st->bet_size_count > 0 && st->raises_made < st->raise_cap && st->stacks[player] > need + MPF_EPS)
    {
        for (int i = 0; i < st->bet_size_count; ++i)
        {
            if (count >= max_actions)
                break;
            out_actions[count++] = MPF_ACTION_RAISE_BASE + i;
        }
    }
    return count;
}

static int mask_to_array(mask_t mask, int *out, int limit)
{
    int count = 0;
    for (int c = 0; c < 52 && count < limit; ++c)
        if (mask_is_set(mask, c))
            out[count++] = c;
    return count;
}

static eval_t eval_holdem_high(const EvalContext *ctx, mask_t hole, mask_t board)
{
    return pe_eval_7c(ctx, hole | board);
}

static eval_t eval_omaha_high(const EvalContext *ctx, mask_t hole, mask_t board, int hole_count)
{
    int hole_cards[6];
    int board_cards[5];
    int hcount = mask_to_array(hole, hole_cards, 6);
    int bcount = mask_to_array(board, board_cards, 5);
    if (hcount < 2 || bcount < 3)
        return 0;

    int use_hole = 2;
    int use_board = 5 - use_hole;
    if (use_board > bcount)
        use_board = bcount;

    eval_t best = 0;
    for (int i = 0; i < hcount; ++i)
        for (int j = i + 1; j < hcount; ++j)
        {
            for (int a = 0; a < bcount; ++a)
                for (int b = a + 1; b < bcount; ++b)
                    for (int c = b + 1; c < bcount; ++c)
                    {
                        mask_t seven = MASK_EMPTY;
                        seven = mask_set(seven, hole_cards[i]);
                        seven = mask_set(seven, hole_cards[j]);
                        seven = mask_set(seven, board_cards[a]);
                        seven = mask_set(seven, board_cards[b]);
                        seven = mask_set(seven, board_cards[c]);
                        eval_t v = pe_eval_7c(ctx, seven);
                        if (v > best)
                            best = v;
                    }
        }
    return best;
}

static void mpf_apply_action_internal(const mpf_state_t *st, int action, mpf_state_t *out)
{
    mpf_state_t *saved_cache[MPF_TREE_ACTION_MAX];
    memcpy(saved_cache, out->action_cache, sizeof(saved_cache));
    int heap_owned = out->heap_owned;
    *out = *st;
    out->perf_stats = st->perf_stats;
    memcpy(out->action_cache, saved_cache, sizeof(saved_cache));
    out->heap_owned = heap_owned;
    out->util_ready = 0;
    MPF_PERF_INC_STATE(st, state_clone_ops);

    int next_tree_idx = mpf_tree_find_next(st, action);

    int player = st->to_act;
    if (player < 0 || player >= st->num_players)
        return;
    if (!st->active[player] || st->stacks[player] <= MPF_EPS)
        return;

    double need = st->to_call - st->round_contrib[player];
    if (need < MPF_EPS)
        need = 0.0;

    if (action == MPF_ACTION_FOLD)
    {
        out->active[player] = 0;
        out->acted_this_round[player] = 1;
        if (mpf_active_count(out) <= 1)
        {
            int winner = -1;
            for (int i = 0; i < out->num_players; ++i)
                if (out->active[i])
                    winner = i;
            mpf_mark_winner_fold(out, winner);
            return;
        }
    }
    else if (action == MPF_ACTION_CALL)
    {
        double pay = need;
        if (pay > out->stacks[player])
            pay = out->stacks[player];
        if (pay > 0.0)
        {
            out->stacks[player] -= pay;
            out->round_contrib[player] += pay;
            out->invested[player] += pay;
            out->pot += pay;
        }
        out->acted_this_round[player] = 1;
    }
    else
    {
        int raise_idx = action - MPF_ACTION_RAISE_BASE;
        if (raise_idx >= 0 && raise_idx < out->bet_size_count && out->raises_made < out->raise_cap)
        {
            double raise_amount = out->bet_sizes[raise_idx];
            if (out->enable_pot_sizing)
                raise_amount *= (out->pot > MPF_EPS ? out->pot : (out->current_bet + out->to_call));
            if (raise_amount < 0.0)
                raise_amount = 0.0;
            double pay = need + raise_amount;
            if (pay > out->stacks[player])
                pay = out->stacks[player];
            if (pay < need + MPF_EPS)
                pay = need;
            if (pay > 0.0)
            {
                out->stacks[player] -= pay;
                out->round_contrib[player] += pay;
                out->invested[player] += pay;
                out->pot += pay;
            }
            if (out->round_contrib[player] > out->to_call + MPF_EPS)
            {
                out->to_call = out->round_contrib[player];
                out->current_bet = out->round_contrib[player];
                out->raises_made += 1;
                for (int i = 0; i < out->num_players; ++i)
                    if (out->active[i] && i != player)
                        out->acted_this_round[i] = 0;
            }
            out->acted_this_round[player] = 1;
        }
    }

    if (out->util_ready)
        return;

    if (out->tree_enabled && out->tree)
    {
        if (next_tree_idx >= 0)
        {
            out->tree_node_idx = next_tree_idx;
            mpf_apply_tree_node(out, next_tree_idx);
        }
        else
        {
            out->tree_enabled = 0;
            out->tree_node_idx = -1;
            mpf_restore_base_bets(out);
        }
    }

    int next = mpf_next_active(out, player + 1);
    out->to_act = next;

    if (mpf_round_complete(out))
    {
        if (out->street == MPF_STREET_RIVER)
        {
            out->street = MPF_STREET_SHOWDOWN;
            mpf_compute_utilities(out);
            return;
        }
        else
        {
            mpf_advance_street(out);
        }
    }
}

static uint64_t mpf_apply_action_wrapper(cfr_game_t *game, uint64_t key, int action, void *user)
{
    mpf_state_t *st = (mpf_state_t *)(uintptr_t)key;
    mpf_perf_stats_t *perf = st ? st->perf_stats : NULL;
    MPF_PERF_INC_STATS(perf, apply_action_calls);
    int next_tree_idx = -1;
    if (st && st->tree_enabled && st->tree && st->tree_node_idx >= 0)
        next_tree_idx = mpf_tree_find_next(st, action);

    mpf_state_t *next_state = NULL;
    if (next_tree_idx >= 0)
    {
        mpf_tree_node_t *node = &st->tree->nodes[next_tree_idx];
#if !defined(_WIN32)
        pthread_mutex_lock(&node->cache_lock);
        pthread_t self = pthread_self();
        mpf_state_t *cached = NULL;
        int free_slot = -1;
        for (int s = 0; s < MPF_NODE_CACHE_SLOTS; ++s)
        {
            if (node->cache_slots[s].owner == self)
            {
                cached = node->cache_slots[s].state;
                break;
            }
            if (free_slot < 0 && node->cache_slots[s].state == NULL)
                free_slot = s;
        }
        if (!cached)
        {
            if (free_slot < 0)
            {
                free_slot = 0;
                if (node->cache_slots[free_slot].state &&
                    node->cache_slots[free_slot].state != st)
                {
                    mpf_state_cleanup_internal(node->cache_slots[free_slot].state);
                    free(node->cache_slots[free_slot].state);
                    node->cache_slots[free_slot].state = NULL;
                }
            }
            cached = (mpf_state_t *)malloc(sizeof(mpf_state_t));
            if (!cached)
            {
                pthread_mutex_unlock(&node->cache_lock);
                return 0;
            }
            memset(cached, 0, sizeof(*cached));
            cached->perf_stats = perf;
            cached->heap_owned = 1;
            node->cache_slots[free_slot].owner = self;
            node->cache_slots[free_slot].state = cached;
            MPF_PERF_INC_STATS(perf, state_heap_allocs);
            MPF_PERF_INC_STATS(perf, state_cache_misses);
        }
        else
        {
            MPF_PERF_INC_STATS(perf, state_cache_hits);
        }
        next_state = cached;
        pthread_mutex_unlock(&node->cache_lock);
#else
        if (!node->state_cache)
        {
            node->state_cache = (mpf_state_t *)malloc(sizeof(mpf_state_t));
            if (!node->state_cache)
                return 0;
            memset(node->state_cache, 0, sizeof(*node->state_cache));
            node->state_cache->perf_stats = perf;
            node->state_cache->heap_owned = 1;
            MPF_PERF_INC_STATS(perf, state_heap_allocs);
            MPF_PERF_INC_STATS(perf, state_cache_misses);
        }
        else
        {
            MPF_PERF_INC_STATS(perf, state_cache_hits);
        }
        next_state = node->state_cache;
#endif
    }
    else
    {
        int cache_idx = mpf_action_cache_index(action);
        if (cache_idx >= 0)
        {
            mpf_state_t *cached = st->action_cache[cache_idx];
            if (!cached)
            {
                cached = (mpf_state_t *)malloc(sizeof(mpf_state_t));
            if (!cached)
                return 0;
            memset(cached, 0, sizeof(*cached));
            cached->perf_stats = st->perf_stats;
            cached->heap_owned = 1;
            st->action_cache[cache_idx] = cached;
            MPF_PERF_INC_STATS(perf, state_heap_allocs);
            MPF_PERF_INC_STATS(perf, state_cache_misses);
            }
            else
            {
                MPF_PERF_INC_STATS(perf, state_cache_hits);
            }
            next_state = cached;
        }
        else
        {
            next_state = (mpf_state_t *)malloc(sizeof(mpf_state_t));
            if (!next_state)
                return 0;
            memset(next_state, 0, sizeof(*next_state));
            next_state->perf_stats = st->perf_stats;
            next_state->heap_owned = 1;
            MPF_PERF_INC_STATS(perf, state_heap_allocs);
            MPF_PERF_INC_STATS(perf, state_cache_misses);
        }
    }
    mpf_apply_action_internal(st, action, next_state);
    return (uint64_t)(uintptr_t)next_state;
}

static int mpf_is_terminal(const mpf_state_t *st)
{
    if (st->util_ready)
        return 1;
    if (mpf_active_count(st) <= 1)
        return 1;
    if (st->street == MPF_STREET_SHOWDOWN)
        return 1;
    return 0;
}

static int mpf_active_count(const mpf_state_t *st)
{
    int cnt = 0;
    for (int i = 0; i < st->num_players; ++i)
        if (st->active[i])
            cnt++;
    return cnt;
}

static void mpf_mark_winner_fold(mpf_state_t *st, int winner)
{
    for (int i = 0; i < st->num_players; ++i)
        st->utilities[i] = -st->invested[i];
    if (winner >= 0)
        st->utilities[winner] += st->pot;
    st->util_ready = 1;
}

static void mpf_compute_utilities(mpf_state_t *st)
{
    if (st->util_ready)
        return;
    MPF_PERF_INC_STATE(st, utility_computations);

    int active_players[MPF_MAX_PLAYERS];
    int act_cnt = 0;
    for (int i = 0; i < st->num_players; ++i)
    {
        st->utilities[i] = -st->invested[i];
        if (st->active[i])
            active_players[act_cnt++] = i;
    }
    if (act_cnt == 0)
    {
        st->util_ready = 1;
        return;
    }
    if (act_cnt == 1)
    {
        st->utilities[active_players[0]] += st->pot;
        st->util_ready = 1;
        return;
    }

    mask_t board = MASK_EMPTY;
    for (int i = 0; i < st->board_revealed; ++i)
        board = mask_set(board, st->board_cards[i]);

    eval_t best = 0;
    int winners[MPF_MAX_PLAYERS];
    int win_cnt = 0;

    for (int idx = 0; idx < act_cnt; ++idx)
    {
        int p = active_players[idx];
        eval_t value = 0;
        switch (st->rules)
        {
        case MPF_RULE_HOLDEM:
            value = eval_holdem_high(st->ctx, st->hole[p], board);
            break;
        case MPF_RULE_SHORTDECK:
            value = eval_holdem_high(st->ctx, st->hole[p], board);
            break;
        case MPF_RULE_PLO4:
        case MPF_RULE_PLO5:
        case MPF_RULE_PLO6:
            value = eval_omaha_high(st->ctx, st->hole[p], board, st->total_hole_cards);
            break;
        default:
            value = eval_holdem_high(st->ctx, st->hole[p], board);
            break;
        }
        if (win_cnt == 0 || value > best)
        {
            best = value;
            winners[0] = p;
            win_cnt = 1;
        }
        else if (value == best)
        {
            winners[win_cnt++] = p;
        }
    }

    double share = (win_cnt > 0) ? (st->pot / win_cnt) : 0.0;
    for (int i = 0; i < win_cnt; ++i)
        st->utilities[winners[i]] += share;
    st->util_ready = 1;
}

static int mpf_round_complete(const mpf_state_t *st)
{
    if (mpf_active_count(st) <= 1)
        return 1;
    for (int i = 0; i < st->num_players; ++i)
    {
        if (!st->active[i])
            continue;
        if (st->stacks[i] > MPF_EPS)
        {
            double diff = st->to_call - st->round_contrib[i];
            if (diff > MPF_EPS)
                return 0;
            if (!st->acted_this_round[i])
                return 0;
        }
    }
    return 1;
}

static int mpf_next_active(const mpf_state_t *st, int idx)
{
    if (mpf_active_count(st) <= 1)
        return -1;
    for (int i = 0; i < st->num_players; ++i)
    {
        int p = (idx + i) % st->num_players;
        if (st->active[p] && st->stacks[p] > MPF_EPS)
            return p;
    }
    return -1;
}

static int mpf_first_player_after(const mpf_state_t *st, int idx)
{
    for (int i = 0; i < st->num_players; ++i)
    {
        int p = (idx + i) % st->num_players;
        if (st->active[p] && st->stacks[p] > MPF_EPS)
            return p;
    }
    return -1;
}

static void mpf_reset_round(mpf_state_t *st, mpf_street_t new_street)
{
    MPF_PERF_INC_STATE(st, round_resets);
    st->street = new_street;
    st->to_call = 0.0;
    st->current_bet = 0.0;
    st->raises_made = 0;
    for (int i = 0; i < st->num_players; ++i)
    {
        st->round_contrib[i] = 0.0;
        st->acted_this_round[i] = 0;
    }
}

static void mpf_update_board(mpf_state_t *st, int revealed)
{
    MPF_PERF_INC_STATE(st, board_updates);
    st->board_revealed = revealed;
    mask_t board = MASK_EMPTY;
    for (int i = 0; i < revealed; ++i)
        board = mask_set(board, st->board_cards[i]);
    st->board_mask = board;
}

static void mpf_advance_street(mpf_state_t *st)
{
    MPF_PERF_INC_STATE(st, street_transitions);
    if (st->street == MPF_STREET_PREFLOP)
    {
        mpf_reset_round(st, MPF_STREET_FLOP);
        mpf_update_board(st, 3);
        int first = mpf_first_player_after(st, st->button_index + 1);
        st->first_to_act = first;
        st->to_act = first;
    }
    else if (st->street == MPF_STREET_FLOP)
    {
        mpf_reset_round(st, MPF_STREET_TURN);
        mpf_update_board(st, 4);
        int first = mpf_first_player_after(st, st->button_index + 1);
        st->first_to_act = first;
        st->to_act = first;
    }
    else if (st->street == MPF_STREET_TURN)
    {
        mpf_reset_round(st, MPF_STREET_RIVER);
        mpf_update_board(st, 5);
        int first = mpf_first_player_after(st, st->button_index + 1);
        st->first_to_act = first;
        st->to_act = first;
    }
}

static void mpf_init_round_flags(mpf_state_t *st)
{
    for (int i = 0; i < st->num_players; ++i)
    {
        st->round_contrib[i] = 0.0;
        st->acted_this_round[i] = 0;
    }
}

static void mpf_restore_base_bets(mpf_state_t *st)
{
    if (!st)
        return;
    st->bet_size_count = st->base_bet_size_count;
    for (int i = 0; i < st->base_bet_size_count && i < MPF_MAX_BET_SIZES; ++i)
        st->bet_sizes[i] = st->base_bet_sizes[i];
    st->enable_pot_sizing = st->base_enable_pot_sizing;
}

static void mpf_apply_tree_snapshot(mpf_state_t *st, const mpf_tree_snapshot_t *snap)
{
    if (!st || !snap || !snap->defined)
        return;
    MPF_PERF_INC_STATE(st, tree_snapshot_applies);
    int limit = st->num_players;
    if (snap->has_num_players && snap->num_players > 0 && snap->num_players < limit)
        limit = snap->num_players;

    if (snap->has_street)
        st->street = snap->street;
    if (snap->has_to_act && snap->to_act >= -1 && snap->to_act < st->num_players)
        st->to_act = snap->to_act;
    if (snap->has_first_to_act && snap->first_to_act >= -1 && snap->first_to_act < st->num_players)
        st->first_to_act = snap->first_to_act;
    if (snap->has_pot)
        st->pot = snap->pot;
    if (snap->has_to_call)
        st->to_call = snap->to_call;
    if (snap->has_current_bet)
        st->current_bet = snap->current_bet;
    if (snap->has_raises_made)
        st->raises_made = snap->raises_made;

    if (snap->has_stacks)
    {
        int count = snap->stacks_len;
        if (count > limit)
            count = limit;
        for (int i = 0; i < count; ++i)
            st->stacks[i] = snap->stacks[i];
    }
    if (snap->has_invested)
    {
        int count = snap->invested_len;
        if (count > limit)
            count = limit;
        for (int i = 0; i < count; ++i)
            st->invested[i] = snap->invested[i];
    }
    if (snap->has_round_contrib)
    {
        int count = snap->round_contrib_len;
        if (count > limit)
            count = limit;
        for (int i = 0; i < count; ++i)
            st->round_contrib[i] = snap->round_contrib[i];
    }
    if (snap->has_active)
    {
        int count = snap->active_len;
        if (count > limit)
            count = limit;
        for (int i = 0; i < count; ++i)
            st->active[i] = snap->active[i] ? 1 : 0;
    }
    if (snap->has_acted)
    {
        int count = snap->acted_len;
        if (count > limit)
            count = limit;
        for (int i = 0; i < count; ++i)
            st->acted_this_round[i] = snap->acted[i] ? 1 : 0;
    }
    if (snap->has_active || snap->has_acted)
    {
        for (int i = limit; i < st->num_players; ++i)
        {
            if (snap->has_active)
                st->active[i] = st->active[i] ? 1 : 0;
            if (snap->has_acted)
                st->acted_this_round[i] = st->acted_this_round[i] ? 1 : 0;
        }
    }

    int board_changed = 0;
    if (snap->has_board)
    {
        int count = snap->board_len;
        if (count > 5)
            count = 5;
        for (int i = 0; i < count; ++i)
        {
            int card = snap->board_cards[i];
            if (card >= 0)
                st->board_cards[i] = card;
        }
        board_changed = 1;
    }
    if (snap->has_board_revealed)
    {
        int reveal = snap->board_revealed;
        if (reveal < 0)
            reveal = 0;
        if (reveal > 5)
            reveal = 5;
        st->board_revealed = reveal;
        board_changed = 1;
    }
    if (board_changed)
        mpf_update_board(st, st->board_revealed);
    st->util_ready = 0;
}

static void mpf_apply_tree_node(mpf_state_t *st, int node_idx)
{
    if (!st || !st->tree)
        return;
    if (node_idx < 0 || node_idx >= st->tree->node_count)
    {
        st->tree_enabled = 0;
        st->tree_node_idx = -1;
        mpf_restore_base_bets(st);
        return;
    }
    while (st && st->tree && node_idx >= 0 && node_idx < st->tree->node_count)
    {
        mpf_tree_node_t *node = &st->tree->nodes[node_idx];
        st->tree_node_idx = node_idx;
#if !defined(_WIN32)
        pthread_mutex_lock(&node->cache_lock);
        pthread_t self = pthread_self();
        int slot = -1;
        for (int s = 0; s < MPF_NODE_CACHE_SLOTS; ++s)
        {
            if (node->cache_slots[s].owner == self)
            {
                slot = s;
                break;
            }
            if (slot < 0 && node->cache_slots[s].state == NULL)
                slot = s;
        }
        if (slot < 0)
            slot = 0;
        if (node->cache_slots[slot].state && node->cache_slots[slot].state != st)
        {
            mpf_state_cleanup_internal(node->cache_slots[slot].state);
            free(node->cache_slots[slot].state);
        }
        node->cache_slots[slot].owner = self;
        node->cache_slots[slot].state = st;
        node->state_key = (uint64_t)(uintptr_t)st;
        if (st->lock_storage)
        {
            if (!node->cache_slots[slot].lock_wired && node->is_locked &&
                node->locked_strategy && node->locked_strategy_count > 0 &&
                cfr_storage_set_locked_strategy(st->lock_storage, node->state_key,
                                                node->locked_strategy,
                                                node->locked_strategy_count) == 0)
                node->cache_slots[slot].lock_wired = 1;
        }
        pthread_mutex_unlock(&node->cache_lock);
#else
        node->state_cache = st;
        node->state_key = (uint64_t)(uintptr_t)st;
        if (st->lock_storage)
            mpf_wire_node_lock(node, node->state_key, st->lock_storage);
#endif

        mpf_restore_base_bets(st);
        if (node->bet_size_count > 0)
        {
            st->bet_size_count = node->bet_size_count;
            for (int i = 0; i < node->bet_size_count && i < MPF_MAX_BET_SIZES; ++i)
                st->bet_sizes[i] = node->bet_sizes[i];
        }
        if (node->use_pot_sizing >= 0)
            st->enable_pot_sizing = node->use_pot_sizing ? 1 : 0;
        else
            st->enable_pot_sizing = st->base_enable_pot_sizing;

        if (node->has_snapshot)
            mpf_apply_tree_snapshot(st, &node->snapshot);

        if (node->type == MPF_TREE_NODE_PLAYER)
            return;

        if (node->type == MPF_TREE_NODE_TERMINAL)
        {
            st->tree_enabled = 0;
            st->tree_node_idx = -1;
            mpf_restore_base_bets(st);
            return;
        }

        if (node->type == MPF_TREE_NODE_CHANCE)
        {
            if (node->action_count == 0)
            {
                st->tree_enabled = 0;
                st->tree_node_idx = -1;
                return;
            }
            int next = node->actions[0].next_index;
            if (next < 0 || next == node_idx)
            {
                st->tree_enabled = 0;
                st->tree_node_idx = -1;
                return;
            }
            node_idx = next;
            continue;
        }

        break;
    }
}

static int mpf_tree_find_next(const mpf_state_t *st, int action)
{
    if (!st || !st->tree || !st->tree_enabled || st->tree_node_idx < 0)
        return -1;
    const mpf_tree_node_t *node = &st->tree->nodes[st->tree_node_idx];
    if (node->type != MPF_TREE_NODE_PLAYER)
        return -1;
    int target = action;
    for (int i = 0; i < node->action_count; ++i)
    {
        const mpf_tree_action_t *act = &node->actions[i];
        int code = -1;
        switch (act->type)
        {
        case MPF_TREE_ACTION_FOLD:
            code = MPF_ACTION_FOLD;
            break;
        case MPF_TREE_ACTION_CALL:
            code = MPF_ACTION_CALL;
            break;
        case MPF_TREE_ACTION_RAISE:
            if (act->size_index >= 0 && act->size_index < st->bet_size_count)
                code = MPF_ACTION_RAISE_BASE + act->size_index;
            break;
        case MPF_TREE_ACTION_CHANCE:
        case MPF_TREE_ACTION_TERMINAL:
            /* Unsupported in adapter; ignore */
            break;
        default:
            break;
        }
        if (code == target)
            return act->next_index;
    }
    return -1;
}

static int mpf_tree_collect_actions(const mpf_state_t *st, int *out_actions, int max_actions)
{
    if (!st || !st->tree || !st->tree_enabled || st->tree_node_idx < 0)
        return 0;
    const mpf_tree_node_t *node = &st->tree->nodes[st->tree_node_idx];
    if (node->type != MPF_TREE_NODE_PLAYER)
        return 0;
    if (node->acting_player != st->to_act)
        return 0;
    int player = st->to_act;
    double need = st->to_call - st->round_contrib[player];
    if (need < MPF_EPS)
        need = 0.0;

    int count = 0;
    int added_fold = 0;
    int added_call = 0;

    for (int i = 0; i < node->action_count; ++i)
    {
        if (count >= max_actions)
            break;
        const mpf_tree_action_t *act = &node->actions[i];
        switch (act->type)
        {
        case MPF_TREE_ACTION_FOLD:
            if (!added_fold && need > MPF_EPS)
            {
                out_actions[count++] = MPF_ACTION_FOLD;
                added_fold = 1;
            }
            break;
        case MPF_TREE_ACTION_CALL:
            if (!added_call)
            {
                out_actions[count++] = MPF_ACTION_CALL;
                added_call = 1;
            }
            break;
        case MPF_TREE_ACTION_RAISE:
            if (st->raises_made < st->raise_cap && st->stacks[player] > need + MPF_EPS)
            {
                int idx = act->size_index;
                if (idx >= 0 && idx < st->bet_size_count)
                    out_actions[count++] = MPF_ACTION_RAISE_BASE + idx;
            }
            break;
        case MPF_TREE_ACTION_CHANCE:
        case MPF_TREE_ACTION_TERMINAL:
            /* not actionable for players */
            break;
        default:
            break;
        }
    }
    return count;
}
static void mpf_apply_preconfig(const mpf_config_t *cfg, mpf_state_t *st)
{
    const mpf_preflop_cfg_t *pre = &cfg->preflop;
    if (!pre->defined)
        return;

    if (pre->has_to_call)
        st->to_call = pre->to_call;
    if (pre->has_current_bet)
        st->current_bet = pre->current_bet;
    if (pre->has_raises)
        st->raises_made = pre->raises_made;

    if (pre->has_round)
        for (int i = 0; i < st->num_players; ++i)
            st->round_contrib[i] = pre->round_contrib[i];

    if (pre->has_active)
        for (int i = 0; i < st->num_players; ++i)
            st->active[i] = pre->active[i] ? 1 : 0;

    if (pre->has_to_act && pre->to_act >= 0)
    {
        int idx = pre->to_act % st->num_players;
        if (idx < 0)
            idx += st->num_players;
        st->to_act = idx;
        st->first_to_act = idx;
    }

    for (int i = 0; i < st->num_players; ++i)
    {
        if (!st->active[i])
        {
            st->acted_this_round[i] = 1;
            continue;
        }
        double need = st->to_call - st->round_contrib[i];
        if (need <= MPF_EPS || st->stacks[i] <= MPF_EPS)
            st->acted_this_round[i] = 1;
        else
            st->acted_this_round[i] = 0;
    }
    if (st->to_act >= 0 && st->to_act < st->num_players)
        st->acted_this_round[st->to_act] = 0;
}


static void mpf_wire_node_lock(mpf_tree_node_t *node, uint64_t state_key, cfr_storage_t *storage)
{
    if (!node || !storage || node->lock_wired)
        return;
    if (node->type != MPF_TREE_NODE_PLAYER)
        return;
    if (!node->is_locked || !node->locked_strategy || node->locked_strategy_count <= 0)
        return;
    if (cfr_storage_set_locked_strategy(storage, state_key,
                                        node->locked_strategy,
                                        node->locked_strategy_count) == 0)
        node->lock_wired = 1;
}

int mpf_apply_locked_strategies(mpf_state_t *root_state, cfr_storage_t *storage)
{
    if (!root_state || !storage)
        return -1;
    root_state->lock_storage = storage;
    int applied = 0;
    if (root_state->tree && root_state->tree_node_idx >= 0)
    {
        mpf_tree_node_t *node = &root_state->tree->nodes[root_state->tree_node_idx];
        if (node->is_locked && node->locked_strategy && node->locked_strategy_count > 0 &&
            node->state_key != 0 && !node->lock_wired)
        {
            if (cfr_storage_set_locked_strategy(storage, node->state_key,
                                                node->locked_strategy,
                                                node->locked_strategy_count) == 0)
            {
                node->lock_wired = 1;
                ++applied;
            }
        }
    }
    return applied;
}

int mpf_build_game(const mpf_config_t *cfg, cfr_game_t *out_game, mpf_state_t *out_state)
{
    if (!cfg || !cfg->ctx || !out_game || !out_state)
        return -1;
    if (cfg->num_players < 2 || cfg->num_players > MPF_MAX_PLAYERS)
        return -1;

    memset(out_state, 0, sizeof(*out_state));
    memset(out_game, 0, sizeof(*out_game));

    mpf_perf_stats_t *perf_stats = NULL;
    if (cfg->perf_pool)
        perf_stats = mpf_perf_stats_pool_acquire(cfg->perf_pool);
    if (!perf_stats)
        perf_stats = cfg->perf_stats;
    out_state->perf_stats = perf_stats;
    out_state->heap_owned = 0;
    if (perf_stats)
        mpf_perf_stats_reset(perf_stats);

    out_state->ctx = cfg->ctx;
    out_state->rules = cfg->rules;
    out_state->lock_storage = NULL;
    out_state->num_players = cfg->num_players;
    out_state->street = cfg->start_street;
    out_state->button_index = (cfg->button_index % cfg->num_players + cfg->num_players) % cfg->num_players;
    out_state->sb = cfg->sb;
    out_state->bb = cfg->bb;
    out_state->ante = cfg->ante;
    out_state->raise_cap = cfg->raise_cap;
    if (out_state->raise_cap < 0)
        out_state->raise_cap = 0;
    out_state->enable_pot_sizing = cfg->enable_pot_sizing;

    out_state->bet_size_count = cfg->bet_size_count_common;
    if (out_state->bet_size_count > MPF_MAX_BET_SIZES)
        out_state->bet_size_count = MPF_MAX_BET_SIZES;
    for (int i = 0; i < out_state->bet_size_count; ++i)
        out_state->bet_sizes[i] = cfg->bet_sizes_common[i];
    out_state->base_bet_size_count = out_state->bet_size_count;
    for (int i = 0; i < out_state->base_bet_size_count; ++i)
        out_state->base_bet_sizes[i] = out_state->bet_sizes[i];
    out_state->base_enable_pot_sizing = out_state->enable_pot_sizing;

    for (int i = 0; i < cfg->num_players; ++i)
    {
        out_state->stacks[i] = cfg->stacks[i];
        out_state->active[i] = 1;
        out_state->hole[i] = cfg->hole[i];
        out_state->invested[i] = 0.0;
        out_state->round_contrib[i] = 0.0;
        out_state->acted_this_round[i] = 0;
    }
    out_state->total_hole_cards = 2;
    if (cfg->rules == MPF_RULE_PLO4)
        out_state->total_hole_cards = 4;
    else if (cfg->rules == MPF_RULE_PLO5)
        out_state->total_hole_cards = 5;
    else if (cfg->rules == MPF_RULE_PLO6)
        out_state->total_hole_cards = 6;

    int board_limit = cfg->board_card_count;
    if (board_limit > 5)
        board_limit = 5;
    if (board_limit < 0)
        board_limit = 0;
    for (int i = 0; i < board_limit; ++i)
        out_state->board_cards[i] = cfg->board_cards[i];
    for (int i = board_limit; i < 5; ++i)
        out_state->board_cards[i] = cfg->board_cards[i];

    int pre_defined = cfg->preflop.defined;
    if (!pre_defined && out_state->ante > 0.0)
    {
        for (int i = 0; i < cfg->num_players; ++i)
        {
            double pay = fmin(out_state->stacks[i], out_state->ante);
            out_state->stacks[i] -= pay;
            out_state->invested[i] += pay;
            out_state->pot += pay;
        }
    }

    out_state->sb_index = (out_state->button_index + 1) % cfg->num_players;
    out_state->bb_index = (out_state->button_index + 2) % cfg->num_players;

    if (!pre_defined)
    {
        double sb_pay = fmin(out_state->stacks[out_state->sb_index], out_state->sb);
        out_state->stacks[out_state->sb_index] -= sb_pay;
        out_state->round_contrib[out_state->sb_index] = sb_pay;
        out_state->invested[out_state->sb_index] += sb_pay;
        out_state->pot += sb_pay;
        out_state->acted_this_round[out_state->sb_index] = 1;

        double bb_pay = fmin(out_state->stacks[out_state->bb_index], out_state->bb);
        out_state->stacks[out_state->bb_index] -= bb_pay;
        out_state->round_contrib[out_state->bb_index] = bb_pay;
        out_state->invested[out_state->bb_index] += bb_pay;
        out_state->pot += bb_pay;
        out_state->acted_this_round[out_state->bb_index] = 1;

        out_state->to_call = bb_pay;
        out_state->current_bet = bb_pay;
    }
    else
    {
        const mpf_preflop_cfg_t *pre = &cfg->preflop;
        double pot = 0.0;
        if (pre->has_active)
        {
            for (int i = 0; i < cfg->num_players; ++i)
                out_state->active[i] = pre->active[i] ? 1 : 0;
        }
        if (pre->has_invested)
        {
            pot = 0.0;
            for (int i = 0; i < cfg->num_players; ++i)
            {
                out_state->invested[i] = pre->invested[i];
                pot += pre->invested[i];
            }
        }
        if (pre->has_round)
        {
            for (int i = 0; i < cfg->num_players; ++i)
                out_state->round_contrib[i] = pre->round_contrib[i];
        }
        if (pre->has_pot)
            out_state->pot = pre->pot;
        else if (pre->has_invested)
            out_state->pot = pot;

        if (pre->has_to_call)
            out_state->to_call = pre->to_call;
        else
            out_state->to_call = 0.0;
        if (pre->has_current_bet)
            out_state->current_bet = pre->current_bet;
        else
            out_state->current_bet = out_state->to_call;
        if (pre->has_raises)
            out_state->raises_made = pre->raises_made;
        else
            out_state->raises_made = 0;
    }

    if (cfg->start_street == MPF_STREET_PREFLOP)
    {
        out_state->first_to_act = mpf_next_active(out_state, out_state->bb_index + 1);
        out_state->to_act = out_state->first_to_act;
        out_state->board_revealed = 0;
        out_state->board_mask = MASK_EMPTY;
    }
    else
    {
        if (cfg->start_street == MPF_STREET_FLOP)
        {
            mpf_reset_round(out_state, MPF_STREET_FLOP);
            mpf_update_board(out_state, 3);
        }
        else if (cfg->start_street == MPF_STREET_TURN)
        {
            mpf_reset_round(out_state, MPF_STREET_TURN);
            mpf_update_board(out_state, 4);
        }
        else if (cfg->start_street == MPF_STREET_RIVER)
        {
            mpf_reset_round(out_state, MPF_STREET_RIVER);
            mpf_update_board(out_state, 5);
        }
        out_state->first_to_act = mpf_first_player_after(out_state, out_state->button_index + 1);
        out_state->to_act = out_state->first_to_act;
    }

    if (cfg->preflop.defined)
        mpf_apply_preconfig(cfg, out_state);

    out_state->tree = cfg->tree;
    out_state->tree_enabled = (cfg->tree && cfg->tree_enforced) ? 1 : 0;
    out_state->tree_node_idx = -1;
    if (out_state->tree_enabled && out_state->tree)
    {
        out_state->tree_node_idx = out_state->tree->root_index;
        mpf_apply_tree_node(out_state, out_state->tree_node_idx);
    }
    else
    {
        mpf_restore_base_bets(out_state);
    }

    out_game->initial_state = out_state;
    out_game->game_data = out_state;
    out_game->is_terminal = mpf_is_terminal_wrapper;
    out_game->get_utility = mpf_get_utility_wrapper;
    out_game->get_actions = mpf_get_actions_wrapper;
    out_game->apply_action = mpf_apply_action_wrapper;
    out_game->current_player = mpf_current_player_wrapper;
    out_game->num_players = cfg->num_players;
    out_game->state_size = sizeof(*out_state);

    return 0;
}

static void mpf_state_cleanup_internal(mpf_state_t *state)
{
    if (!state)
        return;
    for (int i = 0; i < MPF_TREE_ACTION_MAX; ++i)
    {
        mpf_state_t *child = state->action_cache[i];
        if (child)
        {
            mpf_state_cleanup_internal(child);
            if (child->heap_owned)
                free(child);
            state->action_cache[i] = NULL;
        }
    }
}

static int mpf_state_ptr_seen(mpf_state_t **seen, int seen_count, const mpf_state_t *ptr)
{
    for (int i = 0; i < seen_count; ++i)
    {
        if (seen[i] == ptr)
            return 1;
    }
    return 0;
}

static void mpf_tree_release_cache(mpf_tree_def_t *tree, const mpf_state_t *root)
{
    if (!tree)
        return;
    MPF_ADAPTER_DEBUG("MPF: release cache start (nodes=%d, root=%p)\n", tree->node_count, (const void *)root);
    size_t max_entries = (size_t)tree->node_count;
#if !defined(_WIN32)
    max_entries *= MPF_NODE_CACHE_SLOTS;
    pthread_t self = pthread_self();
#endif
    if (max_entries < 1)
        max_entries = 1;
    mpf_state_t **seen = (mpf_state_t **)malloc((max_entries + 1) * sizeof(*seen));
    int seen_count = 0;
    int allow_free = (seen != NULL);
    if (allow_free && root)
        seen[seen_count++] = (mpf_state_t *)root;
    for (int i = 0; i < tree->node_count; ++i)
    {
        mpf_tree_node_t *node = &tree->nodes[i];
#if !defined(_WIN32)
        pthread_mutex_lock(&node->cache_lock);
        for (int s = 0; s < MPF_NODE_CACHE_SLOTS; ++s)
        {
            mpf_state_t *cached = node->cache_slots[s].state;
            if (cached && pthread_equal(node->cache_slots[s].owner, self))
            {
                if (allow_free && !mpf_state_ptr_seen(seen, seen_count, cached))
                {
                    mpf_state_cleanup_internal(cached);
                    if (cached->heap_owned)
                        free(cached);
                    MPF_ADAPTER_DEBUG("MPF: freed cached state %p from node %d slot %d\n",
                                      (void *)cached, i, s);
                    if (seen_count < (int)max_entries + 1)
                        seen[seen_count++] = cached;
                }
                node->cache_slots[s].state = NULL;
                node->cache_slots[s].owner = 0;
            }
        }
        node->state_key = 0;
        for (int s = 0; s < MPF_NODE_CACHE_SLOTS; ++s)
        {
            if (node->cache_slots[s].state)
            {
                node->state_key = (uint64_t)(uintptr_t)node->cache_slots[s].state;
                break;
            }
        }
        pthread_mutex_unlock(&node->cache_lock);
#else
        if (node->state_cache && node->state_cache != root)
        {
            if (allow_free && !mpf_state_ptr_seen(seen, seen_count, node->state_cache))
            {
                mpf_state_cleanup_internal(node->state_cache);
                if (node->state_cache->heap_owned)
                    free(node->state_cache);
                MPF_ADAPTER_DEBUG("MPF: freed node state_cache %p for node %d\n",
                                  (void *)node->state_cache, i);
                if (seen_count < (int)max_entries + 1)
                    seen[seen_count++] = node->state_cache;
            }
        }
        node->state_cache = NULL;
        node->state_key = 0;
#endif
    }
    free(seen);
    MPF_ADAPTER_DEBUG("MPF: release cache complete\n");
}

void mpf_state_cleanup(mpf_state_t *state)
{
    if (!state)
        return;
    MPF_ADAPTER_DEBUG("MPF: mpf_state_cleanup on %p\n", (void *)state);
    mpf_state_cleanup_internal(state);
    if (state->tree)
        mpf_tree_release_cache(state->tree, state);
}

void mpf_state_cleanup_cached(mpf_state_t *state)
{
    MPF_ADAPTER_DEBUG("MPF: mpf_state_cleanup_cached on %p\n", (void *)state);
    mpf_state_cleanup_internal(state);
}
