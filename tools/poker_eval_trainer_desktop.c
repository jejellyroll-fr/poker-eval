/*
 * poker_eval_trainer_desktop.c
 *
 * Native macOS trainer written in C. Cocoa is reached through the Objective-C
 * runtime C ABI; the source itself is compiled as C and contains no Swift or
 * Objective-C syntax.
 */
#include <objc/message.h>
#include <objc/runtime.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void *pe_id;
typedef void *pe_class;
typedef void *pe_sel;
typedef struct { double x; double y; } pe_point_t;
typedef struct { double width; double height; } pe_size_t;
typedef struct { pe_point_t origin; pe_size_t size; } pe_rect_t;

enum { PE_MODAL_OK = 1, PE_WINDOW_STYLE = 15, PE_BACKING_BUFFERED = 2 };

typedef struct {
    uint64_t key;
    int actions;
    double probability[256];
} pe_spot_t;

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
} pe_label_t;

typedef struct {
    uint64_t key;
    int selected;
    int best;
    double selected_probability;
    double best_probability;
} pe_event_t;

typedef struct {
    pe_spot_t *spots;
    size_t spot_count;
    pe_label_t *labels;
    size_t label_count;
    pe_event_t *events;
    size_t event_count;
    size_t event_capacity;
    size_t current;
    int has_current;
    int current_answered;
    unsigned random_state;
    int score;
    int answered;
    int streak;
    int difficulty;
    double probability_loss;
    char solution_path[1024];
    char labels_path[1024];
    pe_id window;
    pe_id solution_status;
    pe_id labels_status;
    pe_id spot_text;
    pe_id context_text;
    pe_id feedback_text;
    pe_id score_text;
    pe_id action_buttons[256];
    pe_id controller;
} pe_trainer_state_t;

static pe_trainer_state_t *g_state;

static pe_sel sel(const char *name) { return sel_registerName(name); }

static pe_id msg0(pe_id object, const char *name)
{
    return ((pe_id (*)(pe_id, pe_sel))objc_msgSend)(object, sel(name));
}

static pe_id msg1_id(pe_id object, const char *name, pe_id argument)
{
    return ((pe_id (*)(pe_id, pe_sel, pe_id))objc_msgSend)(object, sel(name), argument);
}

static pe_id msg_class0(const char *class_name, const char *name)
{
    return msg0((pe_id)objc_getClass(class_name), name);
}

static pe_id msg_class1_id(const char *class_name, const char *name, pe_id argument)
{
    return msg1_id((pe_id)objc_getClass(class_name), name, argument);
}

static pe_id msg_class_string(pe_id klass, const char *name, pe_id string,
                              pe_id target, pe_sel action)
{
    return ((pe_id (*)(pe_id, pe_sel, pe_id, pe_id, pe_sel))objc_msgSend)(
        klass, sel(name), string, target, action);
}

static void msg_void0(pe_id object, const char *name)
{
    ((void (*)(pe_id, pe_sel))objc_msgSend)(object, sel(name));
}

static void msg_void1_id(pe_id object, const char *name, pe_id argument)
{
    ((void (*)(pe_id, pe_sel, pe_id))objc_msgSend)(object, sel(name), argument);
}

static void msg_void1_bool(pe_id object, const char *name, int argument)
{
    ((void (*)(pe_id, pe_sel, int))objc_msgSend)(object, sel(name), argument);
}

static void msg_void1_int(pe_id object, const char *name, int argument)
{
    ((void (*)(pe_id, pe_sel, int))objc_msgSend)(object, sel(name), argument);
}

static void msg_void1_rect(pe_id object, const char *name, pe_rect_t argument)
{
    ((void (*)(pe_id, pe_sel, pe_rect_t))objc_msgSend)(object, sel(name), argument);
}

static pe_id msg_id1_rect(pe_id object, const char *name, pe_rect_t argument)
{
    return ((pe_id (*)(pe_id, pe_sel, pe_rect_t))objc_msgSend)(object, sel(name), argument);
}

static pe_id ns_string(const char *value)
{
    pe_id string_class = (pe_id)objc_getClass("NSString");
    return ((pe_id (*)(pe_id, pe_sel, const char *))objc_msgSend)(
        string_class, sel("stringWithUTF8String:"), value);
}

