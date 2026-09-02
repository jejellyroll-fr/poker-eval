#include <poker_eval/economics/hrc_import.h>

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../solver/domain/finite_double.h"

typedef struct {
    const char *text;
    size_t length;
    size_t position;
} json_reader_t;

/* Exact integer check without the == / != operators so -Wfloat-equal stays
   satisfied. Ordered comparisons keep NaN rejected, matching the previous
   "value != floor(value)" behaviour exactly. */
static int double_is_exact_integer(double value)
{
    double truncated = floor(value);
    return value >= truncated && value <= truncated;
}

static void set_error(json_reader_t *reader, pe_hrc_import_error_t *error,
                      const char *message)
{
    if (!error)
        return;
    error->offset = reader ? reader->position : 0;
    snprintf(error->message, sizeof(error->message), "%s", message);
}

static void skip_space(json_reader_t *reader)
{
    while (reader->position < reader->length &&
           isspace((unsigned char)reader->text[reader->position]))
        ++reader->position;
}

static int take(json_reader_t *reader, char expected)
{
    skip_space(reader);
    if (reader->position >= reader->length || reader->text[reader->position] != expected)
        return 0;
    ++reader->position;
    return 1;
}

static int parse_string(json_reader_t *reader, char **out,
                        pe_hrc_import_error_t *error)
{
    size_t start, length, out_length = 0;
    char *value;
    skip_space(reader);
    if (reader->position >= reader->length || reader->text[reader->position] != '"') {
        set_error(reader, error, "expected JSON string");
        return 0;
    }
    ++reader->position;
    start = reader->position;
    while (reader->position < reader->length) {
        char c = reader->text[reader->position++];
        if (c == '"')
            break;
        if ((unsigned char)c < 32) {
            set_error(reader, error, "control character in JSON string");
            return 0;
        }
        if (c == '\\') {
            if (reader->position >= reader->length) {
                set_error(reader, error, "unfinished JSON escape");
                return 0;
            }
            ++reader->position;
        }
    }
    if (reader->position == 0 || reader->text[reader->position - 1] != '"') {
        set_error(reader, error, "unterminated JSON string");
        return 0;
    }
    length = reader->position - start - 1;
    value = malloc(length + 1);
    if (!value) {
        set_error(reader, error, "out of memory");
        return 0;
    }
    for (size_t i = 0; i < length; ++i) {
        char c = reader->text[start + i];
        if (c == '\\' && i + 1 < length) {
            char escaped = reader->text[start + ++i];
            switch (escaped) {
            case '"': case '\\': case '/': value[out_length++] = escaped; break;
            case 'b': value[out_length++] = '\b'; break;
            case 'f': value[out_length++] = '\f'; break;
            case 'n': value[out_length++] = '\n'; break;
            case 'r': value[out_length++] = '\r'; break;
            case 't': value[out_length++] = '\t'; break;
            default:
                free(value);
                set_error(reader, error, "unsupported JSON escape");
                return 0;
            }
        } else {
            value[out_length++] = c;
        }
    }
    value[out_length] = '\0';
    *out = value;
    return 1;
}

static int parse_number(json_reader_t *reader, double *out,
                        pe_hrc_import_error_t *error)
{
    char stack_token[128];
    char *token = stack_token;
    char *end;
    size_t token_length = 0;
    size_t remaining;
    double value;
    skip_space(reader);
    remaining = reader->length - reader->position;
    while (token_length < remaining &&
           !isspace((unsigned char)reader->text[reader->position + token_length]) &&
           !strchr(",]}", reader->text[reader->position + token_length]))
        ++token_length;
    if (token_length == 0u) {
        set_error(reader, error, "expected finite JSON number");
        return 0;
    }
    if (token_length >= sizeof(stack_token)) {
        token = (char *)malloc(token_length + 1u);
        if (!token) {
            set_error(reader, error, "out of memory");
            return 0;
        }
    }
    for (size_t i = 0u; i < token_length; ++i)
        token[i] = reader->text[reader->position + i];
    token[token_length] = '\0';
    errno = 0;
    value = strtod(token, &end);
    if (end != token + token_length || errno == ERANGE ||
        !pe_finite_double(value)) {
        if (token != stack_token)
            free(token);
        set_error(reader, error, "expected finite JSON number");
        return 0;
    }
    reader->position += token_length;
    if (token != stack_token)
        free(token);
    *out = value;
    return 1;
}

