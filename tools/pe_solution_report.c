/* pe_solution_report - aggregate a .pe_sol by optional spot metadata. */

#include <poker_eval/engine/solvers/cfr/mpf_compact_storage.h>
#include <poker_eval/solver/pe_monker.h>
#include <poker_eval/solver/pe_monker_key.h>

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PE_REPORT_MAX_ACTIONS 256
#define PE_REPORT_MAX_GROUPS 16384

typedef struct {
    uint64_t key;
    char street[32];
    char board[64];
    char flop[64];
    char runout[64];
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
            "       %s --monker-tree TREE --monker-mkr MKR [--entry NAME]\n"
            "       %s --decode-key HEX [--packed-board-shift N --packed-board-cards N]\n"
            "Metadata CSV columns: key,street,board,weight[,flop,runout]\n"
            "Aggregation: --aggregate board|flop|runout (default board)\n",
            program, program, program);
}

static void trim(char *text)
{
    char *end;
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n')
        ++text;
    end = text + strnlen(text, 512u);
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
        char *fields[6] = {0};
        char *field;
        char *save = NULL;
        int n = 0;
        trim(line);
        if (!line[0] || line[0] == '#') continue;
        field = strtok_r(line, ",", &save);
        while (field && n < 6) { fields[n++] = field; field = strtok_r(NULL, ",", &save); }
        if (n < 4 || strcmp(fields[0], "key") == 0) continue;
        if (used == capacity)
        {
            size_t next = capacity ? capacity * 2u : 64u;
            metadata_row_t *grown = (metadata_row_t *)realloc(rows, next * sizeof(*rows));
            if (!grown) { free(rows); fclose(file); return -1; }
            rows = grown; capacity = next;
        }
        memset(&rows[used], 0, sizeof(rows[used]));
        rows[used].key = strtoull(fields[0], NULL, 0);
        snprintf(rows[used].street, sizeof(rows[used].street), "%s", fields[1]);
        snprintf(rows[used].board, sizeof(rows[used].board), "%s", fields[2]);
        if (n >= 5) snprintf(rows[used].flop, sizeof(rows[used].flop), "%s", fields[4]);
        if (n >= 6) snprintf(rows[used].runout, sizeof(rows[used].runout), "%s", fields[5]);
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

static void write_json(FILE *out, const report_group_t *groups, size_t count,
                       const char *aggregation)
{
    fputs("{\"schema\":\"pe-solution-report/v2\",\"aggregation\":", out);
    json_string(out, aggregation);
    fputs(",\"groups\":[", out);
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

static void write_html(FILE *out, const report_group_t *groups, size_t count,
                       const char *aggregation)
{
    fputs("<!doctype html><meta charset=\"utf-8\"><title>poker-eval solution report</title>"
          "<style>body{font:15px system-ui;margin:2rem}table{border-collapse:collapse}"
          "td,th{border:1px solid #ccc;padding:.45rem}.bar{display:inline-block;"
          "height:1rem;background:#3b82f6;margin-right:2px;vertical-align:middle}</style>"
          "<h1>Solution report</h1><p>Aggregation: <code>", out);
    fputs(aggregation, out);
    fputs("</code></p><label>Filter <input id=\"filter\" placeholder=\"flop, street, node...\"></label>"
          "<table id=\"report\"><thead><tr><th>Group</th><th>Infosets</th>"
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
    fputs("</tbody></table><script>const q=document.getElementById('filter');"
          "q.addEventListener('input',()=>{const s=q.value.toLowerCase();"
          "document.querySelectorAll('#report tbody tr').forEach(r=>r.hidden="
          "!r.textContent.toLowerCase().includes(s));});</script>", out);
}

static const char *street_name(mpf_street_t street)
{
    switch (street)
    {
    case MPF_STREET_PREFLOP: return "preflop";
    case MPF_STREET_FLOP: return "flop";
    case MPF_STREET_TURN: return "turn";
    case MPF_STREET_RIVER: return "river";
    case MPF_STREET_SHOWDOWN: return "showdown";
    default: return "unknown";
    }
}

static const char *find_strategy_entry(const pe_monker_mkr_t *archive)
{
    if (!archive) return NULL;
    for (size_t i = 0u; i < archive->count; ++i)
        if (strstr(archive->entries[i].name, "storedstrategy") != NULL)
            return archive->entries[i].name;
    return NULL;
}

static int load_monker_groups(const char *tree_path, const char *mkr_path,
                              const char *entry_name,
                              report_group_t *groups, size_t *group_count)
{
    mpf_tree_def_t *tree = NULL;
    pe_monker_mkr_t archive;
    pe_monker_mkr_strategy_t strategy;
    int32_t *node_of_slot = NULL;
    uint32_t class_count = 0u;
    const char *selected_entry = entry_name;
    pe_monker_status_t tree_status;
    pe_monker_mkr_status_t mkr_status;
    memset(&archive, 0, sizeof(archive));
    memset(&strategy, 0, sizeof(strategy));
    tree_status = pe_monker_tree_load(tree_path, &tree);
    if (tree_status != PE_MONKER_OK || !tree) return -1;
    mkr_status = pe_monker_mkr_read(mkr_path, &archive);
    if (mkr_status != PE_MONKER_MKR_OK) { mpf_tree_free(tree); return -1; }
    if (!selected_entry) selected_entry = find_strategy_entry(&archive);
    if (!selected_entry || pe_monker_mkr_read_strategy(
            &archive, selected_entry, &strategy) != PE_MONKER_MKR_OK)
    {
        pe_monker_mkr_free(&archive); mpf_tree_free(tree); return -1;
    }
    node_of_slot = (int32_t *)calloc(strategy.slot_count, sizeof(*node_of_slot));
    if (!node_of_slot || pe_monker_mkr_bind_strategy(
            tree, &strategy, node_of_slot, strategy.slot_count) != PE_MONKER_MKR_OK ||
        pe_monker_mkr_strategy_class_count(tree, &strategy, &class_count) != PE_MONKER_MKR_OK)
    {
        free(node_of_slot); pe_monker_mkr_strategy_free(&strategy);
        pe_monker_mkr_free(&archive); mpf_tree_free(tree); return -1;
    }
    for (uint32_t slot = 0u; slot < strategy.slot_count; ++slot)
    {
        const pe_monker_mkr_slot_t *stored = &strategy.slots[slot];
        int32_t node_index = node_of_slot[slot];
        mpf_tree_node_t *node;
        report_group_t *group;
        char name[100];
        if (stored->kind != PE_MONKER_SLOT_BYTES || node_index < 0 ||
            node_index >= tree->node_count)
            continue;
        node = &tree->nodes[node_index];
        if (node->action_count <= 0 ||
            stored->count < class_count * (uint32_t)node->action_count)
            continue;
        snprintf(name, sizeof(name), "%s/%s", street_name(node->street),
                 node->id ? node->id : "node");
        group = find_group(groups, group_count, name);
        if (!group) continue;
        group->action_count = node->action_count > group->action_count
            ? node->action_count : group->action_count;
        for (uint32_t class_index = 0u; class_index < class_count; ++class_index)
        {
            group->infosets++;
            group->weight += 1.0;
            for (int action = 0; action < node->action_count; ++action)
                group->actions[action] +=
                    (double)stored->bytes[class_index * (uint32_t)node->action_count +
                                           (uint32_t)action] / 256.0;
        }
    }
    free(node_of_slot);
    pe_monker_mkr_strategy_free(&strategy);
    pe_monker_mkr_free(&archive);
    mpf_tree_free(tree);
    return *group_count > 0u ? 0 : -1;
}

int main(int argc, char **argv)
{
    const char *solution = NULL, *metadata_path = NULL, *json_path = NULL, *html_path = NULL;
    const char *monker_tree = NULL, *monker_mkr = NULL, *monker_entry = NULL;
    const char *decode_key_text = NULL;
    const char *aggregation = "board";
    unsigned packed_shift = 0u, packed_cards = 0u;
    int packed_layout = 0;
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
        else if (strcmp(argv[i], "--monker-tree") == 0 && i + 1 < argc) monker_tree = argv[++i];
        else if (strcmp(argv[i], "--monker-mkr") == 0 && i + 1 < argc) monker_mkr = argv[++i];
        else if (strcmp(argv[i], "--entry") == 0 && i + 1 < argc) monker_entry = argv[++i];
        else if (strcmp(argv[i], "--aggregate") == 0 && i + 1 < argc)
            aggregation = argv[++i];
        else if (strcmp(argv[i], "--decode-key") == 0 && i + 1 < argc) decode_key_text = argv[++i];
        else if (strcmp(argv[i], "--packed-board-shift") == 0 && i + 1 < argc)
        {
            packed_shift = (unsigned)strtoul(argv[++i], NULL, 0);
            packed_layout = 1;
        }
        else if (strcmp(argv[i], "--packed-board-cards") == 0 && i + 1 < argc)
        {
            packed_cards = (unsigned)strtoul(argv[++i], NULL, 0);
            packed_layout = 1;
        }
        else { usage(argv[0]); return 2; }
    }
    if (strcmp(aggregation, "board") != 0 && strcmp(aggregation, "flop") != 0 &&
        strcmp(aggregation, "runout") != 0)
    {
        usage(argv[0]);
        return 2;
    }
    if (decode_key_text)
    {
        uint64_t key = strtoull(decode_key_text, NULL, 0);
        mask_t board = MASK_EMPTY;
        pe_monker_key_status_t status = packed_layout
            ? pe_monker_key_decode_packed_board(key, packed_shift, packed_cards, &board)
            : pe_monker_key_decode_board(key, &board);
        if (status == PE_MONKER_KEY_OK)
        {
            fputs("{\"schema\":\"pe-monker-key/v1\",\"key\":\"", stdout);
            fputs(decode_key_text, stdout);
            printf("\",\"decodable\":true,\"board_mask\":\"0x%016" PRIx64
                   "\",\"status\":\"%s\"}\n", (uint64_t)board,
                   pe_monker_key_status_string(status));
        }
        else
        {
            fputs("{\"schema\":\"pe-monker-key/v1\",\"key\":\"", stdout);
            fputs(decode_key_text, stdout);
            printf("\",\"decodable\":false,\"board_mask\":null,"
                   "\"status\":\"%s\"}\n",
                   pe_monker_key_status_string(status));
        }
        return status == PE_MONKER_KEY_OK ? 0 : 1;
    }
    if ((!solution && (!monker_tree || !monker_mkr)) ||
        (solution && (monker_tree || monker_mkr)) ||
        (solution && load_metadata(metadata_path, &metadata, &metadata_count) != 0))
    {
        fprintf(stderr, "Use --solution FILE or --monker-tree FILE --monker-mkr FILE\n");
        free(metadata); return 1;
    }
    groups = (report_group_t *)calloc(PE_REPORT_MAX_GROUPS, sizeof(*groups));
    if (!groups) { free(metadata); return 1; }
    if (monker_tree)
    {
        if (load_monker_groups(monker_tree, monker_mkr, monker_entry,
                               groups, &group_count) != 0)
        {
            fprintf(stderr, "Unable to decode Monker tree/strategy\n");
            free(groups); return 1;
        }
    }
    else if (pe_sol_open_mmap(solution, &view) != 0)
    {
        fprintf(stderr, "Unable to open solution: %s\n", strerror(errno));
        free(groups); free(metadata); return 1;
    }
    for (size_t i = 0u; solution && i < pe_sol_mmap_infoset_count(view); ++i)
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
        if (meta)
        {
            const char *dimension = meta->board;
            if (strcmp(aggregation, "flop") == 0 && meta->flop[0])
                dimension = meta->flop;
            else if (strcmp(aggregation, "runout") == 0 && meta->runout[0])
                dimension = meta->runout;
            snprintf(name, sizeof(name), "%s/%s", meta->street, dimension);
        }
        else snprintf(name, sizeof(name), "unknown/unknown");
        group = find_group(groups, &group_count, name);
        if (!group) continue;
        group->infosets++;
        group->weight += meta ? meta->weight : 1.0;
        if (actions > group->action_count) group->action_count = actions;
        for (int a = 0; a < actions; ++a) group->actions[a] += probs[a] * (meta ? meta->weight : 1.0);
    }
    if (solution) pe_sol_close_mmap(view);
    free(metadata);
    if (json_path)
    {
        out = fopen(json_path, "w");
        if (!out) { free(groups); return 1; }
        write_json(out, groups, group_count, aggregation); fclose(out);
    }
    else write_json(stdout, groups, group_count, aggregation);
    if (html_path)
    {
        out = fopen(html_path, "w");
        if (!out) { free(groups); return 1; }
        write_html(out, groups, group_count, aggregation); fputc('\n', out); fclose(out);
    }
    free(groups);
    return 0;
}
