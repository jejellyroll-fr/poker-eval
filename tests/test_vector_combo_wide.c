/*
 * test_vector_combo_wide.c - VEC-02: combo indices beyond 16 bits
 *
 * A heads-up PLO4 river range holds C(47,4) = 178,365 combos, which no 16-bit
 * index can hold. These fixtures drive the widened (pe_combo_id_t) surface end
 * to end: a full-width terminal traversal, a best response over the same
 * width, the compatibility callback at ids above 65,535, and the documented
 * rejection of an out-of-range count.
 *
 * The traversal itself is intentionally a terminal root (and a one-action
 * decision node for the best response). The vector lane weighs an opponent
 * action per compatible acting combo, which is quadratic at this width and is
 * issue #285's subject, not this one; what this test pins is the index width.
 */

#include <poker_eval/solver/pe_best_response.h>
#include <poker_eval/solver/pe_persist.h>
#include <poker_eval/solver/pe_ports.h>
#include <poker_eval/solver/pe_traversal.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIDE_COMBOS 178365u

static int failures;

#define CHECK(cond, ...)                                                   \
    do                                                                     \
    {                                                                      \
        if (!(cond))                                                       \
        {                                                                  \
            fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__);         \
            fprintf(stderr, __VA_ARGS__);                                  \
            fputc('\n', stderr);                                           \
            failures++;                                                    \
        }                                                                  \
    } while (0)

typedef struct
{
    int terminal;
} wide_state_t;

static const wide_state_t WIDE_ROOT = {0};
static const wide_state_t WIDE_LEAF = {1};

typedef struct
{
    size_t terminal_n;
    size_t terminal_calls;
    size_t max_player_combo;
    size_t max_opponent_combo;
    size_t compatible_calls;
} wide_capture_t;

static int wide_is_terminal(const void *state, void *user)
{
    (void)user;
    return ((const wide_state_t *)state)->terminal;
}

static int wide_acting_player(const void *state, void *user)
{
    (void)state;
    (void)user;
    return 0;
}

static uint16_t wide_action_count(const void *state, void *user)
{
    (void)user;
    return ((const wide_state_t *)state)->terminal ? 0u : 1u;
}

static uint64_t wide_infoset_key(const void *state, void *user)
{
    (void)user;
    return ((const wide_state_t *)state)->terminal ? 0u : 7u;
}

static const void *wide_apply_action(const void *state, uint16_t action,
                                     void *user)
{
    (void)state;
    (void)user;
    return action == 0u ? &WIDE_LEAF : NULL;
}

static int wide_terminal_values(const void *state, const pe_reach_vec_t *reach,
                                pe_value_vec_t *out_values,
                                uint8_t player_count, void *user)
{
    wide_capture_t *capture = (wide_capture_t *)user;
    size_t combo;

    (void)state;
    (void)reach;
    if (player_count != 2u || out_values[0].n != out_values[1].n)
        return -1;
    for (combo = 0u; combo < out_values[0].n; ++combo)
    {
        out_values[0].v[combo] = 1.0;
        out_values[1].v[combo] = 0.0;
    }
    capture->terminal_n = out_values[0].n;
    capture->terminal_calls++;
    return 0;
}

static int wide_combo_compatible(const void *state, uint8_t player,
                                 pe_combo_id_t player_combo, uint8_t opponent,
                                 pe_combo_id_t opponent_combo, void *user)
{
    wide_capture_t *capture = (wide_capture_t *)user;

    (void)state;
    (void)opponent;
    if (player_combo > capture->max_player_combo)
        capture->max_player_combo = player_combo;
    if (opponent_combo > capture->max_opponent_combo)
        capture->max_opponent_combo = opponent_combo;
    capture->compatible_calls++;
    return 1;
}

static void wide_init_game(pe_vector_game_t *game, const void *root,
                           pe_combo_count_t combos, wide_capture_t *capture)
{
    memset(game, 0, sizeof(*game));
    game->root = root;
    game->user = capture;
    game->player_count = 2u;
    game->combo_count = combos;
    game->is_terminal = wide_is_terminal;
    game->acting_player = wide_acting_player;
    game->action_count = wide_action_count;
    game->infoset_key = wide_infoset_key;
    game->apply_action = wide_apply_action;
    game->terminal_values = wide_terminal_values;
}

static void test_wide_terminal_traversal(void)
{
    pe_vector_game_t game;
    pe_traversal_ctx_t ctx;
    pe_update_batch_t batch = {0};
    const pe_traversal_ops_t *ops = pe_traversal_full_vector_ops();
    wide_capture_t capture = {0};

    memset(&ctx, 0, sizeof(ctx));
    wide_init_game(&game, &WIDE_LEAF, (pe_combo_count_t)WIDE_COMBOS,
                   &capture);
    CHECK(pe_traversal_ctx_init(&ctx, &game) == 0,
          "init rejected the full PLO4 river width");
    if (ctx.initialized)
    {
        CHECK(ctx.reach[0].n == (size_t)WIDE_COMBOS,
              "reach width is %zu, expected %u", ctx.reach[0].n, WIDE_COMBOS);
        CHECK(fabs(ctx.reach[1].v[WIDE_COMBOS - 1u] - 1.0) < 1e-12,
              "the last combo's reach is not 1.0");
        CHECK(ops != NULL && ops->begin_iteration(&ctx, 0u) == 0,
              "begin_iteration failed");
        if (ops != NULL)
            CHECK(ops->run_iteration(&ctx, &batch) == 0,
                  "terminal traversal failed at the widened width");
        CHECK(capture.terminal_calls == 1u,
              "terminal callback ran %zu times", capture.terminal_calls);
        CHECK(capture.terminal_n == (size_t)WIDE_COMBOS,
              "terminal callback saw %zu combo values, expected %u",
              capture.terminal_n, WIDE_COMBOS);
    }
    pe_traversal_ctx_destroy(&ctx);
}

