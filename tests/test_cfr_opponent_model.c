/*
 * @file test_cfr_opponent_model.c
 * @brief Tests for Opponent Models & Multi-Action Nodelock (FEAT-12, #148)
 */

#include <poker_eval/engine/solvers/cfr/mpf_tree.h>
#include <poker_eval/engine/solvers/cfr/cfr_core.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#define CHECK(cond, msg)                            \
    do                                              \
    {                                               \
        if (!(cond))                                \
        {                                           \
            fprintf(stderr, "%s\n", msg);           \
            return 1;                               \
        }                                           \
    } while (0)

/* --- Tree with multi-action raise node for partial locking tests --- */
static const char *k_tree_json =
    "{\n"
    "  \"version\": 1,\n"
    "  \"root\": \"root\",\n"
    "  \"nodes\": [\n"
    "    {\n"
    "      \"id\": \"root\",\n"
    "      \"type\": \"player\",\n"
    "      \"street\": \"FLOP\",\n"
    "      \"player\": 0,\n"
    "      \"actions\": [\n"
    "        {\"type\": \"fold\", \"next\": \"t_fold\"},\n"
    "        {\"type\": \"call\", \"next\": \"t_call\"},\n"
    "        {\"type\": \"raise\", \"bet_size\": 0.5, \"next\": \"t_r50\"},\n"
    "        {\"type\": \"raise\", \"bet_size\": 1.0, \"next\": \"t_r100\"}\n"
    "      ]\n"
    "    },\n"
    "    {\"id\": \"t_fold\", \"type\": \"terminal\"},\n"
    "    {\"id\": \"t_call\", \"type\": \"terminal\"},\n"
    "    {\"id\": \"t_r50\", \"type\": \"terminal\"},\n"
    "    {\"id\": \"t_r100\", \"type\": \"terminal\"}\n"
    "  ]\n"
    "}\n";

/* Apply a partial per-action lock directly in the tree JSON. */
static const char *k_tree_partial_json =
    "{\n"
    "  \"version\": 1,\n"
    "  \"root\": \"root\",\n"
    "  \"nodes\": [\n"
    "    {\n"
    "      \"id\": \"root\",\n"
    "      \"type\": \"player\",\n"
    "      \"street\": \"FLOP\",\n"
    "      \"player\": 0,\n"
    "      \"actions\": [\n"
    "        {\"type\": \"fold\", \"next\": \"t_fold\"},\n"
    "        {\"type\": \"call\", \"next\": \"t_call\"},\n"
    "        {\"type\": \"raise\", \"bet_size\": 0.5, \"next\": \"t_r50\"},\n"
    "        {\"type\": \"raise\", \"bet_size\": 1.0, \"next\": \"t_r100\"}\n"
    "      ]\n"
    "    },\n"
    "    {\"id\": \"t_fold\", \"type\": \"terminal\"},\n"
    "    {\"id\": \"t_call\", \"type\": \"terminal\"},\n"
    "    {\"id\": \"t_r50\", \"type\": \"terminal\"},\n"
    "    {\"id\": \"t_r100\", \"type\": \"terminal\"}\n"
    "  ]\n"
    "}\n";

static int find_node(const mpf_tree_def_t *tree, const char *id)
{
    for (int i = 0; i < tree->node_count; ++i)
        if (tree->nodes[i].id && strcmp(tree->nodes[i].id, id) == 0)
            return i;
    return -1;
}

