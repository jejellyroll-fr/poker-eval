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

#include <poker_eval/engine/solvers/cfr/board_canonical.h>
#include <poker_eval/engine/solvers/cfr/board_texture.h>
#include <poker_eval/solver/pe_monker.h>
#include <poker_eval/solver/pe_monker_classes.h>
#include <poker_eval/solver/pe_runtime.h>
#include "pe_analysis_model.h"
#include <poker_eval/solver/pe_solver_plan.h>
#include <poker_eval/solver/pe_solver_config.h>
#include <poker_eval/engine/solvers/cfr/mpf_tree.h>
#include <osbs/bmutex.h>
#include <osbs/bproc.h>

#include "pe_tree_editor_model.h"
#include "pe_tree_outline.h"

#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>

#include "../src/solver/domain/finite_double.h"
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

/* The report emits up to --report-rows hand rows (the Studio asks for
 * STUDIO_REPORT_ROWS).  Both this and solve_output must hold them, or the
 * table shows a slice of the run and the grid looks nearly empty. */
#define STRATEGY_TABLE_MAX_ROWS 4000u
#define STUDIO_REPORT_ROWS 4000u
/* Big enough for the report the Studio itself asks for.  It was a flat
 * 524288, which a 4000-row table outgrows as soon as the EV column carries
 * numbers instead of "pending" (~140 bytes a row, 550 KB): the capture
 * stopped mid-table and the grid lost whatever came after, silently.  Derive
 * it from the row cap so the two cannot drift apart again. */
#define STRATEGY_ROW_MAX_BYTES 320u
#define STRATEGY_CAPTURE_CAPACITY \
    (STUDIO_REPORT_ROWS * STRATEGY_ROW_MAX_BYTES + 131072u)
#define MONKER_GRID_MAX_ROWS PE_MONKER_CLASS_COUNT

/* strategy_table_add_row writes strategy_rows[i] and monker_hands[i] with the
 * same index, so the row cap must never exceed the grid array.  Raising
 * STRATEGY_TABLE_MAX_ROWS past MONKER_GRID_MAX_ROWS would overflow
 * monker_hands silently; catch it at compile time instead. */
typedef char pe_studio_row_cap_fits[
    (STRATEGY_TABLE_MAX_ROWS <= MONKER_GRID_MAX_ROWS) ? 1 : -1];
/* And the capture must hold a full report, or the grid renders a slice of it
 * with no sign that anything was lost. */
typedef char pe_studio_capture_fits[
    (STRATEGY_CAPTURE_CAPACITY >=
     STUDIO_REPORT_ROWS * STRATEGY_ROW_MAX_BYTES) ? 1 : -1];
/* "No iteration cap" ceiling for single-option stop modes (target-only,
 * manual-only).  The solver core rejects max_iterations==0 with no target,
 * so "run until target / manual stop" is encoded as this huge cap
 * (~30 000 years at 1000 iter/s).  The UI renders it as infinity. */
#define STUDIO_NO_ITER_CAP UINT64_C(1000000000000)
#define STRATEGY_TABLE_ACTIONS MPF_TREE_ACTION_MAX
#define STRATEGY_RESPONSE_ACTIONS 4u
#define MAX_DECISION_STEPS 64u
#define MAX_PLAYERS_DISPLAY 6u
#define MAX_SETUP_PLAYERS 8u

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
    /* Street this node sits on.  A tree may span preflop..river, so the
     * report's rows do NOT all belong to one street and cannot be filtered
     * against a single scope street. */
    int street;
    /* Pot facing the actor at this node, read from the report's
     * "step node=N ... pot=X" line.  The table widget used to draw a
     * hardcoded "POT: 3.0 BB / PREFLOP" whatever was selected. */
    double pot;
    int has_pot;
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
    /* Board abstraction folded into the solver's infoset key.  Exact by
     * default: the coarser levels let one sampled board answer for its whole
     * texture class, which is how a sampled solver covers a board space it
     * cannot enumerate, but the strategy they share is an average. */
    Combo *board_abstraction_combo;
    Combo *stop_mode_combo;
    Label *runtime_label;

    /* Results Views & Widgets */
    TextView *status;
    TextView *strategy_view;
    TableView *strategy_table;
    View *poker_table_view;
    View *board_matrix_view;
    View *setup_table_view;
    View *setup_board_matrix_view;
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

    /* SETUP hand state.  The game/player/board widgets above remain the
     * canonical solver inputs; these widgets only expose the live table
     * context that was previously missing from the setup screen. */
    Combo *setup_street_combo;
    Combo *setup_hero_combo;
    Edit *setup_pot_edit;
    Edit *setup_blinds_edit;
    Edit *setup_stack_edit[MAX_SETUP_PLAYERS];
    Combo *setup_action_combo[MAX_SETUP_PLAYERS];
    Layout *setup_players_layout;
    Label *setup_table_context;

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
    Button *btn_responses[STRATEGY_RESPONSE_ACTIONS];
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

    /* ANALYSIS tab. The computation lives in pe_analysis_model.c so that it
       can be tested without a window; these are only the widgets. */
    Combo *analysis_game_combo;
    Combo *analysis_players_combo;
    Edit *analysis_range_edit[PE_ANALYSIS_MAX_PLAYERS];
    Edit *analysis_board_edit;
    Edit *analysis_dead_edit;
    Edit *analysis_iterations_edit;
    TextView *analysis_equity_view;
    TextView *analysis_breakdown_view;
    Combo *icm_game_combo;
    Combo *icm_mode_combo;
    Tabs *icm_tabs;
    Panel *icm_pages;
    Layout *icm_spot_layout;
    Combo *icm_spot_game_combo;
    Combo *icm_spot_players_combo;
    Combo *icm_spot_street_combo;
    Combo *icm_spot_hero_combo;
    Edit *icm_spot_board_edit;
    Edit *icm_spot_pot_edit;
    Edit *icm_spot_blinds_edit;
    Edit *icm_spot_stack_edit[MAX_SETUP_PLAYERS];
    Combo *icm_spot_action_combo[MAX_SETUP_PLAYERS];
    View *icm_spot_table_view;
    View *icm_spot_board_matrix_view;
    Label *icm_spot_context;
    Edit *analysis_stacks_edit;
    Edit *analysis_payouts_edit;
    Edit *icm_fgs_pot_edit;
    Edit *icm_fgs_depth_edit;
    Edit *icm_fgs_win_edit;
    Edit *icm_hero_edit;
    Edit *icm_opponent_edit;
    Edit *icm_decision_win_edit;
    Edit *icm_risk_edit;
    Edit *icm_gain_edit;
    TextView *analysis_icm_view;
    View *icm_matrix_view;
    Label *icm_matrix_hint;
    Edit *icm_matrix_opponent_range_edit;
    Edit *icm_matrix_iterations_edit;
    Edit *icm_matrix_raise_edit;
    Mutex *icm_matrix_mutex;
    int icm_matrix_running;
    int icm_matrix_cancel_requested;
    uint32_t icm_matrix_done;
    uint32_t icm_matrix_total;
    uint32_t icm_matrix_failed;
    uint32_t icm_matrix_count;
    enum_game_t icm_matrix_game;
    int icm_matrix_hero;
    int icm_matrix_opponent;
    int icm_matrix_active_opponents;
    int icm_matrix_has_bet;
    double icm_matrix_pot;
    double icm_matrix_hero_stack;
    double icm_matrix_opponent_stack;
    double icm_matrix_to_call;
    char icm_matrix_board[128];
    char icm_matrix_opponent_range[512];
    char icm_matrix_stacks[512];
    char icm_matrix_payouts[512];
    long icm_matrix_iterations;
    double icm_matrix_raise_multiple;
    int icm_matrix_ready;
    double icm_matrix_equity[13][13];
    int8_t icm_matrix_action[13][13];

    /* Monker-style tree builder.  The editor is deliberately separate from
     * the loaded MKR model: it owns a small, deterministic topology model and
     * emits the JSON tree format consumed by the solver tools. */
    pe_tree_editor_t tree_editor;
    int tree_editor_ready;
    TextView *tree_editor_view;
    TextView *tree_editor_info;
    TextView *tree_editor_status;
    Combo *tree_editor_node_combo;
    Combo *tree_editor_action_combo;
    Combo *tree_editor_remove_action_combo;
    Combo *tree_editor_street_combo;
    Edit *tree_editor_size_edit;
    Label *tree_editor_selection_label;
    View *tree_editor_canvas;
    View *tree_editor_poker_view;
    Button *tree_editor_collapse_all;
    Button *tree_editor_expand_all;
    Button *tree_editor_collapse_node;
    Button *tree_editor_expand_node;
    int tree_editor_refreshing;
    int tree_outline_ready;
    pe_tree_outline_t tree_outline;

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
    /* Set when a report did not fit the capture, so the UI can say the table
     * is a slice instead of presenting it as the whole thing. */
    int strategy_capture_truncated;
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
    /* Player decision nodes past preflop (street > 0) in the loaded tree,
     * plus the per-street census.  Lane B follows tree decisions on the
     * run's street and rolls later streets out; nodes from any other
     * street are never entered — surfaced as an explicit warning. */
    uint32_t tree_street_nodes[5];
    /* The results table must follow the topology, not the last Setup
     * selection.  This is especially important when an external .mkr is
     * loaded directly: its tree may be 4-max while Setup still says 2. */
    uint32_t tree_player_count;
    /* Per-street player-node census of the loaded tree (see read_tree). */
    char tree_street_summary[128];

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
    /* Rolling capture of the solver's stdout.  It keeps the TAIL, so it has
     * to be large enough for the whole report: at 128 KB a run of a few
     * thousand rows lost most of its hand table before it was ever parsed. */
    char solve_output[4194304];
    /* Cards picked in the RESULTS board matrix.  This is a VIEW filter -- a
     * row is kept when its runout contains every selected card -- not the
     * board of the spot to solve, which lives in board_edit.  Clicking the
     * matrix used to write into board_edit, so it edited the Setup input and
     * left the already-computed report untouched. */
    char result_board_cards[64];

    /* Board abstraction the CURRENT report was produced with
     * (pe_texture_filter_level_t).  The view has to match runouts by the same
     * rule the solver keyed them with: comparing exact canonical boards
     * against a run that merged them by texture finds almost nothing. */
    int solve_board_abstraction;

    /* The solver stays alive after its report and answers "query <board>" on
     * stdin.  It has to: the infoset DESCRIPTIONS (key -> hand/board/node)
     * only exist in the process that played them -- a checkpoint stores
     * strategies, not descriptions -- so a resumed process answers a board
     * query with a fraction of the hands.  While serving, the run is
     * finished for the user even though the process is still up. */
    int solve_serving;
    /* Bytes of solver output already folded into the views, and whether the
     * serving UI has been applied for them.  While serving, the process never
     * exits, so the 10 Hz update loop would otherwise re-copy and re-parse a
     * multi-megabyte report forever -- which locks the interface up once the
     * results are on screen. */
    size_t solve_view_total;
    int solve_view_serving_applied;

    /* Reusable copy of the above for the render paths.  Owned by the app
     * rather than malloc'd per call: i_solve_update runs ten times a second
     * while a solve is live. */
    char *report_scratch;

    /* Checkpoint / resume state.  When a solver run writes a checkpoint,
     * i_solve_end stores the file path here and the next "Resume" click
     * reissues the same solve command with --resume <path>. */
    int solve_checkpoint_available;
    int solve_resume_mode;
    int solve_terminate_sent;
    char solve_checkpoint_path[1024];

    /* Resolved backend and effective thread count of the current (or last)
     * solve.  Captured at i_start_solve so the results view can show the
     * values the solver actually used, not the user's raw input. */
    int solve_has_resolved;
    int solve_resolved_threads;
    char solve_resolved_backend[64];

    /* Stop-mode state of the current (or last) solve.  stop_mode mirrors
     * the Setup combo: 0 = max iterations, 1 = exploitability target
     * (iterations act as a per-run safety cap, extended on each Resume),
     * 2 = run forever (manual stop).  stop_reason holds the solver's
     * verdict ("target" / "max_iterations" / "" when unknown). */
    uint32_t solve_stop_mode;
    uint64_t solve_run_iterations;
    double solve_run_target;
    char solve_stop_reason[32];
    char solve_stop_detail[192];
    /* Last end-of-run diagnostics line (see i_solve_end). */
    char solve_diag_line[320];
};

static void i_on_close(App *app, Event *event);
static void render_strategy_view(App *app, const char *output);
static void render_current_strategy_view(App *app);
static void i_on_result_filter(App *app, Event *event);
static void refresh_result_filters(App *app, const char *output);
static int result_line_board(const char *line, char *out, size_t capacity);

/* Warning appended wherever exploitability is shown.  With boards merged,
 * the best-response search only ever plays inside the abstraction, so the
 * figure is the exploitability of the ABSTRACT game -- it says nothing about
 * an opponent who tells the merged boards apart, and it drops toward zero as
 * the abstraction gets coarser.  Measured: "small" puts all 22100 flops in 2
 * classes, "medium" in 3, "large" in 7. */
static const char *board_abstraction_caveat(const App *app)
{
    static const char *names[] = {"", " (detailed: 366 flop classes)",
                                  " (large: 7 flop classes)",
                                  " (medium: 3 flop classes)",
                                  " (small: 2 flop classes)"};
    int level = app ? app->solve_board_abstraction : 0;
    if (level <= 0 || level > 4)
        return "";
    return names[level];
}

/* Write one command line to the live solver's stdin.  Returns 0 when it was
 * handed over.  Only meaningful while the process is serving queries. */
static int i_solve_send(App *app, const char *line)
{
    Proc *proc;
    int serving;
    uint32_t written = 0u;
    perror_t error;
    if (!app || !line || !*line)
        return -1;
    bmutex_lock(app->solve_mutex);
    proc = app->solve_proc;
    serving = app->solve_serving;
    bmutex_unlock(app->solve_mutex);
    if (!proc || !serving)
        return -1;
    if (!bproc_write(proc, (const byte_t *)line, (uint32_t)strlen(line),
                     &written, &error))
        return -1;
    return 0;
}

/* Combo index to the --board-abstraction spelling the driver takes. */
static const char *selected_board_abstraction(const App *app)
{
    static const char *names[] = {"none", "detailed", "large", "medium", "small"};
    uint32_t index = app && app->board_abstraction_combo
        ? combo_get_selected(app->board_abstraction_combo) : 0u;
    return index < 5u ? names[index] : "none";
}
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
static void i_on_analysis_icm(App *app, Event *event);
static void i_on_compute_icm_matrix(App *app, Event *event);
static void i_on_icm_matrix_input_change(App *app, Event *event);
static int i_icm_matrix_supported(const App *app);
static uint32_t i_icm_matrix_main(App *app);
static void i_icm_matrix_update(App *app);
static void i_icm_matrix_end(App *app, const uint32_t result);
static void i_on_icm_spot_state_change(App *app, Event *event);
static void i_on_icm_spot_players_select(App *app, Event *event);
static void i_on_icm_spot_street_select(App *app, Event *event);
static void i_on_draw_icm_spot_table(App *app, Event *event);
static void i_on_click_icm_spot_table(App *app, Event *event);
static void i_on_draw_icm_spot_board(App *app, Event *event);
static void i_on_click_icm_spot_board(App *app, Event *event);
static uint32_t icm_spot_players(const App *app);
static int icm_spot_street(const App *app);
static const char *icm_spot_action(const App *app, uint32_t player);
static void icm_spot_update_context(App *app);
static void sync_icm_from_spot(App *app);
static void trim_text(char *text);
static int read_tree(App *app, const char *path, pe_monker_tree_header_t *header,
                     pe_monker_combo_layout_t *layout);

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
    if (errno != 0 || end == text || *end != '\0' || !pe_finite_double(parsed) || parsed < 0.0)
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

/* Same names in upper case, for status headings.  street_name() itself must
 * stay lower case: it is passed verbatim to the driver's --street flag. */
static const char *street_name_upper(int street)
{
    static const char *names[] = {"PREFLOP", "FLOP", "TURN", "RIVER"};
    return street >= 0 && street < 4 ? names[street] : "UNKNOWN";
}

static const char *game_name(enum_game_t game)
{
    if (game == game_holdem) return "holdem";
    if (game == game_sdholdem) return "shortdeck";
    if (game == game_omaha) return "plo4";
    if (game == game_omaha5) return "plo5";
    if (game == game_omaha6) return "plo6";
    return "unknown";
}

static uint32_t game_index(enum_game_t game)
{
    if (game == game_holdem) return 0u;
    if (game == game_sdholdem) return 1u;
    if (game == game_omaha) return 2u;
    if (game == game_omaha5) return 3u;
    return 4u;
}

static enum_game_t game_from_index(uint32_t index)
{
    static const enum_game_t games[] = {game_holdem, game_sdholdem,
                                        game_omaha, game_omaha5, game_omaha6};
    return index < sizeof(games) / sizeof(games[0]) ? games[index] : game_holdem;
}

static uint8_t hole_cards_from_game(enum_game_t game)
{
    return game == game_holdem || game == game_sdholdem ? 2u : game == game_omaha ? 4u :
           game == game_omaha5 ? 5u : game == game_omaha6 ? 6u : 0u;
}

