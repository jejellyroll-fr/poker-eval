#include <poker_eval/engine/solvers/cfr/multiway_postflop_adapter.h>
#include <poker_eval/engine/solvers/cfr/cfr_core.h>
#include <poker_eval/engine/solvers/cfr/mpf_tree.h>
#include <poker_eval/core/eval_context.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <poker_eval/core/pthread_compat.h>

typedef struct
{
    mpf_tree_def_t *tree;
    const EvalContext *ctx;
    struct mpf_perf_stats_pool_t *pool;
    mpf_perf_stats_t *shard;
} worker_data_t;

static void *worker_thread(void *arg);
#endif

#define CHECK(cond, msg)               \
    do {                               \
        if (!(cond)) {                 \
            fprintf(stderr, "%s\n", msg); \
            goto fail;                 \
        }                              \
    } while (0)

static const char *k_tree_json =
    "{\n"
    "  \"version\": 1,\n"
    "  \"root\": \"root\",\n"
    "  \"betProfiles\": [\n"
    "    {\"id\": \"default\", \"sizes\": [3.0], \"pot_sizing\": false}\n"
    "  ],\n"
    "  \"nodes\": [\n"
    "    {\n"
    "      \"id\": \"root\",\n"
    "      \"type\": \"player\",\n"
    "      \"street\": \"PREFLOP\",\n"
    "      \"player\": 1,\n"
    "      \"bet_profile\": \"default\",\n"
    "      \"actions\": [\n"
    "        {\"type\": \"call\", \"next\": \"flop_chance\"},\n"
    "        {\"type\": \"raise\", \"size_index\": 0, \"next\": \"terminal_raise\"}\n"
    "      ],\n"
    "      \"snapshot\": {\n"
    "        \"num_players\": 2,\n"
    "        \"street\": \"PREFLOP\",\n"
    "        \"to_act\": 1,\n"
    "        \"first_to_act\": 1,\n"
    "        \"pot\": 1.5,\n"
    "        \"to_call\": 1.0,\n"
    "        \"current_bet\": 1.0,\n"
    "        \"raises_made\": 0,\n"
    "        \"board_revealed\": 0,\n"
    "        \"stacks\": [99.5, 99.0],\n"
    "        \"invested\": [0.5, 1.0],\n"
    "        \"round_contrib\": [0.5, 1.0],\n"
    "        \"active\": [1, 1],\n"
    "        \"acted\": [1, 0]\n"
    "      }\n"
    "    },\n"
    "    {\n"
    "      \"id\": \"flop_chance\",\n"
    "      \"type\": \"chance\",\n"
    "      \"street\": \"PREFLOP\",\n"
    "      \"actions\": [\n"
    "        {\"type\": \"chance\", \"next\": \"flop_player\"}\n"
    "      ],\n"
    "      \"snapshot\": {\n"
    "        \"num_players\": 2,\n"
    "        \"street\": \"FLOP\",\n"
    "        \"to_act\": 0,\n"
    "        \"first_to_act\": 0,\n"
    "        \"pot\": 3.5,\n"
    "        \"to_call\": 0.0,\n"
    "        \"current_bet\": 0.0,\n"
    "        \"raises_made\": 0,\n"
    "        \"board\": [12, 13, 14],\n"
    "        \"board_revealed\": 3,\n"
    "        \"stacks\": [98.5, 98.0],\n"
    "        \"invested\": [1.5, 2.0],\n"
    "        \"round_contrib\": [0.0, 0.0],\n"
    "        \"active\": [1, 1],\n"
    "        \"acted\": [0, 0]\n"
    "      }\n"
    "    },\n"
    "    {\n"
    "      \"id\": \"flop_player\",\n"
    "      \"type\": \"player\",\n"
    "      \"street\": \"FLOP\",\n"
    "      \"player\": 0,\n"
    "      \"bet_profile\": \"default\",\n"
    "      \"actions\": [\n"
    "        {\"type\": \"call\", \"next\": \"terminal_call\"}\n"
    "      ],\n"
    "      \"snapshot\": {\n"
    "        \"num_players\": 2,\n"
    "        \"street\": \"FLOP\",\n"
    "        \"to_act\": 0,\n"
    "        \"first_to_act\": 0,\n"
    "        \"pot\": 3.5,\n"
    "        \"to_call\": 0.0,\n"
    "        \"current_bet\": 0.0,\n"
    "        \"raises_made\": 0,\n"
    "        \"board\": [12, 13, 14],\n"
    "        \"board_revealed\": 3,\n"
    "        \"stacks\": [98.5, 98.0],\n"
    "        \"invested\": [1.5, 2.0],\n"
    "        \"round_contrib\": [0.0, 0.0],\n"
    "        \"active\": [1, 1],\n"
    "        \"acted\": [0, 0]\n"
    "      }\n"
    "    },\n"
    "    {\n"
    "      \"id\": \"terminal_call\",\n"
    "      \"type\": \"terminal\",\n"
    "      \"street\": \"SHOWDOWN\",\n"
    "      \"snapshot\": {\n"
    "        \"num_players\": 2,\n"
    "        \"street\": \"SHOWDOWN\",\n"
    "        \"to_act\": -1,\n"
    "        \"first_to_act\": -1,\n"
    "        \"pot\": 3.5,\n"
    "        \"to_call\": 0.0,\n"
    "        \"current_bet\": 0.0,\n"
    "        \"raises_made\": 0,\n"
    "        \"board\": [12, 13, 14],\n"
    "        \"board_revealed\": 3,\n"
    "        \"stacks\": [98.5, 98.0],\n"
    "        \"invested\": [1.5, 2.0],\n"
    "        \"round_contrib\": [0.0, 0.0],\n"
    "        \"active\": [1, 1],\n"
    "        \"acted\": [1, 1]\n"
    "      }\n"
    "    },\n"
    "    {\n"
    "      \"id\": \"terminal_raise\",\n"
    "      \"type\": \"terminal\",\n"
    "      \"street\": \"SHOWDOWN\",\n"
    "      \"snapshot\": {\n"
    "        \"num_players\": 2,\n"
    "        \"street\": \"SHOWDOWN\",\n"
    "        \"to_act\": -1,\n"
    "        \"first_to_act\": -1,\n"
    "        \"pot\": 4.5,\n"
    "        \"to_call\": 0.0,\n"
    "        \"current_bet\": 0.0,\n"
    "        \"raises_made\": 0,\n"
    "        \"board_revealed\": 0,\n"
    "        \"stacks\": [97.0, 97.0],\n"
    "        \"invested\": [3.0, 3.0],\n"
    "        \"round_contrib\": [0.0, 0.0],\n"
    "        \"active\": [1, 1],\n"
    "        \"acted\": [1, 1]\n"
    "      }\n"
    "    }\n"
    "  ]\n"
    "}\n";
