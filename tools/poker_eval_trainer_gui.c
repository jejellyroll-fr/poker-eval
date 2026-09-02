/* Cross-platform poker-eval trainer GUI: pure C + SDL2. */
#ifdef _WIN32
#include <SDL.h>
#else
#include <SDL2/SDL.h>
#endif

#include <ctype.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <signal.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

#include "pe_sol_format.h"
#include "poker_eval/solver/pe_monker.h"
#include "poker_eval/solver/pe_runtime.h"
#include "pe_tree_json.h"
#include "pe_tree_layout.h"

#ifdef _WIN32
#define PE_GUI_PATH_SEPARATOR '\\'
#define PE_GUI_EXE_SUFFIX ".exe"
#else
#define PE_GUI_PATH_SEPARATOR '/'
#define PE_GUI_EXE_SUFFIX ""
#endif

#define WINDOW_WIDTH 1440
#define WINDOW_HEIGHT 900
#define MAX_ACTIONS 256
#define PE_PREFLOP_GUI_MAX_PLAYERS 6

typedef struct {
    uint64_t key;
    int actions;
    double probability[MAX_ACTIONS];
} spot_t;

typedef struct {
    uint64_t key;
    int action;
    char label[96];
    char street[24];
    char board[64];
    char runout[64];
    char position[32];
    double pot;
    int has_pot;
} label_t;

typedef struct {
    uint64_t key;
    int selected;
    int best;
    double selected_probability;
    double best_probability;
} event_t;

typedef struct {
    spot_t *spots;
    size_t spot_count;
    label_t *labels;
    size_t label_count;
    event_t *events;
    size_t event_count;
    size_t event_capacity;
    size_t current;
    int has_current;
    int answered_current;
    int selected_action;
    int best_action;
    unsigned random_state;
    int score;
    int answered;
    int streak;
    int difficulty;
    double probability_loss;
    char solution_path[1024];
    char labels_path[1024];
    char session_path[1024];
    char action_names[MAX_ACTIONS][32];
    char feedback[256];
    int running;
    int page;
    int training_action_scroll;
    int range_editor_player;
    uint8_t range_matrix[2][13][13];
    uint64_t selected_key;
    size_t selected_spot;
    char game_name[24];
    char board_text[48];
    char range_oop[4096];
    char range_ip[4096];
    int iterations;
    int sample_batch_size;
    int game_index;
    int player_count;
    int engine_index;
    int focus_field;
    char tree_path[1024];
    char mkr_path[1024];
    char executable_dir[1024];
    char solver_status[256];
    char solver_result[1024];
    /* Background solve: the worker thread owns solve_staging and flips
     * solve_state to SOLVE_DONE; the main loop drains the handoff. */
    SDL_Thread *solve_thread;
    SDL_atomic_t solve_state;
    SDL_atomic_t solve_cancel;
    SDL_atomic_t solve_child_pid;
    int solve_exit_status;
    char solve_command[32768];
    char solve_staging[1024];
    char solve_success[64];
    char solve_failure[64];
    /* Loaded tree topology rendered by draw_tree. */
    pe_tree_json_t tree_view;
    pe_monker_tree_header_t tree_header;
    uint32_t tree_combo_count;
    uint8_t tree_hole_cards;
    int tree_header_valid;
    int tree_ranges_valid;
    int tree_json_pending;
    char tree_json_path[1024];
    pe_runtime_capabilities_t runtime;
    int runtime_valid;
} app_t;

typedef struct { int x; int y; int w; int h; } rect_t;

static uint8_t font[128][7];

static void glyph(char character, uint8_t a, uint8_t b, uint8_t c, uint8_t d,
                  uint8_t e, uint8_t f, uint8_t g)
{
    font[(unsigned char)character][0] = a; font[(unsigned char)character][1] = b;
    font[(unsigned char)character][2] = c; font[(unsigned char)character][3] = d;
    font[(unsigned char)character][4] = e; font[(unsigned char)character][5] = f;
    font[(unsigned char)character][6] = g;
}

static void font_init(void)
{
    memset(font, 0, sizeof(font));
    glyph('A',14,17,17,31,17,17,17); glyph('B',30,17,17,30,17,17,30); glyph('C',15,16,16,16,16,16,15);
    glyph('D',30,17,17,17,17,17,30); glyph('E',31,16,16,30,16,16,31); glyph('F',31,16,16,30,16,16,16);
    glyph('G',15,16,16,23,17,17,15); glyph('H',17,17,17,31,17,17,17); glyph('I',31,4,4,4,4,4,31);
    glyph('J',7,2,2,2,2,18,12); glyph('K',17,18,20,24,20,18,17); glyph('L',16,16,16,16,16,16,31);
    glyph('M',17,27,21,21,17,17,17); glyph('N',17,25,25,21,19,19,17); glyph('O',14,17,17,17,17,17,14);
    glyph('P',30,17,17,30,16,16,16); glyph('Q',14,17,17,17,21,18,13); glyph('R',30,17,17,30,20,18,17);
    glyph('S',15,16,16,14,1,1,30); glyph('T',31,4,4,4,4,4,4); glyph('U',17,17,17,17,17,17,14);
    glyph('V',17,17,17,17,17,10,4); glyph('W',17,17,17,21,21,27,17); glyph('X',17,17,10,4,10,17,17);
    glyph('Y',17,17,10,4,4,4,4); glyph('Z',31,1,2,4,8,16,31);
    glyph('0',14,17,19,21,25,17,14); glyph('1',4,12,4,4,4,4,14); glyph('2',14,17,1,2,4,8,31);
    glyph('3',30,1,1,14,1,1,30); glyph('4',2,6,10,18,31,2,2); glyph('5',31,16,16,30,1,1,30);
    glyph('6',14,16,16,30,17,17,14); glyph('7',31,1,2,4,8,8,8); glyph('8',14,17,17,14,17,17,14);
    glyph('9',14,17,17,15,1,1,14);
    glyph(':',0,4,4,0,4,4,0); glyph('.',0,0,0,0,0,12,12); glyph(',',0,0,0,0,0,12,8);
    glyph('-',0,0,0,31,0,0,0); glyph('/',1,2,4,8,16,0,0); glyph('%',25,26,4,8,19,11,3);
    glyph('(',2,4,8,8,8,4,2); glyph(')',8,4,2,2,2,4,8); glyph('+',0,4,4,31,4,4,0);
    glyph('?',14,17,1,2,4,0,4); glyph('!',4,4,4,4,4,0,4); glyph('#',10,31,10,10,31,10,0);
}

static void text(SDL_Renderer *renderer, int x, int y, const char *value, int scale, SDL_Color color)
{
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
        unsigned char character = (unsigned char)toupper(*p);
        if (character >= 128u || character == ' ') { x += 6 * scale; continue; }
        for (int row = 0; row < 7; ++row)
            for (int column = 0; column < 5; ++column)
                if ((font[character][row] & (1u << (4 - column))) != 0u) {
                    SDL_Rect pixel = {x + column * scale, y + row * scale, scale, scale};
                    SDL_RenderFillRect(renderer, &pixel);
                }
        x += 6 * scale;
    }
}

static void fill(SDL_Renderer *renderer, rect_t rect, SDL_Color color)
{
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_Rect target = {rect.x, rect.y, rect.w, rect.h}; SDL_RenderFillRect(renderer, &target);
}

static void border(SDL_Renderer *renderer, rect_t rect, SDL_Color color)
{
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_Rect target = {rect.x, rect.y, rect.w, rect.h}; SDL_RenderDrawRect(renderer, &target);
}

static void copy_field(char *destination, size_t capacity, const char *source);

static int load_solution(app_t *app, const char *path)
{
    FILE *file = fopen(path, "rb"); long size; unsigned char *data; size_t offset = PE_SOL_FMT_HEADER_SIZE; uint64_t count; spot_t *spots;
    pe_sol_fmt_status_t status;
    if (!file || fseek(file, 0, SEEK_END) != 0) { if (file) fclose(file); return -1; }
    size = ftell(file); rewind(file); if (size < 32) { fclose(file); copy_field(app->solver_status, sizeof(app->solver_status), "Truncated .pe_sol header"); return -1; }
    data = (unsigned char *)malloc((size_t)size);
    if (!data || fread(data, 1u, (size_t)size, file) != (size_t)size) { free(data); fclose(file); return -1; }
    fclose(file);
    status = pe_sol_fmt_parse_header(data, (size_t)size, &count);
    if (status != PE_SOL_FMT_OK) {
        static const char *messages[] = {
            "", "Truncated .pe_sol header", "Not a PESOL001 v1 solution",
            "Compressed (zstd) .pe_sol not supported by the GUI",
            "Unknown .pe_sol flags", "Truncated .pe_sol records"};
        free(data);
        copy_field(app->solver_status, sizeof(app->solver_status), messages[status]);
        return -1;
    }
    if (count > SIZE_MAX / sizeof(*spots)) { free(data); return -1; }
    spots = (spot_t *)calloc((size_t)count, sizeof(*spots)); if (!spots && count) { free(data); return -1; }
    for (uint64_t i = 0; i < count; ++i) {
        uint32_t actions;
        if (offset + 12u > (size_t)size) { free(spots); free(data); return -1; }
        spots[i].key = pe_sol_fmt_u64(data, offset); actions = pe_sol_fmt_u32(data, offset + 8u); offset += 12u;
        if (actions == 0u || actions > MAX_ACTIONS || actions > ((size_t)size - offset) / 2u) { free(spots); free(data); return -1; }
        spots[i].actions = (int)actions;
        {
            uint16_t quantized[MAX_ACTIONS];
            for (uint32_t action = 0; action < actions; ++action)
                quantized[action] = pe_sol_fmt_u16(data, offset + action * 2u);
            pe_sol_fmt_dequantize_row(quantized, (int)actions, spots[i].probability);
        }
        offset += (size_t)actions * 2u;
    }
    free(data); free(app->spots); app->spots = spots; app->spot_count = (size_t)count;
    snprintf(app->solution_path, sizeof(app->solution_path), "%s", path); return 0;
}

static uint64_t parse_key(const char *text)
{ return strtoull(text, NULL, 0); }

static int load_labels(app_t *app, const char *path)
{
    FILE *file = fopen(path, "r"); char line[512]; label_t *labels = NULL; size_t used = 0u, capacity = 0u;
    if (!file) return -1;
    while (fgets(line, sizeof(line), file)) {
        char *fields[10] = {0}; char *field; int count = 0;
        field = strtok(line, ",\r\n"); while (field && count < 10) { fields[count++] = field; field = strtok(NULL, ",\r\n"); }
        if (count < 3 || strcmp(fields[0], "key") == 0) continue;
        if (used == capacity) { size_t next = capacity ? capacity * 2u : 32u; label_t *grown = (label_t *)realloc(labels, next * sizeof(*grown)); if (!grown) { free(labels); fclose(file); return -1; } labels = grown; capacity = next; }
        memset(&labels[used], 0, sizeof(labels[used])); labels[used].key = parse_key(fields[0]);
        if (count >= 9) {
            snprintf(labels[used].street, sizeof(labels[used].street), "%s", fields[1]); snprintf(labels[used].board, sizeof(labels[used].board), "%s", fields[2]);
            snprintf(labels[used].runout, sizeof(labels[used].runout), "%s", fields[3]); snprintf(labels[used].position, sizeof(labels[used].position), "%s", fields[4]);
            labels[used].pot = strtod(fields[5], NULL); labels[used].has_pot = 1; labels[used].action = atoi(fields[6]); snprintf(labels[used].label, sizeof(labels[used].label), "%s", fields[7]);
        } else if (count >= 5) {
            snprintf(labels[used].street, sizeof(labels[used].street), "%s", fields[1]); snprintf(labels[used].board, sizeof(labels[used].board), "%s", fields[2]);
            labels[used].action = atoi(fields[3]); snprintf(labels[used].label, sizeof(labels[used].label), "%s", fields[4]);
        } else { labels[used].action = atoi(fields[1]); snprintf(labels[used].label, sizeof(labels[used].label), "%s", fields[2]); }
        ++used;
    }
    fclose(file); free(app->labels); app->labels = labels; app->label_count = used; snprintf(app->labels_path, sizeof(app->labels_path), "%s", path); return 0;
}

static void load_action_names(app_t *app, const char *text)
{
    char buffer[1024];
    char *field;
    int action = 0;
    if (!app || !text) return;
    snprintf(buffer, sizeof(buffer), "%s", text);
    field = strtok(buffer, ",");
    while (field && action < MAX_ACTIONS)
    {
        while (*field == ' ' || *field == '\t') ++field;
        snprintf(app->action_names[action], sizeof(app->action_names[action]),
                 "%s", field);
        ++action;
        field = strtok(NULL, ",");
    }
}

static int load_session_summary(const char *path, int *answered,
                                int *best_answers, double *probability_loss)
{
    FILE *file;
    long size;
    char *data;
    const char *fields[] = {"\"answered\":", "\"best_answers\":",
                            "\"probability_loss\":"};
    const size_t field_lengths[] = {11u, 15u, 18u};
    double parsed[3];
    if (!path || !answered || !best_answers || !probability_loss) return -1;
    file = fopen(path, "rb");
    if (!file || fseek(file, 0, SEEK_END) != 0) { if (file) fclose(file); return -1; }
    size = ftell(file);
    if (size < 0 || size > 16 * 1024 * 1024) { fclose(file); return -1; }
    rewind(file);
    data = (char *)malloc((size_t)size + 1u);
    if (!data || fread(data, 1u, (size_t)size, file) != (size_t)size) { free(data); fclose(file); return -1; }
    fclose(file); data[size] = '\0';
    for (size_t i = 0u; i < 3u; ++i) {
        const char *at = strstr(data, fields[i]); char *end = NULL;
        if (!at) { free(data); return -1; }
        parsed[i] = strtod(at + field_lengths[i], &end);
        if (end == at + field_lengths[i]) { free(data); return -1; }
    }
    *answered = parsed[0] < 0.0 ? 0 : (int)parsed[0];
    *best_answers = parsed[1] < 0.0 ? 0 : (int)parsed[1];
    *probability_loss = parsed[2] >= 0.0 ? parsed[2] : 0.0;
    free(data);
    return 0;
}

static const label_t *label_for(const app_t *app, uint64_t key, int action)
{ for (size_t i = 0; i < app->label_count; ++i) if (app->labels[i].key == key && app->labels[i].action == action) return &app->labels[i]; return NULL; }

static const label_t *metadata_for(const app_t *app, uint64_t key)
{ for (size_t i = 0; i < app->label_count; ++i) if (app->labels[i].key == key) return &app->labels[i]; return NULL; }

static void next_spot(app_t *app)
{
    size_t selected = 0u; size_t eligible = 0u; int has_selected = 0;
    if (app->spot_count == 0u) return;
    for (size_t i = 0; i < app->spot_count; ++i) {
        double best = 0.0; for (int action = 0; action < app->spots[i].actions; ++action) if (app->spots[i].probability[action] > best) best = app->spots[i].probability[action];
        if (app->difficulty == 1 || best < 0.8) {
            ++eligible;
            /* Reservoir sampling keeps one uniformly selected eligible spot
               without imposing a fixed 4096-entry ceiling. */
            app->random_state = app->random_state * 1664525u + 1013904223u;
            if (!has_selected || (size_t)(app->random_state % eligible) == 0u) {
                selected = i;
                has_selected = 1;
            }
        }
    }
    if (!has_selected) return; app->current = selected; app->has_current = 1; app->answered_current = 0; app->selected_action = -1; app->best_action = -1; app->training_action_scroll = 0;
}

