#include <poker_eval/engine/solvers/cfr/multiway_postflop_adapter.h>
#include <poker_eval/engine/solvers/cfr/mpf_tree.h>
#include <poker_eval/engine/solvers/cfr/mpf_stack_index.h>
#include <poker_eval/engine/solvers/cfr/board_canonical.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/solver/pe_combinations.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>

#define MPF_EPS 1e-9

/*
 * CHN-02: the widest flop node the adapter will build, C(52,3). Nothing can
 * legitimately exceed it, so the guard never fires in play — it is there so
 * that a wrong unused-card count is refused rather than turned into an
 * allocation. The cost worth knowing is the other one: every outcome is a
 * cached child state, and at 17296 flops from a full deck that is tens of
 * megabytes for a single node. Lane A exists precisely so this is not how
 * preflop gets solved at scale.
 */
#define MPF_MAX_FLOP_OUTCOMES 22100u

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
static void mpf_wire_node_lock(mpf_tree_node_t *node, uint64_t state_key, cfr_storage_t *storage);
static void mpf_apply_tree_node(mpf_state_t *st, int node_idx);
static int mpf_tree_find_next(const mpf_state_t *st, int action);
static int mpf_tree_collect_actions(const mpf_state_t *st, int *out_actions, int max_actions);

static size_t mask_to_array_count(mask_t mask)
{
    size_t count = 0;
    for (int c = 0; c < 52; ++c)
        if (mask_is_set(mask, c))
            ++count;
    return count;
}

static int mask_to_array(mask_t mask, int *out, int limit);

/* ===== FEAT-03: real chance nodes ======================================
 * When enable_chance_nodes is set, street transitions on the turn and river
 * become chance layers: the solver enumerates every unseen card with uniform
 * weight instead of revealing a fixed pre-dealt board.
 *
 * Chance children are dealt from a chance-awaiting parent state. Because many
 * children stay alive at the same time, they cannot go through the node /
 * action caches; each parent owns a small per-outcome cache instead, so
 * pointers (and therefore infoset keys) stay stable across iterations.
 *
 * In chance mode the adapter switches from raw state pointers to content
 * derived infoset keys (keyed_mode): a 64-bit hash of the board/hole canon
 * pattern (FEAT-02) plus the betting state. This is required because the
 * same (non-chance) decision reappears under many runouts and must map to a
 * single storage entry across iterations. The key -> state mapping is kept
 * in a thread-local table, re-registered every time a state is produced, so
 * stale entries self-heal. */

#define MPF_KEY_MAP_CAP (1 << 17)

typedef struct
{
    uint64_t key;
    mpf_state_t *state;
    const mpf_state_t *owner;
} mpf_key_map_entry_t;

#if defined(_MSC_VER)
#define MPF_THREAD_LOCAL thread
#elif defined(__GNUC__) || defined(__clang__)
#define MPF_THREAD_LOCAL __thread
#else
#define MPF_THREAD_LOCAL _Thread_local
#endif

/* Keyed states must remain resolvable when a game is built on one thread and
 * solved on another.  Entries carry their owning root so separate games can
 * coexist without clearing each other's mappings. */
#if !defined(_WIN32)
static mpf_key_map_entry_t g_mpf_key_map[MPF_KEY_MAP_CAP];
static pthread_mutex_t g_mpf_key_map_lock = PTHREAD_MUTEX_INITIALIZER;
#define MPF_KEY_MAP_LOCK() pthread_mutex_lock(&g_mpf_key_map_lock)
#define MPF_KEY_MAP_UNLOCK() pthread_mutex_unlock(&g_mpf_key_map_lock)
#else
static MPF_THREAD_LOCAL mpf_key_map_entry_t g_mpf_key_map[MPF_KEY_MAP_CAP];
#define MPF_KEY_MAP_LOCK() ((void)0)
#define MPF_KEY_MAP_UNLOCK() ((void)0)
#endif

static uint64_t mpf_key_mix(uint64_t key)
{
    key ^= key >> 33;
    key *= 0xff51afd7ed558ccdull;
    key ^= key >> 33;
    key *= 0xc4ceb9fe1a85ec53ull;
    key ^= key >> 33;
    return key;
}

static mpf_state_t *mpf_key_map_lookup(uint64_t key, const mpf_state_t *owner)
{
    mpf_state_t *result = NULL;
    MPF_KEY_MAP_LOCK();
    uint64_t h = mpf_key_mix(key) & (MPF_KEY_MAP_CAP - 1);
    for (int i = 0; i < MPF_KEY_MAP_CAP; ++i)
    {
        uint64_t idx = (h + (uint64_t)i) & (MPF_KEY_MAP_CAP - 1);
        if (g_mpf_key_map[idx].state && g_mpf_key_map[idx].key == key &&
            g_mpf_key_map[idx].owner == owner)
        {
            result = g_mpf_key_map[idx].state;
            break;
        }
    }
    MPF_KEY_MAP_UNLOCK();
    return result;
}

static void mpf_key_map_register(uint64_t key, mpf_state_t *st)
{
    if (!st || !st->key_map_owner)
        return;
    MPF_KEY_MAP_LOCK();
    uint64_t h = mpf_key_mix(key) & (MPF_KEY_MAP_CAP - 1);
    for (int i = 0; i < MPF_KEY_MAP_CAP; ++i)
    {
        uint64_t idx = (h + (uint64_t)i) & (MPF_KEY_MAP_CAP - 1);
        if (g_mpf_key_map[idx].state == NULL ||
            (g_mpf_key_map[idx].key == key &&
             g_mpf_key_map[idx].owner == st->key_map_owner))
        {
            g_mpf_key_map[idx].key = key;
            g_mpf_key_map[idx].state = st;
            g_mpf_key_map[idx].owner = st->key_map_owner;
            MPF_KEY_MAP_UNLOCK();
            return;
        }
    }
    /* Map full: evict the new key's primary bucket. The parent whose entry
     * is displaced here was already re-registered during its own traversal,
     * so it self-heals on its next visit. */
    g_mpf_key_map[h].key = key;
    g_mpf_key_map[h].state = st;
    g_mpf_key_map[h].owner = st->key_map_owner;
    MPF_KEY_MAP_UNLOCK();
}

static void mpf_key_map_unregister_owner(const mpf_state_t *owner)
{
    if (!owner)
        return;
    MPF_KEY_MAP_LOCK();
    for (size_t i = 0; i < MPF_KEY_MAP_CAP; ++i)
    {
        if (g_mpf_key_map[i].state && g_mpf_key_map[i].owner == owner)
        {
            g_mpf_key_map[i].state = NULL;
            g_mpf_key_map[i].owner = NULL;
        }
    }
    MPF_KEY_MAP_UNLOCK();
}

