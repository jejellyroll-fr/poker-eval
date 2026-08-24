/*
 * Normalize common PokerStars-style hand histories for the product layer.
 * This deliberately keeps the importer textual and lossless at the action
 * level: evaluators can consume the normalized rows without depending on a
 * particular room's wording.
 */
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hand_history_parse.h"

#define FIELD_SIZE 256
#define BOARD_SIZE 128
#define MAX_ROWS 65536

typedef struct {
    char hand_id[FIELD_SIZE];
    char street[32];
    char board[BOARD_SIZE];
    char player[FIELD_SIZE];
    char action[32];
    double amount;
    int has_amount;
    uint64_t infoset_key;
    int has_infoset_key;
} action_row_t;

typedef struct {
    uint64_t key;
    char street[32];
    char board[BOARD_SIZE];
    char position[FIELD_SIZE];
    char action[32];
} mapping_label_t;

static void trim(char *s)
{
    size_t n;
    char *start = s;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        ++start;
    }
    if (start != s) {
        memmove(s, start, strlen(start) + 1u);
    }
    n = strlen(s);
    while (n > 0u && isspace((unsigned char)s[n - 1u])) {
        s[--n] = '\0';
    }
}

static void normalize_token(const char *input, char *output, size_t output_size)
{
    size_t used = 0u;
    if (output_size == 0u) return;
    while (*input != '\0' && used + 1u < output_size) {
        unsigned char ch = (unsigned char)*input++;
        if (isspace(ch)) continue;
        output[used++] = (char)tolower(ch);
    }
    output[used] = '\0';
}

static int semantic_action_code(const char *action)
{
    if (strcmp(action, "fold") == 0) return 1;
    if (strcmp(action, "check") == 0) return 2;
    if (strcmp(action, "call") == 0) return 3;
    if (strcmp(action, "bet") == 0) return 4;
    if (strcmp(action, "raise") == 0) return 5;
    if (strcmp(action, "all-in") == 0 || strcmp(action, "allin") == 0) return 6;
    return 0;
}

static int split_csv(char *line, char **fields, int max_fields)
{
    int count = 0;
    char *cursor = line;
    while (count < max_fields) {
        fields[count++] = cursor;
        char *comma = strchr(cursor, ',');
        if (comma == NULL) break;
        *comma = '\0';
        cursor = comma + 1;
    }
    for (int i = 0; i < count; ++i) trim(fields[i]);
    return count;
}

static void unquote(char *field)
{
    size_t length = strlen(field);
    if (length >= 2u && field[0] == '"' && field[length - 1u] == '"') {
        memmove(field, field + 1, length - 2u);
        field[length - 2u] = '\0';
    }
}

static int parse_normalized_row(char *line, action_row_t *row)
{
    char *fields[8];
    int count = split_csv(line, fields, 8);
    if (count < 7 || strcmp(fields[0], "pe-hand-history/v1") != 0) return 0;
    for (int i = 1; i < count; ++i) unquote(fields[i]);
    memset(row, 0, sizeof(*row));
    snprintf(row->hand_id, sizeof(row->hand_id), "%s", fields[1]);
    snprintf(row->street, sizeof(row->street), "%s", fields[2]);
    snprintf(row->board, sizeof(row->board), "%s", fields[3]);
    snprintf(row->player, sizeof(row->player), "%s", fields[4]);
    snprintf(row->action, sizeof(row->action), "%s", fields[5]);
    trim(row->hand_id); trim(row->street); trim(row->board); trim(row->player); trim(row->action);
    if (fields[6][0] != '\0') {
        char *end = NULL;
        row->amount = strtod(fields[6], &end);
        row->has_amount = end != fields[6];
    }
    return row->action[0] != '\0';
}