static void answer(app_t *app, int selected)
{
    spot_t *spot; int best = 0;
    if (!app->has_current || app->answered_current) return; spot = &app->spots[app->current];
    for (int action = 1; action < spot->actions; ++action) if (spot->probability[action] > spot->probability[best]) best = action;
    app->selected_action = selected; app->best_action = best;
    app->answered_current = 1; app->answered++;
    if (app->event_count == app->event_capacity) { size_t next = app->event_capacity ? app->event_capacity * 2u : 32u; event_t *grown = (event_t *)realloc(app->events, next * sizeof(*grown)); if (grown) { app->events = grown; app->event_capacity = next; } }
    if (app->event_count < app->event_capacity) app->events[app->event_count++] = (event_t){spot->key, selected, best, spot->probability[selected], spot->probability[best]};
    if (selected == best) { app->score++; app->streak++; snprintf(app->feedback, sizeof(app->feedback), "Bonne decision - meilleure frequence : %.1f%%", spot->probability[best] * 100.0); }
    else { const label_t *label = label_for(app, spot->key, best); app->streak = 0; app->probability_loss += spot->probability[best] - spot->probability[selected]; snprintf(app->feedback, sizeof(app->feedback), "A revoir : %s (%.1f%%)", label ? label->label : "option optimale", spot->probability[best] * 100.0); }
    if (app->streak >= 3 && app->difficulty < 5) ++app->difficulty; if (!app->streak && app->difficulty > 1) --app->difficulty;
}

static int inside(rect_t rect, int x, int y)
{ return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h; }

static const char *basename_of(const char *path)
{ const char *slash = strrchr(path, '/'); const char *backslash = strrchr(path, '\\'); if (backslash && (!slash || backslash > slash)) slash = backslash; return slash ? slash + 1 : path; }

static const char *game_name_for(int index)
{
    static const char *names[] = {"holdem", "plo4", "plo5", "plo6"};
    return index >= 0 && index < 4 ? names[index] : names[0];
}

static const char *game_label_for(int index)
{
    static const char *labels[] = {"HOLDEM", "PLO4", "PLO5", "PLO6"};
    return index >= 0 && index < 4 ? labels[index] : labels[0];
}

static const char *engine_label_for(int index)
{
    static const char *labels[] = {"VECTOR CPU", "LEGACY CFR", "GPU VECTOR",
                                   "AUTO V3"};
    return index >= 0 && index < 4 ? labels[index] : labels[0];
}

static const char *runtime_backend_state(
    const pe_runtime_backend_info_t *backend)
{
    if (!backend || !backend->compiled)
        return "not-built";
    if (!backend->runtime_available)
        return "unavailable";
    if (!backend->validated)
        return "rejected";
    return "ready";
}

static int tree_board_count(int street)
{
    if (street == 1) return 3;
    if (street == 2) return 4;
    if (street == 3) return 5;
    return 0;
}

static const char *tree_street_label(int street)
{
    static const char *labels[] = {"PREFLOP", "FLOP", "TURN", "RIVER"};
    return street >= 0 && street < 4 ? labels[street] : "UNKNOWN STREET";
}

static int gui_game_index(enum_game_t game)
{
    if (game == game_holdem) return 0;
    if (game == game_omaha) return 1;
    if (game == game_omaha5) return 2;
    if (game == game_omaha6) return 3;
    return -1;
}

static void copy_field(char *destination, size_t capacity, const char *source);
static void range_matrix_sync_from_fields(app_t *app);

static size_t gui_bounded_length(const char *text, size_t capacity)
{
    size_t length = 0u;
    if (text == NULL)
        return 0u;
    while (length < capacity && text[length] != '\0')
        ++length;
    return length;
}

static void set_default_ranges_for_game(app_t *app)
{
    static const char *oop[] = {"AsKs", "AsKsQd3c", "AsKsQd3c9h", "AsKsQd3c9h5h"};
    static const char *ip[] = {"AhKh", "AhKhJdTc", "AhKhJdTc8s", "AhKhJdTc8s4s"};
    if (!app || app->game_index < 0 || app->game_index > 3)
        return;
    copy_field(app->range_oop, sizeof(app->range_oop), oop[app->game_index]);
    copy_field(app->range_ip, sizeof(app->range_ip), ip[app->game_index]);
    range_matrix_sync_from_fields(app);
}

static void copy_field(char *destination, size_t capacity, const char *source)
{
    if (!destination || capacity == 0u)
        return;
    snprintf(destination, capacity, "%s", source ? source : "");
}

static void shell_quote(const char *source, char *destination, size_t capacity)
{
    size_t used = 0u;
    if (!destination || capacity == 0u)
        return;
    destination[0] = '\0';
#ifdef _WIN32
    if (used + 1u < capacity) destination[used++] = '"';
    for (const char *at = source ? source : ""; *at && used + 2u < capacity; ++at)
    {
        if (*at == '"' && used + 2u < capacity) destination[used++] = '\\';
        destination[used++] = *at;
    }
    if (used + 1u < capacity) destination[used++] = '"';
#else
    if (used + 1u < capacity) destination[used++] = '\'';
    for (const char *at = source ? source : ""; *at && used + 5u < capacity; ++at)
    {
        if (*at == '\'')
        {
            destination[used++] = '\''; destination[used++] = '\\';
            destination[used++] = '\''; destination[used++] = '\'';
        }
        else destination[used++] = *at;
    }
    if (used + 1u < capacity) destination[used++] = '\'';
#endif
    destination[used] = '\0';
}

static int card_token_count(const char *text)
{
    char copy[256];
    char *token;
    int count = 0;
    if (!text || !*text)
        return 0;
    snprintf(copy, sizeof(copy), "%s", text);
    token = strtok(copy, " ,;\t\r\n");
    while (token) {
        size_t length = gui_bounded_length(token, 4u);
        if (length < 2u || length > 3u)
            return -1;
        ++count;
        token = strtok(NULL, " ,;\t\r\n");
    }
    return count;
}

static const char *starter_range_for_player(const app_t *app, int player)
{
    static const char *holdem[] = {
        "AsKs", "AhKh", "AdKd", "AcKc", "QsQh", "QdQc", "JsJh", "JdJc"};
    static const char *plo4[] = {
        "AsKsQd3c", "AhKhJdTc", "AdKdQh2s", "AcKcQs2d",
        "9s9h8d6c", "8c8h7s6d", "5s5h4d4c", "6h6s5d5c"};
    static const char *plo5[] = {
        "AsKsQd3c9h", "AhKhJdTc8s", "AdKdQh2s3h", "AcKcQs2d3s",
        "9s9h8d6c5c", "8c8h7s6d4d", "5s5h4c4h7h", "6h6s5d5c4s"};
    static const char *plo6[] = {
        "AsKsQd3c9h5h", "AhKhJdTc8s4s", "AdKdQh2s3h6h", "AcKcQs2d7s6d",
        "9s9h8d6c5c4c", "8c8h7s6d4d3d", "5s5h4c4h7h6c", "6h6s5d5c4s3s"};
    const char *const *ranges = holdem;
    if (!app || player < 0 || player > 7)
        return "";
    if (player == 0) return app->range_oop;
    if (player == 1) return app->range_ip;
    if (app->game_index == 1) ranges = plo4;
    else if (app->game_index == 2) ranges = plo5;
    else if (app->game_index == 3) ranges = plo6;
    return ranges[player];
}

enum { SOLVE_IDLE = 0, SOLVE_RUNNING = 1, SOLVE_DONE = 2 };

/* Parse the small, internally-built command language used by the GUI. The
 * parser understands the quoting emitted by shell_quote, but never invokes a
 * shell, so paths and ranges remain data rather than executable input. */
#ifndef _WIN32
static int command_to_argv(char *command, char **argv, size_t capacity)
{
    char *read = command;
    char *write = command;
    char *token_start = NULL;
    size_t count = 0u;
    int in_single = 0;
    int in_double = 0;
    int escaped = 0;
    int token = 0;

    if (!command || !argv || capacity < 2u)
        return -1;
    while (*read)
    {
        char value = *read++;
        if (escaped)
        {
            if (!token_start)
                token_start = write;
            *write++ = value;
            escaped = 0;
            token = 1;
            continue;
        }
        if (!in_single && value == '\\')
        {
            if (!token_start)
                token_start = write;
            escaped = 1;
            token = 1;
            continue;
        }
        if (!in_double && value == '\'')
        {
            if (!token_start)
                token_start = write;
            in_single = !in_single;
            token = 1;
            continue;
        }
        if (!in_single && value == '"')
        {
            if (!token_start)
                token_start = write;
            in_double = !in_double;
            token = 1;
            continue;
        }
        if (!in_single && !in_double &&
            (value == ' ' || value == '\t' || value == '\r' || value == '\n'))
        {
            if (token)
            {
                if (count + 1u >= capacity)
                    return -1;
                *write++ = '\0';
                argv[count++] = token_start;
                token_start = NULL;
                token = 0;
            }
            continue;
        }
        *write++ = value;
        token = 1;
    }
    if (escaped || in_single || in_double)
        return -1;
    if (token)
    {
        if (count + 1u >= capacity)
            return -1;
        *write++ = '\0';
        argv[count++] = token_start;
    }
    argv[count] = NULL;
    return (int)count;
}

#endif

static int gui_solver_executable_allowed(const char *path)
{
    const char *name;
    const char *backslash;

    if (path == NULL || path[0] == '\0')
        return 0;
    name = strrchr(path, '/');
    backslash = strrchr(path, '\\');
    if (backslash != NULL && (name == NULL || backslash > name))
        name = backslash;
    name = name != NULL ? name + 1 : path;
    return strcmp(name, "pe-vector-sim") == 0 ||
           strcmp(name, "pe-preflop-solve") == 0 ||
           strcmp(name, "pe-monker-validate") == 0 ||
           strcmp(name, "mpf_run_with_metrics") == 0;
}

#ifdef _WIN32
static int gui_command_executable_allowed(const char *command)
{
    char executable[1024];
    const char *cursor = command;
    size_t length = 0u;

    if (!cursor)
        return 0;
    while (*cursor == ' ' || *cursor == '\t')
        ++cursor;
    if (*cursor == '"')
    {
        ++cursor;
        while (cursor[length] && cursor[length] != '"')
            ++length;
    }
    else
    {
        while (cursor[length] && cursor[length] != ' ' &&
               cursor[length] != '\t')
            ++length;
    }
    if (length == 0u || length >= sizeof(executable))
        return 0;
    for (size_t i = 0u; i < length; ++i)
        executable[i] = cursor[i];
    executable[length] = '\0';
    return gui_solver_executable_allowed(executable);
}
#endif

/* Run an internally-built command without a shell and capture its output into
 * destination. Returns the process exit status, or -1 when it cannot start. */
static int run_command_capture(app_t *app, const char *command,
                               char *destination, size_t capacity)
{
    char output[4096];
    FILE *stream;
    size_t used = 0u;
    int exit_status;
#ifdef _WIN32
    SECURITY_ATTRIBUTES security;
    STARTUPINFOA startup;
    PROCESS_INFORMATION process;
    HANDLE read_handle;
    HANDLE write_handle;
    char command_copy[32768];
    size_t command_length;
    int descriptor;

    if (!app || !command)
        return -1;
    command_length = gui_bounded_length(command, sizeof(command_copy));
    if (command_length >= sizeof(command_copy) ||
        !gui_command_executable_allowed(command))
        return -1;
    for (size_t i = 0u; i <= command_length; ++i)
        command_copy[i] = command[i];
    memset(&security, 0, sizeof(security));
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    if (!CreatePipe(&read_handle, &write_handle, &security, 0))
        return -1;
    if (!SetHandleInformation(read_handle, HANDLE_FLAG_INHERIT, 0))
    {
        CloseHandle(read_handle);
        CloseHandle(write_handle);
        return -1;
    }
    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = write_handle;
    startup.hStdError = write_handle;
    memset(&process, 0, sizeof(process));
    if (!CreateProcessA(NULL, command_copy, NULL, NULL, TRUE, 0, NULL, NULL,
                        &startup, &process))
    {
        CloseHandle(read_handle);
        CloseHandle(write_handle);
        return -1;
    }
    CloseHandle(process.hThread);
    CloseHandle(write_handle);
    SDL_AtomicSet(&app->solve_child_pid, (int)process.dwProcessId);
    if (SDL_AtomicGet(&app->solve_cancel) != 0)
        (void)TerminateProcess(process.hProcess, 1u);
    descriptor = _open_osfhandle((intptr_t)read_handle, _O_RDONLY | _O_BINARY);
    if (descriptor < 0)
    {
        CloseHandle(read_handle);
        (void)TerminateProcess(process.hProcess, 1u);
        (void)WaitForSingleObject(process.hProcess, INFINITE);
        CloseHandle(process.hProcess);
        SDL_AtomicSet(&app->solve_child_pid, 0);
        return -1;
    }
    stream = _fdopen(descriptor, "r");
    if (!stream)
    {
        _close(descriptor);
        (void)TerminateProcess(process.hProcess, 1u);
        (void)WaitForSingleObject(process.hProcess, INFINITE);
        CloseHandle(process.hProcess);
        SDL_AtomicSet(&app->solve_child_pid, 0);
        return -1;
    }
#else
    char command_copy[32768];
    char *argv[64];
    int pipe_fds[2];
    pid_t child;
    size_t command_length;
    int spawn_status;
    posix_spawn_file_actions_t actions;

    if (!app || !command)
        return -1;
    command_length = gui_bounded_length(command, sizeof(command_copy));
    if (command_length >= sizeof(command_copy))
        return -1;
    for (size_t i = 0u; i < command_length; ++i)
        command_copy[i] = command[i];
    command_copy[command_length] = '\0';
    if (command_to_argv(command_copy, argv,
                       sizeof(argv) / sizeof(argv[0])) < 1 ||
        argv[0] == NULL ||
        !gui_solver_executable_allowed(argv[0]) || pipe(pipe_fds) != 0)
        return -1;
    if (posix_spawn_file_actions_init(&actions) != 0)
    {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return -1;
    }
    if (posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], STDOUT_FILENO) != 0 ||
        posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], STDERR_FILENO) != 0 ||
        posix_spawn_file_actions_addclose(&actions, pipe_fds[0]) != 0 ||
        posix_spawn_file_actions_addclose(&actions, pipe_fds[1]) != 0)
    {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        posix_spawn_file_actions_destroy(&actions);
        return -1;
    }
    spawn_status = posix_spawnp(&child, argv[0], &actions, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(pipe_fds[1]);
    if (spawn_status != 0)
    {
        close(pipe_fds[0]);
        return -1;
    }
    SDL_AtomicSet(&app->solve_child_pid, (int)child);
    if (SDL_AtomicGet(&app->solve_cancel) != 0)
        (void)kill(child, SIGTERM);
    stream = fdopen(pipe_fds[0], "r");
    if (!stream)
    {
        close(pipe_fds[0]);
        if (SDL_AtomicGet(&app->solve_cancel) != 0)
            (void)kill(child, SIGTERM);
        (void)waitpid(child, NULL, 0);
        SDL_AtomicSet(&app->solve_child_pid, 0);
        return -1;
    }
#endif
#ifdef _WIN32
    if (!stream)
        return -1;
#endif
    if (capacity > 0u)
        destination[0] = '\0';
    while (fgets(output, sizeof(output), stream))
    {
        size_t length = gui_bounded_length(output, sizeof(output));
        if (capacity == 0u || used + 1u >= capacity)
            continue; /* keep draining so the child does not block */
        if (length >= capacity - used)
            length = capacity - used - 1u;
        if (length > 0u)
        {
            for (size_t i = 0u; i < length; ++i)
                destination[used + i] = output[i];
            used += length;
            destination[used] = '\0';
        }
    }
#ifdef _WIN32
    {
        int close_status = fclose(stream);
        DWORD process_status = 0u;
        DWORD wait_status = WaitForSingleObject(process.hProcess, INFINITE);
        if (wait_status != WAIT_OBJECT_0 ||
            !GetExitCodeProcess(process.hProcess, &process_status))
            exit_status = -1;
        else
            exit_status = (int)process_status;
        CloseHandle(process.hProcess);
        SDL_AtomicSet(&app->solve_child_pid, 0);
        if (close_status != 0)
            return -1;
    }
#else
    {
        int close_status = fclose(stream);
        int wait_status = waitpid(child, &exit_status, 0);
        SDL_AtomicSet(&app->solve_child_pid, 0);
        if (close_status != 0 || wait_status < 0)
            return -1;
    }
    if (WIFEXITED(exit_status))
        exit_status = WEXITSTATUS(exit_status);
    else if (WIFSIGNALED(exit_status))
        exit_status = 128 + WTERMSIG(exit_status);
    else
        exit_status = -1;
#endif
    return exit_status;
}