static void test_wide_best_response(void)
{
    pe_vector_game_t game;
    pe_best_response_vector_config_t config =
        pe_best_response_vector_config_default();
    pe_best_response_vector_result_t result = {0};
    wide_capture_t capture = {0};

    config.max_iterations = 2u;
    wide_init_game(&game, &WIDE_ROOT, (pe_combo_count_t)WIDE_COMBOS,
                   &capture);
    CHECK(pe_best_response_vector(&game, 0u, &config, &result) ==
              PE_SOLVER_OK,
          "best response over the full river width failed");
    CHECK(result.infosets == 1u, "best response saw %u infosets, expected 1",
          result.infosets);
    CHECK(isfinite(result.value), "best response value is not finite");
    CHECK(result.iterations == 2u || result.converged == 1,
          "best response stopped after %u iterations",
          result.iterations);
}

static void test_wide_combo_ids_in_callbacks(void)
{
    pe_vector_game_t game;
    pe_traversal_ctx_t ctx;
    pe_update_batch_t batch = {0};
    const pe_traversal_ops_t *ops = pe_traversal_full_vector_ops();
    wide_capture_t capture = {0};

    memset(&ctx, 0, sizeof(ctx));
    wide_init_game(&game, &WIDE_ROOT, 8u, &capture);
    game.combo_compatible = wide_combo_compatible;
    CHECK(pe_traversal_ctx_init(&ctx, &game) == 0,
          "init rejected an 8-combo compatible game");
    if (ctx.initialized)
    {
        CHECK(ops != NULL && ops->begin_iteration(&ctx, 0u) == 0,
              "begin_iteration failed");
        if (ops != NULL)
            CHECK(ops->run_iteration(&ctx, &batch) == 0,
                  "compatible traversal failed");
        CHECK(capture.compatible_calls > 0u,
              "the compatibility callback was never called");
        CHECK(capture.max_player_combo == 7u,
              "compatibility saw player combo %zu, expected 7",
              capture.max_player_combo);
        CHECK(capture.max_opponent_combo == 7u,
              "compatibility saw opponent combo %zu, expected 7",
              capture.max_opponent_combo);
    }
    pe_traversal_ctx_destroy(&ctx);

    /* The callback type is the public boundary: a 16-bit parameter would
     * truncate these ids before the callback body could see them. */
    capture.max_player_combo = 0u;
    capture.max_opponent_combo = 0u;
    CHECK(wide_combo_compatible(NULL, 0u,
                                (pe_combo_id_t)(WIDE_COMBOS - 2u), 1u,
                                (pe_combo_id_t)(WIDE_COMBOS - 1u),
                                &capture) == 1,
          "direct compatibility call failed");
    CHECK(capture.max_player_combo == (size_t)WIDE_COMBOS - 2u,
          "player combo arrived truncated as %zu", capture.max_player_combo);
    CHECK(capture.max_opponent_combo == (size_t)WIDE_COMBOS - 1u,
          "opponent combo arrived truncated as %zu",
          capture.max_opponent_combo);
}

static void test_wide_storage_binding(void)
{
    pe_vector_game_t game;
    pe_traversal_ctx_t ctx;
    const pe_storage_ops_t *ops = pe_storage_ram_ops();
    void *storage = NULL;
    wide_capture_t capture = {0};

    memset(&ctx, 0, sizeof(ctx));
    wide_init_game(&game, &WIDE_LEAF, (pe_combo_count_t)WIDE_COMBOS,
                   &capture);
    CHECK(pe_traversal_ctx_init(&ctx, &game) == 0,
          "wide traversal init failed");
    if (ctx.initialized && ops != NULL)
    {
        CHECK(ops->create(&storage, 1u) == 0, "storage creation failed");
        CHECK(pe_traversal_ctx_set_storage(&ctx, ops, storage) == 0,
              "a 178,365-combo game was refused by storage binding");
        ops->destroy(storage);
    }
    pe_traversal_ctx_destroy(&ctx);
}

static const char *wide_checkpoint_path(void)
{
    static char path[512];
    const char *directory = getenv("PE_TEST_TMPDIR");
    int written;

    if (!directory || !*directory)
        directory = ".";
    written = snprintf(path, sizeof(path), "%s/wide_combo_checkpoint.bin",
                       directory);
    if (written < 0 || (size_t)written >= sizeof(path))
        return NULL;
    return path;
}