int main(void)
{
    mpf_perf_stats_t stats;
    EvalConfig ecfg = eval_config_holdem();
    cfr_storage_t *storage = NULL;
    int state_initialized = 0;
    mpf_tree_def_t *tree = NULL;
    struct mpf_perf_stats_pool_t *pool = NULL;
    EvalContext *ctx = NULL;
    mpf_config_t cfg;
    cfr_game_t game;
    mpf_state_t state;
    cfr_config_t solve_cfg;
    double exploit = 0.0;
    mpf_tree_error_t tree_err;
    int tree_ok = 0;

    memset(&stats, 0, sizeof(stats));
    ctx = eval_context_create(&ecfg);
    CHECK(ctx != NULL, "EvalContext create");

    memset(&cfg, 0, sizeof(cfg));
    cfg.ctx = ctx;
    cfg.rules = MPF_RULE_HOLDEM;
    cfg.num_players = 2;
    cfg.button_index = 0;
    cfg.start_street = MPF_STREET_PREFLOP;
    cfg.bet_size_count_common = 1;
    cfg.bet_sizes_common[0] = 3.0;
    cfg.raise_cap = 4;
    cfg.enable_pot_sizing = 0;
    cfg.sb = 0.5;
    cfg.bb = 1.0;
    cfg.ante = 0.0;
    cfg.perf_stats = &stats;
    for (int i = 0; i < cfg.num_players; ++i)
        cfg.stacks[i] = 100.0;

    CHECK(mpf_build_game(&cfg, &game, &state) == 0, "mpf_build_game failed");
    state_initialized = 1;

    storage = cfr_storage_create();
    CHECK(storage != NULL, "storage alloc failed");

    memset(&solve_cfg, 0, sizeof(solve_cfg));
    solve_cfg.max_iterations = 5;

    cfr_solve(&game, storage, &solve_cfg, &exploit);
    printf("MPF_PERF_STATS: apply_action=%llu clone_ops=%llu heap_allocs=%llu util=%llu cache_hits=%llu cache_misses=%llu\n",
           (unsigned long long)stats.apply_action_calls,
           (unsigned long long)stats.state_clone_ops,
           (unsigned long long)stats.state_heap_allocs,
           (unsigned long long)stats.utility_computations,
           (unsigned long long)stats.state_cache_hits,
           (unsigned long long)stats.state_cache_misses);

    CHECK(stats.apply_action_calls > 0, "apply_action counter");
    CHECK(stats.state_clone_ops > 0, "state clone counter");
    CHECK(stats.state_heap_allocs > 0, "heap alloc counter");
    CHECK(stats.utility_computations > 0, "utility counter");

    mpf_perf_stats_reset(&stats);
    CHECK(stats.apply_action_calls == 0, "reset apply_action");
    CHECK(stats.state_heap_allocs == 0, "reset heap alloc");

#if !defined(_WIN32)
    memset(&tree_err, 0, sizeof(tree_err));
    tree = mpf_tree_load_json(k_tree_json, strlen(k_tree_json), &tree_err);
    CHECK(tree != NULL, tree_err.message[0] ? tree_err.message : "tree load failed");
    tree_ok = mpf_tree_validate(tree, &tree_err);
    if (!tree_ok)
    {
        fprintf(stderr, "tree validate error: %s\n",
                tree_err.message[0] ? tree_err.message : "<none>");
    }
    CHECK(tree_ok == 1, tree_err.message[0] ? tree_err.message : "tree validate failed");

    pool = mpf_perf_stats_pool_create(4);
    CHECK(pool != NULL, "perf pool create");
    mpf_perf_stats_pool_reset(pool);

    worker_data_t workers[2];
    pthread_t threads[2];
    for (int i = 0; i < 2; ++i)
    {
        memset(&workers[i], 0, sizeof(worker_data_t));
        workers[i].tree = tree;
        workers[i].ctx = ctx;
        workers[i].pool = pool;
        CHECK(pthread_create(&threads[i], NULL, worker_thread, &workers[i]) == 0, "pthread_create");
    }
    for (int i = 0; i < 2; ++i)
    {
        CHECK(pthread_join(threads[i], NULL) == 0, "pthread_join");
    }

    mpf_perf_stats_t pooled_total;
    mpf_perf_stats_pool_collect(pool, &pooled_total);
    printf("MPF_PERF_STATS pooled: apply_action=%llu clone=%llu heap=%llu hits=%llu misses=%llu\n",
           (unsigned long long)pooled_total.apply_action_calls,
           (unsigned long long)pooled_total.state_clone_ops,
           (unsigned long long)pooled_total.state_heap_allocs,
           (unsigned long long)pooled_total.state_cache_hits,
           (unsigned long long)pooled_total.state_cache_misses);
    CHECK(pooled_total.apply_action_calls > 0, "threaded perf counter");
    CHECK(workers[0].shard != NULL, "worker 0 shard");
    CHECK(workers[1].shard != NULL, "worker 1 shard");
    CHECK(workers[0].shard->apply_action_calls > 0, "worker 0 stats");
    CHECK(workers[1].shard->apply_action_calls > 0, "worker 1 stats");
#endif

    cfr_storage_destroy(storage);
    if (state_initialized)
        mpf_state_cleanup(&state);
    if (pool)
        mpf_perf_stats_pool_destroy(pool);
    mpf_tree_free(tree);
    eval_context_destroy(ctx);
    return 0;

fail:
    fprintf(stderr, "MPF_PERF_STATS diag: apply_action=%llu clone=%llu heap=%llu util=%llu cache_hits=%llu cache_misses=%llu\n",
            (unsigned long long)stats.apply_action_calls,
            (unsigned long long)stats.state_clone_ops,
            (unsigned long long)stats.state_heap_allocs,
            (unsigned long long)stats.utility_computations,
            (unsigned long long)stats.state_cache_hits,
            (unsigned long long)stats.state_cache_misses);
    if (storage)
        cfr_storage_destroy(storage);
    if (state_initialized)
        mpf_state_cleanup(&state);
    if (pool)
        mpf_perf_stats_pool_destroy(pool);
    if (tree)
        mpf_tree_free(tree);
    if (ctx)
        eval_context_destroy(ctx);
    return 1;
}