static int parse_bool(json_reader_t *reader, int *out,
                      pe_hrc_import_error_t *error)
{
    skip_space(reader);
    if (reader->position + 4 <= reader->length &&
        strncmp(reader->text + reader->position, "true", 4) == 0) {
        reader->position += 4; *out = 1; return 1;
    }
    if (reader->position + 5 <= reader->length &&
        strncmp(reader->text + reader->position, "false", 5) == 0) {
        reader->position += 5; *out = 0; return 1;
    }
    set_error(reader, error, "expected JSON boolean");
    return 0;
}

static int skip_value(json_reader_t *reader, pe_hrc_import_error_t *error);

static int skip_array(json_reader_t *reader, pe_hrc_import_error_t *error)
{
    if (!take(reader, '[')) return 0;
    skip_space(reader);
    if (take(reader, ']')) return 1;
    for (;;) {
        if (!skip_value(reader, error)) return 0;
        if (take(reader, ']')) return 1;
        if (!take(reader, ',')) {
            set_error(reader, error, "expected comma in JSON array");
            return 0;
        }
    }
}

static int skip_object(json_reader_t *reader, pe_hrc_import_error_t *error)
{
    if (!take(reader, '{')) return 0;
    skip_space(reader);
    if (take(reader, '}')) return 1;
    for (;;) {
        char *key = NULL;
        if (!parse_string(reader, &key, error) || !take(reader, ':')) {
            free(key); return 0;
        }
        free(key);
        if (!skip_value(reader, error)) return 0;
        if (take(reader, '}')) return 1;
        if (!take(reader, ',')) {
            set_error(reader, error, "expected comma in JSON object");
            return 0;
        }
    }
}

static int skip_value(json_reader_t *reader, pe_hrc_import_error_t *error)
{
    char *string = NULL;
    skip_space(reader);
    if (reader->position >= reader->length) return 0;
    switch (reader->text[reader->position]) {
    case '{': return skip_object(reader, error);
    case '[': return skip_array(reader, error);
    case '"':
        if (!parse_string(reader, &string, error)) return 0;
        free(string); return 1;
    default:
        while (reader->position < reader->length &&
               !strchr(",]} \t\r\n", reader->text[reader->position]))
            ++reader->position;
        return 1;
    }
}

static int parse_variant(const char *name, enum_game_t *out)
{
    char lower[32]; size_t n = strnlen(name, sizeof(lower));
    if (n == 0 || n >= sizeof(lower)) return 0;
    for (size_t i = 0; i < n; ++i) lower[i] = (char)tolower((unsigned char)name[i]);
    lower[n] = '\0';
    if (strcmp(lower, "holdem") == 0 || strcmp(lower, "texas_holdem") == 0) *out = game_holdem;
    else if (strcmp(lower, "omaha") == 0 || strcmp(lower, "plo4") == 0) *out = game_omaha;
    else if (strcmp(lower, "plo5") == 0 || strcmp(lower, "omaha5") == 0) *out = game_omaha5;
    else if (strcmp(lower, "plo6") == 0 || strcmp(lower, "omaha6") == 0) *out = game_omaha6;
    else return 0;
    return 1;
}

static int same_ci(const char *left, const char *right)
{
    while (*left && *right) {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right)) return 0;
        ++left; ++right;
    }
    return *left == '\0' && *right == '\0';
}

static int parse_player(json_reader_t *reader, pe_hrc_import_t *out,
                        int player, pe_hrc_import_error_t *error)
{
    int have_range = 0;
    if (!take(reader, '{')) return 0;
    skip_space(reader);
    if (take(reader, '}')) { set_error(reader, error, "player object is empty"); return 0; }
    for (;;) {
        char *key = NULL;
        if (!parse_string(reader, &key, error) || !take(reader, ':')) { free(key); return 0; }
        if (strcmp(key, "stack") == 0) {
            if (!parse_number(reader, &out->pot_model.stacks[player], error)) { free(key); return 0; }
        } else if (strcmp(key, "ante") == 0) {
            if (!parse_number(reader, &out->pot_model.antes[player], error)) { free(key); return 0; }
        } else if (strcmp(key, "range") == 0) {
            free(out->range_text[player]); out->range_text[player] = NULL;
            if (!parse_string(reader, &out->range_text[player], error)) { free(key); return 0; }
            have_range = 1;
        } else if (strcmp(key, "bounty") == 0) {
            if (!parse_number(reader, &out->bounties[player], error)) { free(key); return 0; }
        } else if (!skip_value(reader, error)) { free(key); return 0; }
        free(key);
        if (take(reader, '}')) break;
        if (!take(reader, ',')) { set_error(reader, error, "expected comma in player"); return 0; }
    }
    if (!have_range) { set_error(reader, error, "player range is required"); return 0; }
    return 1;
}