static int test_normalize_lock(void)
{
    /* Full lock: sum exactly 1.0 -> unchanged. */
    double full[4] = {0.1, 0.2, 0.3, 0.4};
    double out[4] = {0};
    CHECK(mpf_tree_normalize_lock(full, 4, NULL, out) == 0, "normalize full");
    double s = 0.0;
    for (int i = 0; i < 4; ++i)
        s += out[i];
    CHECK(fabs(s - 1.0) < 1e-9, "normalize full sums to 1");

    /* Partial lock: RAISE_50=0.3, RAISE_100=0.2, CALL=0.5, FOLD free. */
    double locked[4] = {-1.0, 0.5, 0.3, 0.2}; /* FOLD, CALL, RAISE_50, RAISE_100 */
    CHECK(mpf_tree_normalize_lock(locked, 4, NULL, out) == 0, "normalize partial");
    CHECK(fabs(out[1] - 0.5) < 1e-9, "call pinned");
    CHECK(fabs(out[2] - 0.3) < 1e-9, "raise50 pinned");
    CHECK(fabs(out[3] - 0.2) < 1e-9, "raise100 pinned");
    CHECK(fabs(out[0] - 0.0) < 1e-9, "free fold gets residual (0 because sum=1)");
    s = 0.0;
    for (int i = 0; i < 4; ++i)
        s += out[i];
    CHECK(fabs(s - 1.0) < 1e-9, "partial sums to 1");

    /* Residual distributed: lock 0.6 across 4, sum must still be 1. */
    double locked2[4] = {0.3, -1.0, 0.3, -1.0};
    CHECK(mpf_tree_normalize_lock(locked2, 4, NULL, out) == 0, "normalize residual");
    CHECK(fabs(out[0] - 0.3) < 1e-9 && fabs(out[2] - 0.3) < 1e-9, "pinned preserved");
    s = 0.0;
    for (int i = 0; i < 4; ++i)
        s += out[i];
    CHECK(fabs(s - 1.0) < 1e-9, "residual sums to 1");
    CHECK(fabs(out[1] - out[3]) < 1e-9, "residual split uniformly");

    /* Sum > 1.0 must be rejected. */
    double bad[2] = {0.6, 0.6};
    CHECK(mpf_tree_normalize_lock(bad, 2, NULL, out) != 0, "reject sum>1");
    return 0;
}

static int test_tree_partial_lock(void)
{
    mpf_tree_error_t err;
    memset(&err, 0, sizeof(err));
    mpf_tree_def_t *tree = mpf_tree_load_json(k_tree_json, strlen(k_tree_json), &err);
    CHECK(tree != NULL, err.message);

    int idx = find_node(tree, "root");
    CHECK(idx >= 0, "root node found");
    mpf_tree_node_t *node = &tree->nodes[idx];
    CHECK(node->action_count == 4, "root has 4 actions");

    /* Apply opponent model with partial locks across the same node. */
    const char *model =
        "{"
        "  \"name\": \"Fish\","
        "  \"nodes\": {"
        "    \"root\": { \"RAISE_50\": 0.3, \"RAISE_100\": 0.2, \"CALL\": 0.5 }"
        "  }"
        "}";
    CHECK(pe_cfr_apply_opponent_model(tree, model, strlen(model), &err) == 0, err.message);

    CHECK(node->is_locked == 1, "node is locked after model apply");
    CHECK(node->locked_strategy_count == 4, "locked vector length matches actions");
    /* FOLD is free; CALL=0.5, RAISE_50=0.3, RAISE_100=0.2 -> FOLD gets 0.0. */
    CHECK(fabs(node->locked_strategy[1] - 0.5) < 1e-9, "CALL=0.5");
    CHECK(fabs(node->locked_strategy[2] - 0.3) < 1e-9, "RAISE_50=0.3");
    CHECK(fabs(node->locked_strategy[3] - 0.2) < 1e-9, "RAISE_100=0.2");
    CHECK(fabs(node->locked_strategy[0] - 0.0) < 1e-9, "FOLD=0.0 residual");
    double sum = 0.0;
    for (int i = 0; i < 4; ++i)
        sum += node->locked_strategy[i];
    CHECK(fabs(sum - 1.0) < 1e-9, "locked vector sums to 1.0");

    CHECK(mpf_tree_validate(tree, &err) == 1, "tree validates after model apply");

    mpf_tree_free(tree);
    return 0;
}