static int solve_worker(void *userdata)
{
    app_t *app = (app_t *)userdata;
    app->solve_exit_status =
        run_command_capture(app, app->solve_command, app->solve_staging,
                            sizeof(app->solve_staging));
    SDL_AtomicSet(&app->solve_state, SOLVE_DONE);
    return 0;
}

/* Start the solver subprocess on a worker thread so the event loop keeps
 * rendering. Falls back to a blocking run when the thread cannot start. */
static int launch_solver(app_t *app, const char *command, const char *success,
                         const char *failure)
{
    if (SDL_AtomicGet(&app->solve_state) == SOLVE_RUNNING)
    {
        copy_field(app->solver_status, sizeof(app->solver_status),
                   "Solve already running");
        return -1;
    }
    copy_field(app->solve_command, sizeof(app->solve_command), command);
    copy_field(app->solve_success, sizeof(app->solve_success), success);
    copy_field(app->solve_failure, sizeof(app->solve_failure), failure);
    app->solve_staging[0] = '\0';
    app->solver_result[0] = '\0';
    app->solve_exit_status = -1;
    SDL_AtomicSet(&app->solve_cancel, 0);
    SDL_AtomicSet(&app->solve_child_pid, 0);
    SDL_AtomicSet(&app->solve_state, SOLVE_RUNNING);
    app->solve_thread = SDL_CreateThread(solve_worker, "pe-solve", app);
    if (!app->solve_thread)
    {
        int exit_status = run_command_capture(app, command, app->solver_result,
                                              sizeof(app->solver_result));
        SDL_AtomicSet(&app->solve_state, SOLVE_IDLE);
        copy_field(app->solver_status, sizeof(app->solver_status),
                   exit_status == 0 ? success : failure);
        return exit_status == 0 ? 0 : -1;
    }
    copy_field(app->solver_status, sizeof(app->solver_status),
               "Solving in background...");
    return 0;
}

/* Main-loop side of the handoff: reclaim the finished worker and publish its
 * output. Safe to call every frame; does nothing while a solve is in flight. */
static int load_tree_json_file(app_t *app, const char *path);

/* Read the binary header before rendering or solving.  A Monker .tree does
 * not contain a board, so the UI must expose that fact instead of silently
 * retaining the previous spot's board. */
static int load_tree_context(app_t *app, const char *path)
{
    pe_monker_range_set_t ranges;
    pe_monker_combo_layout_t layout;
    char message[256];
    int game_index;

    if (!app || !path)
        return -1;
    memset(&ranges, 0, sizeof(ranges));
    if (pe_monker_tree_read_header(path, &app->tree_header) != PE_MONKER_OK)
    {
        app->tree_header_valid = 0;
        copy_field(app->solver_status, sizeof(app->solver_status),
                   "Tree rejected: invalid Monker header");
        return -1;
    }
    app->tree_header_valid = 1;
    app->tree_ranges_valid = 0;
    app->tree_combo_count = 0u;
    app->tree_hole_cards = 0u;
    if (pe_monker_tree_read_ranges(path, &ranges) != PE_MONKER_OK ||
        pe_monker_combo_layout_from_count(ranges.combo_count, &layout) !=
            PE_MONKER_OK)
    {
        pe_monker_range_set_free(&ranges);
        app->tree_header_valid = 0;
        copy_field(app->solver_status, sizeof(app->solver_status),
                   "Tree rejected: embedded ranges/combo layout unavailable");
        return -1;
    }
    app->tree_ranges_valid = 1;
    app->tree_combo_count = ranges.combo_count;
    app->tree_hole_cards = layout.hole_cards;
    game_index = gui_game_index(layout.game);
    if (game_index >= 0)
    {
        app->game_index = game_index;
        copy_field(app->game_name, sizeof(app->game_name),
                   game_label_for(game_index));
    }
    pe_monker_range_set_free(&ranges);
    if (app->tree_header.player_count >= 2u &&
        app->tree_header.player_count <= 8u)
        app->player_count = (int)app->tree_header.player_count;

    /* A stale board is more dangerous than an empty board: clear it and make
     * the missing input explicit. The tree's street tells us how many cards
     * the user must provide, but the cards themselves are not in .tree. */
    copy_field(app->board_text, sizeof(app->board_text), "");
    snprintf(message, sizeof(message),
             "Tree loaded: %s, %u players. Board is not stored in .tree; enter %d cards.",
             tree_street_label(app->tree_header.street),
             app->tree_header.player_count,
             tree_board_count(app->tree_header.street));
    copy_field(app->solver_status, sizeof(app->solver_status), message);
    return 0;
}

static void finish_pending_solve(app_t *app)
{
    if (SDL_AtomicGet(&app->solve_state) != SOLVE_DONE)
        return;
    if (app->solve_thread)
    {
        SDL_WaitThread(app->solve_thread, NULL);
        app->solve_thread = NULL;
    }
    SDL_AtomicSet(&app->solve_state, SOLVE_IDLE);
    copy_field(app->solver_result, sizeof(app->solver_result),
               app->solve_staging);
    copy_field(app->solver_status, sizeof(app->solver_status),
               app->solve_exit_status == 0 ? app->solve_success
                                           : app->solve_failure);
    if (app->tree_json_pending)
    {
        app->tree_json_pending = 0;
        if (app->solve_exit_status == 0)
            load_tree_json_file(app, app->tree_json_path);
    }
}

static void cancel_pending_solve(app_t *app)
{
    if (!app || SDL_AtomicGet(&app->solve_state) != SOLVE_RUNNING)
        return;
    SDL_AtomicSet(&app->solve_cancel, 1);
#ifdef _WIN32
    {
        DWORD child = (DWORD)SDL_AtomicGet(&app->solve_child_pid);
        if (child != 0u)
        {
            HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, child);
            if (process)
            {
                (void)TerminateProcess(process, 1u);
                CloseHandle(process);
            }
        }
    }
#else
    {
        int child = SDL_AtomicGet(&app->solve_child_pid);
        if (child > 0)
            (void)kill((pid_t)child, SIGTERM);
    }
#endif
}

/* Load an mpf-format tree JSON file into the tree view. */
static int load_tree_json_file(app_t *app, const char *path)
{
    FILE *file = fopen(path, "rb"); long size; char *data;
    pe_tree_json_t parsed;
    char message[96];
    if (!file || fseek(file, 0, SEEK_END) != 0) { if (file) fclose(file); copy_field(app->solver_status, sizeof(app->solver_status), "Could not open tree JSON"); return -1; }
    size = ftell(file); rewind(file);
    if (size <= 0 || size > 64 * 1024 * 1024) { fclose(file); copy_field(app->solver_status, sizeof(app->solver_status), "Tree JSON too large"); return -1; }
    data = (char *)malloc((size_t)size + 1u);
    if (!data || fread(data, 1u, (size_t)size, file) != (size_t)size) { free(data); fclose(file); copy_field(app->solver_status, sizeof(app->solver_status), "Could not read tree JSON"); return -1; }
    fclose(file);
    data[size] = '\0';
    if (pe_tree_json_parse(data, (size_t)size, &parsed, 4096u) != 0 || parsed.count == 0u) {
        free(data); pe_tree_json_free(&parsed);
        copy_field(app->solver_status, sizeof(app->solver_status), "No readable nodes in tree JSON");
        return -1;
    }
    free(data);
    pe_tree_json_free(&app->tree_view);
    app->tree_view = parsed;
    snprintf(message, sizeof(message), "Tree topology loaded (%zu nodes)", parsed.count);
    copy_field(app->solver_status, sizeof(app->solver_status), message);
    return 0;
}

/* Ask pe-monker-validate to convert a Monker .tree into mpf JSON so the GUI
 * can render the topology without linking the solver library. */
static void start_tree_conversion(app_t *app)
{
    char executable[1200], quoted_executable[1400], tree[1200], output[1200];
    char command[8192];
    if (SDL_AtomicGet(&app->solve_state) == SOLVE_RUNNING)
        return; /* busy with another job; the next drop retries */
    if (app->executable_dir[0])
        snprintf(executable, sizeof(executable), "%s%cpe-monker-validate%s",
                 app->executable_dir, PE_GUI_PATH_SEPARATOR, PE_GUI_EXE_SUFFIX);
    else
        copy_field(executable, sizeof(executable), "pe-monker-validate");
    shell_quote(executable, quoted_executable, sizeof(quoted_executable));
    shell_quote(app->tree_path, tree, sizeof(tree));
    copy_field(app->tree_json_path, sizeof(app->tree_json_path), "pe_gui_tree.json");
    shell_quote(app->tree_json_path, output, sizeof(output));
    snprintf(command, sizeof(command), "%s --tree %s --tree-json %s",
             quoted_executable, tree, output);
    app->tree_json_pending = 1;
    launch_solver(app, command, "Tree converted", "Tree conversion failed");
}

static int run_vector_sim(app_t *app)
{
    char executable[1200], quoted_executable[1400];
    char board[160], tree[1200], mkr[1200];
    char command[32768];
    if (!app)
        return -1;
    if (app->engine_index != 0)
    {
        copy_field(app->solver_status, sizeof(app->solver_status),
                   "Selected engine is not wired to this spot runner yet");
        return -1;
    }
    if (app->tree_path[0] && app->tree_header_valid &&
        app->tree_header.street != 3) {
        copy_field(app->solver_status, sizeof(app->solver_status),
                   "Vector CPU currently supports loaded trees at river only; choose Legacy CFR for this street");
        return -1;
    }
    if (card_token_count(app->board_text) != 5) {
        copy_field(app->solver_status, sizeof(app->solver_status),
                   "Vector CPU needs a complete five-card river board");
        return -1;
    }
    if (!app->tree_path[0] && (!app->range_oop[0] || !app->range_ip[0])) {
        copy_field(app->solver_status, sizeof(app->solver_status),
                   "Enter an OOP and IP range before solving");
        return -1;
    }
    if (app->executable_dir[0])
        snprintf(executable, sizeof(executable), "%s%cpe-vector-sim%s",
                 app->executable_dir, PE_GUI_PATH_SEPARATOR,
                 PE_GUI_EXE_SUFFIX);
    else
        copy_field(executable, sizeof(executable), "pe-vector-sim");
    shell_quote(executable, quoted_executable, sizeof(quoted_executable));
    shell_quote(app->board_text, board, sizeof(board));
    shell_quote(app->tree_path, tree, sizeof(tree));
    shell_quote(app->mkr_path, mkr, sizeof(mkr));
    snprintf(command, sizeof(command), "%s --game %s --board %s --players %d",
             quoted_executable, game_name_for(app->game_index), board,
             app->player_count > 1 ? app->player_count : 2);
    /* A Monker tree carries its own correlated ranges. Sending the GUI's
     * starter ranges alongside it would silently override those real inputs. */
    if (!app->tree_path[0])
        for (int player = 0; player < app->player_count && player < 8; ++player)
        {
            const char *player_range = starter_range_for_player(app, player);
            char quoted_range[4200];
            shell_quote(player_range, quoted_range, sizeof(quoted_range));
            snprintf(command + gui_bounded_length(command, sizeof(command)),
                     sizeof(command) - gui_bounded_length(command, sizeof(command)),
                     " --range%d %s", player, quoted_range);
        }
    if (app->tree_path[0])
        snprintf(command + gui_bounded_length(command, sizeof(command)),
                 sizeof(command) - gui_bounded_length(command, sizeof(command)),
                 " --tree %s", tree);
    if (app->mkr_path[0])
        snprintf(command + gui_bounded_length(command, sizeof(command)),
                 sizeof(command) - gui_bounded_length(command, sizeof(command)),
                 " --mkr %s", mkr);
    return launch_solver(app, command, "Solve complete", "Solver failed");
}

static int is_preflop_request(const char *board)
{
    size_t length;
    if (!board || !*board)
        return 1;
    length = gui_bounded_length(board, 160u);
    if (length == 7u) {
        int equal = 1;
        for (size_t i = 0u; i < length; ++i)
            if (tolower((unsigned char)board[i]) !=
                tolower((unsigned char)"preflop"[i])) equal = 0;
        if (equal) return 1;
    }
    if (length == 4u) {
        int equal = 1;
        for (size_t i = 0u; i < length; ++i)
            if (tolower((unsigned char)board[i]) !=
                tolower((unsigned char)"none"[i])) equal = 0;
        if (equal) return 1;
    }
    return 0;
}

static int run_preflop_solver(app_t *app)
{
    char executable[1200], quoted_executable[1400];
    char ranges[PE_PREFLOP_GUI_MAX_PLAYERS][4200];
    char command[32768];
    int iterations;
    if (!app)
        return -1;
    if (app->tree_path[0] || app->mkr_path[0]) {
        copy_field(app->solver_status, sizeof(app->solver_status),
                   "Loaded .tree/.mkr belongs to a tree spot; enter its board and choose a tree engine");
        return -1;
    }
    if (app->player_count < 2 || app->player_count > PE_PREFLOP_GUI_MAX_PLAYERS) {
        copy_field(app->solver_status, sizeof(app->solver_status),
                   "Lane B preflop supports 2 to 6 players");
        return -1;
    }
    if (app->engine_index != 0) {
        copy_field(app->solver_status, sizeof(app->solver_status),
                   "Preflop Lane B is currently wired to Vector CPU");
        return -1;
    }
    if (app->executable_dir[0])
        snprintf(executable, sizeof(executable), "%s%cpe-preflop-solve%s",
                 app->executable_dir, PE_GUI_PATH_SEPARATOR,
                 PE_GUI_EXE_SUFFIX);
    else
        copy_field(executable, sizeof(executable), "pe-preflop-solve");
    shell_quote(executable, quoted_executable, sizeof(quoted_executable));
    iterations = app->iterations > 0 ? app->iterations : 10000;
    snprintf(command, sizeof(command),
             "%s --game %s --players %d --iterations %d "
             "--samples 128 --raise 2.5 --br-samples 128 --postflop",
             quoted_executable, game_name_for(app->game_index),
             app->player_count, iterations);
    for (int player = 0; player < app->player_count; ++player) {
        shell_quote(starter_range_for_player(app, player), ranges[player],
                    sizeof(ranges[player]));
        snprintf(command + gui_bounded_length(command, sizeof(command)),
                 sizeof(command) - gui_bounded_length(command, sizeof(command)),
                 " --range%d %s", player, ranges[player]);
    }
    return launch_solver(app, command, "Preflop Lane B complete",
                         "Preflop Lane B failed");
}

