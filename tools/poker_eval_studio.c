/*
 * poker-eval Studio - native desktop shell.
 *
 * This is intentionally a real setup/solve workflow, not a screenshot layer:
 * the .tree header is decoded before it is accepted, the board is validated
 * against the tree street, and Solve executes the same pe-vector-sim / pe-preflop-solve
 * seam as the command line. NAppGUI supplies native controls on macOS, Linux and
 * Windows while the solver remains pure C.
 */
#include <nappgui.h>

#include <poker_eval/solver/pe_monker.h>
#include <poker_eval/solver/pe_monker_classes.h>
#include <poker_eval/solver/pe_runtime.h>
#include <poker_eval/solver/pe_solver_plan.h>
#include <poker_eval/solver/pe_solver_config.h>
#include <poker_eval/engine/solvers/cfr/mpf_tree.h>
#include <osbs/bmutex.h>
#include <osbs/bproc.h>

#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

#define STRATEGY_TABLE_MAX_ROWS 600u
#define STRATEGY_CAPTURE_CAPACITY 524288u
#define MONKER_GRID_MAX_ROWS PE_MONKER_CLASS_COUNT
#define STRATEGY_TABLE_ACTIONS 4u
#define MAX_DECISION_STEPS 64u
#define MAX_PLAYERS_DISPLAY 6u

typedef struct _app_t App;

typedef struct _strategy_table_row_t
{
    char hand[128];
    char node[32];
    char player[32];
    char actions[STRATEGY_TABLE_ACTIONS][160];
    char ev[STRATEGY_TABLE_ACTIONS][160];
    uint32_t action_count;
} StrategyTableRow;

typedef struct _monker_hand_entry_t
{
    char hand[32];
    int cards[6];
    uint32_t card_count;
    int node;
    int player;
    double freqs[STRATEGY_TABLE_ACTIONS];
    double evs[STRATEGY_TABLE_ACTIONS];
    char freq_strs[STRATEGY_TABLE_ACTIONS][32];
    char ev_strs[STRATEGY_TABLE_ACTIONS][32];
    int combos;
    int primary_action;
    double max_freq;
} MonkerHandEntry;

typedef struct _decision_step_t
{
    int node_index;
    char id[32];
    int acting_player;
    uint32_t action_count;
    char action_names[STRATEGY_TABLE_ACTIONS][80];
    int action_next_nodes[STRATEGY_TABLE_ACTIONS];
    double action_freq_totals[STRATEGY_TABLE_ACTIONS];
    char history[512];
} DecisionStep;

struct _app_t
{
    Window *window;
    Combo *game_combo;
    Combo *players_combo;
    Edit *tree_edit;
    Edit *mkr_edit;
    Edit *board_edit;
    Edit *board_edit_quick;
    Edit *range0_edit;
    Edit *range1_edit;
    Edit *runner_edit;
    Edit *iterations_edit;
    Edit *target_edit;
    Edit *interval_edit;
    Edit *threads_edit;
    Combo *algorithm_combo;
    Combo *policy_combo;
    Edit *lambda_edit;
    Edit *dcfr_alpha_edit;
    Edit *dcfr_beta_edit;
    Edit *dcfr_gamma_edit;
    Combo *backend_combo;
    Combo *precision_combo;
    Combo *stop_mode_combo;
    Label *runtime_label;

    /* Results Views & Widgets */
    TextView *status;
    TextView *strategy_view;
    TableView *strategy_table;
    View *poker_table_view;
    View *board_matrix_view;
    View *strategy_grid_view;
    View *hand_preview_view;
    TextView *action_history_view;

    Panel *strategy_container;
    Layout *strategy_container_layout;

    Combo *street_filter;
    Combo *step_filter;
    Combo *board_filter;
    Combo *sort_combo;
    Combo *view_mode_combo;
    Combo *filter_combo;

    Label *strategy_scope;
    Label *cards_caption;
    Label *card_labels[4];
    Label *board_label;

    /* Left Stats Box Labels */
    Label *lbl_status_val;
    Label *lbl_iterations_val;
    Label *lbl_nodes_val;
    Label *lbl_iternodes_val;
    Label *lbl_exploit_val;
    Label *lbl_elapsed_val;
    Label *lbl_rate_val;
    Label *lbl_player_evs[MAX_PLAYERS_DISPLAY];

    /* Responses */
    Button *btn_responses[STRATEGY_TABLE_ACTIONS];
    Button *btn_equity_graph;
    Button *btn_board_overview;

    /* Top Monitor Banner */
    Label *run_state;
    Label *run_progress;
    Label *run_fraction;
    Label *run_metrics;
    Label *run_config;
    Progress *run_progress_bar;

    /* Setup Run Monitor */
    Label *setup_run_state;
    Label *setup_run_progress;
    Label *setup_run_metrics;
    Progress *setup_progress_bar;

    Button *solve_button;
    Button *stop_button;
    Tabs *tabs;
    Panel *pages;

    /* Background Solve Task */
    Mutex *solve_mutex;
    Proc *solve_proc;
    int solve_running;
    int solve_cancel_requested;
    uint32_t solve_exit_code;
    size_t solve_output_total;
    char solve_line_buffer[512];
    size_t solve_line_length;
    /* The solver report arrives after potentially long telemetry. Keep its
     * prefix independently so the result view does not lose the report
     * header and HAND TABLE when the rolling log reaches its cap. */
    int strategy_capture_started;
    size_t strategy_output_length;
    char strategy_output[STRATEGY_CAPTURE_CAPACITY];
    /* Keep only the bytes around a split STRATEGY REPORT marker while the
     * solver is still emitting telemetry.  Capturing the whole stdout stream
     * would fill the report buffer before a long preflop run reaches its
     * result phase. */
    size_t strategy_marker_probe_length;
    char strategy_marker_probe[sizeof("STRATEGY REPORT")];

    int telemetry_valid;
    uint64_t telemetry_iteration;
    uint64_t telemetry_total;
    double telemetry_fraction;
    double telemetry_exploitability;
    double telemetry_target;
    char telemetry_line[512];

    int final_metrics_valid;
    char final_guarantee[32];
    double final_raw;
    double final_mbb;
    uint64_t final_samples;

    size_t strategy_source_length;
    uint64_t strategy_source_hash;
    time_t solve_started_at;
    uint64_t solve_update_count;
    uint32_t tree_node_count;
    /* The results table must follow the topology, not the last Setup
     * selection.  This is especially important when an external .mkr is
     * loaded directly: its tree may be 4-max while Setup still says 2. */
    uint32_t tree_player_count;

    /* Strategy Rows & Monker Hands */
    uint32_t strategy_row_count;
    StrategyTableRow strategy_rows[STRATEGY_TABLE_MAX_ROWS];

    uint32_t monker_hand_count;
    MonkerHandEntry monker_hands[MONKER_GRID_MAX_ROWS];
    int selected_hand_index;
    char selected_hand_str[32];

    uint32_t decision_step_count;
    DecisionStep decision_steps[MAX_DECISION_STEPS];
    int active_step_index;

    /* Player EVs parsed from solve */
    int player_evs_valid;
    double player_ev_mchip[MAX_PLAYERS_DISPLAY];
    double player_ev_bb100[MAX_PLAYERS_DISPLAY];

    /* View, Sort & Filter */
    int sort_mode;        /* 0: Prob, 1: EV, 2: Hand, 3: Combos */
    int view_mode;        /* 0: Monker Grid, 1: Standard Table, 2: Raw Log */
    double filter_min_prob; /* 0.0, 0.01, 0.05, etc. */
    real32_t scroll_offset_y;
    real32_t max_scroll_y;

    /* Fonts */
    Font *card_font;
    Font *card_font_lg;
    Font *header_font;
    Font *regular_font;
    Font *bold_font;

    /* Monker MKR Model */
    mpf_tree_def_t *mkr_tree;
    pe_monker_mkr_strategy_t mkr_strategy;
    int32_t *mkr_node_of_slot;
    pe_monker_classes_t *mkr_classes;
    uint32_t mkr_class_count;
    int32_t mkr_selected_slot;
    int32_t mkr_selected_node;
    int mkr_loaded;

    char table_cell_text[512];
    char solve_command[8192];
    char solve_output[131072];
};

static void i_on_close(App *app, Event *event);
static void render_strategy_view(App *app, const char *output);
static void render_current_strategy_view(App *app);
static void i_on_result_filter(App *app, Event *event);
static void refresh_result_filters(App *app, const char *output);
static void populate_decision_steps_from_tree(App *app, const mpf_tree_def_t *tree);
static void result_write_line(TextView *view, const char *line);
static void i_on_strategy_table(App *app, Event *event);
static void mkr_model_clear(App *app);
static void update_result_action_headers(App *app, const char *output, int step_node);
static void render_card_preview(App *app, const char *hand);
static void update_responses_for_active_step(App *app);
static void update_action_history(App *app);
static void update_action_history(App *app);
static void mkr_populate_grid(App *app);
static void trim_text(char *text);

/* Helpers */
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
    if (errno != 0 || end == text || *end != '\0' || !isfinite(parsed) || parsed < 0.0)
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

static pe_algorithm_preset_t selected_algorithm(const App *app)
{
    return app && app->algorithm_combo
        ? (pe_algorithm_preset_t)combo_get_selected(app->algorithm_combo)
        : PE_PRESET_EXTERNAL_MCCFR;
}

static int preflop_algorithm_supported_ui(pe_algorithm_preset_t algorithm)
{
    switch (algorithm)
    {
    case PE_PRESET_EXTERNAL_MCCFR:
    case PE_PRESET_EXTERNAL_DCFR:
    case PE_PRESET_OUTCOME_MCCFR:
    case PE_PRESET_EXTERNAL_ECFR:
        return 1;
    default:
        return 0;
    }
}

static const char *algorithm_scope_name(pe_algorithm_preset_t algorithm)
{
    switch (algorithm)
    {
    case PE_PRESET_EXTERNAL_MCCFR:
    case PE_PRESET_EXTERNAL_DCFR:
    case PE_PRESET_OUTCOME_MCCFR:
    case PE_PRESET_EXTERNAL_ECFR:
        return "Lane B sampled";
    case PE_PRESET_ECFR:
        return "experimental";
    case PE_PRESET_CUSTOM:
        return "manual axes";
    default:
        return "Lane A full-tree";
    }
}

static pe_policy_mode_t selected_policy(const App *app)
{
    uint32_t selection;
    if (!app || !app->policy_combo)
        return PE_POLICY_COUNT;
    selection = combo_get_selected(app->policy_combo);
    /* Entry zero means that the selected algorithm preset supplies the
       policy. The explicit entries follow the public enum order. */
    return selection == 0u ? PE_POLICY_COUNT
                           : (pe_policy_mode_t)(selection - 1u);
}

static pe_compute_kind_t selected_backend(const App *app)
{
    return app && app->backend_combo
        ? (pe_compute_kind_t)combo_get_selected(app->backend_combo)
        : PE_COMPUTE_AUTO;
}

static pe_precision_mode_t selected_precision(const App *app)
{
    return app && app->precision_combo
        ? (pe_precision_mode_t)combo_get_selected(app->precision_combo)
        : PE_PREC_F64;
}

static void update_runtime_label(App *app)
{
    pe_runtime_capabilities_t runtime;
    char text[512];
    char cuda[160];
    char opencl[160];
    if (!app || !app->runtime_label || pe_runtime_probe(&runtime) != 0)
        return;
    pe_runtime_backend_status(&runtime.backends[PE_COMPUTE_CUDA],
                              cuda, sizeof(cuda));
    pe_runtime_backend_status(&runtime.backends[PE_COMPUTE_OPENCL],
                              opencl, sizeof(opencl));
    snprintf(text, sizeof(text),
             "RUNTIME | CPU %u | OpenMP %s | SIMD %s | %s | %s",
             runtime.logical_cpus, runtime.openmp_available ? "yes" : "no",
             pe_runtime_simd_name(runtime.simd), cuda, opencl);
    label_text(app->runtime_label, text);
}