static int test_multi_action_same_node(void)
{
    mpf_tree_error_t err;
    memset(&err, 0, sizeof(err));
    mpf_tree_def_t *tree = mpf_tree_load_json(k_tree_json, strlen(k_tree_json), &err);
    CHECK(tree != NULL, err.message);

    /* Three distinct actions in the same node locked simultaneously. */
    const char *model =
        "{"
        "  \"nodes\": {"
        "    \"root\": ["
        "      {\"action\": \"RAISE_50\", \"freq\": 0.3},"
        "      {\"action\": \"RAISE_100\", \"freq\": 0.2},"
        "      {\"action\": \"CALL\", \"freq\": 0.5}"
        "    ]"
        "  }"
        "}";
    CHECK(pe_cfr_apply_opponent_model(tree, model, strlen(model), &err) == 0, err.message);

    int idx = find_node(tree, "root");
    mpf_tree_node_t *node = &tree->nodes[idx];
    CHECK(fabs(node->locked_strategy[1] - 0.5) < 1e-9, "array-form CALL=0.5");
    CHECK(fabs(node->locked_strategy[2] - 0.3) < 1e-9, "array-form RAISE_50=0.3");
    CHECK(fabs(node->locked_strategy[3] - 0.2) < 1e-9, "array-form RAISE_100=0.2");
    mpf_tree_free(tree);
    return 0;
}

static int test_validation_sum_exceeds_one(void)
{
    mpf_tree_error_t err;
    memset(&err, 0, sizeof(err));
    mpf_tree_def_t *tree = mpf_tree_load_json(k_tree_json, strlen(k_tree_json), &err);
    CHECK(tree != NULL, err.message);

    const char *model =
        "{"
        "  \"nodes\": {"
        "    \"root\": { \"RAISE_50\": 0.6, \"RAISE_100\": 0.6 }"
        "  }"
        "}";
    CHECK(pe_cfr_apply_opponent_model(tree, model, strlen(model), &err) != 0,
          "sum>1 must be rejected");
    CHECK(strstr(err.message, "1.0") != NULL, "error mentions 1.0");
    mpf_tree_free(tree);
    return 0;
}

static int test_serialize_roundtrip(void)
{
    mpf_tree_error_t err;
    memset(&err, 0, sizeof(err));
    mpf_tree_def_t *tree = mpf_tree_load_json(k_tree_json, strlen(k_tree_json), &err);
    CHECK(tree != NULL, err.message);

    const char *model =
        "{"
        "  \"nodes\": {"
        "    \"root\": { \"RAISE_50\": 0.3, \"RAISE_100\": 0.2, \"CALL\": 0.5 }"
        "  }"
        "}";
    CHECK(pe_cfr_apply_opponent_model(tree, model, strlen(model), &err) == 0, err.message);

    size_t len = 0;
    char *json = mpf_tree_serialize_json(tree, &len);
    CHECK(json != NULL && len > 0, "serialize after model apply");

    mpf_tree_def_t *tree2 = mpf_tree_load_json(json, len, &err);
    CHECK(tree2 != NULL, "re-parse serialized tree");
    int idx = find_node(tree2, "root");
    CHECK(idx >= 0, "root present after roundtrip");
    mpf_tree_node_t *node = &tree2->nodes[idx];
    CHECK(node->is_locked == 1, "still locked after roundtrip");
    CHECK(fabs(node->locked_strategy[2] - 0.3) < 1e-6, "RAISE_50 preserved");
    CHECK(fabs(node->locked_strategy[3] - 0.2) < 1e-6, "RAISE_100 preserved");
    CHECK(fabs(node->locked_strategy[1] - 0.5) < 1e-6, "CALL preserved");
    free(json);
    mpf_tree_free(tree);
    mpf_tree_free(tree2);
    return 0;
}

/* Integrate the locked node into a solvable 2-player game and confirm the
 * locked frequencies hold after CFR convergence. */
typedef struct
{
    int is_terminal;
    int player;
    double util[2];
} om_state_t;

static om_state_t g_root = {0, 0, {0.0, 0.0}};
static om_state_t g_t[4];

