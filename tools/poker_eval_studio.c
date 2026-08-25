/*
 * poker-eval Studio - native desktop shell.
 *
 * This is intentionally a real setup/solve workflow, not a screenshot layer:
 * the .tree header is decoded before it is accepted, the board is validated
 * against the tree street, and Solve executes the same pe-vector-sim seam as
 * the command line. NAppGUI supplies native controls on macOS, Linux and
 * Windows while the solver remains pure C.
 */
#include <nappgui.h>

#include <poker_eval/solver/pe_monker.h>
#include <poker_eval/engine/solvers/cfr/mpf_tree.h>
#include <osbs/bmutex.h>
#include <osbs/bproc.h>

#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#define PE_ACCESS _access
#define PE_X_OK 0
#define PE_PATH_SEPARATOR '\\'
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#define PE_PATH_SEPARATOR '/'
#include <unistd.h>
#define PE_ACCESS access
#define PE_X_OK X_OK
#else
#define PE_PATH_SEPARATOR '/'
#include <unistd.h>
#define PE_ACCESS access
#define PE_X_OK X_OK
#endif

#ifdef _WIN32
#define POPEN _popen
#define PCLOSE _pclose
#else
#define POPEN popen
#define PCLOSE pclose
#endif

typedef struct _app_t App;

struct _app_t
{
    Window *window;
    Combo *game_combo;
    Combo *players_combo;
    Edit *tree_edit;
    Edit *mkr_edit;
    Edit *board_edit;
    Edit *range0_edit;
    Edit *range1_edit;
    Edit *runner_edit;
    Edit *iterations_edit;
    Edit *target_edit;
    Edit *interval_edit;
    TextView *status;
    Label *board_label;
    Label *run_state;
    Label *run_progress;
    Label *run_metrics;
    Label *run_config;
    Progress *run_progress_bar;
    Button *solve_button;
    Button *stop_button;
    Mutex *solve_mutex;
    Proc *solve_proc;
    int solve_running;
    int solve_cancel_requested;
    uint32_t solve_exit_code;
    char solve_command[8192];
    char solve_output[16384];
};

static void i_on_close(App *app, Event *event);

static int parse_ui_u64(const char *text, uint64_t *value)
{
    char *end = NULL;
    unsigned long long parsed;
    if (!text || !*text || !value)
        return -1;
    errno = 0;
    parsed = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed == 0u)
        return -1;
    *value = (uint64_t)parsed;
    return 0;
}

static int parse_ui_target(const char *text, double *value)
{
    char *end = NULL;
    double parsed;
    if (!text || !*text || !value)
        return -1;
    errno = 0;
    parsed = strtod(text, &end);
    if (errno != 0 || end == text || *end != '\0' || parsed < 0.0)
        return -1;
    *value = parsed;
    return 0;
}

static int card_count(const char *text)
{
    int count = 0;
    const char *cursor = text;
    while (cursor && *cursor)
    {
        while (*cursor == ' ' || *cursor == '\t' || *cursor == ',') ++cursor;
        if (!*cursor) break;
        if (cursor[0] == '\0' || cursor[1] == '\0') return -1;
        ++count;
        cursor += 2;
        while (*cursor && *cursor != ' ' && *cursor != '\t' && *cursor != ',') ++cursor;
    }
    return count;
}

static int street_cards(int street)
{
    return street == 1 ? 3 : street == 2 ? 4 : street == 3 ? 5 : 0;
}

static const char *street_name(int street)
{
    static const char *names[] = {"preflop", "flop", "turn", "river"};
    return street >= 0 && street < 4 ? names[street] : "unknown";
}

static const char *game_name(enum_game_t game)
{
    if (game == game_holdem) return "holdem";
    if (game == game_omaha) return "plo4";
    if (game == game_omaha5) return "plo5";
    if (game == game_omaha6) return "plo6";
    return "unknown";
}

static uint32_t game_index(enum_game_t game)
{
    if (game == game_holdem) return 0u;
    if (game == game_omaha) return 1u;
    if (game == game_omaha5) return 2u;
    return 3u;
}

static enum_game_t game_from_index(uint32_t index)
{
    static const enum_game_t games[] = {game_holdem, game_omaha,
                                        game_omaha5, game_omaha6};
    return index < sizeof(games) / sizeof(games[0]) ? games[index] : game_holdem;
}

static uint8_t hole_cards_from_game(enum_game_t game)
{
    return game == game_holdem ? 2u : game == game_omaha ? 4u :
           game == game_omaha5 ? 5u : game == game_omaha6 ? 6u : 0u;
}

