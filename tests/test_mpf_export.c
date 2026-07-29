#include <poker_eval/engine/solvers/cfr/multiway_postflop_adapter.h>
#include <poker_eval/engine/solvers/cfr/mpf_tree.h>
#include <poker_eval/engine/solvers/cfr/mpf_export.h>
#include <poker_eval/core/eval_context.h>
#include <poker_eval/engine/solvers/cfr/cfr_core.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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
    "  \"rangeProfiles\": [\n"
    "    {\n"
    "      \"id\": \"root_p1\",\n"
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
    "      \"range_profile\": \"root_p1\",\n"
    "      \"actions\": [\n"
    "        {\"type\": \"call\", \"next\": \"flop_chance\"},\n"
    "        {\"type\": \"raise\", \"size_index\": 0, \"next\": \"terminal_raise\"}\n"
    "      ],\n"
    "      \"snapshot\": {\n"
    "        \"num_players\": 2\n"
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
    "        \"to_act\": 0\n"
    "      }\n"
    "    },\n"
    "    {\n"
    "      \"id\": \"flop_player\",\n"
    "      \"type\": \"player\",\n"
    "      \"street\": \"FLOP\",\n"
    "      \"player\": 0,\n"
    "      \"range_profile\": \"flop_p0\",\n"
    "      \"actions\": [\n"
    "        {\"type\": \"call\", \"next\": \"terminal_call\"}\n"
    "      ],\n"
    "      \"snapshot\": {\n"
    "        \"num_players\": 2,\n"
    "        \"street\": \"FLOP\",\n"
    "        \"to_act\": 0\n"
    "      }\n"
    "    },\n"
    "    {\n"
    "      \"id\": \"terminal_call\",\n"
    "      \"type\": \"terminal\",\n"
    "      \"street\": \"SHOWDOWN\"\n"
    "    },\n"
    "    {\n"
    "      \"id\": \"terminal_raise\",\n"
    "      \"type\": \"terminal\",\n"
    "      \"street\": \"SHOWDOWN\"\n"
    "    }\n"
    "  ]\n"
    "}\n";

int main(void)
{
    EvalContext *ctx = NULL;
    cfr_storage_t *storage = NULL;
    int state_initialized = 0;
    mpf_tree_error_t err;
    mpf_tree_def_t *tree = NULL;
    EvalConfig ecfg;
    mpf_config_t cfg;
    cfr_game_t game;
    mpf_state_t state;
    cfr_config_t solve_cfg;
    double exploit = 0.0;
    mpf_node_summary_t *summaries = NULL;
    int node_count = 0;
    const mpf_node_summary_t *root_summary = NULL;
    double prob_sum = 0.0;
    FILE *f = NULL;
    long size = 0;
    char *buffer = NULL;
    size_t read_bytes = 0;
    char line[512];
    int found_combo = 0;
    mpf_export_options_t csv_opts;

    tree = mpf_tree_load_json(k_tree_json, strlen(k_tree_json), &err);
    CHECK(tree != NULL, err.message);

    ecfg = eval_config_holdem();
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
    cfg.tree = tree;
    cfg.tree_enforced = 1;
    for (int i = 0; i < cfg.num_players; ++i)
        cfg.stacks[i] = 100.0;

    CHECK(mpf_build_game(&cfg, &game, &state) == 0, "mpf_build_game failed");
    state_initialized = 1;

    storage = cfr_storage_create();
    CHECK(storage != NULL, "storage alloc failed");

    memset(&solve_cfg, 0, sizeof(solve_cfg));
    solve_cfg.max_iterations = 5;

    cfr_solve(&game, storage, &solve_cfg, &exploit);

    CHECK(mpf_collect_node_results(tree, storage, &summaries, &node_count) == 0, "collect failed");
    CHECK(node_count == tree->node_count, "unexpected node count");

    for (int i = 0; i < node_count; ++i)
    {
        if (strcmp(summaries[i].node_id, "root") == 0)
        {
            root_summary = &summaries[i];
            break;
        }
    }
    if (root_summary)
    {
        printf("MPF_EXPORT: root summary visited=%d actions=%d\n",
               root_summary->visited, root_summary->action_count);
        for (int i = 0; i < root_summary->action_count; ++i)
        {
            const mpf_node_action_summary_t *act = &root_summary->actions[i];
            printf("  action %d type=%d prob=%.6f\n",
                   i, act->type, act->probability);
            prob_sum += act->probability;
        }
    }
    CHECK(root_summary != NULL, "root summary missing");
    CHECK(root_summary->visited, "root not visited");
    CHECK(root_summary->action_count == 2, "root action count");
    CHECK(fabs(prob_sum - 1.0) < 1e-6, "probabilities should sum to 1");

    CHECK(mpf_export_node_results_json(tree, storage, "test_mpf_export.json") == 0,
          "export json failed");
    f = fopen("test_mpf_export.json", "rb");
    CHECK(f != NULL, "cannot open json output");
    CHECK(fseek(f, 0, SEEK_END) == 0, "seek json");
    size = ftell(f);
    CHECK(size > 0, "json size");
    rewind(f);
    buffer = (char *)malloc((size_t)size + 1);
    CHECK(buffer != NULL, "buffer alloc");
    read_bytes = fread(buffer, 1, (size_t)size, f);
    CHECK(read_bytes == (size_t)size, "json read");
    buffer[size] = '\0';
    fclose(f);
    f = NULL;
    CHECK(strstr(buffer, "\"range_profile\": \"root_p1\"") != NULL ||
          strstr(buffer, "\"range_profile\":\"root_p1\"") != NULL,
          "range profile missing");
    CHECK(strstr(buffer, "\"combos\"") != NULL, "combos missing");
    free(buffer);
    buffer = NULL;
    remove("test_mpf_export.json");

    memset(&csv_opts, 0, sizeof(csv_opts));
    csv_opts.format = MPF_EXPORT_FORMAT_CSV;
    csv_opts.path = "test_mpf_export.csv";
    csv_opts.include_combos = 1;
    csv_opts.filter.street_filter_enabled = 1;
    csv_opts.filter.street = MPF_STREET_PREFLOP;
    CHECK(mpf_export_node_results(tree, storage, &csv_opts) == 0, "export csv failed");

    f = fopen("test_mpf_export.csv", "r");
    CHECK(f != NULL, "cannot open csv output");
    CHECK(fgets(line, sizeof(line), f) != NULL, "csv header");
    CHECK(strncmp(line, "node_id,street,type", 19) == 0, "csv header invalid");
    while (fgets(line, sizeof(line), f))
    {
        if (strstr(line, "AhKh") != NULL)
        {
            found_combo = 1;
            break;
        }
    }
    fclose(f);
    f = NULL;
    CHECK(found_combo, "csv combo row missing");
    remove("test_mpf_export.csv");

    mpf_free_node_results(summaries, node_count);
    if (storage)
        cfr_storage_destroy(storage);
    if (state_initialized)
        mpf_state_cleanup(&state);
    if (ctx)
        eval_context_destroy(ctx);
    mpf_tree_free(tree);
    return 0;

fail:
    if (f) fclose(f);
    if (buffer) free(buffer);
    if (state_initialized)
        mpf_state_cleanup(&state);
    if (storage)
        cfr_storage_destroy(storage);
    if (ctx)
        eval_context_destroy(ctx);
    if (tree)
        mpf_tree_free(tree);
    return 1;
}