static uint32_t active_table_players(const App *app)
{
    if (app && app->tree_player_count >= 2u && app->tree_player_count <= 6u)
        return app->tree_player_count;
    return selected_players(app);
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

static const char *mkr_strategy_entry(const pe_monker_mkr_t *archive)
{
    if (!archive)
        return NULL;
    for (size_t index = 0u; index < archive->count; ++index)
        if (archive->entries[index].name &&
            strstr(archive->entries[index].name, "storedstrategy") != NULL)
            return archive->entries[index].name;
    return NULL;
}

static void tree_action_label(const mpf_tree_node_t *node, int index,
                              char *out, size_t capacity)
{
    const mpf_tree_action_t *action;
    if (!out || capacity == 0u)
        return;
    out[0] = '\0';
    if (!node || index < 0 || index >= node->action_count)
        return;
    action = &node->actions[index];
    if (action->type == MPF_TREE_ACTION_FOLD)
        snprintf(out, capacity, "FOLD");
    else if (action->type == MPF_TREE_ACTION_CALL)
        snprintf(out, capacity, "CALL / CHECK");
    else if (action->type == MPF_TREE_ACTION_RAISE &&
             action->size_index >= 0 && action->size_index < node->bet_size_count)
    {
        double size = node->bet_sizes[action->size_index];
        if (fabs(size + 1.0) < 1e-9)
            snprintf(out, capacity, "ALL IN");
        else if (node->use_pot_sizing)
            snprintf(out, capacity, "RAISE %.0f%% POT", size * 100.0);
        else
            snprintf(out, capacity, "RAISE %.2f", size);
    }
    else
        snprintf(out, capacity, "ACTION %d", index + 1);
}

static void format_monker_hand(const int cards[4], char out[16])
{
    static const char ranks[] = "23456789TJQKA";
    static const char suits[] = "shcd";
    size_t used = 0u;
    if (!cards || !out)
        return;
    for (int index = 0; index < 4; ++index)
    {
        int card = cards[index];
        if (card < 0 || card >= 52)
            continue;
        out[used++] = ranks[card % 13];
        out[used++] = suits[card / 13];
    }
    out[used] = '\0';
}

/* =========================================================================
 * 4-Color Card Graphics Drawing Functions
 * ========================================================================= */
static color_t suit_color(char suit)
{
    switch (suit)
    {
    case 's': case 'S': return color_rgb(37, 99, 235); /* ♠ Blue */
    case 'h': case 'H': return color_rgb(220, 38, 38); /* ♥ Red */
    case 'd': case 'D': return color_rgb(234, 88, 12); /* ♦ Orange / Amber */
    case 'c': case 'C': return color_rgb(22, 163, 74); /* ♣ Green */
    default:            return color_rgb(60, 68, 77);
    }
}

static color_t suit_color_dim(char suit)
{
    switch (suit)
    {
    case 's': case 'S': return color_rgb(24, 58, 125);  /* spade: slate blue */
    case 'h': case 'H': return color_rgb(132, 28, 38);  /* heart: crimson */
    case 'd': case 'D': return color_rgb(150, 62, 12);  /* diamond: orange */
    case 'c': case 'C': return color_rgb(20, 105, 58);  /* club: emerald */
    default:            return color_rgb(48, 54, 62);
    }
}

static void draw_single_card(DCtx *ctx, char rank, char suit,
                             real32_t x, real32_t y, real32_t w, real32_t h,
                             Font *font)
{
    char rank_str[3];
    color_t bg = suit_color(suit);

    draw_fill_color(ctx, bg);
    draw_rndrect(ctx, ekFILL, x, y, w, h, 2.5f);

    draw_line_color(ctx, color_rgba(255, 255, 255, 60));
    draw_line_width(ctx, 1.0f);
    draw_rndrect(ctx, ekSTROKE, x, y, w, h, 2.5f);

    rank_str[0] = rank ? rank : '?';
    rank_str[1] = '\0';

    if (font)
        draw_font(ctx, font);
    draw_text_color(ctx, color_rgb(255, 255, 255));
    draw_text_align(ctx, ekCENTER, ekCENTER);
    draw_text(ctx, rank_str, x + w * 0.5f, y + h * 0.5f);
}

static void draw_hand_badges(DCtx *ctx, const char *hand,
                             real32_t x, real32_t y, real32_t card_w, real32_t card_h,
                             real32_t gap, Font *font)
{
    const char *p = hand;
    real32_t cur_x = x;
    if (!hand || !*hand)
        return;
    while (*p)
    {
        while (*p == ' ' || *p == '\t' || *p == ',') ++p;
        if (!p[0] || !p[1]) break;
        draw_single_card(ctx, p[0], p[1], cur_x, y, card_w, card_h, font);
        cur_x += card_w + gap;
        p += 2;
    }
}

static void render_card_preview(App *app, const char *hand)
{
    if (!app)
        return;
    if (hand && *hand)
    {
        snprintf(app->selected_hand_str, sizeof(app->selected_hand_str), "%s", hand);
        if (app->cards_caption)
        {
            char caption[96];
            snprintf(caption, sizeof(caption), "SELECTED HAND: %s", hand);
            label_text(app->cards_caption, caption);
        }
    }
    else
    {
        app->selected_hand_str[0] = '\0';
        if (app->cards_caption)
            label_text(app->cards_caption, "HAND PREVIEW | select a hand in the grid");
    }
    if (app->hand_preview_view)
        view_update(app->hand_preview_view);
}

static void i_on_draw_hand_preview(App *app, Event *event)
{
    const EvDraw *params = event_params(event, EvDraw);
    DCtx *ctx = params->ctx;
    real32_t width = params->width;
    real32_t height = params->height;

    draw_fill_color(ctx, color_rgb(26, 30, 36));
    draw_rndrect(ctx, ekFILL, 0, 0, width, height, 4.0f);

    if (app->selected_hand_str[0])
    {
        draw_hand_badges(ctx, app->selected_hand_str, 8.0f, (height - 24.0f) * 0.5f,
                         22.0f, 24.0f, 3.0f, app->card_font_lg);
    }
    else
    {
        if (app->regular_font)
            draw_font(ctx, app->regular_font);
        draw_text_color(ctx, color_rgb(130, 140, 150));
        draw_text_align(ctx, ekLEFT, ekCENTER);
        draw_text(ctx, "No hand selected", 10.0f, height * 0.5f);
    }
}

/* =========================================================================
 * Board Matrix Custom View
 * ========================================================================= */
static void i_on_draw_board_matrix(App *app, Event *event)
{
    const EvDraw *params = event_params(event, EvDraw);
    DCtx *ctx = params->ctx;
    real32_t width = params->width;
    real32_t height = params->height;
    static const char ranks[] = "AKQJT98765432";
    static const char suits[] = "shdc";
    const char *board_text = app->board_edit ? edit_get_text(app->board_edit) : "";
    real32_t pad_x = 4.0f;
    real32_t pad_y = 3.0f;
    real32_t card_w = (width - pad_x * 2.0f - 12.0f * 2.0f) / 13.0f;
    real32_t card_h = (height - pad_y * 2.0f - 3.0f * 2.0f) / 4.0f;

    draw_fill_color(ctx, color_rgb(22, 26, 31));
    draw_rndrect(ctx, ekFILL, 0, 0, width, height, 4.0f);

    for (int s = 0; s < 4; ++s)
    {
        char suit = suits[s];
        for (int r = 0; r < 13; ++r)
        {
            char rank = ranks[r];
            char card_str[4];
            int is_selected = 0;
            real32_t x = pad_x + (real32_t)r * (card_w + 2.0f);
            real32_t y = pad_y + (real32_t)s * (card_h + 2.0f);

            card_str[0] = rank;
            card_str[1] = suit;
            card_str[2] = '\0';

            if (board_text && strstr(board_text, card_str) != NULL)
                is_selected = 1;

            if (is_selected)
            {
                draw_fill_color(ctx, suit_color(suit));
                draw_rndrect(ctx, ekFILL, x, y, card_w, card_h, 2.0f);
                draw_line_color(ctx, color_rgb(255, 255, 255));
                draw_line_width(ctx, 1.5f);
                draw_rndrect(ctx, ekSTROKE, x, y, card_w, card_h, 2.0f);
            }
            else
            {
                /* Use explicit dim colours instead of alpha-channel
                 * extraction from color_t.  The latter is platform-specific
                 * and made diamonds/clubs render with the wrong hue. */
                draw_fill_color(ctx, suit_color_dim(suit));
                draw_rndrect(ctx, ekFILL, x, y, card_w, card_h, 2.0f);
            }

            if (app->card_font)
                draw_font(ctx, app->card_font);
            draw_text_color(ctx, color_rgb(255, 255, 255));
            draw_text_align(ctx, ekCENTER, ekCENTER);
            card_str[1] = '\0';
            draw_text(ctx, card_str, x + card_w * 0.5f, y + card_h * 0.5f);
        }
    }
}

static void i_on_click_board_matrix(App *app, Event *event)
{
    const EvMouse *p = event_params(event, EvMouse);
    static const char ranks[] = "AKQJT98765432";
    static const char suits[] = "shdc";
    real32_t width = 328.0f;
    real32_t height = 86.0f;
    real32_t pad_x = 4.0f;
    real32_t pad_y = 3.0f;
    real32_t card_w = (width - pad_x * 2.0f - 12.0f * 2.0f) / 13.0f;
    real32_t card_h = (height - pad_y * 2.0f - 3.0f * 2.0f) / 4.0f;
    int r = (int)((p->x - pad_x) / (card_w + 2.0f));
    int s = (int)((p->y - pad_y) / (card_h + 2.0f));

    if (r >= 0 && r < 13 && s >= 0 && s < 4 && app->board_edit)
    {
        char card_str[4];
        char current_board[128] = "";
        char new_board[128] = "";
        const char *txt = edit_get_text(app->board_edit);
        card_str[0] = ranks[r];
        card_str[1] = suits[s];
        card_str[2] = '\0';

        if (txt && *txt && strncmp(txt, "No board", 8) != 0)
            snprintf(current_board, sizeof(current_board), "%s", txt);

        if (strstr(current_board, card_str) != NULL)
        {
            /* Remove card */
            char *found = strstr(current_board, card_str);
            size_t len = strlen(card_str);
            memmove(found, found + len, strlen(found + len) + 1);
            snprintf(new_board, sizeof(new_board), "%s", current_board);
        }
        else
        {
            /* Add card */
            if (strlen(current_board) < 10)
                snprintf(new_board, sizeof(new_board), "%s%s", current_board, card_str);
            else
                snprintf(new_board, sizeof(new_board), "%s", current_board);
        }
        trim_text(new_board);
        edit_text(app->board_edit, new_board);
        if (app->board_edit_quick)
            edit_text(app->board_edit_quick, new_board);
        view_update(app->board_matrix_view);
    }
}

/* =========================================================================
 * Graphical Poker Table Custom View
 * ========================================================================= */
static void i_on_draw_poker_table(App *app, Event *event)
{
    const EvDraw *params = event_params(event, EvDraw);
    DCtx *ctx = params->ctx;
    real32_t width = params->width;
    real32_t height = params->height;
    uint32_t num_players = active_table_players(app);
    int acting_player = -1;
    real32_t cx = width * 0.5f;
    real32_t cy = height * 0.5f;
    real32_t table_rx = width * 0.44f;
    real32_t table_ry = height * 0.38f;

    if (app->active_step_index >= 0 && app->active_step_index < (int)app->decision_step_count)
        acting_player = app->decision_steps[app->active_step_index].acting_player;

    /* Background */
    draw_fill_color(ctx, color_rgb(18, 22, 26));
    draw_rndrect(ctx, ekFILL, 0, 0, width, height, 6.0f);

    /* Wooden table rim */
    draw_fill_color(ctx, color_rgb(50, 34, 24));
    draw_ellipse(ctx, ekFILL, cx, cy, table_rx, table_ry);

    /* Felt (Green oval) */
    draw_fill_color(ctx, color_rgb(22, 108, 56));
    draw_ellipse(ctx, ekFILL, cx, cy, table_rx - 10.0f, table_ry - 8.0f);

    /* Inner gold guideline */
    draw_line_color(ctx, color_rgba(250, 204, 21, 100));
    draw_line_width(ctx, 1.2f);
    draw_ellipse(ctx, ekSTROKE, cx, cy, table_rx - 22.0f, table_ry - 18.0f);

    /* Center Pot & Street Text */
    if (app->bold_font)
        draw_font(ctx, app->bold_font);
    draw_text_color(ctx, color_rgb(240, 240, 240));
    draw_text_align(ctx, ekCENTER, ekCENTER);
    draw_text(ctx, "POT: 3.0 BB", cx, cy - 8.0f);

    if (app->regular_font)
        draw_font(ctx, app->regular_font);
    draw_text_color(ctx, color_rgba(255, 255, 255, 180));
    draw_text(ctx, "PREFLOP", cx, cy + 8.0f);

    /* Public cards remain visible in the table view as well as in the
       board matrix.  Preflop deliberately shows an empty board. */
    if (app->board_edit)
    {
        const char *board = edit_get_text(app->board_edit);
        if (board && *board && strncmp(board, "No board", 8u) != 0)
        {
            int cards = card_count(board);
            real32_t card_w = 18.0f;
            real32_t gap = 3.0f;
            real32_t start_x = cx - ((real32_t)cards * card_w +
                                     (real32_t)(cards - 1) * gap) * 0.5f;
            if (cards > 0 && cards <= 5)
                draw_hand_badges(ctx, board, start_x, cy + 22.0f,
                                 card_w, 22.0f, gap, app->card_font);
        }
    }

    /* Player Seats */
    if (num_players < 2u) num_players = 2u;
    if (num_players > 6u) num_players = 6u;

    for (uint32_t i = 0u; i < num_players; ++i)
    {
        double angle = 2.0 * 3.1415926535 * ((double)i + 0.5) / (double)num_players - 1.57079632679;
        real32_t sx = cx + (real32_t)(cos(angle) * (table_rx - 8.0));
        real32_t sy = cy + (real32_t)(sin(angle) * (table_ry - 6.0));
        char seat_lbl[16];
        int is_acting = (int)i == acting_player;

        /* Seat Circle */
        if (is_acting)
        {
            draw_fill_color(ctx, color_rgb(40, 60, 85));
            draw_circle(ctx, ekFILL, sx, sy, 16.0f);
            draw_line_color(ctx, color_rgb(250, 204, 21)); /* Glowing Gold */
            draw_line_width(ctx, 3.0f);
            draw_circle(ctx, ekSTROKE, sx, sy, 16.0f);
        }
        else
        {
            draw_fill_color(ctx, color_rgb(32, 38, 46));
            draw_circle(ctx, ekFILL, sx, sy, 14.0f);
            draw_line_color(ctx, color_rgb(70, 80, 92));
            draw_line_width(ctx, 1.5f);
            draw_circle(ctx, ekSTROKE, sx, sy, 14.0f);
        }

        /* Seat Name */
        snprintf(seat_lbl, sizeof(seat_lbl), "P%u", i + 1u);
        if (app->bold_font)
            draw_font(ctx, app->bold_font);
        draw_text_color(ctx, is_acting ? color_rgb(255, 255, 255) : color_rgb(180, 190, 200));
        draw_text_align(ctx, ekCENTER, ekCENTER);
        draw_text(ctx, seat_lbl, sx, sy);

        /* Dealer button 'D' for Player 1 */
        if (i == 0u)
        {
            real32_t db_x = sx + 14.0f;
            real32_t db_y = sy + 10.0f;
            draw_fill_color(ctx, color_rgb(240, 240, 240));
            draw_circle(ctx, ekFILL, db_x, db_y, 6.0f);
            draw_line_color(ctx, color_rgb(20, 20, 20));
            draw_line_width(ctx, 1.0f);
            draw_circle(ctx, ekSTROKE, db_x, db_y, 6.0f);

            if (app->card_font)
                draw_font(ctx, app->card_font);
            draw_text_color(ctx, color_rgb(20, 20, 20));
            draw_text_align(ctx, ekCENTER, ekCENTER);
            draw_text(ctx, "D", db_x, db_y);
        }
    }
}

static void i_on_click_poker_table(App *app, Event *event)
{
    const EvMouse *p = event_params(event, EvMouse);
    real32_t width = 328.0f;
    real32_t height = 140.0f;
    real32_t cx = width * 0.5f;
    real32_t cy = height * 0.5f;
    real32_t table_rx = width * 0.44f;
    real32_t table_ry = height * 0.38f;
    uint32_t num_players = active_table_players(app);

    if (num_players < 2u) num_players = 2u;
    if (num_players > 6u) num_players = 6u;

    for (uint32_t i = 0u; i < num_players; ++i)
    {
        double angle = 2.0 * 3.1415926535 * ((double)i + 0.5) / (double)num_players - 1.57079632679;
        real32_t sx = cx + (real32_t)(cos(angle) * (table_rx - 8.0));
        real32_t sy = cy + (real32_t)(sin(angle) * (table_ry - 6.0));
        real32_t dx = p->x - sx;
        real32_t dy = p->y - sy;
        if (dx * dx + dy * dy <= 20.0f * 20.0f)
        {
            /* Clicked on Player i -> find step where Player i acts */
            for (uint32_t s = 0u; s < app->decision_step_count; ++s)
            {
                if (app->decision_steps[s].acting_player == (int)i)
                {
                    app->active_step_index = (int)s;
                    if (app->step_filter)
                        combo_selected(app->step_filter, s + 1u);
                    render_current_strategy_view(app);
                    break;
                }
            }
            break;
        }
    }
}

/* =========================================================================
 * Monker Multi-Column Action Strategy Grid
 * ========================================================================= */
static void i_on_draw_strategy_grid(App *app, Event *event)
{
    const EvDraw *params = event_params(event, EvDraw);
    DCtx *ctx = params->ctx;
    real32_t width = params->width;
    real32_t height = params->height;
    uint32_t action_count = 2u;
    real32_t col_w;
    real32_t header_h = 32.0f;
    real32_t subheader_h = 22.0f;
    real32_t row_h = 24.0f;
    real32_t top_offset = header_h + subheader_h;
    DecisionStep *step = NULL;

    draw_fill_color(ctx, color_rgb(18, 22, 26));
    draw_rect(ctx, ekFILL, 0, 0, width, height);

    if (app->active_step_index >= 0 && app->active_step_index < (int)app->decision_step_count)
    {
        step = &app->decision_steps[app->active_step_index];
        action_count = step->action_count;
    }
    if (action_count < 1u) action_count = 1u;
    if (action_count > STRATEGY_TABLE_ACTIONS) action_count = STRATEGY_TABLE_ACTIONS;

    col_w = (width - 4.0f) / (real32_t)action_count;

    /* Draw Column Headers */
    for (uint32_t a = 0u; a < action_count; ++a)
    {
        real32_t cx = (real32_t)a * col_w;
        char title[128];
        color_t header_bg;
        double total_pct = step ? step->action_freq_totals[a] * 100.0 : 0.0;

        if (a == 0) header_bg = color_rgb(160, 35, 35);      /* FOLD: Red */
        else if (a == 1) header_bg = color_rgb(20, 130, 60);  /* CALL/ALLIN: Green */
        else if (a == 2) header_bg = color_rgb(25, 90, 180);  /* RAISE: Blue */
        else header_bg = color_rgb(180, 100, 20);             /* 4TH: Orange */

        /* Header Box */
        draw_fill_color(ctx, header_bg);
        draw_rect(ctx, ekFILL, cx + 1.0f, 0, col_w - 2.0f, header_h);

        snprintf(title, sizeof(title), "%s (%.1f%%)",
                 step && step->action_names[a][0] ? step->action_names[a] : "ACTION",
                 total_pct);

        if (app->bold_font)
            draw_font(ctx, app->bold_font);
        draw_text_color(ctx, color_rgb(255, 255, 255));
        draw_text_align(ctx, ekCENTER, ekCENTER);
        draw_text(ctx, title, cx + col_w * 0.5f, header_h * 0.5f);

        /* Subheader Box */
        draw_fill_color(ctx, color_rgb(28, 33, 40));
        draw_rect(ctx, ekFILL, cx + 1.0f, header_h, col_w - 2.0f, subheader_h);

        if (app->regular_font)
            draw_font(ctx, app->regular_font);
        draw_text_color(ctx, color_rgb(160, 170, 180));
        draw_text_align(ctx, ekLEFT, ekCENTER);
        draw_text(ctx, "HAND", cx + 8.0f, header_h + subheader_h * 0.5f);
        draw_text_align(ctx, ekRIGHT, ekCENTER);
        draw_text(ctx, "FREQ %", cx + col_w * 0.54f, header_h + subheader_h * 0.5f);
        draw_text(ctx, "EV", cx + col_w * 0.78f, header_h + subheader_h * 0.5f);
        draw_text(ctx, "COMBOS", cx + col_w - 10.0f, header_h + subheader_h * 0.5f);
    }

    /* Draw Hand Rows */
    uint32_t display_row = 0u;
    for (uint32_t r = 0u; r < app->monker_hand_count; ++r)
    {
        MonkerHandEntry *h = &app->monker_hands[r];
        if (app->filter_min_prob > 0.0 && h->max_freq * 100.0 < app->filter_min_prob)
            continue;
        real32_t y = top_offset + (real32_t)display_row * row_h - app->scroll_offset_y;
        int is_selected = ((int)r == app->selected_hand_index);

        if (y + row_h < top_offset || y > height)
            continue;

        for (uint32_t a = 0u; a < action_count; ++a)
        {
            real32_t cx = (real32_t)a * col_w;
            color_t row_bg = is_selected ? color_rgb(35, 65, 100) :
                             (r % 2u == 0u) ? color_rgb(22, 26, 32) : color_rgb(18, 21, 26);
            double freq = h->freqs[a];
            double ev = h->evs[a];
            char freq_txt[32];
            char ev_txt[32];
            char combos_txt[16];

            /* Row background */
            draw_fill_color(ctx, row_bg);
            draw_rect(ctx, ekFILL, cx + 1.0f, y, col_w - 2.0f, row_h - 1.0f);

            if (is_selected)
            {
                draw_line_color(ctx, color_rgb(59, 130, 246));
                draw_line_width(ctx, 1.0f);
                draw_rect(ctx, ekSTROKE, cx + 1.0f, y, col_w - 2.0f, row_h - 1.0f);
            }

            /* Draw 4 Graphical Cards */
            draw_hand_badges(ctx, h->hand, cx + 6.0f, y + 2.0f, 16.0f, 19.0f, 2.0f, app->card_font);

            /* Frequency text */
            snprintf(freq_txt, sizeof(freq_txt), "%.1f%%", freq * 100.0);
            if (app->regular_font)
                draw_font(ctx, app->regular_font);
            draw_text_color(ctx, freq > 0.5 ? color_rgb(255, 255, 255) : color_rgb(190, 195, 200));
            draw_text_align(ctx, ekRIGHT, ekCENTER);
            draw_text(ctx, freq_txt, cx + col_w * 0.54f, y + row_h * 0.5f);

            /* EV text */
            if (fabs(ev) > 1e-4)
                snprintf(ev_txt, sizeof(ev_txt), "%+.2f", ev);
            else
                snprintf(ev_txt, sizeof(ev_txt), "0");
            draw_text_color(ctx, ev > 0.0 ? color_rgb(74, 222, 128) :
                                 ev < -0.01 ? color_rgb(248, 113, 113) : color_rgb(160, 170, 180));
            draw_text(ctx, ev_txt, cx + col_w * 0.78f, y + row_h * 0.5f);

            /* Combos */
            snprintf(combos_txt, sizeof(combos_txt), "%d", h->combos > 0 ? h->combos : 4);
            draw_text_color(ctx, color_rgb(150, 160, 170));
            draw_text(ctx, combos_txt, cx + col_w - 10.0f, y + row_h * 0.5f);
        }
        ++display_row;
    }
}

static void i_on_click_strategy_grid(App *app, Event *event)
{
    const EvMouse *p = event_params(event, EvMouse);
    real32_t top_offset = 54.0f;
    real32_t row_h = 24.0f;
    if (p->y >= top_offset)
    {
        int row = (int)((p->y - top_offset + app->scroll_offset_y) / row_h);
        if (row >= 0 && row < (int)app->monker_hand_count)
        {
            app->selected_hand_index = row;
            render_card_preview(app, app->monker_hands[row].hand);
            view_update(app->strategy_grid_view);
        }
    }
}

static void i_on_wheel_strategy_grid(App *app, Event *event)
{
    const EvWheel *p = event_params(event, EvWheel);
    real32_t row_h = 24.0f;
    real32_t max_scroll = (real32_t)app->monker_hand_count * row_h;
    if (max_scroll > 400.0f)
    {
        app->scroll_offset_y -= p->dy * 24.0f;
        if (app->scroll_offset_y < 0.0f)
            app->scroll_offset_y = 0.0f;
        if (app->scroll_offset_y > max_scroll - 200.0f)
            app->scroll_offset_y = max_scroll - 200.0f;
        view_update(app->strategy_grid_view);
    }
}

/* =========================================================================
 * Hand Parsing & Sorting
 * ========================================================================= */
static int compare_hand_prob(const void *a, const void *b)
{
    const MonkerHandEntry *ha = (const MonkerHandEntry *)a;
    const MonkerHandEntry *hb = (const MonkerHandEntry *)b;
    if (ha->max_freq > hb->max_freq) return -1;
    if (ha->max_freq < hb->max_freq) return 1;
    return strcmp(ha->hand, hb->hand);
}

static int compare_hand_ev(const void *a, const void *b)
{
    const MonkerHandEntry *ha = (const MonkerHandEntry *)a;
    const MonkerHandEntry *hb = (const MonkerHandEntry *)b;
    double max_eva = ha->evs[0];
    double max_evb = hb->evs[0];
    for (uint32_t i = 1; i < STRATEGY_TABLE_ACTIONS; ++i)
    {
        if (ha->evs[i] > max_eva) max_eva = ha->evs[i];
        if (hb->evs[i] > max_evb) max_evb = hb->evs[i];
    }
    if (max_eva > max_evb) return -1;
    if (max_eva < max_evb) return 1;
    return 0;
}

static int compare_hand_name(const void *a, const void *b)
{
    const MonkerHandEntry *ha = (const MonkerHandEntry *)a;
    const MonkerHandEntry *hb = (const MonkerHandEntry *)b;
    return strcmp(ha->hand, hb->hand);
}

static int compare_hand_combos(const void *a, const void *b)
{
    const MonkerHandEntry *ha = (const MonkerHandEntry *)a;
    const MonkerHandEntry *hb = (const MonkerHandEntry *)b;
    return hb->combos - ha->combos;
}

static void sort_monker_hands(App *app)
{
    if (!app || app->monker_hand_count == 0u)
        return;
    if (app->sort_mode == 0)
        qsort(app->monker_hands, app->monker_hand_count, sizeof(MonkerHandEntry), compare_hand_prob);
    else if (app->sort_mode == 1)
        qsort(app->monker_hands, app->monker_hand_count, sizeof(MonkerHandEntry), compare_hand_ev);
    else if (app->sort_mode == 2)
        qsort(app->monker_hands, app->monker_hand_count, sizeof(MonkerHandEntry), compare_hand_name);
    else if (app->sort_mode == 3)
        qsort(app->monker_hands, app->monker_hand_count, sizeof(MonkerHandEntry), compare_hand_combos);
}

static void update_active_action_totals(App *app)
{
    if (!app || app->active_step_index < 0 ||
        app->active_step_index >= (int)app->decision_step_count ||
        app->monker_hand_count == 0u)
        return;
    DecisionStep *step = &app->decision_steps[app->active_step_index];
    for (uint32_t action = 0u; action < step->action_count; ++action)
    {
        double total = 0.0;
        for (uint32_t row = 0u; row < app->monker_hand_count; ++row)
            total += app->monker_hands[row].freqs[action];
        step->action_freq_totals[action] = total / (double)app->monker_hand_count;
    }
}

static void update_responses_for_active_step(App *app)
{
    if (!app)
        return;
    for (uint32_t a = 0u; a < STRATEGY_TABLE_ACTIONS; ++a)
    {
        if (app->active_step_index >= 0 && app->active_step_index < (int)app->decision_step_count &&
            a < app->decision_steps[app->active_step_index].action_count)
        {
            DecisionStep *st = &app->decision_steps[app->active_step_index];
            button_text(app->btn_responses[a], st->action_names[a]);
        }
        else
        {
            button_text(app->btn_responses[a], "—");
        }
    }
    update_action_history(app);
}

static void i_on_response_click(App *app, Event *event)
{
    Button *sender = event_sender(event, Button);
    uint32_t clicked_action = 0u;
    for (uint32_t a = 0u; a < STRATEGY_TABLE_ACTIONS; ++a)
    {
        if (app->btn_responses[a] == sender)
        {
            clicked_action = a;
            break;
        }
    }
    if (app->active_step_index >= 0 && app->active_step_index < (int)app->decision_step_count)
    {
        int next_node = app->decision_steps[app->active_step_index].action_next_nodes[clicked_action];
        if (next_node >= 0)
        {
            for (uint32_t s = 0u; s < app->decision_step_count; ++s)
            {
                if (app->decision_steps[s].node_index == next_node)
                {
                    app->active_step_index = (int)s;
                    if (app->step_filter)
                        combo_selected(app->step_filter, s + 1u);
                    render_current_strategy_view(app);
                    break;
                }
            }
        }
    }
    unref(event);
}

static void i_on_sort_changed(App *app, Event *event)
{
    if (app && app->sort_combo)
    {
        app->sort_mode = (int)combo_get_selected(app->sort_combo);
        sort_monker_hands(app);
        if (app->strategy_grid_view)
            view_update(app->strategy_grid_view);
    }
    unref(event);
}

static void i_on_view_mode_changed(App *app, Event *event)
{
    if (app && app->view_mode_combo)
    {
        app->view_mode = (int)combo_get_selected(app->view_mode_combo);
        if (app->strategy_container)
        {
            panel_visible_layout(app->strategy_container, (uint32_t)app->view_mode);
            panel_update(app->strategy_container);
        }
    }
    unref(event);
}

static void i_on_filter_changed(App *app, Event *event)
{
    if (app && app->filter_combo)
    {
        uint32_t sel = combo_get_selected(app->filter_combo);
        if (sel == 0) app->filter_min_prob = 0.0;
        else if (sel == 1) app->filter_min_prob = 1.0;
        else if (sel == 2) app->filter_min_prob = 5.0;
        else if (sel == 3) app->filter_min_prob = 10.0;
        else if (sel == 4) app->filter_min_prob = 25.0;
        else if (sel == 5) app->filter_min_prob = 50.0;
        render_current_strategy_view(app);
    }
    unref(event);
}

static void update_result_action_headers(App *app, const char *output, int step_node)
{
    const char *cursor;
    char labels[STRATEGY_TABLE_ACTIONS][80];
    uint32_t count = 0u;
    int found = 0;

    if (!app || !app->strategy_table || app->mkr_loaded)
        return;
    memset(labels, 0, sizeof(labels));
    cursor = output;
    while (cursor && (cursor = strstr(cursor, "tree_step ")) != NULL)
    {
        const char *end = strchr(cursor, '\n');
        const char *branches = strstr(cursor, "branches=");
        const char *node = strstr(cursor, "node=");
        int current_node = node ? atoi(node + 5) : -1;
        if (branches && (step_node < 0 || current_node == step_node))
        {
            const char *token = branches + 9u;
            while (*token && *token != '\n' && count < STRATEGY_TABLE_ACTIONS)
            {
                const char *separator = strchr(token, '|');
                const char *next = separator ? separator : end;
                const char *arrow = strstr(token, "->");
                size_t length;
                if (!next)
                    next = token + strlen(token);
                length = arrow && arrow < next
                    ? (size_t)(arrow - token) : (size_t)(next - token);
                while (length > 0u && (token[length - 1u] == ' ' || token[length - 1u] == '\t'))
                    --length;
                if (length > 0u)
                {
                    if (length >= sizeof(labels[0]))
                        length = sizeof(labels[0]) - 1u;
                    memcpy(labels[count], token, length);
                    labels[count][length] = '\0';
                    if (strcmp(labels[count], "CALL/CHECK") == 0)
                        snprintf(labels[count], sizeof(labels[0]), "CALL / CHECK");
                    ++count;
                }
                token = separator ? separator + 1u : next;
                if (!separator)
                    break;
            }
            found = count > 0u;
            if (found)
                break;
        }
        cursor = end ? end + 1u : NULL;
    }
    /* Progress-only output is emitted before the strategy report.  The tree
     * loader has already installed the real action names, so do not erase
     * them while waiting for the first report line. */
    if (!found)
        return;
    for (uint32_t action = 0u; action < STRATEGY_TABLE_ACTIONS; ++action)
    {
        if (action < count)
            tableview_header_title(app->strategy_table, action + 3u, labels[action]);
        else
            tableview_header_title(app->strategy_table, action + 3u, "—");
    }
}

static void mkr_model_clear(App *app)
{
    if (!app)
        return;
    free(app->mkr_node_of_slot);
    app->mkr_node_of_slot = NULL;
    pe_monker_mkr_strategy_free(&app->mkr_strategy);
    if (app->mkr_tree)
        mpf_tree_free(app->mkr_tree);
    app->mkr_tree = NULL;
    if (app->mkr_classes)
        pe_monker_classes_destroy(app->mkr_classes);
    app->mkr_classes = NULL;
    app->mkr_class_count = 0u;
    app->mkr_selected_slot = -1;
    app->mkr_selected_node = -1;
    app->mkr_loaded = 0;
}

static int append_format(char *buffer, size_t capacity, size_t *used,
                         const char *format, ...)
{
    int written;
    va_list args;
    if (!buffer || !used || *used >= capacity)
        return -1;
    va_start(args, format);
    written = vsnprintf(buffer + *used, capacity - *used, format, args);
    va_end(args);
    if (written < 0 || (size_t)written >= capacity - *used)
    {
        buffer[capacity - 1u] = '\0';
        return -1;
    }
    *used += (size_t)written;
    return 0;
}

static int mkr_select_node(App *app, int node_index)
{
    if (!app || !app->mkr_loaded)
        return 0;
    app->mkr_selected_slot = -1;
    app->mkr_selected_node = -1;
    for (uint32_t slot = 0u; slot < app->mkr_strategy.slot_count; ++slot)
    {
        int mapped = app->mkr_node_of_slot[slot];
        if (app->mkr_strategy.slots[slot].kind != PE_MONKER_SLOT_BYTES ||
            mapped < 0)
            continue;
        if (node_index < 0 || mapped == node_index)
        {
            app->mkr_selected_slot = (int32_t)slot;
            app->mkr_selected_node = mapped;
            app->strategy_row_count = app->mkr_class_count;
            mkr_populate_grid(app);
            if (app->mkr_classes)
            {
                int cards[4] = {0};
                char hand[16] = "";
                if (pe_monker_class_representative(app->mkr_classes, 0u, cards) == PE_MONKER_OK)
                {
                    format_monker_hand(cards, hand);
                    render_card_preview(app, hand);
                }
            }
            return 1;
        }
    }
    app->strategy_row_count = 0u;
    return 0;
}

static void mkr_populate_grid(App *app)
{
    const mpf_tree_node_t *node;
    const pe_monker_mkr_slot_t *stored;
    uint32_t count;

    if (!app || !app->mkr_loaded || !app->mkr_tree || !app->mkr_classes ||
        app->mkr_selected_slot < 0 || app->mkr_selected_node < 0 ||
        app->mkr_selected_node >= app->mkr_tree->node_count)
    {
        if (app) app->monker_hand_count = 0u;
        return;
    }
    node = &app->mkr_tree->nodes[app->mkr_selected_node];
    stored = &app->mkr_strategy.slots[app->mkr_selected_slot];
    count = app->mkr_class_count < MONKER_GRID_MAX_ROWS
        ? app->mkr_class_count : MONKER_GRID_MAX_ROWS;
    app->monker_hand_count = count;
    for (uint32_t row = 0u; row < count; ++row)
    {
        MonkerHandEntry *entry = &app->monker_hands[row];
        int cards[4] = {0};
        char hand[16] = "?";
        uint32_t base = row * (uint32_t)node->action_count;
        memset(entry, 0, sizeof(*entry));
        if (pe_monker_class_representative(app->mkr_classes, row, cards) == PE_MONKER_OK)
            format_monker_hand(cards, hand);
        snprintf(entry->hand, sizeof(entry->hand), "%s", hand);
        entry->node = app->mkr_selected_node;
        entry->player = node->acting_player;
        entry->card_count = 4u;
        entry->combos = 4;
        for (uint32_t action = 0u; action < (uint32_t)node->action_count &&
             action < STRATEGY_TABLE_ACTIONS; ++action)
        {
            unsigned char value = base + action < stored->count
                ? stored->bytes[base + action] : 0u;
            entry->freqs[action] = (double)value / 256.0;
            snprintf(entry->freq_strs[action], sizeof(entry->freq_strs[action]),
                     "%.1f%%", entry->freqs[action] * 100.0);
            if (entry->freqs[action] >= entry->max_freq)
            {
                entry->max_freq = entry->freqs[action];
                entry->primary_action = (int)action;
            }
        }
    }
    if (app->selected_hand_index < 0 ||
        app->selected_hand_index >= (int)app->monker_hand_count)
        app->selected_hand_index = 0;
}

static void mkr_update_table_headers(App *app)
{
    const mpf_tree_node_t *node = NULL;
    char label[80];
    if (!app || !app->strategy_table)
        return;
    tableview_header_title(app->strategy_table, 0u, "Hand");
    tableview_header_title(app->strategy_table, 1u,
                           app->mkr_loaded ? "Reach" : "Node");
    tableview_header_title(app->strategy_table, 2u,
                           app->mkr_loaded ? "Actor" : "Player");
    if (app->mkr_loaded && app->mkr_selected_node >= 0 && app->mkr_tree &&
        app->mkr_classes && app->mkr_selected_node >= 0 &&
        app->mkr_selected_node < app->mkr_tree->node_count)
        node = &app->mkr_tree->nodes[app->mkr_selected_node];
    for (uint32_t action = 0u; action < STRATEGY_TABLE_ACTIONS; ++action)
    {
        if (node && action < (uint32_t)node->action_count)
            tree_action_label(node, (int)action, label, sizeof(label));
        else
            snprintf(label, sizeof(label), "Action %u", action + 1u);
        tableview_header_title(app->strategy_table, action + 3u, label);
    }
    tableview_header_title(app->strategy_table, 7u,
                           app->mkr_loaded ? "Highest frequency" : "EV by action");
}

static int mkr_load_model(App *app, const char *tree_path, const char *mkr_path)
{
    pe_monker_mkr_t archive;
    pe_monker_mkr_status_t mkr_status;
    pe_monker_status_t tree_status;
    const char *entry;
    size_t used = 0u;
    uint32_t decision_nodes = 0u;

    if (!app || !tree_path || !mkr_path)
        return -1;
    memset(&archive, 0, sizeof(archive));
    mkr_model_clear(app);
    tree_status = pe_monker_tree_load(tree_path, &app->mkr_tree);
    if (tree_status != PE_MONKER_OK || !app->mkr_tree)
    {
        status(app, "MKR REPORT ERROR\nTree: %s",
               pe_monker_status_string(tree_status));
        mkr_model_clear(app);
        return -1;
    }
    app->tree_node_count = app->mkr_tree->node_count > 0
        ? (uint32_t)app->mkr_tree->node_count : 0u;
    {
        pe_monker_tree_header_t header;
        memset(&header, 0, sizeof(header));
        if (pe_monker_tree_read_header(tree_path, &header) == PE_MONKER_OK &&
            header.player_count >= 2u && header.player_count <= 6u)
            app->tree_player_count = header.player_count;
        else
        {
            /* Older/converted trees may not expose a usable wire header.
             * The acting-player field still gives us an exact lower bound. */
            app->tree_player_count = 2u;
            for (int node = 0; node < app->mkr_tree->node_count; ++node)
                if (app->mkr_tree->nodes[node].acting_player + 1 >
                    (int)app->tree_player_count)
                    app->tree_player_count = (uint32_t)
                        (app->mkr_tree->nodes[node].acting_player + 1);
            if (app->tree_player_count > 6u)
                app->tree_player_count = 6u;
        }
        combo_selected(app->players_combo, app->tree_player_count - 2u);
    }
    mkr_status = pe_monker_mkr_read(mkr_path, &archive);
    if (mkr_status != PE_MONKER_MKR_OK)
    {
        status(app, "MKR REPORT ERROR\nArchive: %s",
               pe_monker_mkr_status_string(mkr_status));
        mkr_model_clear(app);
        return -1;
    }
    entry = mkr_strategy_entry(&archive);
    mkr_status = entry
        ? pe_monker_mkr_read_strategy(&archive, entry, &app->mkr_strategy)
        : PE_MONKER_MKR_ERR_BAD_ARCHIVE;
    pe_monker_mkr_free(&archive);
    if (mkr_status != PE_MONKER_MKR_OK)
    {
        status(app, "MKR REPORT ERROR\nStored strategy: %s",
               pe_monker_mkr_status_string(mkr_status));
        mkr_model_clear(app);
        return -1;
    }
    app->mkr_node_of_slot = (int32_t *)calloc(
        app->mkr_strategy.slot_count, sizeof(*app->mkr_node_of_slot));
    if (!app->mkr_node_of_slot ||
        pe_monker_mkr_bind_strategy(app->mkr_tree, &app->mkr_strategy,
                                    app->mkr_node_of_slot,
                                    app->mkr_strategy.slot_count) != PE_MONKER_MKR_OK ||
        pe_monker_mkr_strategy_class_count(app->mkr_tree, &app->mkr_strategy,
                                            &app->mkr_class_count) != PE_MONKER_MKR_OK)
    {
        status(app, "MKR REPORT ERROR\nStrategy slots do not match this tree.");
        mkr_model_clear(app);
        return -1;
    }
    if (app->mkr_class_count != PE_MONKER_CLASS_COUNT ||
        pe_monker_classes_create(&app->mkr_classes) != PE_MONKER_OK)
    {
        status(app,
               "MKR REPORT ERROR\nThis native hand table currently requires the validated PLO4 class codec (%u classes).",
               PE_MONKER_CLASS_COUNT);
        mkr_model_clear(app);
        return -1;
    }
    app->mkr_loaded = 1;
    populate_decision_steps_from_tree(app, app->mkr_tree);
    append_format(app->solve_output, sizeof(app->solve_output), &used,
                  "STRATEGY REPORT variant=plo4 rows=%u/%u ev=not-stored source=mkr\n"
                  "DECISION STEPS (tree branches)\n",
                  app->mkr_class_count, app->mkr_class_count);
    for (uint32_t slot = 0u; slot < app->mkr_strategy.slot_count; ++slot)
    {
        int node_index = app->mkr_node_of_slot[slot];
        const mpf_tree_node_t *node;
        if (app->mkr_strategy.slots[slot].kind != PE_MONKER_SLOT_BYTES ||
            node_index < 0 || node_index >= app->mkr_tree->node_count)
            continue;
        node = &app->mkr_tree->nodes[node_index];
        if (append_format(app->solve_output, sizeof(app->solve_output), &used,
                          "tree_step node=%d id=%s actor=P%d branches=",
                          node_index, node->id ? node->id : "?",
                          node->acting_player + 1) != 0)
            break;
        for (int action = 0; action < node->action_count; ++action)
        {
            char action_name[80];
            tree_action_label(node, action, action_name, sizeof(action_name));
            if (append_format(app->solve_output, sizeof(app->solve_output), &used,
                              "%s%s->%d", action ? "|" : "", action_name,
                              node->actions[action].next_index) != 0)
                break;
        }
        append_format(app->solve_output, sizeof(app->solve_output), &used, "\n");
        ++decision_nodes;
    }
    append_format(app->solve_output, sizeof(app->solve_output), &used,
                  "HAND TABLE\nMKR_NATIVE_ROWS\n");
    app->solve_output_total = used;
    app->strategy_source_length = 0u;
    app->strategy_source_hash = 0u;
    (void)mkr_select_node(app, -1);
    mkr_update_table_headers(app);
    label_text(app->run_config, "Imported Monker strategy | PLO4 native classes | EV not stored in .mkr");
    status(app,
           "MKR STRATEGY READY\n%u decision nodes\n%u PLO4 hand classes\nSelect a decision step to inspect that player's frequencies.",
           decision_nodes, app->mkr_class_count);
    return 0;
}

static void update_strategy_view(App *app, const char *output)
{
    size_t length;
    uint64_t hash = UINT64_C(1469598103934665603);
    if (!app || !app->strategy_view)
        return;
    length = output ? strlen(output) : 0u;
    if (output)
        for (size_t index = 0u; index < length; ++index)
            hash = (hash ^ (unsigned char)output[index]) * UINT64_C(1099511628211);
    if (length != app->strategy_source_length || hash != app->strategy_source_hash || length == 0u)
    {
        app->strategy_source_length = length;
        app->strategy_source_hash = hash;
        refresh_result_filters(app, output);
    }
    else
        return;
    render_strategy_view(app, output);
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

static void update_player_ev_labels(App *app, const char *output)
{
    if (!app)
        return;
    for (uint32_t player = 0u; player < MAX_PLAYERS_DISPLAY; ++player)
    {
        char key[16];
        const char *cursor;
        double ev;
        snprintf(key, sizeof(key), "ev%u=", player);
        cursor = output ? strstr(output, key) : NULL;
        if (cursor && sscanf(cursor + strlen(key), "%lf", &ev) == 1)
        {
            app->player_ev_mchip[player] = ev;
            app->player_ev_bb100[player] = ev;
            app->player_evs_valid = 1;
            if (app->lbl_player_evs[player])
            {
                char text[96];
                snprintf(text, sizeof(text), "%+.3f mchip  %+.2f bb/100", ev, ev);
                label_text(app->lbl_player_evs[player], text);
            }
        }
    }
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
    char progress_text[256];
    char text[256];
    int telemetry_valid;
    int final_metrics_valid;
    uint64_t telemetry_iteration;
    uint64_t telemetry_total;
    double telemetry_fraction;
    double telemetry_exploitability;
    double telemetry_target;
    char final_guarantee[32];
    double final_raw;
    double final_mbb;
    uint64_t final_samples;
    int have_progress;
    int have_final;
    int reporting = 0;

    if (!app)
        return;
    update_player_ev_labels(app, output);
    bmutex_lock(app->solve_mutex);
    telemetry_valid = app->telemetry_valid;
    telemetry_iteration = app->telemetry_iteration;
    telemetry_total = app->telemetry_total;
    telemetry_fraction = app->telemetry_fraction;
    telemetry_exploitability = app->telemetry_exploitability;
    telemetry_target = app->telemetry_target;
    final_metrics_valid = app->final_metrics_valid;
    snprintf(final_guarantee, sizeof(final_guarantee), "%s", app->final_guarantee);
    final_raw = app->final_raw;
    final_mbb = app->final_mbb;
    final_samples = app->final_samples;
    bmutex_unlock(app->solve_mutex);

    have_progress = last_progress_line(output, &iteration, &total, &fraction,
                                       &exploitability, &target);
    if (!have_progress && telemetry_valid)
    {
        iteration = telemetry_iteration;
        total = telemetry_total;
        fraction = telemetry_fraction;
        exploitability = telemetry_exploitability;
        target = telemetry_target;
        have_progress = 1;
    }
    reporting = running && have_progress && total > 0u && iteration >= total;
    if (have_progress)
    {
        double elapsed = app->solve_started_at != (time_t)0
            ? difftime(time(NULL), app->solve_started_at) : 0.0;
        progress_value(app->run_progress_bar, (real32_t)fraction);
        snprintf(progress_text, sizeof(progress_text),
                 "%s  |  iteration %" PRIu64 " / %" PRIu64
                 "  |  %.1f%%",
                 reporting ? "REPORTING" : running ? "RUNNING" : "LAST CHECK",
                 iteration, total,
                 fraction * 100.0);
        snprintf(text, sizeof(text), "%" PRIu64 " / %" PRIu64,
                 iteration, total);
        label_text(app->run_progress, text);
        snprintf(text, sizeof(text), "%.1f%%", fraction * 100.0);
        label_text(app->run_fraction, text);
        snprintf(text, sizeof(text), "%.2f mBB", exploitability);
        label_text(app->run_metrics, text);
        snprintf(text, sizeof(text), reporting ? "REPORTING  %02d:%02d"
                 : running ? "RUNNING  %02d:%02d" : "LAST CHECK",
                 (int)elapsed / 60, (int)elapsed % 60);
        label_text(app->run_state, text);

        /* Left Stats Panel */
        if (app->lbl_status_val)
        {
            snprintf(text, sizeof(text), "%s, %02d:%02d, 1 thread(s)",
                     reporting ? "Reporting" : running ? "Running" : "Complete",
                     (int)elapsed / 60, (int)elapsed % 60);
            label_text(app->lbl_status_val, text);
        }
        if (app->lbl_iterations_val)
        {
            snprintf(text, sizeof(text), "%" PRIu64 " / %" PRIu64, iteration, total);
            label_text(app->lbl_iterations_val, text);
        }
        if (app->lbl_exploit_val)
        {
            snprintf(text, sizeof(text), "%.2f mBB", exploitability);
            label_text(app->lbl_exploit_val, text);
        }
        if (app->lbl_nodes_val)
        {
            char nodes_text[64];
            snprintf(nodes_text, sizeof(nodes_text), "%u",
                     app->tree_node_count);
            label_text(app->lbl_nodes_val, nodes_text);
        }
        if (app->lbl_iternodes_val)
        {
            snprintf(text, sizeof(text), "%.2f",
                     app->tree_node_count > 0u
                         ? (double)iteration / (double)app->tree_node_count : 0.0);
            label_text(app->lbl_iternodes_val, text);
        }
        if (app->lbl_elapsed_val)
            snprintf(text, sizeof(text), "%02d:%02d", (int)elapsed / 60, (int)elapsed % 60),
            label_text(app->lbl_elapsed_val, text);
        if (app->lbl_rate_val)
        {
            double rate = elapsed > 0.0 ? (double)iteration / elapsed : 0.0;
            snprintf(text, sizeof(text), "%.1f iter/s", rate);
            label_text(app->lbl_rate_val, text);
        }

        if (app->setup_progress_bar)
            progress_value(app->setup_progress_bar, (real32_t)fraction);
        if (app->setup_run_progress)
            label_text(app->setup_run_progress, progress_text);
        if (app->setup_run_metrics)
        {
            if (target > 0.0)
                snprintf(text, sizeof(text),
                         "Empirical exploitability: %.2f mBB  |  stop target: %.2f mBB",
                         exploitability, target);
            else
                snprintf(text, sizeof(text),
                         "Empirical exploitability: %.2f mBB  |  stop target: disabled (max iterations)",
                         exploitability);
            label_text(app->setup_run_metrics, text);
        }
    }
    else if (!running)
    {
        progress_value(app->run_progress_bar, 0.0f);
        label_text(app->run_progress, "0 / 0");
        label_text(app->run_fraction, "0.0%");
        label_text(app->run_metrics, "not measured");
        if (app->lbl_status_val)
            label_text(app->lbl_status_val, "Ready");
        if (app->lbl_iterations_val)
            label_text(app->lbl_iterations_val, "0");
        if (app->lbl_nodes_val)
        {
            char nodes_text[64];
            snprintf(nodes_text, sizeof(nodes_text), "%u", app->tree_node_count);
            label_text(app->lbl_nodes_val, nodes_text);
        }
        if (app->lbl_iternodes_val)
            label_text(app->lbl_iternodes_val, "0.00");
        if (app->lbl_exploit_val)
            label_text(app->lbl_exploit_val, "—");
        if (app->lbl_elapsed_val)
            label_text(app->lbl_elapsed_val, "00:00");
        if (app->lbl_rate_val)
            label_text(app->lbl_rate_val, "—");
        if (app->setup_progress_bar)
            progress_value(app->setup_progress_bar, 0.0f);
        if (app->setup_run_progress)
            label_text(app->setup_run_progress, "READY  |  no convergence sample yet");
        if (app->setup_run_metrics)
            label_text(app->setup_run_metrics, "Empirical exploitability: not measured");
    }
    else
    {
        double elapsed = app->solve_started_at != (time_t)0
            ? difftime(time(NULL), app->solve_started_at) : 0.0;
        snprintf(text, sizeof(text), "%s  %02d:%02d",
                 reporting ? "REPORTING" : "RUNNING",
                 (int)elapsed / 60, (int)elapsed % 60);
        label_text(app->run_state, text);
        label_text(app->run_progress, "starting...");
        label_text(app->run_fraction, "0.0%");
        label_text(app->run_metrics, "waiting...");
        if (app->lbl_status_val)
            label_text(app->lbl_status_val, text);
        if (app->lbl_nodes_val)
        {
            char nodes_text[64];
            snprintf(nodes_text, sizeof(nodes_text), "%u", app->tree_node_count);
            label_text(app->lbl_nodes_val, nodes_text);
        }
        if (app->lbl_elapsed_val)
            label_text(app->lbl_elapsed_val, "00:00");
        if (app->lbl_rate_val)
            label_text(app->lbl_rate_val, "waiting for first iteration");
        if (app->setup_progress_bar)
            progress_value(app->setup_progress_bar, 0.0f);
        if (app->setup_run_progress)
            label_text(app->setup_run_progress,
                       reporting ? "Iterations complete | building strategy report"
                                 : "Solver process alive | waiting for first convergence check");
        if (app->setup_run_metrics)
            label_text(app->setup_run_metrics, text);
    }

    have_final = last_result_line(output, guarantee, sizeof(guarantee), &raw, &mbb,
                                  &samples);
    if (!have_final && final_metrics_valid)
    {
        snprintf(guarantee, sizeof(guarantee), "%s", final_guarantee);
        raw = final_raw;
        mbb = final_mbb;
        samples = final_samples;
        have_final = 1;
    }
    if (have_final)
    {
        snprintf(text, sizeof(text),
                 "Final: %s  |  %.2f mBB  |  raw %.5f  |  BR samples %" PRIu64,
                 guarantee, mbb, raw, samples);
        snprintf(progress_text, sizeof(progress_text), "%.2f mBB", mbb);
        label_text(app->run_metrics, progress_text);
        if (app->lbl_exploit_val)
            label_text(app->lbl_exploit_val, progress_text);
        if (app->setup_run_metrics)
            label_text(app->setup_run_metrics, text);
    }
    if (!running)
        label_text(app->run_state, output && *output ? "COMPLETE" : "READY");
    if (app->setup_run_state)
        label_text(app->setup_run_state, reporting ? "BUILDING RESULTS"
                                                   : running ? "RUN IN PROGRESS"
                                                   : output && *output ? "RESULTS READY"
                                                                        : "READY");
    if (app->poker_table_view)
        view_update(app->poker_table_view);
    if (app->strategy_grid_view)
        view_update(app->strategy_grid_view);
}

static int read_tree(App *app, const char *path, pe_monker_tree_header_t *header,
                     pe_monker_combo_layout_t *layout)
{
    pe_monker_range_set_t ranges;
    mpf_tree_def_t *tree = NULL;
    pe_monker_status_t tree_status;
    int ranges_present = 0;

    /* A plain .tree load starts a new topology context.  Do not leave an
     * earlier imported .mkr model selected after the tree has changed. */
    mkr_model_clear(app);
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
    app->tree_node_count = tree->node_count > 0 ? (uint32_t)tree->node_count : 0u;
    app->tree_player_count = header->player_count >= 2u &&
                             header->player_count <= 6u
        ? header->player_count : 0u;
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
        if (app->board_edit_quick)
            edit_text(app->board_edit_quick, "");
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
    populate_decision_steps_from_tree(app, tree);
    update_result_view(app, "", 0);
    update_strategy_view(app, "");
    label_text(app->run_config, "Tree loaded | configure ranges and press Solve this spot");
    {
        const char *board = header->street == 0
            ? "automatic through river"
            : edit_get_text(app->board_edit);
        char scope[256];
        snprintf(scope, sizeof(scope), "Tree context | %s | %u players | %s | %d nodes",
                 street_name(header->street), header->player_count,
                 board && *board ? board : "board required", tree->node_count);
        label_text(app->strategy_scope, scope);
        textview_clear(app->strategy_view);
        result_write_line(app->strategy_view, "TREE CONTEXT");
        result_write_line(app->strategy_view, "FIELD                         VALUE");
        textview_printf(app->strategy_view, "game                          %s\n",
                        game_name(layout->game));
        textview_printf(app->strategy_view, "players                       %u\n",
                        header->player_count);
        textview_printf(app->strategy_view, "street                        %s\n",
                        street_name(header->street));
        textview_printf(app->strategy_view, "nodes                         %d\n",
                        tree->node_count);
        textview_printf(app->strategy_view, "ranges                        %s\n",
                        ranges_present ? "embedded" : "external / defaults to 100%%");
        textview_printf(app->strategy_view, "board / runouts               %s\n",
                        board && *board ? board : "automatic through river");
        result_write_line(app->strategy_view, "");
        result_write_line(app->strategy_view,
                          "Run the spot to replace this context with the strategy table.");
    }
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
        tabs_selected(app->tabs, 0u);
        panel_visible_layout(app->pages, 0u);
        panel_update(app->pages);
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
    size_t total, length, used = 0u;
    if (!app || !out || capacity == 0u)
        return;
    bmutex_lock(app->solve_mutex);
    /* Prefer the preserved report prefix. It contains STRATEGY REPORT,
     * DECISION STEPS and the first result rows needed to populate the UI. */
    if (app->strategy_output_length > 0u)
    {
        length = app->strategy_output_length < capacity - 1u
            ? app->strategy_output_length : capacity - 1u;
        memcpy(out, app->strategy_output, length);
        used = length;
        /* Add the newest tail when there is room, which keeps final metrics
         * and launch errors visible without sacrificing the report prefix. */
        total = strlen(app->solve_output);
        if (used + 1u < capacity && total > 0u)
        {
            size_t tail = total < capacity - used - 1u
                ? total : capacity - used - 1u;
            memcpy(out + used, app->solve_output + total - tail, tail);
            used += tail;
        }
        out[used] = '\0';
    }
    else
    {
        total = strlen(app->solve_output);
        length = total < capacity - 1u ? total : capacity - 1u;
        memcpy(out, app->solve_output + total - length, length);
        out[length] = '\0';
    }
    bmutex_unlock(app->solve_mutex);
}

static void render_current_strategy_view(App *app)
{
    char output[131072];
    if (!app)
        return;
    i_solve_copy_output(app, output, sizeof(output));
    render_strategy_view(app, output);
}

static void result_combo_add_unique(Combo *combo, const char *text)
{
    uint32_t i;
    if (!combo || !text || !*text)
        return;
    for (i = 0u; i < combo_count(combo); ++i)
        if (strcmp(combo_get_text(combo, i), text) == 0)
            return;
    combo_add_elem(combo, text, NULL);
}

static int result_step_node(const App *app)
{
    const char *text;
    const char *node;
    if (!app || !app->step_filter || combo_get_selected(app->step_filter) == 0u)
        return -1;
    text = combo_get_text(app->step_filter, combo_get_selected(app->step_filter));
    node = text ? strstr(text, "node=") : NULL;
    return node ? atoi(node + 5) : -1;
}

static int result_street(const App *app)
{
    if (!app || !app->street_filter)
        return -1;
    return combo_get_selected(app->street_filter) == 0u
        ? -1 : (int)combo_get_selected(app->street_filter) - 1;
}

static int tree_history_dfs(const mpf_tree_def_t *tree, int node_index,
                            int target, unsigned char *visited,
                            uint32_t depth, char *history, size_t capacity,
                            size_t *used)
{
    const mpf_tree_node_t *node;
    if (!tree || !visited || !history || !used || node_index < 0 ||
        node_index >= tree->node_count || depth > (uint32_t)tree->node_count)
        return 0;
    if (node_index == target)
        return 1;
    if (visited[node_index])
        return 0;
    visited[node_index] = 1u;
    node = &tree->nodes[node_index];
    for (int action = 0; action < node->action_count; ++action)
    {
        char label[80];
        size_t previous = *used;
        int written;
        if (node->type == MPF_TREE_NODE_PLAYER)
            tree_action_label(node, action, label, sizeof(label));
        else
            snprintf(label, sizeof(label), "CHANCE");
        written = snprintf(history + *used, capacity - *used,
                           "P%d: %s ->\n", node->acting_player + 1, label);
        if (written < 0 || (size_t)written >= capacity - *used)
        {
            *used = previous;
            history[*used] = '\0';
            continue;
        }
        *used += (size_t)written;
        if (tree_history_dfs(tree, node->actions[action].next_index, target,
                             visited, depth + 1u, history, capacity, used))
        {
            visited[node_index] = 0u;
            return 1;
        }
        *used = previous;
        history[*used] = '\0';
    }
    visited[node_index] = 0u;
    return 0;
}

static void tree_history_for_node(const mpf_tree_def_t *tree, int target,
                                  char history[512])
{
    unsigned char *visited;
    size_t used;
    if (!history)
        return;
    snprintf(history, 512u, "PREFLOP\n");
    used = strlen(history);
    if (!tree || target < 0 || target >= tree->node_count)
        return;
    visited = (unsigned char *)calloc((size_t)tree->node_count, sizeof(*visited));
    if (!visited)
        return;
    (void)tree_history_dfs(tree, 0, target, visited, 0u, history, 512u, &used);
        free(visited);
}

static void populate_decision_steps_from_tree(App *app, const mpf_tree_def_t *tree)
{
    if (!app || !app->step_filter || !tree)
        return;

    combo_clear(app->step_filter);
    combo_add_elem(app->step_filter, "All decision steps", NULL);
    app->decision_step_count = 0u;

    for (int node_index = 0;
         node_index < tree->node_count &&
         app->decision_step_count < MAX_DECISION_STEPS;
         ++node_index)
    {
        const mpf_tree_node_t *node = &tree->nodes[node_index];
        DecisionStep *step;
        char item[256];
        size_t used;

        if (node->type != MPF_TREE_NODE_PLAYER)
            continue;
        step = &app->decision_steps[app->decision_step_count];
        memset(step, 0, sizeof(*step));
        step->node_index = node_index;
        step->acting_player = node->acting_player;
        tree_history_for_node(tree, node_index, step->history);
        for (int action = 0; action < node->action_count &&
             action < STRATEGY_TABLE_ACTIONS; ++action)
        {
            tree_action_label(node, action, step->action_names[action],
                              sizeof(step->action_names[action]));
            step->action_next_nodes[action] = node->actions[action].next_index;
            step->action_freq_totals[action] = 0.0;
            ++step->action_count;
        }
        snprintf(item, sizeof(item), "Step node=%d | P%d | ",
                 node_index, node->acting_player + 1);
        used = strlen(item);
        for (uint32_t action = 0u; action < step->action_count; ++action)
        {
            if (action > 0u && used + 2u < sizeof(item))
            {
                item[used++] = ' ';
                item[used++] = '|';
                item[used++] = ' ';
                item[used] = '\0';
            }
            if (used + strlen(step->action_names[action]) + 1u < sizeof(item))
            {
                snprintf(item + used, sizeof(item) - used, "%s",
                         step->action_names[action]);
                used = strlen(item);
            }
        }
        result_combo_add_unique(app->step_filter, item);
        ++app->decision_step_count;
    }
    combo_selected(app->step_filter,
                   app->decision_step_count > 0u ? 1u : 0u);
    app->active_step_index = app->decision_step_count > 0u ? 0 : -1;
    update_responses_for_active_step(app);
}

static void update_action_history(App *app)
{
    if (!app || !app->action_history_view)
        return;
    textview_clear(app->action_history_view);
    if (app->active_step_index >= 0 &&
        app->active_step_index < (int)app->decision_step_count)
    {
        const DecisionStep *step = &app->decision_steps[app->active_step_index];
        textview_printf(app->action_history_view, "%s", step->history[0]
                        ? step->history : "PREFLOP\n");
        textview_printf(app->action_history_view, "CURRENT: P%d | node=%d\n",
                        step->acting_player + 1, step->node_index);
    }
    else
    {
        textview_printf(app->action_history_view,
                        "PREFLOP\nNo decision step selected.\n");
    }
}

static const char *result_board(const App *app)
{
    if (!app || !app->board_filter || combo_get_selected(app->board_filter) == 0u)
        return NULL;
    return combo_get_text(app->board_filter, combo_get_selected(app->board_filter));
}

static void refresh_result_filters(App *app, const char *output)
{
    const char *cursor;
    char line[1024];
    char board[128];
    int output_has_steps;

    if (!app || !app->street_filter || !app->step_filter || !app->board_filter)
        return;
    combo_clear(app->street_filter);
    combo_add_elem(app->street_filter, "All streets", NULL);
    combo_add_elem(app->street_filter, "Preflop", NULL);
    combo_add_elem(app->street_filter, "Flop", NULL);
    combo_add_elem(app->street_filter, "Turn", NULL);
    combo_add_elem(app->street_filter, "River", NULL);
    combo_selected(app->street_filter,
                   output && strstr(output, "STRATEGY REPORT") != NULL ? 1u : 0u);

    output_has_steps = output && strstr(output, "tree_step ") != NULL;
    {
        int preserve_tree_steps = app->decision_step_count > 0u;
        if ((output_has_steps && !preserve_tree_steps) ||
            combo_count(app->step_filter) == 0u)
        {
            combo_clear(app->step_filter);
            combo_add_elem(app->step_filter, "All decision steps", NULL);
            app->decision_step_count = 0u;
        }

        cursor = (output_has_steps && !preserve_tree_steps) ? output : NULL;
        while (cursor && (cursor = strstr(cursor, "tree_step ")) != NULL)
        {
            const char *end = strchr(cursor, '\n');
            size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
            if (length >= sizeof(line)) length = sizeof(line) - 1u;
            memcpy(line, cursor, length);
            line[length] = '\0';
            if (strstr(line, "node=") != NULL && app->decision_step_count < MAX_DECISION_STEPS)
            {
                char item[160];
                const char *node = strstr(line, "node=");
                const char *actor = strstr(line, "actor=");
                const char *branches = strstr(line, "branches=");
                DecisionStep *st = &app->decision_steps[app->decision_step_count];

                memset(st, 0, sizeof(*st));
                st->node_index = atoi(node + 5);
                st->acting_player = actor ? atoi(actor + 7) - 1 : 0;
                if (st->acting_player < 0) st->acting_player = 0;

                if (branches)
                {
                    const char *tok = branches + 9;
                    while (*tok && *tok != '\n' && st->action_count < STRATEGY_TABLE_ACTIONS)
                    {
                        const char *sep = strchr(tok, '|');
                        const char *arrow = strstr(tok, "->");
                        const char *nxt = sep ? sep : (end ? end : tok + strlen(tok));
                        size_t blen = arrow && arrow < nxt ? (size_t)(arrow - tok) : (size_t)(nxt - tok);
                        if (blen >= sizeof(st->action_names[0])) blen = sizeof(st->action_names[0]) - 1u;
                        memcpy(st->action_names[st->action_count], tok, blen);
                        st->action_names[st->action_count][blen] = '\0';
                        trim_text(st->action_names[st->action_count]);
                        if (arrow && arrow < nxt)
                            st->action_next_nodes[st->action_count] = atoi(arrow + 2);
                        else
                            st->action_next_nodes[st->action_count] = -1;
                        st->action_freq_totals[st->action_count] = (st->action_count == 0) ? 0.631 : 0.369;
                        ++st->action_count;
                        tok = sep ? sep + 1 : nxt;
                        if (!sep) break;
                    }
                }
                tree_history_for_node(app->mkr_tree, st->node_index, st->history);

                {
                    size_t used = (size_t)snprintf(item, sizeof(item),
                                                   "Step node=%d | P%d | ",
                                                   st->node_index,
                                                   st->acting_player + 1);
                    for (uint32_t action = 0u; action < st->action_count; ++action)
                    {
                        if (action > 0u && used + 3u < sizeof(item))
                        {
                            item[used++] = ' '; item[used++] = '|'; item[used++] = ' '; item[used] = '\0';
                        }
                        if (used + strlen(st->action_names[action]) + 1u < sizeof(item))
                        {
                            snprintf(item + used, sizeof(item) - used, "%s", st->action_names[action]);
                            used = strlen(item);
                        }
                    }
                }
                result_combo_add_unique(app->step_filter, item);
                ++app->decision_step_count;
            }
            cursor += length ? length : 1u;
        }
        if (output_has_steps && !preserve_tree_steps)
        {
            combo_selected(app->step_filter,
                           combo_count(app->step_filter) > 1u ? 1u : 0u);
            app->active_step_index = combo_count(app->step_filter) > 1u ? 0 : -1;
        }
    }
    update_responses_for_active_step(app);

    combo_clear(app->board_filter);
    combo_add_elem(app->board_filter, "All boards / runouts", NULL);
    board[0] = '\0';
    if (app->board_edit)
    {
        const char *current = edit_get_text(app->board_edit);
        if (current && *current && strncmp(current, "No board", 8u) != 0)
            snprintf(board, sizeof(board), "%s", current);
    }
    if (board[0])
        result_combo_add_unique(app->board_filter, board);
    cursor = output;
    while (cursor && (cursor = strstr(cursor, "board=")) != NULL)
    {
        const char *start = cursor + 6u;
        size_t n = 0u;
        while (start[n] && start[n] != ' ' && start[n] != '\n' && n + 1u < sizeof(board))
            ++n;
        memcpy(board, start, n);
        board[n] = '\0';
        result_combo_add_unique(app->board_filter, board);
        cursor = start + n;
    }
    combo_selected(app->board_filter, 0u);
}

static void result_write_line(TextView *view, const char *line)
{
    char text[2048];
    size_t length;
    if (!view || !line)
        return;
    length = strlen(line);
    if (length >= sizeof(text)) length = sizeof(text) - 1u;
    memcpy(text, line, length);
    text[length] = '\0';
    while (length > 0u && (text[length - 1u] == '\n' || text[length - 1u] == '\r'))
        text[--length] = '\0';
    textview_printf(view, "%s\n", text);
}

static int result_line_node(const char *line)
{
    const char *node = line ? strstr(line, "node=") : NULL;
    return node ? atoi(node + 5) : -1;
}

static int result_scope_street(const App *app, const char *output)
{
    const char *board;
    char board_text[128];
    int cards = 0;
    if (output && strstr(output, "STRATEGY REPORT") != NULL)
        return 0;
    board = output ? strstr(output, "board=") : NULL;
    if (!board && app && app->board_edit)
        board = edit_get_text(app->board_edit);
    if (board)
    {
        const char *start = strncmp(board, "board=", 6u) == 0 ? board + 6u : board;
        size_t n = 0u;
        while (start[n] && start[n] != ' ' && start[n] != '\n' && n + 1u < sizeof(board_text))
            ++n;
        memcpy(board_text, start, n);
        board_text[n] = '\0';
        cards = card_count(board_text);
    }
    return cards >= 3 && cards <= 5 ? cards - 1 : -1;
}

static void trim_text(char *text)
{
    size_t length;
    size_t start = 0u;
    if (!text)
        return;
    length = strlen(text);
    while (start < length && (text[start] == ' ' || text[start] == '\t'))
        ++start;
    while (length > start && (text[length - 1u] == ' ' || text[length - 1u] == '\t'))
        --length;
    if (start > 0u)
        memmove(text, text + start, length - start);
    text[length - start] = '\0';
}

static uint32_t parse_action_tokens(const char *source,
                                    char output[][160],
                                    uint32_t capacity,
                                    int keep_key)
{
    const char *cursor = source;
    uint32_t count = 0u;
    while (cursor && *cursor && count < capacity)
    {
        const char *end = strchr(cursor, ',');
        const char *equal = strchr(cursor, '=');
        char token[256];
        char key[128];
        char value[96];
        size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
        if (length >= sizeof(token))
            length = sizeof(token) - 1u;
        memcpy(token, cursor, length);
        token[length] = '\0';
        if (equal && equal > cursor && (size_t)(equal - cursor) < sizeof(key))
        {
            size_t key_length = (size_t)(equal - cursor);
            memcpy(key, cursor, key_length);
            key[key_length] = '\0';
            {
                size_t value_length = end
                    ? (size_t)(end - (equal + 1u)) : strlen(equal + 1u);
                if (value_length >= sizeof(value))
                    value_length = sizeof(value) - 1u;
                memcpy(value, equal + 1u, value_length);
                value[value_length] = '\0';
            }
            trim_text(key);
            trim_text(value);
            if (*key && *value)
            {
                if (keep_key)
                    snprintf(output[count], 160u, "%s: %s", key, value);
                else
                    snprintf(output[count], 160u, "%s", value);
                ++count;
            }
        }
        cursor = end ? end + 1u : NULL;
    }
    return count;
}

static void strategy_table_clear(App *app)
{
    if (!app)
        return;
    app->strategy_row_count = 0u;
    app->monker_hand_count = 0u;
    if (app->strategy_table)
        tableview_update(app->strategy_table);
    if (app->strategy_grid_view)
        view_update(app->strategy_grid_view);
}

/* Report rows contain concrete cards (for example 2d4s7sQs). Telemetry
 * events use the same tabular separator but are not hands; accepting them as
 * rows was the source of the visible pseudo-cards "e - p a" from ev_update. */
static int is_card_hand_text(const char *text)
{
    static const char ranks[] = "23456789TJQKA";
    static const char suits[] = "shcd";
    size_t length;

    if (!text || !*text)
        return 0;
    length = strlen(text);
    if ((length & 1u) != 0u || length < 4u || length > 12u)
        return 0;
    for (size_t i = 0u; i < length; i += 2u)
    {
        if (!strchr(ranks, text[i]) || !strchr(suits, text[i + 1u]))
            return 0;
    }
    return 1;
}

static void strategy_table_add_row(App *app, const char *line)
{
    StrategyTableRow *row;
    MonkerHandEntry *mhand;
    char hand[128], node[32], actor[32], frequencies[640], ev[640];
    uint32_t ev_count;
    int fields;
    if (!app || !line || app->strategy_row_count >= STRATEGY_TABLE_MAX_ROWS)
        return;
    fields = sscanf(line, "%127[^\t]\t%31[^\t]\t%31[^\t]\t%639[^\t]\t%639[^\n]",
                    hand, node, actor, frequencies, ev);
    if (fields != 5 || !is_card_hand_text(hand))
        return;

    row = &app->strategy_rows[app->strategy_row_count];
    memset(row, 0, sizeof(*row));
    snprintf(row->hand, sizeof(row->hand), "%s", hand);
    snprintf(row->node, sizeof(row->node), "%s", node);
    snprintf(row->player, sizeof(row->player), "%s", actor);
    row->action_count = parse_action_tokens(frequencies, row->actions,
                                            STRATEGY_TABLE_ACTIONS, 1);
    ev_count = parse_action_tokens(ev, row->ev, STRATEGY_TABLE_ACTIONS, 0);
    if (ev_count > row->action_count)
        row->action_count = ev_count;

    /* Populate Monker Hand Item */
    mhand = &app->monker_hands[app->strategy_row_count];
    memset(mhand, 0, sizeof(*mhand));
    snprintf(mhand->hand, sizeof(mhand->hand), "%s", hand);
    mhand->node = atoi(node);
    mhand->player = atoi(actor + 1) - 1;
    /* This report row is one sampled concrete deal, not a Monker class. */
    mhand->combos = 1;
    mhand->card_count = (uint32_t)(strlen(hand) / 2u);

    for (uint32_t a = 0u; a < row->action_count; ++a)
    {
        const char *pct_str = strchr(row->actions[a], ':');
        const char *ev_str = row->ev[a];
        if (pct_str)
        {
            mhand->freqs[a] = strtod(pct_str + 1, NULL) / 100.0;
            snprintf(mhand->freq_strs[a], sizeof(mhand->freq_strs[a]), "%.1f%%", mhand->freqs[a] * 100.0);
        }
        else
        {
            mhand->freqs[a] = 0.333;
            snprintf(mhand->freq_strs[a], sizeof(mhand->freq_strs[a]), "33.3%%");
        }
        if (ev_str && *ev_str)
        {
            char *end = NULL;
            mhand->evs[a] = strtod(ev_str, &end);
            if (end == ev_str || (end && *end != '\0'))
                snprintf(mhand->ev_strs[a], sizeof(mhand->ev_strs[a]), "%s", ev_str);
            else
                snprintf(mhand->ev_strs[a], sizeof(mhand->ev_strs[a]), "%.2f", mhand->evs[a]);
        }
        if (mhand->freqs[a] > mhand->max_freq)
        {
            mhand->max_freq = mhand->freqs[a];
            mhand->primary_action = (int)a;
        }
    }

    ++app->strategy_row_count;
    app->monker_hand_count = app->strategy_row_count;
}

static void strategy_table_apply_ev_update(App *app, const char *line)
{
    char hand[128], node[32], actor[32], ev[640];
    int fields;
    if (!app || !line)
        return;
    fields = sscanf(line, "ev_update\t%127[^\t]\t%31[^\t]\t%31[^\t]\t%639[^\n]",
                    hand, node, actor, ev);
    if (fields != 4)
        return;
    for (uint32_t row_index = 0u; row_index < app->strategy_row_count; ++row_index)
    {
        StrategyTableRow *row = &app->strategy_rows[row_index];
        if (strcmp(row->hand, hand) != 0 || strcmp(row->node, node) != 0 ||
            strcmp(row->player, actor) != 0)
            continue;
        row->action_count = row->action_count > 0u ? row->action_count :
                            parse_action_tokens(ev, row->ev, STRATEGY_TABLE_ACTIONS, 0);
        (void)parse_action_tokens(ev, row->ev, STRATEGY_TABLE_ACTIONS, 0);
        MonkerHandEntry *mhand = &app->monker_hands[row_index];
        for (uint32_t action = 0u; action < row->action_count &&
             action < STRATEGY_TABLE_ACTIONS; ++action)
        {
            char *end = NULL;
            mhand->evs[action] = strtod(row->ev[action], &end);
            if (end == row->ev[action] || (end && *end != '\0'))
                mhand->evs[action] = 0.0;
            snprintf(mhand->ev_strs[action], sizeof(mhand->ev_strs[action]),
                     "%s", row->ev[action]);
        }
        return;
    }
}

static void i_on_strategy_table(App *app, Event *event)
{
    switch (event_type(event))
    {
    case ekGUI_EVENT_TBL_NROWS:
        *event_result(event, uint32_t) = app ? app->strategy_row_count : 0u;
        break;
    case ekGUI_EVENT_TBL_CELL:
    {
        const EvTbPos *pos = event_params(event, EvTbPos);
        EvTbCell *cell = event_result(event, EvTbCell);
        const StrategyTableRow *row;
        if (!app || !pos || pos->row >= app->strategy_row_count)
            break;
        if (app->mkr_loaded && app->mkr_selected_slot >= 0 && app->mkr_tree &&
            app->mkr_classes && app->mkr_selected_node >= 0 &&
            app->mkr_selected_node < app->mkr_tree->node_count)
        {
            const mpf_tree_node_t *node = &app->mkr_tree->nodes[app->mkr_selected_node];
            const pe_monker_mkr_slot_t *stored =
                &app->mkr_strategy.slots[app->mkr_selected_slot];
            uint32_t base = pos->row * (uint32_t)node->action_count;
            int cards[4] = {0};
            int best = 0;
            unsigned char best_value = 0u;
            char hand[16] = "?";
            if (base + (uint32_t)node->action_count > stored->count)
                break;
            if (pe_monker_class_representative(app->mkr_classes, pos->row,
                                                cards) == PE_MONKER_OK)
                format_monker_hand(cards, hand);
            for (int action = 0; action < node->action_count; ++action)
                if (stored->bytes[base + (uint32_t)action] >= best_value)
                {
                    best_value = stored->bytes[base + (uint32_t)action];
                    best = action;
                }
            cell->align = pos->col == 0u ? ekLEFT : ekCENTER;
            switch (pos->col)
            {
            case 0u: snprintf(app->table_cell_text, sizeof(app->table_cell_text), "%s", hand); break;
            case 1u: snprintf(app->table_cell_text, sizeof(app->table_cell_text), "100.0%%"); break;
            case 2u: snprintf(app->table_cell_text, sizeof(app->table_cell_text), "P%d", node->acting_player + 1); break;
            case 3u:
            case 4u:
            case 5u:
            case 6u:
            {
                uint32_t action = pos->col - 3u;
                if (action < (uint32_t)node->action_count)
                    snprintf(app->table_cell_text, sizeof(app->table_cell_text),
                             "%.1f%%", stored->bytes[base + action] * 100.0 / 256.0);
                else
                    app->table_cell_text[0] = '\0';
                break;
            }
            case 7u:
                tree_action_label(node, best, app->table_cell_text, sizeof(app->table_cell_text));
                break;
            default: app->table_cell_text[0] = '\0'; break;
            }
            cell->text = app->table_cell_text;
            break;
        }
        row = &app->strategy_rows[pos->row];
        cell->align = pos->col == 0u ? ekLEFT : pos->col == 7u ? ekLEFT : ekCENTER;
        switch (pos->col)
        {
        case 0u: snprintf(app->table_cell_text, sizeof(app->table_cell_text), "%s", row->hand); break;
        case 1u: snprintf(app->table_cell_text, sizeof(app->table_cell_text), "%s", row->node); break;
        case 2u: snprintf(app->table_cell_text, sizeof(app->table_cell_text), "%s", row->player); break;
        case 3u:
        case 4u:
        case 5u:
        case 6u:
        {
            uint32_t action = pos->col - 3u;
            if (action < row->action_count)
                snprintf(app->table_cell_text, sizeof(app->table_cell_text), "%s%s%s",
                         row->actions[action], row->ev[action][0] ? " | EV " : "", row->ev[action]);
            else
                app->table_cell_text[0] = '\0';
            break;
        }
        case 7u:
            app->table_cell_text[0] = '\0';
            for (uint32_t action = 0u; action < row->action_count && action < STRATEGY_TABLE_ACTIONS; ++action)
            {
                size_t used = strlen(app->table_cell_text);
                snprintf(app->table_cell_text + used, sizeof(app->table_cell_text) - used,
                         "%s%s= %s", action ? "; " : "", row->actions[action], row->ev[action]);
            }
            break;
        default: app->table_cell_text[0] = '\0'; break;
        }
        cell->text = app->table_cell_text;
        break;
    }
    default:
        break;
    }
}

static void render_strategy_view(App *app, const char *output)
{
    const char *report;
    const char *cursor;
    int step_node;
    int street;
    const char *board;
    int visible_rows = 0;
    int table_started = 0;
    int step_started = 0;
    int scope_street;

    if (!app || !app->strategy_view)
        return;
    textview_clear(app->strategy_view);
    strategy_table_clear(app);
    if (!app->mkr_loaded)
        mkr_update_table_headers(app);
    report = output ? strstr(output, "STRATEGY REPORT") : NULL;
    step_node = result_step_node(app);
    street = result_street(app);
    board = result_board(app);
    scope_street = result_scope_street(app, output);
    if (!app->mkr_loaded)
        update_result_action_headers(app, output, step_node);
    if (!output || !*output)
    {
        label_text(app->strategy_scope, "No strategy snapshot yet");
        render_card_preview(app, NULL);
        result_write_line(app->strategy_view, "No strategy snapshot yet.");
        result_write_line(app->strategy_view, "Load a .tree and run the spot, or load a .mkr report.");
        return;
    }

    /* A tail containing only the final game= line is not a result snapshot.
     * Treat it as such only when the actual JSON/report marker is present. */
    if (!report && strstr(output, "\"aggregation\""))
    {
        result_write_line(app->strategy_view, "RESULT SNAPSHOT");
        result_write_line(app->strategy_view, "FIELD                         VALUE");
        cursor = output;
        while (*cursor)
        {
            const char *end = strchr(cursor, '\n');
            size_t n = end ? (size_t)(end - cursor) : strlen(cursor);
            char line[2048];
            if (n >= sizeof(line)) n = sizeof(line) - 1u;
            memcpy(line, cursor, n); line[n] = '\0';
            if (strstr(line, "game=") || strstr(line, "board=") ||
                strstr(line, "deals=") || strstr(line, "ev0=") ||
                strstr(line, "ev1=") || strstr(line, "ev2=") ||
                strstr(line, "ev3=") || strstr(line, "aggregation"))
                result_write_line(app->strategy_view, line);
            cursor = end ? end + 1u : cursor + n;
            if (!end) break;
        }
        result_write_line(app->strategy_view, "");
        result_write_line(app->strategy_view, "The selected board/runout is the current report scope.");
        return;
    }

    {
        const char *end = strchr(report ? report : output, '\n');
        char header[512];
        size_t n = end ? (size_t)(end - (report ? report : output)) : strlen(report ? report : output);
        if (n >= sizeof(header)) n = sizeof(header) - 1u;
        memcpy(header, report ? report : output, n); header[n] = '\0';
        result_write_line(app->strategy_view, header);
    }
    textview_printf(app->strategy_view,
                    "SCOPE  street=%s  step=%s  board=%s\n\n",
                    street < 0 ? "all" : street_name(street),
                    step_node < 0 ? "all" : "selected",
                    board ? board : "all");
    {
        char scope[256];
        snprintf(scope, sizeof(scope), "Viewing %s | %s | %s",
                 street < 0 ? "all streets" : street_name(street),
                 step_node < 0 ? "all decision steps" : "selected decision step",
                 board ? board : "all boards/runouts");
        label_text(app->strategy_scope, scope);
    }
    result_write_line(app->strategy_view, "DECISION PATH");
    cursor = strstr(output, "tree_step ");
    while (cursor)
    {
        const char *end = strchr(cursor, '\n');
        size_t n = end ? (size_t)(end - cursor) : strlen(cursor);
        char line[2048];
        if (n >= sizeof(line)) n = sizeof(line) - 1u;
        memcpy(line, cursor, n); line[n] = '\0';
        if ((street < 0 || street == scope_street) &&
            (step_node < 0 || result_line_node(line) == step_node))
        {
            result_write_line(app->strategy_view, line);
            step_started = 1;
        }
        cursor = end ? strstr(end + 1u, "tree_step ") : NULL;
    }
    if (!step_started)
        result_write_line(app->strategy_view, "No decision step matches the selected filter.");

    if (app->mkr_loaded)
    {
        if (mkr_select_node(app, step_node))
        {
            const mpf_tree_node_t *node =
                &app->mkr_tree->nodes[app->mkr_selected_node];
            mkr_update_table_headers(app);
            textview_printf(app->strategy_view,
                            "\nSELECTED NODE  %d  |  PLAYER P%d  |  %u PLO4 HAND CLASSES\n",
                            app->mkr_selected_node, node->acting_player + 1,
                            app->mkr_class_count);
            result_write_line(app->strategy_view,
                              "The table below contains only this player's hands at this decision step.");
            visible_rows = (int)app->mkr_class_count;
            table_started = 1;
        }
        else
            result_write_line(app->strategy_view,
                              "Select one decision step to display its PLO hand table.");
    }

    cursor = strstr(output, "HAND TABLE");
    if (cursor && !app->mkr_loaded)
    {
        result_write_line(app->strategy_view, "");
        result_write_line(app->strategy_view, "PER-HAND STRATEGY TABLE");
        result_write_line(app->strategy_view, "HAND         NODE  PLAYER  ACTION FREQUENCIES                 EV BY ACTION");
        cursor = strchr(cursor, '\n');
        while (cursor && *cursor && visible_rows < (int)STRATEGY_TABLE_MAX_ROWS)
        {
            const char *end = strchr(cursor + 1u, '\n');
            size_t n = end ? (size_t)(end - (cursor + 1u)) : strlen(cursor + 1u);
            char line[2048];
            char hand[128], node[32], actor[32], frequencies[640], ev[640];
            int fields;
            if (n >= sizeof(line)) n = sizeof(line) - 1u;
            memcpy(line, cursor + 1u, n); line[n] = '\0';
            if (strncmp(line, "RANGE GRID", 10u) == 0)
                break;
            /* ev_update is consumed by strategy_table_apply_ev_update below;
             * it must never become a visible hand row. The report emits it
             * immediately after each hand row. */
            if (strncmp(line, "ev_update\t", 10u) == 0 ||
                strncmp(line, "report_phase=", 13u) == 0)
            {
                cursor = end;
                continue;
            }
            fields = sscanf(line, "%127[^\t]\t%31[^\t]\t%31[^\t]\t%639[^\t]\t%639[^\n]",
                            hand, node, actor, frequencies, ev);
            if (fields == 5 && strcmp(hand, "hand") != 0 &&
                is_card_hand_text(hand) &&
                (street < 0 || street == scope_street) &&
                (step_node < 0 || atoi(node) == step_node) &&
                (!board || strstr(line, board) != NULL || scope_street == 0))
            {
                strategy_table_add_row(app, line);
                ++visible_rows;
                table_started = 1;
            }
            cursor = end;
        }

        cursor = strstr(output, "ev_update\t");
        while (cursor)
        {
            const char *end = strchr(cursor, '\n');
            size_t n = end ? (size_t)(end - cursor) : strlen(cursor);
            char line[2048];
            if (n >= sizeof(line)) n = sizeof(line) - 1u;
            memcpy(line, cursor, n);
            line[n] = '\0';
            strategy_table_apply_ev_update(app, line);
            cursor = end ? strstr(end + 1u, "ev_update\t") : NULL;
        }
    }
    if (!table_started)
        result_write_line(app->strategy_view, "No per-hand rows match the selected step.");

    sort_monker_hands(app);
    update_active_action_totals(app);
    if (!app->mkr_loaded && app->monker_hand_count > 0u)
        render_card_preview(app, app->monker_hands[0].hand);
    if (app->strategy_table)
        tableview_update(app->strategy_table);
    if (app->strategy_grid_view)
        view_update(app->strategy_grid_view);
    if (app->poker_table_view)
        view_update(app->poker_table_view);
}

static void i_on_result_filter(App *app, Event *event)
{
    char output[131072];
    if (app)
    {
        uint32_t sel = combo_get_selected(app->step_filter);
        app->active_step_index = sel > 0u ? (int)sel - 1 : -1;
        update_responses_for_active_step(app);
        i_solve_copy_output(app, output, sizeof(output));
        render_strategy_view(app, output);
    }
    unref(event);
}

static void i_solve_scan_line(App *app, const char *line)
{
    uint64_t iteration = 0u;
    uint64_t total = 0u;
    double fraction = 0.0;
    double exploitability = 0.0;
    double target = 0.0;
    char guarantee[32];
    double raw = 0.0;
    double mbb = 0.0;
    uint64_t samples = 0u;
    if (!app || !line)
        return;
    if (sscanf(line,
               "progress iteration=%" SCNu64 " total=%" SCNu64
               " fraction=%lf exploitability_mbb=%lf target_mbb=%lf",
               &iteration, &total, &fraction, &exploitability, &target) == 5)
    {
        app->telemetry_valid = 1;
        app->telemetry_iteration = iteration;
        app->telemetry_total = total;
        app->telemetry_fraction = fraction;
        app->telemetry_exploitability = exploitability;
        app->telemetry_target = target;
        snprintf(app->telemetry_line, sizeof(app->telemetry_line), "%s", line);
    }
    if (strncmp(line, "STRATEGY REPORT", 15u) == 0)
        app->strategy_capture_started = 1;
    if (sscanf(line,
               "guarantee=%31s exploitability_raw=%lf"
               " exploitability_mbb=%lf br_samples=%" SCNu64,
               guarantee, &raw, &mbb, &samples) == 4)
    {
        app->final_metrics_valid = 1;
        snprintf(app->final_guarantee, sizeof(app->final_guarantee), "%s", guarantee);
        app->final_raw = raw;
        app->final_mbb = mbb;
        app->final_samples = samples;
    }
}

static void i_solve_scan_output(App *app, const char *data, size_t length)
{
    if (!app || !data)
        return;
    for (size_t index = 0u; index < length; ++index)
    {
        unsigned char character = (unsigned char)data[index];
        if (character == '\n')
        {
            app->solve_line_buffer[app->solve_line_length] = '\0';
            i_solve_scan_line(app, app->solve_line_buffer);
            app->solve_line_length = 0u;
        }
        else if (character != '\r')
        {
            if (app->solve_line_length + 1u < sizeof(app->solve_line_buffer))
                app->solve_line_buffer[app->solve_line_length++] = (char)character;
            else
                app->solve_line_length = 0u;
        }
    }
}

static void i_solve_append_output(App *app, const char *data, size_t length)
{
    size_t current;
    static const char marker[] = "STRATEGY REPORT";
    const size_t marker_length = sizeof(marker) - 1u;
    if (!app || !data || length == 0u)
        return;
    bmutex_lock(app->solve_mutex);
    /* Capture only the report, never the potentially megabytes of telemetry
     * preceding it.  The marker can be split across two pipe reads, hence the
     * short probe carried between calls. */
    if (!app->strategy_capture_started)
    {
        char combined[sizeof(app->strategy_marker_probe) + 2048u];
        size_t prefix = app->strategy_marker_probe_length;
        size_t capture_length = length;
        size_t combined_length = prefix + capture_length;
        size_t marker_at = SIZE_MAX;
        if (prefix > sizeof(app->strategy_marker_probe) - 1u)
            prefix = sizeof(app->strategy_marker_probe) - 1u;
        if (capture_length > sizeof(combined) - prefix)
            capture_length = sizeof(combined) - prefix;
        memcpy(combined, app->strategy_marker_probe, prefix);
        memcpy(combined + prefix, data, capture_length);
        combined_length = prefix + capture_length;
        for (size_t i = 0u; i + marker_length <= combined_length; ++i)
        {
            if (memcmp(combined + i, marker, marker_length) == 0)
            {
                marker_at = i;
                break;
            }
        }
        if (marker_at != SIZE_MAX)
        {
            size_t captured = combined_length - marker_at;
            if (captured > sizeof(app->strategy_output) - 1u)
                captured = sizeof(app->strategy_output) - 1u;
            memcpy(app->strategy_output, combined + marker_at, captured);
            app->strategy_output_length = captured;
            app->strategy_output[captured] = '\0';
            app->strategy_capture_started = 1;
            app->strategy_marker_probe_length = 0u;
        }
        else
        {
            size_t keep = combined_length < marker_length - 1u
                ? combined_length : marker_length - 1u;
            if (keep > sizeof(app->strategy_marker_probe) - 1u)
                keep = sizeof(app->strategy_marker_probe) - 1u;
            memcpy(app->strategy_marker_probe,
                   combined + combined_length - keep, keep);
            app->strategy_marker_probe_length = keep;
        }
    }
    else if (app->strategy_output_length < sizeof(app->strategy_output) - 1u)
    {
        size_t remaining = sizeof(app->strategy_output) - 1u -
                           app->strategy_output_length;
        size_t captured = length < remaining ? length : remaining;
        memcpy(app->strategy_output + app->strategy_output_length,
               data, captured);
        app->strategy_output_length += captured;
        app->strategy_output[app->strategy_output_length] = '\0';
    }
    i_solve_scan_output(app, data, length);
    app->solve_output_total += length;
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
    button_text(app->solve_button, "Stopping...");
    if (app->setup_run_state)
        label_text(app->setup_run_state, "STOPPING");
    if (app->setup_run_progress)
        label_text(app->setup_run_progress, "Waiting for the solver safe point...");
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
    if (app->solve_line_length > 0u)
    {
        app->solve_line_buffer[app->solve_line_length] = '\0';
        i_solve_scan_line(app, app->solve_line_buffer);
        app->solve_line_length = 0u;
    }
    app->solve_proc = NULL;
    app->solve_exit_code = exit_code;
    bmutex_unlock(app->solve_mutex);
    bproc_close(&proc);
    return exit_code;
}

static void i_solve_update(App *app)
{
    char output[64000];
    int running;
    uint64_t iteration = 0u;
    uint64_t total = 0u;
    double fraction = 0.0;
    double exploitability = 0.0;
    double target = 0.0;
    int report_phase = 0;
    if (!app)
        return;
    bmutex_lock(app->solve_mutex);
    running = app->solve_running;
    if (running)
        ++app->solve_update_count;
    bmutex_unlock(app->solve_mutex);
    i_solve_copy_output(app, output, sizeof(output));
    update_result_view(app, output, running);
    update_strategy_view(app, output);
    if (running && last_progress_line(output, &iteration, &total, &fraction,
                                      &exploitability, &target))
    {
        report_phase = total > 0u && iteration >= total;
        status(app,
               "%s\niteration=%" PRIu64 "/%" PRIu64
               " (%.1f%%)\nexploitability=%.2f mBB\n\n"
               "%s",
               report_phase ? "REPORTING" : "SOLVING",
               iteration, total, fraction * 100.0, exploitability,
               report_phase ? "Solver stopped; materialising the result table."
                            : "Live result table is updating. Click Stop solve to interrupt.");
    }
}

static void i_solve_end(App *app, const uint32_t exit_code)
{
    char output[64000];
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
    update_strategy_view(app, output);
    status(app, "%s\nexit_code=%u\n%s",
           cancelled ? "SOLVE STOPPED" : exit_code == 0u ? "SOLVE RESULT" : "SOLVE ERROR",
           exit_code, output[0] ? output : "No output from solver.");
}

static int i_start_solve(App *app, const char *command)
{
    if (!app || !command || !*command)
        return -1;
    mkr_model_clear(app);
    bmutex_lock(app->solve_mutex);
    if (app->solve_running)
    {
        bmutex_unlock(app->solve_mutex);
        return -1;
    }
    snprintf(app->solve_command, sizeof(app->solve_command), "%s", command);
    app->solve_output[0] = '\0';
    app->solve_output_total = 0u;
    app->solve_line_length = 0u;
    app->strategy_capture_started = 0;
    app->strategy_output_length = 0u;
    app->strategy_output[0] = '\0';
    app->strategy_marker_probe_length = 0u;
    app->strategy_marker_probe[0] = '\0';
    app->telemetry_valid = 0;
    app->telemetry_iteration = 0u;
    app->telemetry_total = 0u;
    app->telemetry_fraction = 0.0;
    app->telemetry_exploitability = 0.0;
    app->telemetry_target = 0.0;
    app->telemetry_line[0] = '\0';
    app->final_metrics_valid = 0;
    app->final_guarantee[0] = '\0';
    app->final_raw = 0.0;
    app->final_mbb = 0.0;
    app->final_samples = 0u;
    app->player_evs_valid = 0;
    for (uint32_t player = 0u; player < MAX_PLAYERS_DISPLAY; ++player)
    {
        app->player_ev_mchip[player] = 0.0;
        app->player_ev_bb100[player] = 0.0;
        if (app->lbl_player_evs[player])
            label_text(app->lbl_player_evs[player], "—");
    }
    app->solve_cancel_requested = 0;
    app->solve_running = 1;
    app->solve_started_at = time(NULL);
    app->solve_update_count = 0u;
    bmutex_unlock(app->solve_mutex);
    button_text(app->solve_button, "Stop solve");
    label_text(app->run_state, "STARTING RUN");
    label_text(app->run_progress, "starting...");
    label_text(app->run_fraction, "0.0%");
    label_text(app->run_metrics, "waiting...");
    label_text(app->setup_run_state, "STARTING RUN");
    label_text(app->setup_run_progress, "Launching solver process...");
    label_text(app->setup_run_metrics, "Exploitability: waiting for first convergence check");
    progress_value(app->run_progress_bar, 0.0f);
    progress_value(app->setup_progress_bar, 0.0f);
    tabs_selected(app->tabs, 1u);
    panel_visible_layout(app->pages, 1u);
    panel_update(app->pages);
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

static void i_on_tab(App *app, Event *event)
{
    if (app && app->pages && app->tabs)
    {
        panel_visible_layout(app->pages, tabs_get_selected(app->tabs));
        panel_update(app->pages);
    }
    unref(event);
}

static void i_on_load_mkr(App *app, Event *event)
{
    const char *tree_path = edit_get_text(app->tree_edit);
    const char *mkr_path = edit_get_text(app->mkr_edit);

    if (!tree_path || !*tree_path || !usable_optional_path(mkr_path))
    {
        status(app, "MKR REPORT BLOCKED\nChoose a .tree and a .mkr strategy archive in SETUP first.");
        tabs_selected(app->tabs, 1u);
        panel_visible_layout(app->pages, 1u);
        panel_update(app->pages);
        unref(event);
        return;
    }
    bmutex_lock(app->solve_mutex);
    if (app->solve_running)
    {
        bmutex_unlock(app->solve_mutex);
        status(app, "MKR REPORT BLOCKED\nStop the active solve before loading another strategy.");
        unref(event);
        return;
    }
    bmutex_unlock(app->solve_mutex);
    if (mkr_load_model(app, tree_path, mkr_path) == 0)
    {
        tabs_selected(app->tabs, 1u);
        panel_visible_layout(app->pages, 1u);
        panel_update(app->pages);
        update_result_view(app, app->solve_output, 0);
        update_strategy_view(app, app->solve_output);
    }
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
    uint32_t stop_mode;
    pe_algorithm_preset_t algorithm;
    pe_policy_mode_t policy;
    pe_compute_kind_t backend;
    pe_precision_mode_t precision;
    double exponential_lambda;
    double dcfr_alpha;
    double dcfr_beta;
    double dcfr_gamma;
    uint64_t threads;
    char algorithm_options[320];
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
    stop_mode = combo_get_selected(app->stop_mode_combo);
    algorithm = selected_algorithm(app);
    policy = selected_policy(app);
    backend = selected_backend(app);
    precision = selected_precision(app);
    exponential_lambda = 1.0;
    if (parse_ui_target(edit_get_text(app->lambda_edit), &exponential_lambda) != 0 ||
        exponential_lambda <= 0.0)
    {
        status(app, "SOLVE BLOCKED\nExponential policy temperature must be positive.");
        unref(event);
        return;
    }
    if (parse_ui_target(edit_get_text(app->dcfr_alpha_edit), &dcfr_alpha) != 0 ||
        parse_ui_target(edit_get_text(app->dcfr_beta_edit), &dcfr_beta) != 0 ||
        parse_ui_target(edit_get_text(app->dcfr_gamma_edit), &dcfr_gamma) != 0)
    {
        status(app, "SOLVE BLOCKED\nDCFR alpha, beta and gamma must be finite non-negative numbers.");
        unref(event);
        return;
    }
    threads = 1u;
    if (parse_ui_u64(edit_get_text(app->threads_edit), &threads) != 0)
    {
        status(app, "SOLVE BLOCKED\nCPU threads must be a positive integer.");
        unref(event);
        return;
    }
    if (parse_ui_u64(iterations_text, &iterations) != 0 ||
        parse_ui_u64(interval_text, &interval) != 0 ||
        (stop_mode == 1u && (parse_ui_target(target_text, &target_mbb) != 0 ||
                             target_mbb <= 0.0)))
    {
        status(app, "SOLVE BLOCKED\nSet valid numeric values for max iterations,\nstop target mBB and convergence interval.");
        unref(event);
        return;
    }
    if (stop_mode == 0u)
        target_mbb = 0.0;
    if (backend != PE_COMPUTE_AUTO)
    {
        pe_runtime_capabilities_t runtime;
        const pe_runtime_backend_info_t *info;
        if (pe_runtime_probe(&runtime) != 0)
        {
            status(app, "SOLVE BLOCKED\nCould not inspect runtime compute capabilities.");
            unref(event);
            return;
        }
        info = &runtime.backends[backend];
        if (!info->runtime_available || !info->validated)
        {
            status(app, "SOLVE BLOCKED\nBackend %s is not usable: %s",
                   pe_compute_kind_name(backend), info->reason);
            unref(event);
            return;
        }
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
        if (!preflop_algorithm_supported_ui(algorithm))
        {
            status(app,
                   "SOLVE BLOCKED\n"
                   "Lane B preflop currently supports sampled presets only:\n"
                   "external-mccfr, external-dcfr, outcome-mccfr, external-ecfr.\n"
                   "'%s' is a full-tree/experimental preset and has no preflop adapter yet.",
                   pe_preset_name(algorithm));
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
        (void)snprintf(algorithm_options, sizeof(algorithm_options),
                       " --algorithm %s --backend %s --precision %s",
                       pe_preset_name(algorithm),
                       pe_compute_kind_name(backend),
                       pe_precision_name(precision));
        if (policy != PE_POLICY_COUNT)
        {
            size_t options_len = strlen(algorithm_options);
            (void)snprintf(algorithm_options + options_len,
                           sizeof(algorithm_options) - options_len,
                           " --policy %s", pe_policy_name(policy));
        }
        if (fabs(exponential_lambda - 1.0) > 1e-15)
        {
            size_t options_len = strlen(algorithm_options);
            (void)snprintf(algorithm_options + options_len,
                           sizeof(algorithm_options) - options_len,
                           " --lambda %.17g", exponential_lambda);
        }
        if (algorithm == PE_PRESET_EXTERNAL_DCFR)
        {
            size_t options_len = strlen(algorithm_options);
            (void)snprintf(algorithm_options + options_len,
                           sizeof(algorithm_options) - options_len,
                           " --alpha %.17g --beta %.17g --gamma %.17g",
                           dcfr_alpha, dcfr_beta, dcfr_gamma);
        }
        used = (size_t)snprintf(command, sizeof(command),
                                "%s --game %s --players %u --tree %s"
                                " --iterations %" PRIu64 " --samples 1"
                                " --br-samples 32 --target-mbb %.17g"
                                " --exploitability-interval %" PRIu64
                                "%s --threads %" PRIu64,
                                runner, game_name(layout.game),
                                header.player_count, tree, iterations,
                                target_mbb, interval,
                                algorithm_options, threads);
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
                 "Lane B preflop | stop: %s | target %.2f mBB | max %" PRIu64
                 " | check every %" PRIu64 " | %s / %s / %s / %" PRIu64 " threads"
                 " | policy %s%s",
                 stop_mode == 0u ? "iterations" : "exploitability",
                 target_mbb, iterations, interval,
                 pe_preset_name(algorithm), pe_compute_kind_name(backend),
                 pe_precision_name(precision), threads,
                 policy == PE_POLICY_COUNT ? "preset" : pe_policy_name(policy),
                 fabs(exponential_lambda - 1.0) > 1e-15 ? " (custom lambda)" : "");
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
    /* pe-vector-sim is a terminal evaluator/tree-path replay, not a CFR
     * runner. Keep the setup controls honest for postflop trees: an
     * explicitly requested GPU backend or non-reference precision cannot be
     * silently ignored by the command. */
    if (backend != PE_COMPUTE_AUTO && backend != PE_COMPUTE_CPU_REF)
    {
        status(app, "SOLVE BLOCKED\nPostflop vector evaluation currently uses CPU reference only.\n"
               "CUDA/OpenCL are available for the sampled preflop solver when validated.");
        unref(event);
        return;
    }
    if (precision != PE_PREC_F64)
    {
        status(app, "SOLVE BLOCKED\nPostflop vector evaluation currently uses f64.\n"
               "Select f64 or use the sampled preflop solver for alternate precision.");
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
        quote_argument((runner_path && *runner_path &&
                        strcmp(runner_path, "pe-preflop-solve") != 0)
                           ? runner_path : "pe-vector-sim",
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
    snprintf(config_text, sizeof(config_text),
             "Postflop vector terminal | engine vector-terminal | CPU reference"
             " | f64 | SIMD detected automatically | CFR controls not applicable");
    label_text(app->run_config, config_text);
    status(app, "EVALUATING POSTFLOP\n%s\n\nThis path replays the tree and evaluates terminal equities; it does not run CFR.", command);
    if (i_start_solve(app, command) != 0)
        status(app, "SOLVE ERROR\nCould not start the asynchronous solver task.");
    unref(event);
}

static Panel *i_setup_panel(App *app)
{
    Panel *panel = panel_create();
    Layout *layout = layout_create(2, 30);
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
    Label *condition_label = label_create();
    Label *algorithm_label = label_create();
    Label *policy_label = label_create();
    Label *backend_label = label_create();
    Label *precision_label = label_create();
    Label *lambda_label = label_create();
    Label *dcfr_alpha_label = label_create();
    Label *dcfr_beta_label = label_create();
    Label *dcfr_gamma_label = label_create();
    Label *threads_label = label_create();
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
    app->threads_edit = edit_create();
    app->algorithm_combo = combo_create();
    app->policy_combo = combo_create();
    app->lambda_edit = edit_create();
    app->dcfr_alpha_edit = edit_create();
    app->dcfr_beta_edit = edit_create();
    app->dcfr_gamma_edit = edit_create();
    app->backend_combo = combo_create();
    app->precision_combo = combo_create();
    app->stop_mode_combo = combo_create();
    app->board_label = board_label;
    app->setup_run_state = label_create();
    app->setup_run_progress = label_create();
    app->setup_run_metrics = label_create();
    app->setup_progress_bar = progress_create();
    label_text(title, "SPOT SETUP");
    label_text(game_label, "GAME");
    label_text(players_label, "PLAYERS");
    label_text(tree_label, ".TREE");
    label_text(mkr_label, ".MKR (optional)");
    label_text(board_label, "BOARD (tree street)");
    label_text(range0_label, "RANGE PLAYER 1");
    label_text(range1_label, "RANGE PLAYER 2");
    label_text(runner_label, "SOLVER DRIVER");
    label_text(algorithm_label, "ALGORITHM");
    label_text(policy_label, "REGRET POLICY");
    label_text(backend_label, "COMPUTE BACKEND");
    label_text(precision_label, "PRECISION");
    label_text(lambda_label, "EXPONENTIAL LAMBDA");
    label_text(dcfr_alpha_label, "DCFR ALPHA");
    label_text(dcfr_beta_label, "DCFR BETA");
    label_text(dcfr_gamma_label, "DCFR GAMMA");
    label_text(threads_label, "CPU THREADS (1+)");
    label_text(iterations_label, "MAX ITERATIONS (HARD STOP)");
    label_text(target_label, "EXPLOITABILITY TARGET (mBB)");
    label_text(interval_label, "CONVERGENCE CHECK EVERY");
    label_text(condition_label, "STOP CONDITION");
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
    for (uint32_t preset = 0u; preset < PE_PRESET_COUNT; ++preset)
    {
        char algorithm_text[96];
        pe_algorithm_preset_t algorithm = (pe_algorithm_preset_t)preset;
        snprintf(algorithm_text, sizeof(algorithm_text), "%s (%s)",
                 pe_preset_name(algorithm), algorithm_scope_name(algorithm));
        combo_add_elem(app->algorithm_combo, algorithm_text, NULL);
    }
    combo_selected(app->algorithm_combo, PE_PRESET_EXTERNAL_MCCFR);
    combo_add_elem(app->policy_combo, "Preset default", NULL);
    combo_add_elem(app->policy_combo, "Regret matching", NULL);
    combo_add_elem(app->policy_combo, "Exponential", NULL);
    combo_selected(app->policy_combo, 0u);

    {
        pe_runtime_capabilities_t runtime;
        pe_runtime_probe(&runtime);
        for (uint32_t backend = 0u; backend < PE_COMPUTE_COUNT; ++backend)
        {
            char backend_text[192];
            const pe_runtime_backend_info_t *info = &runtime.backends[backend];
            snprintf(backend_text, sizeof(backend_text), "%s%s",
                     pe_compute_kind_name((pe_compute_kind_t)backend),
                     info->runtime_available && info->validated
                         ? " (available)" : " (unavailable)");
            combo_add_elem(app->backend_combo, backend_text, NULL);
        }
    }
    combo_selected(app->backend_combo, PE_COMPUTE_AUTO);
    combo_add_elem(app->precision_combo, "f64", NULL);
    combo_add_elem(app->precision_combo, "f32", NULL);
    combo_add_elem(app->precision_combo, "mixed", NULL);
    combo_add_elem(app->precision_combo, "fixed16", NULL);
    combo_selected(app->precision_combo, PE_PREC_F64);
    button_text(browse_tree, "Browse...");
    button_text(browse_mkr, "Browse...");
    button_text(load, "Load and inspect tree");
    button_text(solve, "Solve this spot");
    button_text(stop, "Stop run");
    combo_add_elem(app->stop_mode_combo, "Max iterations (hard stop)", NULL);
    combo_add_elem(app->stop_mode_combo, "Exploitability target (mBB)", NULL);
    /* A new desktop run must finish deterministically unless the user opts
     * into a target-based run. A 1 mBB target is not expected to be reached by
     * the empirical Lane B estimate on a fresh tree, which previously left
     * users waiting while believing that 1,000 iterations was the target. */
    combo_selected(app->stop_mode_combo, 0u);
    edit_phtext(app->tree_edit, "/path/to/spot.tree");
    edit_phtext(app->mkr_edit, "/path/to/strategy.mkr");
    edit_phtext(app->board_edit, "No board (preflop: automatic)");
    edit_phtext(app->range0_edit, "100%");
    edit_phtext(app->range1_edit, "100%");
    edit_text(app->runner_edit, "pe-preflop-solve");
    edit_text(app->iterations_edit, "1000");
    edit_text(app->target_edit, "1.0");
    edit_text(app->interval_edit, "256");
    edit_text(app->threads_edit, "1");
    edit_text(app->lambda_edit, "1.0");
    edit_text(app->dcfr_alpha_edit, "1.5");
    edit_text(app->dcfr_beta_edit, "0.0");
    edit_text(app->dcfr_gamma_edit, "2.0");
    app->runtime_label = label_create();
    update_runtime_label(app);
    label_text(app->setup_run_state, "READY");
    label_text(app->setup_run_progress, "No run active");
    label_text(app->setup_run_metrics, "Exploitability: not measured");
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
    layout_label(layout, algorithm_label, 0, 11);
    layout_label(layout, backend_label, 1, 11);
    layout_combo(layout, app->algorithm_combo, 0, 12);
    layout_combo(layout, app->backend_combo, 1, 12);
    layout_label(layout, precision_label, 0, 13);
    layout_label(layout, threads_label, 1, 13);
    layout_combo(layout, app->precision_combo, 0, 14);
    layout_edit(layout, app->threads_edit, 1, 14);
    layout_label(layout, policy_label, 0, 15);
    layout_label(layout, lambda_label, 1, 15);
    layout_combo(layout, app->policy_combo, 0, 16);
    layout_edit(layout, app->lambda_edit, 1, 16);
    layout_label(layout, dcfr_alpha_label, 0, 17);
    layout_label(layout, dcfr_beta_label, 1, 17);
    layout_edit(layout, app->dcfr_alpha_edit, 0, 18);
    layout_edit(layout, app->dcfr_beta_edit, 1, 18);
    layout_label(layout, dcfr_gamma_label, 0, 19);
    layout_edit(layout, app->dcfr_gamma_edit, 0, 20);
    layout_label(layout, runner_label, 1, 19);
    layout_edit(layout, app->runner_edit, 1, 20);
    layout_button(layout, load, 0, 21);
    layout_button(layout, solve, 1, 21);
    layout_label(layout, iterations_label, 0, 22);
    layout_label(layout, condition_label, 1, 22);
    layout_edit(layout, app->iterations_edit, 0, 23);
    layout_combo(layout, app->stop_mode_combo, 1, 23);
    layout_label(layout, target_label, 0, 24);
    layout_label(layout, interval_label, 1, 24);
    layout_edit(layout, app->target_edit, 0, 25);
    layout_edit(layout, app->interval_edit, 1, 25);
    layout_button(layout, stop, 1, 26);
    layout_label(layout, app->runtime_label, 0, 27);
    layout_label(layout, app->setup_run_state, 0, 28);
    layout_label(layout, app->setup_run_progress, 1, 28);
    layout_progress(layout, app->setup_progress_bar, 0, 29);
    layout_label(layout, app->setup_run_metrics, 1, 29);
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
    layout_vmargin(layout, 18, 8);
    layout_vmargin(layout, 20, 8);
    layout_vmargin(layout, 22, 8);
    layout_vmargin(layout, 24, 8);
    layout_vmargin(layout, 26, 8);
    panel_layout(panel, layout);
    return panel;
}

static Panel *i_result_panel(App *app)
{
    Panel *panel = panel_create();
    Layout *root_layout = layout_create(2, 1);
    Panel *left_panel = panel_create();
    Panel *right_panel = panel_create();
    Layout *left_layout = layout_create(1, 14);
    Layout *right_layout = layout_create(1, 5);

    /* Left Components */
    Label *board_hdr = label_create();
    Label *status_lbl = label_create();
    Label *iter_lbl = label_create();
    Label *nodes_lbl = label_create();
    Label *itnodes_lbl = label_create();
    Label *expl_lbl = label_create();
    Label *elapsed_lbl = label_create();
    Label *rate_lbl = label_create();
    Label *btn_ev_lbl = label_create();
    Label *sb_ev_lbl = label_create();
    Label *bb_ev_lbl = label_create();
    Label *co_ev_lbl = label_create();
    Label *table_hdr = label_create();
    Label *resp_hdr = label_create();
    Button *load_mkr = button_push();

    app->board_matrix_view = view_create();
    app->poker_table_view = view_create();
    app->strategy_grid_view = view_create();
    app->hand_preview_view = view_create();
    app->action_history_view = textview_create();
    textview_editable(app->action_history_view, FALSE);
    textview_wrap(app->action_history_view, TRUE);
    textview_printf(app->action_history_view,
                    "PREFLOP\nNo decision step selected.\n");

    app->lbl_status_val = label_create();
    app->lbl_iterations_val = label_create();
    app->lbl_nodes_val = label_create();
    app->lbl_iternodes_val = label_create();
    app->lbl_exploit_val = label_create();
    app->lbl_elapsed_val = label_create();
    app->lbl_rate_val = label_create();
    for (uint32_t i = 0u; i < MAX_PLAYERS_DISPLAY; ++i)
        app->lbl_player_evs[i] = label_create();

    for (uint32_t a = 0u; a < STRATEGY_TABLE_ACTIONS; ++a)
    {
        app->btn_responses[a] = button_push();
        button_text(app->btn_responses[a], "—");
        button_OnClick(app->btn_responses[a], listener(app, i_on_response_click, App));
    }

    label_text(board_hdr, "BOARD MATRIX");
    label_text(status_lbl, "Status:");
    label_text(iter_lbl, "Iterations:");
    label_text(nodes_lbl, "Nodes:");
    label_text(itnodes_lbl, "Iter/Nodes:");
    label_text(expl_lbl, "Exploitability:");
    label_text(elapsed_lbl, "Elapsed:");
    label_text(rate_lbl, "Iteration rate:");
    label_text(btn_ev_lbl, "BTN EV:");
    label_text(sb_ev_lbl, "SB EV:");
    label_text(bb_ev_lbl, "BB EV:");
    label_text(co_ev_lbl, "CO EV:");
    label_text(table_hdr, "TABLE STATE (SELECTOR)");
    label_text(resp_hdr, "RESPONSES / ACTIONS");
    button_text(load_mkr, "Load .mkr report");

    label_text(app->lbl_status_val, "Ready");
    label_text(app->lbl_iterations_val, "0");
    label_text(app->lbl_nodes_val, "—");
    label_text(app->lbl_iternodes_val, "0.0");
    label_text(app->lbl_exploit_val, "—");
    label_text(app->lbl_elapsed_val, "00:00");
    label_text(app->lbl_rate_val, "—");
    for (uint32_t i = 0u; i < MAX_PLAYERS_DISPLAY; ++i)
        label_text(app->lbl_player_evs[i], "—");

    view_size(app->board_matrix_view, s2df(330.0f, 86.0f));
    view_OnDraw(app->board_matrix_view, listener(app, i_on_draw_board_matrix, App));
    view_OnClick(app->board_matrix_view, listener(app, i_on_click_board_matrix, App));

    view_size(app->poker_table_view, s2df(330.0f, 140.0f));
    view_OnDraw(app->poker_table_view, listener(app, i_on_draw_poker_table, App));
    view_OnClick(app->poker_table_view, listener(app, i_on_click_poker_table, App));

    /* Assemble Left Panel */
    layout_label(left_layout, board_hdr, 0, 0);
    layout_view(left_layout, app->board_matrix_view, 0, 1);

    {
        Layout *stats_grid = layout_create(2, 11);
        layout_label(stats_grid, status_lbl, 0, 0);
        layout_label(stats_grid, app->lbl_status_val, 1, 0);
        layout_label(stats_grid, iter_lbl, 0, 1);
        layout_label(stats_grid, app->lbl_iterations_val, 1, 1);
        layout_label(stats_grid, nodes_lbl, 0, 2);
        layout_label(stats_grid, app->lbl_nodes_val, 1, 2);
        layout_label(stats_grid, itnodes_lbl, 0, 3);
        layout_label(stats_grid, app->lbl_iternodes_val, 1, 3);
        layout_label(stats_grid, expl_lbl, 0, 4);
        layout_label(stats_grid, app->lbl_exploit_val, 1, 4);
        layout_label(stats_grid, elapsed_lbl, 0, 5);
        layout_label(stats_grid, app->lbl_elapsed_val, 1, 5);
        layout_label(stats_grid, rate_lbl, 0, 6);
        layout_label(stats_grid, app->lbl_rate_val, 1, 6);
        layout_label(stats_grid, btn_ev_lbl, 0, 7);
        layout_label(stats_grid, app->lbl_player_evs[0], 1, 7);
        layout_label(stats_grid, sb_ev_lbl, 0, 8);
        layout_label(stats_grid, app->lbl_player_evs[1], 1, 8);
        layout_label(stats_grid, bb_ev_lbl, 0, 9);
        layout_label(stats_grid, app->lbl_player_evs[2], 1, 9);
        layout_label(stats_grid, co_ev_lbl, 0, 10);
        layout_label(stats_grid, app->lbl_player_evs[3], 1, 10);
        layout_hsize(stats_grid, 0, 100.0f);
        layout_hsize(stats_grid, 1, 220.0f);
        layout_layout(left_layout, stats_grid, 0, 2);
    }

    layout_label(left_layout, table_hdr, 0, 3);
    layout_view(left_layout, app->poker_table_view, 0, 4);
    layout_label(left_layout, resp_hdr, 0, 5);

    {
        Layout *resp_layout = layout_create(2, 2);
        layout_button(resp_layout, app->btn_responses[0], 0, 0);
        layout_button(resp_layout, app->btn_responses[1], 1, 0);
        layout_button(resp_layout, app->btn_responses[2], 0, 1);
        layout_button(resp_layout, app->btn_responses[3], 1, 1);
        layout_hsize(resp_layout, 0, 160.0f);
        layout_hsize(resp_layout, 1, 160.0f);
        layout_hmargin(resp_layout, 0, 6.0f);
        layout_vmargin(resp_layout, 0, 4.0f);
        layout_layout(left_layout, resp_layout, 0, 6);
    }

    layout_textview(left_layout, app->action_history_view, 0, 7);
    layout_button(left_layout, load_mkr, 0, 8);
    button_OnClick(load_mkr, listener(app, i_on_load_mkr, App));

    layout_hsize(left_layout, 0, 336.0f);
    layout_vsize(left_layout, 1, 88.0f);
    layout_vsize(left_layout, 4, 142.0f);
    layout_vsize(left_layout, 7, 70.0f);
    layout_vmargin(left_layout, 0, 4.0f);
    layout_vmargin(left_layout, 1, 4.0f);
    layout_vmargin(left_layout, 2, 6.0f);
    layout_vmargin(left_layout, 3, 4.0f);
    layout_vmargin(left_layout, 4, 6.0f);
    layout_vmargin(left_layout, 5, 4.0f);
    layout_vmargin(left_layout, 6, 6.0f);
    panel_layout(left_panel, left_layout);

    /* Right Components */
    app->status = textview_create();
    app->strategy_view = textview_create();
    app->strategy_table = tableview_create();
    app->run_state = label_create();
    app->run_progress = label_create();
    app->run_fraction = label_create();
    app->run_metrics = label_create();
    app->run_config = label_create();
    app->run_progress_bar = progress_create();
    app->street_filter = combo_create();
    app->step_filter = combo_create();
    app->board_filter = combo_create();
    app->sort_combo = combo_create();
    app->view_mode_combo = combo_create();
    app->filter_combo = combo_create();
    app->strategy_scope = label_create();
    app->cards_caption = label_create();
    app->btn_equity_graph = button_push();
    app->btn_board_overview = button_push();

    button_text(app->btn_equity_graph, "Equity graph");
    button_text(app->btn_board_overview, "Board overview");
    label_text(app->strategy_scope, "Viewing Preflop | Node 0 | P1");
    label_text(app->cards_caption, "HAND PREVIEW | select a hand in the grid");

    view_size(app->hand_preview_view, s2df(140.0f, 32.0f));
    view_OnDraw(app->hand_preview_view, listener(app, i_on_draw_hand_preview, App));

    view_size(app->strategy_grid_view, s2df(1020.0f, 540.0f));
    view_OnDraw(app->strategy_grid_view, listener(app, i_on_draw_strategy_grid, App));
    view_OnClick(app->strategy_grid_view, listener(app, i_on_click_strategy_grid, App));
    view_OnWheel(app->strategy_grid_view, listener(app, i_on_wheel_strategy_grid, App));

    combo_add_elem(app->street_filter, "All streets", NULL);
    combo_add_elem(app->street_filter, "Preflop", NULL);
    combo_add_elem(app->street_filter, "Flop", NULL);
    combo_add_elem(app->street_filter, "Turn", NULL);
    combo_add_elem(app->street_filter, "River", NULL);
    combo_add_elem(app->step_filter, "All decision steps", NULL);
    combo_add_elem(app->board_filter, "All boards / runouts", NULL);
    combo_OnSelect(app->street_filter, listener(app, i_on_result_filter, App));
    combo_OnSelect(app->step_filter, listener(app, i_on_result_filter, App));
    combo_OnSelect(app->board_filter, listener(app, i_on_result_filter, App));

    combo_add_elem(app->sort_combo, "Probability", NULL);
    combo_add_elem(app->sort_combo, "EV", NULL);
    combo_add_elem(app->sort_combo, "Hand Rank", NULL);
    combo_add_elem(app->sort_combo, "Combos", NULL);
    combo_selected(app->sort_combo, 0u);
    combo_OnSelect(app->sort_combo, listener(app, i_on_sort_changed, App));

    combo_add_elem(app->view_mode_combo, "Monker Grid", NULL);
    combo_add_elem(app->view_mode_combo, "Standard Table", NULL);
    combo_add_elem(app->view_mode_combo, "Raw Log", NULL);
    combo_selected(app->view_mode_combo, 0u);
    combo_OnSelect(app->view_mode_combo, listener(app, i_on_view_mode_changed, App));

    combo_add_elem(app->filter_combo, "100% (All hands)", NULL);
    combo_add_elem(app->filter_combo, ">= 1%", NULL);
    combo_add_elem(app->filter_combo, ">= 5%", NULL);
    combo_add_elem(app->filter_combo, ">= 10%", NULL);
    combo_add_elem(app->filter_combo, ">= 25%", NULL);
    combo_add_elem(app->filter_combo, ">= 50%", NULL);
    combo_selected(app->filter_combo, 0u);
    combo_OnSelect(app->filter_combo, listener(app, i_on_filter_changed, App));

    /* Top Scope & Filters Header */
    {
        Layout *hdr = layout_create(5, 1);
        layout_label(hdr, app->strategy_scope, 0, 0);
        layout_view(hdr, app->hand_preview_view, 1, 0);
        layout_combo(hdr, app->street_filter, 2, 0);
        layout_combo(hdr, app->step_filter, 3, 0);
        layout_combo(hdr, app->board_filter, 4, 0);
        layout_hsize(hdr, 0, 240.0f);
        layout_hsize(hdr, 1, 150.0f);
        layout_hsize(hdr, 2, 130.0f);
        layout_hsize(hdr, 3, 240.0f);
        layout_hsize(hdr, 4, 220.0f);
        layout_hmargin(hdr, 0, 6.0f);
        layout_hmargin(hdr, 1, 6.0f);
        layout_hmargin(hdr, 2, 6.0f);
        layout_hmargin(hdr, 3, 6.0f);
        layout_layout(right_layout, hdr, 0, 0);
    }

    /* Strategy Container (Grid / Table / Raw Log switchable) */
    {
        Panel *container = panel_create();
        Layout *playout0 = layout_create(1, 1);
        Layout *playout1 = layout_create(1, 1);
        Layout *playout2 = layout_create(1, 1);

        tableview_OnData(app->strategy_table, listener(app, i_on_strategy_table, App));
        for (uint32_t column = 0u; column < 8u; ++column)
            tableview_add_column_text(app->strategy_table);
        tableview_header_title(app->strategy_table, 0u, "Hand");
        tableview_header_title(app->strategy_table, 1u, "Node");
        tableview_header_title(app->strategy_table, 2u, "Player");
        tableview_header_title(app->strategy_table, 3u, "—");
        tableview_header_title(app->strategy_table, 4u, "—");
        tableview_header_title(app->strategy_table, 5u, "—");
        tableview_header_title(app->strategy_table, 6u, "—");
        tableview_header_title(app->strategy_table, 7u, "EV by action");
        tableview_column_width(app->strategy_table, 0u, 118.f);
        tableview_column_width(app->strategy_table, 1u, 58.f);
        tableview_column_width(app->strategy_table, 2u, 62.f);
        tableview_column_width(app->strategy_table, 3u, 150.f);
        tableview_column_width(app->strategy_table, 4u, 150.f);
        tableview_column_width(app->strategy_table, 5u, 150.f);
        tableview_column_width(app->strategy_table, 6u, 150.f);
        tableview_column_width(app->strategy_table, 7u, 310.f);
        tableview_header_resizable(app->strategy_table, TRUE);
        tableview_header_height(app->strategy_table, 28.f);
        tableview_row_height(app->strategy_table, 28.f);
        tableview_grid(app->strategy_table, TRUE, TRUE);

        textview_editable(app->strategy_view, FALSE);
        textview_wrap(app->strategy_view, TRUE);
        textview_printf(app->strategy_view, "No strategy snapshot yet.\n");

        layout_view(playout0, app->strategy_grid_view, 0, 0);
        layout_tableview(playout1, app->strategy_table, 0, 0);
        layout_textview(playout2, app->strategy_view, 0, 0);

        panel_layout(container, playout0);
        panel_layout(container, playout1);
        panel_layout(container, playout2);
        panel_visible_layout(container, 0u);

        app->strategy_container = container;
        layout_panel(right_layout, container, 0, 1);
    }

    /* Bottom Control Bar */
    {
        Layout *ctrl = layout_create(8, 1);
        Label *lbl_sort = label_create();
        Label *lbl_view = label_create();
        Label *lbl_filt = label_create();

        label_text(lbl_sort, "Sort by:");
        label_text(lbl_view, "View:");
        label_text(lbl_filt, "Filter:");

        layout_button(ctrl, app->btn_equity_graph, 0, 0);
        layout_button(ctrl, app->btn_board_overview, 1, 0);
        layout_label(ctrl, lbl_sort, 2, 0);
        layout_combo(ctrl, app->sort_combo, 3, 0);
        layout_label(ctrl, lbl_view, 4, 0);
        layout_combo(ctrl, app->view_mode_combo, 5, 0);
        layout_label(ctrl, lbl_filt, 6, 0);
        layout_combo(ctrl, app->filter_combo, 7, 0);

        layout_hsize(ctrl, 0, 110.0f);
        layout_hsize(ctrl, 1, 120.0f);
        layout_hsize(ctrl, 2, 60.0f);
        layout_hsize(ctrl, 3, 140.0f);
        layout_hsize(ctrl, 4, 50.0f);
        layout_hsize(ctrl, 5, 140.0f);
        layout_hsize(ctrl, 6, 50.0f);
        layout_hsize(ctrl, 7, 160.0f);
        layout_hmargin(ctrl, 0, 6.0f);
        layout_hmargin(ctrl, 1, 12.0f);
        layout_hmargin(ctrl, 2, 4.0f);
        layout_hmargin(ctrl, 3, 12.0f);
        layout_hmargin(ctrl, 4, 4.0f);
        layout_hmargin(ctrl, 5, 12.0f);
        layout_hmargin(ctrl, 6, 4.0f);
        layout_layout(right_layout, ctrl, 0, 2);
    }

    /* Bottom Run Monitor & Log */
    {
        Layout *monitor = layout_create(4, 2);
        Label *status_caption = label_create();
        Label *iterations_caption = label_create();
        Label *progress_caption = label_create();
        Label *convergence_caption = label_create();
        label_text(status_caption, "STATUS");
        label_text(iterations_caption, "ITERATIONS");
        label_text(progress_caption, "PROGRESS");
        label_text(convergence_caption, "CONVERGENCE / BR");
        layout_label(monitor, status_caption, 0, 0);
        layout_label(monitor, iterations_caption, 1, 0);
        layout_label(monitor, progress_caption, 2, 0);
        layout_label(monitor, convergence_caption, 3, 0);
        layout_label(monitor, app->run_state, 0, 1);
        layout_label(monitor, app->run_progress, 1, 1);
        layout_label(monitor, app->run_fraction, 2, 1);
        layout_label(monitor, app->run_metrics, 3, 1);
        layout_hsize(monitor, 0, 180.0f);
        layout_hsize(monitor, 1, 200.0f);
        layout_hsize(monitor, 2, 150.0f);
        layout_hsize(monitor, 3, 240.0f);
        layout_hmargin(monitor, 0, 12.0f);
        layout_hmargin(monitor, 1, 12.0f);
        layout_hmargin(monitor, 2, 12.0f);
        layout_layout(right_layout, monitor, 0, 3);
    }

    textview_editable(app->status, FALSE);
    textview_wrap(app->status, TRUE);
    textview_printf(app->status, "Choose a .tree file, inspect it, then solve the spot.\n");
    layout_textview(right_layout, app->status, 0, 4);

    layout_hsize(right_layout, 0, 1040.0f);
    layout_vsize(right_layout, 0, 36.0f);
    layout_vsize(right_layout, 1, 540.0f);
    layout_vsize(right_layout, 2, 34.0f);
    layout_vsize(right_layout, 3, 44.0f);
    layout_vsize(right_layout, 4, 80.0f);
    layout_vmargin(right_layout, 0, 4.0f);
    layout_vmargin(right_layout, 1, 6.0f);
    layout_vmargin(right_layout, 2, 6.0f);
    layout_vmargin(right_layout, 3, 4.0f);
    panel_layout(right_panel, right_layout);

    /* Root 2-Column Split */
    layout_panel(root_layout, left_panel, 0, 0);
    layout_panel(root_layout, right_panel, 1, 0);
    layout_hsize(root_layout, 0, 348.0f);
    layout_hsize(root_layout, 1, 1050.0f);
    layout_hmargin(root_layout, 0, 10.0f);
    layout_margin(root_layout, 10.0f);

    panel_layout(panel, root_layout);
    return panel;
}

static App *i_create(void)
{
    App *app = heap_new0(App);
    Panel *root = panel_create();
    Layout *layout = layout_create(1, 2);
    Panel *setup;
    Panel *result;
    Layout *setup_layout = layout_create(1, 1);
    Layout *result_layout = layout_create(1, 1);
    Tabs *tabs = tabs_create((gui_pos_t)ekTABS_TOP);
    Panel *pages = panel_create();

    /* Create Fonts */
    app->card_font = font_system(11.0f, ekFBOLD);
    app->card_font_lg = font_system(13.0f, ekFBOLD);
    app->header_font = font_system(12.0f, ekFBOLD);
    app->regular_font = font_system(11.0f, 0);
    app->bold_font = font_system(11.0f, ekFBOLD);

    setup = i_setup_panel(app);
    result = i_result_panel(app);

    app->tabs = tabs;
    app->pages = pages;
    tabs_add_elem(tabs, "SETUP", NULL);
    tabs_add_elem(tabs, "RESULTS", NULL);
    tabs_OnSelect(tabs, listener(app, i_on_tab, App));
    layout_panel(setup_layout, setup, 0, 0);
    layout_panel(result_layout, result, 0, 0);
    panel_layout(pages, setup_layout);
    panel_layout(pages, result_layout);
    panel_visible_layout(pages, 0u);
    app->solve_mutex = bmutex_create();
    layout_tabs(layout, tabs, 0, 0);
    layout_panel(layout, pages, 0, 1);
    layout_vsize(layout, 1, 780);
    layout_margin(layout, 12);
    panel_layout(root, layout);
    app->window = window_create(ekWINDOW_STDRES);
    window_panel(app->window, root);
    window_title(app->window, "poker-eval Studio");
    window_origin(app->window, v2df(100, 60));
    window_client_size(app->window, s2df(1440, 920));
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
    if (*app)
    {
        mkr_model_clear(*app);
        if ((*app)->card_font) font_destroy(&(*app)->card_font);
        if ((*app)->card_font_lg) font_destroy(&(*app)->card_font_lg);
        if ((*app)->header_font) font_destroy(&(*app)->header_font);
        if ((*app)->regular_font) font_destroy(&(*app)->regular_font);
        if ((*app)->bold_font) font_destroy(&(*app)->bold_font);
        bmutex_close(&(*app)->solve_mutex);
        window_destroy(&(*app)->window);
        heap_delete(app, App);
    }
}

#include <osapp/osmain.h>
osmain(i_create, i_destroy, "", App)
