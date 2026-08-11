#include <poker_eval/engine/solvers/cfr/cfr_core.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

typedef struct
{
    int is_terminal;
    int player;
    double util[2];
} metrics_state_t;

static metrics_state_t g_root = {0, 0, {0.0, 0.0}};
static metrics_state_t g_root_p1 = {0, 1, {0.0, 0.0}};
static metrics_state_t g_win = {1, -1, {1.0, -1.0}};
static metrics_state_t g_lose = {1, -1, {-1.0, 1.0}};

static int metrics_current_player(cfr_game_t *game, uint64_t key, void *user)
{
    (void)game;
    (void)user;
    metrics_state_t *st = (metrics_state_t *)(uintptr_t)key;
    return st->player;
}

static int metrics_is_terminal(cfr_game_t *game, uint64_t key, void *user)
{
    (void)game;
    (void)user;
    metrics_state_t *st = (metrics_state_t *)(uintptr_t)key;
    return st->is_terminal;
}

static double metrics_get_utility(cfr_game_t *game, uint64_t key, int player, void *user)
{
    (void)game;
    (void)user;
    metrics_state_t *st = (metrics_state_t *)(uintptr_t)key;
    return st->util[player];
}

static int metrics_get_actions(cfr_game_t *game, uint64_t key, int *out_actions, int max_actions, void *user)
{
    (void)game;
    (void)user;
    metrics_state_t *st = (metrics_state_t *)(uintptr_t)key;
    if (st->is_terminal)
        return 0;
    if (max_actions < 2)
        return 0;
    out_actions[0] = 0;
    out_actions[1] = 1;
    return 2;
}

static uint64_t metrics_apply_action(cfr_game_t *game, uint64_t key, int action, void *user)
{
    (void)game;
    (void)user;
    metrics_state_t *st = (metrics_state_t *)(uintptr_t)key;
    if (st->is_terminal)
        return key;
    return (uint64_t)(uintptr_t)((action == 0) ? &g_win : &g_lose);
}

typedef struct
{
    int call_count;
    int last_iteration;
    double last_exploitability;
} metrics_listener_ctx_t;

static void metrics_listener(const cfr_metrics_snapshot_t *snapshot, void *user)
{
    metrics_listener_ctx_t *ctx = (metrics_listener_ctx_t *)user;
    ctx->call_count += 1;
    ctx->last_iteration = snapshot->iteration;
    ctx->last_exploitability = snapshot->exploitability;
}

#define CHECK(cond, msg)               \
    do {                               \
        if (!(cond)) {                 \
            fprintf(stderr, "%s\n", msg); \
            return 1;                  \
        }                              \
    } while (0)

int main(void)
{
    cfr_game_t game;
    memset(&game, 0, sizeof(game));
    game.current_player = metrics_current_player;
    game.get_actions = metrics_get_actions;
    game.apply_action = metrics_apply_action;
    game.is_terminal = metrics_is_terminal;
    game.get_utility = metrics_get_utility;
    game.game_data = NULL;
    game.initial_state = &g_root;
    game.state_size = sizeof(metrics_state_t);
    game.num_players = 2;

    cfr_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_iterations = 3;
    cfg.metrics_interval = 1;
    cfg.metrics_level = 1;
    cfg.metrics_history = 2;

    metrics_listener_ctx_t listener_ctx = {0, 0};
    cfr_metrics_buffer_t *buffer = cfr_metrics_buffer_create(4);
    CHECK(buffer != NULL, "failed to allocate metrics buffer");
    cfg.metrics_buffer = buffer;
    cfg.metrics_fn = metrics_listener;
    cfg.metrics_user = &listener_ctx;

    cfr_storage_t *storage = cfr_storage_create();
    CHECK(storage != NULL, "failed to allocate storage");

    double exploitability = 0.0;
    (void)cfr_solve(&game, storage, &cfg, &exploitability);

    CHECK(listener_ctx.call_count == cfg.max_iterations, "listener call count mismatch");
    CHECK(cfr_metrics_buffer_count(buffer) == cfg.metrics_history, "metrics buffer count mismatch");

    cfr_metrics_snapshot_t latest;
    CHECK(cfr_metrics_buffer_get_latest(buffer, &latest) == 0, "failed to get latest snapshot");
    CHECK(latest.iteration == cfg.max_iterations, "latest snapshot iteration mismatch");
    CHECK(latest.nodes_iteration >= 0, "nodes iteration invalid");

    cfr_metrics_snapshot_t previous;
    CHECK(cfr_metrics_buffer_get(buffer, 1, &previous) == 0, "failed to get previous snapshot");
    CHECK(previous.iteration == cfg.max_iterations - 1, "previous snapshot iteration mismatch");

    CHECK(latest.exploitability == 0.0, "exploitability must be disabled when exploitability_interval == 0");

    /* With exploitability_interval set, snapshots must carry the exact
     * best-response exploitability instead of the 0.0 disabled default. */
    cfr_config_t cfg2;
    memset(&cfg2, 0, sizeof(cfg2));
    cfg2.max_iterations = 5;
    cfg2.metrics_interval = 1;
    cfg2.metrics_level = 1;
    cfg2.metrics_history = 2;
    cfg2.exploitability_interval = 1;
    cfg2.metrics_buffer = buffer;
    cfg2.metrics_fn = metrics_listener;
    cfg2.metrics_user = &listener_ctx;
    (void)cfr_solve(&game, storage, &cfg2, NULL);
    CHECK(listener_ctx.last_exploitability > 0.0, "exploitability should be positive on enabled interval");

    /* A best response maximizes the queried player's own terminal utility.
       P1 therefore chooses g_lose (+1 for P1), not g_win (-1 for P1). */
    game.initial_state = &g_root_p1;
    CHECK(cfr_best_response_value(&game, storage, 1, NULL) > 0.999,
          "player 1 best response must maximize player 1 utility");
    game.initial_state = &g_root;

    cfr_metrics_buffer_destroy(buffer);
    cfr_storage_destroy(storage);
    return 0;
}