static int run_legacy_cfr(app_t *app, const char *backend_name, int lane_b)
{
    char executable[1200], quoted_executable[1400];
    char board[160], tree[1200], mkr[1200];
    char ranges[8][4200];
    char command[32768];
    const char *street = "river";
    char street_name[16];
    if (!app) return -1;
    if (!app->tree_path[0]) {
        copy_field(app->solver_status, sizeof(app->solver_status),
                   "Legacy CFR requires a .tree file");
        return -1;
    }
    if (app->game_index > 3) {
        copy_field(app->solver_status, sizeof(app->solver_status),
                   "Legacy CFR variant is not configured");
        return -1;
    }
    if (app->tree_header_valid)
    {
        if (app->tree_header.street < 0 || app->tree_header.street > 3)
        {
            copy_field(app->solver_status, sizeof(app->solver_status),
                       "Tree rejected: unsupported starting street");
            return -1;
        }
        copy_field(street_name, sizeof(street_name),
                   tree_street_label(app->tree_header.street));
        for (char *cursor = street_name; *cursor; ++cursor)
            *cursor = (char)tolower((unsigned char)*cursor);
        street = street_name;
        if (card_token_count(app->board_text) !=
            tree_board_count(app->tree_header.street))
        {
            copy_field(app->solver_status, sizeof(app->solver_status),
                       "Board card count does not match the loaded tree street");
            return -1;
        }
    }
    if (app->executable_dir[0])
        snprintf(executable, sizeof(executable), "%s%c%s%s",
                 app->executable_dir, PE_GUI_PATH_SEPARATOR,
                 "mpf_run_with_metrics", PE_GUI_EXE_SUFFIX);
    else copy_field(executable, sizeof(executable), "mpf_run_with_metrics");
    shell_quote(executable, quoted_executable, sizeof(quoted_executable));
    shell_quote(app->tree_path, tree, sizeof(tree));
    shell_quote(app->board_text, board, sizeof(board));
    snprintf(command, sizeof(command),
             "%s --tree %s --rules %s --street %s --board %s --players %d --iterations %d",
             quoted_executable, tree, game_name_for(app->game_index), street,
             board, app->player_count,
             app->iterations > 0 ? app->iterations : 1000);
    if (backend_name)
        snprintf(command + gui_bounded_length(command, sizeof(command)),
                 sizeof(command) - gui_bounded_length(command, sizeof(command)),
                 " --backend %s", backend_name);
    if (lane_b)
        snprintf(command + gui_bounded_length(command, sizeof(command)),
                 sizeof(command) - gui_bounded_length(command, sizeof(command)),
                 " --lane-b --sample-batch %d",
                 app->sample_batch_size > 0 ? app->sample_batch_size : 128);
    for (int player = 0; player < app->player_count && player < 8; ++player) {
        shell_quote(starter_range_for_player(app, player), ranges[player], sizeof(ranges[player]));
        snprintf(command + gui_bounded_length(command, sizeof(command)),
                 sizeof(command) - gui_bounded_length(command, sizeof(command)),
                 " --range%d %s", player, ranges[player]);
    }
    if (app->mkr_path[0]) {
        shell_quote(app->mkr_path, mkr, sizeof(mkr));
        snprintf(command + gui_bounded_length(command, sizeof(command)),
                 sizeof(command) - gui_bounded_length(command, sizeof(command)),
                 " --mkr %s", mkr);
    }
    return launch_solver(app, command,
                         lane_b ? "AUTO V3 solve complete" :
                         backend_name ? "GPU CFR complete" : "Legacy CFR complete",
                         lane_b ? "AUTO V3 solve failed" :
                         backend_name ? "GPU CFR unavailable or failed" :
                                        "Legacy CFR failed");
}

static int run_selected_solver(app_t *app)
{
    if (!app) return -1;
    if (app->tree_path[0])
    {
        if (!app->tree_header_valid)
        {
            copy_field(app->solver_status, sizeof(app->solver_status),
                       "Loaded tree has no valid context; reload the .tree file");
            return -1;
        }
        if (app->engine_index == 0)
            return run_vector_sim(app);
        if (app->engine_index == 3)
            return run_legacy_cfr(app, "auto", 1);
        return run_legacy_cfr(app, app->engine_index == 2 ? "opencl" : NULL,
                              0);
    }
    if (app->mkr_path[0])
    {
        copy_field(app->solver_status, sizeof(app->solver_status),
                   ".mkr is a strategy archive; load the matching .tree before solving");
        return -1;
    }
    if (is_preflop_request(app->board_text))
        return run_preflop_solver(app);
    if (app->engine_index == 0) return run_vector_sim(app);
    if (app->engine_index == 1) return run_legacy_cfr(app, NULL, 0);
    if (app->engine_index == 2) return run_legacy_cfr(app, "opencl", 0);
    copy_field(app->solver_status, sizeof(app->solver_status),
               "AUTO V3 requires a compatible .tree file");
    return -1;
}

static int has_suffix(const char *path, const char *suffix)
{
    size_t path_length = gui_bounded_length(path, 4096u);
    size_t suffix_length = gui_bounded_length(suffix, 256u);
    if (suffix_length > path_length)
        return 0;
    path += path_length - suffix_length;
    for (size_t i = 0u; i < suffix_length; ++i)
        if (tolower((unsigned char)path[i]) != tolower((unsigned char)suffix[i]))
            return 0;
    return 1;
}

static void set_dropped_file(app_t *app, const char *path)
{
    if (!app || !path)
        return;
    if (has_suffix(path, ".pe_sol"))
    {
        if (load_solution(app, path) == 0)
            copy_field(app->solver_status, sizeof(app->solver_status), "Strategy snapshot loaded");
        else
            copy_field(app->solver_status, sizeof(app->solver_status), "Could not read .pe_sol");
    }
    else if (has_suffix(path, ".tree"))
    {
        copy_field(app->tree_path, sizeof(app->tree_path), path);
        if (load_tree_context(app, path) == 0)
            start_tree_conversion(app);
    }
    else if (has_suffix(path, ".json"))
    {
        load_tree_json_file(app, path);
    }
    else if (has_suffix(path, ".mkr"))
    {
        copy_field(app->mkr_path, sizeof(app->mkr_path), path);
        copy_field(app->solver_status, sizeof(app->solver_status), "Strategy archive loaded");
    }
    else if (has_suffix(path, ".csv"))
    {
        if (load_labels(app, path) == 0)
            copy_field(app->solver_status, sizeof(app->solver_status), "Labels loaded");
        else
            copy_field(app->solver_status, sizeof(app->solver_status), "Could not read labels CSV");
    }
    else
        copy_field(app->solver_status, sizeof(app->solver_status), "Drop .tree, .mkr, .pe_sol or .csv");
}

static char *focused_text(app_t *app, size_t *capacity)
{
    if (!app || !capacity)
        return NULL;
    if (app->focus_field == 1) { *capacity = sizeof(app->board_text); return app->board_text; }
    if (app->focus_field == 2) { *capacity = sizeof(app->range_oop); return app->range_oop; }
    if (app->focus_field == 3) { *capacity = sizeof(app->range_ip); return app->range_ip; }
    if (app->focus_field == 4) { *capacity = sizeof(app->tree_path); return app->tree_path; }
    if (app->focus_field == 5) { *capacity = sizeof(app->mkr_path); return app->mkr_path; }
    return NULL;
}

static void append_text_input(app_t *app, const char *input)
{
    size_t capacity;
    char *destination = focused_text(app, &capacity);
    if (!destination || !input)
        return;
    size_t length = gui_bounded_length(destination, capacity);
    size_t available = capacity > length + 1u ? capacity - length - 1u : 0u;
    if (available > 0u)
    {
        size_t input_length = gui_bounded_length(input, available);
        for (size_t i = 0u; i < input_length; ++i)
            destination[length + i] = input[i];
        destination[length + input_length] = '\0';
    }
}

static void remove_text_input(app_t *app)
{
    size_t capacity;
    char *destination = focused_text(app, &capacity);
    (void)capacity;
    if (destination && *destination)
        destination[gui_bounded_length(destination, capacity) - 1u] = '\0';
}

typedef struct {
    int width;
    int height;
    rect_t sidebar;
    rect_t setup_tab;
    rect_t solve_tab;
    rect_t explore_tab;
    rect_t range_tab;
    rect_t next_button;
    rect_t action_buttons[8];
} gui_layout_t;

static gui_layout_t layout_for(int width, int height)
{
    gui_layout_t layout;
    int content_x = 264;
    int content_width = width - content_x - 28;
    int action_width = (content_width - 48) / 4;
    memset(&layout, 0, sizeof(layout));
    layout.width = width;
    layout.height = height;
    layout.sidebar = (rect_t){0, 0, 232, height};
    layout.setup_tab = (rect_t){24, 150, 184, 48};
    layout.solve_tab = (rect_t){24, 210, 184, 48};
    layout.explore_tab = (rect_t){24, 270, 184, 48};
    layout.range_tab = (rect_t){24, 330, 184, 48};
    layout.next_button = (rect_t){content_x, height - 78, 200, 48};
    for (int action = 0; action < 8; ++action)
        layout.action_buttons[action] = (rect_t){content_x + (action % 4) * (action_width + 12),
                                                 height - 226 + (action / 4) * 54,
                                                 action_width, 44};
    return layout;
}

static void panel(SDL_Renderer *renderer, rect_t rect, SDL_Color surface,
                  SDL_Color outline)
{
    fill(renderer, rect, surface);
    border(renderer, rect, outline);
}

static void line(SDL_Renderer *renderer, int x1, int y1, int x2, int y2,
                 SDL_Color color)
{
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
}

static void card(SDL_Renderer *renderer, int x, int y, const char *value,
                 int accent)
{
    SDL_Color surface = {31, 43, 57, 255};
    SDL_Color outline = accent ? (SDL_Color){82, 151, 255, 255}
                               : (SDL_Color){71, 88, 108, 255};
    panel(renderer, (rect_t){x, y, 76, 96}, surface, outline);
    text(renderer, x + 12, y + 18, value && *value ? value : "--", 3,
         accent ? (SDL_Color){241, 246, 252, 255}
                : (SDL_Color){155, 169, 186, 255});
}

static void stat_value(SDL_Renderer *renderer, int x, int y, const char *name,
                       const char *value, SDL_Color value_color)
{
    text(renderer, x, y, name, 2, (SDL_Color){143, 159, 177, 255});
    text(renderer, x, y + 25, value, 3, value_color);
}

static const pe_tree_json_node_t *find_tree_node(const pe_tree_json_t *tree,
                                                 const char *id)
{
    return pe_tree_json_find_node(tree, id);
}

/* Render the loaded tree topology: BFS layout bounded to the first rows and
 * depths that fit the panel. When no tree is loaded, say so instead of
 * drawing placeholder nodes. */
static void draw_tree(const app_t *app, SDL_Renderer *renderer, int x, int y,
                      int width, int height, SDL_Color white, SDL_Color muted,
                      SDL_Color blue, SDL_Color green)
{
    enum { TREE_MAX_DEPTH = 3, TREE_MAX_PER_ROW = 6 };
    static pe_tree_layout_entry_t entries[(TREE_MAX_DEPTH + 1) * TREE_MAX_PER_ROW];
    pe_tree_layout_t layout;
    char caption[224];
    const int box_w = 104;
    const int box_h = 34;
    int row_step;
    size_t i;

    if (app->tree_view.count == 0u)
    {
        panel(renderer, (rect_t){x + width / 2 - 160, y + 20, 320, 42},
              (SDL_Color){39, 65, 91, 255}, blue);
        text(renderer, x + width / 2 - 100, y + 34, "NO TREE LOADED", 2, white);
        text(renderer, x + 18, y + height - 30,
             "DROP A .tree OR TREE .json TO SHOW ITS TOPOLOGY", 2, muted);
        return;
    }
    if (pe_tree_layout_bfs(&app->tree_view, TREE_MAX_DEPTH, TREE_MAX_PER_ROW,
                           entries, (TREE_MAX_DEPTH + 1) * TREE_MAX_PER_ROW,
                           &layout) != 0)
    {
        text(renderer, x + 18, y + 20, "TREE TOO LARGE TO PREVIEW", 2, muted);
        return;
    }
    row_step = layout.depth_count > 1
                   ? (height - 60 - box_h) / (layout.depth_count - 1)
                   : 0;
    if (row_step > box_h + 28)
        row_step = box_h + 28;

    /* Edges first so the boxes draw over them. */
    for (i = 0u; i < layout.count; ++i)
    {
        const pe_tree_layout_entry_t *entry = &layout.entries[i];
        const pe_tree_layout_entry_t *parent;
        int px, py, cx, cy;
        if (entry->parent_entry < 0)
            continue;
        parent = &layout.entries[entry->parent_entry];
        px = x + (width * (parent->slot + 1)) / (parent->row_width + 1);
        py = y + 8 + parent->depth * row_step + box_h;
        cx = x + (width * (entry->slot + 1)) / (entry->row_width + 1);
        cy = y + 8 + entry->depth * row_step;
        line(renderer, px, py, cx, cy, muted);
    }
    for (i = 0u; i < layout.count; ++i)
    {
        const pe_tree_layout_entry_t *entry = &layout.entries[i];
        const pe_tree_json_node_t *node =
            &app->tree_view.nodes[entry->node_index];
        const pe_tree_json_node_t *parent_node = NULL;
        int cx = x + (width * (entry->slot + 1)) / (entry->row_width + 1);
        int cy = y + 8 + entry->depth * row_step;
        char label[32];
        if (entry->parent_entry >= 0)
            parent_node =
                &app->tree_view.nodes[layout.entries[entry->parent_entry].node_index];
        panel(renderer, (rect_t){cx - box_w / 2, cy, box_w, box_h},
              entry->depth == 0 ? (SDL_Color){39, 65, 91, 255}
                                : (SDL_Color){34, 57, 55, 255},
              entry->depth == 0 ? blue : green);
        if (parent_node && entry->action_from_parent >= 0 &&
            parent_node->action_type[entry->action_from_parent][0])
            snprintf(label, sizeof(label), "%s",
                     parent_node->action_type[entry->action_from_parent]);
        else
            snprintf(label, sizeof(label), "%s",
                     node->type[0] ? node->type : "node");
        text(renderer, cx - box_w / 2 + 6, cy + 4, label, 1, muted);
        snprintf(label, sizeof(label), "%.8s", node->id);
        text(renderer, cx - box_w / 2 + 6, cy + 16, label, 2, white);
    }
    snprintf(caption, sizeof(caption), "TREE  /  ROOT %.20s  /  %zu NODES%s",
             app->tree_view.root_id[0] ? app->tree_view.root_id
                                       : app->tree_view.nodes[0].id,
             app->tree_view.count, layout.truncated ? "  (VIEW BOUNDED)" : "");
    text(renderer, x + 18, y + height - 30, caption, 2, muted);
}

static int rank_index(char rank)
{
    const char *ranks = "AKQJT98765432";
    const char *found = strchr(ranks, (int)toupper((unsigned char)rank));
    return found ? (int)(found - ranks) : -1;
}

static int range_contains_combo(const char *range, int row, int column,
                                int *suited, int *pair)
{
    char copy[4096];
    char *token;
    if (suited) *suited = 0;
    if (pair) *pair = 0;
    if (!range || !*range || row < 0 || column < 0)
        return 0;
    snprintf(copy, sizeof(copy), "%s", range);
    token = strtok(copy, ",; 	\r\n");
    while (token) {
        size_t length = gui_bounded_length(token, sizeof(copy));
        int first = rank_index(token[0]);
        int second = length >= 2u ? rank_index(token[1]) : -1;
        int is_suited = 0;
        int want_row, want_column;
        while (length > 0u && (token[length - 1u] == '+' || token[length - 1u] == 'x'))
            token[--length] = '\0';
        if (length >= 4u) {
            first = rank_index(token[0]);
            second = rank_index(token[2]);
            is_suited = token[1] == token[3];
        } else if (length >= 3u) {
            is_suited = token[2] == 's';
        }
        if (first >= 0 && second >= 0) {
            if (first == second) {
                want_row = want_column = first;
                if (pair) *pair = 1;
            } else if (is_suited) {
                want_row = first < second ? first : second;
                want_column = first < second ? second : first;
            } else {
                want_row = first < second ? second : first;
                want_column = first < second ? first : second;
            }
            if (want_row == row && want_column == column) {
                if (suited) *suited = is_suited;
                return 1;
            }
        }
        token = strtok(NULL, ",; 	\r\n");
    }
    return 0;
}