static void infer_game_from_path(App *app, const char *path)
{
    if (!app || !path || !app->game_combo) return;
    if (strstr(path, "plo6") != NULL) combo_selected(app->game_combo, 3u);
    else if (strstr(path, "plo5") != NULL) combo_selected(app->game_combo, 2u);
    else if (strstr(path, "plo") != NULL) combo_selected(app->game_combo, 1u);
    else if (strstr(path, "holdem") != NULL || strstr(path, "hold'em") != NULL)
        combo_selected(app->game_combo, 0u);
}

static enum_game_t selected_game(const App *app)
{
    return app && app->game_combo ? game_from_index(combo_get_selected(app->game_combo)) : game_holdem;
}

static uint32_t selected_players(const App *app)
{
    return app && app->players_combo ? combo_get_selected(app->players_combo) + 2u : 2u;
}

static void status(App *app, const char *format, ...)
{
    char buffer[8192];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    textview_clear(app->status);
    textview_writef(app->status, buffer);
}

static int last_progress_line(const char *output, uint64_t *iteration,
                              uint64_t *total, double *fraction,
                              double *exploitability, double *target)
{
    const char *cursor;
    const char *found = NULL;
    if (!output)
        return 0;
    cursor = output;
    while ((cursor = strstr(cursor, "progress iteration=")) != NULL)
    {
        found = cursor;
        cursor += 9u;
    }
    if (!found || sscanf(found,
                         "progress iteration=%" SCNu64 " total=%" SCNu64
                         " fraction=%lf exploitability_mbb=%lf target_mbb=%lf",
                         iteration, total, fraction, exploitability, target) != 5)
        return 0;
    return 1;
}

static int last_result_line(const char *output, char *guarantee,
                            size_t guarantee_capacity, double *raw,
                            double *mbb, uint64_t *samples)
{
    const char *cursor;
    const char *found = NULL;
    if (!output || !guarantee || guarantee_capacity == 0u)
        return 0;
    cursor = output;
    while ((cursor = strstr(cursor, "guarantee=")) != NULL)
    {
        found = cursor;
        cursor += 10u;
    }
    if (!found || sscanf(found,
                         "guarantee=%31s exploitability_raw=%lf"
                         " exploitability_mbb=%lf br_samples=%" SCNu64,
                         guarantee, raw, mbb, samples) != 4)
        return 0;
    guarantee[guarantee_capacity - 1u] = '\0';
    return 1;
}

static void update_result_view(App *app, const char *output, int running)
{
    uint64_t iteration = 0u;
    uint64_t total = 0u;
    uint64_t samples = 0u;
    double fraction = 0.0;
    double exploitability = 0.0;
    double target = 0.0;
    double raw = 0.0;
    double mbb = 0.0;
    char guarantee[32] = "not measured";
    char text[256];

    if (!app)
        return;
    if (last_progress_line(output, &iteration, &total, &fraction,
                           &exploitability, &target))
    {
        progress_value(app->run_progress_bar, (real32_t)fraction);
        snprintf(text, sizeof(text),
                 "%s  |  iteration %" PRIu64 " / %" PRIu64
                 "  |  %.1f%%",
                 running ? "RUNNING" : "LAST CHECK", iteration, total,
                 fraction * 100.0);
        label_text(app->run_progress, text);
        snprintf(text, sizeof(text),
                 "Empirical exploitability: %.2f mBB  |  stop target: %.2f mBB",
                 exploitability, target);
        label_text(app->run_metrics, text);
    }
    else if (!running)
    {
        progress_value(app->run_progress_bar, 0.0f);
        label_text(app->run_progress, "READY  |  no convergence sample yet");
        label_text(app->run_metrics, "Empirical exploitability: not measured");
    }

    if (last_result_line(output, guarantee, sizeof(guarantee), &raw, &mbb,
                         &samples))
    {
        snprintf(text, sizeof(text),
                 "Final: %s  |  %.2f mBB  |  raw %.5f  |  BR samples %" PRIu64,
                 guarantee, mbb, raw, samples);
        label_text(app->run_metrics, text);
    }
    label_text(app->run_state, running ? "RUN IN PROGRESS"
                                       : output && *output ? "RESULTS READY"
                                                            : "READY");
}

