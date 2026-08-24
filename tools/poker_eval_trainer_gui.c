/* Cross-platform poker-eval trainer GUI: pure C + SDL2. */
#include <SDL2/SDL.h>

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WINDOW_WIDTH 1440
#define WINDOW_HEIGHT 900
#define MAX_ACTIONS 256

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
    uint64_t selected_key;
    size_t selected_spot;
    char game_name[24];
    char board_text[48];
    char range_oop[48];
    char range_ip[48];
    int iterations;
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

static uint16_t u16(const unsigned char *data, size_t offset)
{ return (uint16_t)((uint16_t)data[offset] | ((uint16_t)data[offset + 1u] << 8)); }

static uint32_t u32(const unsigned char *data, size_t offset)
{ return (uint32_t)data[offset] | ((uint32_t)data[offset + 1u] << 8) | ((uint32_t)data[offset + 2u] << 16) | ((uint32_t)data[offset + 3u] << 24); }

static uint64_t u64(const unsigned char *data, size_t offset)
{
    uint64_t value = 0; for (size_t i = 0; i < 8u; ++i) value |= (uint64_t)data[offset + i] << (i * 8u); return value;
}

static int load_solution(app_t *app, const char *path)
{
    FILE *file = fopen(path, "rb"); long size; unsigned char *data; size_t offset = 32u; uint64_t count; spot_t *spots;
    if (!file || fseek(file, 0, SEEK_END) != 0) { if (file) fclose(file); return -1; }
    size = ftell(file); rewind(file); if (size < 32) { fclose(file); return -1; }
    data = (unsigned char *)malloc((size_t)size);
    if (!data || fread(data, 1u, (size_t)size, file) != (size_t)size) { free(data); fclose(file); return -1; }
    fclose(file);
    if (memcmp(data, "PESOL001", 8u) != 0 || u32(data, 8u) != 1u) { free(data); return -1; }
    count = u64(data, 16u); if (count > SIZE_MAX / sizeof(*spots)) { free(data); return -1; }
    spots = (spot_t *)calloc((size_t)count, sizeof(*spots)); if (!spots && count) { free(data); return -1; }
    for (uint64_t i = 0; i < count; ++i) {
        uint32_t actions;
        if (offset + 12u > (size_t)size) { free(spots); free(data); return -1; }
        spots[i].key = u64(data, offset); actions = u32(data, offset + 8u); offset += 12u;
        if (actions == 0u || actions > MAX_ACTIONS || actions > ((size_t)size - offset) / 2u) { free(spots); free(data); return -1; }
        spots[i].actions = (int)actions;
        for (uint32_t action = 0; action < actions; ++action) { uint16_t quantized = u16(data, offset + action * 2u); spots[i].probability[action] = (double)quantized / 65535.0; }
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
        parsed[i] = strtod(at + strlen(fields[i]), &end);
        if (end == at + strlen(fields[i])) { free(data); return -1; }
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
    size_t candidates[4096]; size_t count = 0u;
    if (app->spot_count == 0u) return;
    for (size_t i = 0; i < app->spot_count && count < 4096u; ++i) {
        double best = 0.0; for (int action = 0; action < app->spots[i].actions; ++action) if (app->spots[i].probability[action] > best) best = app->spots[i].probability[action];
        if (app->difficulty == 1 || best < 0.8) candidates[count++] = i;
    }
    if (!count) return; app->random_state = app->random_state * 1664525u + 1013904223u; app->current = candidates[app->random_state % count]; app->has_current = 1; app->answered_current = 0; app->selected_action = -1; app->best_action = -1;
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

typedef struct {
    int width;
    int height;
    rect_t sidebar;
    rect_t setup_tab;
    rect_t solve_tab;
    rect_t explore_tab;
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

static void draw_tree(SDL_Renderer *renderer, int x, int y, int width,
                      int height, SDL_Color white, SDL_Color muted,
                      SDL_Color blue, SDL_Color green)
{
    int root_x = x + width / 2;
    int child_y = y + 76;
    int child_left = x + width / 4;
    int child_right = x + 3 * width / 4;
    line(renderer, root_x, y + 30, child_left, child_y, muted);
    line(renderer, root_x, y + 30, child_right, child_y, muted);
    panel(renderer, (rect_t){root_x - 66, y, 132, 42}, (SDL_Color){39, 65, 91, 255}, blue);
    panel(renderer, (rect_t){child_left - 66, child_y, 132, 42}, (SDL_Color){34, 57, 55, 255}, green);
    panel(renderer, (rect_t){child_right - 66, child_y, 132, 42}, (SDL_Color){47, 51, 67, 255}, blue);
    text(renderer, root_x - 49, y + 14, "ACTING", 2, white);
    text(renderer, child_left - 49, child_y + 14, "CHECK", 2, white);
    text(renderer, child_right - 49, child_y + 14, "BET 33", 2, white);
    text(renderer, x + 18, y + height - 30, "TREE PATH  /  ROOT > FLOP > DECISION", 2, muted);
}

static void render_solver(SDL_Renderer *renderer, const app_t *app)
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
        text(renderer, left + 40, 251, app->game_name[0] ? app->game_name : "HOLDEM", 2, white);
        text(renderer, left + 260, 212, "PLAYERS", 2, muted);
        panel(renderer, (rect_t){left + 260, 236, 184, 46}, surface2, outline);
        text(renderer, left + 276, 251, "2  /  HEADS-UP", 2, white);
        text(renderer, left + 24, 314, "BOARD", 2, muted);
        panel(renderer, (rect_t){left + 24, 338, 420, 46}, surface2, outline);
        text(renderer, left + 40, 353, app->board_text[0] ? app->board_text : "--  --  --  --  --", 2, white);
        text(renderer, left + 24, 416, "RANGES", 2, muted);
        panel(renderer, (rect_t){left + 24, 440, 204, 46}, surface2, outline);
        panel(renderer, (rect_t){left + 240, 440, 204, 46}, surface2, outline);
        text(renderer, left + 40, 455, app->range_oop[0] ? app->range_oop : "OOP  100%", 2, white);
        text(renderer, left + 256, 455, app->range_ip[0] ? app->range_ip : "IP   100%", 2, white);
        text(renderer, left + 24, 518, "ITERATIONS", 2, muted);
        snprintf(buffer, sizeof(buffer), "%d", app->iterations > 0 ? app->iterations : 100000);
        panel(renderer, (rect_t){left + 24, 542, 204, 46}, surface2, outline);
        text(renderer, left + 40, 557, buffer, 2, white);
        text(renderer, left + 24, 632, "DROP .PE_SOL OR LABEL CSV TO LOAD A PROJECT", 2, orange);

        text(renderer, right + 24, 166, "SPOT PREVIEW", 2, blue);
        text(renderer, right + 24, 208, "BOARD", 2, muted);
        for (int c = 0; c < 5; ++c)
            card(renderer, right + 24 + c * 86, 238, c < 3 ? "--" : "", c < 3);
        text(renderer, right + 24, 366, "ACTION TREE", 2, muted);
        draw_tree(renderer, right + 24, 398, width - right - 76, 180, white, muted, blue, green);
        panel(renderer, (rect_t){right + 24, height - 154, width - right - 76, 54}, (SDL_Color){35, 77, 73, 255}, green);
        text(renderer, right + 42, height - 137, "READY TO SOLVE  >", 2, white);
    }
    else if (app->page == 1)
    {
        int left = 264;
        int right = 1050;
        panel(renderer, (rect_t){left, 140, 760, height - 244}, surface, outline);
        panel(renderer, (rect_t){right, 140, width - right - 28, height - 244}, surface, outline);
        text(renderer, left + 24, 166, "TREE MONITOR", 2, blue);
        text(renderer, left + 24, 208, "ROOT  /  PREFLOP  /  BTN VS BB", 2, muted);
        draw_tree(renderer, left + 28, 260, 680, 220, white, muted, blue, green);
        text(renderer, left + 24, 536, "ITERATION", 2, muted);
        text(renderer, left + 24, 562, app->solution_path[0] ? "SNAPSHOT LOADED" : "WAITING FOR INPUT", 3, app->solution_path[0] ? green : orange);
        line(renderer, left + 24, 618, left + 704, 618, outline);
        stat_value(renderer, left + 24, 646, "INFOSETS", app->spot_count ? "LOADED" : "--", white);
        stat_value(renderer, left + 178, 646, "EXPLOITABILITY", app->spot_count ? "READY" : "--", green);
        stat_value(renderer, left + 400, 646, "STATUS", "CPU / EXACT", blue);
        text(renderer, right + 24, 166, "RUN SUMMARY", 2, blue);
        stat_value(renderer, right + 24, 216, "SOLUTION", app->solution_path[0] ? "OPEN" : "NONE", app->solution_path[0] ? green : orange);
        snprintf(buffer, sizeof(buffer), "%zu", app->spot_count);
        stat_value(renderer, right + 24, 302, "INFOSets", buffer, white);
        snprintf(buffer, sizeof(buffer), "%d", app->iterations > 0 ? app->iterations : 100000);
        stat_value(renderer, right + 24, 388, "BUDGET", buffer, white);
        panel(renderer, (rect_t){right + 24, 500, width - right - 76, 64}, (SDL_Color){27, 49, 70, 255}, outline);
        text(renderer, right + 42, 523, "OPEN A SNAPSHOT OR DROP A FILE", 2, orange);
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
            int button_width = (width - right - 100) / 2;
            for (int action = 0; action < 4; ++action)
            {
                int bx = right + 24 + (action % 2) * (button_width + 12);
                int by = 506 + (action / 2) * 48;
                const char *name = app->action_names[action][0] ? app->action_names[action] : "OPTION";
                panel(renderer, (rect_t){bx, by, button_width, 38},
                      action == app->selected_action ? (SDL_Color){35, 77, 73, 255} : surface2,
                      action == app->selected_action ? green : outline);
                text(renderer, bx + 12, by + 12, name, 2, white);
            }
        }
    }
    if (app->page == 2 && app->spot_count)
    {
        fill(renderer, layout.next_button, (SDL_Color){39, 65, 91, 255});
        text(renderer, layout.next_button.x + 18, layout.next_button.y + 16, "NEXT SPOT", 2, white);
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

static void save_session(const app_t *app)
{
    const char *path = app->session_path[0] ? app->session_path : "trainer-session.json";
    FILE *file = fopen(path, "w"); if (!file) return;
    fprintf(file, "{\"schema\":\"pe-trainer-session/v1\",\"solution\":\"%s\",\"answered\":%d,\"best_answers\":%d,\"probability_loss\":%.17g,\"difficulty\":%d,\"events\":[", app->solution_path, app->answered, app->score, app->probability_loss, app->difficulty);
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
    snprintf(app.game_name, sizeof(app.game_name), "HOLDEM");
    snprintf(app.board_text, sizeof(app.board_text), "2c 7d Th Js Qc");
    snprintf(app.range_oop, sizeof(app.range_oop), "OOP  100%%");
    snprintf(app.range_ip, sizeof(app.range_ip), "IP   100%%");
    font_init();
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0) {
            puts("usage: poker-eval-trainer-gui [--solution FILE] [--labels CSV] [--actions a,b,c] [--session-json FILE] [--resume-session FILE]");
            return 0;
        }
        if (i + 1 >= argc) {
            fprintf(stderr, "missing value for %s\n", argv[i]);
            return 2;
        }
        if (strcmp(argv[i], "--solution") == 0) load_solution(&app, argv[++i]);
        else if (strcmp(argv[i], "--labels") == 0) load_labels(&app, argv[++i]);
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
    if (app.spot_count) next_spot(&app);
    while (app.running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) app.running = 0;
            else if (event.type == SDL_DROPFILE) { const char *path = event.drop.file; size_t length = strlen(path); if (length > 7u && strcmp(path + length - 7u, ".pe_sol") == 0) load_solution(&app, path); else load_labels(&app, path); if (app.spot_count) next_spot(&app); SDL_free(event.drop.file); }
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_n) next_spot(&app);
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_s) save_session(&app);
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_1) app.page = 0;
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_2) app.page = 1;
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_3) app.page = 2;
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_TAB) app.page = (app.page + 1) % 3;
            else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                mouse_x = event.button.x; mouse_y = event.button.y;
                int output_width = 0, output_height = 0;
                SDL_GetRendererOutputSize(renderer, &output_width, &output_height);
                gui_layout_t layout = layout_for(output_width, output_height);
                if (inside(layout.setup_tab, mouse_x, mouse_y)) app.page = 0;
                else if (inside(layout.solve_tab, mouse_x, mouse_y)) app.page = 1;
                else if (inside(layout.explore_tab, mouse_x, mouse_y)) app.page = 2;
                else if (app.page == 0 && mouse_x >= 786 && mouse_x < output_width - 28 &&
                         mouse_y >= output_height - 154 && mouse_y < output_height - 100)
                    app.page = 1;
                else if (app.page == 1 && mouse_x >= 1050 && mouse_x < output_width - 52 &&
                         mouse_y >= 586 && mouse_y < 650)
                    app.page = 2;
                else if (app.page == 2 && app.spot_count && mouse_x >= 264 && mouse_x < 1000 && mouse_y >= 338 && mouse_y < 338 + (int)((app.spot_count < 8 ? app.spot_count : 8) * 52u)) {
                    size_t picked = (size_t)((mouse_y - 338) / 52);
                    if (picked < app.spot_count) {
                        app.selected_spot = picked;
                        app.selected_key = app.spots[picked].key;
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
                         mouse_x < output_width - 52 && mouse_y >= 506 && mouse_y < 582) {
                    int button_width = (output_width - 1030 - 76) / 2;
                    int column = (mouse_x - 1054) / (button_width + 12);
                    int row = (mouse_y - 506) / 48;
                    int action = row * 2 + column;
                    int local_x = mouse_x - (1054 + column * (button_width + 12));
                    int local_y = mouse_y - (506 + row * 48);
                    size_t selected = app.selected_spot < app.spot_count ? app.selected_spot : 0;
                    if (column >= 0 && column < 2 && row >= 0 && row < 2 &&
                        local_x >= 0 && local_x < button_width && local_y >= 0 && local_y < 38 &&
                        action < app.spots[selected].actions) {
                        app.current = selected;
                        app.has_current = 1;
                        app.answered_current = 0;
                        answer(&app, action);
                    }
                }
            }
        }
        render_solver(renderer, &app); SDL_Delay(16);
    }
    save_session(&app); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit(); free(app.spots); free(app.labels); free(app.events); return 0;
}