static void range_matrix_clear(app_t *app)
{
    if (!app) return;
    memset(app->range_matrix, 0, sizeof(app->range_matrix));
}

static void range_matrix_from_text(app_t *app, int player, const char *range)
{
    char copy[4096];
    char *token;
    if (!app || player < 0 || player > 1) return;
    memset(app->range_matrix[player], 0, sizeof(app->range_matrix[player]));
    if (!range) return;
    snprintf(copy, sizeof(copy), "%s", range);
    token = strtok(copy, ",; 	\r\n");
    while (token) {
        int first = rank_index(token[0]);
        int second = rank_index(token[1]);
        if (first >= 0 && second >= 0) {
            int row;
            int column;
            size_t token_length = gui_bounded_length(token, sizeof(copy));
            int suited = token_length >= 3u && token[2] == 's';
            if (token_length >= 4u) {
                first = rank_index(token[0]);
                second = rank_index(token[2]);
                suited = token[1] == token[3];
            }
            if (first == second) {
                row = column = first;
            } else if (suited) {
                row = first < second ? first : second;
                column = first < second ? second : first;
            } else {
                row = first < second ? second : first;
                column = first < second ? first : second;
            }
            if (row >= 0 && column >= 0 && row < 13 && column < 13)
                app->range_matrix[player][row][column] = 1;
        }
        token = strtok(NULL, ",; 	\r\n");
    }
}

static void range_matrix_to_text(app_t *app, int player)
{
    const char *ranks = "AKQJT98765432";
    char *out;
    size_t capacity;
    size_t used = 0u;
    if (!app || player < 0 || player > 1) return;
    out = player == 0 ? app->range_oop : app->range_ip;
    capacity = player == 0 ? sizeof(app->range_oop) : sizeof(app->range_ip);
    out[0] = '\0';
    for (int row = 0; row < 13; ++row) {
        for (int column = 0; column < 13; ++column) {
            if (!app->range_matrix[player][row][column]) continue;
            char token[8];
            int length = row == column
                ? snprintf(token, sizeof(token), "%c%c", ranks[row], ranks[column])
                : snprintf(token, sizeof(token), "%c%c%c", ranks[row], ranks[column],
                           row < column ? 's' : 'o');
            if (length <= 0 || used + (size_t)length + 2u >= capacity) continue;
            if (used) out[used++] = ',';
            for (size_t offset = 0u; offset < (size_t)length; ++offset)
                out[used + offset] = token[offset];
            used += (size_t)length;
            out[used] = '\0';
        }
    }
}

static void range_matrix_sync_from_fields(app_t *app)
{
    if (!app) return;
    range_matrix_from_text(app, 0, app->range_oop);
    range_matrix_from_text(app, 1, app->range_ip);
}

static void draw_hand_matrix(SDL_Renderer *renderer, int x, int y, int width,
                             int height, const char *oop, const char *ip,
                             SDL_Color white, SDL_Color muted,
                             SDL_Color blue, SDL_Color orange,
                             SDL_Color outline)
{
    const char *ranks = "AKQJT98765432";
    int label = 20;
    int cell = (width - label - 8) / 13;
    if (cell < 24) cell = 24;
    if (cell * 13 + label + 8 > width) cell = (width - label - 8) / 13;
    text(renderer, x, y - 25, "RANGE MATRIX", 2, blue);
    text(renderer, x + label + 4, y - 4, ranks, 1, muted);
    for (int row = 0; row < 13; ++row) {
        text(renderer, x, y + row * cell + 8, (char[]){ranks[row], '\0'}, 1, muted);
        for (int column = 0; column < 13; ++column) {
            int suited = 0, pair = 0;
            int has_oop = range_contains_combo(oop, row, column, &suited, &pair);
            int has_ip = range_contains_combo(ip, row, column, &suited, &pair);
            SDL_Color fill_color = {25, 34, 46, 255};
            if (has_oop && has_ip) fill_color = (SDL_Color){115, 87, 139, 255};
            else if (has_oop) fill_color = (SDL_Color){41, 103, 166, 255};
            else if (has_ip) fill_color = (SDL_Color){169, 101, 57, 255};
            fill(renderer, (rect_t){x + label + 4 + column * cell,
                                    y + row * cell, cell - 2, cell - 2}, fill_color);
            border(renderer, (rect_t){x + label + 4 + column * cell,
                                      y + row * cell, cell - 2, cell - 2}, outline);
            {
                char combo[8];
                combo[0] = ranks[row]; combo[1] = ranks[column];
                combo[2] = row == column ? 'p' : row < column ? 's' : 'o';
                combo[3] = '\0';
                text(renderer, x + label + 9 + column * cell,
                     y + row * cell + (cell > 30 ? 9 : 6), combo, cell > 34 ? 1 : 1,
                     has_oop || has_ip ? white : muted);
            }
        }
    }
    text(renderer, x + label + 4, y + 13 * cell + 8, "BLUE OOP", 1, blue);
    text(renderer, x + label + 94, y + 13 * cell + 8, "ORANGE IP", 1, orange);
    text(renderer, x + label + 220, y + 13 * cell + 8, "PURPLE BOTH", 1, white);
}

static void draw_table_view(SDL_Renderer *renderer, int x, int y, int width,
                            int height, const char *board, SDL_Color white,
                            SDL_Color muted, SDL_Color green, SDL_Color outline)
{
    int center_x = x + width / 2;
    int center_y = y + height / 2 + 8;
    int radius_x = width / 2 - 18;
    int radius_y = height / 2 - 24;
    SDL_Color felt = {24, 87, 73, 255};
    SDL_Color rail = {43, 32, 39, 255};
    fill(renderer, (rect_t){x + 18, center_y - radius_y, width - 36, radius_y * 2}, rail);
    for (int row = -radius_y + 12; row <= radius_y - 12; ++row) {
        double ratio = (double)row / (double)(radius_y - 12);
        int half = (int)((double)(radius_x - 20) * sqrt(1.0 - ratio * ratio));
        if (half > 0) fill(renderer, (rect_t){center_x - half, center_y + row, half * 2, 1}, felt);
    }
    border(renderer, (rect_t){x + 18, center_y - radius_y, width - 36, radius_y * 2}, outline);
    text(renderer, center_x - 38, center_y - 18, "POT  0.00", 2, white);
    text(renderer, center_x - 42, center_y + 13, "SPR  --", 1, muted);
    for (int seat = 0; seat < 2; ++seat) {
        int sx = seat ? x + width - 148 : x + 42;
        int sy = seat ? center_y + radius_y - 18 : center_y - radius_y - 22;
        panel(renderer, (rect_t){sx, sy, 106, 34},
              seat ? (SDL_Color){37, 111, 170, 255} : (SDL_Color){154, 67, 81, 255},
              outline);
        text(renderer, sx + 12, sy + 11, seat ? "IP  100" : "OOP 100", 1, white);
    }
    {
        char copy[96];
        snprintf(copy, sizeof(copy), "BOARD  %s", board && *board ? board : "-- -- -- -- --");
        text(renderer, x + 24, y + height - 24, copy, 1, muted);
    }
    (void)green;
}

static void draw_action_summary(SDL_Renderer *renderer, int x, int y, int width,
                                int height, const app_t *app, SDL_Color white,
                                SDL_Color muted, SDL_Color blue, SDL_Color green,
                                SDL_Color orange, SDL_Color outline)
{
    const spot_t *spot = NULL;
    int actions = 4;
    if (app->spot_count) {
        size_t index = app->selected_spot < app->spot_count ? app->selected_spot : 0;
        spot = &app->spots[index];
        actions = spot->actions < 6 ? spot->actions : 6;
    }
    text(renderer, x, y, "ACTIONS / FREQUENCY", 2, blue);
    for (int action = 0; action < actions; ++action) {
        int by = y + 30 + action * ((height - 40) / actions);
        double probability = spot ? spot->probability[action] : 0.0;
        const char *name = app->action_names[action][0] ? app->action_names[action] :
                           (action == 0 ? "FOLD" : action == 1 ? "CHECK / CALL" :
                            action == 2 ? "BET 33%" : action == 3 ? "BET 67%" : "RAISE");
        char value[24];
        snprintf(value, sizeof(value), "%.1f%%", probability * 100.0);
        text(renderer, x, by + 4, name, 1, white);
        fill(renderer, (rect_t){x + 122, by + 3, width - 176, 18}, (SDL_Color){28, 41, 55, 255});
        fill(renderer, (rect_t){x + 122, by + 3, (int)((width - 176) * probability), 18},
             action == app->selected_action ? green : action == 0 ? orange : blue);
        text(renderer, x + width - 45, by + 4, value, 1, white);
        line(renderer, x, by + 28, x + width, by + 28, outline);
    }
    if (!spot) text(renderer, x, y + height - 8, "LOAD .PE_SOL OR RUN A SPOT TO SEE FREQUENCIES", 1, muted);
}

static void draw_strategy_table(SDL_Renderer *renderer, int x, int y, int width,
                                int height, const app_t *app, SDL_Color white,
                                SDL_Color muted, SDL_Color blue, SDL_Color green,
                                SDL_Color outline)
{
    text(renderer, x, y, "COMBO / STRATEGY / REACH / EQUITY / EV", 2, blue);
    fill(renderer, (rect_t){x, y + 28, width, 30}, (SDL_Color){31, 46, 61, 255});
    text(renderer, x + 12, y + 39, "HAND", 1, muted);
    text(renderer, x + 110, y + 39, "STRATEGY", 1, muted);
    text(renderer, x + 270, y + 39, "REACH", 1, muted);
    text(renderer, x + 372, y + 39, "EQUITY", 1, muted);
    text(renderer, x + 480, y + 39, "EV", 1, muted);
    for (int row = 0; row < 4; ++row) {
        int ry = y + 65 + row * 32;
        const char *hands[] = {"AA", "AKs", "AQs", "JTs"};
        fill(renderer, (rect_t){x, ry, width, 30}, row % 2 ? (SDL_Color){23, 34, 47, 255} : (SDL_Color){27, 40, 54, 255});
        text(renderer, x + 12, ry + 10, hands[row], 1, white);
        fill(renderer, (rect_t){x + 110, ry + 9, 106, 12}, (SDL_Color){36, 53, 70, 255});
        fill(renderer, (rect_t){x + 110, ry + 9, row == 0 ? 96 : 42 + row * 12, 12}, row == 0 ? green : blue);
        text(renderer, x + 270, ry + 10, "100.0%", 1, white);
        text(renderer, x + 372, ry + 10, row < 2 ? "82.4%" : "51.0%", 1, green);
        text(renderer, x + 480, ry + 10, row < 2 ? "+1.24" : "+0.18", 1, green);
    }
    text(renderer, x, y + height - 12, "TABLE DISPLAY / COMBO DETAILS APPEAR WHEN A SOLUTION SNAPSHOT IS LOADED", 1, muted);
    (void)outline;
}