static int read_tree(App *app, const char *path, pe_monker_tree_header_t *header,
                     pe_monker_combo_layout_t *layout)
{
    pe_monker_range_set_t ranges;
    mpf_tree_def_t *tree = NULL;
    pe_monker_status_t tree_status;
    int ranges_present = 0;

    memset(&ranges, 0, sizeof(ranges));
    tree_status = pe_monker_tree_read_header(path, header);
    if (tree_status != PE_MONKER_OK)
    {
        status(app, "TREE ERROR\n%s", pe_monker_status_string(tree_status));
        return -1;
    }
    tree_status = pe_monker_tree_load(path, &tree);
    if (tree_status != PE_MONKER_OK || !tree)
    {
        status(app, "TREE ERROR\nTopology could not be decoded: %s",
               pe_monker_status_string(tree_status));
        return -1;
    }
    if (pe_monker_tree_read_ranges(path, &ranges) == PE_MONKER_OK &&
        ranges.combo_count > 0u &&
        pe_monker_combo_layout_from_count(ranges.combo_count, layout) == PE_MONKER_OK)
        ranges_present = 1;
    else
    {
        layout->game = selected_game(app);
        layout->hole_cards = hole_cards_from_game(layout->game);
        layout->combo_count = ranges.combo_count;
    }
    combo_selected(app->game_combo, game_index(layout->game));
    if (header->player_count >= 2u && header->player_count <= 8u)
        combo_selected(app->players_combo, header->player_count - 2u);
    if (header->street == 0)
    {
        edit_text(app->board_edit, "");
        edit_text(app->range0_edit, "100%");
        edit_text(app->range1_edit, "100%");
    }
    else
    {
        if (!edit_get_text(app->range0_edit) || !*edit_get_text(app->range0_edit))
            edit_text(app->range0_edit, "100%");
        if (!edit_get_text(app->range1_edit) || !*edit_get_text(app->range1_edit))
            edit_text(app->range1_edit, "100%");
    }
    label_text(app->board_label, header->street == 0
               ? "BOARD / RUNOUT: automatic through river"
               : "BOARD / RUNOUT: enter the cards for this street");
    update_result_view(app, "", 0);
    status(app,
           "TREE READY\nGame: %s%s\nPlayers: %u\nStreet: %s\nNodes: %d\nRanges: %s\n\n%s",
           game_name(layout->game),
           ranges_present ? " (from tree)" : " (selected)",
           header->player_count, street_name(header->street), tree->node_count,
           ranges_present ? "embedded" : "not embedded; enter external ranges",
           header->street == 0
               ? "Preflop Lane B: ranges are sampled with card removal; public boards are dealt through river."
               : "Enter the board cards for this tree street before Solve.");
    mpf_tree_free(tree);
    pe_monker_range_set_free(&ranges);
    return 0;
}

static void i_on_tree(App *app, Event *event)
{
    const char_t *types[] = {"tree"};
    const char_t *path = comwin_open_file(app->window, "Open Monker tree",
                                          types, 1, NULL, NULL);
    if (path != NULL)
    {
        edit_text(app->tree_edit, path);
        infer_game_from_path(app, path);
    }
    unref(event);
}

static void i_on_mkr(App *app, Event *event)
{
    const char_t *types[] = {"mkr"};
    const char_t *path = comwin_open_file(app->window, "Open strategy archive",
                                          types, 1, NULL, NULL);
    if (path != NULL) edit_text(app->mkr_edit, path);
    unref(event);
}

static void i_on_load(App *app, Event *event)
{
    pe_monker_tree_header_t header;
    pe_monker_combo_layout_t layout;
    const char *path = edit_get_text(app->tree_edit);
    if (!path || !*path)
        status(app, "Choose a .tree file first.");
    else
    {
        infer_game_from_path(app, path);
        (void)read_tree(app, path, &header, &layout);
    }
    unref(event);
}

static int quote_argument(const char *input, char *output, size_t capacity)
{
    size_t used = 0u;
    if (!input || !output || capacity < 3u) return -1;
    output[used++] = '"';
    while (*input)
    {
        if (used + 2u >= capacity) return -1;
        if (*input == '"' || *input == '\\') output[used++] = '\\';
        output[used++] = *input++;
    }
    output[used++] = '"';
    output[used] = '\0';
    return 0;
}

static int usable_optional_path(const char *path)
{
    return path && *path && strncmp(path, "/path/to/", 9u) != 0;
}