#if !defined(_WIN32)
static void *worker_thread(void *arg)
{
    worker_data_t *data = (worker_data_t *)arg;

    mpf_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.ctx = data->ctx;
    cfg.rules = MPF_RULE_HOLDEM;
    cfg.num_players = 2;
    cfg.button_index = 0;
    cfg.start_street = MPF_STREET_PREFLOP;
    cfg.bet_size_count_common = 1;
    cfg.bet_sizes_common[0] = 3.0;
    cfg.raise_cap = 2;
    cfg.enable_pot_sizing = 0;
    cfg.sb = 0.5;
    cfg.bb = 1.0;
    cfg.tree = data->tree;
    cfg.tree_enforced = 1;
    cfg.perf_pool = data->pool;
    for (int i = 0; i < cfg.num_players; ++i)
        cfg.stacks[i] = 40.0;

    cfr_game_t game;
    mpf_state_t state;
    if (mpf_build_game(&cfg, &game, &state) != 0)
        return NULL;
    data->shard = state.perf_stats;

    cfr_storage_t *storage = cfr_storage_create();
    if (!storage)
    {
        mpf_state_cleanup(&state);
        return NULL;
    }

    cfr_config_t solve_cfg;
    memset(&solve_cfg, 0, sizeof(solve_cfg));
    solve_cfg.max_iterations = 2;
    double exploit = 0.0;
    cfr_solve(&game, storage, &solve_cfg, &exploit);

    mpf_state_cleanup(&state);
    cfr_storage_destroy(storage);
    return NULL;
}
#endif
