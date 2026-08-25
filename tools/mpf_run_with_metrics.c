#include <poker_eval/engine/solvers/cfr/multiway_postflop_adapter.h>
#include <poker_eval/engine/solvers/cfr/mpf_tree.h>
#include <poker_eval/engine/solvers/cfr/cfr_core.h>
#include <poker_eval/core/eval_context.h>
#include <poker_eval/solver/pe_capabilities.h>
#include <poker_eval/solver/pe_solver.h>
#include <poker_eval/solver/pe_ports.h>
#include <poker_eval/solver/pe_solver_config.h>
#include <poker_eval/solver/pe_solver_plan.h>
#include <poker_eval/solver/pe_runtime.h>
#include <poker_eval/solver/pe_cfr_external_adapter.h>
#include <poker_eval/engine/solvers/cfr/legacy_vector_adapter.h>
#include <poker_eval/solver/pe_compute.h>
#include <poker_eval/solver/pe_monker.h>
#include <poker_eval/solver/pe_monker_classes.h>
#include <poker_eval/solver/pe_monker_strategy.h>
#include <poker_eval/solver/pe_range.h>
#include <poker_eval/core/modern_cardmask.h>

#include <errno.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct
{
    FILE *stream;
} metrics_writer_t;

typedef struct
{
    int have_traversal;
    pe_traversal_mode_t traversal;
    int have_regret;
    pe_regret_mode_t regret;
    int have_policy;
    pe_policy_mode_t policy;
    int have_averaging;
    pe_averaging_mode_t averaging;
    int have_precision;
    pe_precision_mode_t precision;
    int have_alpha;
    double alpha;
    int have_beta;
    double beta;
    int have_gamma;
    double gamma;
    int have_exponential_lambda;
    double exponential_lambda;
    int have_outcome_epsilon;
    double outcome_epsilon;
} cli_solver_overrides_t;

static int run_vector_backend(cfr_game_t *legacy, int iterations,
                              const char *backend_name,
                              pe_algorithm_preset_t algorithm,
                              pe_precision_mode_t precision,
                              int cpu_threads,
                              const cli_solver_overrides_t *overrides)
{
    pe_legacy_vector_adapter_t adapter;
    pe_solver_config_t config = pe_solver_config_default();
    pe_solver_deps_t deps = pe_solver_deps_default();
    const pe_compute_ops_t *compute = NULL;
    pe_compute_kind_t kind;
    pe_solver_t *solver = NULL;
    pe_solver_status_t status;
    pe_progress_t progress;
    pe_metrics_t metrics;
    pe_runtime_capabilities_t runtime;
    const pe_runtime_backend_info_t *runtime_backend;
    int adapter_ready = 0;

    if (!legacy || !backend_name || iterations <= 0)
        return -1;
    kind = pe_compute_kind_from_name(backend_name);
    if (kind == PE_COMPUTE_COUNT || pe_runtime_probe(&runtime) != 0)
        return -1;
    runtime_backend = &runtime.backends[kind];
    if (!runtime_backend->runtime_available || !runtime_backend->validated)
    {
        fprintf(stderr, "v3 backend refused: %s (%s)\n",
                pe_compute_kind_name(kind), runtime_backend->reason);
        return -1;
    }
    if (kind == PE_COMPUTE_CPU_REF)
        compute = pe_compute_cpu_ref_ops();
    else if (kind == PE_COMPUTE_CPU_PAR)
        compute = pe_compute_cpu_par_ops();
    else if (kind == PE_COMPUTE_OPENCL)
        compute = pe_compute_opencl_ops();
    else if (kind == PE_COMPUTE_CUDA)
        compute = pe_compute_cuda_ops();
    else
        return -1;
    if (!compute || pe_legacy_vector_adapter_init(&adapter, legacy, 1u) != 0)
        return -1;
    adapter_ready = 1;

    config.algorithm.preset = algorithm;
    if (pe_preset_expand(algorithm, &config.algorithm) != 0)
    {
        pe_legacy_vector_adapter_destroy(&adapter);
        return -1;
    }
    if (overrides != NULL)
    {
        if (overrides->have_traversal)
            config.algorithm.traversal = overrides->traversal;
        if (overrides->have_regret)
            config.algorithm.regret = overrides->regret;
        if (overrides->have_policy)
            config.algorithm.policy = overrides->policy;
        if (overrides->have_averaging)
            config.algorithm.averaging = overrides->averaging;
        if (overrides->have_alpha)
            config.algorithm.dcfr_alpha = overrides->alpha;
        if (overrides->have_beta)
            config.algorithm.dcfr_beta = overrides->beta;
        if (overrides->have_gamma)
            config.algorithm.dcfr_gamma = overrides->gamma;
        if (overrides->have_exponential_lambda)
            config.algorithm.exponential_lambda = overrides->exponential_lambda;
        if (overrides->have_outcome_epsilon)
            config.algorithm.outcome_epsilon = overrides->outcome_epsilon;
        if (overrides->have_precision)
            precision = overrides->precision;
    }
    if (overrides != NULL &&
        (overrides->have_traversal || overrides->have_regret ||
         overrides->have_policy || overrides->have_averaging ||
         overrides->have_alpha || overrides->have_beta ||
         overrides->have_gamma || overrides->have_exponential_lambda ||
         overrides->have_outcome_epsilon))
    {
        /* The explicit axes are authoritative for a full-tree run. Without
           CUSTOM the solver would expand the original preset again during
           creation and silently discard the CLI choices. */
        config.algorithm.preset = PE_PRESET_CUSTOM;
    }
    config.execution.backend = kind;
    config.execution.precision = precision;
    config.execution.cpu_threads = cpu_threads;
    /* The game tree remains owned by the exact legacy-to-vector adapter. GPU
       compute ports are used for batched updates/terminal jobs; CPU backends
       execute the same traversal with their selected update kernel. */
    config.execution.stages.traversal = PE_COMPUTE_CPU_REF;
    config.execution.stages.update = kind;
    config.execution.stages.terminal_eval = kind;
    config.execution.deterministic =
        (kind == PE_COMPUTE_CPU_REF || kind == PE_COMPUTE_CPU_PAR) ? 1 : 0;
    config.execution.terminal_batch_size = 256u;
    config.execution.update_batch_size = 256u;
    config.execution.max_ram_bytes = 512u * 1024u * 1024u;
    config.problem.expected_infosets = 4096u;
    config.problem.expected_actions = 8u;
    config.problem.expected_combos = 1u;
    config.max_iterations = (uint64_t)iterations;
    deps.compute = compute;
    deps.vector_game = pe_legacy_vector_adapter_game(&adapter);
    solver = pe_solver_create(&config, &deps);
    status = solver ? pe_solver_run(solver) : PE_SOLVER_ERR_OUT_OF_MEMORY;
    printf("v3_backend=%s status=%d\n", backend_name, (int)status);
    printf("v3_runtime=backend_validated=1 simd_detected=%s simd_cfr=%s "
           "algorithm=%s traversal=%s regret=%s policy=%s averaging=%s\n",
           pe_runtime_simd_name(runtime.simd),
           (kind == PE_COMPUTE_CPU_REF || kind == PE_COMPUTE_CPU_PAR)
               ? (runtime.simd == SIMD_NONE ? "scalar-fallback" : "integrated")
               : "not-integrated",
           config.algorithm.preset == PE_PRESET_CUSTOM
               ? "custom" : pe_preset_name(algorithm),
           pe_traversal_name(config.algorithm.traversal),
           pe_regret_name(config.algorithm.regret),
           pe_policy_name(config.algorithm.policy),
           pe_averaging_name(config.algorithm.averaging));
    if (solver && pe_solver_progress(solver, &progress) == PE_SOLVER_OK)
        printf("v3_progress=%.6f complete=%d iteration=%llu\n",
               progress.fraction, progress.complete,
               (unsigned long long)progress.iteration);
    if (solver && pe_solver_metrics(solver, &metrics) == PE_SOLVER_OK)
        printf("v3_exploitability=%.6f guarantee=%d\n",
               metrics.exploitability_mbb_per_game, (int)metrics.guarantee);
    if (solver) pe_solver_destroy(solver);
    if (adapter_ready) pe_legacy_vector_adapter_destroy(&adapter);
    return status == PE_SOLVER_OK ? 0 : -1;
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s --tree <path> [options]\n"
            "\n"
            "Required:\n"
            "  --tree <path>            JSON or MonkerSolver binary .tree\n"
            "  --mkr <path>             MonkerSolver .mkr strategy archive\n"
            "  --strategy <name>        Strategy entry (default: storedstrategy0)\n"
            "  --rules <kind>           holdem, plo4, plo5 or plo6\n"
            "  --street <kind>          preflop, flop, turn or river\n"
            "  --board <cards>          Board such as AsKdQcJdTh\n"
            "  --range<N> <expr>        Override player N range (N = 0..6)\n"
            "\n"
            "Options:\n"
            "  --iterations <n>         Number of CFR iterations (default: 1000)\n"
            "  --lane-b                 Run the sampled v3 solver on this tree\n"
            "  --sample-batch <n>       Lane B trajectories per update (default: 1)\n"
            "  --threads <n>           CPU-parallel workers (default: 1)\n"
            "  --benchmark-json <path>  Write Lane B throughput metrics as JSON\n"
            "  --metrics-interval <n>   Emit metrics every n iterations (default: 50)\n"
            "  --metrics-file <path>    Write metrics snapshots as JSON lines (use '-' for stdout)\n"
            "  --node-map <path>        Save node->state key mapping for later exports\n"
            "  --checkpoint <path>      Save final storage checkpoint to this file\n"
            "  --players <n>            Override player count (otherwise inferred or default 2)\n"
            "  --stack <amount>         Initial stack for every player (default: 100)\n"
            "  --bb <amount>            Big blind value (default: 1.0)\n"
            "  --sb <amount>            Small blind value (default: 0.5)\n"
            "  --ante <amount>          Ante value (default: 0)\n"
            "  --button <n>             Button index (default: 0)\n"
            "  --pot <amount>           Existing pot at the spot\n"
            "  --to-call <amount>      Current amount to call\n"
            "  --current-bet <amount>  Current bet size\n"
            "  --raises <n>             Raises already made\n"
            "  --to-act <n>             Player to act at the spot\n"
            "  --algorithm <preset>    Select a v3 algorithm preset\n"
            "  --backend <kind>        Select a v3 backend (cpu, cpu_par, cuda, opencl)\n"
            "  --traversal <kind>      Override traversal (full-vector, full-scalar, ...)\n"
            "  --regret <kind>         Override regret update (vanilla, plus, dcfr, ...)\n"
            "  --policy <kind>         Override policy (regret-matching, exponential)\n"
            "  --averaging <kind>      Override averaging (uniform, linear, power, ...)\n"
            "  --precision <kind>      Override value precision (f64, f32, mixed, fixed16)\n"
            "  --alpha <x>             Override DCFR alpha\n"
            "  --beta <x>              Override DCFR beta\n"
            "  --gamma <x>             Override averaging/DCFR gamma\n"
            "  --lambda <x>            Exponential policy temperature (> 0)\n"
            "  --outcome-epsilon <x>   Outcome-sampling exploration (0..1)\n"
            "  --list-algorithms       List registered algorithm presets and exit\n"
            "  --list-backends         List registered compute backends and exit\n"
            "  --show-capabilities     Print the available capability bits and exit\n"
            "  --validate-only         Validate the v3 configuration and exit\n"
            "  --estimate-only         Print the v3 resource estimate and exit\n"
            "  --print-execution-plan  Print the resolved v3 execution plan and exit\n"
            "  --help                   Show this message\n",
            prog);
}

