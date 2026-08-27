#include <poker_eval/economics/icm.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_values(const char *text, double *values, int max_values)
{
    char buffer[2048];
    char *cursor;
    int count = 0;
    if (!text || !values || max_values <= 0 ||
        strnlen(text, sizeof(buffer)) >= sizeof(buffer)) return -1;
    snprintf(buffer, sizeof(buffer), "%s", text);
    cursor = buffer;
    while (cursor && count < max_values) {
        char *comma = strchr(cursor, ',');
        char *end = NULL;
        values[count] = strtod(cursor, &end);
        if (end == cursor) return -1;
        ++count;
        if (!comma) break;
        cursor = comma + 1;
    }
    return count;
}

static int load_matrix(const char *path,
                       double matrix[ICM_MAX_PLAYERS][ICM_MAX_PLAYERS],
                       int *rows, int *columns)
{
    FILE *file = fopen(path, "r");
    char line[2048];
    int row_count = 0;
    int column_count = 0;
    if (!file) return -1;
    while (fgets(line, sizeof(line), file) != NULL && row_count < ICM_MAX_PLAYERS) {
        char *cursor = line;
        int columns_here = 0;
        char *end;
        while (*cursor && columns_here < ICM_MAX_PLAYERS) {
            char *comma = strchr(cursor, ',');
            matrix[row_count][columns_here] = strtod(cursor, &end);
            if (end == cursor) { columns_here = 0; break; }
            ++columns_here;
            if (!comma) break;
            cursor = comma + 1;
        }
        if (columns_here == 0) continue; /* header or blank line */
        if (column_count == 0) column_count = columns_here;
        if (columns_here != column_count) { fclose(file); return -1; }
        ++row_count;
    }
    fclose(file);
    if (row_count == 0 || column_count == 0) return -1;
    *rows = row_count;
    *columns = column_count;
    return 0;
}

static void usage(const char *program)
{
    fprintf(stderr, "usage: %s --stacks 100,50,25 --payouts 50,30,20 [--json FILE]\n"
                    "       %s --stacks 100,50 --asym-payouts MATRIX.csv [--json FILE]\n",
            program, program);
}

int main(int argc, char **argv)
{
    const char *stacks_text = NULL;
    const char *payouts_text = NULL;
    const char *asym_path = NULL;
    const char *json_path = NULL;
    double stacks[ICM_MAX_PLAYERS], payouts[ICM_MAX_PLAYERS];
    double payout_matrix[ICM_MAX_PLAYERS][ICM_MAX_PLAYERS] = {{0}};
    int num_players, num_payouts;
    icm_input_t input = {0};
    icm_result_t result = {0};
    FILE *out = stdout;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--stacks") == 0 && i + 1 < argc) stacks_text = argv[++i];
        else if (strcmp(argv[i], "--payouts") == 0 && i + 1 < argc) payouts_text = argv[++i];
        else if (strcmp(argv[i], "--asym-payouts") == 0 && i + 1 < argc) asym_path = argv[++i];
        else if (strcmp(argv[i], "--json") == 0 && i + 1 < argc) json_path = argv[++i];
        else { usage(argv[0]); return 2; }
    }
    num_players = parse_values(stacks_text, stacks, ICM_MAX_PLAYERS);
    num_payouts = parse_values(payouts_text, payouts, ICM_MAX_PLAYERS);
    if (num_players <= 0 || (asym_path == NULL &&
        (num_payouts <= 0 || num_payouts > num_players))) {
        usage(argv[0]);
        return 2;
    }
    if (json_path) out = fopen(json_path, "w");
    if (!out) { fprintf(stderr, "cannot open %s\n", json_path); return 1; }
    if (asym_path) {
        icm_asymmetric_input_t asym = {0};
        icm_asymmetric_result_t asym_result = {0};
        int matrix_rows, matrix_columns;
        if (load_matrix(asym_path, payout_matrix, &matrix_rows, &matrix_columns) != 0 ||
            matrix_rows != num_players || matrix_columns > num_players) {
            fprintf(stderr, "invalid asymmetric payout matrix\n");
            if (json_path) fclose(out);
            return 2;
        }
        asym.num_players = num_players;
        asym.num_payouts = matrix_columns;
        for (int i = 0; i < num_players; ++i) {
            asym.stacks[i] = stacks[i];
            for (int j = 0; j < matrix_columns; ++j) asym.payouts[i][j] = payout_matrix[i][j];
        }
        if (pe_icm_calculate_asymmetric(&asym, &asym_result) != 0) {
            fprintf(stderr, "invalid asymmetric ICM inputs\n");
            if (json_path) fclose(out);
            return 1;
        }
        fprintf(out, "{\"schema\":\"pe-icm/v1\",\"mode\":\"asymmetric\",\"players\":[");
        for (int i = 0; i < num_players; ++i) {
            if (i) fputc(',', out);
            fprintf(out, "{\"index\":%d,\"stack\":%.9g,\"ev\":%.9g,\"finish_probability\":[",
                    i, asym.stacks[i], asym_result.ev[i]);
            for (int j = 0; j < matrix_columns; ++j) {
                if (j) fputc(',', out);
                fprintf(out, "%.9g", asym_result.finish_probability[i][j]);
            }
            fputs("]}", out);
        }
    } else {
        input.num_players = num_players;
        input.num_payouts = num_payouts;
        for (int i = 0; i < num_players; ++i) input.stacks[i] = stacks[i];
        for (int i = 0; i < num_payouts; ++i) input.payouts[i] = payouts[i];
        if (pe_icm_calculate(&input, &result) != 0) {
            fprintf(stderr, "invalid ICM inputs\n");
            if (json_path) fclose(out);
            return 1;
        }
        fprintf(out, "{\"schema\":\"pe-icm/v1\",\"mode\":\"standard\",\"players\":[");
        for (int i = 0; i < num_players; ++i) {
            if (i) fputc(',', out);
            fprintf(out, "{\"index\":%d,\"stack\":%.9g,\"ev\":%.9g,\"equity\":%.9g}",
                    i, input.stacks[i], result.icm_ev[i], result.equity[i]);
        }
    }
    fputs("]}\n", out);
    if (json_path) fclose(out);
    return 0;
}