static int parse_players(json_reader_t *reader, pe_hrc_import_t *out,
                         pe_hrc_import_error_t *error)
{
    int count = 0;
    if (!take(reader, '[')) return 0;
    skip_space(reader);
    if (take(reader, ']')) { set_error(reader, error, "players array is empty"); return 0; }
    for (;;) {
        if (count >= PE_HRC_MAX_PLAYERS || !parse_player(reader, out, count, error)) return 0;
        ++count;
        if (take(reader, ']')) break;
        if (!take(reader, ',')) { set_error(reader, error, "expected comma in players"); return 0; }
    }
    out->pot_model.player_count = count;
    return 1;
}

static int parse_action(json_reader_t *reader, pe_hrc_import_t *out,
                        int node, int action, pe_hrc_import_error_t *error)
{
    pe_hrc_action_t *parsed = &out->owned_nodes[node].actions[action];
    int have_child = 0;
    if (!take(reader, '{')) return 0;
    skip_space(reader);
    if (take(reader, '}')) { set_error(reader, error, "action object is empty"); return 0; }
    for (;;) {
        char *key = NULL;
        if (!parse_string(reader, &key, error) || !take(reader, ':')) { free(key); return 0; }
        if (strcmp(key, "label") == 0) {
            if (!parse_string(reader, &out->action_labels[node][action], error)) { free(key); return 0; }
            parsed->label = out->action_labels[node][action];
        } else if (strcmp(key, "amount") == 0 || strcmp(key, "increment") == 0) {
            if (!parse_number(reader, &parsed->amount, error)) { free(key); return 0; }
        } else if (strcmp(key, "child") == 0) {
            double child;
            if (!parse_number(reader, &child, error) || child < 0.0 ||
                child > (double)PE_HRC_MAX_NODES - 1.0 ||
                !double_is_exact_integer(child)) { free(key); return 0; }
            parsed->child_index = (int)child; have_child = 1;
        } else if (!skip_value(reader, error)) { free(key); return 0; }
        free(key);
        if (take(reader, '}')) break;
        if (!take(reader, ',')) { set_error(reader, error, "expected comma in action"); return 0; }
    }
    if (!have_child) { set_error(reader, error, "action child is required"); return 0; }
    return 1;
}

static int parse_node(json_reader_t *reader, pe_hrc_import_t *out, int node,
                      pe_hrc_import_error_t *error)
{
    int have_terminal = 0;
    if (!take(reader, '{')) return 0;
    skip_space(reader);
    if (take(reader, '}')) { set_error(reader, error, "node object is empty"); return 0; }
    for (;;) {
        char *key = NULL;
        if (!parse_string(reader, &key, error) || !take(reader, ':')) { free(key); return 0; }
        if (strcmp(key, "terminal") == 0) {
            if (!parse_bool(reader, &out->owned_nodes[node].terminal, error)) { free(key); return 0; }
            have_terminal = 1;
        } else if (strcmp(key, "player") == 0 || strcmp(key, "player_to_act") == 0) {
            double player;
            if (!parse_number(reader, &player, error) ||
                player < (double)INT_MIN || player > (double)INT_MAX ||
                !double_is_exact_integer(player)) { free(key); return 0; }
            out->owned_nodes[node].player_to_act = (int)player;
        } else if (strcmp(key, "actions") == 0) {
            if (!take(reader, '[')) { free(key); return 0; }
            skip_space(reader);
            if (!take(reader, ']')) {
                for (;;) {
                    unsigned action = out->owned_nodes[node].action_count;
                    if (action >= PE_HRC_MAX_ACTIONS || !parse_action(reader, out, node, (int)action, error)) { free(key); return 0; }
                    out->owned_nodes[node].action_count++;
                    if (take(reader, ']')) break;
                    if (!take(reader, ',')) { set_error(reader, error, "expected comma in actions"); free(key); return 0; }
                }
            }
        } else if (!skip_value(reader, error)) { free(key); return 0; }
        free(key);
        if (take(reader, '}')) break;
        if (!take(reader, ',')) { set_error(reader, error, "expected comma in node"); return 0; }
    }
    if (!have_terminal) out->owned_nodes[node].terminal = out->owned_nodes[node].action_count == 0;
    return 1;
}