static void render_solver_legacy(SDL_Renderer *renderer, const app_t *app)
{
    int width, height;
    gui_layout_t layout;
    SDL_Color bg = {12, 18, 25, 255};
    SDL_Color sidebar = {18, 27, 38, 255};
    SDL_Color surface = {23, 34, 47, 255};
    SDL_Color surface2 = {28, 41, 56, 255};
    SDL_Color outline = {49, 69, 91, 255};
    SDL_Color white = {238, 243, 249, 255};
    SDL_Color muted = {143, 159, 177, 255};
    SDL_Color blue = {82, 151, 255, 255};
    SDL_Color green = {79, 201, 146, 255};
    SDL_Color orange = {245, 174, 82, 255};
    char buffer[256];

    SDL_GetRendererOutputSize(renderer, &width, &height);
    if (width < 960) width = 960;
    if (height < 680) height = 680;
    layout = layout_for(width, height);
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderClear(renderer);

    fill(renderer, layout.sidebar, sidebar);
    line(renderer, 232, 0, 232, height, outline);
    text(renderer, 28, 34, "POKER", 4, white);
    text(renderer, 28, 68, "EVAL", 4, blue);
    text(renderer, 28, 108, "SOLVER DESKTOP", 2, muted);
    panel(renderer, (rect_t){24, 584, 184, 68}, (SDL_Color){27, 49, 70, 255}, outline);
    text(renderer, 40, 598, "ENGINE", 2, muted);
    text(renderer, 40, 623, app->solution_path[0] ? "READY" : "NO SOLUTION", 2,
         app->solution_path[0] ? green : orange);
    text(renderer, 28, height - 42, "SDL2  /  NATIVE C", 2, muted);

    panel(renderer, layout.setup_tab, app->page == 0 ? (SDL_Color){39, 65, 91, 255} : sidebar,
          app->page == 0 ? blue : sidebar);
    panel(renderer, layout.solve_tab, app->page == 1 ? (SDL_Color){39, 65, 91, 255} : sidebar,
          app->page == 1 ? blue : sidebar);
    panel(renderer, layout.explore_tab, app->page == 2 ? (SDL_Color){39, 65, 91, 255} : sidebar,
          app->page == 2 ? blue : sidebar);
    text(renderer, 42, 166, "SETUP", 2, white);
    text(renderer, 42, 226, "SOLVE", 2, white);
    text(renderer, 42, 286, "EXPLORE", 2, white);

    text(renderer, 264, 34, app->page == 0 ? "NEW SOLVE" : app->page == 1 ? "SOLVE MONITOR" : "STRATEGY EXPLORER", 4, white);
    text(renderer, 264, 74, app->page == 0 ? "Define the spot before you spend compute." :
                              app->page == 1 ? "Track convergence and inspect the active tree." :
                                               "Navigate infosets without losing the poker context.", 2, muted);
    line(renderer, 264, 112, width - 28, 112, outline);

    if (app->page == 0)
    {
        int left = 264;
        int right = 786;
        panel(renderer, (rect_t){left, 140, 494, height - 244}, surface, outline);
        panel(renderer, (rect_t){right, 140, width - right - 28, height - 244}, surface, outline);
        text(renderer, left + 24, 166, "GAME CONFIGURATION", 2, blue);
        text(renderer, left + 24, 212, "VARIANT", 2, muted);
        panel(renderer, (rect_t){left + 24, 236, 210, 46}, surface2, outline);
        text(renderer, left + 40, 251, game_label_for(app->game_index), 2, white);
        text(renderer, left + 260, 212, "PLAYERS", 2, muted);
        panel(renderer, (rect_t){left + 260, 236, 184, 46}, surface2, outline);
        snprintf(buffer, sizeof(buffer), "%d  /  %s", app->player_count, app->player_count == 2 ? "HEADS-UP" : "MULTIWAY");
        text(renderer, left + 276, 251, buffer, 2, white);
        text(renderer, left + 24, 314, "BOARD", 2, muted);
        panel(renderer, (rect_t){left + 24, 338, 420, 46}, surface2,
              app->focus_field == 1 ? blue : outline);
        text(renderer, left + 40, 353, app->board_text[0] ? app->board_text : "--  --  --  --  --", 2, white);
        text(renderer, left + 24, 416, "RANGES", 2, muted);
        panel(renderer, (rect_t){left + 24, 440, 204, 46}, surface2,
              app->focus_field == 2 ? blue : outline);
        panel(renderer, (rect_t){left + 240, 440, 204, 46}, surface2,
              app->focus_field == 3 ? blue : outline);
        snprintf(buffer, sizeof(buffer), "OOP  %s", app->range_oop[0] ? app->range_oop : "100%");
        text(renderer, left + 40, 455, buffer, 2, white);
        snprintf(buffer, sizeof(buffer), "IP   %s", app->range_ip[0] ? app->range_ip : "100%");
        text(renderer, left + 256, 455, buffer, 2, white);
        text(renderer, left + 24, 518, "ITERATIONS", 2, muted);
        snprintf(buffer, sizeof(buffer), "%d", app->iterations > 0 ? app->iterations : 100000);
        panel(renderer, (rect_t){left + 24, 542, 204, 46}, surface2, outline);
        text(renderer, left + 40, 557, buffer, 2, white);
        text(renderer, left + 24, 518, "ENGINE", 2, muted);
        panel(renderer, (rect_t){left + 260, 542, 184, 46}, surface2, outline);
        text(renderer, left + 276, 557, engine_label_for(app->engine_index), 2, white);
        snprintf(buffer, sizeof(buffer), "ITERATIONS  %d", app->iterations);
        text(renderer, left + 24, 604, buffer, 2, muted);
        if (app->tree_path[0])
            snprintf(buffer, sizeof(buffer), "TREE  %s", basename_of(app->tree_path));
        else
            snprintf(buffer, sizeof(buffer), "TREE  DROP .TREE / .MKR / .PE_SOL / CSV");
        text(renderer, left + 24, 632, buffer, 2, orange);
        if (app->mkr_path[0]) {
            snprintf(buffer, sizeof(buffer), "MKR   %s", basename_of(app->mkr_path));
            text(renderer, left + 24, 664, buffer, 2, orange);
        }
        text(renderer, left + 24, app->mkr_path[0] ? 696 : 664, app->solver_status, 2, app->solver_status[0] ? green : muted);
        text(renderer, left + 24, 730, "CLICK A FIELD TO TYPE  /  DROP FILES  /  SOLVE THIS SPOT", 2, muted);

        text(renderer, right + 24, 166, "SPOT PREVIEW", 2, blue);
        text(renderer, right + 24, 208, "BOARD", 2, muted);
        for (int c = 0; c < 5; ++c)
            card(renderer, right + 24 + c * 86, 238, c < 3 ? "--" : "", c < 3);
        text(renderer, right + 24, 366, "ACTION TREE", 2, muted);
        draw_tree(app, renderer, right + 24, 398, width - right - 76, 180, white, muted, blue, green);
        panel(renderer, (rect_t){right + 24, height - 154, width - right - 76, 54}, (SDL_Color){35, 77, 73, 255}, green);
        text(renderer, right + 42, height - 137, "SOLVE THIS SPOT  >", 2, white);
    }
    else if (app->page == 1)
    {
        int left = 264;
        int right = 1050;
        panel(renderer, (rect_t){left, 140, 760, height - 244}, surface, outline);
        panel(renderer, (rect_t){right, 140, width - right - 28, height - 244}, surface, outline);
        text(renderer, left + 24, 166, "TREE MONITOR", 2, blue);
        snprintf(buffer, sizeof(buffer), "%s  /  %d PLAYERS  /  %s", game_label_for(app->game_index), app->player_count, engine_label_for(app->engine_index));
        text(renderer, left + 24, 208, buffer, 2, muted);
        draw_tree(app, renderer, left + 28, 260, 680, 220, white, muted, blue, green);
        text(renderer, left + 24, 536, "ITERATION", 2, muted);
        text(renderer, left + 24, 562, app->solution_path[0] ? "SNAPSHOT LOADED" : "WAITING FOR INPUT", 3, app->solution_path[0] ? green : orange);
        line(renderer, left + 24, 618, left + 704, 618, outline);
        stat_value(renderer, left + 24, 646, "INFOSETS", app->spot_count ? "LOADED" : "--", white);
        stat_value(renderer, left + 178, 646, "EXPLOITABILITY", app->spot_count ? "READY" : "--", green);
        stat_value(renderer, left + 400, 646, "STATUS", "CPU / EXACT", blue);
        text(renderer, right + 24, 166, "RUN SUMMARY", 2, blue);
        text(renderer, right + 24, 194, app->solver_status, 2, app->solver_status[0] ? green : muted);
        stat_value(renderer, right + 24, 216, "SOLUTION", app->solution_path[0] ? "OPEN" : "NONE", app->solution_path[0] ? green : orange);
        snprintf(buffer, sizeof(buffer), "%zu", app->spot_count);
        stat_value(renderer, right + 24, 302, "INFOSets", buffer, white);
        snprintf(buffer, sizeof(buffer), "%d", app->iterations > 0 ? app->iterations : 100000);
        stat_value(renderer, right + 24, 388, "BUDGET", buffer, white);
        panel(renderer, (rect_t){right + 24, 500, width - right - 76, 120}, (SDL_Color){27, 49, 70, 255}, outline);
        text(renderer, right + 42, 518, "RESULT", 2, muted);
        if (app->solver_result[0]) {
            snprintf(buffer, sizeof(buffer), "%.170s", app->solver_result);
            text(renderer, right + 42, 548, buffer, 2, white);
        } else {
            text(renderer, right + 42, 548, "No run yet", 2, white);
        }
        panel(renderer, (rect_t){right + 24, 586, width - right - 76, 64}, (SDL_Color){35, 77, 73, 255}, green);
        text(renderer, right + 42, 609, "INSPECT STRATEGY  >", 2, white);
    }
    else
    {
        int left = 264;
        int right = 1030;
        panel(renderer, (rect_t){left, 140, 744, height - 244}, surface, outline);
        panel(renderer, (rect_t){right, 140, width - right - 28, height - 244}, surface, outline);
        text(renderer, left + 24, 166, "INFOSSET INDEX", 2, blue);
        text(renderer, left + 24, 208, app->spot_count ? "SELECT A SPOT TO INSPECT ITS POLICY" : "LOAD A .PE_SOL TO BEGIN", 2, muted);
        if (app->spot_count)
        {
            size_t index = app->selected_spot < app->spot_count ? app->selected_spot : 0;
            const spot_t *spot = &app->spots[index];
            snprintf(buffer, sizeof(buffer), "KEY  0x%016llx", (unsigned long long)spot->key);
            text(renderer, left + 24, 258, buffer, 2, white);
            text(renderer, left + 24, 294, "ACTION FREQUENCIES", 2, muted);
            for (int action = 0; action < spot->actions && action < 8; ++action)
            {
                int y = 338 + action * 52;
                const char *name = app->action_names[action][0] ? app->action_names[action] : "OPTION";
                snprintf(buffer, sizeof(buffer), "%s", name);
                text(renderer, left + 24, y + 10, buffer, 2, white);
                fill(renderer, (rect_t){left + 180, y + 8, 390, 24}, (SDL_Color){34, 50, 66, 255});
                fill(renderer, (rect_t){left + 180, y + 8, (int)(390.0 * spot->probability[action]), 24}, action == app->selected_action ? green : blue);
                snprintf(buffer, sizeof(buffer), "%.1f%%", spot->probability[action] * 100.0);
                text(renderer, left + 590, y + 10, buffer, 2, white);
            }
        }
        text(renderer, right + 24, 166, "CONTEXT", 2, blue);
        const label_t *meta = app->spot_count ? metadata_for(app, app->spots[app->selected_spot < app->spot_count ? app->selected_spot : 0].key) : NULL;
        text(renderer, right + 24, 216, "STREET", 2, muted);
        text(renderer, right + 24, 242, meta && meta->street[0] ? meta->street : "UNKNOWN", 3, white);
        text(renderer, right + 24, 306, "BOARD", 2, muted);
        text(renderer, right + 24, 332, meta && meta->board[0] ? meta->board : "NOT PROVIDED", 2, white);
        text(renderer, right + 24, 390, "POSITION", 2, muted);
        text(renderer, right + 24, 416, meta && meta->position[0] ? meta->position : "UNKNOWN", 2, white);
        text(renderer, right + 24, 480, "TRAIN THIS SPOT", 2, muted);
        {
            const spot_t *spot = &app->spots[app->selected_spot < app->spot_count ? app->selected_spot : 0];
            int button_width = (width - right - 100) / 2;
            int visible_rows = (height - 530) / 48;
            int total_rows = (spot->actions + 1) / 2;
            int max_scroll;
            int scroll;
            if (visible_rows < 1) visible_rows = 1;
            max_scroll = total_rows > visible_rows ? total_rows - visible_rows : 0;
            scroll = app->training_action_scroll > max_scroll ? max_scroll : app->training_action_scroll;
            for (int row = 0; row < visible_rows; ++row)
            {
                int action_row = scroll + row;
                for (int column = 0; column < 2; ++column)
                {
                    int action = action_row * 2 + column;
                    int bx;
                    int by;
                    const char *name;
                    if (action >= spot->actions) continue;
                    bx = right + 24 + column * (button_width + 12);
                    by = 506 + row * 48;
                    name = app->action_names[action][0] ? app->action_names[action] : "OPTION";
                    panel(renderer, (rect_t){bx, by, button_width, 38},
                          action == app->selected_action ? (SDL_Color){35, 77, 73, 255} : surface2,
                          action == app->selected_action ? green : outline);
                    text(renderer, bx + 12, by + 12, name, 2, white);
                }
            }
            if (max_scroll > 0)
                text(renderer, right + 24, 506 + visible_rows * 48 - 10,
                     "SCROLL FOR MORE ACTIONS", 1, muted);
        }
    }
    if (app->page == 2 && app->spot_count)
    {
        fill(renderer, layout.next_button, (SDL_Color){39, 65, 91, 255});
        text(renderer, layout.next_button.x + 18, layout.next_button.y + 16, "NEXT SPOT", 2, white);
    }
    SDL_RenderPresent(renderer);
}