static const char *ns_utf8(pe_id string)
{
    if (!string) return NULL;
    return ((const char *(*)(pe_id, pe_sel))objc_msgSend)(string, sel("UTF8String"));
}

static pe_rect_t rect_make(double x, double y, double width, double height)
{
    pe_rect_t rect = {{x, y}, {width, height}};
    return rect;
}

static uint16_t read_u16(const unsigned char *data, size_t offset)
{
    return (uint16_t)data[offset] | ((uint16_t)data[offset + 1u] << 8);
}

static uint32_t read_u32(const unsigned char *data, size_t offset)
{
    return (uint32_t)data[offset] | ((uint32_t)data[offset + 1u] << 8) |
           ((uint32_t)data[offset + 2u] << 16) | ((uint32_t)data[offset + 3u] << 24);
}

static uint64_t read_u64(const unsigned char *data, size_t offset)
{
    uint64_t result = 0;
    for (size_t index = 0u; index < 8u; ++index)
        result |= (uint64_t)data[offset + index] << (index * 8u);
    return result;
}

static uint64_t parse_key(const char *text)
{
    char *end = NULL;
    return strtoull(text, &end, 0);
}

static int load_solution(pe_trainer_state_t *state, const char *path)
{
    FILE *file = fopen(path, "rb");
    unsigned char header[32];
    uint64_t count;
    unsigned char *data;
    long file_size;
    size_t offset = 32u;
    pe_spot_t *spots;
    if (!file) return -1;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return -1; }
    file_size = ftell(file);
    if (file_size < 32) { fclose(file); return -1; }
    rewind(file);
    data = (unsigned char *)malloc((size_t)file_size);
    if (!data || fread(data, 1u, (size_t)file_size, file) != (size_t)file_size) {
        free(data); fclose(file); return -1;
    }
    fclose(file);
    memcpy(header, data, sizeof(header));
    if (memcmp(header, "PESOL001", 8u) != 0 || read_u32(data, 8u) != 1u) {
        free(data); return -1;
    }
    count = read_u64(data, 16u);
    if (count > SIZE_MAX / sizeof(*spots)) { free(data); return -1; }
    spots = (pe_spot_t *)calloc((size_t)count, sizeof(*spots));
    if (!spots && count != 0u) { free(data); return -1; }
    for (uint64_t index = 0u; index < count; ++index) {
        uint32_t actions;
        if (offset + 12u > (size_t)file_size) { free(spots); free(data); return -1; }
        spots[index].key = read_u64(data, offset);
        actions = read_u32(data, offset + 8u);
        offset += 12u;
        if (actions == 0u || actions > 256u || actions > ((size_t)file_size - offset) / 2u) {
            free(spots); free(data); return -1;
        }
        spots[index].actions = (int)actions;
        for (uint32_t action = 0u; action < actions; ++action)
            spots[index].probability[action] = (double)read_u16(data, offset + action * 2u) / 65535.0;
        offset += (size_t)actions * 2u;
    }
    free(data);
    free(state->spots);
    state->spots = spots;
    state->spot_count = (size_t)count;
    snprintf(state->solution_path, sizeof(state->solution_path), "%s", path);
    return 0;
}

static void trim(char *text)
{
    size_t length = strlen(text);
    char *start = text;
    while (*start == ' ' || *start == '\t') ++start;
    if (start != text) memmove(text, start, strlen(start) + 1u);
    length = strlen(text);
    while (length > 0u && (text[length - 1u] == ' ' || text[length - 1u] == '\t')) text[--length] = '\0';
}

