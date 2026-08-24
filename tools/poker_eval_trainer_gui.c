/* Cross-platform poker-eval trainer GUI: pure C + SDL2. */
#include <SDL2/SDL.h>

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WINDOW_WIDTH 1180
#define WINDOW_HEIGHT 760
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
    unsigned random_state;
    int score;
    int answered;
    int streak;
    int difficulty;
    double probability_loss;
    char solution_path[1024];
    char labels_path[1024];
    char action_names[MAX_ACTIONS][32];
    char feedback[256];
    int running;
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
    if (!count) return; app->random_state = app->random_state * 1664525u + 1013904223u; app->current = candidates[app->random_state % count]; app->has_current = 1; app->answered_current = 0;
}

static void answer(app_t *app, int selected)
{
    spot_t *spot; int best = 0;
    if (!app->has_current || app->answered_current) return; spot = &app->spots[app->current];
    for (int action = 1; action < spot->actions; ++action) if (spot->probability[action] > spot->probability[best]) best = action;
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

static void render(SDL_Renderer *renderer, const app_t *app)
{
    SDL_Color white = {235,239,244,255}, muted = {155,165,178,255}, blue = {86,147,255,255}, green = {74,190,125,255}, orange = {242,164,73,255};
    SDL_SetRenderDrawColor(renderer, 18, 23, 30, 255); SDL_RenderClear(renderer);
    fill(renderer, (rect_t){28,24,1124,80}, (SDL_Color){28,36,46,255}); text(renderer, 54, 42, "POKER-EVAL TRAINER", 4, white); text(renderer, 55, 67, "CHOISIS UNE ACTION, PUIS COMPARE-LA A LA STRATEGIE", 2, muted);
    fill(renderer, (rect_t){28,124,1124,64}, (SDL_Color){27,34,43,255});
    text(renderer, 52, 140, app->solution_path[0] ? "SOLUTION CHARGEE" : "DEPOSE UN FICHIER .PE_SOL", 2, app->solution_path[0] ? green : orange);
    text(renderer, 300, 140, app->solution_path[0] ? basename_of(app->solution_path) : "puis un CSV de labels ou --actions", 2, white);
    text(renderer, 52, 164, app->labels_path[0] ? "LABELS CHARGES" : "LABELS MANQUANTS", 2, app->labels_path[0] ? green : orange);
    text(renderer, 300, 164, app->labels_path[0] ? basename_of(app->labels_path) : "glisse-depose un fichier CSV", 2, muted);
    fill(renderer, (rect_t){28,212,1124,280}, (SDL_Color){29,38,49,255}); border(renderer, (rect_t){28,212,1124,280}, (SDL_Color){57,73,91,255});
    if (app->has_current && app->current < app->spot_count) {
        const spot_t *spot = &app->spots[app->current]; const label_t *meta = metadata_for(app, spot->key); char line[512];
        if (meta && meta->street[0]) snprintf(line, sizeof(line), "%s  /  %s  /  %s", meta->street, meta->position[0] ? meta->position : "position inconnue", meta->board[0] ? meta->board : "board non renseigne");
        else snprintf(line, sizeof(line), "CONTEXTE MANQUANT  -  charge le CSV labels pour voir street / board / position");
        text(renderer, 54, 238, line, 2, meta && meta->street[0] ? white : orange);
        if (meta && meta->has_pot) { snprintf(line, sizeof(line), "POT : %.2f", meta->pot); text(renderer, 900, 238, line, 2, muted); }
        snprintf(line, sizeof(line), "SPOT %zu / %zu", app->current + 1u, app->spot_count); text(renderer, 54, 276, line, 3, blue);
        snprintf(line, sizeof(line), "Question : quelle action joues-tu ici ?"); text(renderer, 54, 318, line, 3, white);
        if (app->labels_path[0] == '\0') text(renderer, 54, 354, "Utilise --actions fold,call,raise ou depose un CSV labels.", 2, orange);
        for (int action = 0; action < spot->actions && action < 8; ++action) {
            int column = action % 4, row = action / 4; rect_t button = {54 + column * 245, 382 + row * 48, 225, 38};
            const label_t *label = label_for(app, spot->key, action); char button_text[128];
            const char *name = label ? label->label :
                (app->action_names[action][0] ? app->action_names[action] : "OPTION");
            snprintf(button_text, sizeof(button_text), "%s  %.1f%%", name, spot->probability[action] * 100.0);
            fill(renderer, button, app->answered_current ? (SDL_Color){49,60,72,255} : (SDL_Color){53,80,112,255}); text(renderer, button.x + 14, button.y + 12, button_text, 2, white);
        }
    } else text(renderer, 54, 270, "DEPOSE TON .PE_SOL ICI POUR COMMENCER", 3, white);
    fill(renderer, (rect_t){28,516,1124,112}, (SDL_Color){27,34,43,255});
    text(renderer, 54, 540, app->answered_current ? app->feedback : "ETAPE 1 : CHOISIS UNE ACTION CI-DESSUS", 3, app->answered_current ? (app->streak ? green : orange) : white);
    { char score[128]; snprintf(score, sizeof(score), "SCORE : %d / %d     DIFFICULTE : %d / 5     SERIE : %d", app->score, app->answered, app->difficulty, app->streak); text(renderer, 54, 585, score, 2, muted); }
    fill(renderer, (rect_t){28,654,230,54}, (SDL_Color){53,80,112,255}); text(renderer, 57, 672, "SPOT SUIVANT (N)", 2, white);
    fill(renderer, (rect_t){278,654,440,54}, (SDL_Color){31,42,53,255}); text(renderer, 300, 672, "GLISSE .PE_SOL OU .CSV SUR LA FENETRE", 2, muted);
    SDL_RenderPresent(renderer);
}

static void save_session(const app_t *app)
{
    char path[1200]; snprintf(path, sizeof(path), "trainer-session.json"); FILE *file = fopen(path, "w"); if (!file) return;
    fprintf(file, "{\"schema\":\"pe-trainer-session/v1\",\"solution\":\"%s\",\"answered\":%d,\"best_answers\":%d,\"probability_loss\":%.17g,\"events\":[", app->solution_path, app->answered, app->score, app->probability_loss);
    for (size_t i = 0; i < app->event_count; ++i) { if (i) fputc(',', file); fprintf(file, "{\"key\":\"0x%016llx\",\"selected\":%d,\"best\":%d}", (unsigned long long)app->events[i].key, app->events[i].selected, app->events[i].best); }
    fputs("]}\n", file); fclose(file);
}

int main(int argc, char **argv)
{
    SDL_Window *window; SDL_Renderer *renderer; SDL_Event event; app_t app; int mouse_x, mouse_y;
    memset(&app, 0, sizeof(app)); app.random_state = 1u; app.difficulty = 1; app.running = 1; font_init();
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0) {
            puts("usage: poker-eval-trainer-gui [--solution FILE] [--labels CSV] [--actions a,b,c]");
            return 0;
        }
        if (i + 1 >= argc) {
            fprintf(stderr, "missing value for %s\n", argv[i]);
            return 2;
        }
        if (strcmp(argv[i], "--solution") == 0) load_solution(&app, argv[++i]);
        else if (strcmp(argv[i], "--labels") == 0) load_labels(&app, argv[++i]);
        else if (strcmp(argv[i], "--actions") == 0) load_action_names(&app, argv[++i]);
        else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            return 2;
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
            else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                mouse_x = event.button.x; mouse_y = event.button.y;
                if (inside((rect_t){28,654,230,54}, mouse_x, mouse_y)) next_spot(&app);
                if (app.has_current && app.current < app.spot_count) for (int action = 0; action < app.spots[app.current].actions && action < 8; ++action) if (inside((rect_t){54 + (action % 4) * 245, 382 + (action / 4) * 48, 225, 38}, mouse_x, mouse_y)) answer(&app, action);
            }
        }
        render(renderer, &app); SDL_Delay(16);
    }
    save_session(&app); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit(); free(app.spots); free(app.labels); free(app.events); return 0;
}
