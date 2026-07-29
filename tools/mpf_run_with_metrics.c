#include <poker_eval/engine/solvers/cfr/multiway_postflop_adapter.h>
#include <poker_eval/engine/solvers/cfr/mpf_tree.h>
#include <poker_eval/engine/solvers/cfr/cfr_core.h>
#include <poker_eval/core/eval_context.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    FILE *stream;
} metrics_writer_t;

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s --tree <path> [options]\n"
            "\n"
            "Required:\n"
            "  --tree <path>            JSON file describing the predefined tree\n"
            "\n"
            "Options:\n"
            "  --iterations <n>         Number of CFR iterations (default: 1000)\n"
            "  --metrics-interval <n>   Emit metrics every n iterations (default: 50)\n"
            "  --metrics-file <path>    Write metrics snapshots as JSON lines (use '-' for stdout)\n"
            "  --node-map <path>        Save node->state key mapping for later exports\n"
            "  --checkpoint <path>      Save final storage checkpoint to this file\n"
            "  --players <n>            Override player count (otherwise inferred or default 2)\n"
            "  --stack <amount>         Initial stack for every player (default: 100)\n"
            "  --bb <amount>            Big blind value (default: 1.0)\n"
            "  --sb <amount>            Small blind value (default: 0.5)\n"
            "  --help                   Show this message\n",
            prog);
}

static int parse_int(const char *arg, int *out)
{
    char *end = NULL;
    long v = strtol(arg, &end, 10);
    if (!end || *end != '\0')
        return 0;
    *out = (int)v;
    return 1;
}

static int parse_double(const char *arg, double *out)
{
    char *end = NULL;
    double v = strtod(arg, &end);
    if (!end || *end != '\0')
        return 0;
    *out = v;
    return 1;
}

static char *load_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        return NULL;
    }
    long len = ftell(f);
    if (len < 0)
    {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0)
    {
        fclose(f);
        return NULL;
    }
    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf)
    {
        fclose(f);
        return NULL;
    }
    size_t read_bytes = fread(buf, 1, (size_t)len, f);
    fclose(f);
    if (read_bytes != (size_t)len)
    {
        free(buf);
        return NULL;
    }
    buf[len] = '\0';
    if (out_len)
        *out_len = (size_t)len;
    return buf;
}

static void metrics_listener(const cfr_metrics_snapshot_t *snap, void *user)
{
    metrics_writer_t *writer = (metrics_writer_t *)user;
    if (!writer || !writer->stream || !snap)
        return;
    fprintf(writer->stream,
            "{\"iteration\":%d,"
            "\"elapsed_sec\":%.6f,"
            "\"iteration_time_sec\":%.6f,"
            "\"nodes_iteration\":%ld,"
            "\"nodes_total\":%lld,"
            "\"nodes_per_sec\":%.6f,"
            "\"iterations_per_sec\":%.6f,"
            "\"infosets_total\":%zu,"
            "\"exploitability\":%.6f,"
            "\"ev_mean\":%.6f,"
            "\"ev_stddev\":%.6f,"
            "\"mchips_per_sec\":%.6f,"
            "\"bb_per_100\":%.6f,"
            "\"volatility\":%.6f"
            "}\n",
            snap->iteration,
            snap->elapsed_sec,
            snap->iteration_time_sec,
            snap->nodes_iteration,
            (long long)snap->nodes_total,
            snap->nodes_per_sec,
            snap->iterations_per_sec,
            snap->infosets_total,
            snap->exploitability,
            snap->ev_mean,
            snap->ev_stddev,
            snap->mchips_per_sec,
            snap->bb_per_100,
            snap->volatility);
    fflush(writer->stream);
}

static int infer_players_from_tree(const mpf_tree_def_t *tree)
{
    if (!tree || tree->root_index < 0 || tree->root_index >= tree->node_count)
        return -1;
    const mpf_tree_node_t *root = &tree->nodes[tree->root_index];
    if (root->snapshot.defined && root->snapshot.num_players > 0)
        return root->snapshot.num_players;
    return -1;
}