static int executable_directory(char *out, size_t capacity)
{
    size_t length = 0u;
    char *separator;

    if (!out || capacity < 2u)
        return 0;
#if defined(_WIN32)
    {
        DWORD result = GetModuleFileNameA(NULL, out, (DWORD)capacity);
        if (result == 0u || result >= (DWORD)capacity)
            return 0;
        length = (size_t)result;
    }
#elif defined(__APPLE__)
    {
        uint32_t size = (uint32_t)capacity;
        if (_NSGetExecutablePath(out, &size) != 0)
            return 0;
        out[capacity - 1u] = '\0';
        length = strlen(out);
    }
#elif defined(__linux__)
    {
        ssize_t result = readlink("/proc/self/exe", out, capacity - 1u);
        if (result <= 0)
            return 0;
        out[result] = '\0';
        length = (size_t)result;
    }
#else
    return 0;
#endif
    separator = strrchr(out, PE_PATH_SEPARATOR);
    if (!separator || separator == out)
        return 0;
    *separator = '\0';
    return length > 0u;
}

static const char *resolve_runner(const char *configured, const char *name)
{
    static char local_path[2048];
    char executable_dir[2048];
    if (usable_optional_path(configured) && strcmp(configured, "pe-vector-sim") != 0 &&
        (strchr(configured, '/') != NULL || strchr(configured, '\\') != NULL))
        return PE_ACCESS(configured, PE_X_OK) == 0 ? configured : NULL;
    if (PE_ACCESS(name, PE_X_OK) == 0)
        return name;
    if (executable_directory(executable_dir, sizeof(executable_dir)))
    {
        /* The Studio build lives in build-studio/tools while the solver
           tools live in build/tools.  This also works when launched by
           Finder/the desktop, where the process cwd is not the repository. */
        (void)snprintf(local_path, sizeof(local_path),
                       "%s/../../build/tools/%s", executable_dir, name);
        if (PE_ACCESS(local_path, PE_X_OK) == 0)
            return local_path;
        (void)snprintf(local_path, sizeof(local_path), "%s/%s",
                       executable_dir, name);
        if (PE_ACCESS(local_path, PE_X_OK) == 0)
            return local_path;
    }
    snprintf(local_path, sizeof(local_path), "build/tools/%s", name);
    return PE_ACCESS(local_path, PE_X_OK) == 0 ? local_path : NULL;
}

static void i_solve_copy_output(App *app, char *out, size_t capacity)
{
    size_t total, length;
    if (!app || !out || capacity == 0u)
        return;
    bmutex_lock(app->solve_mutex);
    total = strlen(app->solve_output);
    length = total < capacity - 1u ? total : capacity - 1u;
    memcpy(out, app->solve_output + total - length, length);
    out[length] = '\0';
    bmutex_unlock(app->solve_mutex);
}

static void i_solve_append_output(App *app, const char *data, size_t length)
{
    size_t current;
    if (!app || !data || length == 0u)
        return;
    bmutex_lock(app->solve_mutex);
    current = strlen(app->solve_output);
    if (length >= sizeof(app->solve_output))
    {
        data += length - (sizeof(app->solve_output) - 1u);
        length = sizeof(app->solve_output) - 1u;
        current = 0u;
    }
    if (current + length >= sizeof(app->solve_output))
    {
        size_t drop = current + length - (sizeof(app->solve_output) - 1u);
        memmove(app->solve_output, app->solve_output + drop, current - drop);
        current -= drop;
    }
    memcpy(app->solve_output + current, data, length);
    app->solve_output[current + length] = '\0';
    bmutex_unlock(app->solve_mutex);
}

static void i_solve_request_stop(App *app)
{
    Proc *proc;
    if (!app)
        return;
    bmutex_lock(app->solve_mutex);
    app->solve_cancel_requested = 1;
    proc = app->solve_proc;
    if (proc)
        (void)bproc_cancel(proc);
    bmutex_unlock(app->solve_mutex);
    status(app, "STOPPING\nThe solver is being stopped at the current safe point...");
}

static uint32_t i_solve_main(App *app)
{
    Proc *proc;
    perror_t error = ekPOK;
    byte_t buffer[2048];
    uint32_t read_size = 0u;
    uint32_t exit_code = 1u;

    proc = bproc_exec(app->solve_command, &error);
    bmutex_lock(app->solve_mutex);
    app->solve_proc = proc;
    if (proc && app->solve_cancel_requested)
        (void)bproc_cancel(proc);
    bmutex_unlock(app->solve_mutex);
    if (!proc)
    {
        i_solve_append_output(app, "Could not launch solver process.\n", 34u);
        return exit_code;
    }
    while (bproc_read(proc, buffer, sizeof(buffer), &read_size, &error))
    {
        i_solve_append_output(app, (const char *)buffer, read_size);
        read_size = 0u;
    }
    exit_code = bproc_wait(proc);
    bmutex_lock(app->solve_mutex);
    app->solve_proc = NULL;
    app->solve_exit_code = exit_code;
    bmutex_unlock(app->solve_mutex);
    bproc_close(&proc);
    return exit_code;
}

