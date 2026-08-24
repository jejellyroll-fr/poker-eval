/* pe_solution_report - aggregate a .pe_sol by optional spot metadata. */

#include <poker_eval/engine/solvers/cfr/mpf_compact_storage.h>

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PE_REPORT_MAX_ACTIONS 256
#define PE_REPORT_MAX_GROUPS 4096

typedef struct {
    uint64_t key;
    char street[32];
    char board[64];
    double weight;
} metadata_row_t;

typedef struct {
    char name[100];
    size_t infosets;
    double weight;
    double actions[PE_REPORT_MAX_ACTIONS];
    int action_count;
} report_group_t;

static void usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s --solution FILE [--metadata CSV] [--json FILE] [--html FILE]\n"
            "Metadata CSV columns: key,street,board,weight\n",
            program);
}

static void trim(char *text)
{
    char *end;
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n')
        ++text;
    end = text + strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t' ||
                          end[-1] == '\r' || end[-1] == '\n'))
        *--end = '\0';
}

static int load_metadata(const char *path, metadata_row_t **out, size_t *count)
{
    FILE *file;
    metadata_row_t *rows = NULL;
    size_t used = 0u, capacity = 0u;
    char line[512];
    if (!path) { *out = NULL; *count = 0u; return 0; }
    file = fopen(path, "r");
    if (!file) return -1;
    while (fgets(line, sizeof(line), file))
    {
        char *fields[4];
        char *field;
        char *save = NULL;
        int n = 0;
        trim(line);
        if (!line[0] || line[0] == '#') continue;
        field = strtok_r(line, ",", &save);
        while (field && n < 4) { fields[n++] = field; field = strtok_r(NULL, ",", &save); }
        if (n < 4 || strcmp(fields[0], "key") == 0) continue;
        if (used == capacity)
        {
            size_t next = capacity ? capacity * 2u : 64u;
            metadata_row_t *grown = (metadata_row_t *)realloc(rows, next * sizeof(*rows));
            if (!grown) { free(rows); fclose(file); return -1; }
            rows = grown; capacity = next;
        }
        rows[used].key = strtoull(fields[0], NULL, 0);
        snprintf(rows[used].street, sizeof(rows[used].street), "%s", fields[1]);
        snprintf(rows[used].board, sizeof(rows[used].board), "%s", fields[2]);
        rows[used].weight = strtod(fields[3], NULL);
        if (!isfinite(rows[used].weight) || rows[used].weight < 0.0)
            rows[used].weight = 1.0;
        ++used;
    }
    fclose(file);
    *out = rows;
    *count = used;
    return 0;
}

static const metadata_row_t *find_metadata(const metadata_row_t *rows,
                                           size_t count, uint64_t key)
{
    for (size_t i = 0u; i < count; ++i)
        if (rows[i].key == key) return &rows[i];
    return NULL;
}

static report_group_t *find_group(report_group_t *groups, size_t *count,
                                  const char *name)
{
    for (size_t i = 0u; i < *count; ++i)
        if (strcmp(groups[i].name, name) == 0) return &groups[i];
    if (*count >= PE_REPORT_MAX_GROUPS) return NULL;
    snprintf(groups[*count].name, sizeof(groups[*count].name), "%s", name);
    return &groups[(*count)++];
}

static void json_string(FILE *out, const char *text)
{
    fputc('"', out);
    for (; *text; ++text)
    {
        if (*text == '"' || *text == '\\') fputc('\\', out);
        fputc(*text, out);
    }
    fputc('"', out);
}

static void write_json(FILE *out, const report_group_t *groups, size_t count)
{
    fprintf(out, "{\"schema\":\"pe-solution-report/v1\",\"groups\":[");
    for (size_t i = 0u; i < count; ++i)
    {
        const report_group_t *g = &groups[i];
        double entropy = 0.0;
        if (g->weight > 0.0)
            for (int a = 0; a < g->action_count; ++a)
            {
                double p = g->actions[a] / g->weight;
                if (p > 0.0) entropy -= p * (log(p) / log(2.0));
            }
        if (i) fputc(',', out);
        fprintf(out, "{\"group\":"); json_string(out, g->name);
        fprintf(out, ",\"infosets\":%zu,\"weight\":%.12g,\"entropy_bits\":%.12g,\"action_frequency\":[",
                g->infosets, g->weight, entropy);
        for (int a = 0; a < g->action_count; ++a)
        {
            if (a) fputc(',', out);
            fprintf(out, "%.12g", g->weight > 0.0 ? g->actions[a] / g->weight : 0.0);
        }
        fputs("]}", out);
    }
    fputs("]}\n", out);
}

