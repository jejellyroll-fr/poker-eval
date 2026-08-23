/*
 * test_pe_traversal_vector.c - VEC-02: exact vector traversal skeleton
 */

#include <poker_eval/solver/pe_traversal.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;
static int terminal_calls;

#define CHECK(cond, ...)                                                   \
    do                                                                     \
    {                                                                      \
        if (!(cond))                                                       \
        {                                                                  \
            fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__);       \
            fprintf(stderr, __VA_ARGS__);                                 \
            fputc('\n', stderr);                                           \
            failures++;                                                    \
        }                                                                  \
    } while (0)

typedef struct
{
    int terminal;
    int action;
} toy_state_t;

static const toy_state_t TOY_ROOT = {0, -1};
static const toy_state_t TOY_LEAVES[2] = {{1, 0}, {1, 1}};

static int toy_is_terminal(const void *state, void *user)
{
    (void)user;
    return ((const toy_state_t *)state)->terminal;
}

static int toy_acting_player(const void *state, void *user)
{
    (void)state;
    (void)user;
    return 0;
}

static uint16_t toy_action_count(const void *state, void *user)
{
    (void)user;
    return ((const toy_state_t *)state)->terminal ? 0u : 2u;
}

static uint64_t toy_infoset_key(const void *state, void *user)
{
    (void)state;
    (void)user;
    return UINT64_C(42);
}

static int toy_strategy(const void *state, uint64_t infoset_key,
                        uint16_t action, pe_value_vec_t *out, void *user)
{
    size_t combo;
    (void)state;
    (void)user;
    if (infoset_key != UINT64_C(42) || action > 1u)
        return -1;
    for (combo = 0; combo < out->n; ++combo)
        out->v[combo] = action == 0u
                           ? (0.25 + 0.25 * (double)combo)
                           : (0.75 - 0.25 * (double)combo);
    return 0;
}

static const void *toy_apply_action(const void *state, uint16_t action,
                                    void *user)
{
    (void)state;
    (void)user;
    return action < 2u ? &TOY_LEAVES[action] : NULL;
}

static int toy_terminal_values(const void *state, const pe_reach_vec_t *reach,
                               pe_value_vec_t *out_values,
                               uint8_t player_count, void *user)
{
    const toy_state_t *toy = (const toy_state_t *)state;
    size_t combo;
    (void)user;
    CHECK(player_count == 2u, "terminal callback saw %u players", player_count);
    CHECK(toy->action >= 0 && toy->action < 2,
          "terminal callback saw action %d", toy->action);
    CHECK(fabs(pe_vec_sum(&reach[0]) - 1.5) < 1e-12,
          "reach sum is %.17g, expected 1.5", pe_vec_sum(&reach[0]));
    CHECK(fabs(pe_vec_sum(&reach[1]) - 3.0) < 1e-12,
          "opponent reach sum is %.17g, expected 3", pe_vec_sum(&reach[1]));
    for (combo = 0; combo < out_values[0].n; ++combo)
    {
        out_values[0].v[combo] = toy->action == 0 ? 1.0 : -1.0;
        out_values[1].v[combo] = -out_values[0].v[combo];
    }
    terminal_calls++;
    return 0;
}

static void test_batch_lifetime(void)
{
    pe_update_batch_t batch = {0};
    pe_update_t update = {UINT64_C(7), 2u, 3u, 1.25};

    CHECK(pe_update_batch_push(&batch, update) == 0, "batch push failed");
    CHECK(batch.count == 1u && batch.items[0].delta == 1.25,
          "batch push produced invalid item");
    pe_update_batch_clear(&batch);
    CHECK(batch.count == 0u && batch.capacity > 0u,
          "batch clear discarded reusable capacity");
    pe_update_batch_destroy(&batch);
    CHECK(batch.items == NULL && batch.capacity == 0u,
          "batch destroy did not release storage");
}

static void test_toy_tree(void)
{
    pe_vector_game_t game;
    pe_traversal_ctx_t ctx;
    pe_update_batch_t batch = {0};
    const pe_traversal_ops_t *ops = pe_traversal_full_vector_ops();

    memset(&game, 0, sizeof(game));
    game.root = &TOY_ROOT;
    game.player_count = 2u;
    game.combo_count = 3u;
    game.is_terminal = toy_is_terminal;
    game.acting_player = toy_acting_player;
    game.action_count = toy_action_count;
    game.infoset_key = toy_infoset_key;
    game.strategy = toy_strategy;
    game.apply_action = toy_apply_action;
    game.terminal_values = toy_terminal_values;

    CHECK(ops != NULL && strcmp(ops->name, "full_vector") == 0,
          "full-vector ops are unavailable");
    CHECK(pe_traversal_ctx_init(&ctx, &game) == 0, "context init failed");
    if (!ctx.initialized)
        return;

    terminal_calls = 0;
    CHECK(ops->begin_iteration(&ctx, 7u) == 0, "begin failed");
    CHECK(ops->run_iteration(&ctx, &batch) == 0, "run failed");
    CHECK(ctx.visited_nodes == 3u, "visited %zu nodes, expected 3",
          ctx.visited_nodes);
    CHECK(ctx.terminal_nodes == 2u, "visited %zu terminals, expected 2",
          ctx.terminal_nodes);
    CHECK(terminal_calls == 2, "terminal callback called %d times", terminal_calls);
    CHECK(batch.count == 0u, "VEC-02 emitted updates before VEC-03");
    CHECK(ops->end_iteration(&ctx, 7u) == 0, "end failed");
    CHECK(ops->end_iteration(&ctx, 8u) != 0,
          "end accepted a mismatched iteration");

    pe_update_batch_destroy(&batch);
    pe_traversal_ctx_destroy(&ctx);
}

int main(void)
{
    test_batch_lifetime();
    test_toy_tree();
    if (failures != 0)
    {
        fprintf(stderr, "test_pe_traversal_vector: %d failure(s)\n", failures);
        return 1;
    }
    puts("test_pe_traversal_vector: vector traversal skeleton is sound");
    return 0;
}