static int load_labels(pe_trainer_state_t *state, const char *path)
{
    FILE *file = fopen(path, "r");
    pe_label_t *labels = NULL;
    size_t used = 0u, capacity = 0u;
    char line[512];
    if (!file) return -1;
    while (fgets(line, sizeof(line), file)) {
        char *fields[10] = {0};
        char *field;
        int count = 0;
        trim(line);
        field = strtok(line, ",");
        while (field && count < 10) { fields[count++] = field; field = strtok(NULL, ","); }
        if (count < 3 || strcmp(fields[0], "key") == 0) continue;
        if (used == capacity) {
            size_t next = capacity ? capacity * 2u : 32u;
            pe_label_t *grown = (pe_label_t *)realloc(labels, next * sizeof(*grown));
            if (!grown) { free(labels); fclose(file); return -1; }
            labels = grown; capacity = next;
        }
        memset(&labels[used], 0, sizeof(labels[used]));
        labels[used].key = parse_key(fields[0]);
        if (count >= 9) {
            snprintf(labels[used].street, sizeof(labels[used].street), "%s", fields[1]);
            snprintf(labels[used].board, sizeof(labels[used].board), "%s", fields[2]);
            snprintf(labels[used].runout, sizeof(labels[used].runout), "%s", fields[3]);
            snprintf(labels[used].position, sizeof(labels[used].position), "%s", fields[4]);
            labels[used].pot = strtod(fields[5], NULL); labels[used].has_pot = 1;
            labels[used].action = atoi(fields[6]); snprintf(labels[used].label, sizeof(labels[used].label), "%s", fields[7]);
        } else if (count >= 5) {
            snprintf(labels[used].street, sizeof(labels[used].street), "%s", fields[1]);
            snprintf(labels[used].board, sizeof(labels[used].board), "%s", fields[2]);
            labels[used].action = atoi(fields[3]); snprintf(labels[used].label, sizeof(labels[used].label), "%s", fields[4]);
        } else {
            labels[used].action = atoi(fields[1]); snprintf(labels[used].label, sizeof(labels[used].label), "%s", fields[2]);
        }
        ++used;
    }
    fclose(file);
    free(state->labels); state->labels = labels; state->label_count = used;
    snprintf(state->labels_path, sizeof(state->labels_path), "%s", path);
    return 0;
}

static const pe_label_t *find_label(const pe_trainer_state_t *state, uint64_t key, int action)
{
    for (size_t index = 0u; index < state->label_count; ++index)
        if (state->labels[index].key == key && state->labels[index].action == action) return &state->labels[index];
    return NULL;
}

static const pe_label_t *find_metadata(const pe_trainer_state_t *state, uint64_t key)
{
    for (size_t index = 0u; index < state->label_count; ++index)
        if (state->labels[index].key == key) return &state->labels[index];
    return NULL;
}

static void set_text(pe_id field, const char *text)
{
    if (field) msg_void1_id(field, "setStringValue:", ns_string(text));
}

static void show_error(const char *message)
{
    pe_id alert = msg_class0("NSAlert", "new");
    msg_void1_id(alert, "setMessageText:", ns_string("poker-eval Trainer"));
    msg_void1_id(alert, "setInformativeText:", ns_string(message));
    msg_void0(alert, "runModal");
}

static void update_score(pe_trainer_state_t *state)
{
    char text[128];
    snprintf(text, sizeof(text), "Score : %d / %d   ·   Difficulté : %d", state->score, state->answered, state->difficulty);
    set_text(state->score_text, text);
}

static void render_current(pe_trainer_state_t *state)
{
    pe_spot_t *spot;
    char text[256];
    if (!state->has_current || state->current >= state->spot_count) return;
    spot = &state->spots[state->current];
    snprintf(text, sizeof(text), "Infoset 0x%016llx", (unsigned long long)spot->key);
    set_text(state->spot_text, text);
    {
        const pe_label_t *meta = find_metadata(state, spot->key);
        if (meta) {
            snprintf(text, sizeof(text), "%s  ·  %s  ·  %s  ·  %s%s%s",
                     meta->street, meta->board, meta->runout, meta->position,
                     meta->has_pot ? "  ·  pot " : "", meta->has_pot ? "" : "");
            if (meta->has_pot) {
                char pot[32]; snprintf(pot, sizeof(pot), "%.2f", meta->pot);
                strncat(text, pot, sizeof(text) - strlen(text) - 1u);
            }
            set_text(state->context_text, text);
        } else set_text(state->context_text, "Métadonnées de spot non fournies");
    }
    if (!state->current_answered) set_text(state->feedback_text, "Choisissez l'action que vous joueriez.");
    for (int action = 0; action < 256; ++action) {
        if (!state->action_buttons[action]) continue;
        if (action < spot->actions) {
            const pe_label_t *label = find_label(state, spot->key, action);
            snprintf(text, sizeof(text), "%s  ·  %.1f%%", label ? label->label : "Action", spot->probability[action] * 100.0);
            msg_void1_id(state->action_buttons[action], "setTitle:", ns_string(text));
            msg_void1_bool(state->action_buttons[action], "setHidden:", 0);
            msg_void1_bool(state->action_buttons[action], "setEnabled:", !state->current_answered);
            msg_void1_rect(state->action_buttons[action], "setFrame:", rect_make(24.0 + (action % 5) * 135.0,
                235.0 - (action / 5) * 42.0, 125.0, 32.0));
        } else msg_void1_bool(state->action_buttons[action], "setHidden:", 1);
    }
    update_score(state);
}