static int om_current_player(cfr_game_t *g, uint64_t k, void *u)
{
    (void)g; (void)u;
    return ((om_state_t *)(uintptr_t)k)->player;
}
static int om_is_terminal(cfr_game_t *g, uint64_t k, void *u)
{
    (void)g; (void)u;
    return ((om_state_t *)(uintptr_t)k)->is_terminal;
}
static double om_get_utility(cfr_game_t *g, uint64_t k, int p, void *u)
{
    (void)g; (void)u;
    return ((om_state_t *)(uintptr_t)k)->util[p];
}
static int om_get_actions(cfr_game_t *g, uint64_t k, int *out, int maxn, void *u)
{
    (void)g; (void)u;
    if (((om_state_t *)(uintptr_t)k)->is_terminal)
        return 0;
    if (maxn < 4)
        return 0;
    for (int i = 0; i < 4; ++i)
        out[i] = i;
    return 4;
}
static uint64_t om_apply_action(cfr_game_t *g, uint64_t k, int a, void *u)
{
    (void)g; (void)u;
    om_state_t *st = (om_state_t *)(uintptr_t)k;
    if (st == &g_root)
        return (uint64_t)(uintptr_t)&g_t[a];
    return k;
}

static int test_lock_solver_integration(void)
{
    mpf_tree_error_t err;
    memset(&err, 0, sizeof(err));
    mpf_tree_def_t *tree = mpf_tree_load_json(k_tree_json, strlen(k_tree_json), &err);
    CHECK(tree != NULL, err.message);

    const char *model =
        "{"
        "  \"nodes\": {"
        "    \"root\": { \"RAISE_50\": 0.3, \"RAISE_100\": 0.2, \"CALL\": 0.5 }"
        "  }"
        "}";
    CHECK(pe_cfr_apply_opponent_model(tree, model, strlen(model), &err) == 0, err.message);

    cfr_storage_t *storage = cfr_storage_create();
    CHECK(storage != NULL, "storage created");

    /* Wire the tree's locked_strategy vector into the storage infoset for the
       root key (pointer-keyed, as in test_cfr_node_lock). */
    int idx = find_node(tree, "root");
    mpf_tree_node_t *node = &tree->nodes[idx];
    uint64_t root_key = (uint64_t)(uintptr_t)&g_root;
    CHECK(cfr_storage_set_locked_strategy(storage, root_key,
                                          node->locked_strategy,
                                          node->locked_strategy_count) == 0,
          "wire lock to storage");

    /* Terminal payoffs so the game is well-defined. */
    g_t[0] = (om_state_t){1, -1, {-1.0, 1.0}};  /* fold */
    g_t[1] = (om_state_t){1, -1, {3.0, -3.0}};  /* call */
    g_t[2] = (om_state_t){1, -1, {-2.0, 2.0}};  /* raise50 */
    g_t[3] = (om_state_t){1, -1, {-2.0, 2.0}};  /* raise100 */

    cfr_game_t game;
    memset(&game, 0, sizeof(game));
    game.current_player = om_current_player;
    game.get_actions = om_get_actions;
    game.apply_action = om_apply_action;
    game.is_terminal = om_is_terminal;
    game.get_utility = om_get_utility;
    game.initial_state = (void *)(uintptr_t)&g_root;
    game.state_size = sizeof(om_state_t);
    game.num_players = 2;

    cfr_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_iterations = 300;

    double expl = 0.0;
    double result = cfr_solve(&game, storage, &cfg, &expl);
    CHECK(result >= 0.0, "solve succeeded");

    double avg[4] = {0.0, 0.0, 0.0, 0.0};
    cfr_storage_get_avg_strategy(storage, root_key, 4, avg);
    CHECK(fabs(avg[1] - 0.5) < 1e-3, "avg CALL stays 0.5");
    CHECK(fabs(avg[2] - 0.3) < 1e-3, "avg RAISE_50 stays 0.3");
    CHECK(fabs(avg[3] - 0.2) < 1e-3, "avg RAISE_100 stays 0.2");

    cfr_storage_destroy(storage);
    mpf_tree_free(tree);
    return 0;
}

int main(void)
{
    if (test_normalize_lock() != 0)
        return 1;
    if (test_tree_partial_lock() != 0)
        return 1;
    if (test_multi_action_same_node() != 0)
        return 1;
    if (test_validation_sum_exceeds_one() != 0)
        return 1;
    if (test_serialize_roundtrip() != 0)
        return 1;
    if (test_lock_solver_integration() != 0)
        return 1;
    printf(" PASSED\n");
    return 0;
}