static void i_solve_update(App *app)
{
    char output[7800];
    int running;
    if (!app)
        return;
    bmutex_lock(app->solve_mutex);
    running = app->solve_running;
    bmutex_unlock(app->solve_mutex);
    i_solve_copy_output(app, output, sizeof(output));
    update_result_view(app, output, running);
    status(app, "%s\n%s\n\nClick Stop solve to interrupt the run.",
           running ? "SOLVING" : "SOLVE",
           output[0] ? output : "Waiting for progress...");
}

static void i_solve_end(App *app, const uint32_t exit_code)
{
    char output[7800];
    int cancelled;
    if (!app)
        return;
    bmutex_lock(app->solve_mutex);
    cancelled = app->solve_cancel_requested;
    app->solve_running = 0;
    app->solve_cancel_requested = 0;
    bmutex_unlock(app->solve_mutex);
    button_text(app->solve_button, "Solve this spot");
    i_solve_copy_output(app, output, sizeof(output));
    update_result_view(app, output, 0);
    status(app, "%s\nexit_code=%u\n%s",
           cancelled ? "SOLVE STOPPED" : exit_code == 0u ? "SOLVE RESULT" : "SOLVE ERROR",
           exit_code, output[0] ? output : "No output from solver.");
}

static int i_start_solve(App *app, const char *command)
{
    if (!app || !command || !*command)
        return -1;
    bmutex_lock(app->solve_mutex);
    if (app->solve_running)
    {
        bmutex_unlock(app->solve_mutex);
        return -1;
    }
    snprintf(app->solve_command, sizeof(app->solve_command), "%s", command);
    app->solve_output[0] = '\0';
    app->solve_cancel_requested = 0;
    app->solve_running = 1;
    bmutex_unlock(app->solve_mutex);
    button_text(app->solve_button, "Solve running...");
    osapp_task(app, .10f, i_solve_main, i_solve_update, i_solve_end, App);
    return 0;
}

static void i_on_stop(App *app, Event *event)
{
    int running;
    bmutex_lock(app->solve_mutex);
    running = app->solve_running;
    bmutex_unlock(app->solve_mutex);
    if (running)
        i_solve_request_stop(app);
    else
        status(app, "READY\nNo solver run is currently active.");
    unref(event);
}

