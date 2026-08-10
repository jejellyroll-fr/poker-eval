#include <poker_eval/engine/solvers/cfr/mpf_tree.h>
#include <poker_eval/engine/solvers/cfr/multiway_postflop_adapter.h>
#include <poker_eval/core/eval_context.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int mpf_test_debug_enabled(void)
{
    static int cached = -1;
    if (cached == -1)
    {
        const char *env = getenv("POKER_DEBUG_MPF");
        cached = (env && *env) ? 1 : 0;
    }
    return cached;
}

#define MPF_TEST_DEBUG(...)                    \
    do                                         \
    {                                          \
        if (mpf_test_debug_enabled())          \
            printf(__VA_ARGS__);               \
    } while (0)

#define ASSERT_TRUE(cond, msg)                        \
    do                                                \
    {                                                 \
        if (!(cond))                                  \
        {                                             \
            fprintf(stderr, "Assertion failed: %s\n", \
                    msg);                             \
            goto fail;                                \
        }                                             \
    } while (0)

static const char *k_tree_json =
    "{\n"
    "  \"version\": 1,\n"
    "  \"root\": \"root\",\n"
    "  \"betProfiles\": [\n"
    "    {\"id\": \"default\", \"sizes\": [3.0], \"pot_sizing\": false}\n"
    "  ],\n"
    "  \"rangeProfiles\": [\n"
    "    {\n"
    "      \"id\": \"root_p1_pre\",\n"
    "      \"player\": 1,\n"
    "      \"street\": \"PREFLOP\",\n"
    "      \"combos\": [\n"
    "        {\"hand\": \"AhKh\", \"weight\": 1.0},\n"
    "        {\"hand\": \"AsKs\", \"weight\": 0.5}\n"
    "      ]\n"
    "    },\n"
    "    {\n"
    "      \"id\": \"flop_p0\",\n"
    "      \"player\": 0,\n"
    "      \"street\": \"FLOP\",\n"
    "      \"aliases\": [\"flop_player\"],\n"
    "      \"combos\": [\n"
    "        {\"hand\": \"7c7d\", \"weight\": 1.0}\n"
    "      ]\n"
    "    }\n"
    "  ],\n"
    "  \"nodes\": [\n"
    "    {\n"
    "      \"id\": \"root\",\n"
    "      \"type\": \"player\",\n"
    "      \"street\": \"PREFLOP\",\n"
    "      \"player\": 1,\n"
    "      \"bet_profile\": \"default\",\n"
    "      \"range_profile\": \"root_p1_pre\",\n"
    "      \"is_locked\": true,\n"
    "      \"locked_strategy\": [1.0, 0.0],\n"
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
    mpf_tree_def_t *tree = NULL;
    EvalContext *ctx = NULL;
    char *serialized = NULL;
    mpf_tree_def_t *round_trip = NULL;
    int state_initialized = 0;
    mpf_tree_error_t err;
    mpf_config_t cfg;
    cfr_game_t game;
    mpf_state_t state;
    int actions[8];
    const mpf_tree_node_t *root_node;
    int flop_index;
    const mpf_tree_node_t *flop_node;
    size_t serialized_len;
    mpf_tree_error_t err_roundtrip;
    const mpf_tree_node_t *rt_root;
    int rt_flop_idx;
    const mpf_tree_node_t *rt_flop;
    EvalConfig ecfg;
    int n_actions;
    int has_call;
    int has_raise;
    uint64_t next_key;
    mpf_state_t *next_state;

    tree = mpf_tree_load_json(k_tree_json, strlen(k_tree_json), &err);
    ASSERT_TRUE(tree != NULL, err.message[0] ? err.message : "tree load");

    ASSERT_TRUE(tree->range_profile_count == 2, "range profile count");
    root_node = &tree->nodes[tree->root_index];
    MPF_TEST_DEBUG("MPF_TREE parser: root node type=%d actions=%d range_profile=%s\n",
                   root_node->type, root_node->action_count,
                   root_node->range_profile_id ? root_node->range_profile_id : "<none>");
    ASSERT_TRUE(root_node->range_profile != NULL, "root range profile assigned");
    ASSERT_TRUE(strcmp(root_node->range_profile->id, "root_p1_pre") == 0, "root range id");
    ASSERT_TRUE(root_node->is_locked == 1, "root is_locked parsed");
    ASSERT_TRUE(root_node->locked_strategy_count == 2, "root locked_strategy count");
    ASSERT_TRUE(root_node->locked_strategy != NULL, "root locked_strategy present");
    ASSERT_TRUE(fabs(root_node->locked_strategy[0] - 1.0) < 1e-6, "root locked_strategy[0]");
    ASSERT_TRUE(fabs(root_node->locked_strategy[1] - 0.0) < 1e-6, "root locked_strategy[1]");
    ASSERT_TRUE(root_node->range_profile->combo_count == 2, "root combo count");
    ASSERT_TRUE(strcmp(root_node->range_profile->combos[0].hand, "AhKh") == 0, "root combo hand");
    ASSERT_TRUE(fabs(root_node->range_profile->combos[0].weight - 1.0) < 1e-6, "root combo weight");

    flop_index = -1;
    for (int i = 0; i < tree->node_count; ++i)
    {
        if (strcmp(tree->nodes[i].id, "flop_player") == 0)
        {
            flop_index = i;
            break;
        }
    }
    ASSERT_TRUE(flop_index >= 0, "flop node found");
    flop_node = &tree->nodes[flop_index];
    ASSERT_TRUE(flop_node->range_profile != NULL, "flop range profile assigned");
    ASSERT_TRUE(strcmp(flop_node->range_profile->id, "flop_p0") == 0, "flop range id");
    ASSERT_TRUE(flop_node->range_profile->alias_count == 1, "flop range aliases");

    serialized_len = 0;
    serialized = mpf_tree_serialize_json(tree, &serialized_len);
    ASSERT_TRUE(serialized != NULL, "serialize tree");
    ASSERT_TRUE(serialized_len > 0, "serialized length");
    round_trip = mpf_tree_load_json(serialized, serialized_len, &err_roundtrip);
    ASSERT_TRUE(round_trip != NULL, err_roundtrip.message[0] ? err_roundtrip.message : "round trip load");
    rt_root = &round_trip->nodes[round_trip->root_index];
    ASSERT_TRUE(rt_root->range_profile != NULL, "round trip root range");
    ASSERT_TRUE(strcmp(rt_root->range_profile->id, "root_p1_pre") == 0, "round trip root id");
    ASSERT_TRUE(rt_root->is_locked == 1, "round trip is_locked");
    ASSERT_TRUE(rt_root->locked_strategy_count == 2, "round trip locked_strategy count");
    ASSERT_TRUE(fabs(rt_root->locked_strategy[0] - 1.0) < 1e-6, "round trip locked_strategy[0]");
    rt_flop_idx = -1;
    for (int i = 0; i < round_trip->node_count; ++i)
    {
        if (strcmp(round_trip->nodes[i].id, "flop_player") == 0)
        {
            rt_flop_idx = i;
            break;
        }
    }
    ASSERT_TRUE(rt_flop_idx >= 0, "round trip flop node found");
    rt_flop = &round_trip->nodes[rt_flop_idx];
    ASSERT_TRUE(rt_flop->range_profile != NULL, "round trip flop range");
    ASSERT_TRUE(strcmp(rt_flop->range_profile->id, "flop_p0") == 0, "round trip flop id");

    ecfg = eval_config_holdem();
    ctx = eval_context_create(&ecfg);
    ASSERT_TRUE(ctx != NULL, "EvalContext create");

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
    cfg.tree = tree;
    cfg.tree_enforced = 1;
    for (int i = 0; i < cfg.num_players; ++i)
        cfg.stacks[i] = 100.0;

    ASSERT_TRUE(mpf_build_game(&cfg, &game, &state) == 0, "mpf_build_game");
    state_initialized = 1;

    ASSERT_TRUE(state.tree_enabled == 1, "tree enabled");
    ASSERT_TRUE(state.tree_node_idx >= 0, "tree node index valid");
    ASSERT_TRUE(fabs(state.pot - 1.5) < 1e-6, "root pot snapshot");
    ASSERT_TRUE(state.to_act == 1, "root to_act");
    ASSERT_TRUE(state.street == MPF_STREET_PREFLOP, "root street");
    ASSERT_TRUE(state.board_revealed == 0, "root board revealed");

    n_actions = game.get_actions(&game, (uint64_t)(uintptr_t)&state, actions, 8, NULL);
    ASSERT_TRUE(n_actions == 2, "root action count");
    has_call = 0;
    has_raise = 0;
    for (int i = 0; i < n_actions; ++i)
    {
        if (actions[i] == MPF_ACTION_CALL)
            has_call = 1;
        if (actions[i] == MPF_ACTION_RAISE_BASE)
            has_raise = 1;
    }
    ASSERT_TRUE(has_call, "root has call action");
    ASSERT_TRUE(has_raise, "root has raise action");

    next_key = game.apply_action(&game, (uint64_t)(uintptr_t)&state, MPF_ACTION_CALL, NULL);
    ASSERT_TRUE(next_key != 0, "apply call");
    next_state = (mpf_state_t *)(uintptr_t)next_key;
    ASSERT_TRUE(next_state->street == MPF_STREET_FLOP, "flop street snapshot");
    ASSERT_TRUE(next_state->board_revealed == 3, "flop board revealed");
    ASSERT_TRUE(next_state->to_act == 0, "flop to_act");
    ASSERT_TRUE(next_state->tree_node_idx >= 0, "flop node idx");
    ASSERT_TRUE(fabs(next_state->pot - 3.5) < 1e-6, "flop pot snapshot");

    mpf_state_cleanup(&state);
    mpf_tree_free(round_trip);
    free(serialized);
    mpf_tree_free(tree);
    eval_context_destroy(ctx);
    return 0;

fail:
    if (state_initialized)
        mpf_state_cleanup(&state);
    if (round_trip)
        mpf_tree_free(round_trip);
    if (serialized)
        free(serialized);
    if (tree)
        mpf_tree_free(tree);
    if (ctx)
        eval_context_destroy(ctx);
    return 1;
}