static void render_solver(SDL_Renderer *renderer, const app_t *app)
{
    int width, height;
    SDL_Color bg = {10, 16, 24, 255};
    SDL_Color sidebar = {16, 25, 36, 255};
    SDL_Color surface = {20, 31, 44, 255};
    SDL_Color surface2 = {27, 41, 56, 255};
    SDL_Color outline = {55, 76, 101, 255};
    SDL_Color white = {235, 241, 247, 255};
    SDL_Color muted = {142, 160, 181, 255};
    SDL_Color blue = {76, 157, 232, 255};
    SDL_Color green = {41, 196, 137, 255};
    SDL_Color orange = {235, 145, 78, 255};
    char buffer[256];

    SDL_GetRendererOutputSize(renderer, &width, &height);
    if (width < 1120) width = 1120;
    if (height < 720) height = 720;
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderClear(renderer);

    /* Permanent product navigation, deliberately dense like a solver rather than a trainer. */
    fill(renderer, (rect_t){0, 0, 188, height}, sidebar);
    line(renderer, 188, 0, 188, height, outline);
    text(renderer, 24, 24, "PE", 4, green);
    text(renderer, 24, 70, "SOLVER", 2, white);
    panel(renderer, (rect_t){16, 126, 156, 54}, app->page == 0 ? surface2 : sidebar,
          app->page == 0 ? blue : outline);
    panel(renderer, (rect_t){16, 190, 156, 54}, app->page == 1 ? surface2 : sidebar,
          app->page == 1 ? green : outline);
    panel(renderer, (rect_t){16, 254, 156, 54}, app->page == 2 ? surface2 : sidebar,
          app->page == 2 ? orange : outline);
    panel(renderer, (rect_t){16, 318, 156, 54}, app->page == 3 ? surface2 : sidebar,
          app->page == 3 ? blue : outline);
    text(renderer, 34, 147, "TREE / SETUP", 1, white);
    text(renderer, 34, 211, "SOLVE / STRATEGY", 1, white);
    text(renderer, 34, 275, "EXPLORE / TRAIN", 1, white);
    text(renderer, 34, 339, "EDIT RANGES", 1, white);
    text(renderer, 24, height - 52, "NATIVE C + SDL2", 1, muted);
    text(renderer, 24, height - 30, "MAC / LINUX / WIN", 1, muted);

    /* Top toolbar: every important state is visible without hunting through the screen. */
    fill(renderer, (rect_t){188, 0, width - 188, 70}, (SDL_Color){14, 22, 32, 255});
    text(renderer, 216, 18, app->page == 0 ? "NEW SOLVE" : app->page == 1 ? "SOLVE / STRATEGY" : "EXPLORE", 3, white);
    text(renderer, 216, 47, "LOCAL ANALYSIS WORKSPACE", 1, muted);
    panel(renderer, (rect_t){width - 430, 16, 92, 34}, surface2, outline);
    panel(renderer, (rect_t){width - 326, 16, 92, 34}, surface2, outline);
    panel(renderer, (rect_t){width - 222, 16, 92, 34}, (SDL_Color){31, 94, 78, 255}, green);
    text(renderer, width - 410, 28, "NEW", 1, white);
    text(renderer, width - 306, 28, "OPEN", 1, white);
    text(renderer, width - 202, 28, "SOLVE", 1, white);

    if (app->page == 0) {
        int left = 216;
        int right = 780;
        int bottom = height - 32;
        panel(renderer, (rect_t){left, 94, 532, bottom - 94}, surface, outline);
        panel(renderer, (rect_t){right, 94, width - right - 28, bottom - 94}, surface, outline);
        text(renderer, left + 24, 116, "SPOT CONFIGURATION", 2, blue);
        text(renderer, left + 24, 151, "GAME", 1, muted);
        panel(renderer, (rect_t){left + 24, 168, 212, 40}, surface2, outline);
        text(renderer, left + 38, 181, game_label_for(app->game_index), 2, white);
        text(renderer, left + 258, 151, "PLAYERS", 1, muted);
        panel(renderer, (rect_t){left + 258, 168, 220, 40}, surface2, outline);
        snprintf(buffer, sizeof(buffer), "%d  MAX / %s", app->player_count, app->player_count == 2 ? "HU" : "MULTIWAY");
        text(renderer, left + 272, 181, buffer, 1, white);
        text(renderer, left + 24, 234, "BOARD / RUNOUT (EMPTY = PREFLOP)", 1, muted);
        panel(renderer, (rect_t){left + 24, 251, 454, 40}, surface2, app->focus_field == 1 ? blue : outline);
        text(renderer, left + 38, 264, app->board_text, 1, white);
        text(renderer, left + 24, 317, "RANGES  (EXACT COMBOS OR RANGE TEXT)", 1, muted);
        panel(renderer, (rect_t){left + 24, 334, 216, 40}, surface2, app->focus_field == 2 ? blue : outline);
        panel(renderer, (rect_t){left + 262, 334, 216, 40}, surface2, app->focus_field == 3 ? blue : outline);
        snprintf(buffer, sizeof(buffer), "OOP  %s", app->range_oop); text(renderer, left + 38, 347, buffer, 1, white);
        snprintf(buffer, sizeof(buffer), "IP   %s", app->range_ip); text(renderer, left + 276, 347, buffer, 1, white);
        text(renderer, left + 24, 400, "ENGINE / BUDGET", 1, muted);
        panel(renderer, (rect_t){left + 24, 417, 216, 40}, surface2, outline);
        panel(renderer, (rect_t){left + 262, 417, 216, 40}, surface2, outline);
        text(renderer, left + 38, 430, engine_label_for(app->engine_index), 1,
             app->engine_index == 0 ? green : orange);
        snprintf(buffer, sizeof(buffer), "%d ITERATIONS", app->iterations); text(renderer, left + 276, 430, buffer, 1, white);
        text(renderer, left + 24, 483, "INPUT FILES", 1, muted);
        panel(renderer, (rect_t){left + 24, 500, 454, 36}, surface2, app->focus_field == 4 ? blue : outline);
        snprintf(buffer, sizeof(buffer), "TREE  %s", app->tree_path[0] ? basename_of(app->tree_path) : "drop or type .tree");
        text(renderer, left + 38, 512, buffer, 1, white);
        panel(renderer, (rect_t){left + 24, 546, 454, 36}, surface2, app->focus_field == 5 ? blue : outline);
        snprintf(buffer, sizeof(buffer), "MKR   %s", app->mkr_path[0] ? basename_of(app->mkr_path) : "drop or type .mkr");
        text(renderer, left + 38, 558, buffer, 1, white);
        text(renderer, left + 24, 608, "DROP .TREE / .MKR / .PE_SOL / LABEL CSV", 1, orange);
        text(renderer, left + 24, 632, app->solver_status, 1, app->solver_status[0] ? green : muted);
        snprintf(buffer, sizeof(buffer),
                 "AUTO BATCH %d  /  CPU:%s  CUDA:%s  OPENCL:%s",
                 app->sample_batch_size > 0 ? app->sample_batch_size : 128,
                 app->runtime_valid ? runtime_backend_state(
                     &app->runtime.backends[PE_COMPUTE_CPU_REF]) : "unknown",
                 app->runtime_valid ? runtime_backend_state(
                     &app->runtime.backends[PE_COMPUTE_CUDA]) : "unknown",
                 app->runtime_valid ? runtime_backend_state(
                     &app->runtime.backends[PE_COMPUTE_OPENCL]) : "unknown");
        text(renderer, left + 24, 660, buffer, 1, muted);
        text(renderer, left + 24, 684,
             "HIP / METAL / DISTRIBUTED: NOT INTEGRATED", 1, orange);

        text(renderer, right + 24, 116, "TREE PREVIEW", 2, blue);
        text(renderer, right + 24, 151, "ROOT / PREFLOP", 1, muted);
        draw_tree(app, renderer, right + 24, 180, width - right - 76, 196, white, muted, blue, green);
        text(renderer, right + 24, 410, "ACTIVE BOARD", 1, muted);
        for (int c = 0; c < 5; ++c) card(renderer, right + 24 + c * 70, 430, c < 5 ? "--" : "", c < 5);
        panel(renderer, (rect_t){right + 24, height - 110, width - right - 76, 52}, (SDL_Color){31, 102, 80, 255}, green);
        text(renderer, right + 46, height - 91, "SOLVE THIS SPOT  >", 2, white);
    } else if (app->page == 1) {
        int content_x = 212;
        int matrix_x = 232;
        int matrix_y = 142;
        int matrix_w = width > 1320 ? 610 : 560;
        int right_x = matrix_x + matrix_w + 24;
        int right_w = width - right_x - 28;
        int bottom_y = height - 252;
        panel(renderer, (rect_t){content_x, 94, width - content_x - 28, height - 116}, surface, outline);
        text(renderer, content_x + 20, 112, "STRATEGY VIEW", 2, green);
        snprintf(buffer, sizeof(buffer), "%s  |  %d PLAYERS  |  %s", game_label_for(app->game_index), app->player_count, engine_label_for(app->engine_index));
        text(renderer, content_x + 176, 116, buffer, 1, muted);
        panel(renderer, (rect_t){width - 270, 105, 104, 30}, surface2, outline);
        panel(renderer, (rect_t){width - 154, 105, 104, 30}, (SDL_Color){31, 102, 80, 255}, green);
        text(renderer, width - 251, 115, "TREE", 1, white);
        text(renderer, width - 137, 115, "RUN", 1, white);
        panel(renderer, (rect_t){matrix_x - 8, matrix_y - 12, matrix_w, bottom_y - matrix_y + 4}, (SDL_Color){17, 27, 39, 255}, outline);
        draw_hand_matrix(renderer, matrix_x + 12, matrix_y + 40, matrix_w - 28, bottom_y - matrix_y - 56,
                         app->range_oop, app->range_ip, white, muted, blue, orange, outline);
        panel(renderer, (rect_t){right_x, matrix_y - 12, right_w, 236}, (SDL_Color){17, 27, 39, 255}, outline);
        draw_table_view(renderer, right_x + 12, matrix_y + 4, right_w - 24, 204,
                        app->board_text, white, muted, green, outline);
        panel(renderer, (rect_t){right_x, matrix_y + 238, right_w, bottom_y - matrix_y - 238}, (SDL_Color){17, 27, 39, 255}, outline);
        draw_action_summary(renderer, right_x + 16, matrix_y + 260, right_w - 32, bottom_y - matrix_y - 278,
                            app, white, muted, blue, green, orange, outline);
        panel(renderer, (rect_t){matrix_x - 8, bottom_y + 20, width - matrix_x - 20, height - bottom_y - 148}, (SDL_Color){17, 27, 39, 255}, outline);
        draw_strategy_table(renderer, matrix_x + 14, bottom_y + 36, width - matrix_x - 48, height - bottom_y - 178,
                            app, white, muted, blue, green, outline);
        if (app->solver_status[0]) text(renderer, matrix_x + 14, height - 42, app->solver_status, 1, green);
    } else if (app->page == 2) {
        int left = 212;
        int right = 860;
        panel(renderer, (rect_t){left, 94, 624, height - 116}, surface, outline);
        panel(renderer, (rect_t){right, 94, width - right - 28, height - 116}, surface, outline);
        text(renderer, left + 24, 116, "INFOSSET / STRATEGY EXPLORER", 2, orange);
        if (app->spot_count) {
            size_t index = app->selected_spot < app->spot_count ? app->selected_spot : 0;
            const spot_t *spot = &app->spots[index];
            snprintf(buffer, sizeof(buffer), "KEY  0x%016llx", (unsigned long long)spot->key);
            text(renderer, left + 24, 154, buffer, 1, white);
            text(renderer, left + 24, 184, "ACTION FREQUENCIES", 1, muted);
            for (int action = 0; action < spot->actions && action < 8; ++action) {
                int by = 212 + action * 48;
                const char *name = app->action_names[action][0] ? app->action_names[action] : "OPTION";
                snprintf(buffer, sizeof(buffer), "%s", name); text(renderer, left + 24, by + 8, buffer, 1, white);
                fill(renderer, (rect_t){left + 158, by + 5, 360, 18}, surface2);
                fill(renderer, (rect_t){left + 158, by + 5, (int)(360.0 * spot->probability[action]), 18}, action == app->selected_action ? green : blue);
                snprintf(buffer, sizeof(buffer), "%.1f%%", spot->probability[action] * 100.0);
                text(renderer, left + 535, by + 8, buffer, 1, white);
            }
        } else {
            text(renderer, left + 24, 170, "DROP A .PE_SOL OR RUN A SPOT FIRST", 2, muted);
            draw_hand_matrix(renderer, left + 24, 240, 570, 420, app->range_oop, app->range_ip,
                             white, muted, blue, orange, outline);
        }
        text(renderer, right + 24, 116, "CONTEXT / TRAINING", 2, orange);
        const label_t *meta = app->spot_count ? metadata_for(app, app->spots[app->selected_spot < app->spot_count ? app->selected_spot : 0].key) : NULL;
        text(renderer, right + 24, 160, "STREET", 1, muted); text(renderer, right + 24, 182, meta && meta->street[0] ? meta->street : "NOT PROVIDED", 2, white);
        text(renderer, right + 24, 224, "BOARD", 1, muted); text(renderer, right + 24, 246, meta && meta->board[0] ? meta->board : app->board_text, 1, white);
        text(renderer, right + 24, 288, "POSITION", 1, muted); text(renderer, right + 24, 310, meta && meta->position[0] ? meta->position : "UNKNOWN", 2, white);
        panel(renderer, (rect_t){right + 24, 360, width - right - 76, 50}, surface2, outline); text(renderer, right + 42, 378, "BACK TO STRATEGY", 1, white);
        panel(renderer, (rect_t){right + 24, 430, width - right - 76, 50}, (SDL_Color){31, 102, 80, 255}, green); text(renderer, right + 42, 448, "TRAIN SELECTED SPOT", 1, white);
        panel(renderer, (rect_t){right + 24, 510, width - right - 76, 120}, (SDL_Color){17, 27, 39, 255}, outline);
        text(renderer, right + 42, 530, "RUN OUTPUT", 1, muted);
        if (app->solver_result[0]) text(renderer, right + 42, 560, app->solver_result, 1, white);
        else text(renderer, right + 42, 560, "No run yet", 1, muted);
    } else {
        int left = 212;
        int matrix_x = 246;
        int matrix_y = 170;
        int cell = 38;
        const char *player_label = app->range_editor_player == 0 ? "OOP RANGE" : "IP RANGE";
        panel(renderer, (rect_t){left, 94, 680, height - 116}, surface, outline);
        panel(renderer, (rect_t){916, 94, width - 944, height - 116}, surface, outline);
        text(renderer, left + 24, 116, "RANGE EDITOR", 2, blue);
        text(renderer, left + 24, 142, "CLICK HAND CLASSES TO TOGGLE THEM", 1, muted);
        text(renderer, matrix_x, matrix_y - 20, player_label, 2, green);
        text(renderer, matrix_x + 180, matrix_y - 20, "HOLD'EM CLASS RANGE", 1, muted);
        {
            const char *ranks = "AKQJT98765432";
            for (int row = 0; row < 13; ++row) {
                text(renderer, matrix_x - 22, matrix_y + row * cell + 10,
                     (char[]){ranks[row], '\0'}, 1, muted);
                for (int column = 0; column < 13; ++column) {
                    int selected = app->range_matrix[app->range_editor_player][row][column] != 0;
                    SDL_Color color = selected ? (app->range_editor_player == 0 ? blue : orange) : surface2;
                    fill(renderer, (rect_t){matrix_x + column * cell, matrix_y + row * cell, cell - 2, cell - 2}, color);
                    border(renderer, (rect_t){matrix_x + column * cell, matrix_y + row * cell, cell - 2, cell - 2}, outline);
                    {
                        char hand[4];
                        hand[0] = ranks[row]; hand[1] = ranks[column];
                        hand[2] = row == column ? 'p' : row < column ? 's' : 'o';
                        hand[3] = '\0';
                        text(renderer, matrix_x + column * cell + 7,
                             matrix_y + row * cell + 12, hand, 1, white);
                    }
                }
            }
        }
        panel(renderer, (rect_t){246, 680, 160, 42}, app->range_editor_player == 0 ? (SDL_Color){39, 103, 166, 255} : surface2, blue);
        panel(renderer, (rect_t){418, 680, 160, 42}, app->range_editor_player == 1 ? (SDL_Color){169, 101, 57, 255} : surface2, orange);
        panel(renderer, (rect_t){590, 680, 160, 42}, (SDL_Color){31, 102, 80, 255}, green);
        text(renderer, 270, 695, "EDIT OOP", 1, white);
        text(renderer, 442, 695, "EDIT IP", 1, white);
        text(renderer, 610, 695, "APPLY RANGE", 1, white);
        text(renderer, 940, 116, "RANGE PREVIEW", 2, blue);
        text(renderer, 940, 150, "OOP", 1, muted);
        text(renderer, 940, 172, app->range_oop, 1, white);
        text(renderer, 940, 220, "IP", 1, muted);
        text(renderer, 940, 242, app->range_ip, 1, white);
        text(renderer, 940, 300, "EDITOR NOTES", 1, muted);
        text(renderer, 940, 326, "BLUE = OOP", 1, blue);
        text(renderer, 940, 350, "ORANGE = IP", 1, orange);
        text(renderer, 940, 382, "PLO USES EXACT COMBOS", 1, white);
        text(renderer, 940, 406, "IN THE SETUP FIELDS", 1, white);
        panel(renderer, (rect_t){940, 470, width - 984, 64}, (SDL_Color){31, 102, 80, 255}, green);
        text(renderer, 964, 495, "RETURN TO SETUP  >", 1, white);
    }
    SDL_RenderPresent(renderer);
}

static void render(SDL_Renderer *renderer, const app_t *app)
{
    SDL_Color white = {235,239,244,255}, muted = {155,165,178,255}, blue = {86,147,255,255}, green = {74,190,125,255}, orange = {242,164,73,255};
    SDL_SetRenderDrawColor(renderer, 18, 23, 30, 255); SDL_RenderClear(renderer);
    fill(renderer, (rect_t){28,24,1124,80}, (SDL_Color){28,36,46,255}); text(renderer, 54, 42, "POKER-EVAL TRAINER", 4, white); text(renderer, 55, 67, "CHOISIS UNE ACTION, PUIS COMPARE-LA A LA STRATEGIE", 2, muted);
    fill(renderer, (rect_t){28,124,1124,64}, (SDL_Color){27,34,43,255});
    text(renderer, 52, 140, app->solution_path[0] ? "SOLUTION CHARGEE" : "DEPOSE UN FICHIER .PE_SOL", 2, app->solution_path[0] ? green : orange);
    text(renderer, 300, 140, app->solution_path[0] ? basename_of(app->solution_path) : "puis un CSV de labels ou --actions", 2, white);
    text(renderer, 52, 164, app->labels_path[0] ? "CONTEXTE CHARGE" : "CONTEXTE OPTIONNEL", 2, app->labels_path[0] ? green : orange);
    text(renderer, 300, 164, app->labels_path[0] ? basename_of(app->labels_path) : "--actions fold,check,call,raise", 2, muted);
    fill(renderer, (rect_t){28,212,1124,280}, (SDL_Color){29,38,49,255}); border(renderer, (rect_t){28,212,1124,280}, (SDL_Color){57,73,91,255});
    if (app->has_current && app->current < app->spot_count) {
        const spot_t *spot = &app->spots[app->current]; const label_t *meta = metadata_for(app, spot->key); char line[512];
        if (meta && meta->street[0]) snprintf(line, sizeof(line), "%s  /  %s  /  %s", meta->street, meta->position[0] ? meta->position : "position inconnue", meta->board[0] ? meta->board : "board non renseigne");
        else snprintf(line, sizeof(line), "CONTEXTE MANQUANT  -  charge le CSV labels pour voir street / board / position");
        text(renderer, 54, 238, line, 2, meta && meta->street[0] ? white : orange);
        if (meta && meta->has_pot) { snprintf(line, sizeof(line), "POT : %.2f", meta->pot); text(renderer, 900, 238, line, 2, muted); }
        snprintf(line, sizeof(line), "SPOT %zu / %zu", app->current + 1u, app->spot_count); text(renderer, 54, 276, line, 3, blue);
        snprintf(line, sizeof(line), "Question : quelle action joues-tu ici ?"); text(renderer, 54, 318, line, 3, white);
        if (!app->answered_current && app->labels_path[0] == '\0') text(renderer, 54, 354, "1 lis le contexte   2 choisis une action   3 appuie sur N", 2, orange);
        if (app->answered_current) {
            const label_t *selected = label_for(app, spot->key, app->selected_action);
            const label_t *best = label_for(app, spot->key, app->best_action);
            snprintf(line, sizeof(line), "TA REPONSE : %s   /   SOLUTION : %s",
                     selected ? selected->label : "action choisie",
                     best ? best->label : "meilleure frequence");
            text(renderer, 54, 354, line, 2, app->streak ? green : orange);
        }
        for (int action = 0; action < spot->actions && action < 8; ++action) {
            int column = action % 4, row = action / 4; rect_t button = {54 + column * 245, 382 + row * 48, 225, 38};
            const label_t *label = label_for(app, spot->key, action); char button_text[128];
            const char *name = label ? label->label :
                (app->action_names[action][0] ? app->action_names[action] : "OPTION");
            snprintf(button_text, sizeof(button_text), "%s  %.1f%%", name, spot->probability[action] * 100.0);
            SDL_Color button_color = (SDL_Color){53,80,112,255};
            if (app->answered_current && action == app->selected_action) button_color = app->selected_action == app->best_action ? green : orange;
            else if (app->answered_current && action == app->best_action) button_color = blue;
            fill(renderer, button, button_color); text(renderer, button.x + 14, button.y + 12, button_text, 2, white);
        }
    } else text(renderer, 54, 270, "DEPOSE TON .PE_SOL ICI POUR COMMENCER", 3, white);
    fill(renderer, (rect_t){28,516,1124,112}, (SDL_Color){27,34,43,255});
    text(renderer, 54, 540, app->answered_current ? app->feedback : "ETAPE 1 : CHOISIS UNE ACTION CI-DESSUS", 3, app->answered_current ? (app->streak ? green : orange) : white);
    { char score[128]; snprintf(score, sizeof(score), "SCORE : %d / %d     DIFFICULTE : %d / 5     SERIE : %d", app->score, app->answered, app->difficulty, app->streak); text(renderer, 54, 585, score, 2, muted); }
    fill(renderer, (rect_t){28,654,230,54}, (SDL_Color){53,80,112,255}); text(renderer, 57, 672, "SPOT SUIVANT (N)", 2, white);
    fill(renderer, (rect_t){278,654,440,54}, (SDL_Color){31,42,53,255}); text(renderer, 300, 672, "N : SUIVANT   S : SAUVEGARDER", 2, muted);
    { char session[160]; snprintf(session, sizeof(session), "SESSION : %s", app->session_path[0] ? basename_of(app->session_path) : "trainer-session.json"); text(renderer, 744, 672, session, 2, muted); }
    SDL_RenderPresent(renderer);
}