static uint64_t mpf_fnv1a_hash_seeded(uint64_t seed, const void *data, size_t len)
{
    const unsigned char *p = (const unsigned char *)data;
    uint64_t h = seed;
    for (size_t i = 0; i < len; ++i)
    {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

static uint64_t mpf_fnv1a_hash(const void *data, size_t len)
{
    return mpf_fnv1a_hash_seeded(1469598103934665603ull, data, len);
}

static uint64_t mpf_quant(double v)
{
    if (v < 0.0)
        v = 0.0;
    if (v > 1e12)
        v = 1e12;
    return (uint64_t)llround(v * 10000.0);
}

/* Canonical board+hole pattern hash (suit-permutation invariant, FEAT-02). */
static uint64_t mpf_pattern_hash(const mpf_state_t *st, int player)
{
    mask_t set = st->board_mask | st->hole[player];
    int n = st->board_revealed + st->total_hole_cards;
    char key[200];
    char fallback[200];
    const char *str = fallback;
    if (pe_board_canonical_key(set, n, key, sizeof(key)) == 0)
    {
        str = key;
    }
    else
    {
        int pos = 0;
        for (int c = 0; c < MODERN_DECK_SIZE && pos < 100; ++c)
        {
            if (mask_is_set(set, c))
            {
                fallback[pos++] = (char)('0' + (MODERN_GET_RANK(c) % 10));
                fallback[pos++] = (char)('0' + MODERN_GET_SUIT(c));
            }
        }
        fallback[pos] = '\0';
    }
    return mpf_fnv1a_hash(str, strlen(str));
}

/* Texture-only abstraction still needs the acting player's exact private
 * pattern, but must not retain the exact board in that component. */
static uint64_t mpf_hole_pattern_hash(const mpf_state_t *st, int player)
{
    mask_t hole = st->hole[player];
    char key[100];
    char fallback[100];
    const char *str = fallback;
    if (pe_board_canonical_key(hole, st->total_hole_cards,
                               key, sizeof(key)) == 0)
    {
        str = key;
    }
    else
    {
        int pos = 0;
        for (int c = 0; c < MODERN_DECK_SIZE && pos < 98; ++c)
        {
            if (mask_is_set(hole, c))
            {
                fallback[pos++] = (char)('0' + (MODERN_GET_RANK(c) % 10));
                fallback[pos++] = (char)('0' + MODERN_GET_SUIT(c));
            }
        }
        fallback[pos] = '\0';
    }
    return mpf_fnv1a_hash(str, strlen(str));
}

/* ABS-02: replace the exact private-hand/board pattern with the trained
 * abstraction pair only when the caller explicitly enabled strength
 * bucketing and supplied (or successfully trained) a model. The caller's
 * zero-value configuration therefore takes the exact historical path. */
static int mpf_abstraction_key(const mpf_state_t *st, int player,
                               uint64_t *out_key)
{
    if (!st || !out_key || player < 0 || player >= st->num_players ||
        (st->strength_buckets_per_street <= 0 &&
         st->texture_filter_level <= PE_TEXTURE_FILTER_NONE))
        return 0;
    if (st->board_revealed < 3 || st->board_revealed > 5)
        return 0;

    uint64_t texture = pe_board_texture_id(
        st->board_mask, (pe_texture_filter_level_t)st->texture_filter_level);
    if (st->strength_buckets_per_street > 0 && st->abstraction_model &&
        mask_popcount(st->hole[player]) == st->total_hole_cards)
    {
        const pe_abstraction_ops_t *ops = pe_abstraction_ops();
        if (ops && ops->bucket_of)
        {
            int bucket = ops->bucket_of(st->abstraction_model, st->ctx,
                                        st->hole[player], st->board_mask,
                                        (int)st->street);
            if (bucket >= 0)
            {
                *out_key = (((uint64_t)(uint32_t)bucket) << 32) ^ texture;
                return 1;
            }
        }
    }

    if (st->texture_filter_level <= PE_TEXTURE_FILTER_NONE)
        return 0;

    /* PERFECT has no merge and therefore preserves the historical canonical
       board+hole hash when no strength abstraction is active. */
    if (st->strength_buckets_per_street <= 0 &&
        st->texture_filter_level == PE_TEXTURE_FILTER_PERFECT)
        return 0;

    /* Texture-only mode keeps the private pattern exact and merges only the
       board component. PERFECT naturally becomes the exact board id. */
    uint64_t hole_hash = mpf_hole_pattern_hash(st, player);
    *out_key = mpf_fnv1a_hash_seeded(hole_hash, &texture, sizeof(texture));
    return 1;
}

/* Content-derived infoset key for a decision state (keyed_mode only).
 * All bet/board fields are folded into a single running hash; each field
 * re-seeds from the previous field's digest so the key depends on every
 * field, not just the last one hashed. */
static uint64_t mpf_infoset_key(const mpf_state_t *st)
{
    uint64_t h = 1469598103934665603ull;
    int p = (st->to_act >= 0 && st->to_act < st->num_players) ? st->to_act : 0;
    uint64_t fields[4];
    fields[0] = (uint64_t)(st->street & 0xF) |
                ((uint64_t)(p & 0xF) << 4) |
                ((uint64_t)(st->raises_made & 0xFF) << 8) |
                ((uint64_t)(st->board_revealed & 0xF) << 16);
    fields[1] = mpf_quant(st->pot);
    fields[2] = mpf_quant(st->to_call);
    fields[3] = mpf_quant(st->current_bet);
    for (int i = 0; i < 4; ++i)
        h = mpf_fnv1a_hash_seeded(h, &fields[i], sizeof(fields[i]));
    uint64_t rc = mpf_quant(st->round_contrib[p]);
    uint64_t acted_flags = 0;
    uint64_t active_flags = 0;
    for (int i = 0; i < st->num_players; ++i)
    {
        if (st->acted_this_round[i])
            acted_flags |= (1ull << (uint64_t)(i % 64));
        if (st->active[i])
            active_flags |= (1ull << (uint64_t)(i % 64));
    }
    h = mpf_fnv1a_hash_seeded(h, &rc, sizeof(rc));
    h = mpf_fnv1a_hash_seeded(h, &acted_flags, sizeof(acted_flags));
    h = mpf_fnv1a_hash_seeded(h, &active_flags, sizeof(active_flags));
    uint64_t bh;
    uint64_t abstraction_key;
    if ((st->strength_buckets_per_street > 0 ||
         st->texture_filter_level > PE_TEXTURE_FILTER_NONE) &&
        mpf_abstraction_key(st, p, &abstraction_key))
        bh = abstraction_key;
    else
        bh = mpf_pattern_hash(st, p);
    h = mpf_fnv1a_hash_seeded(h, &bh, sizeof(bh));
    /* FEAT-10 (#146): fold the committed-stack configuration hash into the
       infoset key so asymmetrical (and equivalent-by-action-order) stack
       structures map to distinct-but-deduplicated infosets. We hash the
       configuration directly (not the sparse handle) so the key is stable
       across games and parts. */
    if (st->stack_index)
    {
        mpf_stack_config_t scfg;
        mpf_stack_config_from_arrays(&scfg, st->num_players,
                                    st->round_contrib, st->stacks, st->active);
        uint64_t sh = mpf_stack_config_hash(&scfg);
        h = mpf_fnv1a_hash_seeded(h, &sh, sizeof(sh));
    }
    return h;
}

static uint64_t mpf_state_key(const mpf_state_t *st)
{
    if (st->keyed_mode)
        return mpf_infoset_key(st);
    return (uint64_t)(uintptr_t)st;
}

/* cfr_game get_infoset_key callback: hash a state pointer into its infoset key.
   Wired (FEAT-10 #146) so storage indexes by the content-derived infoset key
   (which now folds in the sparse stack-config id) rather than the raw pointer. */
static uint64_t mpf_get_infoset_key_wrapper(const void *state)
{
    const mpf_state_t *st = (const mpf_state_t *)state;
    if (!st)
        return 0;
    return mpf_infoset_key(st);
}

/* Storage key for a tree node's state: when the sparse stack index is active
   the solver writes storage under the infoset hash, so the node key must be
   that same hash (not the raw pointer) for the exporter/collector to find the
   entries. Falls back to the pointer when no index is present. */
static uint64_t mpf_node_storage_key(const mpf_state_t *st)
{
    if (st && st->stack_index)
        return mpf_infoset_key(st);
    return (uint64_t)(uintptr_t)st;
}

/* FEAT-10 (#146): resolve (and cache) the sparse stack-config id for a state.
   The id is derived from the committed-stack configuration of the active
   players, so equivalent configs reachable via different action orders share
   one id. Safe to call repeatedly; only inserts when the id is still 0. */
static void mpf_state_resolve_cfg_id(mpf_state_t *st)
{
    if (!st || st->stack_cfg_id != 0 || !st->stack_index)
        return;
    mpf_stack_config_t cfg;
    mpf_stack_config_from_arrays(&cfg, st->num_players,
                                st->round_contrib, st->stacks,
                                st->active);
    uint32_t id = 0;
    if (mpf_stack_index_put(st->stack_index, &cfg, &id) == 0)
        st->stack_cfg_id = id;
}

static mpf_state_t *mpf_wrapper_state(cfr_game_t *game, uint64_t key);

static int mpf_get_street(cfr_game_t *game, uint64_t key, void *user_data)
{
    const mpf_state_t *st = mpf_wrapper_state(game, key);
    (void)user_data;
    return st ? (int)st->street : -1;
}

/* Enumerate the unused cards (not in any hole, not yet revealed). */
static int mpf_unused_cards(const mpf_state_t *st, int *out, int max)
{
    int used[52];
    int used_count = 0;
    for (int p = 0; p < st->num_players; ++p)
    {
        int list[6];
        int cnt = mask_to_array(st->hole[p], list, 6);
        for (int i = 0; i < cnt && used_count < 52; ++i)
            used[used_count++] = list[i];
    }
    for (int i = 0; i < st->board_revealed; ++i)
        used[used_count++] = st->board_cards[i];
    if (used_count > 52)
        used_count = 52;
    int count = 0;
    for (int c = 0; c < 52 && count < max; ++c)
    {
        int is_used = 0;
        for (int i = 0; i < used_count; ++i)
        {
            if (used[i] == c)
            {
                is_used = 1;
                break;
            }
        }
        if (!is_used)
            out[count++] = c;
    }
    return count;
}

/* ===== FEAT-14 (#150): folded-range card bunching ======================
 * When players fold preflop, the cards in their (unknown) hands are
 * statistically removed from the stub deck: a card that folded players play
 * often is less likely to remain available for the turn/river. Each folded
 * player with a provided range distribution and an *unspecified* hole
 * contributes a per-card survival factor (1 - f_p(c)), where f_p(c) is the
 * probability that card c was in player p's folded hand. The deal weight of
 * card c is then survival(c) normalized over all currently unseen cards, so
 * weights sum to 1 at every chance node and the uniform mode (all survival
 * factors 1) is recovered exactly when the estimator is disabled. */

static double mpf_bunching_state_survival(const mpf_state_t *st, int card)
{
    double s = 1.0;
    for (int p = 0; p < st->num_players; ++p)
    {
        /* Only players who have already folded (and therefore whose cards
         * are not in the stub) can deplete the deck. */
        if (st->active[p])
            continue;
        if (!st->folded_range_provided[p])
            continue;
        /* A fully-specified hole is already removed deterministically by the
         * unused-card enumeration; applying the range on top would
         * double-remove those cards. */
        if (mask_to_array_count(st->hole[p]) >= st->total_hole_cards)
            continue;
        double f = st->folded_range_prob[p][card];
        if (f < 0.0)
            f = 0.0;
        if (f > 1.0)
            f = 1.0;
        s *= (1.0 - f);
    }
    return s;
}

static int mpf_bunching_enabled(const mpf_state_t *st)
{
    return (st && st->enable_chance_nodes && st->enable_card_bunching) ? 1 : 0;
}

/* Per-outcome chance weight (FEAT-14): normalized survival probability of
 * the dealt card. Returns 1.0 (uniform) whenever the estimator is disabled
 * or the outcome index is out of range. */
static double mpf_get_chance_weight_wrapper(cfr_game_t *game, uint64_t key, int outcome, void *user)
{
    {
        const mpf_state_t *root = mpf_wrapper_state(game, key);
        if (mpf_state_chance_kind(root) == PE_CHANCE_PRIVATE_HANDS)
        {
            (void)user;
            if (outcome < 0 || outcome >= root->private_deal_count)
                return 0.0;
            return root->private_deals[outcome].weight;
        }
    }

    const mpf_state_t *st = mpf_wrapper_state(game, key);
    (void)user;
    /* Flop outcomes are combination indices, not card indices: the bunching
       estimator below reads `outcome` as a position in the unused-card list
       and would weight the wrong card. Every flop is equally likely until
       CHN-04 teaches the estimator to speak in combinations. */
    if (mpf_state_chance_kind(st) == PE_CHANCE_FLOP_THREE)
        return 1.0;
    if (!mpf_bunching_enabled(st))
        return 1.0;
    int cards[52];
    int count = mpf_unused_cards(st, cards, 52);
    if (outcome < 0 || outcome >= count)
        return 1.0;
    int card = cards[outcome];
    double w_target = 0.0;
    int found = 0;
    double wsum = 0.0;
    for (int i = 0; i < count; ++i)
    {
        double s = mpf_bunching_state_survival(st, cards[i]);
        if (cards[i] == card)
        {
            w_target = s;
            found = 1;
        }
        wsum += s;
    }
    if (!found || wsum <= 0.0)
        return 1.0 / (double)count;
    return w_target / wsum;
}

int mpf_bunching_compute_survival(const mpf_config_t *cfg, double out_survival[52])
{
    if (!cfg || !out_survival)
        return -1;
    for (int c = 0; c < 52; ++c)
        out_survival[c] = 1.0;
    if (!cfg->enable_card_bunching)
        return 0;
    int hole_cards = 2;
    if (cfg->rules == MPF_RULE_PLO4)
        hole_cards = 4;
    else if (cfg->rules == MPF_RULE_PLO5)
        hole_cards = 5;
    else if (cfg->rules == MPF_RULE_PLO6)
        hole_cards = 6;
    for (int p = 0; p < cfg->num_players; ++p)
    {
        int folded = 0;
        if (cfg->preflop.defined && cfg->preflop.has_active)
            folded = cfg->preflop.active[p] ? 0 : 1;
        if (!folded)
            continue;
        if (!cfg->folded_range_provided[p])
            continue;
        if (cfg->hole_specified[p] &&
            mask_to_array_count(cfg->hole[p]) >= hole_cards)
            continue;
        for (int c = 0; c < 52; ++c)
        {
            double f = cfg->folded_range_prob[p][c];
            if (f < 0.0)
                f = 0.0;
            if (f > 1.0)
                f = 1.0;
            out_survival[c] *= (1.0 - f);
        }
    }
    return 0;
}

/* Does the preflop-to-flop transition belong to chance, or did the caller
 * pin the board? Three configured board cards mean the flop is a given and
 * dealing it again would both contradict the caller and put cards back in the
 * deck that are already on the table. */
static int mpf_flop_is_chance(const mpf_state_t *st)
{
    return (st && st->enable_chance_nodes && st->known_board_cards < 3) ? 1 : 0;
}

static void mpf_enter_chance(mpf_state_t *st)
{
    mpf_street_t next_street = st->street;
    int deal = 1;
    if (st->street == MPF_STREET_PREFLOP)
    {
        next_street = MPF_STREET_FLOP;
        deal = 3;
    }
    else if (st->street == MPF_STREET_FLOP)
        next_street = MPF_STREET_TURN;
    else if (st->street == MPF_STREET_TURN)
        next_street = MPF_STREET_RIVER;
    st->street = next_street;
    st->chance_deal_cards = deal;
    st->chance_pending = 1;
    st->to_act = -1;
    st->util_ready = 0;
    MPF_PERF_INC_STATE(st, street_transitions);
}

/* Deal `count` board cards into a child state. The flop deals three at once
 * and the turn and river one; the arithmetic is the same either way, and
 * sharing it is what keeps the one-card path bit-for-bit what it was. */
static void mpf_chance_deal_cards_internal(const mpf_state_t *st, const int *cards,
                                           int count, mpf_state_t *out)
{
    /* out may be a cached child from a previous iteration holding the whole
     * betting/chance subtree dealt before. Release it so the re-deal never
     * drops live references (leak) and cleanup stays single-owner. */
    mpf_state_cleanup_internal(out);
    *out = *st;
    out->perf_stats = st->perf_stats;
    out->heap_owned = 1;
    out->util_ready = 0;
    /* Cloned state borrows the shared sparse index but never owns it. */
    out->owns_stack_index = 0;
    out->owns_abstraction_model = 0;
    for (int i = 0; i < MPF_TREE_ACTION_MAX; ++i)
        out->action_cache[i] = NULL;
    for (int i = 0; i < 52; ++i)
        out->chance_children[i] = NULL;
    out->chance_children_count = 0;
    out->flop_children = NULL;
    out->flop_child_count = 0;
    for (int i = 0; i < count && out->board_revealed < 5; ++i)
    {
        out->board_cards[out->board_revealed] = cards[i];
        out->board_revealed += 1;
    }
    mpf_update_board(out, out->board_revealed);
    out->known_board_cards = out->board_revealed;
    out->chance_pending = 0;
    out->chance_deal_cards = 0;
    mpf_reset_round(out, out->street);
    out->first_to_act = mpf_first_player_after(out, out->button_index + 1);
    out->to_act = out->first_to_act;
    if (out->tree_enabled && out->tree && out->tree_node_idx >= 0)
        mpf_apply_tree_node(out, out->tree_node_idx);
}

static void mpf_chance_deal_internal(const mpf_state_t *st, int card_idx, mpf_state_t *out)
{
    mpf_chance_deal_cards_internal(st, &card_idx, 1, out);
}

/* ===== state wrapper helpers ========================================== */

static mpf_state_t *mpf_wrapper_state(cfr_game_t *game, uint64_t key)
{
    mpf_state_t *root = (mpf_state_t *)game->game_data;
    if (root && root->keyed_mode)
        return mpf_key_map_lookup(key, root);
    return (mpf_state_t *)(uintptr_t)key;
}

static int mpf_current_player_wrapper(cfr_game_t *game, uint64_t key, void *user)
{
    const mpf_state_t *st = mpf_wrapper_state(game, key);
    (void)user;
    return st ? st->to_act : -1;
}

static int mpf_is_terminal_wrapper(cfr_game_t *game, uint64_t key, void *user)
{
    const mpf_state_t *st = mpf_wrapper_state(game, key);
    (void)user;
    return mpf_is_terminal(st);
}

static double mpf_get_utility_wrapper(cfr_game_t *game, uint64_t key, int player, void *user)
{
    mpf_state_t *st = mpf_wrapper_state(game, key);
    (void)user;
    if (!st)
        return 0.0;
    if (!st->util_ready)
        mpf_compute_utilities(st);
    if (player < 0 || player >= st->num_players)
        return 0.0;
    return st->utilities[player];
}

/* FEAT-06: effective all-in + dynamic STPR helpers. */

/* Total chips a raise candidate at index `raise_idx` forces the player to put
 * into the pot (call portion + the new raise increment). The increment is an
 * absolute amount, or a fraction of the current pot when pot-sizing is on. */
static double mpf_raise_total_amount(const mpf_state_t *st, int raise_idx, double need)
{
    if (raise_idx < 0 || raise_idx >= st->bet_size_count)
        return need;
    double inc = st->bet_sizes[raise_idx];
    if (st->enable_pot_sizing)
        inc *= (st->pot > MPF_EPS ? st->pot : (st->current_bet + st->to_call));
    if (inc < 0.0)
        inc = 0.0;
    double pay = need + inc;
    if (pay > st->stacks[st->to_act])
        pay = st->stacks[st->to_act];
    if (pay < need + MPF_EPS)
        pay = need;
    return pay;
}

/* Stack left after committing `pay` chips. */
static double mpf_remaining_after(const mpf_state_t *st, double pay)
{
    double rem = st->stacks[st->to_act] - pay;
    return rem < 0.0 ? 0.0 : rem;
}

/* Dynamic Stack-to-Pot Ratio at the current node: effective stack behind the
 * acting player divided by the current pot. Recomputed at every decision node
 * rather than seeded from the starting stack (FEAT-06). */
static double mpf_compute_stpr(const mpf_state_t *st)
{
    double eff_stack = st->stacks[st->to_act];
    if (eff_stack < 0.0)
        eff_stack = 0.0;
    if (st->pot <= MPF_EPS)
        return eff_stack > MPF_EPS ? INFINITY : 0.0;
    return eff_stack / st->pot;
}

/* True when a remaining stack of `remaining` chips is small enough to trigger
 * MonkerSolver-style effective all-in: remaining <= threshold% * pot. */
static int mpf_is_effective_all_in(const mpf_state_t *st, double remaining)
{
    if (!st->is_pot_limit)
        return 0;
    double threshold = st->committal_threshold_percent;
    if (threshold <= 0.0)
        threshold = 100.0; /* default 1.0 * pot == 100% */
    double limit = st->pot * (threshold / 100.0);
    return remaining <= limit + MPF_EPS;
}

static int mpf_get_actions_wrapper(cfr_game_t *game, uint64_t key, int *out_actions, int max_actions, void *user)
{
    const mpf_state_t *st = mpf_wrapper_state(game, key);
    (void)user;
    if (!st)
        return 0;
    if (mpf_is_terminal(st))
        return 0;
    int player = st->to_act;
    if (player < 0 || player >= st->num_players)
        return 0;
    if (!st->active[player])
        return 0;
    if (st->stacks[player] <= MPF_EPS)
        return 0;

    /* Dynamic STPR at this decision node. */
    ((mpf_state_t *)st)->stpr = mpf_compute_stpr(st);

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
        /* Track emitted raise chip amounts so duplicate sizings (e.g. a 100%
         * pot bet that equals an all-in) are pruned exactly once. */
        double emitted[MPF_MAX_BET_SIZES];
        int emitted_count = 0;
        int all_in_emitted = 0;

        for (int i = 0; i < st->bet_size_count; ++i)
        {
            if (count >= max_actions)
                break;
            double pay = mpf_raise_total_amount(st, i, need);
            double remaining = mpf_remaining_after(st, pay);

            /* Collapse into an all-in ONLY when the raise was already capped by
             * the stack, i.e. the player could not make the full sizing anyway.
             * In that case MPF_ACTION_ALL_IN commits the same (legal) amount as
             * the capped raise, so it never becomes an illegal pot-limit overbet.
             * A raise that leaves a short remainder but is still within the
             * player's stack stays a normal (legal) raise candidate. */
            int stack_capped = (pay >= st->stacks[player] - MPF_EPS);
            if (stack_capped && mpf_is_effective_all_in(st, remaining))
            {
                if (!all_in_emitted)
                {
                    if (count < max_actions)
                        out_actions[count] = MPF_ACTION_ALL_IN;
                    count++;
                    all_in_emitted = 1;
                }
                continue;
            }

            /* Prune duplicate chip amounts. */
            int dup = 0;
            for (int j = 0; j < emitted_count; ++j)
            {
                if (fabs(emitted[j] - pay) < MPF_EPS)
                {
                    dup = 1;
                    break;
                }
            }
            if (dup)
                continue;
            if (emitted_count < MPF_MAX_BET_SIZES)
                emitted[emitted_count++] = pay;

            if (count < max_actions)
                out_actions[count] = MPF_ACTION_RAISE_BASE + i;
            count++;
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

static void mpf_state_release_chance_children(mpf_state_t *st)
{
    for (int i = 0; i < st->chance_children_count && i < 52; ++i)
    {
        mpf_state_t *child = st->chance_children[i];
        if (child)
        {
            mpf_state_cleanup_internal(child);
            if (child->heap_owned)
                free(child);
            st->chance_children[i] = NULL;
        }
    }
    st->chance_children_count = 0;
}

static void mpf_apply_action_internal(const mpf_state_t *st, int action, mpf_state_t *out)
{
    mpf_state_t *saved_cache[MPF_TREE_ACTION_MAX];
    memcpy(saved_cache, out->action_cache, sizeof(saved_cache));
    int heap_owned = out->heap_owned;
    /* out may be a cached slot whose previous incarnation was a chance node;
     * its dealt subtree must not leak (it is freed before the morph). */
    mpf_state_release_chance_children(out);
    *out = *st;
    out->perf_stats = st->perf_stats;
    memcpy(out->action_cache, saved_cache, sizeof(saved_cache));
    out->heap_owned = heap_owned;
    /* Cloned state borrows the shared sparse index but never owns it. */
    out->owns_stack_index = 0;
    out->util_ready = 0;
    out->chance_children_count = 0;
    for (int i = 0; i < 52; ++i)
        out->chance_children[i] = NULL;
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
    else if (action == MPF_ACTION_ALL_IN)
    {
        /* Effective all-in candidate (FEAT-06): commit the entire stack. */
        double pay = out->stacks[player];
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
        else if (out->keyed_mode && out->enable_chance_nodes &&
                 (out->street == MPF_STREET_FLOP || out->street == MPF_STREET_TURN))
        {
            /* FEAT-03: the next street is a real chance node; the solver
             * enumerates every unseen card instead of a fixed runout. */
            mpf_enter_chance(out);
        }
        else if (out->keyed_mode && out->street == MPF_STREET_PREFLOP &&
                 mpf_flop_is_chance(out))
        {
            /* CHN-02: the flop is dealt as one combination of three. */
            mpf_enter_chance(out);
        }
        else
        {
            mpf_advance_street(out);
        }
    }
}

static uint64_t mpf_apply_action_wrapper(cfr_game_t *game, uint64_t key, int action, void *user)
{
    mpf_state_t *st = mpf_wrapper_state(game, key);
    mpf_perf_stats_t *perf = st ? st->perf_stats : NULL;
    (void)user;
    if (!st)
        return 0;
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
    /* FEAT-10 (#146): child inherits the shared sparse stack index and gets
       its own (cached) config id so the infoset key distinguishes stacks. */
    if (next_state)
    {
        next_state->stack_index = st ? st->stack_index : NULL;
        mpf_state_resolve_cfg_id(next_state);
    }
    if (st && st->keyed_mode && next_state)
        mpf_key_map_register(mpf_infoset_key(next_state), next_state);
    return next_state ? mpf_state_key(next_state) : 0;
}

/* ===== FEAT-03: chance node wrappers ================================== */

pe_chance_kind_t mpf_state_chance_kind(const mpf_state_t *state)
{
    if (!state)
        return PE_CHANCE_NONE;

    /* The root deals first: a state cannot be waiting for a board card before
       anyone has been given a hand. */
    if (state->private_pending)
        return PE_CHANCE_PRIVATE_HANDS;

    if (!state->chance_pending)
        return PE_CHANCE_NONE;

    /* A flop is three cards dealt at once. Dealing them one at a time would
       reach the same boards but weight each of them six times — once per
       ordering — so the two kinds are genuinely different nodes and not a
       convenience. chance_deal_cards is what the transition recorded. */
    if (state->chance_deal_cards == 3)
        return PE_CHANCE_FLOP_THREE;
    return PE_CHANCE_BOARD_ONE;
}

const mpf_state_t *mpf_state_for_key(cfr_game_t *game, uint64_t key)
{
    if (!game)
        return NULL;
    return mpf_wrapper_state(game, key);
}

static int mpf_is_chance_wrapper(cfr_game_t *game, uint64_t key, void *user)
{
    const mpf_state_t *st = mpf_wrapper_state(game, key);
    (void)user;
    return (mpf_state_chance_kind(st) != PE_CHANCE_NONE) ? 1 : 0;
}

static int mpf_get_chance_outcomes_wrapper(cfr_game_t *game, uint64_t key, void *user)
{
    const mpf_state_t *st = mpf_wrapper_state(game, key);
    (void)user;
    switch (mpf_state_chance_kind(st))
    {
    case PE_CHANCE_PRIVATE_HANDS:
        return st->private_deal_count;
    case PE_CHANCE_BOARD_ONE:
    {
        int cards[52];
        return mpf_unused_cards(st, cards, 52);
    }
    case PE_CHANCE_FLOP_THREE:
    {
        int cards[52];
        int count = mpf_unused_cards(st, cards, 52);
        uint64_t combos = pe_comb_count((unsigned)count, 3);
        if (combos == 0 || combos > MPF_MAX_FLOP_OUTCOMES)
            return 0;
        return (int)combos;
    }
    case PE_CHANCE_NONE:
    case PE_CHANCE_DRAW_N:
    case PE_CHANCE_KIND_COUNT:
    default:
        return 0;
    }
}

static uint64_t mpf_apply_chance_wrapper(cfr_game_t *game, uint64_t key, int outcome, void *user)
{
    mpf_state_t *st = mpf_wrapper_state(game, key);
    (void)user;
    if (!st)
        return 0;

    if (mpf_state_chance_kind(st) == PE_CHANCE_PRIVATE_HANDS)
    {
        /* Deal the private hands. The child is cached and owned by the root,
           exactly as a board-card child is, so the traversal's release_state
           stays a no-op for chance children. */
        mpf_state_t *dealt;
        if (outcome < 0 || outcome >= st->private_deal_count)
            return 0;
        dealt = st->private_children[outcome];
        if (!dealt)
        {
            dealt = (mpf_state_t *)malloc(sizeof(mpf_state_t));
            if (!dealt)
                return 0;
            *dealt = *st;
            dealt->heap_owned = 1;
            dealt->owns_stack_index = 0;
            dealt->private_deals = NULL;
            dealt->private_children = NULL;
            dealt->private_deal_count = 0;
            dealt->private_pending = 0;
            dealt->util_ready = 0;
            for (int i = 0; i < MPF_TREE_ACTION_MAX; ++i)
                dealt->action_cache[i] = NULL;
            for (int i = 0; i < 52; ++i)
                dealt->chance_children[i] = NULL;
            dealt->chance_children_count = 0;
            dealt->flop_children = NULL;
            dealt->flop_child_count = 0;
            for (int i = 0; i < st->num_players; ++i)
                dealt->hole[i] = st->private_deals[outcome].hole[i];
            st->private_children[outcome] = dealt;
        }
        /* The dealt state must be reachable by its infoset key, exactly like a
           board-card child: keyed mode is what the storage indexes by, and an
           unregistered state is one the traversal cannot look up again. */
        if (st->keyed_mode)
            mpf_key_map_register(mpf_infoset_key(dealt), dealt);
        return mpf_state_key(dealt);
    }

    if (mpf_state_chance_kind(st) == PE_CHANCE_FLOP_THREE)
    {
        int cards[52];
        int count = mpf_unused_cards(st, cards, 52);
        uint64_t combos = pe_comb_count((unsigned)count, 3);
        unsigned flop[PE_COMB_MAX_K];
        int dealt[3];
        mpf_state_t *child;
        if (combos == 0 || combos > MPF_MAX_FLOP_OUTCOMES)
            return 0;
        if (outcome < 0 || (uint64_t)outcome >= combos)
            return 0;
        if (pe_comb_unrank((unsigned)count, 3, (uint64_t)outcome, flop) != PE_SOLVER_OK)
            return 0;
        if (!st->flop_children)
        {
            st->flop_children =
                (mpf_state_t **)calloc((size_t)combos, sizeof(mpf_state_t *));
            if (!st->flop_children)
                return 0;
            st->flop_child_count = (int)combos;
        }
        if (outcome >= st->flop_child_count)
            return 0;
        for (int i = 0; i < 3; ++i)
            dealt[i] = cards[flop[i]];
        child = st->flop_children[outcome];
        if (!child)
        {
            child = (mpf_state_t *)malloc(sizeof(mpf_state_t));
            if (!child)
                return 0;
            memset(child, 0, sizeof(*child));
            st->flop_children[outcome] = child;
        }
        mpf_chance_deal_cards_internal(st, dealt, 3, child);
        if (st->keyed_mode)
            mpf_key_map_register(mpf_infoset_key(child), child);
        return mpf_state_key(child);
    }

    /* Past this point the node deals one board card, which is the only kind
       chance_children[52] can index. */
    if (mpf_state_chance_kind(st) != PE_CHANCE_BOARD_ONE)
        return 0;
    int cards[52];
    int count = mpf_unused_cards(st, cards, 52);
    if (outcome < 0 || outcome >= count)
        return 0;
    mpf_state_t *child = st->chance_children[outcome];
    if (!child)
    {
        child = (mpf_state_t *)malloc(sizeof(mpf_state_t));
        if (!child)
            return 0;
        memset(child, 0, sizeof(*child));
        st->chance_children[outcome] = child;
        if (st->chance_children_count <= outcome)
            st->chance_children_count = outcome + 1;
    }
    mpf_chance_deal_internal(st, cards[outcome], child);
    if (st->keyed_mode)
        mpf_key_map_register(mpf_infoset_key(child), child);
    return mpf_state_key(child);
}

/*
 * CHN-03: draw one chance outcome by direct sampling.
 *
 * The deck deals uniformly; anything an estimator biases (a bunched board
 * card, a weighted private deal) is sampled at its actual probability and the
 * importance ratio carries the correction back to the uniform scale. A flop
 * combination is drawn uniformly, so its ratio is exactly 1.0 and a sweep of
 * the sampler reproduces the enumerated distribution one-for-one.
 */
int mpf_chance_sample(const mpf_state_t *st, pe_rng_t *rng,
                      pe_chance_sample_t *out)
{
    if (!out)
        return -1;
    out->outcome = 0;
    out->importance_ratio = 1.0;
    if (!st || !rng)
        return -1;

    switch (mpf_state_chance_kind(st))
    {
    case PE_CHANCE_PRIVATE_HANDS:
    {
        int n = st->private_deal_count;
        if (n <= 0)
            return -1;
        double total = 0.0;
        for (int i = 0; i < n; ++i)
            total += st->private_deals[i].weight;
        if (!(total > 0.0))
            return -1;
        double u = pe_rng_uniform01(rng) * total;
        double acc = 0.0;
        int pick = n - 1;
        for (int i = 0; i < n; ++i)
        {
            acc += st->private_deals[i].weight;
            if (u < acc)
            {
                pick = i;
                break;
            }
        }
        out->outcome = pick;
        out->importance_ratio =
            (1.0 / (double)n) /
            (st->private_deals[pick].weight / total);
        return 0;
    }

    case PE_CHANCE_FLOP_THREE:
    {
        int cards[52];
        int count = mpf_unused_cards(st, cards, 52);
        uint64_t combos = pe_comb_count((unsigned)count, 3);
        if (combos == 0 || combos > MPF_MAX_FLOP_OUTCOMES)
            return -1;
        out->outcome = (int)pe_rng_below(rng, (uint32_t)combos);
        out->importance_ratio = 1.0; /* uniform over combinations */
        return 0;
    }

    case PE_CHANCE_BOARD_ONE:
    {
        int cards[52];
        int count = mpf_unused_cards(st, cards, 52);
        if (count <= 0)
            return -1;
        if (!mpf_bunching_enabled(st))
        {
            out->outcome = (int)pe_rng_below(rng, (uint32_t)count);
            out->importance_ratio = 1.0;
            return 0;
        }
        double wsum = 0.0;
        double weights[52];
        for (int i = 0; i < count; ++i)
        {
            weights[i] = mpf_bunching_state_survival(st, cards[i]);
            wsum += weights[i];
        }
        if (!(wsum > 0.0))
        {
            out->outcome = (int)pe_rng_below(rng, (uint32_t)count);
            out->importance_ratio = 1.0;
            return 0;
        }
        double u = pe_rng_uniform01(rng) * wsum;
        double acc = 0.0;
        int pick = count - 1;
        for (int i = 0; i < count; ++i)
        {
            acc += weights[i];
            if (u < acc)
            {
                pick = i;
                break;
            }
        }
        out->outcome = pick;
        out->importance_ratio =
            (1.0 / (double)count) / (weights[pick] / wsum);
        return 0;
    }

    case PE_CHANCE_NONE:
    case PE_CHANCE_DRAW_N:
    case PE_CHANCE_KIND_COUNT:
    default:
        return -1;
    }
}

static int mpf_is_terminal(const mpf_state_t *st)
{
    if (!st)
        return 1;
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

/* The unmatched part of the active player's wager is returned before rake.
 * Folded players cannot create an uncalled excess: only the surviving
 * player's highest contribution can be unmatched. */
static double mpf_uncalled_amount(const mpf_state_t *st)
{
    if (!st || mpf_active_count(st) != 1)
        return 0.0;
    int active_player = -1;
    for (int i = 0; i < st->num_players; ++i)
        if (st->active[i])
            active_player = i;
    if (active_player < 0)
        return 0.0;

    double second_highest = 0.0;
    for (int i = 0; i < st->num_players; ++i)
    {
        if (i == active_player)
            continue;
        if (st->invested[i] > second_highest)
            second_highest = st->invested[i];
    }
    double uncalled = st->invested[active_player] - second_highest;
    return uncalled > MPF_EPS ? uncalled : 0.0;
}

/* Net pot distributed at terminal nodes. Rake is deducted unless the
 * hand never saw a flop and no-flop-no-drop is configured. */
static double mpf_terminal_pot(const mpf_state_t *st)
{
    const rake_config_t *r = &st->rake;
    const double uncalled = mpf_uncalled_amount(st);
    if (r->percentage <= 0.0)
        return st->pot;
    if (r->no_flop_no_drop && st->street < MPF_STREET_FLOP && st->board_revealed == 0)
        return st->pot;
    return pe_apply_rake_excluding_uncalled(st->pot, uncalled, r);
}

static void mpf_mark_winner_fold(mpf_state_t *st, int winner)
{
    for (int i = 0; i < st->num_players; ++i)
        st->utilities[i] = -st->invested[i];
    if (winner >= 0)
        st->utilities[winner] += mpf_terminal_pot(st);
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
        st->utilities[active_players[0]] += mpf_terminal_pot(st);
        st->util_ready = 1;
        return;
    }

    mask_t board = MASK_EMPTY;
    for (int i = 0; i < st->board_revealed; ++i)
        board = mask_set(board, st->board_cards[i]);

    /* Evaluate every active hand once so it can be reused for each side pot. */
    eval_t hand_val[MPF_MAX_PLAYERS];
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
        hand_val[p] = value;
    }

    /* Award each side pot to the best hand among the players who can
       win it.  The pot contributed by a layer of invested amounts is
       shared by the players who invested at least that much; folded
       players' chips stay in the pot and go to the winner, but a layer
       contributed by players no remaining player can cover is refunded
       (matching the poker rule for uncalled bets). */
    {
        /* Collect the distinct total investment levels, ascending. */
        double levels[MPF_MAX_PLAYERS];
        int n_levels = 0;
        for (int i = 0; i < st->num_players; ++i)
        {
            if (st->invested[i] <= MPF_EPS)
                continue;
            double lv = st->invested[i];
            int dup = 0;
            for (int j = 0; j < n_levels; ++j)
            {
                if (levels[j] == lv)
                {
                    dup = 1;
                    break;
                }
            }
            if (!dup)
                levels[n_levels++] = lv;
        }
        /* Ascending insertion sort (n_levels <= MPF_MAX_PLAYERS). */
        for (int i = 1; i < n_levels; ++i)
        {
            double lv = levels[i];
            int j = i - 1;
            while (j >= 0 && levels[j] > lv)
            {
                levels[j + 1] = levels[j];
                --j;
            }
            levels[j + 1] = lv;
        }

        double prev_level = 0.0;
        for (int li = 0; li < n_levels; ++li)
        {
            double layer = levels[li] - prev_level;
            if (layer <= MPF_EPS)
                continue;

            /* Contributors: every player (folded included) whose chips
               reach this layer. */
            int n_layer_players = 0;
            int layer_players[MPF_MAX_PLAYERS];
            for (int i = 0; i < st->num_players; ++i)
            {
                if (st->invested[i] >= levels[li] - MPF_EPS)
                    layer_players[n_layer_players++] = i;
            }

            /* Active players who are entitled to this layer. */
            eval_t best = 0;
            int winners[MPF_MAX_PLAYERS];
            int win_cnt = 0;
            for (int k = 0; k < n_layer_players; ++k)
            {
                int p = layer_players[k];
                if (!st->active[p])
                    continue;
                if (win_cnt == 0 || hand_val[p] > best)
                {
                    best = hand_val[p];
                    winners[0] = p;
                    win_cnt = 1;
                }
                else if (hand_val[p] == best)
                {
                    winners[win_cnt++] = p;
                }
            }

            if (win_cnt > 0)
            {
                /* Winner(s) of this layer take it all, including folded
                   players' contributions, with rake applied. */
                double layer_pot = pe_apply_rake(layer * (double)n_layer_players,
                                                &st->rake);
                double share = layer_pot / (double)win_cnt;
                for (int i = 0; i < win_cnt; ++i)
                    st->utilities[winners[i]] += share;
            }
            else
            {
                /* No surviving player covered this layer (uncalled bet):
                   refund the layer to the players who put it in. */
                for (int k = 0; k < n_layer_players; ++k)
                    st->utilities[layer_players[k]] += layer;
            }
            prev_level = levels[li];
        }
    }
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
        node->state_key = mpf_node_storage_key(st);
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
        node->state_key = mpf_node_storage_key(st);
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

/*
 * Resolve the private ranges into fixed holes (RNG-02).
 *
 * Card indices are the same 0..51 in both representations, so the conversion
 * is a bit-for-bit walk rather than a rank/suit round trip.
 *
 * A one-combo range is a fixed hand written another way and is materialised
 * here. Anything wider is left alone: the root private chance node deals it
 * (RNG-03), and writing a hole here would pin the player to one combo.
 *
 * Returns 0, or -1 on a range that is unprepared or empty.
 */
static int mpf_resolve_ranges(const mpf_config_t *cfg, mask_t *out_hole,
                              int *out_specified)
{
    for (int p = 0; p < cfg->num_players && p < MPF_MAX_PLAYERS; ++p)
    {
        pe_range_view_t view;
        mask_t m = MASK_EMPTY;

        if (cfg->range[p] == NULL)
            continue;

        if (!pe_solver_range_is_prepared(cfg->range[p], 1e-9))
            return -1;

        view = pe_solver_range_view(cfg->range[p]);
        if (view.count != 1)
            continue;   /* wider ranges are dealt by the root chance node */

        for (int c = 0; c < MODERN_DECK_SIZE; ++c)
            if (StdDeck_CardMask_CARD_IS_SET(view.combos[0].hand, c))
                m = mask_set(m, c);

        out_hole[p] = m;
        out_specified[p] = 1;
    }
    return 0;
}

/*
 * Build the joint private deals (RNG-03).
 *
 * The cartesian product of the players' ranges, minus every combination that
 * is impossible because two players would hold the same card, or because a
 * card is already on the board. That removal is not a detail: it is the whole
 * reason a range solve differs from solving each hand independently. Two
 * players both holding "AA" have 36 nominal pairs and 6 real ones.
 *
 * Weights multiply and are then renormalised over what survives, so the deals
 * form a probability distribution — which is what the chance node needs and
 * what makes the counterfactual reach probabilities mean anything.
 *
 * A player with no range contributes their fixed hole, as a single certain
 * option. A player with neither contributes an empty hand, which is what the
 * model already did.
 *
 * Returns 0 on success, -1 when the product is too large to enumerate or no
 * combination survives.
 */
#define MPF_MAX_PRIVATE_DEALS (1u << 20)

static int mpf_range_option_count(const mpf_config_t *cfg, int p)
{
    if (cfg->range[p] != NULL)
        return (int)pe_solver_range_view(cfg->range[p]).count;
    return 1;   /* the fixed hole, or an empty hand */
}

static void mpf_range_option(const mpf_config_t *cfg, int p, int idx,
                             mask_t *out_hole, double *out_weight)
{
    if (cfg->range[p] != NULL)
    {
        pe_range_view_t view = pe_solver_range_view(cfg->range[p]);
        mask_t m = MASK_EMPTY;
        for (int c = 0; c < MODERN_DECK_SIZE; ++c)
            if (StdDeck_CardMask_CARD_IS_SET(view.combos[idx].hand, c))
                m = mask_set(m, c);
        *out_hole = m;
        *out_weight = view.combos[idx].weight;
        return;
    }
    *out_hole = cfg->hole[p];
    *out_weight = 1.0;
}

static int mpf_build_private_deals(const mpf_config_t *cfg, mask_t board,
                                   mpf_private_deal_t **out_deals,
                                   int *out_count)
{
    int counts[MPF_MAX_PLAYERS];
    int idx[MPF_MAX_PLAYERS];
    unsigned long long product = 1;
    mpf_private_deal_t *deals;
    int capacity;
    int n = 0;
    double total = 0.0;
    int p;

    for (p = 0; p < cfg->num_players; ++p)
    {
        counts[p] = mpf_range_option_count(cfg, p);
        if (counts[p] <= 0)
            return -1;
        idx[p] = 0;
        product *= (unsigned long long)counts[p];
        if (product > MPF_MAX_PRIVATE_DEALS)
            return -1;   /* refused rather than truncated */
    }

    capacity = (int)product;
    deals = (mpf_private_deal_t *)calloc((size_t)capacity, sizeof(mpf_private_deal_t));
    if (!deals)
        return -1;

    for (;;)
    {
        mask_t used = board;
        double w = 1.0;
        int ok = 1;

        for (p = 0; p < cfg->num_players && ok; ++p)
        {
            mask_t h;
            double pw;
            mpf_range_option(cfg, p, idx[p], &h, &pw);
            /* A card cannot be in two hands, nor in a hand and on the board. */
            if ((h & used) != 0)
                ok = 0;
            else
            {
                used |= h;
                w *= pw;
                deals[n].hole[p] = h;
            }
        }

        if (ok && w > 0.0)
        {
            deals[n].weight = w;
            total += w;
            n++;
        }

        /* Odometer over the players' option indices. */
        for (p = cfg->num_players - 1; p >= 0; --p)
        {
            if (++idx[p] < counts[p])
                break;
            idx[p] = 0;
        }
        if (p < 0)
            break;
    }

    if (n == 0 || !(total > 0.0))
    {
        free(deals);
        return -1;
    }

    for (int i = 0; i < n; ++i)
        deals[i].weight /= total;

    *out_deals = deals;
    *out_count = n;
    return 0;
}

/*
 * Release what mpf_build_game has allocated, on a path that then fails.
 *
 * The function has always allocated a stack index early and had no way to
 * give it back; nothing noticed, because it had no failure path after that
 * point. RNG-03 added two — an unprepared range and a configuration whose
 * every deal is impossible — and leaks(1) noticed immediately.
 */
static int mpf_build_fail(mpf_state_t *st)
{
    if (st->owns_abstraction_model && st->abstraction_model)
    {
        const pe_abstraction_ops_t *ops = pe_abstraction_ops();
        if (ops && ops->destroy)
            ops->destroy((pe_abstraction_model_t *)st->abstraction_model);
        st->abstraction_model = NULL;
        st->owns_abstraction_model = 0;
    }
    if (st->owns_stack_index && st->stack_index)
    {
        mpf_stack_index_destroy(st->stack_index);
        st->stack_index = NULL;
        st->owns_stack_index = 0;
    }
    free(st->private_children);
    free(st->private_deals);
    st->private_children = NULL;
    st->private_deals = NULL;
    st->private_deal_count = 0;
    free(st->flop_children);
    st->flop_children = NULL;
    st->flop_child_count = 0;
    return -1;
}

static void mpf_collect_abstraction_hands(const int *cards, int card_count,
                                          int need, int start, int depth,
                                          mask_t current, mask_t *out,
                                          size_t cap, size_t *count)
{
    if (*count >= cap)
        return;
    if (depth == need)
    {
        out[(*count)++] = current;
        return;
    }
    int remaining = need - depth;
    for (int i = start; i <= card_count - remaining && *count < cap; ++i)
    {
        mpf_collect_abstraction_hands(cards, card_count, need, i + 1,
                                      depth + 1, mask_set(current, cards[i]),
                                      out, cap, count);
    }
}

/* Train the default MPF model only when a postflop board is already known.
 * Chance-node games may provide a pre-trained model through the config; a
 * future street can then use it without mutating the state during keying. */
static int mpf_prepare_abstraction(const mpf_config_t *cfg, mpf_state_t *st)
{
    if (!cfg || !st || st->strength_buckets_per_street <= 0)
        return 0;
    if (cfg->abstraction_model)
    {
        st->abstraction_model = cfg->abstraction_model;
        return 0;
    }
    if (st->board_revealed < 3 || st->board_revealed > 5 ||
        (st->total_hole_cards != 2 && st->total_hole_cards != 4))
        return 0;

    int cards[MODERN_DECK_SIZE];
    int card_count = 0;
    for (int card = 0; card < MODERN_DECK_SIZE; ++card)
        if (!mask_is_set(st->board_mask, card))
            cards[card_count++] = card;

    size_t cap = (st->total_hole_cards == 2) ? 1326u : 1024u;
    mask_t *hands = (mask_t *)calloc(cap, sizeof(*hands));
    if (!hands)
        return -1;
    size_t hand_count = 0;
    mpf_collect_abstraction_hands(cards, card_count, st->total_hole_cards,
                                  0, 0, MASK_EMPTY, hands, cap, &hand_count);

    pe_abstraction_config_t acfg;
    memset(&acfg, 0, sizeof(acfg));
    acfg.strength.n_buckets = st->strength_buckets_per_street;
    acfg.strength.hole_cards = st->total_hole_cards;
    acfg.strength.max_samples = (uint32_t)cap;
    acfg.texture_filter = (pe_texture_filter_level_t)st->texture_filter_level;
    const pe_abstraction_ops_t *ops = pe_abstraction_ops();
    pe_abstraction_model_t *model = NULL;
    int rc = (ops && ops->train && hand_count > 0) ?
        ops->train(&model, st->ctx, st->board_mask, hands, hand_count, &acfg) : -1;
    free(hands);
    if (rc != 0 || !model)
        return -1;
    st->abstraction_model = model;
    st->owns_abstraction_model = 1;
    return 0;
}

int mpf_build_game(const mpf_config_t *cfg, cfr_game_t *out_game, mpf_state_t *out_state)
{
    if (!cfg || !cfg->ctx || !out_game || !out_state)
        return -1;
    if (cfg->num_players < 2 || cfg->num_players > MPF_MAX_PLAYERS)
        return -1;

    /* NOTE: callers that reuse the same out_state across mpf_build_game
       calls (e.g. test_mpf_tree's setup_plo4, test_mpf_perf_stats) must
       mpf_state_cleanup() it first; we cannot safely free a possibly-
       uninitialized previous index here, so we just wipe the struct. */
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
    out_state->strength_buckets_per_street = cfg->strength_buckets_per_street;
    out_state->texture_filter_level = cfg->texture_filter_level;
    out_state->abstraction_model = cfg->abstraction_model;
    out_state->owns_abstraction_model = 0;
    out_state->rake = cfg->rake;
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

    /* FEAT-06: pot-limit effective all-in + dynamic STPR. is_pot_limit:
     *  0 (or any non-{0,1} value, e.g. after a zero-initialized config) and
     *  -1 both mean AUTO-DERIVE from the rules (PLO4/5/6 => pot-limit). This
     *  keeps the primary PLO CLI working even when callers memset the config
     *  to zero and never set the field explicitly. 1 = forced on. */
    out_state->is_pot_limit = cfg->is_pot_limit;
    if (out_state->is_pot_limit == -1 || out_state->is_pot_limit == 0)
    {
        out_state->is_pot_limit =
            (cfg->rules == MPF_RULE_PLO4 || cfg->rules == MPF_RULE_PLO5 ||
             cfg->rules == MPF_RULE_PLO6) ? 1 : 0;
    }
    else if (out_state->is_pot_limit != 1)
    {
        out_state->is_pot_limit = 0;
    }
    out_state->committal_threshold_percent = cfg->committal_threshold_percent;
    if (out_state->committal_threshold_percent <= 0.0)
        out_state->committal_threshold_percent = 100.0; /* default 1.0 * pot */
    out_state->stpr = 0.0;

    /* FEAT-10 (#146): own the sparse stack-config index at the root so the
       whole traversal shares one deterministic config-id namespace. */
    /* FEAT-10 (#146): own the sparse stack-config index at the root so the
       whole traversal shares one deterministic config-id namespace. The
       root config id is resolved at the END of build_game (see below) once
       stacks/round_contrib/active are fully populated. */
    out_state->stack_index = mpf_stack_index_create(256);
    out_state->owns_stack_index = (out_state->stack_index != NULL) ? 1 : 0;

    out_state->bet_size_count = cfg->bet_size_count_common;
    if (out_state->bet_size_count > MPF_MAX_BET_SIZES)
        out_state->bet_size_count = MPF_MAX_BET_SIZES;
    for (int i = 0; i < out_state->bet_size_count; ++i)
        out_state->bet_sizes[i] = cfg->bet_sizes_common[i];
    out_state->base_bet_size_count = out_state->bet_size_count;
    for (int i = 0; i < out_state->base_bet_size_count; ++i)
        out_state->base_bet_sizes[i] = out_state->bet_sizes[i];
    out_state->base_enable_pot_sizing = out_state->enable_pot_sizing;

    /* A one-combo range is a fixed hand written another way; anything wider is
       refused until RNG-03 provides the root private chance. */
    mask_t resolved_hole[MPF_MAX_PLAYERS];
    int resolved_specified[MPF_MAX_PLAYERS];
    for (int i = 0; i < MPF_MAX_PLAYERS; ++i)
    {
        resolved_hole[i] = cfg->hole[i];
        resolved_specified[i] = cfg->hole_specified[i];
    }
    if (mpf_resolve_ranges(cfg, resolved_hole, resolved_specified) != 0)
        return mpf_build_fail(out_state);

    /* Any range wider than one combo turns the root into a private-deal chance
       node. Below one, nothing changes and the model behaves as it always
       has — which is what keeps every existing configuration bit-identical. */
    int needs_private_deal = 0;
    for (int i = 0; i < cfg->num_players; ++i)
        if (cfg->range[i] != NULL && pe_solver_range_view(cfg->range[i]).count > 1)
            needs_private_deal = 1;

    for (int i = 0; i < cfg->num_players; ++i)
    {
        out_state->stacks[i] = cfg->stacks[i];
        out_state->active[i] = 1;
        out_state->hole[i] = resolved_hole[i];
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
    int reveal_at_start = (cfg->start_street == MPF_STREET_FLOP) ? 3 :
                          (cfg->start_street == MPF_STREET_TURN) ? 4 :
                          (cfg->start_street == MPF_STREET_RIVER) ? 5 : 0;
    out_state->known_board_cards = board_limit;
    int load_limit = board_limit;
    if (cfg->enable_chance_nodes && reveal_at_start > 0 && load_limit > reveal_at_start)
        load_limit = reveal_at_start; /* turn/river cards are dealt by chance */
    for (int i = 0; i < load_limit; ++i)
        out_state->board_cards[i] = cfg->board_cards[i];
    for (int i = load_limit; i < 5; ++i)
    {
        if (cfg->enable_chance_nodes && reveal_at_start > 0)
            out_state->board_cards[i] = -1;
        else
            out_state->board_cards[i] = cfg->board_cards[i];
    }

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
    out_state->enable_chance_nodes = cfg->enable_chance_nodes ? 1 : 0;
    out_state->keyed_mode = cfg->enable_chance_nodes ? 1 : 0;
    out_state->key_map_owner = out_state;
    out_state->chance_pending = 0;
    out_state->chance_deal_cards = 0;
    out_state->chance_children_count = 0;
    for (int i = 0; i < 52; ++i)
        out_state->chance_children[i] = NULL;
    out_state->flop_children = NULL;
    out_state->flop_child_count = 0;

    out_state->private_deals = NULL;
    out_state->private_children = NULL;
    out_state->private_deal_count = 0;
    out_state->private_pending = 0;
    if (needs_private_deal)
    {
        /* Keyed mode is required: the dealt states are heap clones, and the
           storage must index them by infoset key rather than by pointer. */
        out_state->keyed_mode = 1;
        if (mpf_build_private_deals(cfg, out_state->board_mask,
                                    &out_state->private_deals,
                                    &out_state->private_deal_count) != 0)
            return mpf_build_fail(out_state);
        out_state->private_children =
            (mpf_state_t **)calloc((size_t)out_state->private_deal_count,
                                   sizeof(mpf_state_t *));
        if (!out_state->private_children)
        {
            return mpf_build_fail(out_state);
        }
        out_state->private_pending = 1;
        /* Until the deal happens nobody holds anything: leaving the fixed
           holes in place would let an infoset key see cards the player has
           not been given. */
        for (int i = 0; i < cfg->num_players; ++i)
            if (cfg->range[i] != NULL && pe_solver_range_view(cfg->range[i]).count > 1)
                out_state->hole[i] = MASK_EMPTY;
    }
    /* FEAT-14 (#150): folded-range card bunching configuration. Copied into
       the state (and inherited by every derived child through the state
       clone in mpf_apply_action_internal / mpf_chance_deal_internal) so the
       chance-deal weights can be evaluated at any node of the traversal. */
    out_state->enable_card_bunching = cfg->enable_card_bunching ? 1 : 0;
    for (int p = 0; p < cfg->num_players; ++p)
    {
        out_state->folded_range_provided[p] = cfg->folded_range_provided[p] ? 1 : 0;
        for (int c = 0; c < 52; ++c)
            out_state->folded_range_prob[p][c] = cfg->folded_range_prob[p][c];
    }
    if (out_state->tree_enabled && out_state->tree)
    {
        out_state->tree_node_idx = out_state->tree->root_index;
        mpf_apply_tree_node(out_state, out_state->tree_node_idx);
    }
    else
    {
        mpf_restore_base_bets(out_state);
    }

    if (mpf_prepare_abstraction(cfg, out_state) != 0)
        return mpf_build_fail(out_state);

    /* FEAT-10 (#146): now that stacks/round_contrib/active are fully
       populated (including any tree-node or preconfig overrides), resolve
       the root's sparse stack-config id so the infoset key distinguishes
       asymmetrical stacks from the very first storage write. */
    mpf_state_resolve_cfg_id(out_state);

    out_game->initial_state = out_state;
    out_game->game_data = out_state;
    out_game->is_terminal = mpf_is_terminal_wrapper;
    out_game->get_utility = mpf_get_utility_wrapper;
    out_game->get_actions = mpf_get_actions_wrapper;
    out_game->apply_action = mpf_apply_action_wrapper;
    out_game->current_player = mpf_current_player_wrapper;
    out_game->get_street = mpf_get_street;
    out_game->is_chance = mpf_is_chance_wrapper;
    out_game->get_chance_outcomes = mpf_get_chance_outcomes_wrapper;
    out_game->get_chance_weight = mpf_get_chance_weight_wrapper;
    out_game->apply_chance = mpf_apply_chance_wrapper;
    /* FEAT-10 (#146): route storage lookups through the content-derived infoset
       key (which folds in the sparse stack-config id) instead of the raw state
       pointer, so asymmetric stack configs deduplicate into shared infosets.
       Wired only when not already in keyed_mode, because there state_key is
       already a content hash and reinterpreting it as a pointer would crash. */
    if (!out_state->keyed_mode)
        out_game->get_infoset_key = mpf_get_infoset_key_wrapper;
    out_game->num_players = cfg->num_players;
    out_game->state_size = sizeof(*out_state);

    if (out_state->keyed_mode)
    {
        mpf_key_map_register(mpf_infoset_key(out_state), out_state);
        out_game->initial_state = (void *)(uintptr_t)mpf_infoset_key(out_state);
    }

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
    for (int i = 0; i < state->chance_children_count && i < 52; ++i)
    {
        mpf_state_t *child = state->chance_children[i];
        if (child)
        {
            mpf_state_cleanup_internal(child);
            if (child->heap_owned)
                free(child);
            state->chance_children[i] = NULL;
        }
    }
    state->chance_children_count = 0;

    for (int i = 0; i < state->private_deal_count; ++i)
    {
        mpf_state_t *child = state->private_children ? state->private_children[i] : NULL;
        if (child)
        {
            mpf_state_cleanup_internal(child);
            if (child->heap_owned)
                free(child);
            state->private_children[i] = NULL;
        }
    }
    free(state->private_children);
    free(state->private_deals);
    state->private_children = NULL;
    state->private_deals = NULL;
    state->private_deal_count = 0;

    for (int i = 0; i < state->flop_child_count; ++i)
    {
        mpf_state_t *child = state->flop_children ? state->flop_children[i] : NULL;
        if (child)
        {
            mpf_state_cleanup_internal(child);
            if (child->heap_owned)
                free(child);
            state->flop_children[i] = NULL;
        }
    }
    free(state->flop_children);
    state->flop_children = NULL;
    state->flop_child_count = 0;
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
                node->state_key = mpf_node_storage_key(node->cache_slots[s].state);
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
    mpf_key_map_unregister_owner(state->key_map_owner ? state->key_map_owner : state);
    mpf_state_cleanup_internal(state);
    /* FEAT-10 (#146): only the owning root frees the shared sparse index. */
    if (state->owns_stack_index && state->stack_index)
    {
        mpf_stack_index_destroy(state->stack_index);
        state->stack_index = NULL;
        state->owns_stack_index = 0;
    }
    if (state->owns_abstraction_model && state->abstraction_model)
    {
        const pe_abstraction_ops_t *ops = pe_abstraction_ops();
        if (ops && ops->destroy)
            ops->destroy((pe_abstraction_model_t *)state->abstraction_model);
        state->abstraction_model = NULL;
        state->owns_abstraction_model = 0;
    }
    if (state->tree)
        mpf_tree_release_cache(state->tree, state);
}

void mpf_state_cleanup_cached(mpf_state_t *state)
{
    MPF_ADAPTER_DEBUG("MPF: mpf_state_cleanup_cached on %p\n", (void *)state);
    mpf_state_cleanup_internal(state);
}

/* FEAT-10 (#146): diagnostic accessors for the sparse stack-config index. */
size_t mpf_state_stack_index_count(const mpf_state_t *state)
{
    if (!state || !state->stack_index)
        return 0;
    return mpf_stack_index_count(state->stack_index);
}

size_t mpf_state_stack_index_capacity(const mpf_state_t *state)
{
    if (!state || !state->stack_index)
        return 0;
    return mpf_stack_index_capacity(state->stack_index);
}

uint64_t mpf_state_infoset_key(const mpf_state_t *state)
{
    if (!state)
        return 0;
    /* Ensure the sparse config id is resolved so the key matches what the
       solver stored under. */
    mpf_state_t *mut = (mpf_state_t *)state;
    mpf_state_resolve_cfg_id(mut);
    return mpf_infoset_key(state);
}