static void i_on_solve(App *app, Event *event)
{
    pe_monker_tree_header_t header;
    pe_monker_combo_layout_t layout;
    char tree[2048], board[256], runner[2048], mkr[2048];
    char range0[4200], range1[4200];
    char command[8192];
    size_t used = 0u;
    int board_cards;
    const char *tree_path = edit_get_text(app->tree_edit);
    const char *board_text;
    const char *runner_path;
    const char *mkr_path;
    const char *range0_text;
    const char *range1_text;
    const char *iterations_text;
    const char *target_text;
    const char *interval_text;
    uint64_t iterations;
    uint64_t interval;
    double target_mbb;
    char config_text[256];

    bmutex_lock(app->solve_mutex);
    if (app->solve_running)
    {
        bmutex_unlock(app->solve_mutex);
        i_solve_request_stop(app);
        unref(event);
        return;
    }
    bmutex_unlock(app->solve_mutex);

    if (!tree_path || !*tree_path || read_tree(app, tree_path, &header, &layout) != 0)
    {
        unref(event);
        return;
    }
    board_text = edit_get_text(app->board_edit);
    runner_path = edit_get_text(app->runner_edit);
    mkr_path = edit_get_text(app->mkr_edit);
    range0_text = edit_get_text(app->range0_edit);
    range1_text = edit_get_text(app->range1_edit);
    iterations_text = edit_get_text(app->iterations_edit);
    target_text = edit_get_text(app->target_edit);
    interval_text = edit_get_text(app->interval_edit);
    if (parse_ui_u64(iterations_text, &iterations) != 0 ||
        parse_ui_u64(interval_text, &interval) != 0 ||
        parse_ui_target(target_text, &target_mbb) != 0)
    {
        status(app, "SOLVE BLOCKED\nSet valid numeric values for max iterations,\nstop target mBB and convergence interval.");
        unref(event);
        return;
    }
    if (header.street == 0)
    {
        const char *configured_runner = usable_optional_path(runner_path)
            ? runner_path : "pe-preflop-solve";
        if (strcmp(configured_runner, "pe-vector-sim") == 0)
            configured_runner = "pe-preflop-solve";
        configured_runner = resolve_runner(configured_runner, "pe-preflop-solve");
        if (!configured_runner)
        {
            status(app, "SOLVE ERROR\nCould not find pe-preflop-solve next to Studio or in build/tools.");
            unref(event);
            return;
        }
        if (header.player_count < 2u || header.player_count > 6u ||
            usable_optional_path(mkr_path))
        {
            status(app, "SOLVE BLOCKED\nPreflop Lane B supports 2..6 players and solves the tree from ranges; .mkr import is not used by this path.");
            unref(event);
            return;
        }
        if (quote_argument(tree_path, tree, sizeof(tree)) != 0 ||
            quote_argument(configured_runner, runner, sizeof(runner)) != 0)
        {
            status(app, "SOLVE ERROR\nPath is too long.");
            unref(event);
            return;
        }
        used = (size_t)snprintf(command, sizeof(command),
                                "%s --game %s --players %u --tree %s"
                                " --iterations %" PRIu64 " --samples 128"
                                " --br-samples 32 --target-mbb %.17g"
                                " --exploitability-interval %" PRIu64,
                                runner, game_name(layout.game),
                                header.player_count, tree, iterations,
                                target_mbb, interval);
        for (uint32_t player = 0u; player < header.player_count; ++player)
        {
            const char *range_text = player == 0u ? range0_text :
                                     player == 1u ? range1_text : NULL;
            char range[4200];
            if (!range_text || !*range_text)
                range_text = "100%";
            if (quote_argument(range_text, range, sizeof(range)) != 0 ||
                used >= sizeof(command))
            {
                status(app, "SOLVE ERROR\nRange text is too long.");
                unref(event);
                return;
            }
            used += (size_t)snprintf(command + used, sizeof(command) - used,
                                     " --range%u %s", player, range);
        }
        (void)snprintf(command + strlen(command), sizeof(command) - strlen(command),
                       " 2>&1");
        snprintf(config_text, sizeof(config_text),
                 "Lane B preflop | stop target %.2f mBB | max iterations %" PRIu64
                 " | check every %" PRIu64,
                 target_mbb, iterations, interval);
        label_text(app->run_config, config_text);
        status(app, "SOLVING PREFLOP\n%s\n\nEmpty ranges are 100%%; boards are dealt through river.", command);
        if (i_start_solve(app, command) != 0)
            status(app, "SOLVE ERROR\nCould not start the asynchronous solver task.");
        unref(event);
        return;
    }
    board_cards = card_count(board_text);
    if (board_cards != street_cards(header.street))
    {
        status(app, "SOLVE BLOCKED\nExpected %d board cards for %s, received %d.",
               street_cards(header.street), street_name(header.street), board_cards);
        unref(event);
        return;
    }
    if (header.street != 3)
    {
        status(app, "SOLVE BLOCKED\nVector CPU currently consumes river tree spots.\nUse Legacy CFR for %s trees.",
               street_name(header.street));
        unref(event);
        return;
    }
    if (layout.combo_count == 0u && header.player_count != 2u)
    {
        status(app, "SOLVE BLOCKED\nThis native vector screen needs one external range per player; current tree has %u players.",
               header.player_count);
        unref(event);
        return;
    }
    if (quote_argument(tree_path, tree, sizeof(tree)) != 0 ||
        quote_argument(board_text, board, sizeof(board)) != 0 ||
        quote_argument(runner_path && *runner_path ? runner_path : "pe-vector-sim",
                       runner, sizeof(runner)) != 0)
    {
        status(app, "SOLVE ERROR\nPath is too long.");
        unref(event);
        return;
    }
    used = (size_t)snprintf(command, sizeof(command),
                            "%s --game %s --board %s --players %u --tree %s",
                            runner, game_name(layout.game), board,
                            header.player_count, tree);
    if (layout.combo_count == 0u)
    {
        if (!range0_text || !*range0_text || !range1_text || !*range1_text ||
            quote_argument(range0_text, range0, sizeof(range0)) != 0 ||
            quote_argument(range1_text, range1, sizeof(range1)) != 0)
        {
            status(app, "SOLVE BLOCKED\nEnter both external ranges for this rangeless tree.");
            unref(event);
            return;
        }
        used += (size_t)snprintf(command + used, sizeof(command) - used,
                                 " --range0 %s --range1 %s", range0, range1);
    }
    if (usable_optional_path(mkr_path) && quote_argument(mkr_path, mkr, sizeof(mkr)) == 0)
        (void)snprintf(command + used, sizeof(command) - used, " --mkr %s", mkr);
    (void)snprintf(command + strlen(command), sizeof(command) - strlen(command), " 2>&1");
    status(app, "SOLVING\n%s", command);
    if (i_start_solve(app, command) != 0)
        status(app, "SOLVE ERROR\nCould not start the asynchronous solver task.");
    unref(event);
}