static int write_json_string(FILE *file, const char *value)
{
    const unsigned char *cursor = (const unsigned char *)(value ? value : "");
    if (fputc('"', file) == EOF) return -1;
    for (; *cursor; ++cursor) {
        switch (*cursor) {
        case '"': if (fputs("\\\"", file) == EOF) return -1; break;
        case '\\': if (fputs("\\\\", file) == EOF) return -1; break;
        case '\b': if (fputs("\\b", file) == EOF) return -1; break;
        case '\f': if (fputs("\\f", file) == EOF) return -1; break;
        case '\n': if (fputs("\\n", file) == EOF) return -1; break;
        case '\r': if (fputs("\\r", file) == EOF) return -1; break;
        case '\t': if (fputs("\\t", file) == EOF) return -1; break;
        default:
            if (*cursor < 0x20u) {
                if (fprintf(file, "\\u%04x", (unsigned int)*cursor) < 0) return -1;
            } else if (fputc(*cursor, file) == EOF) return -1;
            break;
        }
    }
    return fputc('"', file) == EOF ? -1 : 0;
}

static void save_session(const app_t *app)
{
    const char *path = app->session_path[0] ? app->session_path : "trainer-session.json";
    FILE *file = fopen(path, "w"); if (!file) return;
    fputs("{\"schema\":\"pe-trainer-session/v1\",\"solution\":", file);
    if (write_json_string(file, app->solution_path) != 0) { fclose(file); return; }
    fprintf(file, ",\"answered\":%d,\"best_answers\":%d,\"probability_loss\":%.17g,\"difficulty\":%d,\"events\":[", app->answered, app->score, app->probability_loss, app->difficulty);
    for (size_t i = 0; i < app->event_count; ++i) { if (i) fputc(',', file); fprintf(file, "{\"key\":\"0x%016llx\",\"selected\":%d,\"best\":%d}", (unsigned long long)app->events[i].key, app->events[i].selected, app->events[i].best); }
    fputs("]}\n", file); fclose(file);
}

int main(int argc, char **argv)
{
    SDL_Window *window; SDL_Renderer *renderer; SDL_Event event; app_t app; int mouse_x, mouse_y;
    const char *resume_path = NULL;
    memset(&app, 0, sizeof(app));
    app.random_state = 1u;
    app.difficulty = 1;
    app.running = 1;
    app.page = 0;
    app.selected_action = -1;
    app.iterations = 100000;
    app.sample_batch_size = 128;
    app.game_index = 0;
    app.player_count = 2;
    app.engine_index = 0;
    app.focus_field = 0;
    app.runtime_valid = pe_runtime_probe(&app.runtime) == 0;
    snprintf(app.game_name, sizeof(app.game_name), "HOLDEM");
    snprintf(app.board_text, sizeof(app.board_text), "2c 7d Th Js Qc");
    set_default_ranges_for_game(&app);
    copy_field(app.solver_status, sizeof(app.solver_status), "Drop a tree or solution to begin");
    {
        const char *slash = strrchr(argv[0], '/');
        const char *backslash = strrchr(argv[0], '\\');
        if (backslash && (!slash || backslash > slash)) slash = backslash;
        if (slash)
        {
            size_t length = (size_t)(slash - argv[0]);
            if (length >= sizeof(app.executable_dir)) length = sizeof(app.executable_dir) - 1u;
            for (size_t offset = 0u; offset < length; ++offset)
                app.executable_dir[offset] = argv[0][offset];
            app.executable_dir[length] = '\0';
        }
    }
    font_init();
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0) {
            puts("usage: poker-eval-trainer-gui [--tree FILE] [--mkr FILE] [--solution FILE] [--labels CSV] [--actions a,b,c] [--session-json FILE] [--resume-session FILE]");
            return 0;
        }
        if (i + 1 >= argc) {
            fprintf(stderr, "missing value for %s\n", argv[i]);
            return 2;
        }
        if (strcmp(argv[i], "--solution") == 0) set_dropped_file(&app, argv[++i]);
        else if (strcmp(argv[i], "--labels") == 0) set_dropped_file(&app, argv[++i]);
        else if (strcmp(argv[i], "--tree") == 0) set_dropped_file(&app, argv[++i]);
        else if (strcmp(argv[i], "--mkr") == 0) set_dropped_file(&app, argv[++i]);
        else if (strcmp(argv[i], "--actions") == 0) load_action_names(&app, argv[++i]);
        else if (strcmp(argv[i], "--session-json") == 0)
            snprintf(app.session_path, sizeof(app.session_path), "%s", argv[++i]);
        else if (strcmp(argv[i], "--resume-session") == 0)
            resume_path = argv[++i];
        else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            return 2;
        }
    }
    if (resume_path) {
        int answered = 0, best_answers = 0;
        double probability_loss = 0.0;
        if (load_session_summary(resume_path, &answered, &best_answers,
                                 &probability_loss) != 0) {
            fprintf(stderr, "cannot read session %s\n", resume_path);
            free(app.spots); free(app.labels); return 1;
        }
        app.answered = answered; app.score = best_answers;
        app.probability_loss = probability_loss;
        if (answered > 0) {
            double accuracy = (double)best_answers / (double)answered;
            app.difficulty = accuracy >= 0.80 ? 3 : accuracy >= 0.60 ? 2 : 1;
        }
    }
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    window = SDL_CreateWindow("poker-eval Trainer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_RESIZABLE);
    renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : NULL;
    if (!window || !renderer) { fprintf(stderr, "SDL window: %s\n", SDL_GetError()); if (renderer) SDL_DestroyRenderer(renderer); if (window) SDL_DestroyWindow(window); SDL_Quit(); return 1; }
    SDL_StartTextInput();
    if (app.spot_count) next_spot(&app);
    while (app.running) {
        finish_pending_solve(&app);
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                app.running = 0;
                cancel_pending_solve(&app);
                break;
            }
            else if (event.type == SDL_DROPFILE) {
                set_dropped_file(&app, event.drop.file);
                if (app.spot_count) next_spot(&app);
                SDL_free(event.drop.file);
            }
            else if (event.type == SDL_MOUSEWHEEL && app.page == 2 && app.spot_count) {
                int output_width = 0, output_height = 0;
                int visible_rows;
                int total_rows;
                int max_scroll;
                SDL_GetRendererOutputSize(renderer, &output_width, &output_height);
                (void)output_width;
                visible_rows = (output_height - 530) / 48;
                if (visible_rows < 1) visible_rows = 1;
                total_rows = (app.spots[app.selected_spot < app.spot_count ? app.selected_spot : 0].actions + 1) / 2;
                max_scroll = total_rows > visible_rows ? total_rows - visible_rows : 0;
                app.training_action_scroll -= event.wheel.y;
                if (app.training_action_scroll < 0) app.training_action_scroll = 0;
                if (app.training_action_scroll > max_scroll) app.training_action_scroll = max_scroll;
            }
            else if (event.type == SDL_TEXTINPUT) append_text_input(&app, event.text.text);
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_n) next_spot(&app);
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_s) save_session(&app);
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_BACKSPACE) remove_text_input(&app);
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) app.focus_field = 0;
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_1) app.page = 0;
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_2) app.page = 1;
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_3) app.page = 2;
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_4) app.page = 3;
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_TAB) app.page = (app.page + 1) % 4;
            else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                mouse_x = event.button.x; mouse_y = event.button.y;
                int output_width = 0, output_height = 0;
                SDL_GetRendererOutputSize(renderer, &output_width, &output_height);
                gui_layout_t layout = layout_for(output_width, output_height);
                if (inside(layout.setup_tab, mouse_x, mouse_y)) app.page = 0;
                else if (inside(layout.solve_tab, mouse_x, mouse_y)) app.page = 1;
                else if (inside(layout.explore_tab, mouse_x, mouse_y)) app.page = 2;
                else if (inside(layout.range_tab, mouse_x, mouse_y)) app.page = 3;
                else if (mouse_x >= output_width - 222 && mouse_x < output_width - 130 &&
                         mouse_y >= 0 && mouse_y < 70) {
                    if (run_selected_solver(&app) == 0)
                        app.page = 1;
                }
                else if (app.page == 0 && mouse_x >= 804 && mouse_x < output_width - 28 &&
                         mouse_y >= output_height - 110 && mouse_y < output_height - 58) {
                    if (run_selected_solver(&app) == 0)
                        app.page = 1;
                }
                else if (app.page == 0 && mouse_x >= 240 && mouse_x < 452 &&
                         mouse_y >= 168 && mouse_y < 208) {
                    app.game_index = (app.game_index + 1) % 4;
                    set_default_ranges_for_game(&app);
                }
                else if (app.page == 0 && mouse_x >= 474 && mouse_x < 694 &&
                         mouse_y >= 168 && mouse_y < 208) {
                    app.player_count = app.player_count >= 8 ? 2 : app.player_count + 1;
                }
                else if (app.page == 0 && mouse_x >= 240 && mouse_x < 694 &&
                         mouse_y >= 251 && mouse_y < 291) {
                    app.focus_field = 1;
                }
                else if (app.page == 0 && mouse_x >= 240 && mouse_x < 456 &&
                         mouse_y >= 334 && mouse_y < 374) {
                    app.focus_field = 2;
                }
                else if (app.page == 0 && mouse_x >= 478 && mouse_x < 694 &&
                         mouse_y >= 334 && mouse_y < 374) {
                    app.focus_field = 3;
                }
                else if (app.page == 0 && mouse_x >= 240 && mouse_x < 456 &&
                         mouse_y >= 417 && mouse_y < 457) {
                    app.engine_index = (app.engine_index + 1) % 4;
                }
                else if (app.page == 0 && mouse_x >= 240 && mouse_x < 694 &&
                         mouse_y >= 500 && mouse_y < 536) {
                    app.focus_field = 4;
                }
                else if (app.page == 0 && mouse_x >= 240 && mouse_x < 694 &&
                         mouse_y >= 546 && mouse_y < 582) {
                    app.focus_field = 5;
                }
                else if (app.page == 1 && mouse_x >= output_width - 154 && mouse_x < output_width - 50 &&
                         mouse_y >= 105 && mouse_y < 135)
                    run_selected_solver(&app);
                else if (app.page == 1 && mouse_x >= 1050 && mouse_x < output_width - 52 &&
                         mouse_y >= 586 && mouse_y < 650)
                    app.page = 2;
                else if (app.page == 2 && app.spot_count && mouse_x >= 264 && mouse_x < 1000 && mouse_y >= 338 && mouse_y < 338 + (int)((app.spot_count < 8 ? app.spot_count : 8) * 52u)) {
                    size_t picked = (size_t)((mouse_y - 338) / 52);
                    if (picked < app.spot_count) {
                        app.selected_spot = picked;
                        app.selected_key = app.spots[picked].key;
                        app.training_action_scroll = 0;
                    }
                }
                else if (app.page == 2 && inside(layout.next_button, mouse_x, mouse_y)) {
                    next_spot(&app);
                    if (app.has_current) {
                        app.selected_spot = app.current;
                        app.selected_key = app.spots[app.current].key;
                    }
                }
                else if (app.page == 2 && app.spot_count && mouse_x >= 1030 &&
                         mouse_x < output_width - 52 && mouse_y >= 506) {
                    int button_width = (output_width - 1030 - 76) / 2;
                    int visible_rows = (output_height - 530) / 48;
                    int total_rows;
                    int max_scroll;
                    int scroll;
                    int column;
                    int row;
                    int action;
                    int local_x;
                    int local_y;
                    size_t selected = app.selected_spot < app.spot_count ? app.selected_spot : 0;
                    if (visible_rows < 1) visible_rows = 1;
                    total_rows = (app.spots[selected].actions + 1) / 2;
                    max_scroll = total_rows > visible_rows ? total_rows - visible_rows : 0;
                    scroll = app.training_action_scroll > max_scroll ? max_scroll : app.training_action_scroll;
                    column = button_width > 0 ? (mouse_x - 1054) / (button_width + 12) : -1;
                    row = (mouse_y - 506) / 48;
                    action = (scroll + row) * 2 + column;
                    local_x = column >= 0 ? mouse_x - (1054 + column * (button_width + 12)) : -1;
                    local_y = mouse_y - (506 + row * 48);
                    if (column >= 0 && column < 2 && row >= 0 && row < visible_rows &&
                        local_x >= 0 && local_x < button_width && local_y >= 0 && local_y < 38 &&
                        action < app.spots[selected].actions) {
                        app.current = selected;
                        app.has_current = 1;
                        app.answered_current = 0;
                        answer(&app, action);
                    }
                }
                else if (app.page == 3 && mouse_x >= 246 && mouse_x < 246 + 13 * 38 &&
                         mouse_y >= 170 && mouse_y < 170 + 13 * 38) {
                    if (app.game_index != 0) {
                        copy_field(app.solver_status, sizeof(app.solver_status),
                                   "PLO needs exact multi-card ranges in Setup");
                    } else {
                        int column = (mouse_x - 246) / 38;
                        int row = (mouse_y - 170) / 38;
                        app.range_matrix[app.range_editor_player][row][column] =
                            (uint8_t)!app.range_matrix[app.range_editor_player][row][column];
                        range_matrix_to_text(&app, app.range_editor_player);
                        copy_field(app.solver_status, sizeof(app.solver_status), "Range matrix edited");
                    }
                }
                else if (app.page == 3 && mouse_x >= 246 && mouse_x < 406 &&
                         mouse_y >= 680 && mouse_y < 722)
                    app.range_editor_player = 0;
                else if (app.page == 3 && mouse_x >= 418 && mouse_x < 578 &&
                         mouse_y >= 680 && mouse_y < 722)
                    app.range_editor_player = 1;
                else if (app.page == 3 && mouse_x >= 590 && mouse_x < 750 &&
                         mouse_y >= 680 && mouse_y < 722) {
                    if (app.game_index == 0) {
                        copy_field(app.solver_status, sizeof(app.solver_status), "Range applied to current spot");
                        app.page = 0;
                    } else {
                        copy_field(app.solver_status, sizeof(app.solver_status),
                                   "PLO keeps exact ranges entered in Setup");
                    }
                }
            }
        }
        render_solver(renderer, &app); SDL_Delay(16);
    }
    /* Stop the child before joining the worker so closing never waits for a
     * long solve to finish naturally. */
    cancel_pending_solve(&app);
    while (SDL_AtomicGet(&app.solve_state) == SOLVE_RUNNING) SDL_Delay(16);
    finish_pending_solve(&app);
    pe_tree_json_free(&app.tree_view);
    save_session(&app); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit(); free(app.spots); free(app.labels); free(app.events); return 0;
}