/* Storage and checkpoint records carry the combo count too: the wide range
 * must survive resolve, shape, values and a v4 checkpoint round-trip. */
static void test_wide_storage_and_checkpoint(void)
{
    const pe_storage_ops_t *ops = pe_storage_ram_ops();
    const pe_persist_ops_t *persist = pe_persist_checkpoint_ops();
    pe_solver_config_t config = pe_solver_config_default();
    const char *path = wide_checkpoint_path();
    pe_persist_target_t target = {0};
    pe_persist_source_t source = {0};
    pe_infoset_id_t id;
    pe_infoset_id_t restored_id;
    pe_combo_count_t combos = 0u;
    uint16_t actions = 0u;
    size_t length = 0u;
    uint64_t iteration = 0u;
    double *regret;
    const double *restored;
    void *left = NULL;
    void *right = NULL;

    CHECK(path != NULL && ops != NULL && persist != NULL,
          "wide checkpoint dependencies unavailable");
    if (!path || !ops || !persist)
        return;
    target.path = path;
    target.game_hash = UINT64_C(0x11112222);
    target.tree_hash = UINT64_C(0x33334444);
    source.path = path;
    source.game_hash = UINT64_C(0x11112222);
    source.tree_hash = UINT64_C(0x33334444);
    config.problem.expected_infosets = 1u;
    config.problem.expected_actions = 1u;
    config.problem.expected_combos = (pe_combo_count_t)WIDE_COMBOS;
    CHECK(ops->create(&left, 1u) == 0 && ops->create(&right, 1u) == 0,
          "wide storage creation failed");
    if (!left || !right)
        goto done;
    id = ops->resolve(left, UINT64_C(0x281), 1u,
                      (pe_combo_count_t)WIDE_COMBOS, 3);
    CHECK(id != PE_INFOSET_ID_INVALID, "wide infoset resolution failed");
    if (id == PE_INFOSET_ID_INVALID)
        goto done;
    CHECK(ops->shape(left, id, &actions, &combos, NULL) == 0 &&
              actions == 1u && combos == (pe_combo_count_t)WIDE_COMBOS,
          "wide storage shape is %u x %u", actions, combos);
    regret = ops->values(left, id, PE_VALUES_REGRET, &length);
    CHECK(regret != NULL && length == (size_t)WIDE_COMBOS,
          "wide regret slab has %zu slots", length);
    if (!regret)
        goto done;
    regret[0] = 1.5;
    regret[WIDE_COMBOS - 1u] = -2.5;
    CHECK(persist->save(NULL, &target, &config, ops, left, 700u) == 0,
          "wide checkpoint save failed");
    CHECK(persist->load(NULL, &source, &config, ops, right, &iteration) == 0,
          "wide checkpoint load failed");
    CHECK(iteration == 700u, "wide checkpoint restored iteration %llu",
          (unsigned long long)iteration);
    restored_id = ops->find(right, UINT64_C(0x281));
    CHECK(restored_id != PE_INFOSET_ID_INVALID,
          "wide infoset was not restored");
    combos = 0u;
    CHECK(ops->shape(right, restored_id, &actions, &combos, NULL) == 0 &&
              combos == (pe_combo_count_t)WIDE_COMBOS,
          "restored combo count is %u", combos);
    restored = ops->values_const(right, restored_id, PE_VALUES_REGRET,
                                 &length);
    CHECK(restored != NULL && length == (size_t)WIDE_COMBOS &&
              fabs(restored[WIDE_COMBOS - 1u] + 2.5) < 1e-15,
          "the last combo's value did not round-trip");
done:
    if (ops != NULL)
    {
        ops->destroy(left);
        ops->destroy(right);
    }
    remove(path);
}

static void test_oversized_combo_count_rejected(void)
{
    pe_vector_game_t game;
    pe_traversal_ctx_t ctx;
    pe_best_response_vector_config_t config =
        pe_best_response_vector_config_default();
    pe_best_response_vector_result_t result = {0};
    wide_capture_t capture = {0};

    memset(&ctx, 0, sizeof(ctx));
    wide_init_game(&game, &WIDE_LEAF,
                   (pe_combo_count_t)(PE_COMBO_COUNT_MAX + 1u), &capture);
    CHECK(pe_traversal_ctx_init(&ctx, &game) != 0,
          "init accepted a combo count above PE_COMBO_COUNT_MAX");
    CHECK(pe_best_response_vector(&game, 0u, &config, &result) ==
              PE_SOLVER_ERR_INVALID_CONFIG,
          "best response accepted a combo count above PE_COMBO_COUNT_MAX");
}

int main(void)
{
    test_wide_terminal_traversal();
    test_wide_best_response();
    test_wide_combo_ids_in_callbacks();
    test_wide_storage_binding();
    test_wide_storage_and_checkpoint();
    test_oversized_combo_count_rejected();
    if (failures != 0)
    {
        fprintf(stderr, "test_vector_combo_wide: %d failure(s)\n", failures);
        return 1;
    }
    puts("test_vector_combo_wide: combo indices stay intact at 178,365");
    return 0;
}
