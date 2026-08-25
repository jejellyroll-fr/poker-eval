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

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

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
    Edit *tree_edit;
    Edit *mkr_edit;
    Edit *board_edit;
    Edit *runner_edit;
    TextView *status;
    Label *context;
};

static void i_on_close(App *app, Event *event);

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

static int read_tree(App *app, const char *path, pe_monker_tree_header_t *header,
                     pe_monker_combo_layout_t *layout)
{
    pe_monker_range_set_t ranges;
    mpf_tree_def_t *tree = NULL;
    pe_monker_status_t tree_status;

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
    if (pe_monker_tree_read_ranges(path, &ranges) != PE_MONKER_OK ||
        pe_monker_combo_layout_from_count(ranges.combo_count, layout) != PE_MONKER_OK)
    {
        mpf_tree_free(tree);
        pe_monker_range_set_free(&ranges);
        status(app, "TREE ERROR\nEmbedded ranges or combo layout is not supported");
        return -1;
    }
    status(app,
           "TREE READY\nGame: %s\nPlayers: %u\nStreet: %s\nNodes: %d\nRange combos: %u\n\nThe board is not stored in .tree. Enter the %d board cards before Solve.",
           game_name(layout->game), header->player_count,
           street_name(header->street), tree->node_count, ranges.combo_count,
           street_cards(header->street));
    mpf_tree_free(tree);
    pe_monker_range_set_free(&ranges);
    return 0;
}

static void i_on_tree(App *app, Event *event)
{
    const char_t *types[] = {"tree"};
    const char_t *path = comwin_open_file(app->window, "Open Monker tree",
                                          types, 1, NULL, NULL);
    if (path != NULL) edit_text(app->tree_edit, path);
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
        (void)read_tree(app, path, &header, &layout);
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

static void i_on_solve(App *app, Event *event)
{
    pe_monker_tree_header_t header;
    pe_monker_combo_layout_t layout;
    char tree[2048], board[256], runner[2048], mkr[2048];
    char command[8192];
    char output[8192];
    FILE *pipe;
    size_t used = 0u;
    int board_cards;
    const char *tree_path = edit_get_text(app->tree_edit);
    const char *board_text = edit_get_text(app->board_edit);
    const char *runner_path = edit_get_text(app->runner_edit);
    const char *mkr_path = edit_get_text(app->mkr_edit);

    if (!tree_path || !*tree_path || read_tree(app, tree_path, &header, &layout) != 0)
    {
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
    if (mkr_path && *mkr_path && quote_argument(mkr_path, mkr, sizeof(mkr)) == 0)
        (void)snprintf(command + used, sizeof(command) - used, " --mkr %s", mkr);
    (void)snprintf(command + strlen(command), sizeof(command) - strlen(command), " 2>&1");
    status(app, "SOLVING\n%s", command);
    pipe = POPEN(command, "r");
    if (!pipe)
    {
        status(app, "SOLVE ERROR\nCould not launch pe-vector-sim. Set the runner path.");
        unref(event);
        return;
    }
    output[0] = '\0';
    while (fgets(output + strlen(output), (int)(sizeof(output) - strlen(output)), pipe) != NULL)
    {
        if (strlen(output) + 1u >= sizeof(output)) break;
    }
    (void)PCLOSE(pipe);
    status(app, "SOLVE RESULT\n%s", output[0] ? output : "No output from solver.");
    unref(event);
}

static Panel *i_setup_panel(App *app)
{
    Panel *panel = panel_create();
    Layout *layout = layout_create(2, 10);
    Label *title = label_create();
    Label *tree_label = label_create();
    Label *mkr_label = label_create();
    Label *board_label = label_create();
    Label *runner_label = label_create();
    Button *browse_tree = button_push();
    Button *browse_mkr = button_push();
    Button *load = button_push();
    Button *solve = button_push();
    app->tree_edit = edit_create();
    app->mkr_edit = edit_create();
    app->board_edit = edit_create();
    app->runner_edit = edit_create();
    label_text(title, "SPOT SETUP");
    label_text(tree_label, ".TREE");
    label_text(mkr_label, ".MKR (optional)");
    label_text(board_label, "BOARD (tree street)");
    label_text(runner_label, "VECTOR RUNNER");
    button_text(browse_tree, "Browse...");
    button_text(browse_mkr, "Browse...");
    button_text(load, "Load and inspect tree");
    button_text(solve, "Solve this spot");
    edit_phtext(app->tree_edit, "/path/to/spot.tree");
    edit_phtext(app->mkr_edit, "/path/to/strategy.mkr");
    edit_phtext(app->board_edit, "Ah Kd 7c 2s Qh");
    edit_text(app->runner_edit, "pe-vector-sim");
    button_OnClick(browse_tree, listener(app, i_on_tree, App));
    button_OnClick(browse_mkr, listener(app, i_on_mkr, App));
    button_OnClick(load, listener(app, i_on_load, App));
    button_OnClick(solve, listener(app, i_on_solve, App));
    layout_label(layout, title, 0, 0);
    layout_label(layout, tree_label, 0, 1);
    layout_edit(layout, app->tree_edit, 0, 2);
    layout_button(layout, browse_tree, 1, 2);
    layout_label(layout, mkr_label, 0, 3);
    layout_edit(layout, app->mkr_edit, 0, 4);
    layout_button(layout, browse_mkr, 1, 4);
    layout_label(layout, board_label, 0, 5);
    layout_edit(layout, app->board_edit, 0, 6);
    layout_label(layout, runner_label, 0, 7);
    layout_edit(layout, app->runner_edit, 0, 8);
    layout_button(layout, load, 0, 9);
    layout_button(layout, solve, 1, 9);
    layout_hsize(layout, 0, 460);
    layout_hsize(layout, 1, 150);
    layout_margin(layout, 12);
    layout_hmargin(layout, 0, 8);
    layout_vmargin(layout, 0, 8);
    layout_vmargin(layout, 1, 8);
    layout_vmargin(layout, 3, 8);
    layout_vmargin(layout, 5, 8);
    layout_vmargin(layout, 7, 8);
    panel_layout(panel, layout);
    return panel;
}

static Panel *i_result_panel(App *app)
{
    Panel *panel = panel_create();
    Layout *layout = layout_create(1, 2);
    Label *title = label_create();
    app->status = textview_create();
    label_text(title, "CONTEXT / TREE VALIDATION / SOLVE OUTPUT");
    textview_editable(app->status, FALSE);
    textview_wrap(app->status, TRUE);
    textview_printf(app->status, "Choose a .tree file, inspect it, then solve the exact spot.\n");
    layout_label(layout, title, 0, 0);
    layout_textview(layout, app->status, 0, 1);
    layout_hsize(layout, 0, 580);
    layout_vsize(layout, 1, 520);
    layout_margin(layout, 12);
    layout_vmargin(layout, 0, 8);
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
    osapp_finish();
    unref(app);
    unref(event);
}

static void i_destroy(App **app)
{
    window_destroy(&(*app)->window);
    heap_delete(app, App);
}

#include <osapp/osmain.h>
osmain(i_create, i_destroy, "", App)