static void next_spot(pe_trainer_state_t *state)
{
    size_t candidates[4096];
    size_t count = 0u;
    if (state->spot_count == 0u) { set_text(state->spot_text, "Ouvrez un fichier .pe_sol pour commencer"); return; }
    for (size_t index = 0u; index < state->spot_count && count < 4096u; ++index) {
        double best = 0.0;
        for (int action = 0; action < state->spots[index].actions; ++action)
            if (state->spots[index].probability[action] > best) best = state->spots[index].probability[action];
        if (state->difficulty == 1 || best < 0.8) candidates[count++] = index;
    }
    if (count == 0u) return;
    state->random_state = state->random_state * 1664525u + 1013904223u;
    state->current = candidates[state->random_state % count];
    state->has_current = 1; state->current_answered = 0;
    render_current(state);
}

static void record_event(pe_trainer_state_t *state, pe_event_t event)
{
    if (state->event_count == state->event_capacity) {
        size_t next = state->event_capacity ? state->event_capacity * 2u : 32u;
        pe_event_t *grown = (pe_event_t *)realloc(state->events, next * sizeof(*grown));
        if (!grown) return;
        state->events = grown; state->event_capacity = next;
    }
    state->events[state->event_count++] = event;
}

static void answer_action(pe_trainer_state_t *state, int selected)
{
    pe_spot_t *spot;
    int best = 0;
    char text[256];
    if (!state->has_current || state->current_answered) return;
    spot = &state->spots[state->current];
    for (int action = 1; action < spot->actions; ++action)
        if (spot->probability[action] > spot->probability[best]) best = action;
    state->answered++; state->current_answered = 1;
    record_event(state, (pe_event_t){spot->key, selected, best, spot->probability[selected], spot->probability[best]});
    if (selected == best) {
        state->score++; state->streak++;
        snprintf(text, sizeof(text), "Correct · meilleure action : %.1f%%", spot->probability[best] * 100.0);
    } else {
        const pe_label_t *label;
        state->streak = 0; state->probability_loss += spot->probability[best] - spot->probability[selected];
        label = find_label(state, spot->key, best);
        snprintf(text, sizeof(text), "À revoir : %s · %.1f%%", label ? label->label : "Action", spot->probability[best] * 100.0);
    }
    if (state->streak >= 3) state->difficulty = state->difficulty < 5 ? state->difficulty + 1 : 5;
    if (state->streak == 0) state->difficulty = state->difficulty > 1 ? state->difficulty - 1 : 1;
    set_text(state->feedback_text, text);
    render_current(state);
}

static void choose_file(pe_trainer_state_t *state, int labels)
{
    pe_id panel = msg_class0("NSOpenPanel", "openPanel");
    pe_id url;
    const char *path;
    if (((int (*)(pe_id, pe_sel))objc_msgSend)(panel, sel("runModal")) != PE_MODAL_OK) return;
    url = msg0(panel, "URL");
    path = ns_utf8(msg0(url, "path"));
    if (!path) return;
    if ((labels ? load_labels(state, path) : load_solution(state, path)) != 0) {
        show_error(labels ? "Impossible de lire les labels CSV." : "Impossible de lire le fichier .pe_sol."); return;
    }
    if (labels) {
        char text[256]; snprintf(text, sizeof(text), "Labels : %s · %zu actions", path, state->label_count); set_text(state->labels_status, text);
        render_current(state);
    } else {
        char text[256]; snprintf(text, sizeof(text), "Solution : %s · %zu infosets", path, state->spot_count); set_text(state->solution_status, text);
        state->score = 0; state->answered = 0; state->streak = 0; state->difficulty = 1; state->event_count = 0u; state->probability_loss = 0.0;
        next_spot(state);
    }
}