static void write_html(FILE *out, const report_group_t *groups, size_t count)
{
    fputs("<!doctype html><meta charset=\"utf-8\"><title>poker-eval solution report</title>"
          "<style>body{font:15px system-ui;margin:2rem}table{border-collapse:collapse}"
          "td,th{border:1px solid #ccc;padding:.45rem}.bar{display:inline-block;"
          "height:1rem;background:#3b82f6;margin-right:2px;vertical-align:middle}</style>"
          "<h1>Solution report</h1><table><thead><tr><th>Group</th><th>Infosets</th>"
          "<th>Entropy</th><th>Action frequencies</th></tr></thead><tbody>", out);
    for (size_t i = 0u; i < count; ++i)
    {
        const report_group_t *g = &groups[i];
        double entropy = 0.0;
        fprintf(out, "<tr><td>");
        for (const char *p = g->name; *p; ++p)
            fputs(*p == '<' ? "&lt;" : (*p == '&' ? "&amp;" : (char[2]){*p, 0}), out);
        fprintf(out, "</td><td>%zu</td><td>", g->infosets);
        if (g->weight > 0.0)
            for (int a = 0; a < g->action_count; ++a)
            {
                double p = g->actions[a] / g->weight;
                if (p > 0.0) entropy -= p * (log(p) / log(2.0));
            }
        fprintf(out, "%.4f</td><td>", entropy);
        for (int a = 0; a < g->action_count; ++a)
        {
            double p = g->weight > 0.0 ? g->actions[a] / g->weight : 0.0;
            fprintf(out, "<span class=bar style=\"width:%.1fpx\" title=\"action %d %.2f%%\"></span>",
                    p * 160.0, a, p * 100.0);
        }
        fputs("</td></tr>", out);
    }
    fputs("</tbody></table>", out);
}

int main(int argc, char **argv)
{
    const char *solution = NULL, *metadata_path = NULL, *json_path = NULL, *html_path = NULL;
    metadata_row_t *metadata = NULL;
    report_group_t *groups;
    size_t metadata_count = 0u, group_count = 0u;
    pe_sol_mmap_t *view = NULL;
    FILE *out;
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--solution") == 0 && i + 1 < argc) solution = argv[++i];
        else if (strcmp(argv[i], "--metadata") == 0 && i + 1 < argc) metadata_path = argv[++i];
        else if (strcmp(argv[i], "--json") == 0 && i + 1 < argc) json_path = argv[++i];
        else if (strcmp(argv[i], "--html") == 0 && i + 1 < argc) html_path = argv[++i];
        else { usage(argv[0]); return 2; }
    }
    if (!solution || load_metadata(metadata_path, &metadata, &metadata_count) != 0 ||
        pe_sol_open_mmap(solution, &view) != 0)
    {
        fprintf(stderr, "Unable to open solution or metadata: %s\n", strerror(errno));
        free(metadata); return 1;
    }
    groups = (report_group_t *)calloc(PE_REPORT_MAX_GROUPS, sizeof(*groups));
    if (!groups) { pe_sol_close_mmap(view); free(metadata); return 1; }
    for (size_t i = 0u; i < pe_sol_mmap_infoset_count(view); ++i)
    {
        uint64_t key = 0u;
        double probs[PE_REPORT_MAX_ACTIONS];
        int actions = 0;
        const metadata_row_t *meta;
        char name[100];
        report_group_t *group;
        if (pe_sol_mmap_get_strategy(view, i, &key, PE_REPORT_MAX_ACTIONS,
                                     probs, &actions) != 0) continue;
        meta = find_metadata(metadata, metadata_count, key);
        if (meta) snprintf(name, sizeof(name), "%s/%s", meta->street, meta->board);
        else snprintf(name, sizeof(name), "unknown/unknown");
        group = find_group(groups, &group_count, name);
        if (!group) continue;
        group->infosets++;
        group->weight += meta ? meta->weight : 1.0;
        if (actions > group->action_count) group->action_count = actions;
        for (int a = 0; a < actions; ++a) group->actions[a] += probs[a] * (meta ? meta->weight : 1.0);
    }
    pe_sol_close_mmap(view); free(metadata);
    if (json_path)
    {
        out = fopen(json_path, "w");
        if (!out) { free(groups); return 1; }
        write_json(out, groups, group_count); fclose(out);
    }
    else write_json(stdout, groups, group_count);
    if (html_path)
    {
        out = fopen(html_path, "w");
        if (!out) { free(groups); return 1; }
        write_html(out, groups, group_count); fputc('\n', out); fclose(out);
    }
    free(groups);
    return 0;
}