static int write_node_map_csv(const mpf_tree_def_t *tree, const char *path)
{
    if (!tree || !path)
        return -1;
    FILE *f = fopen(path, "w");
    if (!f)
        return -1;
    fprintf(f, "node_id,state_key\n");
    for (int i = 0; i < tree->node_count; ++i)
    {
        const mpf_tree_node_t *node = &tree->nodes[i];
        if (!node->id || node->state_key == 0)
            continue;
        fprintf(f, "%s,0x%llx\n",
                node->id,
                (unsigned long long)node->state_key);
    }
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    const char *tree_path = NULL;
    const char *metrics_path = NULL;
    const char *checkpoint_path = NULL;
    const char *node_map_path = NULL;
    int iterations = 1000;
    int metrics_interval = 50;
    int metrics_history = 128;
    int metrics_level = 2;
    int players_override = -1;
    double stack_amount = 100.0;
    double sb_amount = 0.5;
    double bb_amount = 1.0;

    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--tree") == 0 && i + 1 < argc)
        {
            tree_path = argv[++i];
        }
        else if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc)
        {
            if (!parse_int(argv[++i], &iterations) || iterations <= 0)
            {
                fprintf(stderr, "Invalid iterations value\n");
                return 1;
            }
        }
        else if (strcmp(argv[i], "--metrics-interval") == 0 && i + 1 < argc)
        {
            if (!parse_int(argv[++i], &metrics_interval) || metrics_interval <= 0)
            {
                fprintf(stderr, "Invalid metrics interval\n");
                return 1;
            }
        }
        else if (strcmp(argv[i], "--metrics-history") == 0 && i + 1 < argc)
        {
            if (!parse_int(argv[++i], &metrics_history) || metrics_history <= 0)
            {
                fprintf(stderr, "Invalid metrics history\n");
                return 1;
            }
        }
        else if (strcmp(argv[i], "--metrics-file") == 0 && i + 1 < argc)
        {
            metrics_path = argv[++i];
        }
        else if (strcmp(argv[i], "--node-map") == 0 && i + 1 < argc)
        {
            node_map_path = argv[++i];
        }
        else if (strcmp(argv[i], "--checkpoint") == 0 && i + 1 < argc)
        {
            checkpoint_path = argv[++i];
        }
        else if (strcmp(argv[i], "--players") == 0 && i + 1 < argc)
        {
            if (!parse_int(argv[++i], &players_override) || players_override <= 0)
            {
                fprintf(stderr, "Invalid players value\n");
                return 1;
            }
        }
        else if (strcmp(argv[i], "--stack") == 0 && i + 1 < argc)
        {
            if (!parse_double(argv[++i], &stack_amount) || stack_amount <= 0.0)
            {
                fprintf(stderr, "Invalid stack value\n");
                return 1;
            }
        }
        else if (strcmp(argv[i], "--bb") == 0 && i + 1 < argc)
        {
            if (!parse_double(argv[++i], &bb_amount) || bb_amount <= 0.0)
            {
                fprintf(stderr, "Invalid big blind value\n");
                return 1;
            }
        }
        else if (strcmp(argv[i], "--sb") == 0 && i + 1 < argc)
        {
            if (!parse_double(argv[++i], &sb_amount) || sb_amount <= 0.0)
            {
                fprintf(stderr, "Invalid small blind value\n");
                return 1;
            }
        }
        else if (strcmp(argv[i], "--help") == 0)
        {
            usage(argv[0]);
            return 0;
        }
        else
        {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    if (!tree_path)
    {
        usage(argv[0]);
        return 1;
    }

    size_t json_len = 0;
    char *json_data = load_file(tree_path, &json_len);
    if (!json_data)
    {
        fprintf(stderr, "Failed to read tree file '%s': %s\n", tree_path, strerror(errno));
        return 1;
    }

    mpf_tree_error_t tree_err = {0};
    mpf_tree_def_t *tree = mpf_tree_load_json(json_data, json_len, &tree_err);
    free(json_data);
    if (!tree)
    {
        fprintf(stderr, "Tree load error: %s\n", tree_err.message[0] ? tree_err.message : "unknown");
        return 1;
    }
    if (!mpf_tree_validate(tree, &tree_err))
    {
        fprintf(stderr, "Tree validation error: %s\n", tree_err.message[0] ? tree_err.message : "unknown");
        mpf_tree_free(tree);
        return 1;
    }

    int inferred_players = infer_players_from_tree(tree);
    int num_players = players_override > 0 ? players_override : (inferred_players > 0 ? inferred_players : 2);
    if (num_players <= 0 || num_players > MPF_MAX_PLAYERS)
    {
        fprintf(stderr, "Unsupported player count: %d\n", num_players);
        mpf_tree_free(tree);
        return 1;
    }

    EvalConfig ecfg = eval_config_holdem();
    EvalContext *ctx = eval_context_create(&ecfg);
    if (!ctx)
    {
        fprintf(stderr, "Failed to create EvalContext\n");
        mpf_tree_free(tree);
        return 1;
    }

    struct mpf_perf_stats_pool_t *perf_pool = mpf_perf_stats_pool_create(4);
    if (!perf_pool)
    {
        fprintf(stderr, "Failed to create perf stats pool\n");
        eval_context_destroy(ctx);
        mpf_tree_free(tree);
        return 1;
    }

    mpf_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.ctx = ctx;
    cfg.rules = MPF_RULE_HOLDEM;
    cfg.num_players = num_players;
    cfg.button_index = 0;
    cfg.start_street = MPF_STREET_PREFLOP;
    cfg.bet_size_count_common = 1;
    cfg.bet_sizes_common[0] = bb_amount * 3.0;
    cfg.raise_cap = 4;
    cfg.enable_pot_sizing = 0;
    cfg.sb = sb_amount;
    cfg.bb = bb_amount;
    cfg.ante = 0.0;
    cfg.tree = tree;
    cfg.tree_enforced = 1;
    cfg.perf_pool = perf_pool;
    for (int i = 0; i < cfg.num_players; ++i)
        cfg.stacks[i] = stack_amount;

    cfr_game_t game;
    mpf_state_t root_state;
    if (mpf_build_game(&cfg, &game, &root_state) != 0)
    {
        fprintf(stderr, "mpf_build_game failed\n");
        mpf_perf_stats_pool_destroy(perf_pool);
        eval_context_destroy(ctx);
        mpf_tree_free(tree);
        return 1;
    }

    cfr_storage_t *storage = cfr_storage_create();
    if (!storage)
    {
        fprintf(stderr, "Failed to create CFR storage\n");
        mpf_state_cleanup(&root_state);
        mpf_perf_stats_pool_destroy(perf_pool);
        eval_context_destroy(ctx);
        mpf_tree_free(tree);
        return 1;
    }

    cfr_metrics_buffer_t *metrics_buffer = cfr_metrics_buffer_create(metrics_history);
    metrics_writer_t writer = {0};
    FILE *metrics_file = NULL;
    if (metrics_path)
    {
        if (strcmp(metrics_path, "-") == 0)
        {
            writer.stream = stdout;
        }
        else
        {
            metrics_file = fopen(metrics_path, "w");
            if (!metrics_file)
            {
                fprintf(stderr, "Failed to open metrics file '%s': %s\n", metrics_path, strerror(errno));
                cfr_metrics_buffer_destroy(metrics_buffer);
                cfr_storage_destroy(storage);
                mpf_state_cleanup(&root_state);
                mpf_perf_stats_pool_destroy(perf_pool);
                eval_context_destroy(ctx);
                mpf_tree_free(tree);
                return 1;
            }
            writer.stream = metrics_file;
        }
    }

    cfr_config_t solve_cfg;
    memset(&solve_cfg, 0, sizeof(solve_cfg));
    solve_cfg.max_iterations = iterations;
    solve_cfg.metrics_interval = metrics_interval;
    solve_cfg.metrics_history = metrics_history;
    solve_cfg.metrics_level = metrics_level;
    solve_cfg.metrics_buffer = metrics_buffer;
    if (writer.stream)
    {
        solve_cfg.metrics_fn = metrics_listener;
        solve_cfg.metrics_user = &writer;
    }

    double exploitability = 0.0;
    cfr_solve(&game, storage, &solve_cfg, &exploitability);

    if (node_map_path)
    {
        if (write_node_map_csv(tree, node_map_path) != 0)
        {
            fprintf(stderr, "Failed to write node map '%s': %s\n", node_map_path, strerror(errno));
        }
        else
        {
            printf("Node map saved to %s\n", node_map_path);
        }
    }

    if (checkpoint_path)
    {
        uint64_t iteration_written = (uint64_t)iterations;
        if (cfr_storage_save_checkpoint(storage, checkpoint_path, iteration_written) != 0)
        {
            fprintf(stderr, "Failed to write checkpoint '%s': %s\n", checkpoint_path, strerror(errno));
        }
    }

    cfr_metrics_snapshot_t latest = {0};
    if (metrics_buffer && cfr_metrics_buffer_get_latest(metrics_buffer, &latest) == 0)
    {
        printf("Final metrics: iter=%d exploitability=%.6f nodes_total=%lld elapsed=%.3fs\n",
               latest.iteration,
               latest.exploitability,
               (long long)latest.nodes_total,
               latest.elapsed_sec);
    }
    else
    {
        printf("Finished %d iterations, exploitability=%.6f\n", iterations, exploitability);
    }

    mpf_perf_stats_t perf_totals;
    memset(&perf_totals, 0, sizeof(perf_totals));
    mpf_perf_stats_pool_collect(perf_pool, &perf_totals);
    printf("Perf stats: apply_action=%llu cache_hits=%llu cache_misses=%llu\n",
           (unsigned long long)perf_totals.apply_action_calls,
           (unsigned long long)perf_totals.state_cache_hits,
           (unsigned long long)perf_totals.state_cache_misses);

    if (metrics_file)
        fclose(metrics_file);
    if (metrics_buffer)
        cfr_metrics_buffer_destroy(metrics_buffer);
    cfr_storage_destroy(storage);
    mpf_state_cleanup(&root_state);
    mpf_perf_stats_pool_destroy(perf_pool);
    eval_context_destroy(ctx);
    mpf_tree_free(tree);
    return 0;
}