static void export_session(pe_trainer_state_t *state)
{
    pe_id panel = msg_class0("NSSavePanel", "savePanel");
    pe_id url;
    const char *path;
    FILE *file;
    if (!state->solution_path[0]) { show_error("Chargez d'abord une solution."); return; }
    msg_void1_id(panel, "setNameFieldStringValue:", ns_string("trainer-session.json"));
    if (((int (*)(pe_id, pe_sel))objc_msgSend)(panel, sel("runModal")) != PE_MODAL_OK) return;
    url = msg0(panel, "URL"); path = ns_utf8(msg0(url, "path")); if (!path) return;
    file = fopen(path, "w"); if (!file) { show_error("Impossible d'écrire la session JSON."); return; }
    fprintf(file, "{\"schema\":\"pe-trainer-session/v1\",\"solution\":\"%s\",\"answered\":%d,\"best_answers\":%d,\"probability_loss\":%.17g,\"events\":[",
            state->solution_path, state->answered, state->score, state->probability_loss);
    for (size_t index = 0u; index < state->event_count; ++index) {
        pe_event_t *event = &state->events[index];
        if (index) fputc(',', file);
        fprintf(file, "{\"key\":\"0x%016llx\",\"selected\":%d,\"best\":%d,\"selected_probability\":%.17g,\"best_probability\":%.17g}",
                (unsigned long long)event->key, event->selected, event->best,
                event->selected_probability, event->best_probability);
    }
    fputs("]}\n", file); fclose(file);
}

static void on_open_solution(pe_id self, pe_sel selector, pe_id sender) { (void)self; (void)selector; (void)sender; choose_file(g_state, 0); }
static void on_open_labels(pe_id self, pe_sel selector, pe_id sender) { (void)self; (void)selector; (void)sender; choose_file(g_state, 1); }
static void on_next(pe_id self, pe_sel selector, pe_id sender) { (void)self; (void)selector; (void)sender; next_spot(g_state); }
static void on_export(pe_id self, pe_sel selector, pe_id sender) { (void)self; (void)selector; (void)sender; export_session(g_state); }
static void on_action(pe_id self, pe_sel selector, pe_id sender)
{
    int tag = ((int (*)(pe_id, pe_sel))objc_msgSend)(sender, sel("tag"));
    (void)self; (void)selector; answer_action(g_state, tag);
}

static pe_id make_label(const char *text, pe_rect_t frame, double size)
{
    pe_id field = ((pe_id (*)(pe_id, pe_sel, pe_id))objc_msgSend)(
        (pe_id)objc_getClass("NSTextField"), sel("labelWithString:"), ns_string(text));
    msg_void1_rect(field, "setFrame:", frame);
    msg_void1_id(field, "setFont:", ((pe_id (*)(pe_id, pe_sel, double))objc_msgSend)(
        (pe_id)objc_getClass("NSFont"), sel("systemFontOfSize:"), size));
    return field;
}

static pe_id make_button(pe_id controller, const char *title, pe_sel action, pe_rect_t frame)
{
    pe_id button = msg_class_string((pe_id)objc_getClass("NSButton"), "buttonWithTitle:target:action:",
                                    ns_string(title), controller, action);
    msg_void1_rect(button, "setFrame:", frame);
    return button;
}