static Panel *i_setup_panel(App *app)
{
    Panel *panel = panel_create();
    Layout *layout = layout_create(2, 18);
    Label *title = label_create();
    Label *game_label = label_create();
    Label *players_label = label_create();
    Label *tree_label = label_create();
    Label *mkr_label = label_create();
    Label *board_label = label_create();
    Label *range0_label = label_create();
    Label *range1_label = label_create();
    Label *runner_label = label_create();
    Label *iterations_label = label_create();
    Label *target_label = label_create();
    Label *interval_label = label_create();
    app->game_combo = combo_create();
    app->players_combo = combo_create();
    Button *browse_tree = button_push();
    Button *browse_mkr = button_push();
    Button *load = button_push();
    Button *solve = button_push();
    Button *stop = button_push();
    app->solve_button = solve;
    app->stop_button = stop;
    app->tree_edit = edit_create();
    app->mkr_edit = edit_create();
    app->board_edit = edit_create();
    app->range0_edit = edit_create();
    app->range1_edit = edit_create();
    app->runner_edit = edit_create();
    app->iterations_edit = edit_create();
    app->target_edit = edit_create();
    app->interval_edit = edit_create();
    app->board_label = board_label;
    label_text(title, "SPOT SETUP");
    label_text(game_label, "GAME");
    label_text(players_label, "PLAYERS");
    label_text(tree_label, ".TREE");
    label_text(mkr_label, ".MKR (optional)");
    label_text(board_label, "BOARD (tree street)");
    label_text(range0_label, "RANGE PLAYER 1");
    label_text(range1_label, "RANGE PLAYER 2");
    label_text(runner_label, "VECTOR RUNNER");
    label_text(iterations_label, "MAX ITERATIONS");
    label_text(target_label, "STOP TARGET (mBB, 0 = off)");
    label_text(interval_label, "CONVERGENCE CHECK EVERY");
    combo_add_elem(app->game_combo, "Hold'em", NULL);
    combo_add_elem(app->game_combo, "PLO4", NULL);
    combo_add_elem(app->game_combo, "PLO5", NULL);
    combo_add_elem(app->game_combo, "PLO6", NULL);
    combo_selected(app->game_combo, 1u);
    for (uint32_t players = 2u; players <= 8u; ++players)
    {
        char text[16];
        snprintf(text, sizeof(text), "%u", players);
        combo_add_elem(app->players_combo, text, NULL);
    }
    combo_selected(app->players_combo, 0u);
    button_text(browse_tree, "Browse...");
    button_text(browse_mkr, "Browse...");
    button_text(load, "Load and inspect tree");
    button_text(solve, "Solve this spot");
    button_text(stop, "Stop run");
    edit_phtext(app->tree_edit, "/path/to/spot.tree");
    edit_phtext(app->mkr_edit, "/path/to/strategy.mkr");
    edit_phtext(app->board_edit, "No board (preflop: automatic)");
    edit_phtext(app->range0_edit, "100%");
    edit_phtext(app->range1_edit, "100%");
    edit_text(app->runner_edit, "pe-vector-sim");
    edit_text(app->iterations_edit, "10000");
    edit_text(app->target_edit, "1.0");
    edit_text(app->interval_edit, "256");
    button_OnClick(browse_tree, listener(app, i_on_tree, App));
    button_OnClick(browse_mkr, listener(app, i_on_mkr, App));
    button_OnClick(load, listener(app, i_on_load, App));
    button_OnClick(solve, listener(app, i_on_solve, App));
    button_OnClick(stop, listener(app, i_on_stop, App));
    layout_label(layout, title, 0, 0);
    layout_label(layout, game_label, 0, 1);
    layout_combo(layout, app->game_combo, 0, 2);
    layout_label(layout, players_label, 1, 1);
    layout_combo(layout, app->players_combo, 1, 2);
    layout_label(layout, tree_label, 0, 3);
    layout_edit(layout, app->tree_edit, 0, 4);
    layout_button(layout, browse_tree, 1, 4);
    layout_label(layout, mkr_label, 0, 5);
    layout_edit(layout, app->mkr_edit, 0, 6);
    layout_button(layout, browse_mkr, 1, 6);
    layout_label(layout, board_label, 0, 7);
    layout_edit(layout, app->board_edit, 0, 8);
    layout_label(layout, range0_label, 0, 9);
    layout_edit(layout, app->range0_edit, 0, 10);
    layout_label(layout, range1_label, 1, 9);
    layout_edit(layout, app->range1_edit, 1, 10);
    layout_label(layout, runner_label, 0, 11);
    layout_edit(layout, app->runner_edit, 0, 12);
    layout_button(layout, load, 0, 13);
    layout_button(layout, solve, 1, 13);
    layout_label(layout, iterations_label, 0, 14);
    layout_label(layout, target_label, 1, 14);
    layout_edit(layout, app->iterations_edit, 0, 15);
    layout_edit(layout, app->target_edit, 1, 15);
    layout_label(layout, interval_label, 0, 16);
    layout_button(layout, stop, 1, 16);
    layout_edit(layout, app->interval_edit, 0, 17);
    layout_hsize(layout, 0, 460);
    layout_hsize(layout, 1, 150);
    layout_margin(layout, 12);
    layout_hmargin(layout, 0, 8);
    layout_vmargin(layout, 0, 8);
    layout_vmargin(layout, 2, 8);
    layout_vmargin(layout, 4, 8);
    layout_vmargin(layout, 6, 8);
    layout_vmargin(layout, 8, 8);
    layout_vmargin(layout, 10, 8);
    layout_vmargin(layout, 12, 8);
    layout_vmargin(layout, 14, 8);
    layout_vmargin(layout, 16, 8);
    panel_layout(panel, layout);
    return panel;
}