static int parse_nodes(json_reader_t *reader, pe_hrc_import_t *out,
                       pe_hrc_import_error_t *error)
{
    size_t count = 0;
    if (!take(reader, '[')) return 0;
    skip_space(reader);
    if (take(reader, ']')) { set_error(reader, error, "nodes array is empty"); return 0; }
    for (;;) {
        if (count >= PE_HRC_MAX_NODES || !parse_node(reader, out, (int)count, error)) return 0;
        ++count;
        if (take(reader, ']')) break;
        if (!take(reader, ',')) { set_error(reader, error, "expected comma in nodes"); return 0; }
    }
    out->config.tree.node_count = count;
    return 1;
}

static int parse_payouts(json_reader_t *reader, pe_hrc_import_t *out,
                         pe_hrc_import_error_t *error)
{
    int count = 0;
    if (!take(reader, '[')) return 0;
    skip_space(reader);
    if (take(reader, ']')) { set_error(reader, error, "payouts array is empty"); return 0; }
    for (;;) {
        if (count >= ICM_MAX_PLAYERS || !parse_number(reader, &out->payouts[count], error)) return 0;
        ++count;
        if (take(reader, ']')) break;
        if (!take(reader, ',')) { set_error(reader, error, "expected comma in payouts"); return 0; }
    }
    out->num_payouts = count;
    return 1;
}

static int parse_document(json_reader_t *reader, pe_hrc_import_t *out,
                          pe_hrc_import_error_t *error)
{
    int have_nodes = 0, have_players = 0;
    enum_game_t variant = game_holdem;
    if (!take(reader, '{')) { set_error(reader, error, "JSON root must be an object"); return 0; }
    skip_space(reader);
    if (take(reader, '}')) { set_error(reader, error, "JSON root is empty"); return 0; }
    for (;;) {
        char *key = NULL;
        if (!parse_string(reader, &key, error) || !take(reader, ':')) { free(key); return 0; }
        if (strcmp(key, "variant") == 0) {
            char *name = NULL;
            if (!parse_string(reader, &name, error) || !parse_variant(name, &variant)) { free(name); free(key); set_error(reader, error, "unsupported variant"); return 0; }
            free(name);
        } else if (strcmp(key, "root") == 0) {
            double root;
            if (!parse_number(reader, &root, error) || root < 0.0 ||
                root > (double)PE_HRC_MAX_NODES - 1.0 ||
                !double_is_exact_integer(root)) { free(key); return 0; }
            out->config.tree.root_index = (int)root;
        } else if (strcmp(key, "players") == 0) {
            if (!parse_players(reader, out, error)) { free(key); return 0; }
            have_players = 1;
        } else if (strcmp(key, "nodes") == 0) {
            out->owned_nodes = calloc(PE_HRC_MAX_NODES, sizeof(*out->owned_nodes));
            if (!out->owned_nodes || !parse_nodes(reader, out, error)) { free(key); return 0; }
            have_nodes = 1;
        } else if (strcmp(key, "initial_pot") == 0) {
            if (!parse_number(reader, &out->pot_model.initial_pot, error)) { free(key); return 0; }
        } else if (strcmp(key, "iterations") == 0) {
            double value;
            if (!parse_number(reader, &value, error) || value < 0.0 ||
                value > (double)UINT_MAX || !double_is_exact_integer(value)) { free(key); return 0; }
            out->config.iterations = (unsigned)value;
        } else if (strcmp(key, "max_profiles") == 0) {
            double value;
            if (!parse_number(reader, &value, error) || value <= 0.0 ||
                value > (double)SIZE_MAX || !double_is_exact_integer(value)) { free(key); return 0; }
            out->config.max_profiles = (size_t)value;
        } else if (strcmp(key, "payouts") == 0) {
            if (!parse_payouts(reader, out, error)) { free(key); return 0; }
        } else if (strcmp(key, "bounty_multiplier") == 0) {
            if (!parse_number(reader, &out->bounty_multiplier, error)) { free(key); return 0; }
            out->has_bounty_multiplier = 1;
        } else if (!skip_value(reader, error)) { free(key); return 0; }
        free(key);
        if (take(reader, '}')) break;
        if (!take(reader, ',')) { set_error(reader, error, "expected comma in root object"); return 0; }
    }
    if (!have_players || !have_nodes) { set_error(reader, error, "players and nodes are required"); return 0; }
    out->config.tree.nodes = out->owned_nodes;
    out->config.tree.num_players = out->pot_model.player_count;
    out->config.terminal_value = NULL;
    out->config.user_data = NULL;
    if (!out->has_bounty_multiplier)
        out->bounty_multiplier = 1.0;
    else if (out->bounty_multiplier < 0.0) {
        set_error(reader, error, "bounty multiplier cannot be negative");
        return 0;
    }
    for (int p = 0; p < out->pot_model.player_count; ++p) {
        StdDeck_CardMask empty;
        StdDeck_CardMask_RESET(empty);
        if (pe_range_parse(variant, out->range_text[p], empty, NULL, &out->owned_ranges[p]) != PE_STATUS_OK) {
            set_error(reader, error, "invalid player range");
            return 0;
        }
        out->config.ranges[p].combos = out->owned_ranges[p]->combos;
        out->config.ranges[p].count = out->owned_ranges[p]->count;
    }
    out->config.terminal_value = NULL;
    return 1;
}

