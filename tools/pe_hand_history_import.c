/*
 * Normalize common PokerStars-style hand histories for the product layer.
 * This deliberately keeps the importer textual and lossless at the action
 * level: evaluators can consume the normalized rows without depending on a
 * particular room's wording.
 */
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
} action_row_t;

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

static void extract_hand_id(const char *line, char *out, size_t out_size)
{
    const char *hash = strchr(line, '#');
    const char *p;
    size_t n = 0u;
    if (hash == NULL) {
        return;
    }
    p = hash + 1;
    while (*p != '\0' && !isspace((unsigned char)*p) && n + 1u < out_size) {
        out[n++] = *p++;
    }
    out[n] = '\0';
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

static int parse_amount(const char *text, double *amount)
{
    const char *p = text;
    const char *last = NULL;
    char *end;
    while (*p != '\0') {
        if (*p == '$' || isdigit((unsigned char)*p)) {
            last = p + (*p == '$');
        }
        ++p;
    }
    if (last == NULL) {
        return 0;
    }
    errno = 0;
    *amount = strtod(last, &end);
    return end != last && errno == 0;
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
    row->has_amount = parse_amount(hit + verb_len, &row->amount);
    return row->player[0] != '\0';
}

static void usage(const char *program)
{
    fprintf(stderr, "usage: %s --input FILE [--output FILE] [--format json|csv]\n", program);
}

int main(int argc, char **argv)
{
    const char *input_path = NULL;
    const char *output_path = NULL;
    const char *format = "json";
    action_row_t *rows;
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
        } else if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    if (input_path == NULL || (strcmp(format, "json") != 0 && strcmp(format, "csv") != 0)) {
        usage(argv[0]);
        return 2;
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
        if (strstr(line, "Hand #") != NULL) {
            extract_hand_id(line, hand_id, sizeof(hand_id));
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
        fputs("schema,hand_id,street,board,player,action,amount\n", output);
        for (j = 0u; j < row_count; ++j) {
            fprintf(output, "pe-hand-history/v1,");
            fprintf(output, "\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",", rows[j].hand_id,
                    rows[j].street, rows[j].board, rows[j].player, rows[j].action);
            if (rows[j].has_amount) {
                fprintf(output, "%.2f\n", rows[j].amount);
            } else {
                fputs("\n", output);
            }
        }
    } else {
        size_t j;
        fputs("{\"schema\":\"pe-hand-history/v1\",\"actions\":[", output);
        for (j = 0u; j < row_count; ++j) {
            if (j != 0u) fputc(',', output);
            fputs("{\"hand_id\":", output); json_string(output, rows[j].hand_id);
            fputs(",\"street\":", output); json_string(output, rows[j].street);
            fputs(",\"board\":", output); json_string(output, rows[j].board);
            fputs(",\"player\":", output); json_string(output, rows[j].player);
            fputs(",\"action\":", output); json_string(output, rows[j].action);
            fputs(",\"amount\":", output);
            if (rows[j].has_amount) fprintf(output, "%.2f", rows[j].amount); else fputs("null", output);
            fputc('}', output);
        }
        fputs("]}\n", output);
    }
    if (output_path != NULL) fclose(output);
    fprintf(stderr, "imported %zu action rows\n", row_count);
    free(rows);
    return 0;
}