static int load_mapping(const char *path, mapping_label_t **out, size_t *count)
{
    FILE *file;
    mapping_label_t *labels = NULL;
    size_t used = 0u, capacity = 0u;
    char line[2048];
    if (!path || !out || !count) return -1;
    file = fopen(path, "r");
    if (!file) return -1;
    while (fgets(line, sizeof(line), file) != NULL) {
        char *fields[10];
        int field_count;
        trim(line);
        if (line[0] == '\0' || !isdigit((unsigned char)line[0])) continue;
        field_count = split_csv(line, fields, 10);
        if ((field_count < 5) || (field_count >= 4 &&
            strcmp(fields[0], "key") == 0)) continue;
        if (used == capacity) {
            size_t next = capacity ? capacity * 2u : 64u;
            mapping_label_t *grown = (mapping_label_t *)realloc(labels, next * sizeof(*grown));
            if (!grown) { free(labels); fclose(file); return -1; }
            labels = grown;
            capacity = next;
        }
        memset(&labels[used], 0, sizeof(labels[used]));
        labels[used].key = strtoull(fields[0], NULL, 0);
        if (field_count >= 8) {
            /* rich labels: key,street,board,runout,position,pot,action,label */
            snprintf(labels[used].street, sizeof(labels[used].street), "%s", fields[1]);
            snprintf(labels[used].board, sizeof(labels[used].board), "%s", fields[2]);
            snprintf(labels[used].position, sizeof(labels[used].position), "%s", fields[4]);
            snprintf(labels[used].action, sizeof(labels[used].action), "%s", fields[6]);
        } else {
            /* compact labels: key,street,board,action,label */
            snprintf(labels[used].street, sizeof(labels[used].street), "%s", fields[1]);
            snprintf(labels[used].board, sizeof(labels[used].board), "%s", fields[2]);
            snprintf(labels[used].action, sizeof(labels[used].action), "%s", fields[3]);
        }
        ++used;
    }
    fclose(file);
    *out = labels;
    *count = used;
    return 0;
}

static int mapping_matches(const mapping_label_t *label, const action_row_t *row)
{
    char lhs[BOARD_SIZE], rhs[BOARD_SIZE], lhs_street[32], rhs_street[32];
    char lhs_action[32], rhs_action[32];
    char lhs_position[FIELD_SIZE], rhs_position[FIELD_SIZE];
    normalize_token(label->board, lhs, sizeof(lhs));
    normalize_token(row->board, rhs, sizeof(rhs));
    normalize_token(label->street, lhs_street, sizeof(lhs_street));
    normalize_token(row->street, rhs_street, sizeof(rhs_street));
    normalize_token(label->action, lhs_action, sizeof(lhs_action));
    normalize_token(row->action, rhs_action, sizeof(rhs_action));
    if (strcmp(lhs_street, rhs_street) != 0 || strcmp(lhs, rhs) != 0) return 0;
    if (strcmp(lhs_action, rhs_action) != 0 &&
        (lhs_action[0] < '0' || lhs_action[0] > '9' ||
         atoi(lhs_action) != semantic_action_code(row->action))) return 0;
    normalize_token(label->position, lhs_position, sizeof(lhs_position));
    normalize_token(row->player, rhs_position, sizeof(rhs_position));
    return lhs_position[0] == '\0' || strcmp(lhs_position, rhs_position) == 0 ||
           strcmp(lhs_position, "*") == 0;
}

static int map_row(action_row_t *row, const mapping_label_t *labels, size_t count)
{
    const mapping_label_t *fallback = NULL;
    if (!row || !labels) return 0;
    for (size_t i = 0u; i < count; ++i) {
        if (!mapping_matches(&labels[i], row)) continue;
        if (labels[i].position[0] == '\0' || strcmp(labels[i].position, "*") == 0) {
            fallback = &labels[i];
            continue;
        }
        row->infoset_key = labels[i].key;
        row->has_infoset_key = 1;
        return 1;
    }
    if (fallback) {
        row->infoset_key = fallback->key;
        row->has_infoset_key = 1;
        return 1;
    }
    return 0;
}

static void json_string(FILE *out, const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    fputc('"', out);
    while (*p != 0u) {
        if (*p == '"' || *p == '\\') {
            fputc('\\', out);
            fputc((int)*p, out);
        } else if (*p == '\n') {
            fputs("\\n", out);
        } else if (*p == '\r') {
            fputs("\\r", out);
        } else if (*p == '\t') {
            fputs("\\t", out);
        } else if (*p < 32u) {
            fputc(' ', out);
        } else {
            fputc((int)*p, out);
        }
        ++p;
    }
    fputc('"', out);
}

