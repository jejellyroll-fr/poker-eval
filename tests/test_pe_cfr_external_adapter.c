/* Lane B bridge smoke test for a legacy multi-street/chance game. */

#include <poker_eval/solver/pe_cfr_external_adapter.h>
#include <poker_eval/solver/pe_storage.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    int kind; /* 0 chance, 1 decision, 2 terminal */
    int outcome;
    int action;
} node_t;

static int g_releases;
static node_t g_root = {0, 0, 0};

static int is_terminal(cfr_game_t *game, uint64_t key, void *user)
{
    (void)game; (void)user;
    return ((const node_t *)(uintptr_t)key)->kind == 2;
}

static int current_player(cfr_game_t *game, uint64_t key, void *user)
{
    const node_t *node = (const node_t *)(uintptr_t)key;
    (void)game; (void)user;
    return node->kind == 1 ? 0 : -1;
}

static int get_actions(cfr_game_t *game, uint64_t key, int *out,
                       int max_actions, void *user)
{
    (void)game; (void)user;
    if (((const node_t *)(uintptr_t)key)->kind != 1 || max_actions < 2)
        return 0;
    out[0] = 0; out[1] = 1;
    return 2;
}

static uint64_t apply_action(cfr_game_t *game, uint64_t key, int action,
                             void *user)
{
    const node_t *source = (const node_t *)(uintptr_t)key;
    node_t *child;
    (void)game; (void)user;
    if (source->kind != 1 || action < 0 || action > 1) return 0;
    child = (node_t *)calloc(1u, sizeof(*child));
    if (!child) return 0;
    child->kind = 2; child->outcome = source->outcome; child->action = action;
    return (uint64_t)(uintptr_t)child;
}

static double utility(cfr_game_t *game, uint64_t key, int player, void *user)
{
    const node_t *node = (const node_t *)(uintptr_t)key;
    (void)game; (void)user;
    if (player != 0) return 0.0;
    return node->action == node->outcome ? 1.0 : -1.0;
}

static int is_chance(cfr_game_t *game, uint64_t key, void *user)
{
    (void)game; (void)user;
    return ((const node_t *)(uintptr_t)key)->kind == 0;
}

static int chance_outcomes(cfr_game_t *game, uint64_t key, void *user)
{
    (void)game; (void)key; (void)user;
    /* More outcomes than the action port's 64-slot limit: the adapter must
       sample this chance domain in bounded memory. */
    return 128;
}

static uint64_t apply_chance(cfr_game_t *game, uint64_t key, int outcome,
                             void *user)
{
    const node_t *source = (const node_t *)(uintptr_t)key;
    node_t *child;
    (void)game; (void)user;
    if (source->kind != 0 || outcome < 0 || outcome >= 128) return 0;
    child = (node_t *)calloc(1u, sizeof(*child));
    if (!child) return 0;
    child->kind = 1; child->outcome = outcome & 1;
    return (uint64_t)(uintptr_t)child;
}

static void release_state(cfr_game_t *game, uint64_t key, void *user)
{
    (void)game; (void)user;
    if ((const node_t *)(uintptr_t)key != &g_root)
    {
        ++g_releases;
        free((void *)(uintptr_t)key);
    }
}

int main(void)
{
    cfr_game_t legacy;
    pe_cfr_external_adapter_t adapter;
    pe_external_sampling_ctx_t ctx;
    pe_update_batch_t batch = {0};
    pe_storage_t *storage;
    memset(&legacy, 0, sizeof(legacy));
    legacy.initial_state = &g_root;
    legacy.num_players = 2;
    legacy.is_terminal = is_terminal;
    legacy.current_player = current_player;
    legacy.get_actions = get_actions;
    legacy.apply_action = apply_action;
    legacy.get_utility = utility;
    legacy.is_chance = is_chance;
    legacy.get_chance_outcomes = chance_outcomes;
    legacy.apply_chance = apply_chance;
    legacy.release_state = release_state;
    if (pe_cfr_external_adapter_init(&adapter, &legacy) != 0 ||
        !pe_cfr_external_adapter_game(&adapter))
        return 1;
    storage = pe_storage_create(4u);
    if (!storage || pe_external_sampling_ctx_init(
            &ctx, pe_cfr_external_adapter_game(&adapter), pe_storage_ram_ops(),
            storage, 0, UINT64_C(0x1234)) != 0)
    {
        pe_storage_destroy(storage);
        return 1;
    }
    for (int i = 0; i < 32; ++i)
    {
        if (pe_external_sampling_run(&ctx, &batch) != 0 || batch.count != 2u)
        {
            pe_update_batch_destroy(&batch);
            pe_external_sampling_ctx_destroy(&ctx);
            pe_storage_destroy(storage);
            return 1;
        }
    }
    pe_update_batch_destroy(&batch);
    pe_external_sampling_ctx_destroy(&ctx);
    pe_storage_destroy(storage);
    if (g_releases == 0)
        return 1;
    puts("test_pe_cfr_external_adapter: legacy chance game bridged to Lane B");
    return 0;
}