static void infer_game_from_path(App *app, const char *path)
{
    if (!app || !path || !app->game_combo) return;
    if (strstr(path, "plo6") != NULL) combo_selected(app->game_combo, 4u);
    else if (strstr(path, "plo5") != NULL) combo_selected(app->game_combo, 3u);
    else if (strstr(path, "plo") != NULL) combo_selected(app->game_combo, 2u);
    else if (strstr(path, "short") != NULL || strstr(path, "sixplus") != NULL)
        combo_selected(app->game_combo, 1u);
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

static void algorithm_axes_label(pe_algorithm_preset_t algorithm,
                                 char *out, size_t capacity)
{
    pe_algorithm_config_t axes;
    if (!out || capacity == 0u)
        return;
    memset(&axes, 0, sizeof(axes));
    axes.preset = algorithm;
    if (pe_preset_expand(algorithm, &axes) != 0)
    {
        snprintf(out, capacity, "unavailable");
        return;
    }
    snprintf(out, capacity, "traversal=%s regret=%s averaging=%s",
             pe_traversal_name(axes.traversal),
             pe_regret_name(axes.regret),
             pe_averaging_name(axes.averaging));
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
    if (!app)
        return;
    if (app->status)
    {
        textview_clear(app->status);
        textview_writef(app->status, buffer);
    }
    if (app->tree_editor_status)
    {
        textview_clear(app->tree_editor_status);
        textview_writef(app->tree_editor_status, buffer);
    }
}

static int tree_editor_selected_node(const App *app)
{
    const char *text;
    uint32_t selected;
    int node = -1;
    if (!app || !app->tree_editor_node_combo ||
        combo_count(app->tree_editor_node_combo) == 0u)
        return -1;
    selected = combo_get_selected(app->tree_editor_node_combo);
    if (selected >= combo_count(app->tree_editor_node_combo))
        return -1;
    text = combo_get_text(app->tree_editor_node_combo, selected);
    if (text)
        (void)sscanf(text, "[%d]", &node);
    return node;
}

static mpf_tree_action_type_t tree_editor_selected_action(const App *app)
{
    uint32_t selected = app && app->tree_editor_action_combo
        && combo_count(app->tree_editor_action_combo) > 0u
        ? combo_get_selected(app->tree_editor_action_combo) : 0u;
    return selected <= (uint32_t)MPF_TREE_ACTION_CHANCE
        ? (mpf_tree_action_type_t)selected : MPF_TREE_ACTION_FOLD;
}

static void tree_editor_refresh(App *app, int selected_node)
{
    char label[256];
    int outline_row;
    if (!app || !app->tree_editor_ready || app->tree_editor_refreshing)
        return;
    /* combo_clear/combo_selected can synchronously emit OnSelect on some
     * NAppGUI backends.  Keep refresh transactional so a UI refresh cannot
     * recursively refresh itself and overflow the stack. */
    app->tree_editor_refreshing = 1;
    if (!app->tree_outline_ready)
    {
        pe_tree_outline_config_t config =
            pe_tree_outline_default_config(app->tree_editor.player_count);
        pe_tree_outline_init(&app->tree_outline, &config);
        app->tree_outline_ready = 1;
    }
    else
    {
        app->tree_outline.config.player_count = app->tree_editor.player_count;
        if (app->tree_outline.config.player_count < 1)
            app->tree_outline.config.player_count = 1;
        if (app->tree_outline.config.player_count > PE_TREE_OUTLINE_MAX_SEATS)
            app->tree_outline.config.player_count = PE_TREE_OUTLINE_MAX_SEATS;
    }
    (void)pe_tree_outline_build(&app->tree_outline, &app->tree_editor);
    combo_clear(app->tree_editor_node_combo);
    for (int node = 0; node < app->tree_editor.node_count; ++node)
    {
        const pe_tree_editor_node_t *entry = &app->tree_editor.nodes[node];
        if (pe_tree_editor_reachable(&app->tree_editor, node))
        {
            snprintf(label, sizeof(label), "[%d] %s | %s%s", node, entry->id,
                     pe_tree_editor_node_type_name(entry->type),
                     entry->type == MPF_TREE_NODE_PLAYER ? " | P" : "");
            if (entry->type == MPF_TREE_NODE_PLAYER)
                snprintf(label + strlen(label), sizeof(label) - strlen(label),
                         "%d", entry->acting_player + 1);
            combo_add_elem(app->tree_editor_node_combo, label, NULL);
        }
    }
    if (selected_node >= 0)
    {
        for (uint32_t item = 0u; item < combo_count(app->tree_editor_node_combo); ++item)
        {
            const char *item_text = combo_get_text(app->tree_editor_node_combo, item);
            int item_node = -1;
            if (item_text)
                (void)sscanf(item_text, "[%d]", &item_node);
            if (item_node == selected_node)
            {
                combo_selected(app->tree_editor_node_combo, item);
                break;
            }
        }
    }
    if (combo_count(app->tree_editor_node_combo) > 0u &&
        combo_get_selected(app->tree_editor_node_combo) >= combo_count(app->tree_editor_node_combo))
        combo_selected(app->tree_editor_node_combo, 0u);
    combo_clear(app->tree_editor_remove_action_combo);
    {
        int node = tree_editor_selected_node(app);
        if (node >= 0 && node < app->tree_editor.node_count)
        {
            const pe_tree_editor_node_t *entry = &app->tree_editor.nodes[node];
            for (int action = 0; action < entry->action_count; ++action)
            {
                char action_text[128];
                snprintf(action_text, sizeof(action_text), "%d: %s", action + 1,
                         pe_tree_editor_action_name(entry->actions[action].type));
                combo_add_elem(app->tree_editor_remove_action_combo, action_text, NULL);
            }
        }
    }
    {
        int node = tree_editor_selected_node(app);
        if (node >= 0 && node < app->tree_editor.node_count)
        {
            const pe_tree_editor_node_t *entry = &app->tree_editor.nodes[node];
            snprintf(label, sizeof(label), "Selected node %d | %s | %s | actions %d",
                     node, entry->id, pe_tree_editor_node_type_name(entry->type),
                     entry->action_count);
        }
        else
        {
            snprintf(label, sizeof(label), "No node selected");
        }
        label_text(app->tree_editor_selection_label, label);
    }
    outline_row = pe_tree_outline_row_of_node(&app->tree_outline,
                                               tree_editor_selected_node(app));
    if (app->tree_editor_info)
    {
        textview_clear(app->tree_editor_info);
        if (outline_row >= 0)
        {
            const pe_tree_outline_row_t *entry =
                &app->tree_outline.rows[outline_row];
            textview_printf(app->tree_editor_info,
                            "Path: %s\nStreet: %s | acting: %s\n"
                            "Pot: %.2f | SPR: %.2f | To call: %.2f\n",
                            entry->path_key[0] ? entry->path_key : "(root)",
                            street_name(entry->street),
                            entry->acting_player >= 0 ? "player" : "terminal",
                            entry->chips.pot, entry->chips.spr,
                            entry->chips.to_call);
            for (int seat = 0; seat < app->tree_outline.config.player_count; ++seat)
                textview_printf(app->tree_editor_info,
                                "P%d: %.2f behind | %.2f committed%s\n",
                                seat + 1, entry->chips.stacks[seat],
                                entry->chips.committed[seat],
                                entry->chips.all_in ? " | all-in" : "");
        }
        else
        {
            textview_printf(app->tree_editor_info,
                            "Selected node is hidden behind a collapsed ancestor.\n"
                            "Use Expand all or select an open branch.\n");
        }
    }
    if (app->tree_editor_canvas)
        view_update(app->tree_editor_canvas);
    app->tree_editor_refreshing = 0;
}

static int tree_editor_layout_nodes(const pe_tree_editor_t *editor,
                                    int depth[PE_TREE_EDITOR_MAX_NODES],
                                    int order[PE_TREE_EDITOR_MAX_NODES],
                                    int *max_depth, int *max_rows)
{
    int queue[PE_TREE_EDITOR_MAX_NODES];
    int rows[PE_TREE_EDITOR_MAX_NODES] = {0};
    int head = 0;
    int tail = 0;
    if (!editor || !depth || !order || !max_depth || !max_rows ||
        editor->root_index < 0 || editor->root_index >= editor->node_count)
        return 0;
    for (int i = 0; i < PE_TREE_EDITOR_MAX_NODES; ++i)
    {
        depth[i] = -1;
        order[i] = -1;
    }
    depth[editor->root_index] = 0;
    queue[tail++] = editor->root_index;
    *max_depth = 0;
    *max_rows = 1;
    while (head < tail)
    {
        int node_index = queue[head++];
        const pe_tree_editor_node_t *node = &editor->nodes[node_index];
        int node_depth = depth[node_index];
        order[node_index] = rows[node_depth]++;
        if (node_depth > *max_depth)
            *max_depth = node_depth;
        if (rows[node_depth] > *max_rows)
            *max_rows = rows[node_depth];
        for (int action = 0; action < node->action_count; ++action)
        {
            int next = node->actions[action].next_index;
            if (next >= 0 && next < editor->node_count && depth[next] < 0 &&
                tail < PE_TREE_EDITOR_MAX_NODES)
            {
                depth[next] = node_depth + 1;
                queue[tail++] = next;
            }
        }
    }
    return tail;
}

/*
 * Node geometry, in one place.
 *
 * The draw handler and the click handler have to agree on where a node is;
 * computing that twice is how a canvas ends up selecting the box next to the
 * one under the cursor. Both call this.
 */
typedef struct
{
    real32_t box_w;
    real32_t box_h;
    real32_t col_gap;
    real32_t row_gap;
} pe_tree_canvas_metrics_t;

static pe_tree_canvas_metrics_t tree_editor_metrics(real32_t width,
                                                    real32_t height,
                                                    int max_depth,
                                                    int max_rows)
{
    pe_tree_canvas_metrics_t m;
    m.box_w = 112.0f;
    m.box_h = 32.0f;
    m.col_gap = max_depth > 0
        ? (width - 32.0f - m.box_w) / (real32_t)max_depth - m.box_w
        : 0.0f;
    if (m.col_gap < 10.0f)
        m.col_gap = 10.0f;
    m.row_gap = max_rows > 1
        ? (height - 52.0f - m.box_h) / (real32_t)(max_rows - 1)
        : 0.0f;
    if (m.row_gap < 12.0f)
        m.row_gap = 12.0f;
    return m;
}

static int tree_editor_outline_row_at(const App *app, real32_t x,
                                      real32_t y)
{
    int row;
    if (!app || y < 38.0f)
        return -1;
    row = (int)((y - 38.0f) / 30.0f);
    if (row < 0 || row >= app->tree_outline.row_count)
        return -1;
    (void)x;
    return row;
}

static color_t tree_editor_outline_color(const pe_tree_outline_row_t *row)
{
    if (!row)
        return color_rgb(65, 72, 80);
    if (row->revisit)
        return color_rgb(105, 75, 30);
    if (row->type == MPF_TREE_NODE_PLAYER)
        return color_rgb(32, 91, 145);
    if (row->type == MPF_TREE_NODE_CHANCE)
        return color_rgb(130, 85, 25);
    return color_rgb(65, 70, 78);
}

static void i_on_draw_tree_outline(App *app, Event *event)
{
    const EvDraw *params = event_params(event, EvDraw);
    DCtx *ctx = params->ctx;
    int selected = tree_editor_selected_node(app);
    real32_t width = params->width;
    real32_t height = params->height;

    draw_fill_color(ctx, color_rgb(246, 246, 246));
    draw_rect(ctx, ekFILL, 0, 0, width, height);
    if (!app || !app->tree_editor_ready || !app->tree_outline_ready ||
        app->tree_outline.row_count == 0)
    {
        draw_fill_color(ctx, color_rgb(246, 246, 246));
        draw_rect(ctx, ekFILL, 0, 0, width, height);
        if (app && app->regular_font)
            draw_font(ctx, app->regular_font);
        draw_text_color(ctx, color_rgb(80, 80, 80));
        draw_text_align(ctx, ekCENTER, ekCENTER);
        draw_text(ctx, "Create or import a tree", width * 0.5f,
                  height * 0.5f);
        return;
    }

    draw_fill_color(ctx, color_rgb(226, 226, 226));
    draw_rect(ctx, ekFILL, 0, 0, width, 34.0f);
    if (app->bold_font)
        draw_font(ctx, app->bold_font);
    draw_text_color(ctx, color_rgb(45, 45, 45));
    draw_text_align(ctx, ekLEFT, ekCENTER);
    draw_text(ctx, "ACTION TREE", 14.0f, 17.0f);
    if (app->regular_font)
        draw_font(ctx, app->regular_font);
    draw_text_color(ctx, color_rgb(100, 100, 100));
    draw_text_align(ctx, ekRIGHT, ekCENTER);
    draw_text(ctx, "click a row to select  |  click arrow to fold",
              width - 14.0f, 17.0f);

    for (int index = 0; index < app->tree_outline.row_count; ++index)
    {
        const pe_tree_outline_row_t *row = &app->tree_outline.rows[index];
        const pe_tree_editor_node_t *node =
            &app->tree_editor.nodes[row->node_index];
        real32_t y = 38.0f + (real32_t)index * 30.0f;
        real32_t x = 12.0f + (real32_t)row->depth * 25.0f;
        real32_t box_x = x + 18.0f;
        char label[192];
        char chip_text[96];
        char initial[2];
        color_t fill = tree_editor_outline_color(row);

        if (y + 28.0f < 34.0f || y > height)
            continue;
        if (row->node_index == selected)
        {
            draw_fill_color(ctx, color_rgb(211, 228, 245));
            draw_rect(ctx, ekFILL, 0, y - 2.0f, width, 30.0f);
        }
        if (row->parent_row >= 0)
        {
            draw_line_color(ctx, color_rgb(190, 190, 190));
            draw_line_width(ctx, 1.0f);
            draw_line(ctx, x - 8.0f, y + 13.0f, x + 12.0f, y + 13.0f);
        }
        if (row->child_count > 0)
        {
            draw_text_color(ctx, color_rgb(100, 100, 100));
            draw_text_align(ctx, ekCENTER, ekCENTER);
            draw_text(ctx, row->expanded ? "[-]" : "[+]", x, y + 13.0f);
        }
        initial[0] = row->type == MPF_TREE_NODE_PLAYER ? 'P' :
                    row->type == MPF_TREE_NODE_CHANCE ? 'C' :
                    row->label[0] == 'F' ? 'F' :
                    row->label[0] == 'C' ? 'C' :
                    row->label[0] == 'R' ? 'R' : 'T';
        initial[1] = '\0';
        draw_fill_color(ctx, fill);
        draw_rndrect(ctx, ekFILL, box_x, y + 2.0f, 25.0f, 25.0f, 3.0f);
        draw_text_color(ctx, color_rgb(255, 255, 255));
        draw_text_align(ctx, ekCENTER, ekCENTER);
        draw_text(ctx, initial, box_x + 12.5f, y + 14.5f);

        if (row->label[0] != '\0')
            snprintf(label, sizeof(label), "%s  %s", row->label, node->id);
        else
            snprintf(label, sizeof(label), "Root  %s", node->id);
        if (row->type == MPF_TREE_NODE_PLAYER)
            snprintf(label + strlen(label), sizeof(label) - strlen(label),
                     "  P%d", row->acting_player + 1);
        else if (row->revisit)
            snprintf(label + strlen(label), sizeof(label) - strlen(label),
                     "  (already shown)");
        snprintf(chip_text, sizeof(chip_text), "pot %.2f  SPR %.2f",
                 row->chips.pot, row->chips.spr);
        draw_text_color(ctx, color_rgb(35, 35, 35));
        draw_text_align(ctx, ekLEFT, ekCENTER);
        draw_text(ctx, label, box_x + 34.0f, y + 10.0f);
        draw_text_color(ctx, color_rgb(105, 105, 105));
        draw_text(ctx, chip_text, box_x + 34.0f, y + 22.0f);
    }
}

static void i_on_click_tree_outline(App *app, Event *event)
{
    const EvMouse *mouse = event_params(event, EvMouse);
    int row;
    if (!app || !app->tree_editor_ready || !app->tree_outline_ready)
        return;
    row = tree_editor_outline_row_at(app, mouse->x, mouse->y);
    if (row < 0)
        return;
    if (mouse->x < 55.0f + (real32_t)app->tree_outline.rows[row].depth * 25.0f &&
        app->tree_outline.rows[row].child_count > 0)
    {
        (void)pe_tree_outline_toggle(&app->tree_outline, row);
        tree_editor_refresh(app, -1);
    }
    else
    {
        tree_editor_refresh(app, app->tree_outline.rows[row].node_index);
    }
}

static void i_on_draw_tree_poker(App *app, Event *event)
{
    const EvDraw *params = event_params(event, EvDraw);
    DCtx *ctx = params->ctx;
    real32_t width = params->width;
    real32_t height = params->height;
    int node = tree_editor_selected_node(app);
    int row = app && app->tree_outline_ready
        ? pe_tree_outline_row_of_node(&app->tree_outline, node) : -1;
    double pot = row >= 0 ? app->tree_outline.rows[row].chips.pot : 1.5;
    double stack0 = row >= 0 ? app->tree_outline.rows[row].chips.stacks[0] : 100.0;
    double stack1 = row >= 0 ? app->tree_outline.rows[row].chips.stacks[1] : 100.0;
    char text[64];

    draw_fill_color(ctx, color_rgb(239, 239, 239));
    draw_rect(ctx, ekFILL, 0, 0, width, height);
    draw_fill_color(ctx, color_rgb(66, 28, 30));
    draw_rndrect(ctx, ekFILL, 18.0f, 20.0f, width - 36.0f, height - 40.0f,
                 34.0f);
    draw_fill_color(ctx, color_rgb(26, 129, 37));
    draw_rndrect(ctx, ekFILL, 32.0f, 32.0f, width - 64.0f, height - 64.0f,
                 26.0f);
    draw_line_color(ctx, color_rgb(74, 170, 80));
    draw_line_width(ctx, 1.0f);
    draw_rndrect(ctx, ekSTROKE, 32.0f, 32.0f, width - 64.0f, height - 64.0f,
                 26.0f);
    if (app && app->bold_font)
        draw_font(ctx, app->bold_font);
    draw_text_color(ctx, color_rgb(255, 255, 255));
    draw_text_align(ctx, ekCENTER, ekCENTER);
    snprintf(text, sizeof(text), "POT %.2f", pot);
    draw_text(ctx, text, width * 0.5f, height * 0.5f);
    if (app && app->regular_font)
        draw_font(ctx, app->regular_font);
    snprintf(text, sizeof(text), "P1  %.2f", stack0);
    draw_text(ctx, text, width * 0.5f, 54.0f);
    snprintf(text, sizeof(text), "P2  %.2f", stack1);
    draw_text(ctx, text, width * 0.5f, height - 54.0f);
}

static real32_t tree_editor_node_x(const pe_tree_canvas_metrics_t *m, int depth)
{
    return 16.0f + (real32_t)depth * (m->box_w + m->col_gap);
}

static real32_t tree_editor_node_y(const pe_tree_canvas_metrics_t *m, int order)
{
    return 26.0f + (real32_t)order * m->row_gap;
}

static void i_on_draw_tree_editor(App *app, Event *event)
{
    const EvDraw *params = event_params(event, EvDraw);
    DCtx *ctx = params->ctx;
    int depth[PE_TREE_EDITOR_MAX_NODES];
    int order[PE_TREE_EDITOR_MAX_NODES];
    int max_depth = 0;
    int max_rows = 1;
    real32_t width = params->width;
    real32_t height = params->height;
    pe_tree_canvas_metrics_t metrics;
    real32_t box_w;
    real32_t box_h;
    int selected = tree_editor_selected_node(app);

    draw_fill_color(ctx, color_rgb(18, 22, 27));
    draw_rect(ctx, ekFILL, 0, 0, width, height);
    if (!app || !app->tree_editor_ready ||
        tree_editor_layout_nodes(&app->tree_editor, depth, order,
                                 &max_depth, &max_rows) <= 0)
    {
        if (app && app->regular_font)
            draw_font(ctx, app->regular_font);
        draw_text_color(ctx, color_rgb(150, 160, 170));
        draw_text_align(ctx, ekCENTER, ekCENTER);
        draw_text(ctx, "Create or import a tree to see its action map.",
                  width * 0.5f, height * 0.5f);
        return;
    }

    metrics = tree_editor_metrics(width, height, max_depth, max_rows);
    box_w = metrics.box_w;
    box_h = metrics.box_h;

    /* Draw connectors first, so node cards sit above the action lines. */
    draw_line_width(ctx, 1.0f);
    for (int node_index = 0; node_index < app->tree_editor.node_count; ++node_index)
    {
        const pe_tree_editor_node_t *node = &app->tree_editor.nodes[node_index];
        real32_t x0, y0;
        if (depth[node_index] < 0)
            continue;
        x0 = tree_editor_node_x(&metrics, depth[node_index]);
        y0 = tree_editor_node_y(&metrics, order[node_index]);
        for (int action = 0; action < node->action_count; ++action)
        {
            int next = node->actions[action].next_index;
            char action_label[64];
            real32_t x1, y1;
            if (next < 0 || next >= app->tree_editor.node_count || depth[next] < 0)
                continue;
            x1 = tree_editor_node_x(&metrics, depth[next]);
            y1 = tree_editor_node_y(&metrics, order[next]);
            draw_line_color(ctx, color_rgb(95, 105, 115));
            draw_line(ctx, x0 + box_w, y0 + box_h * 0.5f, x1, y1 + box_h * 0.5f);
            if (node->actions[action].type == MPF_TREE_ACTION_RAISE &&
                node->actions[action].size_index >= 0 &&
                node->actions[action].size_index < node->bet_size_count)
                snprintf(action_label, sizeof(action_label), "%.2gx pot",
                         node->bet_sizes[node->actions[action].size_index]);
            else
                snprintf(action_label, sizeof(action_label), "%s",
                         pe_tree_editor_action_name(node->actions[action].type));
            if (app->regular_font)
                draw_font(ctx, app->regular_font);
            draw_text_color(ctx, color_rgb(180, 190, 200));
            draw_text_align(ctx, ekCENTER, ekCENTER);
            draw_text(ctx, action_label, (x0 + box_w + x1) * 0.5f,
                      (y0 + y1 + box_h) * 0.5f - 8.0f);
        }
    }

    for (int node_index = 0; node_index < app->tree_editor.node_count; ++node_index)
    {
        const pe_tree_editor_node_t *node = &app->tree_editor.nodes[node_index];
        real32_t x, y;
        char node_label[96];
        if (depth[node_index] < 0)
            continue;
        x = tree_editor_node_x(&metrics, depth[node_index]);
        y = tree_editor_node_y(&metrics, order[node_index]);
        if (node_index == selected)
        {
            draw_fill_color(ctx, color_rgb(37, 99, 235));
            draw_line_color(ctx, color_rgb(147, 197, 253));
            draw_line_width(ctx, 2.0f);
        }
        else if (node->type == MPF_TREE_NODE_PLAYER)
        {
            draw_fill_color(ctx, color_rgb(30, 73, 115));
            draw_line_color(ctx, color_rgb(76, 150, 210));
            draw_line_width(ctx, 1.0f);
        }
        else if (node->type == MPF_TREE_NODE_CHANCE)
        {
            draw_fill_color(ctx, color_rgb(112, 77, 24));
            draw_line_color(ctx, color_rgb(235, 180, 70));
            draw_line_width(ctx, 1.0f);
        }
        else
        {
            draw_fill_color(ctx, color_rgb(55, 62, 70));
            draw_line_color(ctx, color_rgb(120, 130, 140));
            draw_line_width(ctx, 1.0f);
        }
        draw_rndrect(ctx, ekFILL, x, y, box_w, box_h, 5.0f);
        draw_rndrect(ctx, ekSTROKE, x, y, box_w, box_h, 5.0f);
        snprintf(node_label, sizeof(node_label), "%s  %s%s", node->id,
                 pe_tree_editor_node_type_name(node->type),
                 node->type == MPF_TREE_NODE_PLAYER ? " P" : "");
        if (node->type == MPF_TREE_NODE_PLAYER)
            snprintf(node_label + strlen(node_label),
                     sizeof(node_label) - strlen(node_label), "%d",
                     node->acting_player + 1);
        if (app->regular_font)
            draw_font(ctx, app->regular_font);
        draw_text_color(ctx, color_rgb(245, 247, 250));
        draw_text_align(ctx, ekCENTER, ekCENTER);
        draw_text(ctx, node_label, x + box_w * 0.5f, y + box_h * 0.5f);
    }
}

static int tree_editor_import_tree(App *app, const mpf_tree_def_t *tree)
{
    if (!app || !tree || !app->tree_editor_ready ||
        !pe_tree_editor_import(&app->tree_editor, tree))
        return 0;
    app->tree_outline_ready = 0;
    tree_editor_refresh(app, tree->root_index);
    return 1;
}


/*
 * Click a node to select it.
 *
 * Selecting through the NODE dropdown alone meant scrolling a list to reach a
 * box already visible on the canvas; on any tree deeper than the demo that is
 * the slowest part of building one. The dropdown stays -- it is how a node is
 * named -- but the canvas is now the fast path, and the two stay in sync
 * because both write through tree_editor_refresh.
 */
static void i_on_click_tree_editor(App *app, Event *event)
{
    const EvMouse *mouse = event_params(event, EvMouse);
    int depth[PE_TREE_EDITOR_MAX_NODES];
    int order[PE_TREE_EDITOR_MAX_NODES];
    int max_depth = 0;
    int max_rows = 1;
    pe_tree_canvas_metrics_t metrics;
    real32_t width;
    real32_t height;
    int node_index;

    if (app == NULL || !app->tree_editor_ready ||
        app->tree_editor_canvas == NULL)
        return;
    if (tree_editor_layout_nodes(&app->tree_editor, depth, order,
                                 &max_depth, &max_rows) <= 0)
        return;
    {
        S2Df size;
        view_get_size(app->tree_editor_canvas, &size);
        width = size.width;
        height = size.height;
    }
    metrics = tree_editor_metrics(width, height, max_depth, max_rows);
    for (node_index = 0; node_index < app->tree_editor.node_count; ++node_index)
    {
        real32_t x;
        real32_t y;
        if (depth[node_index] < 0)
            continue;
        x = tree_editor_node_x(&metrics, depth[node_index]);
        y = tree_editor_node_y(&metrics, order[node_index]);
        if (mouse->x >= x && mouse->x <= x + metrics.box_w &&
            mouse->y >= y && mouse->y <= y + metrics.box_h)
        {
            tree_editor_refresh(app, node_index);
            return;
        }
    }
}

/*
 * One click per standard size, the way a tree actually gets built.
 *
 * Typing "0.75" into a field to add a three-quarter-pot raise is a step that
 * exists only because the field does; these are the sizes a betting tree is
 * made of. The size field stays for anything non-standard.
 */
static void i_tree_editor_add_sized(App *app, mpf_tree_action_type_t type,
                                    double size)
{
    int node = tree_editor_selected_node(app);
    int child = -1;

    if (app == NULL || !app->tree_editor_ready)
    {
        status(app, "TREE BUILDER\nCreate or import a tree before adding an action.");
        return;
    }
    if (!pe_tree_editor_add_action(&app->tree_editor, node, type, size, &child))
    {
        status(app, "TREE BUILDER\nCould not add this action. Player nodes accept "
               "up to %d actions; chance and terminal nodes are not editable.",
               PE_TREE_EDITOR_MAX_ACTIONS);
        return;
    }
    if (type == MPF_TREE_ACTION_RAISE)
        status(app, "TREE BUILDER\nAdded a %.2gx pot raise on node %d -> node %d.",
               size, node, child);
    else
        status(app, "TREE BUILDER\nAdded %s on node %d -> node %d.",
               pe_tree_editor_action_name(type), node, child);
    tree_editor_refresh(app, child >= 0 ? child : node);
}

static void i_on_tree_quick_fold(App *app, Event *event)
{
    unref(event);
    i_tree_editor_add_sized(app, MPF_TREE_ACTION_FOLD, 0.0);
}

static void i_on_tree_quick_call(App *app, Event *event)
{
    unref(event);
    i_tree_editor_add_sized(app, MPF_TREE_ACTION_CALL, 0.0);
}

static void i_on_tree_quick_25(App *app, Event *event)
{
    unref(event);
    i_tree_editor_add_sized(app, MPF_TREE_ACTION_RAISE, 0.25);
}

static void i_on_tree_quick_50(App *app, Event *event)
{
    unref(event);
    i_tree_editor_add_sized(app, MPF_TREE_ACTION_RAISE, 0.5);
}

static void i_on_tree_quick_75(App *app, Event *event)
{
    unref(event);
    i_tree_editor_add_sized(app, MPF_TREE_ACTION_RAISE, 0.75);
}

static void i_on_tree_quick_pot(App *app, Event *event)
{
    unref(event);
    i_tree_editor_add_sized(app, MPF_TREE_ACTION_RAISE, 1.0);
}

static void i_on_tree_quick_overbet(App *app, Event *event)
{
    unref(event);
    i_tree_editor_add_sized(app, MPF_TREE_ACTION_RAISE, 2.0);
}

static void i_on_tree_editor_new(App *app, Event *event)
{
    uint32_t street = app && app->tree_editor_street_combo
        ? combo_get_selected(app->tree_editor_street_combo) : 0u;
    if (app)
    {
        pe_tree_editor_init(&app->tree_editor, (int)selected_players(app),
                            (mpf_street_t)street);
        app->tree_editor_ready = 1;
        app->tree_outline_ready = 0;
        edit_text(app->tree_edit, "poker_eval_tree.json");
        tree_editor_refresh(app, app->tree_editor.root_index);
        status(app, "TREE BUILDER READY\nNew %s tree | %u players\n\n"
               "Select a node, choose an action and press Add action.\n"
               "Raise sizes are pot multiples (0.5 = half pot, 1.0 = pot).",
               street_name((int)street), selected_players(app));
    }
    unref(event);
}

static void i_on_tree_editor_node_select(App *app, Event *event)
{
    if (app && app->tree_editor_ready && !app->tree_editor_refreshing)
    {
        int node = tree_editor_selected_node(app);
        if (app->tree_outline_ready &&
            pe_tree_outline_row_of_node(&app->tree_outline, node) < 0)
            (void)pe_tree_outline_reveal(&app->tree_outline,
                                         &app->tree_editor, node);
        tree_editor_refresh(app, node);
    }
    unref(event);
}

static void i_on_tree_editor_collapse_node(App *app, Event *event)
{
    int node = tree_editor_selected_node(app);
    int row;
    if (!app || !app->tree_editor_ready || !app->tree_outline_ready)
    {
        status(app, "TREE BUILDER\nCreate a tree before folding a node.");
        unref(event);
        return;
    }
    row = pe_tree_outline_row_of_node(&app->tree_outline, node);
    if (row < 0 || !pe_tree_outline_set_expanded(&app->tree_outline, row, 0))
    {
        status(app, "TREE BUILDER\nThe selected node is a leaf or is hidden.");
        unref(event);
        return;
    }
    tree_editor_refresh(app, node);
    unref(event);
}

static void i_on_tree_editor_expand_node(App *app, Event *event)
{
    int node = tree_editor_selected_node(app);
    int row;
    if (!app || !app->tree_editor_ready || !app->tree_outline_ready)
    {
        status(app, "TREE BUILDER\nCreate a tree before expanding a node.");
        unref(event);
        return;
    }
    row = pe_tree_outline_row_of_node(&app->tree_outline, node);
    if (row < 0 || !pe_tree_outline_set_expanded(&app->tree_outline, row, 1))
    {
        status(app, "TREE BUILDER\nThe selected node is a leaf or is hidden.");
        unref(event);
        return;
    }
    tree_editor_refresh(app, node);
    unref(event);
}

static void i_on_tree_editor_collapse_all(App *app, Event *event)
{
    if (app && app->tree_editor_ready && app->tree_outline_ready)
    {
        pe_tree_outline_collapse_all(&app->tree_outline, &app->tree_editor);
        tree_editor_refresh(app, -1);
    }
    unref(event);
}

static void i_on_tree_editor_expand_all(App *app, Event *event)
{
    if (app && app->tree_editor_ready && app->tree_outline_ready)
    {
        pe_tree_outline_expand_all(&app->tree_outline);
        tree_editor_refresh(app, -1);
    }
    unref(event);
}

static void i_on_tree_editor_add(App *app, Event *event)
{
    int node = tree_editor_selected_node(app);
    int child = -1;
    double size = 0.0;
    mpf_tree_action_type_t type = tree_editor_selected_action(app);
    const char *size_text = app && app->tree_editor_size_edit
        ? edit_get_text(app->tree_editor_size_edit) : NULL;
    if (!app || !app->tree_editor_ready)
    {
        status(app, "TREE BUILDER\nCreate or import a tree before adding an action.");
        unref(event);
        return;
    }
    if (type == MPF_TREE_ACTION_RAISE &&
        (!size_text || !*size_text || parse_ui_target(size_text, &size) != 0 || size <= 0.0))
    {
        status(app, "TREE BUILDER\nRaise size must be a positive pot multiple.");
        unref(event);
        return;
    }
    if (!pe_tree_editor_add_action(&app->tree_editor, node, type, size, &child))
    {
        status(app, "TREE BUILDER\nCould not add this action. Player nodes accept up to %d actions; chance and terminal nodes are not editable.",
               PE_TREE_EDITOR_MAX_ACTIONS);
        unref(event);
        return;
    }
    tree_editor_refresh(app, child);
    status(app, "TREE BUILDER\nAdded %s from node %d -> node %d.\n"
           "Child player nodes start with Fold and Call / Check.",
           pe_tree_editor_action_name(type), node, child);
    unref(event);
}

static void i_on_tree_editor_remove(App *app, Event *event)
{
    int node = tree_editor_selected_node(app);
    uint32_t action = app && app->tree_editor_remove_action_combo
        ? combo_get_selected(app->tree_editor_remove_action_combo) : 0u;
    if (!app || !app->tree_editor_ready ||
        !pe_tree_editor_remove_action(&app->tree_editor, node, (int)action))
    {
        status(app, "TREE BUILDER\nA player node must keep at least one action.");
        unref(event);
        return;
    }
    tree_editor_refresh(app, node);
    status(app, "TREE BUILDER\nRemoved action %u from node %d.\n"
           "The detached child remains in the edit history but is no longer reachable from the root.",
           action + 1u, node);
    unref(event);
}

static int tree_editor_save_path(App *app, char *path, size_t capacity)
{
    const char *configured = app && app->tree_edit ? edit_get_text(app->tree_edit) : NULL;
    size_t length;
    if (!path || capacity == 0u)
        return 0;
    if (!configured || !*configured || strcmp(configured, "/path/to/spot.tree") == 0)
        configured = "poker_eval_tree.json";
    length = strlen(configured);
    if (length >= capacity)
        return 0;
    snprintf(path, capacity, "%s", configured);
    if (length < 5u || strcmp(configured + length - 5u, ".json") != 0)
    {
        if (length + 5u >= capacity)
            return 0;
        snprintf(path + length, capacity - length, ".json");
    }
    edit_text(app->tree_edit, path);
    return 1;
}

static void i_on_tree_editor_save(App *app, Event *event)
{
    char path[2048];
    char error[256] = {0};
    if (!app->tree_editor_ready || !pe_tree_editor_validate(&app->tree_editor,
                                                             error, sizeof(error)))
    {
        status(app, "TREE BUILDER BLOCKED\nTree validation failed: %s",
               error[0] ? error : "invalid tree");
        unref(event);
        return;
    }
    if (!tree_editor_save_path(app, path, sizeof(path)) ||
        !pe_tree_editor_write_json(&app->tree_editor, path, error, sizeof(error)))
    {
        status(app, "TREE BUILDER ERROR\n%s", error[0] ? error : "could not save JSON tree");
        unref(event);
        return;
    }
    status(app, "TREE SAVED\n%s\n\nJSON CFR format is ready for the mpf-* tools.", path);
    unref(event);
}

static void i_on_tree_editor_load(App *app, Event *event)
{
    const char *path = app && app->tree_edit ? edit_get_text(app->tree_edit) : NULL;
    pe_monker_tree_header_t header;
    pe_monker_combo_layout_t layout;
    if (!path || !*path)
    {
        status(app, "TREE BUILDER\nChoose a JSON or Monker .tree path in Setup first.");
        unref(event);
        return;
    }
    if (read_tree(app, path, &header, &layout) != 0)
    {
        /* read_tree owns the format detection and reports whether the path
         * was missing, malformed JSON, or an invalid Monker tree. */
        unref(event);
        return;
    }
    status(app, "TREE BUILDER\nImported current tree from %s.\n"
           "The builder edits topology and bet sizes; keep external ranges in Setup before solving.",
           path);
    unref(event);
}

static void i_on_tree_editor_setup(App *app, Event *event)
{
    char path[2048];
    char error[256] = {0};
    if (!app->tree_editor_ready || !pe_tree_editor_validate(&app->tree_editor,
                                                             error, sizeof(error)))
    {
        status(app, "TREE BUILDER BLOCKED\nTree validation failed: %s",
               error[0] ? error : "invalid tree");
        unref(event);
        return;
    }
    if (!tree_editor_save_path(app, path, sizeof(path)) ||
        !pe_tree_editor_write_json(&app->tree_editor, path, error, sizeof(error)))
    {
        status(app, "TREE BUILDER ERROR\n%s", error[0] ? error : "save the tree before using it in Setup");
        unref(event);
        return;
    }
    tabs_selected(app->tabs, 0u);
    panel_visible_layout(app->pages, 0u);
    panel_update(app->pages);
    status(app, "TREE BUILDER\nSaved and selected in Setup: %s\n"
           "Use Load and inspect tree to refresh the setup context.", path);
    unref(event);
}

static Panel *i_tree_editor_panel(App *app)
{
    Panel *panel = panel_create();
    Layout *root = layout_create(2, 1);
    Layout *left = layout_create(1, 2);
    Layout *right = layout_create(1, 2);
    Layout *right_controls = layout_create(2, 15);
    Label *title = label_create();
    Label *node_label = label_create();
    Label *action_label = label_create();
    Label *size_label = label_create();
    Label *remove_label = label_create();
    Label *street_label = label_create();
    Button *new_tree = button_push();
    Button *load_tree = button_push();
    Button *add_action = button_push();
    Button *remove_action = button_push();
    Button *save_tree = button_push();
    Button *use_setup = button_push();
    Button *quick_fold = button_push();
    Button *quick_call = button_push();
    Button *quick_25 = button_push();
    Button *quick_50 = button_push();
    Button *quick_75 = button_push();
    Button *quick_pot = button_push();
    Button *quick_over = button_push();
    Button *collapse_node = button_push();
    Button *expand_node = button_push();
    Button *collapse_all = button_push();
    Button *expand_all = button_push();
    Label *quick_label = label_create();

    app->tree_editor_view = textview_create();
    app->tree_editor_canvas = view_create();
    app->tree_editor_poker_view = view_create();
    view_OnClick(app->tree_editor_canvas,
                 listener(app, i_on_click_tree_outline, App));
    view_OnDraw(app->tree_editor_canvas,
                listener(app, i_on_draw_tree_outline, App));
    view_OnDraw(app->tree_editor_poker_view,
                listener(app, i_on_draw_tree_poker, App));
    app->tree_editor_info = textview_create();
    app->tree_editor_status = textview_create();
    app->tree_editor_node_combo = combo_create();
    app->tree_editor_action_combo = combo_create();
    app->tree_editor_remove_action_combo = combo_create();
    app->tree_editor_street_combo = combo_create();
    app->tree_editor_size_edit = edit_create();
    app->tree_editor_selection_label = label_create();
    textview_editable(app->tree_editor_view, FALSE);
    textview_wrap(app->tree_editor_view, FALSE);
    textview_editable(app->tree_editor_info, FALSE);
    textview_wrap(app->tree_editor_info, TRUE);
    textview_editable(app->tree_editor_status, FALSE);
    textview_wrap(app->tree_editor_status, TRUE);
    textview_printf(app->tree_editor_status,
                    "Create a tree, select a node and add actions.\n");
    label_text(title, "TREE BUILDER | click a node on the map to select it");
    label_text(node_label, "NODE");
    label_text(action_label, "ACTION");
    label_text(size_label, "RAISE SIZE (pot multiple)");
    label_text(remove_label, "REMOVE ACTION");
    label_text(street_label, "NEW TREE STREET");
    label_text(app->tree_editor_selection_label, "No tree created");
    combo_add_elem(app->tree_editor_action_combo, "Fold", NULL);
    combo_add_elem(app->tree_editor_action_combo, "Call / Check", NULL);
    combo_add_elem(app->tree_editor_action_combo, "Raise", NULL);
    combo_add_elem(app->tree_editor_action_combo, "Chance", NULL);
    combo_add_elem(app->tree_editor_street_combo, "Preflop", NULL);
    combo_add_elem(app->tree_editor_street_combo, "Flop", NULL);
    combo_add_elem(app->tree_editor_street_combo, "Turn", NULL);
    combo_add_elem(app->tree_editor_street_combo, "River", NULL);
    combo_selected(app->tree_editor_action_combo, 2u);
    combo_selected(app->tree_editor_street_combo, 0u);
    edit_text(app->tree_editor_size_edit, "0.5");
    button_text(new_tree, "New tree");
    button_text(load_tree, "Import current");
    button_text(add_action, "Add action");
    button_text(remove_action, "Remove action");
    button_text(save_tree, "Save JSON");
    button_text(use_setup, "Use in Setup");
    button_text(collapse_node, "Collapse node");
    button_text(expand_node, "Expand node");
    button_text(collapse_all, "Collapse all");
    button_text(expand_all, "Expand all");
    button_OnClick(new_tree, listener(app, i_on_tree_editor_new, App));
    button_OnClick(load_tree, listener(app, i_on_tree_editor_load, App));
    button_OnClick(add_action, listener(app, i_on_tree_editor_add, App));
    button_OnClick(remove_action, listener(app, i_on_tree_editor_remove, App));
    button_OnClick(save_tree, listener(app, i_on_tree_editor_save, App));
    button_OnClick(use_setup, listener(app, i_on_tree_editor_setup, App));
    button_OnClick(collapse_node, listener(app, i_on_tree_editor_collapse_node, App));
    button_OnClick(expand_node, listener(app, i_on_tree_editor_expand_node, App));
    button_OnClick(collapse_all, listener(app, i_on_tree_editor_collapse_all, App));
    button_OnClick(expand_all, listener(app, i_on_tree_editor_expand_all, App));
    app->tree_editor_collapse_node = collapse_node;
    app->tree_editor_expand_node = expand_node;
    app->tree_editor_collapse_all = collapse_all;
    app->tree_editor_expand_all = expand_all;
    label_text(quick_label, "QUICK ACTIONS (add to selected node)");
    button_text(quick_fold, "Fold");
    button_text(quick_call, "Call / Check");
    button_text(quick_25, "25%");
    button_text(quick_50, "50%");
    button_text(quick_75, "75%");
    button_text(quick_pot, "Pot");
    button_text(quick_over, "2x Pot");
    button_OnClick(quick_fold, listener(app, i_on_tree_quick_fold, App));
    button_OnClick(quick_call, listener(app, i_on_tree_quick_call, App));
    button_OnClick(quick_25, listener(app, i_on_tree_quick_25, App));
    button_OnClick(quick_50, listener(app, i_on_tree_quick_50, App));
    button_OnClick(quick_75, listener(app, i_on_tree_quick_75, App));
    button_OnClick(quick_pot, listener(app, i_on_tree_quick_pot, App));
    button_OnClick(quick_over, listener(app, i_on_tree_quick_overbet, App));
    combo_OnSelect(app->tree_editor_node_combo,
                   listener(app, i_on_tree_editor_node_select, App));
    view_size(app->tree_editor_canvas, s2df(760.0f, 350.0f));
    view_size(app->tree_editor_poker_view, s2df(470.0f, 260.0f));
    layout_label(left, title, 0, 0);
    layout_view(left, app->tree_editor_canvas, 0, 1);
    layout_vsize(left, 1, 350.0f);
    layout_view(right, app->tree_editor_poker_view, 0, 0);
    layout_vsize(right, 0, 270.0f);
    layout_label(right_controls, street_label, 0, 0);
    layout_combo(right_controls, app->tree_editor_street_combo, 1, 0);
    layout_label(right_controls, node_label, 0, 1);
    layout_combo(right_controls, app->tree_editor_node_combo, 1, 1);
    layout_label(right_controls, action_label, 0, 2);
    layout_combo(right_controls, app->tree_editor_action_combo, 1, 2);
    layout_label(right_controls, size_label, 0, 3);
    layout_edit(right_controls, app->tree_editor_size_edit, 1, 3);
    layout_label(right_controls, remove_label, 0, 4);
    layout_combo(right_controls, app->tree_editor_remove_action_combo, 1, 4);
    layout_button(right_controls, new_tree, 0, 5);
    layout_button(right_controls, load_tree, 1, 5);
    layout_button(right_controls, add_action, 0, 6);
    layout_button(right_controls, remove_action, 1, 6);
    layout_button(right_controls, save_tree, 0, 7);
    layout_button(right_controls, use_setup, 1, 7);
    {
        /* One row of standard sizes, the way a betting tree is actually
           built. The size field above stays for anything non-standard. */
        Layout *quick = layout_create(4, 2);
        layout_button(quick, quick_fold, 0, 0);
        layout_button(quick, quick_call, 1, 0);
        layout_button(quick, quick_25, 2, 0);
        layout_button(quick, quick_50, 3, 0);
        layout_button(quick, quick_75, 0, 1);
        layout_button(quick, quick_pot, 1, 1);
        layout_button(quick, quick_over, 2, 1);
        layout_hsize(quick, 0, 108.0f);
        layout_hsize(quick, 1, 108.0f);
        layout_hsize(quick, 2, 108.0f);
        layout_hsize(quick, 3, 108.0f);
        layout_hmargin(quick, 0, 4.0f);
        layout_hmargin(quick, 1, 4.0f);
        layout_hmargin(quick, 2, 4.0f);
        layout_vmargin(quick, 0, 4.0f);
        layout_label(right_controls, quick_label, 0, 8);
        layout_layout(right_controls, quick, 0, 9);
    }
    layout_button(right_controls, collapse_node, 0, 10);
    layout_button(right_controls, expand_node, 1, 10);
    layout_button(right_controls, collapse_all, 0, 11);
    layout_button(right_controls, expand_all, 1, 11);
    layout_label(right_controls, app->tree_editor_selection_label, 0, 12);
    layout_textview(right_controls, app->tree_editor_info, 0, 13);
    layout_textview(right_controls, app->tree_editor_status, 0, 14);
    layout_hsize(right_controls, 0, 190.0f);
    layout_hsize(right_controls, 1, 270.0f);
    layout_hmargin(right_controls, 0, 8.0f);
    layout_vmargin(right_controls, 0, 8.0f);
    layout_vmargin(right_controls, 3, 12.0f);
    layout_vmargin(right_controls, 4, 8.0f);
    layout_vmargin(right_controls, 5, 8.0f);
    layout_vmargin(right_controls, 6, 8.0f);
    layout_vmargin(right_controls, 7, 12.0f);
    layout_vmargin(right_controls, 8, 4.0f);
    layout_vmargin(right_controls, 9, 10.0f);
    layout_vsize(right_controls, 13, 108.0f);
    layout_vsize(right_controls, 14, 140.0f);
    layout_layout(right, right_controls, 0, 1);
    layout_vsize(right, 1, 670.0f);
    layout_layout(root, left, 0, 0);
    layout_layout(root, right, 1, 0);
    layout_hsize(root, 0, 760.0f);
    layout_hsize(root, 1, 470.0f);
    layout_hmargin(root, 0, 12.0f);
    layout_margin(root, 12.0f);
    panel_layout(panel, root);
    pe_tree_editor_init(&app->tree_editor, (int)selected_players(app),
                        MPF_STREET_PREFLOP);
    app->tree_editor_ready = 1;
    edit_text(app->tree_edit, "poker_eval_tree.json");
    tree_editor_refresh(app, app->tree_editor.root_index);
    return panel;
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

static int strategy_matrix_rank_count(const App *app)
{
    enum_game_t game = selected_game(app);
    return game == game_holdem ? 13 : game == game_sdholdem ? 9 : 0;
}

static int strategy_matrix_rank_index(char rank, const char *ranks)
{
    const char *found = ranks ? strchr(ranks, rank) : NULL;
    return found ? (int)(found - ranks) : -1;
}

static int strategy_matrix_position(const char *hand, const char *ranks,
                                    int *row, int *column)
{
    int first;
    int second;
    char rank_a;
    char rank_b;
    if (!hand || strlen(hand) < 4u || !row || !column)
        return 0;
    rank_a = (char)toupper((unsigned char)hand[0]);
    rank_b = (char)toupper((unsigned char)hand[2]);
    first = strategy_matrix_rank_index(rank_a, ranks);
    second = strategy_matrix_rank_index(rank_b, ranks);
    if (first < 0 || second < 0)
        return 0;
    if (first == second)
    {
        /* Pair: the diagonal. */
        *row = first;
        *column = second;
    }
    else
    {
        /* Above the diagonal is suited, below is offsuit -- which the cell
         * labels already assumed (row < column prints "s", row > column
         * prints "o").  The suits were never read, so every hand was
         * normalised to row < column: the offsuit half of the grid could not
         * be reached at all, and each suited cell silently averaged the
         * suited AND offsuit versions of its class together. */
        int suited = tolower((unsigned char)hand[1]) ==
                     tolower((unsigned char)hand[3]);
        int high = first < second ? first : second;  /* smaller index = higher rank */
        int low = first < second ? second : first;
        *row = suited ? high : low;
        *column = suited ? low : high;
    }
    return 1;
}

static const StrategyTableRow *strategy_source_row(const App *app,
                                                    const MonkerHandEntry *entry)
{
    if (!app || !entry)
        return NULL;
    for (uint32_t index = 0u; index < app->strategy_row_count; ++index)
    {
        const StrategyTableRow *source = &app->strategy_rows[index];
        int source_node = atoi(source->node);
        int source_player = source->player[0] == 'P'
            ? atoi(source->player + 1) - 1 : atoi(source->player);
        if (strcmp(source->hand, entry->hand) == 0 &&
            source_node == entry->node && source_player == entry->player)
            return source;
    }
    return NULL;
}

static int strategy_action_kind(const char *name)
{
    char normalized[128];
    size_t length;
    if (!name)
        return 2;
    snprintf(normalized, sizeof(normalized), "%s", name);
    for (length = 0u; normalized[length] != '\0'; ++length)
        normalized[length] = (char)toupper((unsigned char)normalized[length]);
    if (strstr(normalized, "ALL IN") || strstr(normalized, "ALL-IN") ||
        strstr(normalized, "ALLIN"))
        return 4;
    if (strstr(normalized, "FOLD"))
        return 0;
    if (strstr(normalized, "CHECK"))
        return 1;
    if (strstr(normalized, "CALL"))
        return 2;
    if (strstr(normalized, "RAISE") || strstr(normalized, "BET"))
        return 3;
    return 2;
}

static color_t strategy_action_color(int action)
{
    switch (action)
    {
    case 0: return color_rgb(126, 42, 50);  /* fold */
    case 1: return color_rgb(45, 92, 111);  /* check */
    case 2: return color_rgb(30, 105, 72);  /* call */
    case 3: return color_rgb(35, 83, 142);  /* raise */
    case 4: return color_rgb(105, 57, 135);  /* all-in */
    default: return color_rgb(45, 51, 59);
    }
}

static const char *strategy_action_name(int action)
{
    static const char *names[] = {"FOLD", "CHECK", "CALL", "RAISE", "ALL-IN"};
    return action >= 0 && action < 5 ? names[action] : "ACTION";
}

static void draw_strategy_action_legend(App *app, DCtx *ctx,
                                        real32_t x, real32_t y)
{
    if (app && app->bold_font)
        draw_font(ctx, app->bold_font);
    draw_text_color(ctx, color_rgb(220, 226, 232));
    draw_text_align(ctx, ekLEFT, ekCENTER);
    draw_text(ctx, "ACTION COLOR", x, y);
    if (app && app->regular_font)
        draw_font(ctx, app->regular_font);
    for (int action = 0; action < 5; ++action)
    {
        real32_t item_y = y + 27.0f + (real32_t)action * 31.0f;
        draw_fill_color(ctx, strategy_action_color(action));
        draw_rndrect(ctx, ekFILL, x, item_y - 8.0f, 14.0f, 16.0f, 3.0f);
        draw_text_color(ctx, color_rgb(205, 213, 220));
        draw_text(ctx, strategy_action_name(action), x + 23.0f, item_y);
    }
}

static void draw_strategy_action_matrix(App *app, DCtx *ctx,
                                        real32_t width, real32_t height)
{
    const char *ranks = strategy_matrix_rank_count(app) == 9
        ? "AKQJT9876" : "AKQJT98765432";
    int count = (int)strlen(ranks);
    double values[13][13][5] = {{{0.0}}};
    /* Concrete deals per 13x13 class.  A class such as 87s holds several
     * sampled combos and each contributes frequencies summing to 1, so the
     * cell must average them.  Adding them straight up is what produced the
     * impossible "200%" on any class with two sampled combos. */
    int contributors[13][13] = {{0}};
    int left = 42;
    int top = 50;
    real32_t cell = (width - (real32_t)left - 270.0f) / (real32_t)count;
    real32_t by_height = (height - (real32_t)top - 8.0f) / (real32_t)count;
    int active_node = -1;
    int has_values = 0;

    if (app->active_step_index >= 0 &&
        app->active_step_index < (int)app->decision_step_count)
        active_node = app->decision_steps[app->active_step_index].node_index;
    if (cell > by_height)
        cell = by_height;
    if (cell < 18.0f)
        cell = 18.0f;

    for (uint32_t entry_index = 0u; entry_index < app->monker_hand_count;
         ++entry_index)
    {
        const MonkerHandEntry *entry = &app->monker_hands[entry_index];
        int row;
        int column;
        if (active_node >= 0 && entry->node >= 0 && entry->node != active_node)
            continue;
        if (!strategy_matrix_position(entry->hand, ranks, &row, &column))
            continue;
        if (app->mkr_loaded && app->mkr_tree && app->mkr_selected_node >= 0 &&
            app->mkr_selected_node < app->mkr_tree->node_count)
        {
            const mpf_tree_node_t *node = &app->mkr_tree->nodes[app->mkr_selected_node];
            for (uint32_t action = 0u; action < (uint32_t)node->action_count &&
                 action < STRATEGY_TABLE_ACTIONS; ++action)
            {
                char action_label[96];
                tree_action_label(node, (int)action, action_label,
                                  sizeof(action_label));
                values[row][column][strategy_action_kind(action_label)] +=
                    entry->freqs[action];
                has_values = 1;
            }
            ++contributors[row][column];
        }
        else
        {
            const StrategyTableRow *source = strategy_source_row(app, entry);
            if (!source)
                continue;
            for (uint32_t action = 0u; action < source->action_count &&
                 action < STRATEGY_TABLE_ACTIONS; ++action)
            {
                values[row][column][strategy_action_kind(source->actions[action])] +=
                    entry->freqs[action];
                has_values = 1;
            }
            ++contributors[row][column];
        }
    }
    /* Average the class: one row per sampled deal, so the mean strategy of
     * the class.  Frequencies then sum to 1 per cell, as they must. */
    for (int row = 0; row < 13; ++row)
        for (int column = 0; column < 13; ++column)
            if (contributors[row][column] > 1)
                for (int action = 0; action < 5; ++action)
                    values[row][column][action] /=
                        (double)contributors[row][column];

    if (app->bold_font)
        draw_font(ctx, app->bold_font);
    draw_text_color(ctx, color_rgb(225, 230, 235));
    draw_text_align(ctx, ekLEFT, ekCENTER);
    draw_text(ctx, "ACTION MATRIX | ACTIVE SPOT", 12.0f, 18.0f);
    if (app->regular_font)
        draw_font(ctx, app->regular_font);
    draw_text_color(ctx, color_rgb(150, 160, 170));
    draw_text(ctx, count == 9 ? "SHORT DECK NL" : "NLH",
              width - 12.0f, 18.0f);
    draw_strategy_action_legend(app, ctx, left + cell * (real32_t)count + 28.0f, 65.0f);

    for (int row = 0; row < count; ++row)
    {
        for (int column = 0; column < count; ++column)
        {
            int primary = -1;
            double total = 0.0;
            /* The cell is coloured by its dominant action, so the label has
             * to be THAT action's share.  Printing `total` -- the sum over
             * every action -- meant 100% in every populated cell once the
             * class was averaged, and 200% before that. */
            double primary_share = 0.0;
            real32_t x = (real32_t)left + (real32_t)column * cell;
            real32_t y = (real32_t)top + (real32_t)row * cell;
            char hand[8];
            for (int action = 0; action < 5; ++action)
            {
                total += values[row][column][action];
                if (primary < 0 || values[row][column][action] >
                    values[row][column][primary])
                    primary = action;
            }
            if (total <= 0.0)
                primary = -1;
            else if (primary >= 0)
                primary_share = values[row][column][primary] / total;
            draw_fill_color(ctx, primary >= 0 ? strategy_action_color(primary) :
                            color_rgb(39, 45, 53));
            draw_rndrect(ctx, ekFILL, x + 1.0f, y + 1.0f,
                         cell - 2.0f, cell - 2.0f, 2.0f);
            snprintf(hand, sizeof(hand), "%c%c%s", ranks[row], ranks[column],
                     row == column ? "" : row < column ? "s" : "o");
            if (app->regular_font)
                draw_font(ctx, app->regular_font);
            draw_text_color(ctx, color_rgb(235, 240, 244));
            draw_text_align(ctx, ekCENTER, ekCENTER);
            draw_text(ctx, hand, x + cell * 0.5f, y + cell * 0.38f);
            if (total > 0.0)
            {
                char percentage[24];
                snprintf(percentage, sizeof(percentage), "%.0f%%",
                         primary_share * 100.0);
                draw_text_color(ctx, color_rgb(190, 205, 215));
                draw_text(ctx, percentage, x + cell * 0.5f,
                          y + cell * 0.70f);
            }
        }
        draw_text_color(ctx, color_rgb(170, 180, 190));
        draw_text_align(ctx, ekCENTER, ekCENTER);
        draw_text(ctx, (char[2]){ranks[row], '\0'}, left - 16.0f,
                  top + ((real32_t)row + 0.5f) * cell);
        draw_text(ctx, (char[2]){ranks[row], '\0'},
                  left + ((real32_t)row + 0.5f) * cell, top - 17.0f);
    }
    if (!has_values)
    {
        draw_text_color(ctx, color_rgb(150, 160, 170));
        draw_text_align(ctx, ekLEFT, ekCENTER);
        draw_text(ctx, "No strategy result for the selected spot yet.",
                  left + cell * (real32_t)count + 28.0f, 280.0f);
    }
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

static void i_on_draw_icm_spot_table(App *app, Event *event)
{
    const EvDraw *params = event_params(event, EvDraw);
    DCtx *ctx = params->ctx;
    real32_t width = params->width;
    real32_t height = params->height;
    uint32_t players = icm_spot_players(app);
    uint32_t hero = app->icm_spot_hero_combo
        ? combo_get_selected(app->icm_spot_hero_combo) : 0u;
    real32_t cx = width * 0.5f;
    real32_t cy = height * 0.5f + 2.0f;
    real32_t rx = width * 0.36f;
    real32_t ry = height * 0.31f;
    const char *board = app->icm_spot_board_edit
        ? edit_get_text(app->icm_spot_board_edit) : "";
    int cards = card_count(board);
    char pot[96];
    char street[64];

    if (players < 2u) players = 2u;
    if (players > MAX_SETUP_PLAYERS) players = MAX_SETUP_PLAYERS;
    draw_fill_color(ctx, color_rgb(15, 19, 23));
    draw_rect(ctx, ekFILL, 0, 0, width, height);
    draw_fill_color(ctx, color_rgb(72, 31, 34));
    draw_ellipse(ctx, ekFILL, cx, cy, rx + 14.0f, ry + 12.0f);
    draw_fill_color(ctx, color_rgb(20, 113, 48));
    draw_ellipse(ctx, ekFILL, cx, cy, rx, ry);
    draw_line_color(ctx, color_rgb(78, 165, 76));
    draw_line_width(ctx, 1.0f);
    draw_ellipse(ctx, ekSTROKE, cx, cy, rx - 5.0f, ry - 5.0f);

    snprintf(pot, sizeof(pot), "POT %s", app->icm_spot_pot_edit
             ? edit_get_text(app->icm_spot_pot_edit) : "1.50");
    snprintf(street, sizeof(street), "%s", street_name(icm_spot_street(app)));
    if (app->bold_font) draw_font(ctx, app->bold_font);
    draw_text_color(ctx, color_rgb(245, 245, 245));
    draw_text_align(ctx, ekCENTER, ekCENTER);
    draw_text(ctx, pot, cx, cy - 13.0f);
    if (app->regular_font) draw_font(ctx, app->regular_font);
    draw_text_color(ctx, color_rgb(183, 198, 187));
    draw_text(ctx, street, cx, cy + 3.0f);
    if (cards > 0 && cards <= 5)
    {
        real32_t card_w = width > 400.0f ? 28.0f : 22.0f;
        real32_t card_h = width > 400.0f ? 34.0f : 28.0f;
        real32_t gap = 4.0f;
        real32_t start_x = cx - ((real32_t)cards * card_w +
                                 (real32_t)(cards - 1) * gap) * 0.5f;
        draw_hand_badges(ctx, board, start_x, cy + 14.0f,
                         card_w, card_h, gap, app->card_font);
    }
    for (uint32_t player = 0u; player < players; ++player)
    {
        double angle = 2.0 * 3.1415926535 * (double)player / (double)players -
                       1.57079632679;
        real32_t sx = cx + (real32_t)(cos(angle) * (rx + 2.0f));
        real32_t sy = cy + (real32_t)(sin(angle) * (ry + 2.0f));
        real32_t seat_w = players > 6u ? 74.0f : 84.0f;
        real32_t seat_h = 37.0f;
        real32_t left = sx - seat_w * 0.5f;
        real32_t top = sy - seat_h * 0.5f;
        char seat[96];
        const char *stack = app->icm_spot_stack_edit[player]
            ? edit_get_text(app->icm_spot_stack_edit[player]) : "100";
        const char *action = icm_spot_action(app, player);
        int is_hero = player == hero;
        draw_fill_color(ctx, is_hero ? color_rgb(29, 91, 138)
                                     : color_rgb(39, 47, 56));
        draw_rndrect(ctx, ekFILL, left, top, seat_w, seat_h, 6.0f);
        draw_line_color(ctx, is_hero ? color_rgb(52, 157, 218)
                                     : color_rgb(91, 105, 119));
        draw_line_width(ctx, is_hero ? 2.0f : 1.0f);
        draw_rndrect(ctx, ekSTROKE, left, top, seat_w, seat_h, 6.0f);
        snprintf(seat, sizeof(seat), "P%u  %s  |  %s", player + 1u,
                 stack && *stack ? stack : "0", action && *action ? action : "-");
        if (app->card_font) draw_font(ctx, app->card_font);
        draw_text_color(ctx, is_hero ? color_rgb(255, 255, 255)
                                     : color_rgb(215, 222, 228));
        draw_text_align(ctx, ekCENTER, ekCENTER);
        draw_text(ctx, seat, sx, sy);
        if (player == 0u)
        {
            draw_fill_color(ctx, color_rgb(229, 231, 235));
            draw_circle(ctx, ekFILL, left + seat_w - 2.0f, top + 2.0f, 6.0f);
            draw_text_color(ctx, color_rgb(20, 25, 30));
            draw_text(ctx, "D", left + seat_w - 2.0f, top + 2.0f);
        }
    }
}

static void i_on_click_icm_spot_table(App *app, Event *event)
{
    const EvMouse *mouse = event_params(event, EvMouse);
    S2Df size;
    uint32_t players = icm_spot_players(app);
    real32_t cx;
    real32_t cy;
    real32_t rx;
    real32_t ry;
    int nearest = -1;
    real32_t nearest_distance = 1000000.0f;
    if (players < 2u) players = 2u;
    if (players > MAX_SETUP_PLAYERS) players = MAX_SETUP_PLAYERS;
    view_get_size(app->icm_spot_table_view, &size);
    cx = size.width * 0.5f;
    cy = size.height * 0.5f + 2.0f;
    rx = size.width * 0.36f;
    ry = size.height * 0.31f;
    for (uint32_t player = 0u; player < players; ++player)
    {
        double angle = 2.0 * 3.1415926535 * (double)player / (double)players -
                       1.57079632679;
        real32_t sx = cx + (real32_t)(cos(angle) * (rx + 2.0f));
        real32_t sy = cy + (real32_t)(sin(angle) * (ry + 2.0f));
        real32_t dx = mouse->x - sx;
        real32_t dy = mouse->y - sy;
        real32_t distance = dx * dx + dy * dy;
        if (distance < nearest_distance)
        {
            nearest = (int)player;
            nearest_distance = distance;
        }
    }
    if (nearest >= 0 && nearest_distance < 52.0f * 52.0f &&
        app->icm_spot_hero_combo)
    {
        combo_selected(app->icm_spot_hero_combo, (uint32_t)nearest);
        icm_spot_update_context(app);
        i_on_icm_spot_state_change(app, NULL);
    }
    unref(event);
}

static void i_on_draw_icm_spot_board(App *app, Event *event)
{
    const EvDraw *params = event_params(event, EvDraw);
    DCtx *ctx = params->ctx;
    real32_t width = params->width;
    real32_t height = params->height;
    static const char ranks[] = "AKQJT98765432";
    static const char suits[] = "shdc";
    const char *board_text = app->icm_spot_board_edit
        ? edit_get_text(app->icm_spot_board_edit) : "";
    real32_t pad_x = 4.0f;
    real32_t pad_y = 3.0f;
    real32_t card_w = (width - pad_x * 2.0f - 12.0f * 2.0f) / 13.0f;
    real32_t card_h = (height - pad_y * 2.0f - 3.0f * 2.0f) / 4.0f;
    draw_fill_color(ctx, color_rgb(22, 26, 31));
    draw_rndrect(ctx, ekFILL, 0, 0, width, height, 4.0f);
    for (int s = 0; s < 4; ++s)
    {
        for (int r = 0; r < 13; ++r)
        {
            char card[3] = {ranks[r], suits[s], '\0'};
            real32_t x = pad_x + (real32_t)r * (card_w + 2.0f);
            real32_t y = pad_y + (real32_t)s * (card_h + 2.0f);
            int selected = board_text && strstr(board_text, card) != NULL;
            draw_fill_color(ctx, selected ? suit_color(suits[s])
                                          : suit_color_dim(suits[s]));
            draw_rndrect(ctx, ekFILL, x, y, card_w, card_h, 2.0f);
            if (selected)
            {
                draw_line_color(ctx, color_rgb(255, 255, 255));
                draw_line_width(ctx, 1.5f);
                draw_rndrect(ctx, ekSTROKE, x, y, card_w, card_h, 2.0f);
            }
            if (app->card_font) draw_font(ctx, app->card_font);
            draw_text_color(ctx, color_rgb(255, 255, 255));
            draw_text_align(ctx, ekCENTER, ekCENTER);
            card[1] = '\0';
            draw_text(ctx, card, x + card_w * 0.5f, y + card_h * 0.5f);
        }
    }
}

static void i_on_click_icm_spot_board(App *app, Event *event)
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
    int max_cards = street_cards(icm_spot_street(app));
    if (r >= 0 && r < 13 && s >= 0 && s < 4 && app->icm_spot_board_edit)
    {
        char card[3] = {ranks[r], suits[s], '\0'};
        char current[128] = "";
        char next[128] = "";
        const char *text = edit_get_text(app->icm_spot_board_edit);
        if (text && *text && strncmp(text, "No board", 8) != 0)
            snprintf(current, sizeof(current), "%s", text);
        if (strstr(current, card) != NULL)
        {
            char *found = strstr(current, card);
            memmove(found, found + 2, strlen(found + 2) + 1);
            snprintf(next, sizeof(next), "%s", current);
        }
        else if (max_cards > 0 && card_count(current) < max_cards)
            snprintf(next, sizeof(next), "%s%s", current, card);
        else
            snprintf(next, sizeof(next), "%s", current);
        trim_text(next);
        edit_text(app->icm_spot_board_edit, next);
        view_update(app->icm_spot_board_matrix_view);
        i_on_icm_spot_state_change(app, NULL);
    }
    unref(event);
}

static void i_on_icm_spot_state_change(App *app, Event *event)
{
    unref(event);
    icm_spot_update_context(app);
    if (app)
        app->icm_matrix_ready = 0;
    i_on_icm_matrix_input_change(app, NULL);
    if (app && app->icm_tabs && tabs_get_selected(app->icm_tabs) == 1u)
    {
        sync_icm_from_spot(app);
        i_on_analysis_icm(app, NULL);
    }
}

static void i_on_icm_spot_players_select(App *app, Event *event)
{
    uint32_t players = icm_spot_players(app);
    if (players < 2u) players = 2u;
    if (players > MAX_SETUP_PLAYERS) players = MAX_SETUP_PLAYERS;
    if (app->icm_spot_hero_combo &&
        combo_get_selected(app->icm_spot_hero_combo) >= players)
        combo_selected(app->icm_spot_hero_combo, players - 1u);
    i_on_icm_spot_state_change(app, event);
}

static void i_on_icm_spot_street_select(App *app, Event *event)
{
    int street = icm_spot_street(app);
    int expected = street_cards(street);
    const char *board = app->icm_spot_board_edit
        ? edit_get_text(app->icm_spot_board_edit) : "";
    if (street == 0)
        edit_text(app->icm_spot_board_edit, "");
    else if (card_count(board) != expected)
        edit_text(app->icm_spot_board_edit, "");
    i_on_icm_spot_state_change(app, event);
}

static Panel *i_icm_spot_panel(App *app)
{
    Panel *panel = panel_create();
    Layout *root = layout_create(1, 9);
    Layout *game_controls = layout_create(4, 1);
    Layout *spot_controls = layout_create(2, 3);
    Layout *hero_controls = layout_create(2, 1);
    Layout *player_controls = layout_create(6, 5);
    Label *title = label_create();
    Label *game_label = label_create();
    Label *players_label = label_create();
    Label *street_label = label_create();
    Label *pot_label = label_create();
    Label *blinds_label = label_create();
    Label *hero_label = label_create();
    Label *board_label = label_create();
    Label *players_caption = label_create();
    Label *stack_header = label_create();
    Label *action_header = label_create();
    Label *stack_header_right = label_create();
    Label *action_header_right = label_create();

    app->icm_spot_game_combo = combo_create();
    app->icm_spot_players_combo = combo_create();
    app->icm_spot_street_combo = combo_create();
    app->icm_spot_hero_combo = combo_create();
    app->icm_spot_board_edit = edit_create();
    app->icm_spot_pot_edit = edit_create();
    app->icm_spot_blinds_edit = edit_create();
    app->icm_spot_table_view = view_create();
    app->icm_spot_board_matrix_view = view_create();
    app->icm_spot_context = label_create();

    label_text(title, "ICM SPOT SETUP | TABLE STATE");
    label_text(game_label, "GAME");
    label_text(players_label, "PLAYERS");
    label_text(street_label, "STREET");
    label_text(pot_label, "POT");
    label_text(blinds_label, "BLINDS");
    label_text(hero_label, "HERO / ACTING PLAYER");
    label_text(board_label, "BOARD | click cards to set the current flop/turn/river");
    label_text(players_caption, "PLAYERS | stack and action in progress");
    label_text(stack_header, "STACK");
    label_text(action_header, "ACTION");
    label_text(stack_header_right, "STACK");
    label_text(action_header_right, "ACTION");

    combo_add_elem(app->icm_spot_game_combo, "Hold'em", NULL);
    combo_add_elem(app->icm_spot_game_combo, "Short Deck NL", NULL);
    combo_add_elem(app->icm_spot_game_combo, "PLO4", NULL);
    combo_add_elem(app->icm_spot_game_combo, "Stud", NULL);
    combo_add_elem(app->icm_spot_game_combo, "Draw", NULL);
    combo_selected(app->icm_spot_game_combo, 0u);
    for (uint32_t count = 2u; count <= 8u; ++count)
    {
        char text[16];
        snprintf(text, sizeof(text), "%u", count);
        combo_add_elem(app->icm_spot_players_combo, text, NULL);
    }
    combo_selected(app->icm_spot_players_combo, 0u);
    combo_add_elem(app->icm_spot_street_combo, "Preflop", NULL);
    combo_add_elem(app->icm_spot_street_combo, "Flop", NULL);
    combo_add_elem(app->icm_spot_street_combo, "Turn", NULL);
    combo_add_elem(app->icm_spot_street_combo, "River", NULL);
    combo_selected(app->icm_spot_street_combo, 0u);
    for (uint32_t player = 0u; player < MAX_SETUP_PLAYERS; ++player)
    {
        char text[32];
        snprintf(text, sizeof(text), "Player %u", player + 1u);
        combo_add_elem(app->icm_spot_hero_combo, text, NULL);
        app->icm_spot_stack_edit[player] = edit_create();
        app->icm_spot_action_combo[player] = combo_create();
        combo_add_elem(app->icm_spot_action_combo[player], "Fold", NULL);
        combo_add_elem(app->icm_spot_action_combo[player], "Check", NULL);
        combo_add_elem(app->icm_spot_action_combo[player], "Call", NULL);
        combo_add_elem(app->icm_spot_action_combo[player], "Bet", NULL);
        combo_add_elem(app->icm_spot_action_combo[player], "Raise", NULL);
        combo_add_elem(app->icm_spot_action_combo[player], "All-in", NULL);
        combo_selected(app->icm_spot_action_combo[player], 1u);
        edit_text(app->icm_spot_stack_edit[player], "100");
    }
    combo_selected(app->icm_spot_hero_combo, 0u);
    edit_text(app->icm_spot_board_edit, "");
    edit_text(app->icm_spot_pot_edit, "1.50");
    edit_text(app->icm_spot_blinds_edit, "0.5 / 1");

    combo_OnSelect(app->icm_spot_game_combo,
                   listener(app, i_on_icm_spot_state_change, App));
    combo_OnSelect(app->icm_spot_players_combo,
                   listener(app, i_on_icm_spot_players_select, App));
    combo_OnSelect(app->icm_spot_street_combo,
                   listener(app, i_on_icm_spot_street_select, App));
    combo_OnSelect(app->icm_spot_hero_combo,
                   listener(app, i_on_icm_spot_state_change, App));
    edit_OnChange(app->icm_spot_board_edit,
                  listener(app, i_on_icm_spot_state_change, App));
    edit_OnChange(app->icm_spot_pot_edit,
                  listener(app, i_on_icm_spot_state_change, App));
    edit_OnChange(app->icm_spot_blinds_edit,
                  listener(app, i_on_icm_spot_state_change, App));
    for (uint32_t player = 0u; player < MAX_SETUP_PLAYERS; ++player)
    {
        edit_OnChange(app->icm_spot_stack_edit[player],
                      listener(app, i_on_icm_spot_state_change, App));
        combo_OnSelect(app->icm_spot_action_combo[player],
                       listener(app, i_on_icm_spot_state_change, App));
    }
    view_size(app->icm_spot_table_view, s2df(650.0f, 215.0f));
    view_OnDraw(app->icm_spot_table_view,
                listener(app, i_on_draw_icm_spot_table, App));
    view_OnClick(app->icm_spot_table_view,
                 listener(app, i_on_click_icm_spot_table, App));
    view_size(app->icm_spot_board_matrix_view, s2df(328.0f, 86.0f));
    view_OnDraw(app->icm_spot_board_matrix_view,
                listener(app, i_on_draw_icm_spot_board, App));
    view_OnClick(app->icm_spot_board_matrix_view,
                 listener(app, i_on_click_icm_spot_board, App));

    layout_label(game_controls, game_label, 0, 0);
    layout_combo(game_controls, app->icm_spot_game_combo, 1, 0);
    layout_label(game_controls, players_label, 2, 0);
    layout_combo(game_controls, app->icm_spot_players_combo, 3, 0);
    layout_hsize(game_controls, 0, 110.0f);
    layout_hsize(game_controls, 1, 300.0f);
    layout_hsize(game_controls, 2, 80.0f);
    layout_hsize(game_controls, 3, 130.0f);
    layout_hmargin(game_controls, 0, 6.0f);
    layout_hmargin(game_controls, 1, 12.0f);
    layout_hmargin(game_controls, 2, 6.0f);
    layout_label(spot_controls, street_label, 0, 0);
    layout_combo(spot_controls, app->icm_spot_street_combo, 1, 0);
    layout_label(spot_controls, pot_label, 0, 1);
    layout_edit(spot_controls, app->icm_spot_pot_edit, 1, 1);
    layout_label(spot_controls, blinds_label, 0, 2);
    layout_edit(spot_controls, app->icm_spot_blinds_edit, 1, 2);
    layout_hsize(spot_controls, 0, 170.0f);
    layout_hsize(spot_controls, 1, 250.0f);
    layout_label(hero_controls, hero_label, 0, 0);
    layout_combo(hero_controls, app->icm_spot_hero_combo, 1, 0);
    layout_hsize(hero_controls, 0, 170.0f);
    layout_hsize(hero_controls, 1, 250.0f);
    layout_label(player_controls, players_caption, 0, 0);
    layout_label(player_controls, stack_header, 1, 0);
    layout_label(player_controls, action_header, 2, 0);
    layout_label(player_controls, label_create(), 3, 0);
    layout_label(player_controls, stack_header_right, 4, 0);
    layout_label(player_controls, action_header_right, 5, 0);
    for (uint32_t player = 0u; player < MAX_SETUP_PLAYERS; ++player)
    {
        Label *label = label_create();
        char text[16];
        uint32_t column = player < 4u ? 0u : 3u;
        uint32_t row = player % 4u + 1u;
        snprintf(text, sizeof(text), "P%u", player + 1u);
        label_text(label, text);
        layout_label(player_controls, label, column, row);
        layout_edit(player_controls, app->icm_spot_stack_edit[player], column + 1u, row);
        layout_combo(player_controls, app->icm_spot_action_combo[player], column + 2u, row);
    }
    layout_hsize(player_controls, 0, 42.0f);
    layout_hsize(player_controls, 1, 72.0f);
    layout_hsize(player_controls, 2, 150.0f);
    layout_hsize(player_controls, 3, 42.0f);
    layout_hsize(player_controls, 4, 72.0f);
    layout_hsize(player_controls, 5, 150.0f);
    layout_hmargin(player_controls, 0, 5.0f);
    layout_hmargin(player_controls, 1, 5.0f);
    layout_hmargin(player_controls, 3, 5.0f);
    layout_hmargin(player_controls, 4, 5.0f);

    layout_label(root, title, 0, 0);
    layout_layout(root, game_controls, 0, 1);
    layout_view(root, app->icm_spot_table_view, 0, 2);
    layout_label(root, app->icm_spot_context, 0, 3);
    layout_layout(root, spot_controls, 0, 4);
    layout_layout(root, hero_controls, 0, 5);
    layout_label(root, board_label, 0, 6);
    layout_view(root, app->icm_spot_board_matrix_view, 0, 7);
    layout_layout(root, player_controls, 0, 8);
    layout_hsize(root, 0, 650.0f);
    layout_vsize(root, 2, 185.0f);
    layout_vsize(root, 7, 86.0f);
    layout_vsize(root, 8, 132.0f);
    layout_margin(root, 10.0f);
    panel_layout(panel, root);
    icm_spot_update_context(app);
    return panel;
}

static int setup_street(const App *app)
{
    return app && app->setup_street_combo
        ? (int)combo_get_selected(app->setup_street_combo) : 0;
}

static const char *setup_action(const App *app, uint32_t player)
{
    if (!app || player >= MAX_SETUP_PLAYERS || !app->setup_action_combo[player])
        return "Fold";
    return combo_get_text(app->setup_action_combo[player],
                          combo_get_selected(app->setup_action_combo[player]));
}

static void setup_update_context(App *app)
{
    char context[512];
    const char *board;
    uint32_t players;
    int street;

    if (!app)
        return;
    board = app->board_edit ? edit_get_text(app->board_edit) : "";
    players = icm_spot_players(app);
    if (players < 2u)
        players = 2u;
    if (players > MAX_SETUP_PLAYERS)
        players = MAX_SETUP_PLAYERS;
    street = setup_street(app);
    snprintf(context, sizeof(context),
             "%s | %u players | pot %s | board %s | Hero P%u",
             street_name(street), players,
             app->setup_pot_edit ? edit_get_text(app->setup_pot_edit) : "1.5",
             board && *board ? board : "empty",
             app->setup_hero_combo ? combo_get_selected(app->setup_hero_combo) + 1u : 1u);
    if (app->setup_table_context)
        label_text(app->setup_table_context, context);
    if (app->setup_table_view)
        view_update(app->setup_table_view);
    if (app->setup_board_matrix_view)
        view_update(app->setup_board_matrix_view);
}

static enum_game_t icm_spot_game(const App *app)
{
    static const enum_game_t games[] = {
        game_holdem, game_sdholdem, game_omaha, game_7stud, game_5draw
    };
    uint32_t index = app && app->icm_spot_game_combo
        ? combo_get_selected(app->icm_spot_game_combo) : 0u;
    return index < sizeof(games) / sizeof(games[0]) ? games[index] : game_holdem;
}

static uint32_t icm_spot_players(const App *app)
{
    return app && app->icm_spot_players_combo
        ? combo_get_selected(app->icm_spot_players_combo) + 2u : 2u;
}

static int icm_spot_street(const App *app)
{
    return app && app->icm_spot_street_combo
        ? (int)combo_get_selected(app->icm_spot_street_combo) : 0;
}

static const char *icm_spot_action(const App *app, uint32_t player)
{
    if (!app || player >= MAX_SETUP_PLAYERS ||
        !app->icm_spot_action_combo[player])
        return "Check";
    return combo_get_text(app->icm_spot_action_combo[player],
                          combo_get_selected(app->icm_spot_action_combo[player]));
}

static void icm_spot_update_context(App *app)
{
    char context[512];
    const char *board;
    uint32_t players;
    if (!app)
        return;
    board = app->icm_spot_board_edit
        ? edit_get_text(app->icm_spot_board_edit) : "";
    players = icm_spot_players(app);
    if (players < 2u) players = 2u;
    if (players > MAX_SETUP_PLAYERS) players = MAX_SETUP_PLAYERS;
    snprintf(context, sizeof(context), "%s | %u players | pot %s | board %s | Hero P%u",
             street_name(icm_spot_street(app)), players,
             app->icm_spot_pot_edit ? edit_get_text(app->icm_spot_pot_edit) : "1.50",
             board && *board ? board : "empty",
             app->icm_spot_hero_combo
                 ? combo_get_selected(app->icm_spot_hero_combo) + 1u : 1u);
    if (app->icm_spot_context)
        label_text(app->icm_spot_context, context);
    if (app->icm_spot_table_view)
        view_update(app->icm_spot_table_view);
    if (app->icm_spot_board_matrix_view)
        view_update(app->icm_spot_board_matrix_view);
}

static uint32_t icm_game_index_for_spot(enum_game_t game)
{
    if (game == game_sdholdem)
        return 1u;
    if (game == game_omaha || game == game_omaha5 || game == game_omaha6)
        return 2u;
    if (game == game_7stud)
        return 3u;
    if (game == game_5draw)
        return 4u;
    return 0u;
}

static void sync_icm_from_spot(App *app)
{
    char stacks[512];
    char number[64];
    const char *hero_stack_text;
    const char *pot_text;
    const char *hero_action;
    uint32_t players;
    uint32_t hero;
    uint32_t opponent;
    size_t used = 0u;
    double hero_stack = 0.0;
    double pot = 0.0;

    if (!app || !app->icm_game_combo || !app->analysis_stacks_edit ||
        !app->icm_spot_stack_edit[0] || !app->icm_spot_pot_edit)
        return;

    /* The spot controls are canonical.  Keep all eight visible seats in the
     * calculator state so changing P7/P8 cannot silently rewrite or discard
     * the spot.  The ICM engine itself accepts more seats than the equity
     * analysis surface, while the action matrix remains heads-up by design. */
    players = icm_spot_players(app);
    if (players < 2u)
        players = 2u;
    if (players > PE_ANALYSIS_MAX_PLAYERS)
        players = PE_ANALYSIS_MAX_PLAYERS;
    combo_selected(app->icm_game_combo,
                   icm_game_index_for_spot(icm_spot_game(app)));
    stacks[0] = '\0';
    for (uint32_t player = 0u; player < players; ++player)
    {
        const char *stack = edit_get_text(app->icm_spot_stack_edit[player]);
        double parsed;
        if (!stack || !*stack || parse_ui_target(stack, &parsed) != 0)
            stack = "0";
        if (player != 0u && used + 2u < sizeof(stacks))
            used += (size_t)snprintf(stacks + used, sizeof(stacks) - used, ", ");
        if (used < sizeof(stacks))
            used += (size_t)snprintf(stacks + used, sizeof(stacks) - used,
                                     "%s", stack);
    }
    edit_text(app->analysis_stacks_edit, stacks);

    /* The calculator starts with a three-place example ladder.  A two-seat
     * spot cannot pay a third place, so keep the user's ladder when it is
     * already compatible and trim only the incompatible tail. */
    if (app->analysis_payouts_edit)
    {
        double payouts[PE_ANALYSIS_MAX_PLAYERS];
        char error[PE_ANALYSIS_ERROR_MAX];
        int payout_count = 0;
        if (pe_analysis_parse_numbers(edit_get_text(app->analysis_payouts_edit),
                                      payouts, PE_ANALYSIS_MAX_PLAYERS,
                                      &payout_count, error, sizeof(error)) == 0 &&
            payout_count > (int)players)
        {
            char normalized[256];
            size_t payout_used = 0u;
            normalized[0] = '\0';
            for (int payout = 0; payout < (int)players; ++payout)
            {
                if (payout != 0 && payout_used + 2u < sizeof(normalized))
                    payout_used += (size_t)snprintf(normalized + payout_used,
                                                    sizeof(normalized) - payout_used,
                                                    ", ");
                if (payout_used < sizeof(normalized))
                    payout_used += (size_t)snprintf(normalized + payout_used,
                                                    sizeof(normalized) - payout_used,
                                                    "%.2f", payouts[payout]);
            }
            edit_text(app->analysis_payouts_edit, normalized);
        }
    }

    /* FGS needs one win weight per active seat.  Spot setup has no separate
     * future-hand weighting control, therefore use equal weights whenever the
     * calculator still contains a ladder for another table size. */
    if (app->icm_fgs_win_edit)
    {
        double weights[PE_ANALYSIS_MAX_PLAYERS];
        char error[PE_ANALYSIS_ERROR_MAX];
        int weight_count = 0;
        if (pe_analysis_parse_numbers(edit_get_text(app->icm_fgs_win_edit),
                                      weights, PE_ANALYSIS_MAX_PLAYERS,
                                      &weight_count, error, sizeof(error)) == 0 &&
            weight_count != (int)players)
        {
            char normalized[256];
            size_t weight_used = 0u;
            double weight = 100.0 / (double)players;
            normalized[0] = '\0';
            for (int player = 0; player < (int)players; ++player)
            {
                if (player != 0 && weight_used + 2u < sizeof(normalized))
                    weight_used += (size_t)snprintf(normalized + weight_used,
                                                    sizeof(normalized) - weight_used,
                                                    ", ");
                if (weight_used < sizeof(normalized))
                    weight_used += (size_t)snprintf(normalized + weight_used,
                                                    sizeof(normalized) - weight_used,
                                                    "%.2f", weight);
            }
            edit_text(app->icm_fgs_win_edit, normalized);
        }
    }

    hero = app->icm_spot_hero_combo
        ? combo_get_selected(app->icm_spot_hero_combo) : 0u;
    if (hero >= players)
        hero = 0u;
    opponent = hero == 0u ? 1u : 0u;
    snprintf(number, sizeof(number), "%u", hero + 1u);
    if (app->icm_hero_edit)
        edit_text(app->icm_hero_edit, number);
    snprintf(number, sizeof(number), "%u", opponent + 1u);
    if (app->icm_opponent_edit)
        edit_text(app->icm_opponent_edit, number);

    hero_stack_text = edit_get_text(app->icm_spot_stack_edit[hero]);
    pot_text = edit_get_text(app->icm_spot_pot_edit);
    if (hero_stack_text)
        (void)parse_ui_target(hero_stack_text, &hero_stack);
    if (pot_text)
        (void)parse_ui_target(pot_text, &pot);
    hero_action = icm_spot_action(app, hero);
    if (hero_action && (strstr(hero_action, "All") || strstr(hero_action, "all")))
        snprintf(number, sizeof(number), "%.2f", hero_stack);
    else
        snprintf(number, sizeof(number), "0.00");
    if (app->icm_risk_edit)
        edit_text(app->icm_risk_edit, number);
    snprintf(number, sizeof(number), "%.2f", pot);
    if (app->icm_gain_edit)
        edit_text(app->icm_gain_edit, number);
    edit_text(app->icm_fgs_pot_edit, number);
}

static void setup_refresh_player_rows(App *app)
{
    uint32_t players;
    if (!app || !app->setup_players_layout)
        return;
    players = icm_spot_players(app);
    if (players < 2u)
        players = 2u;
    if (players > MAX_SETUP_PLAYERS)
        players = MAX_SETUP_PLAYERS;
    if (app->setup_hero_combo && combo_get_selected(app->setup_hero_combo) >= players)
        combo_selected(app->setup_hero_combo, players - 1u);
    setup_update_context(app);
    sync_icm_from_spot(app);
    if (app)
        app->icm_matrix_ready = 0;
    i_on_icm_matrix_input_change(app, NULL);
    if (app && app->icm_tabs && tabs_get_selected(app->icm_tabs) == 1u)
        i_on_analysis_icm(app, NULL);
    if (app && app->strategy_grid_view)
        view_update(app->strategy_grid_view);
}

static void i_on_setup_state_change(App *app, Event *event)
{
    uint32_t selected = app && app->icm_tabs
        ? tabs_get_selected(app->icm_tabs) : 0u;
    unref(event);
    setup_update_context(app);
}

static void i_on_setup_players_select(App *app, Event *event)
{
    setup_refresh_player_rows(app);
    sync_icm_from_spot(app);
    i_on_icm_matrix_input_change(app, NULL);
    if (app && app->icm_tabs && tabs_get_selected(app->icm_tabs) == 1u)
        i_on_analysis_icm(app, NULL);
    unref(event);
}

static void i_on_setup_street_select(App *app, Event *event)
{
    int street = setup_street(app);
    int expected = street_cards(street);
    const char *board = app->board_edit ? edit_get_text(app->board_edit) : "";
    int current = card_count(board);

    /* A street switch is an explicit setup action.  Keep a valid board when
     * it already matches the requested street; otherwise clear it so an
     * old flop can never accidentally be solved as a river. */
    if (street == 0)
        edit_text(app->board_edit, "");
    else if (current != expected)
        edit_text(app->board_edit, "");
    setup_update_context(app);
    i_on_icm_matrix_input_change(app, NULL);
    if (app->status)
    {
        if (street == 0)
            status(app, "SETUP\nPreflop selected. The board is dealt automatically through the tree.");
        else
            status(app, "SETUP\n%s selected. Choose exactly %d board cards below or type them in BOARD.",
                   street_name(street), expected);
    }
    unref(event);
}

static void i_on_icm_tab(App *app, Event *event)
{
    uint32_t selected = app && app->icm_tabs
        ? tabs_get_selected(app->icm_tabs) : 0u;
    unref(event);
    if (app && app->icm_pages && app->icm_tabs)
    {
        panel_visible_layout(app->icm_pages, selected);
        panel_update(app->icm_pages);
        if (selected == 1u)
        {
            sync_icm_from_spot(app);
            i_on_analysis_icm(app, NULL);
        }
    }
}

static void i_on_draw_setup_table(App *app, Event *event)
{
    const EvDraw *params = event_params(event, EvDraw);
    DCtx *ctx = params->ctx;
    real32_t width = params->width;
    real32_t height = params->height;
    uint32_t players = selected_players(app);
    uint32_t hero = app->setup_hero_combo
        ? combo_get_selected(app->setup_hero_combo) : 0u;
    real32_t cx = width * 0.5f;
    real32_t cy = height * 0.5f + 2.0f;
    real32_t rx = width * 0.36f;
    real32_t ry = height * 0.31f;
    const char *board = app->board_edit ? edit_get_text(app->board_edit) : "";
    int cards = card_count(board);
    char pot[96];
    char street[64];

    if (players < 2u) players = 2u;
    if (players > MAX_SETUP_PLAYERS) players = MAX_SETUP_PLAYERS;
    draw_fill_color(ctx, color_rgb(15, 19, 23));
    draw_rect(ctx, ekFILL, 0, 0, width, height);

    /* Keep the same dark burgundy rim / green felt language as Results and
     * Tree Builder, with a genuine ellipse instead of a rounded rectangle. */
    draw_fill_color(ctx, color_rgb(72, 31, 34));
    draw_ellipse(ctx, ekFILL, cx, cy, rx + 14.0f, ry + 12.0f);
    draw_fill_color(ctx, color_rgb(20, 113, 48));
    draw_ellipse(ctx, ekFILL, cx, cy, rx, ry);
    draw_line_color(ctx, color_rgb(78, 165, 76));
    draw_line_width(ctx, 1.0f);
    draw_ellipse(ctx, ekSTROKE, cx, cy, rx - 5.0f, ry - 5.0f);

    snprintf(pot, sizeof(pot), "POT %s", app->setup_pot_edit
             ? edit_get_text(app->setup_pot_edit) : "1.50");
    snprintf(street, sizeof(street), "%s", street_name(setup_street(app)));
    if (app->bold_font) draw_font(ctx, app->bold_font);
    draw_text_color(ctx, color_rgb(245, 245, 245));
    draw_text_align(ctx, ekCENTER, ekCENTER);
    draw_text(ctx, pot, cx, cy - 13.0f);
    if (app->regular_font) draw_font(ctx, app->regular_font);
    draw_text_color(ctx, color_rgb(183, 198, 187));
    draw_text(ctx, street, cx, cy + 3.0f);
    if (cards > 0 && cards <= 5)
    {
        real32_t card_w = width > 400.0f ? 28.0f : 22.0f;
        real32_t card_h = width > 400.0f ? 34.0f : 28.0f;
        real32_t gap = 4.0f;
        real32_t start_x = cx - ((real32_t)cards * card_w +
                                 (real32_t)(cards - 1) * gap) * 0.5f;
        draw_hand_badges(ctx, board, start_x, cy + 14.0f,
                         card_w, card_h, gap, app->card_font);
    }

    for (uint32_t player = 0u; player < players; ++player)
    {
        double angle = 2.0 * 3.1415926535 * (double)player / (double)players -
                       1.57079632679;
        real32_t sx = cx + (real32_t)(cos(angle) * (rx + 2.0f));
        real32_t sy = cy + (real32_t)(sin(angle) * (ry + 2.0f));
        real32_t seat_w = players > 6u ? 74.0f : 84.0f;
        real32_t seat_h = 37.0f;
        real32_t left = sx - seat_w * 0.5f;
        real32_t top = sy - seat_h * 0.5f;
        char seat[96];
        const char *stack = app->setup_stack_edit[player]
            ? edit_get_text(app->setup_stack_edit[player]) : "100";
        const char *action = setup_action(app, player);
        int is_hero = player == hero;

        draw_fill_color(ctx, is_hero ? color_rgb(29, 91, 138)
                                     : color_rgb(39, 47, 56));
        draw_rndrect(ctx, ekFILL, left, top, seat_w, seat_h, 6.0f);
        draw_line_color(ctx, is_hero ? color_rgb(52, 157, 218)
                                     : color_rgb(91, 105, 119));
        draw_line_width(ctx, is_hero ? 2.0f : 1.0f);
        draw_rndrect(ctx, ekSTROKE, left, top, seat_w, seat_h, 6.0f);
        snprintf(seat, sizeof(seat), "P%u  %s  |  %s", player + 1u,
                 stack && *stack ? stack : "0", action && *action ? action : "-");
        if (app->card_font) draw_font(ctx, app->card_font);
        draw_text_color(ctx, is_hero ? color_rgb(255, 255, 255)
                                     : color_rgb(215, 222, 228));
        draw_text_align(ctx, ekCENTER, ekCENTER);
        draw_text(ctx, seat, sx, sy);
        if (player == 0u)
        {
            draw_fill_color(ctx, color_rgb(229, 231, 235));
            draw_circle(ctx, ekFILL, left + seat_w - 2.0f, top + 2.0f, 6.0f);
            draw_text_color(ctx, color_rgb(20, 25, 30));
            draw_text(ctx, "D", left + seat_w - 2.0f, top + 2.0f);
        }
    }
}

static void i_on_click_setup_table(App *app, Event *event)
{
    const EvMouse *mouse = event_params(event, EvMouse);
    S2Df size;
    uint32_t players = selected_players(app);
    real32_t cx;
    real32_t cy;
    real32_t rx;
    real32_t ry;
    int nearest = -1;
    real32_t nearest_distance = 1000000.0f;

    if (players < 2u) players = 2u;
    if (players > MAX_SETUP_PLAYERS) players = MAX_SETUP_PLAYERS;
    view_get_size(app->setup_table_view, &size);
    cx = size.width * 0.5f;
    cy = size.height * 0.5f + 2.0f;
    rx = size.width * 0.36f;
    ry = size.height * 0.31f;
    for (uint32_t player = 0u; player < players; ++player)
    {
        double angle = 2.0 * 3.1415926535 * (double)player / (double)players -
                       1.57079632679;
        real32_t sx = cx + (real32_t)(cos(angle) * (rx + 2.0f));
        real32_t sy = cy + (real32_t)(sin(angle) * (ry + 2.0f));
        real32_t dx = mouse->x - sx;
        real32_t dy = mouse->y - sy;
        real32_t distance = dx * dx + dy * dy;
        if (distance < nearest_distance)
        {
            nearest = (int)player;
            nearest_distance = distance;
        }
    }
    if (nearest >= 0 && nearest_distance < 52.0f * 52.0f && app->setup_hero_combo)
    {
        combo_selected(app->setup_hero_combo, (uint32_t)nearest);
        setup_update_context(app);
        status(app, "SETUP\nHero selected: Player %d. Edit its current action below.", nearest + 1);
    }
    unref(event);
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
    const char *board_text = app->result_board_cards;
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
    int max_cards = 5;

    if (r >= 0 && r < 13 && s >= 0 && s < 4)
    {
        /* Toggle the card in the RESULTS view filter.  Rows are kept when
         * their runout contains every selected card, so a partial pick such
         * as "Ah" means "every runout containing the ace of hearts" -- which
         * is what a board matrix is for.  Up to five cards, one full board. */
        char card_str[4];
        char current[64];
        char updated[64];
        char *found;
        card_str[0] = ranks[r];
        card_str[1] = suits[s];
        card_str[2] = '\0';
        snprintf(current, sizeof(current), "%s", app->result_board_cards);
        found = strstr(current, card_str);
        if (found)
        {
            memmove(found, found + 2u, strlen(found + 2u) + 1u);
            snprintf(updated, sizeof(updated), "%s", current);
        }
        else if (card_count(current) < max_cards)
            snprintf(updated, sizeof(updated), "%s%s", current, card_str);
        else
            snprintf(updated, sizeof(updated), "%s", current);
        snprintf(app->result_board_cards, sizeof(app->result_board_cards),
                 "%s", updated);
        view_update(app->board_matrix_view);
        /* Ask the LIVE solver for this board rather than filtering the
         * sampled report: the report holds at most --report-rows infosets,
         * so most boards are simply absent from it.  A query walks every
         * infoset the solve visited.  Only a complete board is a question
         * the solver can answer; a partial pick stays a filter. */
        {
            int cards = card_count(app->result_board_cards);
            if (cards >= 3 && cards <= 5)
            {
                char command[96];
                snprintf(command, sizeof(command), "query %s\n",
                         app->result_board_cards);
                if (i_solve_send(app, command) == 0)
                    status(app,
                           "QUERYING %s\nAsking the running solver for this board's "
                           "hand table.\nThe result replaces the sampled report below.",
                           app->result_board_cards);
            }
        }
        /* Re-render either way: with no live solver this is still a filter
         * over whatever the report already holds. */
        render_current_strategy_view(app);
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
    {
        /* Follow the selected decision step rather than a fixed preflop
         * placeholder: on a tree spanning several streets the table has to
         * say which one is on screen. */
        const DecisionStep *active = NULL;
        char pot_text[48];
        const char *street_text = "PREFLOP";
        if (app->active_step_index >= 0 &&
            app->active_step_index < (int)app->decision_step_count)
        {
            active = &app->decision_steps[app->active_step_index];
            street_text = street_name_upper(active->street);
        }
        if (active && active->has_pot)
            snprintf(pot_text, sizeof(pot_text), "POT: %.2f BB", active->pot);
        else
            snprintf(pot_text, sizeof(pot_text), "POT: --");
        draw_text(ctx, pot_text, cx, cy - 8.0f);

        if (app->regular_font)
            draw_font(ctx, app->regular_font);
        draw_text_color(ctx, color_rgba(255, 255, 255, 180));
        draw_text(ctx, street_text, cx, cy + 8.0f);
    }

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
static int strategy_matrix_rank_count(const App *app);
static void draw_strategy_action_matrix(App *app, DCtx *ctx,
                                        real32_t width, real32_t height);

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

    if (strategy_matrix_rank_count(app) > 0)
    {
        draw_strategy_action_matrix(app, ctx, width, height);
        return;
    }

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
    for (uint32_t a = 0u; a < STRATEGY_RESPONSE_ACTIONS; ++a)
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
    for (uint32_t a = 0u; a < STRATEGY_RESPONSE_ACTIONS; ++a)
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
    tableview_header_title(app->strategy_table, 3u + STRATEGY_TABLE_ACTIONS,
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
        if (total >= STUDIO_NO_ITER_CAP)
        {
            /* Single-option stop modes (target-only / manual-only) carry
             * no iteration cap: show infinity instead of the raw 1e12. */
            snprintf(progress_text, sizeof(progress_text),
                     "%s  |  iteration %" PRIu64 " / no cap"
                     "  |  target %.2f mBB",
                     reporting ? "REPORTING" : running ? "RUNNING" : "LAST CHECK",
                     iteration, target);
        }
        else
        {
            snprintf(progress_text, sizeof(progress_text),
                     "%s  |  iteration %" PRIu64 " / %" PRIu64
                     "  |  %.1f%%",
                     reporting ? "REPORTING" : running ? "RUNNING" : "LAST CHECK",
                     iteration, total,
                     fraction * 100.0);
        }
        if (total >= STUDIO_NO_ITER_CAP)
            snprintf(text, sizeof(text), "%" PRIu64 " / no cap", iteration);
        else
            snprintf(text, sizeof(text), "%" PRIu64 " / %" PRIu64,
                     iteration, total);
        label_text(app->run_progress, text);
        if (total >= STUDIO_NO_ITER_CAP)
            snprintf(text, sizeof(text), "tgt %.2f", target);
        else
            snprintf(text, sizeof(text), "%.1f%%", fraction * 100.0);
        label_text(app->run_fraction, text);
        snprintf(text, sizeof(text), "%.2f mBB%s", exploitability,
                 board_abstraction_caveat(app)[0] ? "  (abstract)" : "");
        label_text(app->run_metrics, text);
        snprintf(text, sizeof(text), reporting ? "REPORTING  %02d:%02d"
                 : running ? "RUNNING  %02d:%02d" : "LAST CHECK",
                 (int)elapsed / 60, (int)elapsed % 60);
        label_text(app->run_state, text);

        /* Left Stats Panel */
        if (app->lbl_status_val)
        {
            int tcount = (app->solve_has_resolved && app->solve_resolved_threads > 0)
                             ? app->solve_resolved_threads
                             : 1;
            snprintf(text, sizeof(text), "%s, %02d:%02d, %d thread(s)",
                     reporting ? "Reporting" : running ? "Running" : "Complete",
                     (int)elapsed / 60, (int)elapsed % 60, tcount);
            label_text(app->lbl_status_val, text);
        }
        if (app->lbl_iterations_val)
        {
            if (total >= STUDIO_NO_ITER_CAP)
                snprintf(text, sizeof(text), "%" PRIu64 " / no cap", iteration);
            else
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
            const char *caveat = board_abstraction_caveat(app);
            if (target > 0.0)
                snprintf(text, sizeof(text),
                         "Empirical exploitability: %.2f mBB  |  stop target: %.2f mBB%s%s",
                         exploitability, target,
                         caveat[0] ? "  |  MEASURED INSIDE THE BOARD ABSTRACTION" : "",
                         caveat);
            else
                snprintf(text, sizeof(text),
                         "Empirical exploitability: %.2f mBB  |  stop target: disabled (max iterations)%s%s",
                         exploitability,
                         caveat[0] ? "  |  MEASURED INSIDE THE BOARD ABSTRACTION" : "",
                         caveat);
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
        {
            const char *caveat = board_abstraction_caveat(app);
            snprintf(text, sizeof(text),
                     "Final: %s  |  %.2f mBB  |  raw %.5f  |  BR samples %" PRIu64 "%s%s",
                     guarantee, mbb, raw, samples,
                     caveat[0] ? "  |  MEASURED INSIDE THE BOARD ABSTRACTION" : "",
                     caveat);
        }
        snprintf(progress_text, sizeof(progress_text), "%.2f mBB%s", mbb,
                 board_abstraction_caveat(app)[0] ? "  (abstract)" : "");
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

static int tree_path_is_json(const char *path)
{
    size_t length;
    if (!path)
        return 0;
    length = strlen(path);
    return (length >= 5u && strcmp(path + length - 5u, ".json") == 0) ||
           (length >= 10u && strcmp(path + length - 10u, ".tree.json") == 0);
}

static int infer_json_tree_header(const mpf_tree_def_t *tree,
                                  pe_monker_tree_header_t *header,
                                  pe_monker_combo_layout_t *layout,
                                  enum_game_t game)
{
    int players = 2;
    int street = MPF_STREET_PREFLOP;
    int first_to_act = 0;
    if (!tree || !header || !layout || tree->root_index < 0 ||
        tree->root_index >= tree->node_count)
        return 0;
    for (int node = 0; node < tree->node_count; ++node)
    {
        const mpf_tree_node_t *entry = &tree->nodes[node];
        if (entry->acting_player >= 0 && entry->acting_player + 1 > players)
            players = entry->acting_player + 1;
        if (entry->has_snapshot && entry->snapshot.has_num_players &&
            entry->snapshot.num_players >= 2 && entry->snapshot.num_players <= 8)
            players = entry->snapshot.num_players;
    }
    street = tree->nodes[tree->root_index].street;
    first_to_act = tree->nodes[tree->root_index].acting_player;
    memset(header, 0, sizeof(*header));
    header->player_count = (uint32_t)(players >= 2 && players <= 8 ? players : 2);
    header->street = street >= MPF_STREET_PREFLOP && street <= MPF_STREET_RIVER
        ? street : MPF_STREET_PREFLOP;
    header->first_to_act = first_to_act >= 0 ? first_to_act : 0;
    layout->game = game;
    layout->hole_cards = hole_cards_from_game(game);
    layout->combo_count = 0u;
    return 1;
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
    if (tree_path_is_json(path))
    {
        mpf_tree_error_t json_error = {0};
        tree = mpf_tree_load_json_file(path, &json_error);
        if (!tree || !infer_json_tree_header(tree, header, layout, selected_game(app)))
        {
            status(app, "TREE ERROR\n%s", json_error.message[0]
                   ? json_error.message : "JSON tree could not be decoded");
            mpf_tree_free(tree);
            return -1;
        }
        tree_status = PE_MONKER_OK;
    }
    else
    {
        tree_status = pe_monker_tree_read_header(path, header);
        if (tree_status != PE_MONKER_OK)
        {
            status(app, "TREE ERROR\n%s", pe_monker_status_string(tree_status));
            return -1;
        }
        tree_status = pe_monker_tree_load(path, &tree);
    }
    if (tree_status != PE_MONKER_OK || !tree)
    {
        status(app, "TREE ERROR\nTopology could not be decoded: %s",
               pe_monker_status_string(tree_status));
        return -1;
    }
    app->tree_node_count = tree->node_count > 0 ? (uint32_t)tree->node_count : 0u;
    {
        /* Per-street census shown in TREE CONTEXT: proves what the file
         * actually holds.  The binary Monker format stores a single
         * street (all nodes inherit the header street); JSON trees may
         * carry several.  Lane B follows the run street's decisions. */
        uint32_t street_nodes[5] = {0u, 0u, 0u, 0u, 0u};
        char *street_text = app->tree_street_summary;
        size_t used = 0u;
        static const char *names[5] = {"PRE", "FLOP", "TURN", "RIVER", "SHOWDOWN"};
        if (tree->nodes && tree->node_count > 0)
        {
            for (int node_index = 0; node_index < tree->node_count; ++node_index)
            {
                const mpf_tree_node_t *node = &tree->nodes[node_index];
                if (node->type == MPF_TREE_NODE_PLAYER &&
                    (int)node->street >= 0 && (int)node->street < 5)
                    ++street_nodes[(int)node->street];
            }
        }
        /* Unconditional: loading a node-less tree must clear the previous
         * tree's census, not inherit it. */
        for (int street = 0; street < 5; ++street)
            app->tree_street_nodes[street] = street_nodes[street];
        street_text[0] = '\0';
        for (int street = 0; street < 5; ++street)
        {
            if (street_nodes[street] == 0u)
                continue;
            used += (size_t)snprintf(street_text + used,
                                     sizeof(app->tree_street_summary) - used,
                                     "%s%s=%u", used ? " " : "",
                                     names[street], street_nodes[street]);
            if (used + 1u >= sizeof(app->tree_street_summary))
                break;
        }
        if (used == 0u)
            snprintf(street_text, sizeof(app->tree_street_summary), "none");
    }
    /* A new tree is a new spot; card picks from the previous one would
     * silently filter the new report down to nothing. */
    app->result_board_cards[0] = '\0';
    app->tree_player_count = header->player_count >= 2u &&
                             header->player_count <= 6u
        ? header->player_count : 0u;
    if (app->tree_editor_ready)
        (void)tree_editor_import_tree(app, tree);
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
    if (app->setup_street_combo)
        combo_selected(app->setup_street_combo, (uint32_t)header->street);
    if (header->street == 0)
    {
        edit_text(app->board_edit, "");
        if (app->board_edit_quick)
            edit_text(app->board_edit_quick, "");
        edit_text(app->range0_edit, "100%");
        edit_text(app->range1_edit, "100%");
        /* Blinds build the preflop pot; a leftover postflop figure here
         * would be silently ignored, so clear it. */
        if (app->setup_pot_edit)
            edit_text(app->setup_pot_edit, "");
    }
    else
    {
        if (!edit_get_text(app->range0_edit) || !*edit_get_text(app->range0_edit))
            edit_text(app->range0_edit, "100%");
        if (!edit_get_text(app->range1_edit) || !*edit_get_text(app->range1_edit))
            edit_text(app->range1_edit, "100%");
        if (app->setup_pot_edit)
            edit_phtext(app->setup_pot_edit, "Required: pot in the middle at this node");
    }
    label_text(app->board_label, header->street == 0
               ? "BOARD / RUNOUT: automatic through river"
               : "BOARD / RUNOUT: enter the cards for this street");
    setup_refresh_player_rows(app);
    setup_update_context(app);
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
        textview_printf(app->strategy_view, "decision nodes by street      %s\n",
                        app->tree_street_summary[0] ? app->tree_street_summary : "?");
        textview_printf(app->strategy_view, "solved streets                every street the tree wires into; past its last node the board is dealt and rolled out\n");
        textview_printf(app->strategy_view, "ranges                        %s\n",
                        ranges_present ? "embedded" : "external / defaults to 100%%");
        textview_printf(app->strategy_view, "board / runouts               %s\n",
                        board && *board ? board : "automatic through river");
        result_write_line(app->strategy_view, "");
        result_write_line(app->strategy_view,
                          "Run the spot to replace this context with the strategy table.");
    }
    status(app,
           "TREE READY\nGame: %s%s\nPlayers: %u\nStreet: %s\nNodes: %d (%s)\nRanges: %s\n\n%s",
           game_name(layout->game),
           ranges_present ? " (from tree)" : " (selected)",
           header->player_count, street_name(header->street), tree->node_count,
           app->tree_street_summary[0] ? app->tree_street_summary : "?",
           ranges_present ? "embedded" : "not embedded; enter external ranges",
           header->street == 0
               ? "Preflop Lane B: ranges are sampled with card removal; public boards are dealt through river."
               : "Postflop Lane B: enter the board cards AND the pot at the root before Solve\n"
                 "(there are no blinds postflop, and stacks are read as remaining).");
    mpf_tree_free(tree);
    pe_monker_range_set_free(&ranges);
    return 0;
}

static void i_on_tree(App *app, Event *event)
{
    /* Both tree formats the loader accepts: the binary Monker .tree, and the
     * JSON trees read by tree_path_is_json (.json, including .tree.json).
     * Filtering on "tree" alone hid every JSON tree from this dialog and
     * left typing the path as the only way in. */
    const char_t *types[] = {"tree", "json"};
    const char_t *path = comwin_open_file(app->window, "Open tree (.tree or .json)",
                                          types, 2, NULL, NULL);
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
        const char *relative_candidates[] = {
            "%s/%s",
            "%s/../%s",
            "%s/../../build/tools/%s",
            "%s/../../build-studio/tools/%s",
            "%s/../../../build/tools/%s",
            "%s/../../../build-studio/tools/%s"
        };
        for (size_t candidate = 0u;
             candidate < sizeof(relative_candidates) / sizeof(relative_candidates[0]);
             ++candidate)
        {
            (void)snprintf(local_path, sizeof(local_path), relative_candidates[candidate],
                           executable_dir, name);
            if (PE_ACCESS(local_path, PE_X_OK) == 0)
                return local_path;
        }
    }
    (void)snprintf(local_path, sizeof(local_path), "build/tools/%s", name);
    if (PE_ACCESS(local_path, PE_X_OK) == 0)
        return local_path;
    (void)snprintf(local_path, sizeof(local_path), "build-studio/tools/%s", name);
    return PE_ACCESS(local_path, PE_X_OK) == 0 ? local_path : NULL;
}

/* spot_checkpoint_path derives a stable per-spot checkpoint file path
 * from the source tree path: "<tree_dir>/<tree_basename>.ckpt" (or just
 * "<basename>.ckpt" when the tree path has no directory), so the solver
 * can write there during a normal run and the Studio can resume from it.
 * Returns 0 on success, -1 if the path is too long. */
static int spot_checkpoint_path(const char *tree_path, char *out, size_t capacity)
{
    const char *base;
    const char *sep;
    size_t base_len;
    size_t dir_len;
    if (!tree_path || !*tree_path || !out || capacity == 0u)
        return -1;
    sep = strrchr(tree_path, PE_PATH_SEPARATOR);
    base = (sep != NULL) ? sep + 1 : tree_path;
    dir_len = (sep != NULL) ? (size_t)(sep - tree_path) : 0u;
    base_len = strlen(base);
    /* Strip a trailing ".tree" if any. */
    if (base_len > 5u &&
        strcmp(base + base_len - 5u, ".tree") == 0)
    {
        base_len -= 5u;
    }
    if (base_len == 0u)
    {
        base = "spot";
        base_len = 4u;
    }
    if (dir_len > 0u)
    {
        if (snprintf(out, capacity, "%.*s%c%.*s.ckpt",
                     (int)dir_len, tree_path, PE_PATH_SEPARATOR,
                     (int)base_len, base) >= (int)capacity)
            return -1;
    }
    else if (snprintf(out, capacity, "%.*s.ckpt",
                      (int)base_len, base) >= (int)capacity)
    {
        return -1;
    }
    return 0;
}

/* Appends at most n bytes from src to a NUL-terminated buffer,
 * honouring capacity.  *used tracks the current length. */
static void i_copy_append(char *out, size_t capacity, size_t *used,
                          const char *src, size_t n)
{
    size_t room;
    if (!out || capacity == 0u || !used || !src || n == 0u)
        return;
    if (*used + 1u >= capacity)
        return;
    room = capacity - 1u - *used;
    if (n > room)
        n = room;
    memcpy(out + *used, src, n);
    *used += n;
    out[*used] = '\0';
}

static void i_solve_copy_output(App *app, char *out, size_t capacity)
{
    size_t total, length, used = 0u;
    if (!app || !out || capacity == 0u)
        return;
    bmutex_lock(app->solve_mutex);
    /* Assemble a render-friendly window instead of a dumb byte prefix.
     * On multi-megabyte reports (e.g. plo4, 100k+ rows) the first 64KB
     * hold only the header and DECISION STEPS: the HAND TABLE the grid
     * needs starts far beyond, and the guarantee=/progress lines the
     * stats need live before the report marker — so a prefix copy
     * renders an empty Results panel after Stop.  The window below
     * carries: synthetic progress + guarantee lines (rebuilt from the
     * scanned telemetry/metrics), the report header, the first decision
     * steps, then HAND TABLE rows until the budget runs out. */
    if (app->strategy_output_length > 0u)
    {
        char line[256];
        const char *cursor;
        const char *end;
        const char *hands;
        size_t n;
        unsigned steps = 0u;
        int width;
        out[0] = '\0';
        used = 0u;
        if (app->telemetry_valid)
        {
            width = snprintf(line, sizeof(line),
                             "progress iteration=%" PRIu64 " total=%" PRIu64
                             " fraction=%f exploitability_mbb=%f target_mbb=%f\n",
                             app->telemetry_iteration, app->telemetry_total,
                             app->telemetry_fraction, app->telemetry_exploitability,
                             app->telemetry_target);
            if (width > 0)
                i_copy_append(out, capacity, &used, line, (size_t)width);
        }
        if (app->final_metrics_valid)
        {
            width = snprintf(line, sizeof(line),
                             "guarantee=%s exploitability_raw=%f"
                             " exploitability_mbb=%f br_samples=%" PRIu64 "\n",
                             app->final_guarantee, app->final_raw,
                             app->final_mbb, app->final_samples);
            if (width > 0)
                i_copy_append(out, capacity, &used, line, (size_t)width);
        }
        end = strchr(app->strategy_output, '\n');
        n = end ? (size_t)(end - app->strategy_output) + 1u
                : strlen(app->strategy_output);
        i_copy_append(out, capacity, &used, app->strategy_output, n);
        cursor = app->strategy_output;
        while (steps < 96u && (cursor = strstr(cursor, "tree_step ")) != NULL)
        {
            end = strchr(cursor, '\n');
            n = end ? (size_t)(end - cursor) + 1u : strlen(cursor);
            if (used + n + 1u > capacity)
                break;
            i_copy_append(out, capacity, &used, cursor, n);
            cursor += n;
            ++steps;
        }
        hands = strstr(app->strategy_output, "HAND TABLE");
        if (hands != NULL)
        {
            i_copy_append(out, capacity, &used, "HAND TABLE\n", 11u);
            cursor = strchr(hands, '\n');
            cursor = cursor ? cursor + 1u : NULL;
            while (cursor && *cursor)
            {
                end = strchr(cursor, '\n');
                n = end ? (size_t)(end - cursor) + 1u : strlen(cursor);
                if (used + n + 1u > capacity)
                    break;
                i_copy_append(out, capacity, &used, cursor, n);
                /* RANGE GRID ends the hand section; copying it is useless
                 * for the grid and would eat the row budget. */
                if (n >= 10u && strncmp(cursor, "RANGE GRID", 10u) == 0)
                    break;
                cursor = end ? end + 1u : NULL;
            }
        }
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

/* Reusable buffer holding a snapshot of the solver output, sized to
 * solve_output.  NULL only if the one allocation failed. */
static char *report_scratch(App *app)
{
    if (!app)
        return NULL;
    if (!app->report_scratch)
        app->report_scratch = (char *)malloc(sizeof(app->solve_output));
    return app->report_scratch;
}

/* The report can run to megabytes, so these copies use the shared scratch: a
 * stack buffer big enough would blow the thread stack, and a small one would
 * truncate the hand table exactly like solve_output used to. */
static void render_current_strategy_view(App *app)
{
    char *output = report_scratch(app);
    if (!output)
        return;
    i_solve_copy_output(app, output, sizeof(app->solve_output));
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

/* Money along one betting path.
 *
 * A node's pot is NOT a property of the node: these trees funnel several
 * lines into the same next-street node, so turn_first sits in 2.00 after a
 * checked flop and 4.64 after bet-call.  It is only well defined for a given
 * path, which is why it is accumulated here, down the same walk that prints
 * the decision history, rather than declared per node. */
typedef struct
{
    double pot;
    double contrib[2];   /* this round, per seat */
    double bet;          /* highest contribution this round */
    int street;
} path_money_t;

/* Both size conventions the engine uses, confirmed against a solve:
 * a "chips" size is the increment ABOVE the call, a pot_sizing size is a
 * fraction of the pot once the call is made. */
static void path_money_apply(path_money_t *money, const mpf_tree_node_t *node,
                             int action)
{
    int seat = node->acting_player >= 0 && node->acting_player < 2
                   ? node->acting_player : 0;
    double outstanding = money->bet - money->contrib[seat];
    if (outstanding < 0.0)
        outstanding = 0.0;
    if (node->actions[action].type == MPF_TREE_ACTION_FOLD)
        return;
    if (node->actions[action].type == MPF_TREE_ACTION_CALL)
    {
        money->pot += outstanding;
        money->contrib[seat] = money->bet;
        return;
    }
    if (node->actions[action].type == MPF_TREE_ACTION_RAISE)
    {
        int index = node->actions[action].size_index;
        double size = (index >= 0 && index < node->bet_size_count)
                          ? node->bet_sizes[index] : 0.0;
        double raise_by;
        if (size < 0.0)           /* all-in / min-raise markers */
            return;
        raise_by = node->use_pot_sizing ? size * (money->pot + outstanding) : size;
        money->pot += outstanding + raise_by;
        money->contrib[seat] = money->bet + raise_by;
        money->bet = money->contrib[seat];
    }
}

static int tree_history_dfs(const mpf_tree_def_t *tree, int node_index,
                            int target, unsigned char *visited,
                            uint32_t depth, char *history, size_t capacity,
                            size_t *used, path_money_t *money,
                            double *out_pot)
{
    const mpf_tree_node_t *node;
    if (!tree || !visited || !history || !used || node_index < 0 ||
        node_index >= tree->node_count || depth > (uint32_t)tree->node_count)
        return 0;
    if (node_index >= 0 && node_index < tree->node_count &&
        (int)tree->nodes[node_index].street != money->street)
    {
        /* New street: the round's contributions reset, the pot carries. */
        money->street = (int)tree->nodes[node_index].street;
        money->contrib[0] = money->contrib[1] = 0.0;
        money->bet = 0.0;
    }
    if (node_index == target)
    {
        if (out_pot)
            *out_pot = money->pot;
        return 1;
    }
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
        /* A tree may span several streets.  Without a marker the whole path
         * read as one long preflop sequence, which is what made a working
         * multi-street solve look stuck on the first street. */
        {
            int child = node->actions[action].next_index;
            if (child >= 0 && child < tree->node_count)
            {
                int child_street = (int)tree->nodes[child].street;
                if (child_street > (int)node->street && child_street <= 3)
                {
                    int extra = snprintf(history + *used, capacity - *used,
                                         "-- %s --\n",
                                         street_name_upper(child_street));
                    if (extra > 0 && (size_t)extra < capacity - *used)
                        *used += (size_t)extra;
                }
            }
        }
        {
            path_money_t saved = *money;
            path_money_apply(money, node, action);
            if (tree_history_dfs(tree, node->actions[action].next_index, target,
                                 visited, depth + 1u, history, capacity, used,
                                 money, out_pot))
            {
                visited[node_index] = 0u;
                return 1;
            }
            *money = saved;
        }
        *used = previous;
        history[*used] = '\0';
    }
    visited[node_index] = 0u;
    return 0;
}

/* The pot the tree starts from: what the root node declares, else the POT AT
 * ROOT field for a postflop tree, else the blinds the driver posts by default
 * (pe-preflop-solve's DEFAULT_SMALL_BLIND + DEFAULT_BIG_BLIND). */
static double tree_root_pot(const App *app, const mpf_tree_def_t *tree, int root)
{
    if (tree && root >= 0 && root < tree->node_count &&
        tree->nodes[root].has_snapshot && tree->nodes[root].snapshot.has_pot)
        return tree->nodes[root].snapshot.pot;
    if (tree && root >= 0 && root < tree->node_count &&
        (int)tree->nodes[root].street != 0)
    {
        double pot = 0.0;
        if (app && app->setup_pot_edit &&
            parse_ui_target(edit_get_text(app->setup_pot_edit), &pot) == 0 &&
            pot > 0.0)
            return pot;
        return 0.0;
    }
    return 1.5;
}

static void tree_history_for_node(const App *app, const mpf_tree_def_t *tree,
                                  int target, char history[512],
                                  double *out_pot, int *out_has_pot)
{
    unsigned char *visited;
    size_t used;
    int root;
    path_money_t money;
    if (out_has_pot)
        *out_has_pot = 0;
    if (!history)
        return;
    history[0] = '\0';
    if (!tree || target < 0 || target >= tree->node_count)
    {
        snprintf(history, 512u, "PREFLOP\n");
        return;
    }
    root = (tree->root_index >= 0 && tree->root_index < tree->node_count)
               ? tree->root_index : 0;
    /* The heading is the street the tree is ROOTED at, not a fixed
     * "PREFLOP": a flop or river tree starts where it starts. */
    snprintf(history, 512u, "%s\n",
             street_name_upper((int)tree->nodes[root].street));
    used = strlen(history);
    visited = (unsigned char *)calloc((size_t)tree->node_count, sizeof(*visited));
    if (!visited)
        return;
    memset(&money, 0, sizeof(money));
    money.pot = tree_root_pot(app, tree, root);
    money.street = (int)tree->nodes[root].street;
    if (money.street == 0)
    {
        /* Heads-up blinds are already in the middle at the preflop root. */
        money.contrib[0] = 0.5;
        money.contrib[1] = 1.0;
        money.bet = 1.0;
    }
    {
        double pot = money.pot;
        if (tree_history_dfs(tree, root, target, visited, 0u, history, 512u,
                             &used, &money, &pot) && out_pot && out_has_pot)
        {
            *out_pot = pot;
            *out_has_pot = money.pot > 0.0 || pot > 0.0;
        }
    }
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
        step->street = (int)node->street;
        step->acting_player = node->acting_player;
        tree_history_for_node(app, tree, node_index, step->history,
                              &step->pot, &step->has_pot);
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
    /* "All streets".  This used to jump to Preflop whenever a report was
     * present, which was harmless only while every row was mislabelled
     * preflop anyway.  Now that rows carry their real street, pinning the
     * filter here hid every flop/turn/river row the moment a run finished. */
    combo_selected(app->street_filter, 0u);

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
                tree_history_for_node(app, app->mkr_tree, st->node_index,
                                      st->history, &st->pot, &st->has_pot);

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
    /* Pot per node, from the report's observed-decision lines
     * ("step node=N actor=P1 hand=... pot=X to_call=Y ...").  The tree alone
     * cannot supply it: the pot at a node depends on the betting path. */
    cursor = output;
    while (cursor && (cursor = strstr(cursor, "step node=")) != NULL)
    {
        const char *pot_field;
        const char *line_end = strchr(cursor, '\n');
        int node_index = atoi(cursor + 10);
        /* "tree_step node=" also ends in "step node=" -- those lines carry
         * no pot, and strstr below simply finds nothing in them. */
        pot_field = strstr(cursor, "pot=");
        if (pot_field && (!line_end || pot_field < line_end))
        {
            double value = atof(pot_field + 4);
            for (uint32_t i = 0u; i < app->decision_step_count; ++i)
                if (app->decision_steps[i].node_index == node_index &&
                    !app->decision_steps[i].has_pot)
                {
                    app->decision_steps[i].pot = value;
                    app->decision_steps[i].has_pot = 1;
                    break;
                }
        }
        cursor = line_end ? line_end + 1 : NULL;
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
    /* Runouts actually played, from the report rows' sixth column.  A
     * preflop-rooted run deals a different board every iteration, so this is
     * the only place those boards exist.  Capped: the combo is for picking a
     * runout, not for listing thousands. */
    {
        const char *scan = strstr(output ? output : "", "HAND TABLE");
        uint32_t added = 0u;
        while (scan && added < 64u && (scan = strchr(scan, '\n')) != NULL)
        {
            char line[2048];
            const char *end;
            size_t n;
            ++scan;
            end = strchr(scan, '\n');
            n = end ? (size_t)(end - scan) : strlen(scan);
            if (n == 0u)
                break;
            if (n >= sizeof(line))
                n = sizeof(line) - 1u;
            memcpy(line, scan, n);
            line[n] = '\0';
            if (strncmp(line, "report_phase=", 13u) == 0)
                break;
            if (strncmp(line, "ev_update\t", 10u) != 0 &&
                result_line_board(line, board, sizeof(board)))
            {
                result_combo_add_unique(app->board_filter, board);
                ++added;
            }
            scan = end;
        }
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

/* Street of one report row, from the decision step that owns its node.
 * The report's own header carries the run's ROOT street only, so a tree
 * spanning several streets cannot be filtered against a single value: every
 * flop/turn/river row would be judged preflop and dropped. */
static int result_node_street(const App *app, int node_index)
{
    if (!app || node_index < 0)
        return -1;
    for (uint32_t i = 0u; i < app->decision_step_count; ++i)
        if (app->decision_steps[i].node_index == node_index)
            return app->decision_steps[i].street;
    return -1;
}

/* Board of one report row: its sixth tab-separated field, added so a
 * per-board filter has something to match.  A row from an older report has
 * only five fields and matches nothing, rather than matching everything. */
static int result_line_board(const char *line, char *out, size_t capacity)
{
    const char *cursor = line;
    int tabs = 0;
    size_t used = 0u;
    if (!line || !out || capacity == 0u)
        return 0;
    out[0] = '\0';
    while (*cursor && tabs < 5)
        if (*cursor++ == '\t')
            ++tabs;
    if (tabs < 5)
        return 0;
    while (*cursor && *cursor != '\n' && *cursor != '\t' &&
           used + 1u < capacity)
        out[used++] = *cursor++;
    out[used] = '\0';
    return used > 0u && strcmp(out, "-") != 0;
}

static int result_line_board_matches(const char *line, const char *board)
{
    char row_board[64];
    if (!board || !*board)
        return 1;
    if (!result_line_board(line, row_board, sizeof(row_board)))
        return 0;
    return strcmp(row_board, board) == 0;
}

/* Text like "Ks7d2c" to a card mask; 0 cards on a malformed string. */
static int board_text_to_mask(const char *text, mask_t *out)
{
    static const char ranks[] = "23456789TJQKA";
    static const char suits[] = "hdcs";
    int count = 0;
    *out = MASK_EMPTY;
    if (!text)
        return 0;
    while (text[0] && text[1])
    {
        const char *r = strchr(ranks, (char)toupper((unsigned char)text[0]));
        const char *u = strchr(suits, (char)tolower((unsigned char)text[1]));
        if (!r || !u)
            return 0;
        *out = mask_set(*out, MODERN_MAKE_CARD((int)(r - ranks), (int)(u - suits)));
        ++count;
        text += 2;
    }
    return count;
}

/* Does this row's runout match the cards picked in the board matrix?
 *
 * A complete pick -- as many cards as the row's board holds -- matches by
 * SUIT ISOMORPHISM, not by spelling: Ks7d2c and Kh7s2d are the same board up
 * to a renaming of the suits, and the solver now keys its infosets that way,
 * so the view must read them the same way.  Without this a run answers only
 * for the exact runouts it happened to deal.
 *
 * A partial pick keeps the literal "contains these cards" meaning, which is
 * the useful reading of half a board and is not isomorphism-invariant
 * anyway (there is no such thing as "the ace of hearts" up to suit
 * renaming). */
static int result_line_board_contains(const char *line, const char *cards,
                                     int level)
{
    char row_board[64];
    mask_t picked = MASK_EMPTY;
    mask_t row_mask = MASK_EMPTY;
    int picked_count;
    int row_count;
    size_t i;

    if (!cards || !*cards)
        return 1;
    if (!result_line_board(line, row_board, sizeof(row_board)))
        return 0;

    picked_count = board_text_to_mask(cards, &picked);
    row_count = board_text_to_mask(row_board, &row_mask);
    if (picked_count > 0 && picked_count == row_count)
    {
        char picked_key[32];
        char row_key[32];
        /* Match by whatever rule the run keyed its infosets with.  With an
         * abstraction on, the solver merged boards by texture, so demanding
         * an exact isomorphic board here would reject nearly every row of a
         * report that does contain the answer. */
        if (level > 0)
            return pe_board_texture_id(picked, (pe_texture_filter_level_t)level) ==
                   pe_board_texture_id(row_mask, (pe_texture_filter_level_t)level);
        if (pe_board_canonical_key(picked, picked_count,
                                   picked_key, sizeof(picked_key)) == 0 &&
            pe_board_canonical_key(row_mask, row_count,
                                   row_key, sizeof(row_key)) == 0)
            return strcmp(picked_key, row_key) == 0;
    }
    for (i = 0u; cards[i] && cards[i + 1u]; i += 2u)
    {
        char card[3];
        card[0] = cards[i];
        card[1] = cards[i + 1u];
        card[2] = '\0';
        if (strstr(row_board, card) == NULL)
            return 0;
    }
    return 1;
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

/* Apply one ev_update to the row it belongs to.
 *
 * The report emits a hand row and its ev_update back to back, so the pairing
 * is positional.  Matching by (hand, node, player) instead looked safe but
 * was not: a hand is sampled at the same node many times over a run, and the
 * search always stopped at the FIRST match, so every later occurrence kept
 * its EV column on "pending" while the first row was overwritten again and
 * again.  On a multi-street tree, where the same hand recurs at every node
 * it reaches, that left most of the table blank. */
static void strategy_table_apply_ev_update_at(App *app, uint32_t row_index,
                                              const char *line)
{
    char hand[128], node[32], actor[32], ev[640];
    StrategyTableRow *row;
    MonkerHandEntry *mhand;
    int fields;
    if (!app || !line || row_index >= app->strategy_row_count)
        return;
    fields = sscanf(line, "ev_update\t%127[^\t]\t%31[^\t]\t%31[^\t]\t%639[^\n]",
                    hand, node, actor, ev);
    if (fields != 4)
        return;
    row = &app->strategy_rows[row_index];
    /* Still verify the pairing: a dropped or reordered line must not write
     * one hand's equity onto another's. */
    if (strcmp(row->hand, hand) != 0 || strcmp(row->node, node) != 0 ||
        strcmp(row->player, actor) != 0)
        return;
    row->action_count = row->action_count > 0u ? row->action_count :
                        parse_action_tokens(ev, row->ev, STRATEGY_TABLE_ACTIONS, 0);
    (void)parse_action_tokens(ev, row->ev, STRATEGY_TABLE_ACTIONS, 0);
    mhand = &app->monker_hands[row_index];
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
            cell->align = pos->col == 0u ||
                          pos->col == 3u + STRATEGY_TABLE_ACTIONS
                              ? ekLEFT : ekCENTER;
            if (pos->col >= 3u &&
                pos->col < 3u + STRATEGY_TABLE_ACTIONS)
            {
                uint32_t action = pos->col - 3u;
                if (action < (uint32_t)node->action_count)
                    snprintf(app->table_cell_text, sizeof(app->table_cell_text),
                             "%.1f%%", stored->bytes[base + action] * 100.0 / 256.0);
                else
                    app->table_cell_text[0] = '\0';
            }
            else if (pos->col == 3u + STRATEGY_TABLE_ACTIONS)
            {
                tree_action_label(node, best, app->table_cell_text,
                                  sizeof(app->table_cell_text));
            }
            else
            switch (pos->col)
            {
            case 0u: snprintf(app->table_cell_text, sizeof(app->table_cell_text), "%s", hand); break;
            case 1u: snprintf(app->table_cell_text, sizeof(app->table_cell_text), "100.0%%"); break;
            case 2u: snprintf(app->table_cell_text, sizeof(app->table_cell_text), "P%d", node->acting_player + 1); break;
            default: app->table_cell_text[0] = '\0'; break;
            }
            cell->text = app->table_cell_text;
            break;
        }
        row = &app->strategy_rows[pos->row];
        cell->align = pos->col == 0u ||
                      pos->col == 3u + STRATEGY_TABLE_ACTIONS
                          ? ekLEFT : ekCENTER;
        if (pos->col >= 3u &&
            pos->col < 3u + STRATEGY_TABLE_ACTIONS)
        {
            uint32_t action = pos->col - 3u;
            if (action < row->action_count)
                snprintf(app->table_cell_text, sizeof(app->table_cell_text), "%s%s%s",
                         row->actions[action], row->ev[action][0] ? " | EV " : "", row->ev[action]);
            else
                app->table_cell_text[0] = '\0';
        }
        else if (pos->col == 3u + STRATEGY_TABLE_ACTIONS)
        {
            app->table_cell_text[0] = '\0';
            for (uint32_t action = 0u; action < row->action_count &&
                 action < STRATEGY_TABLE_ACTIONS; ++action)
            {
                size_t used = strlen(app->table_cell_text);
                snprintf(app->table_cell_text + used,
                         sizeof(app->table_cell_text) - used,
                         "%s%s= %s", action ? "; " : "", row->actions[action],
                         row->ev[action]);
            }
        }
        else
        switch (pos->col)
        {
        case 0u: snprintf(app->table_cell_text, sizeof(app->table_cell_text), "%s", row->hand); break;
        case 1u: snprintf(app->table_cell_text, sizeof(app->table_cell_text), "%s", row->node); break;
        case 2u: snprintf(app->table_cell_text, sizeof(app->table_cell_text), "%s", row->player); break;
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
        {
            int line_node = result_line_node(line);
            int line_street = result_node_street(app, line_node);
            /* line_street < 0 means the node's street is unknown (steps
             * rebuilt from the report carry no street).  Unknown must not
             * mean preflop, or the rows vanish under a street filter. */
            if ((street < 0 || line_street < 0 || street == line_street) &&
                (step_node < 0 || line_node == step_node))
            {
                result_write_line(app->strategy_view, line);
                step_started = 1;
            }
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
        /* Row the next ev_update belongs to, or -1 when the hand row just
         * seen was filtered out (its equity must not land on the row kept
         * before it). */
        int pending_ev_row = -1;
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
            /* The report emits ev_update immediately after its hand row, so
             * it is applied to that row rather than searched for. It must
             * never become a visible row of its own. */
            if (strncmp(line, "ev_update\t", 10u) == 0)
            {
                if (pending_ev_row >= 0)
                    strategy_table_apply_ev_update_at(app, (uint32_t)pending_ev_row,
                                                      line);
                pending_ev_row = -1;
                cursor = end;
                continue;
            }
            if (strncmp(line, "report_phase=", 13u) == 0)
            {
                cursor = end;
                continue;
            }
            {
                char row_board[64];
                row_board[0] = '\0';
                fields = sscanf(line,
                                "%127[^\t]\t%31[^\t]\t%31[^\t]\t%639[^\t]\t%639[^\t]\t%63[^\n]",
                                hand, node, actor, frequencies, ev, row_board);
                /* Six fields since the report gained a per-row board; five is
                 * an older report, which simply has no board to filter on. */
                if (fields >= 5)
                    fields = 5;
                trim_text(row_board);
            }
            if (fields == 5 && strcmp(hand, "hand") != 0 && is_card_hand_text(hand))
            {
                int row_node = atoi(node);
                int row_street = result_node_street(app, row_node);
                const char *row_board_text = strstr(line, "\t");
                (void)row_board_text;
                if ((street < 0 || row_street < 0 || street == row_street) &&
                    (step_node < 0 || row_node == step_node) &&
                    (!board || result_line_board_matches(line, board)) &&
                    result_line_board_contains(line, app->result_board_cards,
                                               app->solve_board_abstraction))
                {
                    pending_ev_row = (int)app->strategy_row_count;
                    strategy_table_add_row(app, line);
                    if ((int)app->strategy_row_count == pending_ev_row)
                        pending_ev_row = -1;   /* row was rejected */
                    else
                    {
                        ++visible_rows;
                        table_started = 1;
                    }
                }
                else
                {
                    pending_ev_row = -1;
                }
            }
            cursor = end;
        }
    }
    if (!table_started)
    {
        /* Say WHY there is nothing, because the usual cause is not a solve
         * that has not converged.  A preflop-rooted run deals a fresh runout
         * every iteration, so pinning cards is brutally selective: one card
         * appears in ~9.6% of deals, two in ~0.75%, three in ~0.045%.  Three
         * picked cards leave a couple of rows out of four thousand. */
        if (app->result_board_cards[0])
            textview_printf(app->strategy_view,
                            "No per-hand rows contain %s.\n"
                            "This is a runout filter, not a convergence problem: each\n"
                            "iteration deals its own board, so one pinned card matches\n"
                            "about 9.6%% of deals, two about 0.75%% and three about 0.05%%.\n"
                            "Pin fewer cards, or solve that exact flop directly with a\n"
                            "flop-rooted tree (SETUP: BOARD + POT AT ROOT).\n",
                            app->result_board_cards);
        else
            result_write_line(app->strategy_view,
                              "No per-hand rows match the selected step.");
    }

    sort_monker_hands(app);
    update_active_action_totals(app);
    /* Nothing is selected until the user clicks a row.  Previewing row 0 and
     * leaving selected_hand_index at 0 made an arbitrary hand look chosen. */
    if (!app->mkr_loaded)
    {
        app->selected_hand_index = -1;
        render_card_preview(app, NULL);
    }
    if (app->strategy_table)
        tableview_update(app->strategy_table);
    if (app->strategy_grid_view)
        view_update(app->strategy_grid_view);
    if (app->poker_table_view)
        view_update(app->poker_table_view);
}

static void i_on_result_filter(App *app, Event *event)
{
    if (app)
    {
        char *output = report_scratch(app);
        uint32_t sel = combo_get_selected(app->step_filter);
        app->active_step_index = sel > 0u ? (int)sel - 1 : -1;
        update_responses_for_active_step(app);
        if (output)
        {
            i_solve_copy_output(app, output, sizeof(app->solve_output));
            render_strategy_view(app, output);
        }
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
    if (strncmp(line, "interactive=", 12u) == 0)
    {
        /* NO LOCKING HERE.  i_solve_scan_line is called with solve_mutex
         * already held, and every other field it touches is written bare for
         * that reason.  Taking the lock again deadlocked the reader thread
         * while it HELD the mutex, so the UI thread blocked forever in
         * i_solve_update and the whole window froze the instant this marker
         * arrived -- that is, right after the results appeared.
         *
         * A stopped run serves too: with no iteration cap, stopping is the
         * only way a run ever ends, so refusing to serve after a stop put
         * board queries out of reach entirely. */
        app->solve_serving = (line[12] == '1');
        if (app->solve_serving)
            app->solve_cancel_requested = 0;
        return;
    }
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

    /* The solver prints "solver_phase=complete stop_reason=<target|
     * max_iterations> report=starting" at the end of every run.  Capture
     * the verdict so i_solve_end can tell "target reached" apart from
     * "safety cap reached" in exploitability-target mode. */
    {
        const char *marker = "stop_reason=";
        const char *found = strstr(line, marker);
        if (found != NULL)
        {
            char reason[32];
            size_t length = 0u;
            found += strlen(marker);
            while (found[length] != '\0' && found[length] != ' ' &&
                   found[length] != '\n' && found[length] != '\r' &&
                   length + 1u < sizeof(reason))
            {
                reason[length] = found[length];
                ++length;
            }
            reason[length] = '\0';
            if (length > 0u)
                snprintf(app->solve_stop_reason, sizeof(app->solve_stop_reason),
                         "%s", reason);
            /* solve_run_iterations was declared and read in four places but
             * never assigned, so every message that quoted it said "0
             * iterations".  The last progress line is what the run reached. */
            app->solve_run_iterations = app->telemetry_iteration;
        }
    }

    /* The solver's stop_detail line carries the cause the loop itself named,
     * plus the footprint it held.  Keep it whole: when a run ends on its own
     * this single line is the answer to "why". */
    if (strstr(line, "stop_detail ") != NULL)
        snprintf(app->solve_stop_detail, sizeof(app->solve_stop_detail),
                 "%s", line);

    /* The solver logs "checkpoint_saved=1 path=<file>" whenever it writes
     * a checkpoint.  Capture the file path so the Resume button can be
     * offered without the user having to type anything. */
    if (strstr(line, "checkpoint_saved=1") != NULL)
    {
        const char *path_eq = strstr(line, "path=");
        if (path_eq != NULL)
        {
            const char *path_start = path_eq + 5u;
            size_t path_len = 0u;
            while (path_start[path_len] != '\0' &&
                   path_start[path_len] != ' ' &&
                   path_start[path_len] != '\n' &&
                   path_start[path_len] != '\r' &&
                   path_len + 1u < sizeof(app->solve_checkpoint_path))
            {
                app->solve_checkpoint_path[path_len] = path_start[path_len];
                ++path_len;
            }
            if (path_len > 0u)
            {
                app->solve_checkpoint_path[path_len] = '\0';
                app->solve_checkpoint_available = 1;
            }
        }
        else
        {
            /* No path= field: the solver wrote to the --checkpoint path
             * we passed in; trust the command-line contract and enable
             * Resume whenever a checkpoint was emitted. */
            app->solve_checkpoint_available = 1;
        }
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
     * short probe carried between calls.
     *
     * The search runs on every chunk, not just until the first report.  Each
     * board query prints a fresh "STRATEGY REPORT", and latching the capture
     * on the first one appended every later report behind it: the window
     * below takes the FIRST "HAND TABLE" it finds, so the grid kept showing
     * the opening report while the buffer filled with answers nobody saw --
     * and once it was full, every new one was dropped in silence.  Restarting
     * at each marker keeps the latest report, which is the one on screen. */
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
            app->strategy_capture_truncated = 0;
            app->strategy_marker_probe_length = 0u;
        }
        else if (app->strategy_capture_started)
        {
            if (app->strategy_output_length < sizeof(app->strategy_output) - 1u)
            {
                size_t remaining = sizeof(app->strategy_output) - 1u -
                                   app->strategy_output_length;
                size_t captured = length < remaining ? length : remaining;
                memcpy(app->strategy_output + app->strategy_output_length,
                       data, captured);
                app->strategy_output_length += captured;
                app->strategy_output[app->strategy_output_length] = '\0';
                if (captured < length)
                    app->strategy_capture_truncated = 1;
            }
            else
                app->strategy_capture_truncated = 1;
            /* A marker split across this chunk and the next still has to be
             * found, so keep probing while capturing. */
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
    /* A serving solver has already finished its work and is only waiting for
     * queries: ask it to quit so it exits cleanly, rather than signalling it
     * as though a solve were being interrupted. */
    if (i_solve_send(app, "quit\n") == 0)
    {
        bmutex_lock(app->solve_mutex);
        app->solve_serving = 0;
    app->solve_view_total = 0u;
    app->solve_view_serving_applied = 0;
        bmutex_unlock(app->solve_mutex);
        return;
    }
    int escalate;
    /* Decide under the lock, act OUTSIDE it.  bproc_terminate and especially
     * bproc_cancel touch the process and its pipes and can block; holding
     * solve_mutex across them deadlocked all three parties: the reader thread
     * waited for the mutex inside i_solve_append_output, so it stopped
     * draining the pipe, so the solver blocked writing to a full pipe, so the
     * process call never returned.  The UI froze with the solver asleep at
     * 0% CPU. */
    bmutex_lock(app->solve_mutex);
    app->solve_cancel_requested = 1;
    proc = app->solve_proc;
    escalate = app->solve_terminate_sent;
    if (proc && !escalate)
        app->solve_terminate_sent = 1;
    bmutex_unlock(app->solve_mutex);

    /* First click: SIGTERM so the solver can flush a checkpoint and a partial
     * report.  A subsequent click escalates when it has not exited yet. */
    if (proc)
    {
        if (!escalate)
            (void)bproc_terminate(proc);
        else
            (void)bproc_cancel(proc);
    }
    button_text(app->solve_button,
                app->solve_terminate_sent ? "Force kill" : "Stopping...");
    if (app->setup_run_state)
        label_text(app->setup_run_state, "STOPPING");
    if (app->setup_run_progress)
        label_text(app->setup_run_progress,
                   app->solve_terminate_sent
                       ? "Waiting for safe point (force kill pending)..."
                       : "Waiting for the solver safe point...");
    status(app,
           app->solve_terminate_sent
               ? "STOPPING (force)\nThe solver did not exit gracefully; sending SIGKILL."
               : "STOPPING\nThe solver is being stopped at the current safe point...");
}

static uint32_t i_solve_main(App *app)
{
    Proc *proc;
    perror_t error = ekPOK;
    byte_t buffer[2048];
    uint32_t read_size = 0u;
    uint32_t exit_code = 1u;

    proc = bproc_exec(app->solve_command, &error);
    {
        /* Same rule here: never call into the process while holding the
         * mutex the reader thread needs. */
        int cancel_now;
        bmutex_lock(app->solve_mutex);
        app->solve_proc = proc;
        cancel_now = proc && app->solve_cancel_requested;
        bmutex_unlock(app->solve_mutex);
        if (cancel_now)
            (void)bproc_cancel(proc);
    }
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
    /* These feed update_result_view, which builds the hand table.  A 64 KB
     * stack copy silently kept only the tail of a multi-megabyte report, so
     * the table showed a sliver of the run whatever the caps allowed. */
    char *output;
    int running;
    int capturing = 0;
    size_t strat_len = 0u;
    uint64_t iteration = 0u;
    uint64_t total = 0u;
    double fraction = 0.0;
    double exploitability = 0.0;
    double target = 0.0;
    int report_phase = 0;
    if (!app)
        return;
    int serving;
    bmutex_lock(app->solve_mutex);
    running = app->solve_running;
    serving = app->solve_serving;
    capturing = app->strategy_capture_started;
    strat_len = app->strategy_output_length;
    if (running)
        ++app->solve_update_count;
    bmutex_unlock(app->solve_mutex);
    /* The process is still up to answer board queries, but the solve itself
     * is over: present it as finished so the user is not left staring at a
     * progress bar that will never move again. */
    if (serving)
        running = 0;
    /* Nothing new from a process that is only waiting for queries: there is
     * nothing to redraw, and redrawing anyway is what froze the UI. */
    {
        size_t total;
        bmutex_lock(app->solve_mutex);
        total = app->solve_output_total;
        bmutex_unlock(app->solve_mutex);
        if (serving && total == app->solve_view_total &&
            app->solve_view_serving_applied)
            return;
        app->solve_view_total = total;
    }
    output = report_scratch(app);
    if (!output)
        return;
    {
        /* Tick profiling for the UI freeze hunt.  Only slow ticks are
         * reported, so the log stays readable; set PE_STUDIO_TICK_LOG=all to
         * see every one. */
        const char *tick_log = getenv("PE_STUDIO_TICK_LOG");
        clock_t t0 = tick_log ? clock() : 0;
        clock_t t1, t2, t3;
        i_solve_copy_output(app, output, sizeof(app->solve_output));
        t1 = tick_log ? clock() : 0;
        update_result_view(app, output, running);
        t2 = tick_log ? clock() : 0;
        update_strategy_view(app, output);
        t3 = tick_log ? clock() : 0;
        if (tick_log)
        {
            double copy_ms = 1000.0 * (double)(t1 - t0) / CLOCKS_PER_SEC;
            double result_ms = 1000.0 * (double)(t2 - t1) / CLOCKS_PER_SEC;
            double strat_ms = 1000.0 * (double)(t3 - t2) / CLOCKS_PER_SEC;
            double total_ms = copy_ms + result_ms + strat_ms;
            if (total_ms > 50.0 || strcmp(tick_log, "all") == 0)
            {
                fprintf(stdout,
                        "TICK serving=%d running=%d bytes=%zu copy=%.1fms "
                        "result=%.1fms strategy=%.1fms total=%.1fms\n",
                        serving, running, app->solve_view_total,
                        copy_ms, result_ms, strat_ms, total_ms);
                fflush(stdout);
            }
        }
    }
    if (serving)
    {
        app->solve_view_serving_applied = 1;
        /* The button still stops the process, so it must not read "Solve
         * this spot": while serving, solve_running is set and a click goes
         * to the stop path. */
        button_text(app->solve_button, "Release solver");
        label_text(app->run_state, "READY FOR BOARD QUERIES");
        label_text(app->setup_run_state, "READY FOR BOARD QUERIES");
        {
            int truncated;
            bmutex_lock(app->solve_mutex);
            truncated = app->strategy_capture_truncated;
            bmutex_unlock(app->solve_mutex);
            /* Say WHY the run ended.  "SOLVE COMPLETE" alone reads as "it
             * finished", which is wrong for a run that was still going and
             * stopped on a budget, a signal or a traversal failure. */
            status(app,
                   "SOLVE COMPLETE — SOLVER HELD OPEN FOR QUERIES\n"
                   "Ended after %" PRIu64 " iterations, reason: %s.%s\n"
                   "%s\n"
                   "Click 3 to 5 cards in the BOARD MATRIX to ask the solver for that\n"
                   "exact board's hand table; fewer cards just filter what is shown.\n"
                   "Release solver (or Stop run) frees it and ends the session.",
                   app->solve_run_iterations,
                   app->solve_stop_reason[0] ? app->solve_stop_reason
                                             : "not reported",
                   truncated
                       ? "  WARNING: the report did not fit the capture buffer,"
                         " so the hand table below is a slice of it."
                       : "",
                   app->solve_stop_detail);
        }
    }
    else if (running && capturing)
    {
        /* The solver stopped iterating and is streaming the per-hand
         * report (empirical EVs are slow: seconds per row on big plo
         * trees).  Say so explicitly — otherwise the run looks hung and
         * the user clicks Stop again, SIGKILLing the report mid-write
         * and leaving an empty grid (the #1 cause of "no results"). */
        status(app,
               "REPORTING\nstrategy report streaming (%.1f KB received)\n"
               "The grid fills in live. Please wait — do NOT click Stop "
               "again, it would kill the report.",
               (double)strat_len / 1024.0);
    }
    else if (running && last_progress_line(output, &iteration, &total, &fraction,
                                      &exploitability, &target))
    {
        report_phase = total > 0u && total < STUDIO_NO_ITER_CAP && iteration >= total;
        if (total >= STUDIO_NO_ITER_CAP)
            status(app,
                   "%s\niteration=%" PRIu64 " (no cap)\nexploitability=%.2f mBB / target=%.2f mBB\n\n"
                   "%s",
                   report_phase ? "REPORTING" : "SOLVING",
                   iteration, exploitability, target,
                   "Live result table is updating. Click Stop solve to interrupt.");
        else
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
    char *output;
    int cancelled;
    int has_checkpoint;
    if (!app)
        return;
    bmutex_lock(app->solve_mutex);
    cancelled = app->solve_cancel_requested;
    has_checkpoint = app->solve_checkpoint_available;
    app->solve_running = 0;
    app->solve_cancel_requested = 0;
    app->solve_terminate_sent = 0;
    app->solve_serving = 0;
    app->solve_resume_mode = 0;
    bmutex_unlock(app->solve_mutex);
    /* NOTE: output must be copied and the result/strategy views refreshed
     * in every branch — otherwise the Results panel stays empty and the
     * status shows "No output from solver." even on exit_code=0. */
    output = report_scratch(app);
    if (!output)
        return;
    i_solve_copy_output(app, output, sizeof(app->solve_output));
    update_result_view(app, output, 0);
    update_strategy_view(app, output);
    /* Temporary end-of-run diagnostics: what did the views actually
     * receive?  Written to the status and dumped to
     * /tmp/studio_last_output.txt for post-mortem analysis. */
    {
        char diag[320];
        char gg[32] = "";
        double raw = 0.0, mbb = 0.0, df = 0.0, de = 0.0, dg = 0.0;
        uint64_t di = 0u, dt = 0u, ds = 0u;
        size_t out_len, strat_len, solve_len;
        int tv, fv, hp, hf;
        FILE *dump;
        bmutex_lock(app->solve_mutex);
        strat_len = app->strategy_output_length;
        solve_len = strlen(app->solve_output);
        tv = app->telemetry_valid;
        fv = app->final_metrics_valid;
        bmutex_unlock(app->solve_mutex);
        out_len = strlen(output);
        hp = last_progress_line(output, &di, &dt, &df, &de, &dg);
        hf = last_result_line(output, gg, sizeof(gg), &raw, &mbb, &ds);
        dump = fopen("/tmp/studio_last_output.txt", "w");
        if (dump)
        {
            fwrite(output, 1u, out_len, dump);
            fclose(dump);
        }
        snprintf(diag, sizeof(diag),
                 "[diag out=%zu strat=%zu solve=%zu tv=%d fv=%d prog=%d final=%d "
                 "reason=%s mode=%u rows=%u hands=%u]",
                 out_len, strat_len, solve_len, tv, fv, hp, hf,
                 app->solve_stop_reason, app->solve_stop_mode,
                 app->strategy_row_count, app->monker_hand_count);
        snprintf(app->solve_diag_line, sizeof(app->solve_diag_line), "%s", diag);
    }
    /* A checkpoint is only worth offering as "Resume" when continuing is
     * meaningful.  A clean finish at the requested stop condition (mode 0
     * max reached, or mode 1 target reached) would resume into a no-op —
     * re-running an already-satisfied command — so fall back to a fresh
     * solve there.  Resume stays for: manual stops, and mode-1 runs that
     * hit the per-run iteration cap before reaching the target. */
    if (!cancelled && has_checkpoint)
    {
        if ((app->solve_stop_mode == 0u &&
             strcmp(app->solve_stop_reason, "max_iterations") == 0) ||
            (app->solve_stop_mode == 1u &&
             strcmp(app->solve_stop_reason, "target") == 0))
        {
            bmutex_lock(app->solve_mutex);
            app->solve_checkpoint_available = 0;
            bmutex_unlock(app->solve_mutex);
            has_checkpoint = 0;
        }
    }
    if (has_checkpoint && app->solve_checkpoint_path[0] != '\0' &&
        (cancelled || app->solve_stop_mode == 1u || app->solve_stop_mode == 2u ||
         app->solve_stop_reason[0] == '\0'))
    {
        if (!cancelled && app->solve_stop_mode == 1u &&
            strcmp(app->solve_stop_reason, "max_iterations") == 0)
        {
            button_text(app->solve_button, "Resume this spot");
            status(app, "TARGET NOT REACHED\nexit_code=%u\nExploitability %.2f mBB is still above the %.2f mBB target after %" PRIu64 " iterations.\nClick \"Resume this spot\" to continue toward the target.\nCheckpoint: %s\n%s",
                   exit_code, app->final_mbb, app->solve_run_target,
                   app->solve_run_iterations, app->solve_checkpoint_path,
                   output[0] ? output : "No output from solver.");
        }
        else if (!cancelled &&
                 strcmp(app->solve_stop_reason, "error") == 0)
        {
            button_text(app->solve_button, "Resume this spot");
            status(app, "SOLVE ENDED ON A TRAVERSAL ERROR\nexit_code=%u\nA sampled traversal returned a non-finite value after %" PRIu64 " iterations and the run ended there.  What the solve had is kept.\nThe solver's last lines below say at which iteration.\nCheckpoint: %s\n%s",
                   exit_code, app->solve_run_iterations,
                   app->solve_checkpoint_path,
                   output[0] ? output : "No output from solver.");
        }
        else if (!cancelled &&
                 strcmp(app->solve_stop_reason, "memory_budget") == 0)
        {
            /* A run with no iteration cap grows its state space for as long
             * as it runs.  It used to be the kernel that ended those, with
             * everything lost; now the solver stops itself first and the
             * checkpoint is on disk. */
            button_text(app->solve_button, "Resume this spot");
            status(app, "MEMORY BUDGET REACHED\nexit_code=%u\nThe solve stopped itself after %" PRIu64 " iterations rather than be killed for memory; the strategy, the report and the checkpoint are intact.\nTo go further: use a coarser BOARD ABSTRACTION (it bounds how many infosets exist at all), cap the iterations, or raise the budget.\nCheckpoint: %s\n%s",
                   exit_code, app->solve_run_iterations,
                   app->solve_checkpoint_path,
                   output[0] ? output : "No output from solver.");
        }
        else
        {
            button_text(app->solve_button, "Resume this spot");
            status(app, "%s\nexit_code=%u\nA checkpoint was saved to %s; click \"Resume this spot\" to continue, or change Setup / delete the .ckpt for a fresh solve.\n%s\n%s",
                   cancelled ? "SOLVE STOPPED" : exit_code == 0u ? "SOLVE RESULT" : "SOLVE ERROR",
                   exit_code, app->solve_checkpoint_path,
                   output[0] ? output : "No output from solver.",
                   app->solve_diag_line);
        }
    }
    else
    {
        if (!cancelled && app->solve_stop_mode == 1u &&
            strcmp(app->solve_stop_reason, "target") == 0)
        {
            char reached_line[128];
            snprintf(reached_line, sizeof(reached_line),
                     "TARGET REACHED: exploitability %.2f mBB <= target %.2f mBB.\n",
                     app->final_mbb, app->solve_run_target);
            button_text(app->solve_button, "Solve this spot");
            status(app, "SOLVE RESULT\n%s\nexit_code=%u\n%s",
                   reached_line, exit_code,
                   output[0] ? output : "No output from solver.");
        }
        else
        {
            button_text(app->solve_button, "Solve this spot");
            status(app, "%s\nexit_code=%u\n%s",
                   cancelled ? "SOLVE STOPPED" : exit_code == 0u ? "SOLVE RESULT" : "SOLVE ERROR",
                   exit_code, output[0] ? output : "No output from solver.");
        }
    }
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
    /* snprintf with overlapping src/dst is UB; the Resume path used to
     * call i_start_solve(app, app->solve_command). Guard it. */
    if ((const void *)command != (const void *)app->solve_command)
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
    app->solve_stop_reason[0] = '\0';
    app->player_evs_valid = 0;
    for (uint32_t player = 0u; player < MAX_PLAYERS_DISPLAY; ++player)
    {
        app->player_ev_mchip[player] = 0.0;
        app->player_ev_bb100[player] = 0.0;
        if (app->lbl_player_evs[player])
            label_text(app->lbl_player_evs[player], "—");
    }
    app->solve_cancel_requested = 0;
    app->solve_terminate_sent = 0;
    /* A fresh run replaces any prior checkpoint for this spot unless the
     * caller is resuming from it.  In resume mode keep the path as a
     * fallback until the solver re-emits checkpoint_saved=1; the flag is
     * consumed here so the *next* fresh run starts clean. */
    if (!app->solve_resume_mode)
    {
        app->solve_checkpoint_available = 0;
        app->solve_checkpoint_path[0] = '\0';
    }
    app->solve_resume_mode = 0;
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

/* i_rewrite_iterations replaces the first "--iterations <digits>" token in
 * cmd with "--iterations <value>".  Returns 0 on success, -1 when the
 * token is missing or the buffer is too small.  Used by Resume in
 * exploitability-target mode to extend the safety cap so the continued
 * run actually performs new iterations toward the target instead of
 * exiting immediately (already at max). */
static int i_rewrite_iterations(char *cmd, size_t capacity, uint64_t value)
{
    const char *flag = " --iterations ";
    char *found;
    char *digits;
    char *end;
    char replacement[64];
    size_t head_len, tail_len, repl_len;
    int repl;
    if (!cmd || capacity == 0u)
        return -1;
    found = strstr(cmd, flag);
    if (!found)
        return -1;
    digits = found + strlen(flag);
    end = digits;
    while (*end >= '0' && *end <= '9')
        ++end;
    if (end == digits)
        return -1;
    repl = snprintf(replacement, sizeof(replacement), " --iterations %" PRIu64, value);
    if (repl <= 0 || (size_t)repl >= sizeof(replacement))
        return -1;
    repl_len = (size_t)repl;
    head_len = (size_t)(found - cmd);
    tail_len = strlen(end);
    if (head_len + repl_len + tail_len + 1u > capacity)
        return -1;
    memmove(cmd + head_len + repl_len, end, tail_len + 1u);
    memcpy(cmd + head_len, replacement, repl_len);
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
        /* The ICM page has its own spot/calculator tabs.  Refresh the
         * calculator as soon as the top-level page becomes visible so a
         * spot edited in another page is never shown as stale defaults. */
        if (tabs_get_selected(app->tabs) == 4u && app->icm_tabs &&
            tabs_get_selected(app->icm_tabs) == 1u)
        {
            sync_icm_from_spot(app);
            i_on_analysis_icm(app, NULL);
        }
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
    char tree[2048], board[256], runner[2048];
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
    pe_compute_kind_t requested_backend;
    pe_precision_mode_t precision;
    double exponential_lambda;
    double dcfr_alpha;
    double dcfr_beta;
    double dcfr_gamma;
    uint64_t threads;
    char algorithm_options[320];
    char config_text[512];
    char algorithm_axes[192];

    bmutex_lock(app->solve_mutex);
    if (app->solve_running)
    {
        bmutex_unlock(app->solve_mutex);
        i_solve_request_stop(app);
        unref(event);
        return;
    }
    bmutex_unlock(app->solve_mutex);

    /* The same button is reused for "Solve this spot" (fresh run) and
     * "Resume this spot" (continue from the saved checkpoint).  When a
     * checkpoint is available, re-issue the last solve command with
     * --resume <ckpt> appended (before the trailing " 2>&1").
     * Escape hatches for a fresh solve: switching to a different tree
     * (different derived .ckpt) or deleting the .ckpt file falls through
     * to the fresh path below. */
    if (app->solve_checkpoint_available && app->solve_checkpoint_path[0] != '\0' &&
        app->solve_command[0] != '\0')
    {
        char resume_cmd[8192];
        char resume_quoted[1100];
        const char *suffix = " 2>&1";
        size_t base_len;
        {
            char expected_ckpt[1024];
            int fresh_needed = 0;
            if (tree_path && *tree_path &&
                spot_checkpoint_path(tree_path, expected_ckpt,
                                     sizeof(expected_ckpt)) == 0 &&
                strcmp(expected_ckpt, app->solve_checkpoint_path) != 0)
            {
                fresh_needed = 1;
            }
            if (!fresh_needed)
            {
                FILE *probe = fopen(app->solve_checkpoint_path, "rb");
                if (probe)
                    fclose(probe);
                else
                    fresh_needed = 1;
            }
            if (fresh_needed)
            {
                bmutex_lock(app->solve_mutex);
                app->solve_checkpoint_available = 0;
                app->solve_checkpoint_path[0] = '\0';
                app->solve_resume_mode = 0;
                bmutex_unlock(app->solve_mutex);
            }
            else
            {
                snprintf(resume_cmd, sizeof(resume_cmd), "%s", app->solve_command);
        base_len = strlen(resume_cmd);
        {
            size_t suffix_len = strlen(suffix);
            if (base_len >= suffix_len &&
                strcmp(resume_cmd + base_len - suffix_len, suffix) == 0)
            {
                resume_cmd[base_len - suffix_len] = '\0';
                base_len -= suffix_len;
            }
        }
        /* Don't stack --resume flags if the stored command already has one. */
        if (strstr(resume_cmd, "--resume") == NULL &&
            quote_argument(app->solve_checkpoint_path, resume_quoted,
                           sizeof(resume_quoted)) == 0 &&
            strlen(resume_cmd) + strlen(resume_quoted) + 32u < sizeof(resume_cmd))
        {
            snprintf(resume_cmd + strlen(resume_cmd),
                     sizeof(resume_cmd) - strlen(resume_cmd),
                     " --resume %s%s", resume_quoted, suffix);
        }
        else if (strstr(resume_cmd, "--resume") == NULL)
        {
            status(app, "SOLVE ERROR\nCheckpoint path is too long.");
            unref(event);
            return;
        }
        else if (strlen(resume_cmd) + strlen(suffix) + 1u < sizeof(resume_cmd))
        {
            snprintf(resume_cmd + strlen(resume_cmd),
                     sizeof(resume_cmd) - strlen(resume_cmd), "%s", suffix);
        }
        /* Exploitability-target mode: runs carry no iteration cap
         * (STUDIO_NO_ITER_CAP), so normally there is nothing to extend.
         * Only extend when the stored cap is a legacy finite value from
         * an older build; otherwise resume verbatim toward the target. */
        if (app->solve_stop_mode == 1u && app->solve_run_iterations > 0u &&
            app->solve_run_iterations < STUDIO_NO_ITER_CAP)
        {
            uint64_t extended = app->solve_run_iterations * 2u;
            if (extended < app->solve_run_iterations || extended > STUDIO_NO_ITER_CAP)
                extended = STUDIO_NO_ITER_CAP;
            if (i_rewrite_iterations(resume_cmd, sizeof(resume_cmd), extended) == 0)
                app->solve_run_iterations = extended;
        }
        app->solve_resume_mode = 1;
        snprintf(app->solve_command, sizeof(app->solve_command), "%s", resume_cmd);
        if (app->solve_stop_mode == 1u)
            status(app, "RESUMING\nReissuing last solve command with --resume %s.\nContinues toward %.2f mBB (no iteration cap).",
                   app->solve_checkpoint_path, app->solve_run_target);
        else
            status(app, "RESUMING\nReissuing last solve command with --resume %s.",
                   app->solve_checkpoint_path);
        if (i_start_solve(app, resume_cmd) != 0)
            status(app, "SOLVE ERROR\nCould not start the asynchronous solver task.");
        unref(event);
        return;
            }
        }
    }

    if (!tree_path || !*tree_path || read_tree(app, tree_path, &header, &layout) != 0)
    {
        unref(event);
        return;
    }
    if (layout.game == game_sdholdem)
    {
        status(app,
               "SOLVE BLOCKED\n"
               "Short Deck is available for spot setup, ICM and result-matrix display,\n"
               "but the installed solver drivers currently support Hold'em and PLO only.");
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
    algorithm_axes_label(algorithm, algorithm_axes, sizeof(algorithm_axes));
    policy = selected_policy(app);
    backend = selected_backend(app);
    requested_backend = backend;
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
    if (stop_mode != 1u)
        target_mbb = 0.0;
    if (stop_mode != 0u)
    {
        /* Single stop option at a time: target-only (mode 1) and
         * manual-only (mode 2) carry no iteration cap.  The core rejects
         * max_iterations==0, so encode "no cap" as STUDIO_NO_ITER_CAP;
         * the progress views render it as infinity. */
        iterations = STUDIO_NO_ITER_CAP;
    }
    /* Lane B drives preflop AND flop/turn/river roots through the same
     * sampled solver: pe-preflop-solve takes --street/--board/--pot to root
     * the game at a street instead of at the blinds.  Everything else on
     * this path (algorithm, backend, precision, stop rule, checkpoints)
     * behaves identically on every street. */
    if (header.street <= 3u)
    {
        pe_runtime_capabilities_t runtime;
        const pe_runtime_backend_info_t *info;
        char backend_display[96];
        char root_options[512];
        double root_pot = 0.0;
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
            status(app, "SOLVE BLOCKED\nLane B supports 2..6 players and solves the tree from ranges; .mkr import is not used by this path.");
            unref(event);
            return;
        }
        /* Postflop root: the board is the spot, and the pot is an input the
         * tree cannot supply (the binary Monker header only carries
         * committed chips at street zero, and clears them afterwards). */
        root_options[0] = '\0';
        if (header.street > 0u)
        {
            board_cards = card_count(board_text);
            if (board_cards != street_cards((int)header.street))
            {
                status(app,
                       "SOLVE BLOCKED\nExpected %d board cards for %s, received %d.",
                       street_cards((int)header.street),
                       street_name((int)header.street), board_cards);
                unref(event);
                return;
            }
            if (parse_ui_target(app->setup_pot_edit
                                    ? edit_get_text(app->setup_pot_edit) : "",
                                &root_pot) != 0 || root_pot <= 0.0)
            {
                status(app,
                       "SOLVE BLOCKED\nA %s root needs POT AT ROOT (BB): there are no\n"
                       "blinds to post postflop, so the money already in the middle\n"
                       "must be entered.",
                       street_name((int)header.street));
                unref(event);
                return;
            }
            if (quote_argument(board_text, board, sizeof(board)) != 0)
            {
                status(app, "SOLVE ERROR\nBoard text is too long.");
                unref(event);
                return;
            }
            /* first_to_act comes from the tree header; -1 means "unspecified"
             * and the solver defaults to seat 0. */
            (void)snprintf(root_options, sizeof(root_options),
                           " --street %s --board %s --pot %.17g",
                           street_name((int)header.street), board, root_pot);
            if (header.first_to_act >= 0 &&
                header.first_to_act < (int)header.player_count)
            {
                size_t root_len = strlen(root_options);
                (void)snprintf(root_options + root_len,
                               sizeof(root_options) - root_len,
                               " --to-act %d", header.first_to_act);
            }
        }
        if (!preflop_algorithm_supported_ui(algorithm))
        {
            status(app,
                   "SOLVE BLOCKED\n"
                   "Lane B currently supports sampled presets only:\n"
                   "external-mccfr, external-dcfr, outcome-mccfr, external-ecfr.\n"
                   "'%s' is a full-tree/experimental preset and has no Lane B adapter yet.",
                   pe_preset_name(algorithm));
            unref(event);
            return;
        }
        if (pe_runtime_probe(&runtime) != 0)
        {
            status(app, "SOLVE BLOCKED\nCould not inspect runtime compute capabilities.");
            unref(event);
            return;
        }
        if (backend == PE_COMPUTE_AUTO)
        {
            /* For the external-sampling preflop driver the host loop is
             * single-threaded.  Honour the user's thread count rather than
             * always picking the backend with the highest measured
             * single-kernel rate (which is a poor predictor for this driver
             * on small preflop ranges).
             *
             *   threads == 1  -> cpu_ref (OpenMP pool stays dormant, no
             *                    per-batch parallel-for overhead).
             *   threads  > 1  -> cpu_par when available so the thread count
             *                    the user typed is actually used by the
             *                    per-batch kernels. */
            const pe_runtime_backend_info_t *ref = &runtime.backends[PE_COMPUTE_CPU_REF];
            const pe_runtime_backend_info_t *par = &runtime.backends[PE_COMPUTE_CPU_PAR];
            if (threads > 1u && par->runtime_available && par->validated)
            {
                backend = PE_COMPUTE_CPU_PAR;
            }
            else if (ref->runtime_available && ref->validated)
            {
                backend = PE_COMPUTE_CPU_REF;
                threads = 1u;
            }
            else
            {
                backend = pe_runtime_recommended_backend(&runtime);
                if (backend == PE_COMPUTE_AUTO)
                {
                    status(app, "SOLVE BLOCKED\nNo validated CPU/GPU backend is available.");
                    unref(event);
                    return;
                }
            }
        }
        info = &runtime.backends[backend];
        if (!info->runtime_available || !info->validated)
        {
            status(app, "SOLVE BLOCKED\nBackend %s is not usable: %s",
                   pe_compute_kind_name(backend), info->reason);
            unref(event);
            return;
        }
        /* The cpu_ref adapter enforces a single thread (cpu_threads must be 0
         * or 1).  If the user explicitly picked cpu_ref with threads > 1,
         * block the run with a clear message rather than silently clamping
         * (silent clamps make the field look ignored).  The user can either
         * lower CPU threads or switch the backend combo to cpu_par / auto. */
        if (backend == PE_COMPUTE_CPU_REF && threads > 1u)
        {
            status(app,
                   "SOLVE BLOCKED\nBackend %s is single-threaded; CPU threads is %" PRIu64
                   " but cpu_ref can only use 1.\n"
                   "Switch the backend combo to cpu_par (or auto) to use more than one CPU thread.",
                   pe_compute_kind_name(backend), threads);
            unref(event);
            return;
        }
        if (requested_backend == PE_COMPUTE_AUTO)
            snprintf(backend_display, sizeof(backend_display), "auto -> %s",
                     pe_compute_kind_name(backend));
        else
            snprintf(backend_display, sizeof(backend_display), "%s",
                     pe_compute_kind_name(backend));
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
                                "%s --game %s --players %u --tree %s%s"
                                " --iterations %" PRIu64 " --samples 1"
                                " --br-samples 32 --target-mbb %.17g"
                                " --exploitability-interval %" PRIu64
                                " --report-rows %u"
                                " --board-abstraction %s"
                                " --interactive"
                                "%s --threads %" PRIu64,
                                runner, game_name(layout.game),
                                header.player_count, tree, root_options,
                                iterations, target_mbb, interval,
                                (unsigned)STUDIO_REPORT_ROWS,
                                selected_board_abstraction(app),
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

        /* Append --checkpoint <spot>.ckpt (and --resume if applicable).
         * The solver writes its checkpoint next to the tree so a stopped
         * run can be resumed without losing the math state. */
        {
            char ckpt_path[1024];
            char ckpt_quoted[1100];
            if (spot_checkpoint_path(tree_path, ckpt_path, sizeof(ckpt_path)) != 0)
            {
                status(app, "SOLVE ERROR\nCould not derive a checkpoint path for this spot.");
                unref(event);
                return;
            }
            if (quote_argument(ckpt_path, ckpt_quoted, sizeof(ckpt_quoted)) != 0)
            {
                status(app, "SOLVE ERROR\nCheckpoint path is too long.");
                unref(event);
                return;
            }
            used += (size_t)snprintf(command + used, sizeof(command) - used,
                                     " --checkpoint %s", ckpt_quoted);
            if (app->solve_resume_mode && app->solve_checkpoint_available &&
                app->solve_checkpoint_path[0] != '\0')
            {
                char resume_quoted[1100];
                if (quote_argument(app->solve_checkpoint_path, resume_quoted,
                                   sizeof(resume_quoted)) == 0)
                {
                    used += (size_t)snprintf(command + used, sizeof(command) - used,
                                             " --resume %s", resume_quoted);
                }
            }
        }

        (void)snprintf(command + strlen(command), sizeof(command) - strlen(command),
                       " 2>&1");

        /* Set OMP_NUM_THREADS for the spawned shell so any parallel region
         * (per-batch kernel, terminal-eval microbench, future host-side
         * parallel sampling) honors the user-selected thread count.  We
         * prepend it because the inner solver is launched by /bin/bash -c. */
        {
            /* "exec env VAR=..." makes the shell REPLACE itself with the
             * solver.  Without it bash keeps running as a parent (the
             * trailing redirection stops it from exec-ing on its own), so
             * bproc_terminate signalled bash and left the solver orphaned:
             * it kept writing to the pipe, the reader never saw EOF, and the
             * run stayed "running" forever with no way back. */
            char env_prefix[64];
            int prefix_len = snprintf(env_prefix, sizeof(env_prefix),
                                      "exec env OMP_NUM_THREADS=%" PRIu64 " ",
                                      threads);
            size_t cmd_len = strlen(command);
            if (prefix_len > 0 && (size_t)prefix_len + cmd_len + 1u < sizeof(command))
            {
                memmove(command + prefix_len, command, cmd_len + 1u);
                memcpy(command, env_prefix, (size_t)prefix_len);
            }
        }
        snprintf(config_text, sizeof(config_text),
                 "Lane B %s | algorithm %s | %s | stop: %s | target %.2f mBB | max %" PRIu64
                 " | check every %" PRIu64 " | %s / %s / %s / %" PRIu64 " threads"
" | policy %s%s | SIMD %s | boards %s",
                  street_name((int)header.street),
                  pe_preset_name(algorithm), algorithm_axes,
                  stop_mode == 0u ? "iterations"
                  : stop_mode == 1u ? "exploitability only (no iteration cap)"
                  : "manual only (no cap, no target)",
                  target_mbb, iterations, interval,
                 pe_preset_name(algorithm), backend_display,
                 pe_precision_name(precision), threads,
                 policy == PE_POLICY_COUNT ? "preset" : pe_policy_name(policy),
                 fabs(exponential_lambda - 1.0) > 1e-15 ? " (custom lambda)" : "",
                 pe_runtime_simd_name(runtime.simd),
                 selected_board_abstraction(app));
        label_text(app->run_config, config_text);
        {
            const char *parallel_note = (backend == PE_COMPUTE_CPU_PAR)
                ? "OpenMP parallel regions run with the user-selected thread count;\n"
                  "note: the external-sampling host loop is single-threaded (only per-batch\n"
                  "kernels are parallel)."
                : "The external-sampling host loop is single-threaded; per-batch kernels\n"
                  "are scalar.  Pick cpu_par from the backend combo and increase CPU threads\n"
                  "if you want OpenMP inside the per-batch kernels (only profitable on\n"
                  "larger batches).";
            const char *stop_note = (stop_mode == 1u)
                ? "\nStop rule: exploitability target ONLY (no iteration cap) —\n"
                  "runs until empirical exploitability <= target or you click Stop run."
                : (stop_mode == 2u)
                    ? "\nStop rule: manual only — runs until you click Stop run."
                    : "";
            /* A tree may span several streets: when the round-closing
             * action of one street wires into a player node on the next, the
             * board is dealt and the tree carries on there.  Only nodes on a
             * street EARLIER than the root are unreachable -- play never
             * walks backwards. */
            char scope_note[256];
            char root_note[192];
            uint32_t unreachable = 0u;
            scope_note[0] = '\0';
            for (uint32_t street = 0u; street < header.street; ++street)
                unreachable += app->tree_street_nodes[street];
            if (unreachable > 0u)
            {
                snprintf(scope_note, sizeof(scope_note),
                         "\nSCOPE WARNING: this tree holds %u decision node(s) on a "
                         "street before the %s root; play never walks backwards, so "
                         "those are unreachable.",
                         unreachable, street_name((int)header.street));
            }
            if (header.street > 0u)
                snprintf(root_note, sizeof(root_note),
                         "Board %s is dead; pot %.2f BB and the stacks are taken as "
                         "remaining (no blinds postflop). Later streets are dealt "
                         "through river.",
                         board_text && *board_text ? board_text : "(none)", root_pot);
            else
                snprintf(root_note, sizeof(root_note),
                         "Empty ranges are 100%%; boards are dealt through river.");
            status(app,
                   "SOLVING %s\n%s\n\nAlgorithm: %s\nAxes: %s\nBackend: %s\n"
                   "SIMD detected: %s (CFR traversal scalar)\nOMP_NUM_THREADS=%" PRIu64 "\n"
                   "%s%s%s\n%s",
                   street_name_upper((int)header.street),
                   command, pe_preset_name(algorithm), algorithm_axes,
                   backend_display, pe_runtime_simd_name(runtime.simd),
                   threads, parallel_note, stop_note, scope_note, root_note);
        }
        app->solve_has_resolved = 1;
        app->solve_board_abstraction = (int)combo_get_selected(
            app->board_abstraction_combo);
        app->solve_resolved_threads = (int)threads;
        snprintf(app->solve_resolved_backend, sizeof(app->solve_resolved_backend),
                 "%s", pe_compute_kind_name(backend));
        app->solve_stop_mode = stop_mode;
        app->solve_run_iterations = iterations;
        app->solve_run_target = target_mbb;
        if (i_start_solve(app, command) != 0)
            status(app, "SOLVE ERROR\nCould not start the asynchronous solver task.");
        unref(event);
        return;
    }
    /* Streets past river do not exist: the header is malformed or the
     * file is not a spot tree. */
    status(app,
           "SOLVE BLOCKED\nThis tree declares street %u, which is not a\n"
           "playable root (expected preflop, flop, turn or river).",
           header.street);
    unref(event);
}

static Panel *i_setup_panel(App *app)
{
    Panel *panel = panel_create();
    Panel *form_panel = panel_create();
    Layout *root = layout_create(1, 1);
    Layout *layout = layout_create(2, 32);
    Label *title = label_create();
    Label *game_label = label_create();
    Label *players_label = label_create();
    Label *tree_label = label_create();
    Label *mkr_label = label_create();
    Label *board_label = label_create();
    Label *pot_label = label_create();
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
    Label *abstraction_label = label_create();
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
    app->setup_pot_edit = edit_create();
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
    app->board_abstraction_combo = combo_create();
    app->stop_mode_combo = combo_create();
    app->board_label = board_label;
    app->setup_run_state = label_create();
    app->setup_run_progress = label_create();
    app->setup_run_metrics = label_create();
    app->setup_progress_bar = progress_create();
    label_text(title, "SOLVER SETUP | tree, ranges and run");
    label_text(game_label, "GAME");
    label_text(players_label, "PLAYERS");
    label_text(tree_label, ".TREE");
    label_text(mkr_label, ".MKR (optional)");
    label_text(board_label, "BOARD (tree street)");
    label_text(pot_label, "POT AT ROOT (BB, postflop trees)");
    label_text(range0_label, "RANGE PLAYER 1");
    label_text(range1_label, "RANGE PLAYER 2");
    label_text(runner_label, "SOLVER DRIVER");
    label_text(algorithm_label, "ALGORITHM");
    label_text(policy_label, "REGRET POLICY");
    label_text(backend_label, "COMPUTE BACKEND");
    label_text(precision_label, "PRECISION");
    label_text(abstraction_label, "BOARD ABSTRACTION");
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
    combo_add_elem(app->game_combo, "Short Deck NL", NULL);
    combo_add_elem(app->game_combo, "PLO4", NULL);
    combo_add_elem(app->game_combo, "PLO5", NULL);
    combo_add_elem(app->game_combo, "PLO6", NULL);
    combo_selected(app->game_combo, 2u);
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
            if (info->runtime_available && info->validated)
                snprintf(backend_text, sizeof(backend_text), "%s (available, validated)",
                         pe_compute_kind_name((pe_compute_kind_t)backend));
            else
                snprintf(backend_text, sizeof(backend_text), "%s (unavailable: %s)",
                         pe_compute_kind_name((pe_compute_kind_t)backend),
                         info->reason[0] != '\0' ? info->reason : "not validated");
            combo_add_elem(app->backend_combo, backend_text, NULL);
        }
    }
    combo_selected(app->backend_combo, PE_COMPUTE_AUTO);
    combo_add_elem(app->precision_combo, "f64", NULL);
    combo_add_elem(app->precision_combo, "f32", NULL);
    combo_add_elem(app->precision_combo, "mixed", NULL);
    combo_add_elem(app->precision_combo, "fixed16", NULL);
    combo_selected(app->precision_combo, PE_PREC_F64);
    /* Ordered finest first, so the useful choice sits next to "exact".
     * small/medium/large ignore board ranks entirely -- 2, 3 and 7 classes
     * for all 22100 flops -- so they merge a king-high board with a nine-high
     * one.  Detailed keeps the ranks (366 flop classes) and is the level to
     * pick when the board has to be played. */
    combo_add_elem(app->board_abstraction_combo, "None (exact boards)", NULL);
    combo_add_elem(app->board_abstraction_combo, "Detailed (366 flop classes)", NULL);
    combo_add_elem(app->board_abstraction_combo, "Large (7 classes, no ranks)", NULL);
    combo_add_elem(app->board_abstraction_combo, "Medium (3 classes, no ranks)", NULL);
    combo_add_elem(app->board_abstraction_combo, "Small (2 classes, no ranks)", NULL);
    combo_selected(app->board_abstraction_combo, 0u);
    button_text(browse_tree, "Browse...");
    button_text(browse_mkr, "Browse...");
    button_text(load, "Load and inspect tree");
    button_text(solve, "Solve this spot");
    button_text(stop, "Stop run");
    combo_add_elem(app->stop_mode_combo, "Max iterations (hard stop)", NULL);
    combo_add_elem(app->stop_mode_combo, "Exploitability target (mBB)", NULL);
    combo_add_elem(app->stop_mode_combo, "Run forever (manual stop)", NULL);
    /* A new desktop run must finish deterministically unless the user opts
     * into a target-based run. A 1 mBB target is not expected to be reached by
     * the empirical Lane B estimate on a fresh tree, which previously left
     * users waiting while believing that 1,000 iterations was the target. */
    combo_selected(app->stop_mode_combo, 0u);
    edit_phtext(app->tree_edit, "/path/to/spot.tree");
    edit_phtext(app->mkr_edit, "/path/to/strategy.mkr");
    edit_phtext(app->board_edit, "No board (preflop: automatic)");
    /* Postflop roots carry no blinds: the money already in the middle is
     * an input, and the binary Monker header only stores committed chips
     * at street zero. Preflop runs ignore it (blinds build the pot). */
    edit_phtext(app->setup_pot_edit, "Preflop: unused (blinds build the pot)");
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
    combo_OnSelect(app->players_combo,
                   listener(app, i_on_setup_players_select, App));
    combo_OnSelect(app->game_combo,
                   listener(app, i_on_setup_state_change, App));
    edit_OnChange(app->board_edit,
                  listener(app, i_on_setup_state_change, App));
    edit_OnChange(app->setup_pot_edit,
                  listener(app, i_on_setup_state_change, App));
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
    layout_label(layout, pot_label, 1, 7);
    layout_edit(layout, app->setup_pot_edit, 1, 8);
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
    layout_label(layout, abstraction_label, 0, 30);
    layout_combo(layout, app->board_abstraction_combo, 0, 31);

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
    layout_vmargin(layout, 29, 8);
    panel_layout(form_panel, layout);
    layout_panel(root, form_panel, 0, 0);
    layout_margin(root, 10.0f);
    panel_layout(panel, root);
    return panel;
}

static Panel *i_result_panel(App *app)
{
    Panel *panel = panel_create();
    Layout *root_layout = layout_create(2, 1);
    Panel *left_panel = panel_create();
    Panel *right_panel = panel_create();
    Layout *left_layout = layout_create(1, 14);
    Layout *right_layout = layout_create(1, 6);

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

    for (uint32_t a = 0u; a < STRATEGY_RESPONSE_ACTIONS; ++a)
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
        Layout *hdr = layout_create(6, 1);
        layout_label(hdr, app->strategy_scope, 0, 0);
        /* The caption explains what the preview beside it is showing. It was
           built and updated but never placed, so the preview had no label and
           the control was never destroyed with the window. */
        layout_label(hdr, app->cards_caption, 1, 0);
        layout_view(hdr, app->hand_preview_view, 2, 0);
        layout_combo(hdr, app->street_filter, 3, 0);
        layout_combo(hdr, app->step_filter, 4, 0);
        layout_combo(hdr, app->board_filter, 5, 0);
        layout_hsize(hdr, 0, 240.0f);
        layout_hsize(hdr, 1, 250.0f);
        layout_hsize(hdr, 2, 150.0f);
        layout_hsize(hdr, 3, 130.0f);
        layout_hsize(hdr, 4, 240.0f);
        layout_hsize(hdr, 5, 220.0f);
        layout_hmargin(hdr, 0, 6.0f);
        layout_hmargin(hdr, 1, 6.0f);
        layout_hmargin(hdr, 2, 6.0f);
        layout_hmargin(hdr, 3, 6.0f);
        layout_hmargin(hdr, 4, 6.0f);
        layout_layout(right_layout, hdr, 0, 0);
    }

    /* Strategy Container (Grid / Table / Raw Log switchable) */
    {
        Panel *container = panel_create();
        Layout *playout0 = layout_create(1, 1);
        Layout *playout1 = layout_create(1, 1);
        Layout *playout2 = layout_create(1, 1);

        tableview_OnData(app->strategy_table, listener(app, i_on_strategy_table, App));
        for (uint32_t column = 0u;
             column < 3u + STRATEGY_TABLE_ACTIONS + 1u; ++column)
            tableview_add_column_text(app->strategy_table);
        tableview_header_title(app->strategy_table, 0u, "Hand");
        tableview_header_title(app->strategy_table, 1u, "Node");
        tableview_header_title(app->strategy_table, 2u, "Player");
        for (uint32_t column = 0u; column < STRATEGY_TABLE_ACTIONS; ++column)
            tableview_header_title(app->strategy_table, 3u + column, "—");
        tableview_header_title(app->strategy_table,
                               3u + STRATEGY_TABLE_ACTIONS, "EV by action");
        tableview_column_width(app->strategy_table, 0u, 118.f);
        tableview_column_width(app->strategy_table, 1u, 58.f);
        tableview_column_width(app->strategy_table, 2u, 62.f);
        for (uint32_t column = 0u; column < STRATEGY_TABLE_ACTIONS; ++column)
            tableview_column_width(app->strategy_table, 3u + column, 150.f);
        tableview_column_width(app->strategy_table,
                               3u + STRATEGY_TABLE_ACTIONS, 310.f);
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

    /* The run configuration line and the progress bar were both updated from
       the solve callbacks but never placed in a layout: the user could not see
       either, and NAppGUI never owned them, so they outlived the window. */
    {
        Layout *runline = layout_create(2, 1);
        layout_label(runline, app->run_config, 0, 0);
        layout_progress(runline, app->run_progress_bar, 1, 0);
        layout_hsize(runline, 0, 700.0f);
        layout_hsize(runline, 1, 320.0f);
        layout_hmargin(runline, 0, 12.0f);
        layout_layout(right_layout, runline, 0, 4);
    }

    textview_editable(app->status, FALSE);
    textview_wrap(app->status, TRUE);
    textview_printf(app->status, "Choose a .tree file, inspect it, then solve the spot.\n");
    layout_textview(right_layout, app->status, 0, 5);

    layout_hsize(right_layout, 0, 1040.0f);
    layout_vsize(right_layout, 0, 36.0f);
    layout_vsize(right_layout, 1, 540.0f);
    layout_vsize(right_layout, 2, 34.0f);
    layout_vsize(right_layout, 3, 44.0f);
    layout_vsize(right_layout, 4, 24.0f);
    layout_vsize(right_layout, 5, 80.0f);
    layout_vmargin(right_layout, 0, 4.0f);
    layout_vmargin(right_layout, 1, 6.0f);
    layout_vmargin(right_layout, 2, 6.0f);
    layout_vmargin(right_layout, 3, 4.0f);
    layout_vmargin(right_layout, 4, 4.0f);
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


/* ------------------------------------------------------------------ *
 * ANALYSIS tab
 *
 * Two calculators that answer questions the solver cannot: what a range is
 * worth against another one right now, what a range actually hit on a board,
 * and what a tournament stack is worth in money rather than chips.
 *
 * Nothing here computes. Every button reads the fields, calls into
 * pe_analysis_model.c and renders whatever comes back -- including the
 * refusal, because these inputs are typed by hand and a blank result is
 * indistinguishable from a wrong one.
 * ------------------------------------------------------------------ */

static enum_game_t i_analysis_game(const App *app)
{
    switch (combo_get_selected(app->analysis_game_combo))
    {
    case 1:  return game_omaha;
    case 2:  return game_omaha8;
    case 3:  return game_holdem8;
    case 4:  return game_7stud;
    case 0:
    default: return game_holdem;
    }
}

static int i_analysis_player_count(const App *app)
{
    uint32_t index = combo_get_selected(app->analysis_players_combo);
    int count = (int)index + 2;
    if (count < 2)
        count = 2;
    if (count > PE_ANALYSIS_MAX_PLAYERS)
        count = PE_ANALYSIS_MAX_PLAYERS;
    return count;
}

static void i_on_analysis_players(App *app, Event *event)
{
    int count = i_analysis_player_count(app);
    int player;
    unref(event);
    /* Ranges past the player count stay visible but are clearly inert, which
       is less surprising than widgets appearing and disappearing. */
    for (player = 0; player < PE_ANALYSIS_MAX_PLAYERS; ++player)
        edit_editable(app->analysis_range_edit[player], player < count);
}

static void i_on_analysis_equity(App *app, Event *event)
{
    pe_analysis_equity_request_t request;
    pe_analysis_equity_report_t report;
    const char *iterations_text;
    int player;

    unref(event);
    memset(&request, 0, sizeof(request));
    request.game = i_analysis_game(app);
    request.player_count = i_analysis_player_count(app);
    for (player = 0; player < request.player_count; ++player)
        request.ranges[player] = edit_get_text(app->analysis_range_edit[player]);
    request.board = edit_get_text(app->analysis_board_edit);
    request.dead = edit_get_text(app->analysis_dead_edit);
    iterations_text = edit_get_text(app->analysis_iterations_edit);
    request.iterations = iterations_text != NULL ? atol(iterations_text) : 0;

    textview_clear(app->analysis_equity_view);
    if (pe_analysis_equity(&request, &report) != 0)
    {
        textview_printf(app->analysis_equity_view, "Cannot compute: %s\n",
                        report.error);
        return;
    }
    textview_printf(app->analysis_equity_view,
                    "%-4s %9s %9s %9s %9s\n", "", "EQUITY", "WIN", "TIE",
                    "COMBOS");
    for (player = 0; player < report.player_count; ++player)
        textview_printf(app->analysis_equity_view,
                        "P%-3d %8.2f%% %8.2f%% %8.2f%% %9zu\n",
                        player + 1,
                        report.equity[player] * 100.0,
                        report.win[player] * 100.0,
                        report.tie[player] * 100.0,
                        report.combos[player]);
    /* "samples" from the engine counts matchups after card removal, not board
       draws; saying so stops a correct answer from looking under-sampled. */
    textview_printf(app->analysis_equity_view,
                    "\n%ld matchups evaluated (%s)\n", report.samples,
                    report.exact ? "exact enumeration"
                                 : "engine heuristic");
}

static void i_on_analysis_breakdown(App *app, Event *event)
{
    pe_analysis_breakdown_t report;
    int i;

    unref(event);
    textview_clear(app->analysis_breakdown_view);
    if (pe_analysis_breakdown(i_analysis_game(app),
                              edit_get_text(app->analysis_range_edit[0]),
                              edit_get_text(app->analysis_board_edit),
                              edit_get_text(app->analysis_dead_edit),
                              &report) != 0)
    {
        textview_printf(app->analysis_breakdown_view, "Cannot classify: %s\n",
                        report.error);
        return;
    }
    textview_printf(app->analysis_breakdown_view,
                    "Player 1 range on this board: %zu combos live",
                    report.live_combos);
    if (report.blocked_combos != 0u)
        textview_printf(app->analysis_breakdown_view, ", %zu blocked",
                        report.blocked_combos);
    textview_printf(app->analysis_breakdown_view, "\n\n");
    for (i = 0; i < PE_HAND_CLASS_COUNT; ++i)
    {
        char bar[27];
        int filled = (int)(report.share[i] * 25.0 + 0.5);
        int c;
        if (report.weight[i] <= 0.0)
            continue;
        for (c = 0; c < 25; ++c)
            bar[c] = c < filled ? '#' : '.';
        bar[25] = '\0';
        textview_printf(app->analysis_breakdown_view, "%-15s %6.2f%%  %s\n",
                        pe_hand_class_name((pe_hand_class_t)i),
                        report.share[i] * 100.0, bar);
    }
}

static void i_on_analysis_icm(App *app, Event *event)
{
    pe_analysis_icm_request_t request;
    pe_analysis_icm_report_t report;
    const char *mode_name;
    int i;

    unref(event);
    memset(&request, 0, sizeof(request));
    request.stacks = edit_get_text(app->analysis_stacks_edit);
    request.payouts = edit_get_text(app->analysis_payouts_edit);
    request.mode = app->icm_mode_combo != NULL
        ? (pe_analysis_tournament_mode_t)combo_get_selected(app->icm_mode_combo)
        : PE_ANALYSIS_TOURNAMENT_ICM;
    request.fgs_depth = app->icm_fgs_depth_edit != NULL
        ? atoi(edit_get_text(app->icm_fgs_depth_edit)) : 0;
    request.fgs_pot = app->icm_fgs_pot_edit != NULL
        ? edit_get_text(app->icm_fgs_pot_edit) : NULL;
    request.fgs_win_probabilities = app->icm_fgs_win_edit != NULL
        ? edit_get_text(app->icm_fgs_win_edit) : NULL;
    textview_clear(app->analysis_icm_view);
    if (pe_analysis_icm(&request, &report) != 0)
    {
        textview_printf(app->analysis_icm_view, "Cannot compute: %s\n",
                        report.error);
        return;
    }
    mode_name = report.mode == PE_ANALYSIS_TOURNAMENT_CHIP_EV ? "ChipEV" :
                report.mode == PE_ANALYSIS_TOURNAMENT_FGS ? "FGS" : "ICM";
    textview_printf(app->analysis_icm_view,
                    "Mode %s | prize pool %.2f over %d paid place%s\n\n",
                    mode_name,
                    report.prize_pool, report.payout_count,
                    report.payout_count == 1 ? "" : "s");
    textview_printf(app->analysis_icm_view, "%-4s %10s %9s %9s %10s\n",
                    "", "STACK", "CHIPS", mode_name, "EV");
    for (i = 0; i < report.player_count; ++i)
        textview_printf(app->analysis_icm_view,
                        "P%-3d %10.0f %8.2f%% %8.2f%% %10.2f\n", i + 1,
                        report.stacks[i], report.chip_share[i] * 100.0,
                        report.equity[i] * 100.0, report.ev[i]);
    if (report.mode == PE_ANALYSIS_TOURNAMENT_FGS)
        textview_printf(app->analysis_icm_view,
                        "\nFGS simulated %d future hand%s (%zu terminal scenarios).\n",
                        report.fgs_depth, report.fgs_depth == 1 ? "" : "s",
                        report.fgs_leaf_count);
    else if (report.mode == PE_ANALYSIS_TOURNAMENT_ICM)
        textview_printf(app->analysis_icm_view,
                        "\nCHIPS is the raw stack share; ICM is the share of the\n"
                        "prize pool after stack pressure.\n");
    else
        textview_printf(app->analysis_icm_view,
                        "\nChipEV keeps the prize pool proportional to chips;\n"
                        "no tournament pressure is applied.\n");
}

static void i_on_icm_mode_select(App *app, Event *event)
{
    int fgs = combo_get_selected(app->icm_mode_combo) ==
              PE_ANALYSIS_TOURNAMENT_FGS;
    unref(event);
    edit_editable(app->icm_fgs_pot_edit, fgs ? TRUE : FALSE);
    edit_editable(app->icm_fgs_depth_edit, fgs ? TRUE : FALSE);
    edit_editable(app->icm_fgs_win_edit, fgs ? TRUE : FALSE);
    if (app->icm_matrix_view)
    {
        app->icm_matrix_ready = 0;
        if (combo_get_selected(app->icm_mode_combo) !=
            PE_ANALYSIS_TOURNAMENT_ICM)
            label_text(app->icm_matrix_hint,
                       "Action grid is available only with the ICM model.");
        else if (i_icm_matrix_supported(app))
            label_text(app->icm_matrix_hint,
                       "Square matrix available for NLH and Short Deck NL only.");
        view_update(app->icm_matrix_view);
    }
}

static void i_on_icm_decision(App *app, Event *event)
{
    pe_analysis_icm_decision_request_t request;
    pe_analysis_icm_decision_report_t report;
    double win_percent;

    unref(event);
    memset(&request, 0, sizeof(request));
    request.stacks = edit_get_text(app->analysis_stacks_edit);
    request.payouts = edit_get_text(app->analysis_payouts_edit);
    request.hero_index = atoi(edit_get_text(app->icm_hero_edit)) - 1;
    request.opponent_index = atoi(edit_get_text(app->icm_opponent_edit)) - 1;
    win_percent = atof(edit_get_text(app->icm_decision_win_edit));
    request.win_probability = win_percent / 100.0;
    request.chips_at_risk = atof(edit_get_text(app->icm_risk_edit));
    request.chips_to_win = atof(edit_get_text(app->icm_gain_edit));
    textview_clear(app->analysis_icm_view);
    if (pe_analysis_icm_decision(&request, &report) != 0)
    {
        textview_printf(app->analysis_icm_view,
                        "Cannot evaluate decision: %s\n", report.error);
        return;
    }
    textview_printf(app->analysis_icm_view,
                    "ICM DECISION | P%d vs P%d | win %.2f%%\n\n"
                    "Fold / current EV       %10.2f\n"
                    "Win outcome EV           %10.2f\n"
                    "Lose outcome EV          %10.2f\n"
                    "Decision EV               %10.2f\n"
                    "Delta vs fold             %+10.2f\n\n"
                    "Effective transfer: +%.2f / -%.2f chips\n",
                    request.hero_index + 1, request.opponent_index + 1,
                    win_percent, report.current_ev, report.win_ev,
                    report.lose_ev, report.decision_ev, report.delta_vs_fold,
                    report.effective_win, report.effective_loss);
}

static int i_icm_matrix_supported(const App *app)
{
    uint32_t game;
    if (!app || !app->icm_game_combo)
        return 0;
    if (app->icm_mode_combo &&
        combo_get_selected(app->icm_mode_combo) != PE_ANALYSIS_TOURNAMENT_ICM)
        return 0;
    game = combo_get_selected(app->icm_game_combo);
    return game == 0u || game == 1u;
}

static void i_on_icm_game_select(App *app, Event *event)
{
    if (app)
        app->icm_matrix_ready = 0;
    if (app && app->icm_matrix_hint)
    {
        if (app->icm_mode_combo &&
            combo_get_selected(app->icm_mode_combo) !=
            PE_ANALYSIS_TOURNAMENT_ICM)
            label_text(app->icm_matrix_hint,
                       "Action grid is available only with the ICM model.");
        else
            label_text(app->icm_matrix_hint,
                       i_icm_matrix_supported(app)
                           ? "Square matrix available for NLH and Short Deck NL only."
                           : "No square matrix for PLO, Stud or Draw; use the ICM table.");
    }
    if (app && app->icm_matrix_view)
        view_update(app->icm_matrix_view);
    unref(event);
}

static int icm_spot_action_is_fold(const App *app, uint32_t player)
{
    return strategy_action_kind(setup_action(app, player)) == 0;
}

static double icm_spot_edit_number(Edit *edit)
{
    double value = 0.0;
    if (edit)
        (void)parse_ui_target(edit_get_text(edit), &value);
    return value >= 0.0 && pe_finite_double(value) ? value : 0.0;
}

static int icm_matrix_decision_delta(const char *stacks, const char *payouts,
                                     double equity, int hero, int opponent,
                                     double risk, double gain,
                                     double *delta)
{
    pe_analysis_icm_decision_request_t request;
    pe_analysis_icm_decision_report_t report;

    if (!delta || !stacks || !payouts)
        return -1;
    memset(&request, 0, sizeof(request));
    request.stacks = stacks;
    request.payouts = payouts;
    request.hero_index = hero;
    request.opponent_index = opponent;
    request.win_probability = equity;
    request.chips_at_risk = risk;
    request.chips_to_win = gain;
    if (pe_analysis_icm_decision(&request, &report) != 0)
        return -1;
    *delta = report.delta_vs_fold;
    return 0;
}

static void i_on_compute_icm_matrix(App *app, Event *event)
{
    const char *ranks;
    const char *opponent_range;
    const char *board;
    int count;
    int hero;
    int opponent = -1;
    int opponent_action = -1;
    int active_opponents = 0;
    int failed = 0;
    uint32_t players;
    double pot;
    double hero_stack;
    double to_call = 0.0;
    double opponent_stack = 0.0;
    int has_bet = 0;

    unref(event);
    if (!app || !app->icm_matrix_view)
        return;
    if (app->icm_mode_combo &&
        combo_get_selected(app->icm_mode_combo) != PE_ANALYSIS_TOURNAMENT_ICM)
    {
        app->icm_matrix_ready = 0;
        if (app->icm_matrix_hint)
            label_text(app->icm_matrix_hint,
                       "Action grid is available only with the ICM model.");
        view_update(app->icm_matrix_view);
        return;
    }
    if (!i_icm_matrix_supported(app))
    {
        app->icm_matrix_ready = 0;
        if (app->icm_matrix_hint)
            label_text(app->icm_matrix_hint,
                       "No action grid for PLO, Stud or Draw; use the ICM table.");
        view_update(app->icm_matrix_view);
        return;
    }

    players = selected_players(app);
    if (players < 2u)
        players = 2u;
    if (players > PE_ANALYSIS_MAX_PLAYERS)
        players = PE_ANALYSIS_MAX_PLAYERS;
    hero = app->icm_spot_hero_combo
        ? (int)combo_get_selected(app->icm_spot_hero_combo) : 0;
    if (hero < 0 || hero >= (int)players)
        hero = 0;

    pot = icm_spot_edit_number(app->setup_pot_edit);
    hero_stack = icm_spot_edit_number(app->setup_stack_edit[hero]);
    for (uint32_t player = 0u; player < players; ++player)
    {
        int action;
        if ((int)player == hero)
            continue;
        action = strategy_action_kind(setup_action(app, player));
        if (action == 0)
            continue;
        if (opponent < 0 ||
            ((action == 3 || action == 4) &&
             opponent_action != 3 && opponent_action != 4))
        {
            opponent = (int)player;
            opponent_stack = icm_spot_edit_number(app->setup_stack_edit[player]);
            opponent_action = action;
        }
        ++active_opponents;
        if (action == 3 || action == 4)
            has_bet = 1;
    }
    if (opponent < 0)
    {
        /* A fully folded table has no matchup to evaluate. */
        app->icm_matrix_ready = 0;
        if (app->icm_matrix_hint)
            label_text(app->icm_matrix_hint,
                       "No active opponent in the spot; action grid unavailable.");
        view_update(app->icm_matrix_view);
        return;
    }

    /* Spot setup does not currently ask for bet sizes.  Use the visible pot
     * as the current pot and a half-pot wager as the deterministic default;
     * the all-in amount always comes from the hero stack. */
    if (has_bet)
        to_call = pot * 0.5;
    if (to_call < 0.0 || !pe_finite_double(to_call))
        to_call = 0.0;
    ranks = combo_get_selected(app->icm_game_combo) == 1u
        ? "AKQJT9876" : "AKQJT98765432";
    count = (int)strlen(ranks);
    opponent_range = app->icm_matrix_opponent_range_edit
        ? edit_get_text(app->icm_matrix_opponent_range_edit) : "100%";
    if (!opponent_range || !*opponent_range)
        opponent_range = "100%";
    board = app->board_edit ? edit_get_text(app->board_edit) : "";
    if (!board || strstr(board, "No board") != NULL)
        board = "";

    for (int row = 0; row < count; ++row)
    {
        for (int column = 0; column < count; ++column)
        {
            char hand[8];
            pe_analysis_equity_request_t request;
            pe_analysis_equity_report_t report;
            int range_player = 1;
            double scores[5] = {-1.0e100, -1.0e100, -1.0e100,
                                -1.0e100, -1.0e100};
            double best_score;
            int primary = -1;
            double call_risk;
            double raise_risk;
            double raise_gain;
            double allin_gain;

            snprintf(hand, sizeof(hand), "%c%c%s", ranks[row], ranks[column],
                     row == column ? "" : row < column ? "s" : "o");
            memset(&request, 0, sizeof(request));
            request.game = combo_get_selected(app->icm_game_combo) == 1u
                ? game_sdholdem : game_holdem;
            request.player_count = active_opponents + 1;
            request.ranges[0] = hand;
            for (uint32_t player = 0u; player < players && range_player < PE_ANALYSIS_MAX_PLAYERS;
                 ++player)
            {
                if ((int)player == hero || icm_spot_action_is_fold(app, player))
                    continue;
                request.ranges[range_player++] = opponent_range;
            }
            request.board = board;
            request.monte_carlo = 1;
            request.iterations = 1500;
            if (pe_analysis_equity(&request, &report) != 0)
            {
                app->icm_matrix_equity[row][column] = 0.0;
                app->icm_matrix_action[row][column] = -1;
                ++failed;
                continue;
            }
            app->icm_matrix_equity[row][column] = report.equity[0];

            /* Fold is the zero-delta baseline.  Check is legal when no
             * opponent has bet; otherwise call/raise/all-in are compared by
             * their ICM decision EV against the first live opponent. */
            scores[0] = 0.0;
            if (!has_bet)
                scores[1] = 0.0;
            if (has_bet && hero_stack >= to_call &&
                icm_matrix_decision_delta(edit_get_text(app->analysis_stacks_edit),
                                          edit_get_text(app->analysis_payouts_edit),
                                          report.equity[0], hero, opponent,
                                          to_call, pot, &scores[2]) != 0)
                ++failed;
            call_risk = has_bet ? to_call : 0.0;
            raise_risk = call_risk + pot * 0.5;
            if (raise_risk > hero_stack)
                raise_risk = hero_stack;
            raise_gain = pot + raise_risk;
            allin_gain = pot + opponent_stack;
            if (hero_stack > to_call && raise_risk > to_call &&
                icm_matrix_decision_delta(edit_get_text(app->analysis_stacks_edit),
                                          edit_get_text(app->analysis_payouts_edit),
                                          report.equity[0], hero, opponent,
                                          raise_risk, raise_gain, &scores[3]) != 0)
                ++failed;
            if (hero_stack > 0.0 &&
                icm_matrix_decision_delta(edit_get_text(app->analysis_stacks_edit),
                                          edit_get_text(app->analysis_payouts_edit),
                                          report.equity[0], hero, opponent,
                                          hero_stack, allin_gain, &scores[4]) != 0)
                ++failed;
            best_score = scores[0];
            primary = 0;
            for (int action = 1; action < 5; ++action)
            {
                if (scores[action] > best_score + 1.0e-9)
                {
                    best_score = scores[action];
                    primary = action;
                }
            }
            app->icm_matrix_action[row][column] = (int8_t)primary;
        }
    }
    /* Keep usable cells visible when one malformed/blocked class fails.  A
     * single bad matchup should not hide the other 168 (or 80) decisions. */
    app->icm_matrix_ready = failed < count * count;
    if (app->icm_matrix_hint)
    {
        char message[160];
        snprintf(message, sizeof(message),
                 "Action grid: %d/%d hands evaluated%s | MC 1500 / hand | %d active opponent%s.",
                 count * count - failed, count * count,
                 failed != 0 ? " (some cells skipped)" : "",
                 active_opponents,
                 active_opponents == 1 ? "" : "s");
        label_text(app->icm_matrix_hint, message);
    }
    view_update(app->icm_matrix_view);
}

static void i_on_icm_matrix_input_change(App *app, Event *event)
{
    if (app && app->icm_matrix_mutex)
    {
        int running;
        bmutex_lock(app->icm_matrix_mutex);
        running = app->icm_matrix_running;
        if (running)
            app->icm_matrix_cancel_requested = 1;
        bmutex_unlock(app->icm_matrix_mutex);
        app->icm_matrix_ready = 0;
        if (app->icm_matrix_hint)
            label_text(app->icm_matrix_hint, running
                       ? "Inputs changed. Stopping the current grid evaluation..."
                       : "Inputs changed. Click Evaluate all hands to refresh the action grid.");
        if (app->icm_matrix_view)
            view_update(app->icm_matrix_view);
    }
    unref(event);
}

static void i_on_draw_icm_matrix(App *app, Event *event)
{
    const EvDraw *params = event_params(event, EvDraw);
    DCtx *ctx = params->ctx;
    real32_t width = params->width;
    real32_t height = params->height;
    uint32_t game = app && app->icm_game_combo
        ? combo_get_selected(app->icm_game_combo) : 0u;
    const char *ranks = game == 1u ? "AKQJT9876" : "AKQJT98765432";
    int count = (int)strlen(ranks);
    real32_t left = 42.0f;
    real32_t top = 42.0f;
    real32_t legend_width = 170.0f;
    real32_t usable_width = width - left - legend_width;
    real32_t usable_height = height - top - 12.0f;
    real32_t cell = usable_width / (real32_t)count;
    real32_t by_height = usable_height / (real32_t)count;
    char text[16];

    if (by_height < cell)
        cell = by_height;
    draw_fill_color(ctx, color_rgb(18, 22, 27));
    draw_rect(ctx, ekFILL, 0, 0, width, height);
    if (!i_icm_matrix_supported(app))
    {
        if (app && app->bold_font)
            draw_font(ctx, app->bold_font);
        draw_text_color(ctx, color_rgb(225, 230, 235));
        draw_text_align(ctx, ekCENTER, ekCENTER);
        draw_text(ctx, app && app->icm_mode_combo &&
                  combo_get_selected(app->icm_mode_combo) !=
                  PE_ANALYSIS_TOURNAMENT_ICM
                  ? "Action grid requires ICM model"
                  : "No square matrix for this game", width * 0.5f,
                  height * 0.5f - 10.0f);
        if (app && app->regular_font)
            draw_font(ctx, app->regular_font);
        draw_text_color(ctx, color_rgb(150, 160, 170));
        draw_text(ctx, app && app->icm_mode_combo &&
                  combo_get_selected(app->icm_mode_combo) !=
                  PE_ANALYSIS_TOURNAMENT_ICM
                  ? "Select ICM to classify Fold / Check / Call / Raise / All-in."
                  : "Use the ICM result table for PLO, Stud and Draw.",
                  width * 0.5f, height * 0.5f + 14.0f);
        return;
    }

    if (app && app->bold_font)
        draw_font(ctx, app->bold_font);
    draw_text_color(ctx, color_rgb(225, 230, 235));
    draw_text_align(ctx, ekLEFT, ekCENTER);
    draw_text(ctx, "ACTION MATRIX  |  ICM + EQUITY", 12.0f, 16.0f);
    if (app && app->regular_font)
        draw_font(ctx, app->regular_font);
    draw_text_color(ctx, color_rgb(150, 160, 170));
    draw_text_align(ctx, ekRIGHT, ekCENTER);
    draw_text(ctx, game == 1u ? "SHORT DECK NL" : "NLH", width - 12.0f,
              16.0f);

    if (!app || !app->icm_matrix_ready)
    {
        draw_text_align(ctx, ekCENTER, ekCENTER);
        draw_text_color(ctx, color_rgb(150, 160, 170));
        draw_text(ctx, "Click Evaluate all hands", width * 0.5f,
                  height * 0.5f - 10.0f);
        draw_text(ctx, "Each cell is evaluated against the configured spot",
                  width * 0.5f, height * 0.5f + 14.0f);
        return;
    }

    /* The legend uses the same semantic colors as Results and Tree Builder:
     * the color is the highest ICM decision EV for that starting-hand class. */
    if (app->bold_font)
        draw_font(ctx, app->bold_font);
    draw_text_align(ctx, ekLEFT, ekCENTER);
    draw_text_color(ctx, color_rgb(220, 226, 232));
    draw_text(ctx, "BEST ACTION", left + cell * (real32_t)count + 14.0f, 58.0f);
    if (app->regular_font)
        draw_font(ctx, app->regular_font);
    for (int action = 0; action < 5; ++action)
    {
        real32_t legend_x = left + cell * (real32_t)count + 14.0f;
        real32_t legend_y = 84.0f + (real32_t)action * 28.0f;
        draw_fill_color(ctx, strategy_action_color(action));
        draw_rndrect(ctx, ekFILL, legend_x, legend_y - 8.0f,
                     14.0f, 16.0f, 3.0f);
        draw_text_color(ctx, color_rgb(205, 213, 220));
        draw_text_align(ctx, ekLEFT, ekCENTER);
        draw_text(ctx, strategy_action_name(action), legend_x + 23.0f,
                  legend_y);
    }

    for (int row = 0; row < count; ++row)
    {
        char rank_label[2] = {ranks[row], '\0'};
        for (int col = 0; col < count; ++col)
        {
            real32_t x = left + (real32_t)col * cell;
            real32_t y = top + (real32_t)row * cell;
            int action = app->icm_matrix_action[row][col];
            color_t fill = action >= 0 ? strategy_action_color(action) :
                           color_rgb(39, 45, 53);
            draw_fill_color(ctx, fill);
            draw_rndrect(ctx, ekFILL, x + 1.0f, y + 1.0f,
                         cell - 2.0f, cell - 2.0f, 2.0f);
            snprintf(text, sizeof(text), "%c%c%s", ranks[row], ranks[col],
                     row == col ? "" : row < col ? "s" : "o");
            draw_text_color(ctx, color_rgb(225, 230, 235));
            draw_text_align(ctx, ekCENTER, ekCENTER);
            draw_text(ctx, text, x + cell * 0.5f, y + cell * 0.38f);
            snprintf(text, sizeof(text), "%.0f%%",
                     app->icm_matrix_equity[row][col] * 100.0);
            draw_text_color(ctx, color_rgb(205, 215, 222));
            draw_text(ctx, text, x + cell * 0.5f, y + cell * 0.70f);
            if (action >= 0)
            {
                const char *marker = action == 0 ? "F" :
                                     action == 1 ? "Ck" :
                                     action == 2 ? "C" :
                                     action == 3 ? "R" : "AI";
                draw_text_color(ctx, color_rgb(245, 248, 250));
                draw_text_align(ctx, ekRIGHT, ekTOP);
                draw_text(ctx, marker, x + cell - 4.0f, y + 3.0f);
            }
        }
        draw_text_color(ctx, color_rgb(170, 180, 190));
        draw_text_align(ctx, ekCENTER, ekCENTER);
        draw_text(ctx, rank_label, left - 17.0f,
                  top + ((real32_t)row + 0.5f) * cell);
        draw_text(ctx, rank_label,
                  left + ((real32_t)row + 0.5f) * cell,
                  top - 17.0f);
    }
}

static Panel *i_icm_panel(App *app)
{
    Panel *panel = panel_create();
    Layout *root = layout_create(2, 1);
    Layout *outer = layout_create(1, 2);
    Layout *spot_page_layout = layout_create(1, 1);
    Layout *calculator_page_layout = layout_create(1, 1);
    Layout *controls = layout_create(2, 12);
    Layout *results = layout_create(2, 4);
    Panel *spot_panel = i_icm_spot_panel(app);
    Panel *calculator_panel = panel_create();
    Label *title = label_create();
    Label *game_label = label_create();
    Label *mode_label = label_create();
    Label *stacks_label = label_create();
    Label *payouts_label = label_create();
    Label *fgs_pot_label = label_create();
    Label *fgs_depth_label = label_create();
    Label *fgs_win_label = label_create();
    Label *matrix_range_label = label_create();
    Label *matrix_iterations_label = label_create();
    Label *matrix_raise_label = label_create();
    Label *compute_label = label_create();
    Label *result_label = label_create();
    Button *compute = button_push();
    Button *matrix_compute = button_push();

    app->icm_game_combo = combo_create();
    app->icm_mode_combo = combo_create();
    app->analysis_stacks_edit = edit_create();
    app->analysis_payouts_edit = edit_create();
    app->icm_fgs_pot_edit = edit_create();
    app->icm_fgs_depth_edit = edit_create();
    app->icm_fgs_win_edit = edit_create();
    app->analysis_icm_view = textview_create();
    app->icm_matrix_view = view_create();
    app->icm_matrix_hint = label_create();
    app->icm_matrix_opponent_range_edit = edit_create();
    app->icm_matrix_iterations_edit = edit_create();
    app->icm_matrix_raise_edit = edit_create();
    app->icm_tabs = tabs_create(ekGUI_POS_TOP);
    app->icm_pages = panel_create();

    label_text(title, "ICM | tournament chips to money");
    label_text(game_label, "GAME FORMAT");
    label_text(mode_label, "MODEL");
    label_text(stacks_label, "STACKS (comma separated)");
    label_text(payouts_label, "PAYOUTS (largest first)");
    label_text(fgs_pot_label, "FGS POT (chips / hand)");
    label_text(fgs_depth_label, "FGS DEPTH (hands)");
    label_text(fgs_win_label, "FGS WIN WEIGHTS");
    label_text(matrix_range_label, "OPPONENT RANGE (ACTION GRID)");
    label_text(matrix_iterations_label, "EQUITY SAMPLES / CLASS");
    label_text(matrix_raise_label, "RAISE SIZE (pot multiple)");
    label_text(compute_label, "ICM CALCULATION");
    label_text(result_label, "ICM RESULT");
    label_text(app->icm_matrix_hint,
               "Square matrix available for NLH and Short Deck NL only.");
    combo_add_elem(app->icm_game_combo, "NLH", NULL);
    combo_add_elem(app->icm_game_combo, "Short Deck NL", NULL);
    combo_add_elem(app->icm_game_combo, "PLO", NULL);
    combo_add_elem(app->icm_game_combo, "Stud", NULL);
    combo_add_elem(app->icm_game_combo, "Draw", NULL);
    combo_selected(app->icm_game_combo, 0u);
    combo_OnSelect(app->icm_game_combo,
                   listener(app, i_on_icm_game_select, App));
    combo_add_elem(app->icm_mode_combo, "ICM", NULL);
    combo_add_elem(app->icm_mode_combo, "ChipEV", NULL);
    combo_add_elem(app->icm_mode_combo, "FGS", NULL);
    combo_selected(app->icm_mode_combo, PE_ANALYSIS_TOURNAMENT_ICM);
    combo_OnSelect(app->icm_mode_combo,
                   listener(app, i_on_icm_mode_select, App));
    edit_text(app->analysis_stacks_edit, "5000, 3000, 2000");
    edit_text(app->analysis_payouts_edit, "500, 300, 200");
    edit_text(app->icm_fgs_pot_edit, "100");
    edit_text(app->icm_fgs_depth_edit, "2");
    edit_text(app->icm_fgs_win_edit, "50, 30, 20");
    edit_text(app->icm_matrix_opponent_range_edit, "100%");
    edit_text(app->icm_matrix_iterations_edit, "300");
    edit_text(app->icm_matrix_raise_edit, "0.5");
    i_on_icm_mode_select(app, NULL);
    button_text(compute, "Calculate ICM");
    button_OnClick(compute, listener(app, i_on_analysis_icm, App));
    button_text(matrix_compute, "Evaluate all hands");
    button_OnClick(matrix_compute,
                   listener(app, i_on_compute_icm_matrix, App));
    edit_OnChange(app->analysis_stacks_edit,
                  listener(app, i_on_icm_matrix_input_change, App));
    edit_OnChange(app->analysis_payouts_edit,
                  listener(app, i_on_icm_matrix_input_change, App));
    edit_OnChange(app->icm_matrix_opponent_range_edit,
                  listener(app, i_on_icm_matrix_input_change, App));
    edit_OnChange(app->icm_matrix_iterations_edit,
                  listener(app, i_on_icm_matrix_input_change, App));
    edit_OnChange(app->icm_matrix_raise_edit,
                  listener(app, i_on_icm_matrix_input_change, App));
    textview_editable(app->analysis_icm_view, FALSE);
    textview_wrap(app->analysis_icm_view, FALSE);
    textview_printf(app->analysis_icm_view,
                    "Enter stacks and a payout ladder, then calculate ICM.\n");
    view_OnDraw(app->icm_matrix_view,
                listener(app, i_on_draw_icm_matrix, App));
    view_size(app->icm_matrix_view, s2df(720.0f, 540.0f));

    layout_label(controls, title, 0, 0);
    layout_label(controls, game_label, 0, 1);
    layout_combo(controls, app->icm_game_combo, 1, 1);
    layout_label(controls, mode_label, 0, 2);
    layout_combo(controls, app->icm_mode_combo, 1, 2);
    layout_label(controls, stacks_label, 0, 3);
    layout_edit(controls, app->analysis_stacks_edit, 1, 3);
    layout_label(controls, payouts_label, 0, 4);
    layout_edit(controls, app->analysis_payouts_edit, 1, 4);
    layout_label(controls, fgs_pot_label, 0, 5);
    layout_edit(controls, app->icm_fgs_pot_edit, 1, 5);
    layout_label(controls, fgs_depth_label, 0, 6);
    layout_edit(controls, app->icm_fgs_depth_edit, 1, 6);
    layout_label(controls, fgs_win_label, 0, 7);
    layout_edit(controls, app->icm_fgs_win_edit, 1, 7);
    layout_label(controls, matrix_range_label, 0, 8);
    layout_edit(controls, app->icm_matrix_opponent_range_edit, 1, 8);
    layout_label(controls, matrix_iterations_label, 0, 9);
    layout_edit(controls, app->icm_matrix_iterations_edit, 1, 9);
    layout_label(controls, matrix_raise_label, 0, 10);
    layout_edit(controls, app->icm_matrix_raise_edit, 1, 10);
    layout_label(controls, compute_label, 0, 11);
    layout_button(controls, compute, 1, 11);
    layout_hsize(controls, 0, 190.0f);
    layout_hsize(controls, 1, 300.0f);
    layout_hmargin(controls, 0, 8.0f);
    layout_vmargin(controls, 0, 10.0f);
    layout_vmargin(controls, 1, 8.0f);
    layout_vmargin(controls, 2, 8.0f);
    layout_vmargin(controls, 3, 8.0f);
    layout_vmargin(controls, 4, 8.0f);
    layout_vmargin(controls, 5, 8.0f);
    layout_vmargin(controls, 6, 8.0f);
    layout_vmargin(controls, 7, 12.0f);
    layout_vmargin(controls, 8, 8.0f);
    layout_vmargin(controls, 9, 8.0f);
    layout_vmargin(controls, 10, 8.0f);

    layout_label(results, result_label, 0, 0);
    layout_textview(results, app->analysis_icm_view, 0, 1);
    layout_label(results, app->icm_matrix_hint, 0, 2);
    layout_button(results, matrix_compute, 1, 2);
    layout_view(results, app->icm_matrix_view, 0, 3);
    layout_hsize(results, 0, 720.0f);
    layout_hsize(results, 1, 160.0f);
    layout_vsize(results, 1, 180.0f);
    layout_vsize(results, 3, 540.0f);
    layout_vmargin(results, 0, 6.0f);
    layout_vmargin(results, 1, 12.0f);
    layout_vmargin(results, 2, 6.0f);

    layout_layout(root, controls, 0, 0);
    layout_layout(root, results, 1, 0);
    layout_valign(root, 0, 0, ekTOP);
    layout_valign(root, 1, 0, ekTOP);
    layout_hsize(root, 0, 510.0f);
    layout_hsize(root, 1, 720.0f);
    layout_hmargin(root, 0, 16.0f);
    layout_margin(root, 12.0f);
    /* ICM has two deliberately separate workflows.  SPOT SETUP owns the
     * hand/table state that used to be incorrectly shown in SETUP; the
     * calculator page owns tournament chips, payouts and ICM/FGS results. */
    panel_layout(calculator_panel, root);
    tabs_add_elem(app->icm_tabs, "SPOT SETUP", NULL);
    tabs_add_elem(app->icm_tabs, "ICM CALCULATOR", NULL);
    tabs_selected(app->icm_tabs, 0u);
    tabs_OnSelect(app->icm_tabs, listener(app, i_on_icm_tab, App));
    layout_panel(spot_page_layout, spot_panel, 0, 0);
    layout_panel(calculator_page_layout, calculator_panel, 0, 0);
    layout_tabs(outer, app->icm_tabs, 0, 0);
    layout_panel(outer, app->icm_pages, 0, 1);
    layout_vsize(outer, 1, 780.0f);
    layout_margin(outer, 10.0f);
    panel_layout(app->icm_pages, spot_page_layout);
    panel_layout(app->icm_pages, calculator_page_layout);
    panel_visible_layout(app->icm_pages, 0u);
    panel_layout(panel, outer);
    return panel;
}

static Panel *i_analysis_panel(App *app)
{
    Panel *panel = panel_create();
    Layout *root = layout_create(2, 1);
    Layout *left = layout_create(2, 14);
    Layout *right = layout_create(1, 6);
    Label *title = label_create();
    Label *game_label = label_create();
    Label *players_label = label_create();
    Label *board_label = label_create();
    Label *dead_label = label_create();
    Label *iterations_label = label_create();
    Label *icm_title = label_create();
    Label *stacks_label = label_create();
    Label *payouts_label = label_create();
    Label *equity_title = label_create();
    Label *breakdown_title = label_create();
    Label *icm_result_title = label_create();
    Button *equity_button = button_push();
    Button *breakdown_button = button_push();
    Button *icm_button = button_push();
    int player;

    app->analysis_game_combo = combo_create();
    app->analysis_players_combo = combo_create();
    app->analysis_board_edit = edit_create();
    app->analysis_dead_edit = edit_create();
    app->analysis_iterations_edit = edit_create();
    app->analysis_equity_view = textview_create();
    app->analysis_breakdown_view = textview_create();
    app->analysis_stacks_edit = edit_create();
    app->analysis_payouts_edit = edit_create();
    app->analysis_icm_view = textview_create();

    label_text(title, "ANALYSIS | equity, made-hand breakdown and ICM");
    label_text(game_label, "GAME");
    label_text(players_label, "PLAYERS");
    label_text(board_label, "BOARD (e.g. AhKd7s)");
    label_text(dead_label, "DEAD CARDS");
    label_text(iterations_label, "MONTE CARLO ITERATIONS");
    label_text(icm_title, "ICM | tournament chips to money");
    label_text(stacks_label, "STACKS (comma separated)");
    label_text(payouts_label, "PAYOUTS (largest first)");
    label_text(equity_title, "EQUITY");
    label_text(breakdown_title, "WHAT THE RANGE HIT (player 1)");
    label_text(icm_result_title, "ICM");

    combo_add_elem(app->analysis_game_combo, "Hold'em", NULL);
    combo_add_elem(app->analysis_game_combo, "Omaha", NULL);
    combo_add_elem(app->analysis_game_combo, "Omaha Hi/Lo", NULL);
    combo_add_elem(app->analysis_game_combo, "Hold'em Hi/Lo", NULL);
    combo_add_elem(app->analysis_game_combo, "7-card Stud", NULL);
    combo_selected(app->analysis_game_combo, 0u);
    for (player = 2; player <= PE_ANALYSIS_MAX_PLAYERS; ++player)
    {
        char text[16];
        snprintf(text, sizeof(text), "%d players", player);
        combo_add_elem(app->analysis_players_combo, text, NULL);
    }
    combo_selected(app->analysis_players_combo, 0u);
    combo_OnSelect(app->analysis_players_combo,
                   listener(app, i_on_analysis_players, App));

    layout_label(left, title, 0, 0);
    layout_label(left, game_label, 0, 1);
    layout_combo(left, app->analysis_game_combo, 1, 1);
    layout_label(left, players_label, 0, 2);
    layout_combo(left, app->analysis_players_combo, 1, 2);
    for (player = 0; player < PE_ANALYSIS_MAX_PLAYERS; ++player)
    {
        Label *range_label = label_create();
        char caption[24];
        snprintf(caption, sizeof(caption), "RANGE P%d", player + 1);
        label_text(range_label, caption);
        app->analysis_range_edit[player] = edit_create();
        edit_text(app->analysis_range_edit[player],
                  player == 0 ? "AA,KK,QQ,AKs" : "22+,ATs+,AJo+");
        edit_editable(app->analysis_range_edit[player], player < 2);
        layout_label(left, range_label, 0, 3 + (uint32_t)player);
        layout_edit(left, app->analysis_range_edit[player], 1,
                    3 + (uint32_t)player);
    }
    layout_label(left, board_label, 0, 9);
    layout_edit(left, app->analysis_board_edit, 1, 9);
    layout_label(left, dead_label, 0, 10);
    layout_edit(left, app->analysis_dead_edit, 1, 10);
    layout_label(left, iterations_label, 0, 11);
    layout_edit(left, app->analysis_iterations_edit, 1, 11);
    layout_button(left, equity_button, 0, 12);
    layout_button(left, breakdown_button, 1, 12);

    {
        Layout *icm_block = layout_create(2, 4);
        layout_label(icm_block, icm_title, 0, 0);
        layout_label(icm_block, stacks_label, 0, 1);
        layout_edit(icm_block, app->analysis_stacks_edit, 1, 1);
        layout_label(icm_block, payouts_label, 0, 2);
        layout_edit(icm_block, app->analysis_payouts_edit, 1, 2);
        layout_button(icm_block, icm_button, 1, 3);
        layout_hsize(icm_block, 0, 190.0f);
        layout_hsize(icm_block, 1, 260.0f);
        layout_hmargin(icm_block, 0, 8.0f);
        layout_vmargin(icm_block, 0, 8.0f);
        layout_vmargin(icm_block, 1, 6.0f);
        layout_vmargin(icm_block, 2, 6.0f);
        layout_layout(left, icm_block, 0, 13);
    }

    button_text(equity_button, "Compute equity");
    button_text(breakdown_button, "What did it hit?");
    button_text(icm_button, "Compute ICM");
    button_OnClick(equity_button, listener(app, i_on_analysis_equity, App));
    button_OnClick(breakdown_button,
                   listener(app, i_on_analysis_breakdown, App));
    button_OnClick(icm_button, listener(app, i_on_analysis_icm, App));

    edit_text(app->analysis_board_edit, "");
    edit_phtext(app->analysis_board_edit, "empty = preflop");
    edit_text(app->analysis_dead_edit, "");
    edit_phtext(app->analysis_dead_edit, "cards removed from the deck");
    edit_text(app->analysis_iterations_edit, "200000");
    edit_text(app->analysis_stacks_edit, "5000, 3000, 2000");
    edit_text(app->analysis_payouts_edit, "500, 300, 200");

    textview_editable(app->analysis_equity_view, FALSE);
    textview_editable(app->analysis_breakdown_view, FALSE);
    textview_editable(app->analysis_icm_view, FALSE);
    textview_wrap(app->analysis_equity_view, FALSE);
    textview_wrap(app->analysis_breakdown_view, FALSE);
    textview_wrap(app->analysis_icm_view, FALSE);
    textview_printf(app->analysis_equity_view,
                    "Fill the ranges and press Compute equity.\n");
    textview_printf(app->analysis_breakdown_view,
                    "Give a board of 3 cards or more, then press "
                    "What did it hit?\n");
    textview_printf(app->analysis_icm_view,
                    "Give stacks and a payout ladder, then press "
                    "Compute ICM.\n");

    layout_hsize(left, 0, 190.0f);
    layout_hsize(left, 1, 260.0f);
    layout_hmargin(left, 0, 8.0f);
    {
        uint32_t row;
        for (row = 0u; row < 13u; ++row)
            layout_vmargin(left, row, 6.0f);
    }

    layout_label(right, equity_title, 0, 0);
    layout_textview(right, app->analysis_equity_view, 0, 1);
    layout_label(right, breakdown_title, 0, 2);
    layout_textview(right, app->analysis_breakdown_view, 0, 3);
    layout_label(right, icm_result_title, 0, 4);
    layout_textview(right, app->analysis_icm_view, 0, 5);
    layout_hsize(right, 0, 760.0f);
    layout_vsize(right, 1, 150.0f);
    layout_vsize(right, 3, 230.0f);
    layout_vsize(right, 5, 200.0f);
    layout_vmargin(right, 0, 4.0f);
    layout_vmargin(right, 1, 10.0f);
    layout_vmargin(right, 2, 4.0f);
    layout_vmargin(right, 4, 4.0f);

    layout_layout(root, left, 0, 0);
    layout_layout(root, right, 1, 0);
    layout_hmargin(root, 0, 16.0f);
    layout_margin(root, 12.0f);
    panel_layout(panel, root);
    return panel;
}

static App *i_create(void)
{
    App *app = heap_new0(App);
    Panel *root = panel_create();
    Layout *layout = layout_create(1, 2);
    Panel *setup;
    Panel *result;
    Panel *editor;
    Panel *analysis;
    Panel *icm;
    Layout *setup_layout = layout_create(1, 1);
    Layout *result_layout = layout_create(1, 1);
    Layout *editor_layout = layout_create(1, 1);
    Layout *analysis_layout = layout_create(1, 1);
    Layout *icm_layout = layout_create(1, 1);
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
    editor = i_tree_editor_panel(app);
    analysis = i_analysis_panel(app);
    icm = i_icm_panel(app);

    app->tabs = tabs;
    app->pages = pages;
    tabs_add_elem(tabs, "SETUP", NULL);
    tabs_add_elem(tabs, "RESULTS", NULL);
    tabs_add_elem(tabs, "TREE BUILDER", NULL);
    tabs_add_elem(tabs, "ANALYSIS", NULL);
    tabs_add_elem(tabs, "ICM", NULL);
    tabs_OnSelect(tabs, listener(app, i_on_tab, App));
    layout_panel(setup_layout, setup, 0, 0);
    layout_panel(result_layout, result, 0, 0);
    layout_panel(editor_layout, editor, 0, 0);
    layout_panel(analysis_layout, analysis, 0, 0);
    layout_panel(icm_layout, icm, 0, 0);
    panel_layout(pages, setup_layout);
    panel_layout(pages, result_layout);
    panel_layout(pages, editor_layout);
    panel_layout(pages, analysis_layout);
    panel_layout(pages, icm_layout);
    panel_visible_layout(pages, 0u);
    app->solve_mutex = bmutex_create();
    app->icm_matrix_mutex = bmutex_create();
    layout_tabs(layout, tabs, 0, 0);
    layout_panel(layout, pages, 0, 1);
    layout_vsize(layout, 1, 780);
    layout_margin(layout, 12);
    panel_layout(root, layout);
    app->window = window_create(ekWINDOW_STDRES);
    window_panel(app->window, root);
    window_title(app->window, "poker-eval Studio");
    window_origin(app->window, v2df(100, 60));
    /* SETUP contains the table, board matrix and up to eight seat rows.  The
     * previous 1440x920 default was shorter than that layout and macOS could
     * receive negative control origins while switching pages. */
    window_client_size(app->window, s2df(1660, 1200));
    window_OnClose(app->window, listener(app, i_on_close, App));
    window_show(app->window);
    return app;
}

static void i_on_close(App *app, Event *event)
{
    int icm_running = 0;
    bmutex_lock(app->solve_mutex);
    if (app->solve_running)
    {
        bmutex_unlock(app->solve_mutex);
        i_solve_request_stop(app);
        unref(event);
        return;
    }
    bmutex_unlock(app->solve_mutex);
    if (app->icm_matrix_mutex)
    {
        bmutex_lock(app->icm_matrix_mutex);
        icm_running = app->icm_matrix_running;
        if (icm_running)
            app->icm_matrix_cancel_requested = 1;
        bmutex_unlock(app->icm_matrix_mutex);
        if (icm_running)
        {
            status(app, "ICM action grid is still evaluating; close again when it finishes.");
            unref(event);
            return;
        }
    }
    osapp_finish();
    unref(app);
    unref(event);
}

static void i_destroy(App **app)
{
    if (*app)
    {
        mkr_model_clear(*app);
        /* The native draw listeners retain their View while the OS control
         * is alive.  Detach them before destroying the window tree so the
         * callback -> View reference cannot outlive the component layout. */
        if ((*app)->board_matrix_view)
        {
            view_OnDraw((*app)->board_matrix_view, NULL);
            view_OnClick((*app)->board_matrix_view, NULL);
        }
        if ((*app)->setup_board_matrix_view)
        {
            view_OnDraw((*app)->setup_board_matrix_view, NULL);
            view_OnClick((*app)->setup_board_matrix_view, NULL);
        }
        if ((*app)->setup_table_view)
        {
            view_OnDraw((*app)->setup_table_view, NULL);
            view_OnClick((*app)->setup_table_view, NULL);
        }
        if ((*app)->poker_table_view)
        {
            view_OnDraw((*app)->poker_table_view, NULL);
            view_OnClick((*app)->poker_table_view, NULL);
        }
        if ((*app)->strategy_grid_view)
        {
            view_OnDraw((*app)->strategy_grid_view, NULL);
            view_OnClick((*app)->strategy_grid_view, NULL);
            view_OnWheel((*app)->strategy_grid_view, NULL);
        }
        if ((*app)->hand_preview_view)
            view_OnDraw((*app)->hand_preview_view, NULL);
        if ((*app)->tree_editor_canvas)
        {
            view_OnDraw((*app)->tree_editor_canvas, NULL);
            view_OnClick((*app)->tree_editor_canvas, NULL);
        }
        if ((*app)->tree_editor_poker_view)
            view_OnDraw((*app)->tree_editor_poker_view, NULL);
        if ((*app)->icm_matrix_view)
            view_OnDraw((*app)->icm_matrix_view, NULL);
        if ((*app)->icm_spot_board_matrix_view)
        {
            view_OnDraw((*app)->icm_spot_board_matrix_view, NULL);
            view_OnClick((*app)->icm_spot_board_matrix_view, NULL);
        }
        if ((*app)->icm_spot_table_view)
        {
            view_OnDraw((*app)->icm_spot_table_view, NULL);
            view_OnClick((*app)->icm_spot_table_view, NULL);
        }
        /*
         * Components own references to the fonts used by their labels and
         * text views.  Tear down the window tree first so those references
         * are released before destroying the application-owned fonts.
         */
        window_destroy(&(*app)->window);
        if ((*app)->card_font) font_destroy(&(*app)->card_font);
        if ((*app)->card_font_lg) font_destroy(&(*app)->card_font_lg);
        if ((*app)->header_font) font_destroy(&(*app)->header_font);
        if ((*app)->regular_font) font_destroy(&(*app)->regular_font);
        if ((*app)->bold_font) font_destroy(&(*app)->bold_font);
        bmutex_close(&(*app)->solve_mutex);
        bmutex_close(&(*app)->icm_matrix_mutex);
        heap_delete(app, App);
    }
}

#include <osapp/osmain.h>
osmain(i_create, i_destroy, "", App)