static Panel *i_result_panel(App *app)
{
    Panel *panel = panel_create();
    Layout *layout = layout_create(1, 7);
    Label *title = label_create();
    app->status = textview_create();
    app->run_state = label_create();
    app->run_progress = label_create();
    app->run_metrics = label_create();
    app->run_config = label_create();
    app->run_progress_bar = progress_create();
    label_text(title, "RESULTS / CONVERGENCE / RUN LOG");
    label_text(app->run_state, "READY");
    label_text(app->run_progress, "READY  |  no convergence sample yet");
    label_text(app->run_metrics, "Empirical exploitability: not measured");
    label_text(app->run_config, "Configure the spot, then press Solve this spot.");
    textview_editable(app->status, FALSE);
    textview_wrap(app->status, TRUE);
    textview_printf(app->status, "Choose a .tree file, inspect it, then solve the exact spot.\n");
    layout_label(layout, title, 0, 0);
    layout_label(layout, app->run_state, 0, 1);
    layout_progress(layout, app->run_progress_bar, 0, 2);
    layout_label(layout, app->run_progress, 0, 3);
    layout_label(layout, app->run_metrics, 0, 4);
    layout_label(layout, app->run_config, 0, 5);
    layout_textview(layout, app->status, 0, 6);
    layout_hsize(layout, 0, 580);
    layout_vsize(layout, 6, 520);
    layout_margin(layout, 12);
    layout_vmargin(layout, 0, 8);
    layout_vmargin(layout, 1, 8);
    layout_vmargin(layout, 2, 8);
    layout_vmargin(layout, 3, 8);
    layout_vmargin(layout, 4, 8);
    layout_vmargin(layout, 5, 8);
    panel_layout(panel, layout);
    return panel;
}

static App *i_create(void)
{
    App *app = heap_new0(App);
    Panel *root = panel_create();
    Layout *layout = layout_create(2, 1);
    Panel *setup = i_setup_panel(app);
    Panel *result = i_result_panel(app);
    app->solve_mutex = bmutex_create();
    layout_panel(layout, setup, 0, 0);
    layout_panel(layout, result, 1, 0);
    layout_hsize(layout, 0, 640);
    layout_hsize(layout, 1, 700);
    layout_hmargin(layout, 0, 16);
    layout_margin(layout, 16);
    panel_layout(root, layout);
    app->window = window_create(ekWINDOW_STDRES);
    window_panel(app->window, root);
    window_title(app->window, "poker-eval Studio");
    window_origin(app->window, v2df(180, 100));
    window_OnClose(app->window, listener(app, i_on_close, App));
    window_show(app->window);
    return app;
}

static void i_on_close(App *app, Event *event)
{
    bmutex_lock(app->solve_mutex);
    if (app->solve_running)
    {
        bmutex_unlock(app->solve_mutex);
        i_solve_request_stop(app);
        unref(event);
        return;
    }
    bmutex_unlock(app->solve_mutex);
    osapp_finish();
    unref(app);
    unref(event);
}

static void i_destroy(App **app)
{
    bmutex_close(&(*app)->solve_mutex);
    window_destroy(&(*app)->window);
    heap_delete(app, App);
}

#include <osapp/osmain.h>
osmain(i_create, i_destroy, "", App)