static void print_algorithms(void)
{
    for (int i = 0; i < PE_PRESET_COUNT; ++i)
    {
        pe_algorithm_preset_t preset = (pe_algorithm_preset_t)i;
        printf("%s%s\n", pe_preset_name(preset),
               pe_preset_is_experimental(preset) ? " (experimental)" : "");
    }
}

static void print_backends(void)
{
    pe_runtime_capabilities_t runtime;
    if (pe_runtime_probe(&runtime) != 0)
        return;
    printf("runtime cpus=%u openmp=%s simd=%s\n",
           runtime.logical_cpus, runtime.openmp_available ? "yes" : "no",
           pe_runtime_simd_name(runtime.simd));
    for (int i = 0; i < PE_COMPUTE_COUNT; ++i)
    {
        char line[256];
        pe_runtime_backend_status(&runtime.backends[i], line, sizeof(line));
        printf("%s\n", line);
    }
}

static int overrides_have_axis(const cli_solver_overrides_t *overrides)
{
    return overrides != NULL && (overrides->have_traversal ||
                                 overrides->have_regret ||
                                 overrides->have_policy ||
                                 overrides->have_averaging);
}

static int run_introspection(pe_algorithm_preset_t preset, int have_preset,
                             pe_compute_kind_t backend, int have_backend,
                             int show_capabilities, int validate_only,
                             int estimate_only, int print_plan,
                             const cli_solver_overrides_t *overrides)
{
    pe_solver_config_t config = pe_solver_config_default();
    pe_solver_t *solver;
    int result = 0;

    /* An introspection call is useful before a tree is loaded. The one-node
       hint is enough for plan validation; callers with a real tree can use
       --estimate-only to replace it in the next CLI tranche. */
    config.problem.expected_infosets = 1u;
    config.problem.expected_actions = 2u;
    config.problem.expected_combos = 1u;
    if (have_preset)
        config.algorithm.preset = preset;
    if (overrides_have_axis(overrides))
    {
        /* Expand the selected preset once, then make the explicit axis edits
           authoritative. Leaving the preset in place would make the resolver
           expand it again and erase those edits. */
        if (have_preset)
            pe_preset_expand(preset, &config.algorithm);
        config.algorithm.preset = PE_PRESET_CUSTOM;
        if (overrides->have_traversal)
            config.algorithm.traversal = overrides->traversal;
        if (overrides->have_regret)
            config.algorithm.regret = overrides->regret;
        if (overrides->have_policy)
            config.algorithm.policy = overrides->policy;
        if (overrides->have_averaging)
            config.algorithm.averaging = overrides->averaging;
    }
    if (overrides != NULL)
    {
        if (overrides->have_precision)
            config.execution.precision = overrides->precision;
        if (overrides->have_alpha)
            config.algorithm.dcfr_alpha = overrides->alpha;
        if (overrides->have_beta)
            config.algorithm.dcfr_beta = overrides->beta;
        if (overrides->have_gamma)
            config.algorithm.dcfr_gamma = overrides->gamma;
        if (overrides->have_exponential_lambda)
            config.algorithm.exponential_lambda = overrides->exponential_lambda;
        if (overrides->have_outcome_epsilon)
            config.algorithm.outcome_epsilon = overrides->outcome_epsilon;
    }
    if (have_backend)
    {
        config.execution.backend = backend;
        config.execution.stages.traversal = backend;
        config.execution.stages.update = backend;
        config.execution.stages.terminal_eval = backend;
    }

    solver = pe_solver_create(&config, NULL);
    if (solver == NULL)
    {
        fprintf(stderr, "Failed to create v3 solver for introspection\n");
        return 1;
    }

    if (show_capabilities)
    {
        uint64_t caps = 0u;
        char text[PE_CAPS_STRING_MAX];
        if (pe_solver_capabilities(solver, &caps) != PE_SOLVER_OK)
            result = 2;
        else
        {
            pe_caps_to_string(caps, text, sizeof(text));
            printf("capabilities=%s\n", text);
        }
        print_backends();
    }

    if (validate_only)
    {
        pe_diagnostics_t diagnostics;
        pe_solver_status_t status = pe_solver_validate(solver, &diagnostics);
        printf("validation=%s\n", status == PE_SOLVER_OK ? "ok" : "error");
        for (size_t i = 0; i < diagnostics.count; ++i)
            printf("%s: %s\n",
                   pe_valid_severity_name(diagnostics.items[i].severity),
                   diagnostics.items[i].message);
        if (status != PE_SOLVER_OK)
            result = 2;
    }

    if (estimate_only)
    {
        pe_estimate_t estimate;
        pe_solver_status_t status = pe_solver_estimate(solver, &estimate);
        if (status != PE_SOLVER_OK)
        {
            fprintf(stderr, "estimate failed: status=%d\n", (int)status);
            result = 2;
        }
        else
        {
            printf("infosets=%llu slots=%llu host_bytes=%llu within_budget=%d\n",
                   (unsigned long long)estimate.infosets,
                   (unsigned long long)estimate.slots,
                   (unsigned long long)estimate.host_bytes,
                   estimate.within_budget);
        }
    }

    if (print_plan)
    {
        pe_execution_plan_t plan;
        char text[1024];
        pe_solver_status_t status = pe_solver_plan(solver, &plan);
        if (status != PE_SOLVER_OK)
        {
            fprintf(stderr, "plan resolution failed: status=%d\n", (int)status);
            result = 2;
        }
        else
        {
            pe_plan_to_string(&plan, text, sizeof(text));
            printf("%s", text);
        }
    }

    pe_solver_destroy(solver);
    return result;
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

static int parse_rule_name(const char *name, mpf_rule_t *out_rule,
                           enum_game_t *out_game)
{
    if (!name || !out_rule || !out_game)
        return 0;
    if (strcmp(name, "holdem") == 0)
    {
        *out_rule = MPF_RULE_HOLDEM;
        *out_game = game_holdem;
        return 1;
    }
    if (strcmp(name, "plo4") == 0 || strcmp(name, "omaha") == 0)
    {
        *out_rule = MPF_RULE_PLO4;
        *out_game = game_omaha;
        return 1;
    }
    if (strcmp(name, "plo5") == 0)
    {
        *out_rule = MPF_RULE_PLO5;
        *out_game = game_omaha5;
        return 1;
    }
    if (strcmp(name, "plo6") == 0)
    {
        *out_rule = MPF_RULE_PLO6;
        *out_game = game_omaha6;
        return 1;
    }
    return 0;
}

static int parse_street_name(const char *name, mpf_street_t *out)
{
    if (!name || !out)
        return 0;
    if (strcmp(name, "preflop") == 0) *out = MPF_STREET_PREFLOP;
    else if (strcmp(name, "flop") == 0) *out = MPF_STREET_FLOP;
    else if (strcmp(name, "turn") == 0) *out = MPF_STREET_TURN;
    else if (strcmp(name, "river") == 0) *out = MPF_STREET_RIVER;
    else return 0;
    return 1;
}

static int parse_card(const char *text, int *out_card)
{
    static const char ranks[] = "23456789TJQKA";
    int rank = -1;
    int suit = -1;
    char r;
    char s;

    if (!text || !out_card || text[0] == '\0' || text[1] == '\0')
        return 0;
    r = (char)toupper((unsigned char)text[0]);
    s = (char)tolower((unsigned char)text[1]);
    for (int i = 0; i < 13; ++i)
        if (r == ranks[i])
            rank = i;
    if (s == 'c') suit = MODERN_SUIT_CLUBS;
    else if (s == 'd') suit = MODERN_SUIT_DIAMONDS;
    else if (s == 'h') suit = MODERN_SUIT_HEARTS;
    else if (s == 's') suit = MODERN_SUIT_SPADES;
    if (rank < 0 || suit < 0)
        return 0;
    *out_card = MODERN_MAKE_CARD(rank, suit);
    return 1;
}

static int parse_board(const char *text, int out_cards[5], int *out_count,
                       mask_t *out_mask)
{
    int count = 0;
    mask_t mask = MASK_EMPTY;
    size_t i = 0;

    if (!text || !out_cards || !out_count || !out_mask)
        return 0;
    while (text[i] != '\0')
    {
        char card_text[3];
        int card;
        while (text[i] == ',' || text[i] == '/' || isspace((unsigned char)text[i]))
            ++i;
        if (text[i] == '\0')
            break;
        if (text[i + 1] == '\0')
            return 0;
        card_text[0] = text[i];
        card_text[1] = text[i + 1];
        card_text[2] = '\0';
        if (!parse_card(card_text, &card) || count >= 5 || mask_is_set(mask, card))
            return 0;
        mask = mask_set(mask, card);
        out_cards[count++] = card;
        i += 2;
    }
    *out_count = count;
    *out_mask = mask;
    return 1;
}

static StdDeck_CardMask modern_to_std_mask(mask_t mask)
{
    StdDeck_CardMask out;
    StdDeck_CardMask_RESET(out);
    for (int card = 0; card < 52; ++card)
        if (mask_is_set(mask, card))
            StdDeck_CardMask_SET(out, card);
    return out;
}

static void monker_to_internal_card_mask(pe_range_t *range)
{
    /* Monker's wire/class order is s,h,c,d; poker-eval's standard deck is
       c,d,h,s. The range reader preserves the wire integer order, so this
       conversion is intentionally kept at the interop boundary. */
    static const int monker_to_internal_suit[4] = {
        MODERN_SUIT_SPADES, MODERN_SUIT_HEARTS,
        MODERN_SUIT_CLUBS, MODERN_SUIT_DIAMONDS
    };
    if (!range)
        return;
    for (size_t i = 0; i < range->count; ++i)
    {
        StdDeck_CardMask old = range->combos[i].hand;
        StdDeck_CardMask fresh;
        StdDeck_CardMask_RESET(fresh);
        for (int card = 0; card < 52; ++card)
            if (StdDeck_CardMask_CARD_IS_SET(old, card))
            {
                int external_suit = card / 13;
                int rank = card % 13;
                StdDeck_CardMask_SET(fresh,
                    MODERN_MAKE_CARD(rank, monker_to_internal_suit[external_suit]));
            }
        range->combos[i].hand = fresh;
    }
}

static int load_cli_tree(const char *path, mpf_tree_def_t **out_tree,
                         pe_monker_tree_header_t *out_header, int *out_binary)
{
    pe_monker_status_t monker_status;
    pe_monker_tree_header_t header;
    mpf_tree_error_t err = {0};

    if (!path || !out_tree)
        return 0;
    *out_tree = NULL;
    if (out_binary)
        *out_binary = 0;
    memset(&header, 0, sizeof(header));
    monker_status = pe_monker_tree_read_header(path, &header);
    if (monker_status == PE_MONKER_OK)
    {
        monker_status = pe_monker_tree_load(path, out_tree);
        if (monker_status != PE_MONKER_OK)
        {
            fprintf(stderr, "Monker .tree load error: %s\n",
                    pe_monker_status_string(monker_status));
            return 0;
        }
        if (out_header)
            *out_header = header;
        if (out_binary)
            *out_binary = 1;
        return 1;
    }

    size_t len = 0;
    char *json = load_file(path, &len);
    if (!json)
    {
        fprintf(stderr, "Failed to read tree file '%s': %s\n",
                path, strerror(errno));
        return 0;
    }
    *out_tree = mpf_tree_load_json(json, len, &err);
    free(json);
    if (!*out_tree)
    {
        fprintf(stderr, "Tree load error: %s\n",
                err.message[0] ? err.message : "unknown");
        return 0;
    }
    return 1;
}

typedef struct
{
    cfr_game_t *game;
    cfr_storage_t *storage;
    const pe_monker_strategy_t *strategy;
    const pe_monker_classes_t *classes;
    uint64_t *visited;
    size_t visited_count;
    size_t visited_capacity;
    size_t lock_count;
    int failed;
} monker_lock_walk_t;

static int monker_lock_seen(monker_lock_walk_t *walk, uint64_t key)
{
    for (size_t i = 0; i < walk->visited_count; ++i)
        if (walk->visited[i] == key)
            return 1;
    if (walk->visited_count == walk->visited_capacity)
    {
        size_t next_capacity = walk->visited_capacity ? walk->visited_capacity * 2u : 256u;
        uint64_t *next = (uint64_t *)realloc(walk->visited,
                                             next_capacity * sizeof(*next));
        if (!next)
            return -1;
        walk->visited = next;
        walk->visited_capacity = next_capacity;
    }
    walk->visited[walk->visited_count++] = key;
    return 0;
}

static int seed_monker_locks(monker_lock_walk_t *walk, uint64_t key, int depth)
{
    const mpf_state_t *state;
    int seen;

    if (!walk || !walk->game || depth > CFR_DEFAULT_MAX_DEPTH)
        return -1;
    state = mpf_state_for_key(walk->game, key);
    if (!state)
        return -1;
    if (walk->game->is_terminal(walk->game, key, NULL))
        return 0;
    seen = monker_lock_seen(walk, key);
    if (seen < 0)
        return -1;
    if (seen > 0)
        return 0;

    if (walk->game->is_chance && walk->game->is_chance(walk->game, key, NULL))
    {
        int outcomes = walk->game->get_chance_outcomes(walk->game, key, NULL);
        for (int i = 0; i < outcomes; ++i)
        {
            uint64_t child = walk->game->apply_chance(walk->game, key, i, NULL);
            if (!child || seed_monker_locks(walk, child, depth + 1) != 0)
                return -1;
        }
        return 0;
    }

    int actions[CFR_MAX_ACTIONS];
    int action_count = walk->game->get_actions(walk->game, key, actions,
                                                CFR_MAX_ACTIONS, NULL);
    int player = walk->game->current_player(walk->game, key, NULL);
    if (action_count <= 0 || player < 0 || player >= MPF_MAX_PLAYERS)
        return 0;
    if (state->tree_node_idx < 0)
    {
        fprintf(stderr, "Cannot import strategy: state has no tree node\n");
        return -1;
    }

    int cards[4];
    int card_count = 0;
    for (int card = 0; card < 52; ++card)
        if (mask_is_set(state->hole[player], card))
        {
            if (card_count >= 4)
                break;
            /* Convert poker-eval's c,d,h,s suit order to Monker's s,h,c,d. */
            static const int internal_to_monker_suit[4] = {1, 3, 2, 0};
            int suit = card / 13;
            cards[card_count++] = (internal_to_monker_suit[suit] * 13) + (card % 13);
        }
    if (card_count != 4)
    {
        fprintf(stderr, "Cannot import PLO strategy at node %d: player %d has %d hole cards\n",
                state->tree_node_idx, player, card_count);
        return -1;
    }

    double probs[CFR_MAX_ACTIONS];
    uint16_t stored_actions = 0;
    int specified = 0;
    pe_monker_status_t status = pe_monker_strategy_probs(
        walk->strategy, state->tree_node_idx, cards, probs,
        CFR_MAX_ACTIONS, &stored_actions, &specified);
    if (status != PE_MONKER_OK || stored_actions != (uint16_t)action_count)
    {
        fprintf(stderr, "Cannot import strategy at node %d: %s (tree actions=%d, archive actions=%u)\n",
                state->tree_node_idx, pe_monker_status_string(status),
                action_count, (unsigned)stored_actions);
        return -1;
    }
    (void)specified;

    uint64_t storage_key = walk->game->get_infoset_key ?
        walk->game->get_infoset_key(state) : key;
    if (cfr_storage_set_locked_strategy(walk->storage, storage_key,
                                        probs, action_count) != 0)
        return -1;
    walk->lock_count++;

    for (int i = 0; i < action_count; ++i)
    {
        uint64_t child = walk->game->apply_action(walk->game, key, actions[i], NULL);
        if (!child || seed_monker_locks(walk, child, depth + 1) != 0)
            return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    pe_runtime_capabilities_t runtime_caps = {0};
    int runtime_caps_valid = 0;
    const char *tree_path = NULL;
    const char *mkr_path = NULL;
    const char *strategy_name = "storedstrategy0";
    const char *rules_name = NULL;
    const char *street_name = NULL;
    const char *board_text = NULL;
    const char *range_text[MPF_MAX_PLAYERS] = {0};
    const char *metrics_path = NULL;
    const char *checkpoint_path = NULL;
    const char *node_map_path = NULL;
    int iterations = 1000;
    int lane_b = 0;
    int sample_batch = 1;
    int cpu_threads = 1;
    const char *benchmark_json_path = NULL;
    int metrics_interval = 50;
    int metrics_history = 128;
    int metrics_level = 2;
    int players_override = -1;
    double stack_amount = 100.0;
    double sb_amount = 0.5;
    double bb_amount = 1.0;
    double ante_amount = 0.0;
    int button_index = 0;
    int have_pot = 0;
    double pot_amount = 0.0;
    int have_to_call = 0;
    double to_call_amount = 0.0;
    int have_current_bet = 0;
    double current_bet_amount = 0.0;
    int have_raises = 0;
    int raises_made = 0;
    int have_to_act = 0;
    int to_act = 0;
    const char *algorithm_name = NULL;
    const char *backend_name = NULL;
    int list_algorithms = 0;
    int list_backends = 0;
    int show_capabilities = 0;
    int validate_only = 0;
    int estimate_only = 0;
    int print_plan = 0;
    const char *traversal_name = NULL;
    const char *regret_name = NULL;
    const char *policy_name = NULL;
    const char *averaging_name = NULL;
    const char *precision_name = NULL;
    pe_algorithm_preset_t selected_preset = PE_PRESET_EXTERNAL_MCCFR;
    pe_compute_kind_t selected_backend = PE_COMPUTE_AUTO;
    pe_precision_mode_t selected_precision = PE_PREC_F64;
    cli_solver_overrides_t overrides;

    memset(&overrides, 0, sizeof(overrides));

    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--tree") == 0 && i + 1 < argc)
        {
            tree_path = argv[++i];
        }
        else if (strcmp(argv[i], "--mkr") == 0 && i + 1 < argc)
        {
            mkr_path = argv[++i];
        }
        else if (strcmp(argv[i], "--strategy") == 0 && i + 1 < argc)
        {
            strategy_name = argv[++i];
        }
        else if (strcmp(argv[i], "--rules") == 0 && i + 1 < argc)
        {
            rules_name = argv[++i];
        }
        else if (strcmp(argv[i], "--street") == 0 && i + 1 < argc)
        {
            street_name = argv[++i];
        }
        else if (strcmp(argv[i], "--board") == 0 && i + 1 < argc)
        {
            board_text = argv[++i];
        }
        else if (strncmp(argv[i], "--range", 7) == 0 &&
                 argv[i][7] >= '0' && argv[i][7] <= '6' && i + 1 < argc)
        {
            int player = argv[i][7] - '0';
            ++i;
            range_text[player] = argv[i];
        }
        else if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc)
        {
            if (!parse_int(argv[++i], &iterations) || iterations <= 0)
            {
                fprintf(stderr, "Invalid iterations value\n");
                return 1;
            }
        }
        else if (strcmp(argv[i], "--lane-b") == 0)
        {
            lane_b = 1;
        }
        else if (strcmp(argv[i], "--sample-batch") == 0 && i + 1 < argc)
        {
            if (!parse_int(argv[++i], &sample_batch) || sample_batch <= 0)
            {
                fprintf(stderr, "Invalid sample batch value\n");
                return 1;
            }
        }
        else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc)
        {
            if (!parse_int(argv[++i], &cpu_threads) || cpu_threads <= 0)
            {
                fprintf(stderr, "Invalid threads value\n");
                return 1;
            }
        }
        else if (strcmp(argv[i], "--benchmark-json") == 0 && i + 1 < argc)
        {
            benchmark_json_path = argv[++i];
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
        else if (strcmp(argv[i], "--ante") == 0 && i + 1 < argc)
        {
            if (!parse_double(argv[++i], &ante_amount) || ante_amount < 0.0)
            {
                fprintf(stderr, "Invalid ante value\n");
                return 1;
            }
        }
        else if (strcmp(argv[i], "--button") == 0 && i + 1 < argc)
        {
            if (!parse_int(argv[++i], &button_index) || button_index < 0)
            {
                fprintf(stderr, "Invalid button value\n");
                return 1;
            }
        }
        else if (strcmp(argv[i], "--pot") == 0 && i + 1 < argc)
        {
            if (!parse_double(argv[++i], &pot_amount) || pot_amount < 0.0)
            {
                fprintf(stderr, "Invalid pot value\n");
                return 1;
            }
            have_pot = 1;
        }
        else if (strcmp(argv[i], "--to-call") == 0 && i + 1 < argc)
        {
            if (!parse_double(argv[++i], &to_call_amount) || to_call_amount < 0.0)
            {
                fprintf(stderr, "Invalid to-call value\n");
                return 1;
            }
            have_to_call = 1;
        }
        else if (strcmp(argv[i], "--current-bet") == 0 && i + 1 < argc)
        {
            if (!parse_double(argv[++i], &current_bet_amount) || current_bet_amount < 0.0)
            {
                fprintf(stderr, "Invalid current-bet value\n");
                return 1;
            }
            have_current_bet = 1;
        }
        else if (strcmp(argv[i], "--raises") == 0 && i + 1 < argc)
        {
            if (!parse_int(argv[++i], &raises_made) || raises_made < 0)
            {
                fprintf(stderr, "Invalid raises value\n");
                return 1;
            }
            have_raises = 1;
        }
        else if (strcmp(argv[i], "--to-act") == 0 && i + 1 < argc)
        {
            if (!parse_int(argv[++i], &to_act) || to_act < 0)
            {
                fprintf(stderr, "Invalid to-act value\n");
                return 1;
            }
            have_to_act = 1;
        }
        else if (strcmp(argv[i], "--algorithm") == 0 && i + 1 < argc)
        {
            algorithm_name = argv[++i];
        }
        else if (strcmp(argv[i], "--backend") == 0 && i + 1 < argc)
        {
            backend_name = argv[++i];
        }
        else if (strcmp(argv[i], "--traversal") == 0 && i + 1 < argc)
        {
            traversal_name = argv[++i];
        }
        else if (strcmp(argv[i], "--regret") == 0 && i + 1 < argc)
        {
            regret_name = argv[++i];
        }
        else if (strcmp(argv[i], "--policy") == 0 && i + 1 < argc)
        {
            policy_name = argv[++i];
        }
        else if (strcmp(argv[i], "--averaging") == 0 && i + 1 < argc)
        {
            averaging_name = argv[++i];
        }
        else if (strcmp(argv[i], "--precision") == 0 && i + 1 < argc)
        {
            precision_name = argv[++i];
        }
        else if (strcmp(argv[i], "--alpha") == 0 && i + 1 < argc)
        {
            if (!parse_double(argv[++i], &overrides.alpha))
            {
                fprintf(stderr, "Invalid alpha value\n");
                return 1;
            }
            overrides.have_alpha = 1;
        }
        else if (strcmp(argv[i], "--beta") == 0 && i + 1 < argc)
        {
            if (!parse_double(argv[++i], &overrides.beta))
            {
                fprintf(stderr, "Invalid beta value\n");
                return 1;
            }
            overrides.have_beta = 1;
        }
        else if (strcmp(argv[i], "--gamma") == 0 && i + 1 < argc)
        {
            if (!parse_double(argv[++i], &overrides.gamma))
            {
                fprintf(stderr, "Invalid gamma value\n");
                return 1;
            }
            overrides.have_gamma = 1;
        }
        else if (strcmp(argv[i], "--lambda") == 0 && i + 1 < argc)
        {
            if (!parse_double(argv[++i], &overrides.exponential_lambda) ||
                !(overrides.exponential_lambda > 0.0))
            {
                fprintf(stderr, "Invalid lambda value (must be > 0)\n");
                return 1;
            }
            overrides.have_exponential_lambda = 1;
        }
        else if (strcmp(argv[i], "--outcome-epsilon") == 0 && i + 1 < argc)
        {
            if (!parse_double(argv[++i], &overrides.outcome_epsilon) ||
                overrides.outcome_epsilon < 0.0 ||
                overrides.outcome_epsilon > 1.0)
            {
                fprintf(stderr, "Invalid outcome epsilon (must be in [0,1])\n");
                return 1;
            }
            overrides.have_outcome_epsilon = 1;
        }
        else if (strcmp(argv[i], "--list-algorithms") == 0)
            list_algorithms = 1;
        else if (strcmp(argv[i], "--list-backends") == 0)
            list_backends = 1;
        else if (strcmp(argv[i], "--show-capabilities") == 0)
            show_capabilities = 1;
        else if (strcmp(argv[i], "--validate-only") == 0)
            validate_only = 1;
        else if (strcmp(argv[i], "--estimate-only") == 0)
            estimate_only = 1;
        else if (strcmp(argv[i], "--print-execution-plan") == 0)
            print_plan = 1;
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

    if (list_algorithms)
    {
        print_algorithms();
        return 0;
    }
    if (list_backends)
    {
        print_backends();
        return 0;
    }

    {
        int have_preset = algorithm_name != NULL;
        int have_backend = backend_name != NULL;
        pe_algorithm_preset_t preset = PE_PRESET_COUNT;
        pe_compute_kind_t backend = PE_COMPUTE_COUNT;
        int have_expert_override = traversal_name != NULL || regret_name != NULL ||
                                   policy_name != NULL ||
                                   averaging_name != NULL || precision_name != NULL ||
                                   overrides.have_alpha || overrides.have_beta ||
                                   overrides.have_gamma ||
                                   overrides.have_exponential_lambda ||
                                   overrides.have_outcome_epsilon;

        if (have_preset)
        {
            preset = pe_preset_from_name(algorithm_name);
            if (preset == PE_PRESET_COUNT)
            {
                fprintf(stderr, "Unknown algorithm preset: %s\n", algorithm_name);
                return 1;
            }
            selected_preset = preset;
        }
        if (have_backend)
        {
            backend = pe_compute_kind_from_name(backend_name);
            if (backend == PE_COMPUTE_COUNT)
            {
                fprintf(stderr, "Unknown backend: %s\n", backend_name);
                return 1;
            }
            selected_backend = backend;
        }
        if (traversal_name != NULL)
        {
            overrides.traversal = pe_traversal_from_name(traversal_name);
            if (overrides.traversal == PE_TRAVERSAL_COUNT)
            {
                fprintf(stderr, "Unknown traversal: %s\n", traversal_name);
                return 1;
            }
            overrides.have_traversal = 1;
        }
        if (regret_name != NULL)
        {
            overrides.regret = pe_regret_from_name(regret_name);
            if (overrides.regret == PE_REGRET_COUNT)
            {
                fprintf(stderr, "Unknown regret mode: %s\n", regret_name);
                return 1;
            }
            overrides.have_regret = 1;
        }
        if (policy_name != NULL)
        {
            overrides.policy = pe_policy_from_name(policy_name);
            if (overrides.policy == PE_POLICY_COUNT)
            {
                fprintf(stderr, "Unknown policy: %s\n", policy_name);
                return 1;
            }
            overrides.have_policy = 1;
        }
        if (averaging_name != NULL)
        {
            overrides.averaging = pe_averaging_from_name(averaging_name);
            if (overrides.averaging == PE_AVG_COUNT)
            {
                fprintf(stderr, "Unknown averaging mode: %s\n", averaging_name);
                return 1;
            }
            overrides.have_averaging = 1;
        }
        if (precision_name != NULL)
        {
            overrides.precision = pe_precision_from_name(precision_name);
            if (overrides.precision == PE_PREC_COUNT)
            {
                fprintf(stderr, "Unknown precision mode: %s\n", precision_name);
                return 1;
            }
            overrides.have_precision = 1;
            selected_precision = overrides.precision;
        }
        if (!tree_path && (show_capabilities || validate_only || estimate_only ||
                           print_plan || have_expert_override))
        {
            if (have_expert_override && !show_capabilities && !validate_only &&
                !estimate_only && !print_plan)
                print_plan = 1;
            return run_introspection(preset, have_preset, backend, have_backend,
                                     show_capabilities, validate_only,
                                     estimate_only, print_plan, &overrides);
        }
    }

    /* Never let an explicit v3 choice disappear into the fixed legacy
       runner. A full-tree v3 request is routed below after the shared tree
       has been parsed, while --lane-b keeps its sampled semantics. */
    if (!lane_b && mkr_path != NULL &&
        (algorithm_name != NULL || backend_name != NULL ||
         traversal_name != NULL || regret_name != NULL ||
         policy_name != NULL || averaging_name != NULL ||
         precision_name != NULL || overrides.have_alpha ||
         overrides.have_beta || overrides.have_gamma ||
         overrides.have_exponential_lambda ||
         overrides.have_outcome_epsilon))
    {
        fprintf(stderr,
                "--mkr is an import path and cannot be combined with a full-tree "
                "v3 algorithm/backend override; remove --mkr\n");
        return 2;
    }
    {
        const pe_runtime_backend_info_t *backend_info;
        if (pe_runtime_probe(&runtime_caps) != 0 ||
            selected_backend < 0 || selected_backend >= PE_COMPUTE_COUNT)
        {
            fprintf(stderr, "Could not probe the requested backend\n");
            return 2;
        }
        runtime_caps_valid = 1;
        if (selected_backend == PE_COMPUTE_AUTO)
        {
            selected_backend = pe_runtime_recommended_backend(&runtime_caps);
            if (selected_backend == PE_COMPUTE_AUTO)
            {
                fprintf(stderr, "no validated runtime solver backend is available\n");
                return 2;
            }
            printf("backend_auto_resolved=%s\n",
                   pe_compute_kind_name(selected_backend));
        }
        backend_info = &runtime_caps.backends[selected_backend];
        if (!backend_info->runtime_available || !backend_info->validated)
        {
            fprintf(stderr, "backend refused: %s (%s)\n",
                    pe_compute_kind_name(selected_backend),
                    backend_info->reason);
            return 2;
        }
    }

    if (!tree_path)
    {
        usage(argv[0]);
        return 1;
    }

    pe_monker_tree_header_t monker_header;
    int binary_tree = 0;
    mpf_tree_def_t *tree = NULL;
    if (!load_cli_tree(tree_path, &tree, &monker_header, &binary_tree))
        return 1;
    mpf_tree_error_t tree_err = {0};
    if (!mpf_tree_validate(tree, &tree_err))
    {
        fprintf(stderr, "Tree validation error: %s\n", tree_err.message[0] ? tree_err.message : "unknown");
        mpf_tree_free(tree);
        return 1;
    }

    int inferred_players = infer_players_from_tree(tree);
    int num_players = players_override > 0 ? players_override :
        (binary_tree && monker_header.player_count > 0 ?
         (int)monker_header.player_count : (inferred_players > 0 ? inferred_players : 2));
    if (num_players <= 0 || num_players > MPF_MAX_PLAYERS)
    {
        fprintf(stderr, "Unsupported player count: %d\n", num_players);
        mpf_tree_free(tree);
        return 1;
    }

    mpf_rule_t rules = MPF_RULE_PLO4;
    enum_game_t range_game = game_omaha;
    if (rules_name)
    {
        if (!parse_rule_name(rules_name, &rules, &range_game))
        {
            fprintf(stderr, "Unknown rules kind: %s\n", rules_name);
            mpf_tree_free(tree);
            return 1;
        }
    }
    else if (mkr_path)
    {
        /* The strategy class reader is currently exact for Monker's PLO4
           four-card table. A future Hold'em archive can select its variant
           explicitly with --rules. */
        rules = MPF_RULE_PLO4;
        range_game = game_omaha;
    }
    else
    {
        rules = MPF_RULE_HOLDEM;
        range_game = game_holdem;
    }

    mpf_street_t start_street = binary_tree ?
        (mpf_street_t)monker_header.street : MPF_STREET_PREFLOP;
    if (street_name && !parse_street_name(street_name, &start_street))
    {
        fprintf(stderr, "Unknown street: %s\n", street_name);
        mpf_tree_free(tree);
        return 1;
    }
    if (start_street < MPF_STREET_PREFLOP || start_street > MPF_STREET_RIVER)
    {
        fprintf(stderr, "Unsupported start street: %d\n", (int)start_street);
        mpf_tree_free(tree);
        return 1;
    }

    int board_cards[5] = {0};
    int board_count = 0;
    mask_t board_mask = MASK_EMPTY;
    if (board_text && !parse_board(board_text, board_cards, &board_count, &board_mask))
    {
        fprintf(stderr, "Invalid board (expected cards such as AsKdQcJdTh)\n");
        mpf_tree_free(tree);
        return 1;
    }
    int expected_board = start_street == MPF_STREET_FLOP ? 3 :
                         start_street == MPF_STREET_TURN ? 4 :
                         start_street == MPF_STREET_RIVER ? 5 : 0;
    if ((expected_board > 0 && board_count != expected_board) ||
        (expected_board == 0 && board_count != 0))
    {
        fprintf(stderr, "Board has %d cards but %s requires %d\n",
                board_count, street_name ? street_name : "the selected street",
                expected_board);
        mpf_tree_free(tree);
        return 1;
    }
    if (mkr_path && rules != MPF_RULE_PLO4)
    {
        fprintf(stderr, "Imported Monker strategies currently require --rules plo4\n");
        mpf_tree_free(tree);
        return 1;
    }

    pe_range_t *ranges[MPF_MAX_PLAYERS] = {0};
    pe_monker_range_set_t tree_ranges = {0};
    if (binary_tree)
    {
        pe_monker_status_t range_status = pe_monker_tree_read_ranges(tree_path, &tree_ranges);
        if (range_status != PE_MONKER_OK)
        {
            fprintf(stderr, "Monker range block error: %s\n",
                    pe_monker_status_string(range_status));
            mpf_tree_free(tree);
            return 1;
        }
        if (tree_ranges.player_count != 0 && tree_ranges.player_count != (uint32_t)num_players)
        {
            fprintf(stderr, "Tree contains ranges for %u players, spot has %d\n",
                    tree_ranges.player_count, num_players);
            pe_monker_range_set_free(&tree_ranges);
            mpf_tree_free(tree);
            return 1;
        }
        for (uint32_t p = 0; p < tree_ranges.player_count; ++p)
        {
            monker_to_internal_card_mask(tree_ranges.players[p]);
            if (pe_solver_range_prepare(tree_ranges.players[p]) != PE_SOLVER_OK)
            {
                fprintf(stderr, "Tree range %u is empty after conversion\n", p);
                pe_monker_range_set_free(&tree_ranges);
                mpf_tree_free(tree);
                return 1;
            }
            ranges[p] = tree_ranges.players[p];
        }
        if (!rules_name && tree_ranges.combo_count == 270725u)
        {
            rules = MPF_RULE_PLO4;
            range_game = game_omaha;
        }
    }
    for (int p = 0; p < num_players; ++p)
    {
        if (!range_text[p])
            continue;
        if (pe_solver_range_parse(range_game, range_text[p],
                                  modern_to_std_mask(board_mask), &ranges[p]) != PE_SOLVER_OK)
        {
            fprintf(stderr, "Invalid range for player %d: %s\n", p, range_text[p]);
            for (int q = 0; q < num_players; ++q)
                if (ranges[q] && (!tree_ranges.players || ranges[q] != tree_ranges.players[q]))
                    pe_range_free(ranges[q]);
            pe_monker_range_set_free(&tree_ranges);
            mpf_tree_free(tree);
            return 1;
        }
    }

    EvalConfig ecfg = (rules == MPF_RULE_HOLDEM || rules == MPF_RULE_SHORTDECK) ?
        eval_config_holdem() : eval_config_omaha();
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
    cfg.rules = rules;
    cfg.num_players = num_players;
    cfg.button_index = button_index % num_players;
    cfg.start_street = start_street;
    cfg.board_card_count = board_count;
    for (int i = 0; i < board_count; ++i)
        cfg.board_cards[i] = board_cards[i];
    cfg.bet_size_count_common = 1;
    cfg.bet_sizes_common[0] = bb_amount * 3.0;
    cfg.raise_cap = 4;
    cfg.enable_pot_sizing = 0;
    cfg.sb = sb_amount;
    cfg.bb = bb_amount;
    cfg.ante = ante_amount;
    cfg.tree = tree;
    cfg.tree_enforced = 1;
    cfg.perf_pool = perf_pool;
    for (int i = 0; i < cfg.num_players; ++i)
    {
        cfg.range[i] = ranges[i];
        cfg.stacks[i] = (binary_tree && monker_header.stacks[i] > 0.0) ?
            monker_header.stacks[i] : stack_amount;
    }
    if (binary_tree)
    {
        cfg.preflop.defined = 1;
        cfg.preflop.has_to_act = 1;
        cfg.preflop.to_act = monker_header.first_to_act;
        if (start_street == MPF_STREET_PREFLOP)
        {
            double committed_pot = monker_header.dead_money;
            cfg.preflop.has_round = 1;
            for (int i = 0; i < cfg.num_players; ++i)
            {
                cfg.preflop.round_contrib[i] = monker_header.committed[i];
                committed_pot += monker_header.committed[i];
            }
            cfg.preflop.has_pot = 1;
            cfg.preflop.pot = committed_pot;
        }
    }
    if (have_pot || have_to_call || have_current_bet || have_raises || have_to_act)
    {
        cfg.preflop.defined = 1;
        if (have_pot) { cfg.preflop.has_pot = 1; cfg.preflop.pot = pot_amount; }
        if (have_to_call) { cfg.preflop.has_to_call = 1; cfg.preflop.to_call = to_call_amount; }
        if (have_current_bet)
        {
            cfg.preflop.has_current_bet = 1;
            cfg.preflop.current_bet = current_bet_amount;
        }
        if (have_raises) { cfg.preflop.has_raises = 1; cfg.preflop.raises_made = raises_made; }
        if (have_to_act) { cfg.preflop.has_to_act = 1; cfg.preflop.to_act = to_act; }
    }

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

    if (!lane_b &&
        (algorithm_name != NULL || backend_name != NULL ||
         traversal_name != NULL || regret_name != NULL ||
         policy_name != NULL || averaging_name != NULL ||
         precision_name != NULL || overrides.have_alpha ||
         overrides.have_beta || overrides.have_gamma ||
         overrides.have_exponential_lambda ||
         overrides.have_outcome_epsilon))
    {
        pe_algorithm_preset_t v3_algorithm = algorithm_name != NULL
            ? selected_preset : PE_PRESET_CFR_VECTOR;
        if (overrides.have_traversal &&
            overrides.traversal == PE_TRAVERSAL_CHANCE_VECTOR)
        {
            fprintf(stderr,
                    "full-tree v3 currently does not expose the chance-vector "
                    "traversal; use full-vector/full-scalar or --lane-b\n");
            mpf_state_cleanup(&root_state);
            mpf_perf_stats_pool_destroy(perf_pool);
            eval_context_destroy(ctx);
            for (int p = 0; p < num_players; ++p)
                if (ranges[p] && (!tree_ranges.players || ranges[p] != tree_ranges.players[p]))
                    pe_range_free(ranges[p]);
            pe_monker_range_set_free(&tree_ranges);
            mpf_tree_free(tree);
            return 2;
        }
        int v3_status = run_vector_backend(&game, iterations,
                                           pe_compute_kind_name(selected_backend),
                                           v3_algorithm, selected_precision,
                                           cpu_threads, &overrides);
        mpf_state_cleanup(&root_state);
        mpf_perf_stats_pool_destroy(perf_pool);
        eval_context_destroy(ctx);
        for (int p = 0; p < num_players; ++p)
            if (ranges[p] && (!tree_ranges.players || ranges[p] != tree_ranges.players[p]))
                pe_range_free(ranges[p]);
        pe_monker_range_set_free(&tree_ranges);
        mpf_tree_free(tree);
        return v3_status == 0 ? 0 : 1;
    }

    /* The same fully configured legacy tree can now be driven by Lane B.  This
       is intentionally a separate execution path: the legacy CFR storage is
       not mixed with the v3 storage, while the game adapter preserves all
       existing action, street, board-chance and terminal semantics. */
    if (lane_b)
    {
        pe_cfr_external_adapter_t adapter;
        pe_solver_config_t lane_cfg = pe_solver_config_default();
        pe_solver_deps_t lane_deps = pe_solver_deps_default();
        pe_solver_t *lane_solver;
        pe_solver_status_t lane_status;
        pe_progress_t lane_progress;
        pe_metrics_t lane_metrics;
        clock_t benchmark_start = clock();
        if (mkr_path || pe_cfr_external_adapter_init(&adapter, &game) != 0)
        {
            fprintf(stderr, "Lane B requires a compatible solve tree and does not import --mkr strategies\n");
            mpf_state_cleanup(&root_state);
            mpf_perf_stats_pool_destroy(perf_pool);
            eval_context_destroy(ctx);
            for (int p = 0; p < num_players; ++p)
                if (ranges[p] && (!tree_ranges.players || ranges[p] != tree_ranges.players[p]))
                    pe_range_free(ranges[p]);
            pe_monker_range_set_free(&tree_ranges);
            mpf_tree_free(tree);
            return 1;
        }
        pe_compute_kind_t lane_backend = selected_backend == PE_COMPUTE_AUTO
            ? PE_COMPUTE_CPU_REF : selected_backend;

        if (lane_backend == PE_COMPUTE_CUDA || lane_backend == PE_COMPUTE_OPENCL)
        {
            fprintf(stderr,
                    "Lane B sampled traversal is not GPU-enabled in this build; "
                    "use the GPU vector path without --lane-b\n");
            mpf_state_cleanup(&root_state);
            mpf_perf_stats_pool_destroy(perf_pool);
            eval_context_destroy(ctx);
            for (int p = 0; p < num_players; ++p)
                if (ranges[p] && (!tree_ranges.players || ranges[p] != tree_ranges.players[p]))
                    pe_range_free(ranges[p]);
            pe_monker_range_set_free(&tree_ranges);
            mpf_tree_free(tree);
            return 2;
        }
        lane_cfg.algorithm.preset = selected_preset;
        if (overrides_have_axis(&overrides))
        {
            /* Keep the preset as the baseline, but make every explicit axis
               authoritative. This is the same resolution contract exposed by
               --print-execution-plan, now applied to the real Lane B run. */
            if (pe_preset_expand(selected_preset, &lane_cfg.algorithm) != 0)
            {
                fprintf(stderr, "could not expand selected algorithm preset\n");
                mpf_state_cleanup(&root_state);
                mpf_perf_stats_pool_destroy(perf_pool);
                eval_context_destroy(ctx);
                for (int p = 0; p < num_players; ++p)
                    if (ranges[p] && (!tree_ranges.players || ranges[p] != tree_ranges.players[p]))
                        pe_range_free(ranges[p]);
                pe_monker_range_set_free(&tree_ranges);
                mpf_tree_free(tree);
                return 2;
            }
            lane_cfg.algorithm.preset = PE_PRESET_CUSTOM;
            if (overrides.have_traversal)
                lane_cfg.algorithm.traversal = overrides.traversal;
            if (overrides.have_regret)
                lane_cfg.algorithm.regret = overrides.regret;
            if (overrides.have_policy)
                lane_cfg.algorithm.policy = overrides.policy;
            if (overrides.have_averaging)
                lane_cfg.algorithm.averaging = overrides.averaging;
        }
        else if (pe_preset_expand(selected_preset, &lane_cfg.algorithm) != 0)
        {
            fprintf(stderr, "could not expand selected algorithm preset\n");
            mpf_state_cleanup(&root_state);
            mpf_perf_stats_pool_destroy(perf_pool);
            eval_context_destroy(ctx);
            for (int p = 0; p < num_players; ++p)
                if (ranges[p] && (!tree_ranges.players || ranges[p] != tree_ranges.players[p]))
                    pe_range_free(ranges[p]);
            pe_monker_range_set_free(&tree_ranges);
            mpf_tree_free(tree);
            return 2;
        }
        if (overrides.have_alpha)
            lane_cfg.algorithm.dcfr_alpha = overrides.alpha;
        if (overrides.have_beta)
            lane_cfg.algorithm.dcfr_beta = overrides.beta;
        if (overrides.have_gamma)
            lane_cfg.algorithm.dcfr_gamma = overrides.gamma;
        if (overrides.have_exponential_lambda)
            lane_cfg.algorithm.exponential_lambda = overrides.exponential_lambda;
        if (overrides.have_outcome_epsilon)
            lane_cfg.algorithm.outcome_epsilon = overrides.outcome_epsilon;

        /* Lane B supports external and outcome sampling. A custom full-tree
           axis is rejected here instead of silently falling back to a sampled
           algorithm with different semantics. */
        if (lane_cfg.algorithm.traversal != PE_TRAVERSAL_EXTERNAL_SAMPLING &&
            lane_cfg.algorithm.traversal != PE_TRAVERSAL_OUTCOME_SAMPLING)
        {
            fprintf(stderr,
                    "Lane B sampled traversal requires external-sampling or "
                    "outcome-sampling; requested %s\n",
                    pe_traversal_name(lane_cfg.algorithm.traversal));
            mpf_state_cleanup(&root_state);
            mpf_perf_stats_pool_destroy(perf_pool);
            eval_context_destroy(ctx);
            for (int p = 0; p < num_players; ++p)
                if (ranges[p] && (!tree_ranges.players || ranges[p] != tree_ranges.players[p]))
                    pe_range_free(ranges[p]);
            pe_monker_range_set_free(&tree_ranges);
            mpf_tree_free(tree);
            return 2;
        }
        lane_cfg.execution.backend = lane_backend;
        lane_cfg.execution.stages.traversal = lane_backend;
        lane_cfg.execution.stages.update = lane_backend;
        lane_cfg.execution.stages.terminal_eval = lane_backend;
        lane_cfg.execution.precision = selected_precision;
        lane_cfg.execution.cpu_threads = cpu_threads;
        lane_cfg.execution.sample_batch_size = (size_t)sample_batch;
        lane_cfg.execution.deterministic = 1;
        lane_cfg.problem.expected_infosets = 1u;
        lane_cfg.problem.expected_actions = 2u;
        lane_cfg.problem.expected_combos = 1u;
        lane_cfg.max_iterations = (uint64_t)iterations;
        lane_cfg.seed = UINT64_C(0x50455f4c414e455f) ^ (uint64_t)iterations;
        lane_deps.external_game = pe_cfr_external_adapter_game(&adapter);
        lane_solver = pe_solver_create(&lane_cfg, &lane_deps);
        lane_status = lane_solver ? pe_solver_run(lane_solver) : PE_SOLVER_ERR_OUT_OF_MEMORY;
        clock_t benchmark_end = clock();
        double elapsed_cpu = benchmark_end >= benchmark_start &&
                CLOCKS_PER_SEC > 0
            ? (double)(benchmark_end - benchmark_start) / (double)CLOCKS_PER_SEC
            : 0.0;
        uint64_t trajectories = (uint64_t)iterations * (uint64_t)sample_batch;
        double trajectories_per_second = elapsed_cpu > 0.0
            ? (double)trajectories / elapsed_cpu : 0.0;
        printf("lane_b=algorithm=%s traversal=%s regret=%s policy=%s "
               "averaging=%s backend=%s precision=%s threads=%d "
               "sample_batch=%d iterations=%d status=%d\n",
               lane_cfg.algorithm.preset == PE_PRESET_CUSTOM
                   ? "custom" : pe_preset_name(selected_preset),
               pe_traversal_name(lane_cfg.algorithm.traversal),
               pe_regret_name(lane_cfg.algorithm.regret),
               pe_policy_name(lane_cfg.algorithm.policy),
               pe_averaging_name(lane_cfg.algorithm.averaging),
               pe_compute_kind_name(lane_backend),
               pe_precision_name(selected_precision), cpu_threads,
               sample_batch, iterations, (int)lane_status);
        printf("lane_b_runtime=backend_validated=%d simd_detected=%s "
               "simd_cfr=not-integrated\n",
               runtime_caps_valid ? 1 : 0,
               runtime_caps_valid ? pe_runtime_simd_name(runtime_caps.simd)
                                   : "unknown");
        printf("lane_b_benchmark=cpu_seconds:%.6f trajectories:%llu throughput:%.3f/s\n",
               elapsed_cpu, (unsigned long long)trajectories,
               trajectories_per_second);
        if (lane_solver && pe_solver_progress(lane_solver, &lane_progress) == PE_SOLVER_OK)
            printf("lane_b_progress=%.6f complete=%d iteration=%llu\n",
                   lane_progress.fraction, lane_progress.complete,
                   (unsigned long long)lane_progress.iteration);
        if (lane_solver && pe_solver_metrics(lane_solver, &lane_metrics) == PE_SOLVER_OK)
            printf("lane_b_br_guarantee=%d exploitability_mbb=%.6f\n",
                   (int)lane_metrics.guarantee,
                   lane_metrics.exploitability_mbb_per_game);
        if (benchmark_json_path)
        {
            FILE *benchmark_file = fopen(benchmark_json_path, "w");
            if (!benchmark_file)
                fprintf(stderr, "Failed to write benchmark JSON '%s': %s\n",
                        benchmark_json_path, strerror(errno));
            else
            {
                fprintf(benchmark_file,
                        "{\"schema\":\"pe-lane-b-benchmark/v1\","
                        "\"algorithm\":\"%s\",\"traversal\":\"%s\","
                        "\"regret\":\"%s\",\"policy\":\"%s\","
                        "\"averaging\":\"%s\",\"backend\":\"%s\","
                        "\"backend_validated\":%s,\"precision\":\"%s\","
                        "\"simd_detected\":\"%s\","
                        "\"simd_cfr_integrated\":false,\"threads\":%d,"
                        "\"iterations\":%d,\"sample_batch\":%d,"
                        "\"trajectories\":%llu,\"cpu_seconds\":%.9f,"
                        "\"trajectories_per_second\":%.9f,\"status\":%d}\n",
                        lane_cfg.algorithm.preset == PE_PRESET_CUSTOM
                            ? "custom" : pe_preset_name(selected_preset),
                        pe_traversal_name(lane_cfg.algorithm.traversal),
                        pe_regret_name(lane_cfg.algorithm.regret),
                        pe_policy_name(lane_cfg.algorithm.policy),
                        pe_averaging_name(lane_cfg.algorithm.averaging),
                        pe_compute_kind_name(lane_backend),
                        runtime_caps_valid ? "true" : "false",
                        pe_precision_name(selected_precision),
                        runtime_caps_valid ? pe_runtime_simd_name(runtime_caps.simd)
                                           : "unknown",
                        cpu_threads,
                        iterations, sample_batch,
                        (unsigned long long)trajectories, elapsed_cpu,
                        trajectories_per_second, (int)lane_status);
                fclose(benchmark_file);
            }
        }
        pe_solver_destroy(lane_solver);
        mpf_state_cleanup(&root_state);
        mpf_perf_stats_pool_destroy(perf_pool);
        eval_context_destroy(ctx);
        for (int p = 0; p < num_players; ++p)
            if (ranges[p] && (!tree_ranges.players || ranges[p] != tree_ranges.players[p]))
                pe_range_free(ranges[p]);
        pe_monker_range_set_free(&tree_ranges);
        mpf_tree_free(tree);
        return lane_status == PE_SOLVER_OK ? 0 : 1;
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

    pe_monker_mkr_t archive = {0};
    pe_monker_mkr_metadata_t metadata = {0};
    pe_monker_mkr_strategy_t stored = {0};
    pe_monker_classes_t *classes = NULL;
    pe_monker_strategy_t *imported = NULL;
    if (mkr_path)
    {
        pe_monker_mkr_status_t mkr_status = pe_monker_mkr_read(mkr_path, &archive);
        pe_monker_mkr_status_t strategy_status = PE_MONKER_MKR_OK;
        int metadata_ok = pe_monker_mkr_read_metadata(&archive, &metadata) ==
                          PE_MONKER_MKR_OK;
        if (mkr_status == PE_MONKER_MKR_OK)
            strategy_status = pe_monker_mkr_read_strategy(&archive, strategy_name, &stored);
        if (mkr_status != PE_MONKER_MKR_OK || strategy_status != PE_MONKER_MKR_OK)
        {
            fprintf(stderr, "Monker .mkr load error: %s\n", mkr_status != PE_MONKER_MKR_OK ?
                    pe_monker_mkr_status_string(mkr_status) :
                    pe_monker_mkr_status_string(strategy_status));
            pe_monker_mkr_strategy_free(&stored);
            pe_monker_mkr_free(&archive);
            cfr_storage_destroy(storage);
            mpf_state_cleanup(&root_state);
            mpf_perf_stats_pool_destroy(perf_pool);
            eval_context_destroy(ctx);
            for (int p = 0; p < num_players; ++p)
                if (ranges[p] && (!tree_ranges.players || ranges[p] != tree_ranges.players[p]))
                    pe_range_free(ranges[p]);
            pe_monker_range_set_free(&tree_ranges);
            mpf_tree_free(tree);
            return 1;
        }
        if (pe_monker_classes_create(&classes) != PE_MONKER_OK ||
            pe_monker_strategy_open(tree, &stored, classes, &imported) != PE_MONKER_OK)
        {
            fprintf(stderr, "Monker strategy does not match the supplied tree\n");
            pe_monker_classes_destroy(classes);
            pe_monker_mkr_strategy_free(&stored);
            pe_monker_mkr_free(&archive);
            cfr_storage_destroy(storage);
            mpf_state_cleanup(&root_state);
            mpf_perf_stats_pool_destroy(perf_pool);
            eval_context_destroy(ctx);
            for (int p = 0; p < num_players; ++p)
                if (ranges[p] && (!tree_ranges.players || ranges[p] != tree_ranges.players[p]))
                    pe_range_free(ranges[p]);
            pe_monker_range_set_free(&tree_ranges);
            mpf_tree_free(tree);
            return 1;
        }
        if (metadata_ok)
            printf("Monker archive: strategy=%s iterations=%lld version=%lld classes=%u\n",
                   strategy_name, (long long)metadata.iterations,
                   (long long)metadata.version,
                   pe_monker_strategy_class_count(imported));
        else
            printf("Monker archive: strategy=%s classes=%u (metadata absent)\n",
                   strategy_name, pe_monker_strategy_class_count(imported));
    }

    if (validate_only)
    {
        printf("validation=ok tree_format=%s players=%d street=%d ranges=%s strategy=%s\n",
               binary_tree ? "monker" : "json", num_players, (int)start_street,
               ranges[0] ? "loaded" : "not-supplied",
               imported ? "bound" : "not-supplied");
        if (imported)
        {
            pe_monker_strategy_close(imported);
            pe_monker_classes_destroy(classes);
            pe_monker_mkr_strategy_free(&stored);
            pe_monker_mkr_free(&archive);
        }
        cfr_storage_destroy(storage);
        mpf_state_cleanup(&root_state);
        mpf_perf_stats_pool_destroy(perf_pool);
        eval_context_destroy(ctx);
        for (int p = 0; p < num_players; ++p)
            if (ranges[p] && (!tree_ranges.players || ranges[p] != tree_ranges.players[p]))
                pe_range_free(ranges[p]);
        pe_monker_range_set_free(&tree_ranges);
        mpf_tree_free(tree);
        return 0;
    }

    size_t imported_lock_count = 0;
    if (imported)
    {
        monker_lock_walk_t walk;
        memset(&walk, 0, sizeof(walk));
        walk.game = &game;
        walk.storage = storage;
        walk.strategy = imported;
        walk.classes = classes;
        uint64_t root_key = (uint64_t)(uintptr_t)game.initial_state;
        if (seed_monker_locks(&walk, root_key, 0) != 0)
        {
            fprintf(stderr, "Failed while binding imported strategy to the spot\n");
            free(walk.visited);
            pe_monker_strategy_close(imported);
            pe_monker_classes_destroy(classes);
            pe_monker_mkr_strategy_free(&stored);
            pe_monker_mkr_free(&archive);
            cfr_storage_destroy(storage);
            mpf_state_cleanup(&root_state);
            mpf_perf_stats_pool_destroy(perf_pool);
            eval_context_destroy(ctx);
            for (int p = 0; p < num_players; ++p)
                if (ranges[p] && (!tree_ranges.players || ranges[p] != tree_ranges.players[p]))
                    pe_range_free(ranges[p]);
            pe_monker_range_set_free(&tree_ranges);
            mpf_tree_free(tree);
            return 1;
        }
        imported_lock_count = walk.lock_count;
        free(walk.visited);
        printf("Monker strategy bound: %zu infosets\n", imported_lock_count);
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
    if (imported)
    {
        cfr_exploitability_result_t result;
        memset(&result, 0, sizeof(result));
        if (cfr_exploitability_multiway(&game, storage, NULL, &result) != 0)
        {
            fprintf(stderr, "Failed to evaluate imported strategy exploitability\n");
            if (metrics_file) fclose(metrics_file);
            if (metrics_buffer) cfr_metrics_buffer_destroy(metrics_buffer);
            pe_monker_strategy_close(imported);
            pe_monker_classes_destroy(classes);
            pe_monker_mkr_strategy_free(&stored);
            pe_monker_mkr_free(&archive);
            cfr_storage_destroy(storage);
            mpf_state_cleanup(&root_state);
            mpf_perf_stats_pool_destroy(perf_pool);
            eval_context_destroy(ctx);
            for (int p = 0; p < num_players; ++p)
                if (ranges[p] && (!tree_ranges.players || ranges[p] != tree_ranges.players[p]))
                    pe_range_free(ranges[p]);
            pe_monker_range_set_free(&tree_ranges);
            mpf_tree_free(tree);
            return 1;
        }
        exploitability = result.total_exploitability;
        printf("Imported strategy results: exploitability=%.6f\n", exploitability);
        for (int p = 0; p < result.num_players; ++p)
            printf("  player%d policy=%.6f br=%.6f gap=%.6f\n", p,
                   result.policy_value[p], result.br_value[p],
                   result.exploitability[p]);
    }
    else
    {
        cfr_solve(&game, storage, &solve_cfg, &exploitability);
    }

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
    if (imported)
    {
        pe_monker_strategy_close(imported);
        pe_monker_classes_destroy(classes);
        pe_monker_mkr_strategy_free(&stored);
        pe_monker_mkr_free(&archive);
    }
    for (int p = 0; p < num_players; ++p)
        if (ranges[p] && (!tree_ranges.players || ranges[p] != tree_ranges.players[p]))
            pe_range_free(ranges[p]);
    pe_monker_range_set_free(&tree_ranges);
    mpf_state_cleanup(&root_state);
    mpf_perf_stats_pool_destroy(perf_pool);
    eval_context_destroy(ctx);
    mpf_tree_free(tree);
    return 0;
}