void pe_hrc_import_free(pe_hrc_import_t *imported)
{
    if (!imported) return;
    for (int p = 0; p < PE_HRC_MAX_PLAYERS; ++p) {
        pe_range_free(imported->owned_ranges[p]);
        free(imported->range_text[p]);
    }
    for (int n = 0; n < PE_HRC_MAX_NODES; ++n)
        for (int a = 0; a < PE_HRC_MAX_ACTIONS; ++a)
            free(imported->action_labels[n][a]);
    free(imported->owned_nodes);
    memset(imported, 0, sizeof(*imported));
}

int pe_hrc_import_json(const char *json, size_t length,
                       pe_hrc_terminal_fn terminal_value, void *user_data,
                       pe_hrc_import_t *out, pe_hrc_import_error_t *error)
{
    json_reader_t reader;
    if (error) memset(error, 0, sizeof(*error));
    if (!json || !out || length == 0) return -1;
    memset(out, 0, sizeof(*out));
    out->config.max_profiles = 100000;
    out->config.iterations = 100;
    out->bounty_multiplier = 1.0;
    reader.text = json; reader.length = length; reader.position = 0;
    if (!parse_document(&reader, out, error)) { pe_hrc_import_free(out); return -1; }
    out->config.terminal_value = terminal_value;
    out->config.user_data = user_data;
    if (!terminal_value || pe_hrc_validate(&out->config) != PE_HRC_OK) {
        set_error(&reader, error, "imported HRC tree failed validation");
        pe_hrc_import_free(out); return -1;
    }
    skip_space(&reader);
    if (reader.position != reader.length) {
        set_error(&reader, error, "trailing JSON data");
        pe_hrc_import_free(out); return -1;
    }
    return 0;
}

int pe_hrc_import_json_file(const char *path, pe_hrc_terminal_fn terminal_value,
                            void *user_data, pe_hrc_import_t *out,
                            pe_hrc_import_error_t *error)
{
    FILE *file; long size; char *buffer; size_t read_size; int result;
    if (error) memset(error, 0, sizeof(*error));
    if (!path) { if (error) snprintf(error->message, sizeof(error->message), "path is required"); return -1; }
    file = fopen(path, "rb"); if (!file) { if (error) snprintf(error->message, sizeof(error->message), "cannot open file"); return -1; }
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); if (error) snprintf(error->message, sizeof(error->message), "cannot seek file"); return -1; }
    size = ftell(file); if (size <= 0 || fseek(file, 0, SEEK_SET) != 0) { fclose(file); if (error) snprintf(error->message, sizeof(error->message), "invalid file size"); return -1; }
    buffer = malloc((size_t)size + 1u); if (!buffer) { fclose(file); if (error) snprintf(error->message, sizeof(error->message), "out of memory"); return -1; }
    read_size = fread(buffer, 1, (size_t)size, file); fclose(file);
    if (read_size != (size_t)size) { free(buffer); if (error) snprintf(error->message, sizeof(error->message), "cannot read file"); return -1; }
    buffer[read_size] = '\0';
    result = pe_hrc_import_json(buffer, read_size, terminal_value, user_data, out, error);
    free(buffer); return result;
}

static int is_fold(const char *label)
{
    return label && (same_ci(label, "fold") || same_ci(label, "f"));
}