static int starts_with(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static void extract_board(const char *line, char *out, size_t out_size)
{
    const char *open = strchr(line, '[');
    size_t used = 0u;
    out[0] = '\0';
    while (open != NULL && used + 1u < out_size)
    {
        const char *close = strchr(open + 1, ']');
        if (close == NULL) break;
        size_t n = (size_t)(close - open - 1);
        if (n > out_size - used - 1u) n = out_size - used - 1u;
        if (used != 0u && n != 0u && used + 1u < out_size) out[used++] = ' ';
        memcpy(out + used, open + 1, n);
        used += n;
        out[used] = '\0';
        open = strchr(close + 1, '[');
    }
    trim(out);
}

static int parse_action(const char *line, action_row_t *row)
{
    static const char *const verbs[] = {" folds", " checks", " calls ", " bets ", " raises "};
    static const char *const names[] = {"fold", "check", "call", "bet", "raise"};
    size_t i;
    const char *hit = NULL;
    size_t verb_len = 0u;
    for (i = 0u; i < sizeof(verbs) / sizeof(verbs[0]); ++i) {
        const char *candidate = strstr(line, verbs[i]);
        if (candidate != NULL && (hit == NULL || candidate < hit)) {
            hit = candidate;
            verb_len = strlen(verbs[i]);
            strncpy(row->action, names[i], sizeof(row->action) - 1u);
            row->action[sizeof(row->action) - 1u] = '\0';
        }
    }
    if (hit == NULL) {
        return 0;
    }
    if ((size_t)(hit - line) >= sizeof(row->player)) {
        return 0;
    }
    memcpy(row->player, line, (size_t)(hit - line));
    row->player[hit - line] = '\0';
    trim(row->player);
    hh_strip_trailing_colon(row->player);
    row->has_amount = hh_parse_amount(hit + verb_len, &row->amount);
    return row->player[0] != '\0';
}

static void usage(const char *program)
{
    fprintf(stderr, "usage: %s --input FILE [--output FILE] [--format json|csv] "
                    "[--mapping LABELS.csv] [--input-format pokerstars|normalized]\n", program);
}

int main(int argc, char **argv)
{
    const char *input_path = NULL;
    const char *output_path = NULL;
    const char *mapping_path = NULL;
    const char *input_format = "pokerstars";
    const char *format = "json";
    action_row_t *rows;
    mapping_label_t *mapping = NULL;
    size_t mapping_count = 0u;
    size_t row_count = 0u;
    FILE *input;
    FILE *output;
    char line[2048];
    char hand_id[FIELD_SIZE] = "";
    char street[32] = "preflop";
    char board[BOARD_SIZE] = "";
    int i;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
            input_path = argv[++i];
        } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            output_path = argv[++i];
        } else if (strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
            format = argv[++i];
        } else if (strcmp(argv[i], "--mapping") == 0 && i + 1 < argc) {
            mapping_path = argv[++i];
        } else if (strcmp(argv[i], "--input-format") == 0 && i + 1 < argc) {
            input_format = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    if (input_path == NULL || (strcmp(format, "json") != 0 && strcmp(format, "csv") != 0) ||
        (strcmp(input_format, "pokerstars") != 0 && strcmp(input_format, "normalized") != 0)) {
        usage(argv[0]);
        return 2;
    }
    if (mapping_path != NULL && load_mapping(mapping_path, &mapping, &mapping_count) != 0) {
        fprintf(stderr, "cannot load mapping %s\n", mapping_path);
        return 1;
    }
    input = fopen(input_path, "r");
    if (input == NULL) {
        fprintf(stderr, "cannot open %s\n", input_path);
        return 1;
    }
    rows = (action_row_t *)calloc(MAX_ROWS, sizeof(*rows));
    if (rows == NULL) {
        fclose(input);
        return 1;
    }
    while (fgets(line, sizeof(line), input) != NULL) {
        action_row_t row;
        trim(line);
        if (strcmp(input_format, "normalized") == 0) {
            if (strncmp(line, "schema,", 7) == 0 || line[0] == '\0') continue;
            if (row_count < MAX_ROWS && parse_normalized_row(line, &row)) {
                if (mapping_path != NULL) map_row(&row, mapping, mapping_count);
                rows[row_count++] = row;
            }
            continue;
        }
        if (strstr(line, "Hand #") != NULL) {
            hh_extract_hand_id(line, hand_id, sizeof(hand_id));
            strcpy(street, "preflop");
            board[0] = '\0';
        } else if (strstr(line, "*** FLOP ***") != NULL) {
            strcpy(street, "flop");
            extract_board(line, board, sizeof(board));
        } else if (strstr(line, "*** TURN ***") != NULL) {
            strcpy(street, "turn");
            extract_board(line, board, sizeof(board));
        } else if (strstr(line, "*** RIVER ***") != NULL) {
            strcpy(street, "river");
            extract_board(line, board, sizeof(board));
        } else if (row_count < MAX_ROWS) {
            memset(&row, 0, sizeof(row));
            if (!parse_action(line, &row)) {
                continue;
            }
            strncpy(row.hand_id, hand_id, sizeof(row.hand_id) - 1u);
            strncpy(row.street, street, sizeof(row.street) - 1u);
            strncpy(row.board, board, sizeof(row.board) - 1u);
            if (mapping_path != NULL) map_row(&row, mapping, mapping_count);
            rows[row_count++] = row;
        }
    }
    fclose(input);
    output = output_path == NULL ? stdout : fopen(output_path, "w");
    if (output == NULL) {
        free(rows);
        fprintf(stderr, "cannot open output\n");
        return 1;
    }
    if (strcmp(format, "csv") == 0) {
        size_t j;
        fputs(mapping_path ? "schema,hand_id,street,board,player,action,amount,infoset_key,mapping_status\n" :
                             "schema,hand_id,street,board,player,action,amount\n", output);
        for (j = 0u; j < row_count; ++j) {
            fprintf(output, "pe-hand-history/v1,");
            fprintf(output, "\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",", rows[j].hand_id,
                    rows[j].street, rows[j].board, rows[j].player, rows[j].action);
            if (rows[j].has_amount) {
                fprintf(output, "%.2f", rows[j].amount);
            } else {
                fputc(',', output);
            }
            if (mapping_path) {
                if (rows[j].has_infoset_key)
                    fprintf(output, ",0x%016" PRIx64 ",mapped\n", rows[j].infoset_key);
                else
                    fputs(",,unmapped\n", output);
            } else {
                fputc('\n', output);
            }
        }
    } else {
        size_t j;
        fputs("{\"schema\":\"pe-hand-history/v1\"", output);
        if (mapping_path) {
            size_t mapped = 0u;
            for (j = 0u; j < row_count; ++j) if (rows[j].has_infoset_key) ++mapped;
            fputs(",\"mapping\":", output); json_string(output, mapping_path);
            fprintf(output, ",\"mapped_actions\":%zu,\"unmapped_actions\":%zu", mapped, row_count - mapped);
        }
        fputs(",\"actions\":[", output);
        for (j = 0u; j < row_count; ++j) {
            if (j != 0u) fputc(',', output);
            fputs("{\"hand_id\":", output); json_string(output, rows[j].hand_id);
            fputs(",\"street\":", output); json_string(output, rows[j].street);
            fputs(",\"board\":", output); json_string(output, rows[j].board);
            fputs(",\"player\":", output); json_string(output, rows[j].player);
            fputs(",\"action\":", output); json_string(output, rows[j].action);
            fputs(",\"amount\":", output);
            if (rows[j].has_amount) fprintf(output, "%.2f", rows[j].amount); else fputs("null", output);
            if (mapping_path) {
                fputs(",\"infoset_key\":", output);
                if (rows[j].has_infoset_key)
                    fprintf(output, "\"0x%016" PRIx64 "\"", rows[j].infoset_key);
                else
                    fputs("null", output);
                fputs(",\"mapping_status\":", output);
                json_string(output, rows[j].has_infoset_key ? "mapped" : "unmapped");
            }
            fputc('}', output);
        }
        fputs("]}\n", output);
    }
    if (output_path != NULL) fclose(output);
    fprintf(stderr, "imported %zu action rows\n", row_count);
    free(rows);
    free(mapping);
    return 0;
}