static void build_window(pe_trainer_state_t *state)
{
    pe_id app = msg_class0("NSApplication", "sharedApplication");
    pe_id window_class = (pe_id)objc_getClass("NSWindow");
    pe_id view;
    Class controller_class = objc_allocateClassPair((Class)objc_getClass("NSObject"), "PokerEvalTrainerController", 0);
    class_addMethod(controller_class, sel("openSolution:"), (IMP)on_open_solution, "v@:@");
    class_addMethod(controller_class, sel("openLabels:"), (IMP)on_open_labels, "v@:@");
    class_addMethod(controller_class, sel("nextSpot:"), (IMP)on_next, "v@:@");
    class_addMethod(controller_class, sel("exportSession:"), (IMP)on_export, "v@:@");
    class_addMethod(controller_class, sel("actionPressed:"), (IMP)on_action, "v@:@");
    objc_registerClassPair(controller_class);
    state->controller = msg0((pe_id)controller_class, "new");
    state->window = msg0(window_class, "alloc");
    state->window = ((pe_id (*)(pe_id, pe_sel, pe_rect_t, int, int, int))objc_msgSend)(
        state->window, sel("initWithContentRect:styleMask:backing:defer:"), rect_make(0, 0, 720, 500), PE_WINDOW_STYLE, PE_BACKING_BUFFERED, 0);
    msg_void1_id(state->window, "setTitle:", ns_string("poker-eval Trainer"));
    msg_void0(state->window, "center");
    view = msg_id1_rect(msg_class0("NSView", "alloc"), "initWithFrame:", rect_make(0, 0, 720, 500));
    msg_void1_id(state->window, "setContentView:", view);

    msg_void1_id(view, "addSubview:", make_label("poker-eval Trainer", rect_make(24, 442, 650, 30), 26));
    msg_void1_id(view, "addSubview:", make_label("Play-vs-solution natif · vos fichiers restent sur cette machine", rect_make(24, 416, 650, 22), 13));
    msg_void1_id(view, "addSubview:", make_button(state->controller, "Ouvrir .pe_sol…", sel("openSolution:"), rect_make(24, 373, 150, 32)));
    msg_void1_id(view, "addSubview:", make_button(state->controller, "Ouvrir labels…", sel("openLabels:"), rect_make(183, 373, 150, 32)));
    msg_void1_id(view, "addSubview:", make_button(state->controller, "Exporter session…", sel("exportSession:"), rect_make(342, 373, 160, 32)));
    state->solution_status = make_label("Aucune solution chargée", rect_make(24, 344, 650, 20), 12);
    state->labels_status = make_label("Labels : aucun", rect_make(24, 323, 650, 20), 12);
    state->spot_text = make_label("Ouvrez un fichier .pe_sol pour commencer", rect_make(24, 278, 650, 30), 19);
    state->context_text = make_label("", rect_make(24, 252, 650, 20), 13);
    state->feedback_text = make_label("", rect_make(24, 105, 650, 26), 14);
    state->score_text = make_label("Score : 0 / 0   ·   Difficulté : 1", rect_make(24, 75, 650, 22), 13);
    msg_void1_id(view, "addSubview:", state->solution_status);
    msg_void1_id(view, "addSubview:", state->labels_status);
    msg_void1_id(view, "addSubview:", state->spot_text);
    msg_void1_id(view, "addSubview:", state->context_text);
    msg_void1_id(view, "addSubview:", state->feedback_text);
    msg_void1_id(view, "addSubview:", state->score_text);
    for (int action = 0; action < 256; ++action) {
        state->action_buttons[action] = make_button(state->controller, "Action", sel("actionPressed:"), rect_make(24, 235, 125, 32));
        msg_void1_int(state->action_buttons[action], "setTag:", action);
        msg_void1_bool(state->action_buttons[action], "setHidden:", 1);
        msg_void1_id(view, "addSubview:", state->action_buttons[action]);
    }
    msg_void1_id(view, "addSubview:", make_button(state->controller, "Spot suivant", sel("nextSpot:"), rect_make(24, 32, 130, 32)));
    msg_void1_int(app, "setActivationPolicy:", 0);
    msg_void1_id(state->window, "makeKeyAndOrderFront:", NULL);
}

int main(int argc, char **argv)
{
    pe_id app;
    pe_trainer_state_t state;
    memset(&state, 0, sizeof(state));
    state.random_state = 1u; state.difficulty = 1; g_state = &state;
    app = msg_class0("NSApplication", "sharedApplication");
    build_window(&state);
    for (int index = 1; index + 1 < argc; ++index) {
        if (strcmp(argv[index], "--solution") == 0) load_solution(&state, argv[++index]);
        else if (strcmp(argv[index], "--labels") == 0) load_labels(&state, argv[++index]);
    }
    if (state.spot_count > 0u) next_spot(&state);
    if (state.solution_path[0]) { char text[1200]; snprintf(text, sizeof(text), "Solution : %s · %zu infosets", state.solution_path, state.spot_count); set_text(state.solution_status, text); }
    if (state.labels_path[0]) { char text[1200]; snprintf(text, sizeof(text), "Labels : %s · %zu actions", state.labels_path, state.label_count); set_text(state.labels_status, text); }
    msg_void1_bool(app, "activateIgnoringOtherApps:", 1);
    msg_void0(app, "run");
    free(state.spots); free(state.labels); free(state.events);
    return 0;
}