int pe_hrc_trace_pot(const pe_hrc_tree_t *tree, const pe_hrc_pot_model_t *model,
                     const uint16_t *path, size_t path_length,
                     pe_hrc_pot_trace_t *out)
{
    double levels[PE_HRC_MAX_PLAYERS];
    int node_index;
    if (!tree || !model || !out || (path_length && !path) ||
        model->player_count != tree->num_players || model->player_count <= 0 ||
        model->player_count > PE_HRC_MAX_PLAYERS ||
        !pe_finite_double(model->initial_pot) || model->initial_pot < 0.0)
        return -1;
    memset(out, 0, sizeof(*out));
    out->total_pot = model->initial_pot;
    for (int p = 0; p < model->player_count; ++p) {
        if (!pe_finite_double(model->stacks[p]) || model->stacks[p] < 0.0 ||
            !pe_finite_double(model->antes[p]) || model->antes[p] < 0.0 ||
            model->antes[p] > model->stacks[p]) return -1;
        out->committed[p] = model->antes[p];
        out->total_pot += model->antes[p];
    }
    node_index = tree->root_index;
    for (size_t depth = 0; depth < path_length; ++depth) {
        const pe_hrc_node_t *node;
        const pe_hrc_action_t *action;
        int player;
        if (node_index < 0 || (size_t)node_index >= tree->node_count) return -1;
        node = &tree->nodes[node_index];
        if (node->terminal || path[depth] >= node->action_count) return -1;
        player = node->player_to_act;
        if (player < 0 || player >= model->player_count) return -1;
        action = &node->actions[path[depth]];
        if (action->amount < 0.0 || !pe_finite_double(action->amount) ||
            out->committed[player] + action->amount > model->stacks[player] + 1e-9) return -1;
        out->committed[player] += action->amount;
        if (is_fold(action->label)) out->folded[player] = 1;
        out->total_pot += action->amount;
        node_index = action->child_index;
    }
    for (int p = 0; p < model->player_count; ++p) levels[p] = out->committed[p];
    {
        double previous = 0.0;
        for (int slice = 0; slice < model->player_count; ++slice) {
        double level = HUGE_VAL; int contributors = 0; uint8_t eligible = 0;
        for (int p = 0; p < model->player_count; ++p)
            if (levels[p] > previous + 1e-9 && levels[p] < level) level = levels[p];
        if (!pe_finite_double(level)) break;
        for (int p = 0; p < model->player_count; ++p) {
            if (levels[p] + 1e-9 >= level) ++contributors;
            if (!out->folded[p] && levels[p] + 1e-9 >= level) eligible |= (uint8_t)(1u << p);
        }
        if (contributors > 0 && level > previous + 1e-9) {
            out->slices[out->slice_count].amount = (level - previous) * contributors;
            out->slices[out->slice_count].eligible_mask = eligible;
            ++out->slice_count;
        }
        previous = level;
        }
    }
    if (out->slice_count == 0) {
        uint8_t eligible = 0;
        for (int p = 0; p < model->player_count; ++p) if (!out->folded[p]) eligible |= (uint8_t)(1u << p);
        out->slices[0].amount = out->total_pot;
        out->slices[0].eligible_mask = eligible; out->slice_count = 1;
    } else {
        out->slices[0].amount += model->initial_pot;
    }
    return 0;
}

int pe_hrc_import_make_pko_input(const pe_hrc_import_t *imported, size_t max_profiles,
                                 pe_pko_range_outcome_fn outcome, void *user_data,
                                 pe_pko_range_input_t *out)
{
    if (!imported || !out || !outcome || imported->pot_model.player_count <= 0 ||
        imported->num_payouts <= 0 || imported->num_payouts > imported->pot_model.player_count)
        return -1;
    memset(out, 0, sizeof(*out));
    out->base.icm.num_players = imported->pot_model.player_count;
    out->base.icm.num_payouts = imported->num_payouts;
    out->base.bounty_multiplier = imported->bounty_multiplier;
    for (int p = 0; p < out->base.icm.num_players; ++p) {
        out->base.icm.stacks[p] = imported->pot_model.stacks[p];
        out->base.bounties[p] = imported->bounties[p];
        out->ranges[p] = imported->config.ranges[p];
        for (int position = 0; position < imported->num_payouts; ++position)
            out->base.icm.payouts[p][position] = imported->payouts[position];
    }
    out->max_profiles = max_profiles; out->outcome = outcome; out->user_data = user_data;
    return 0;
}
