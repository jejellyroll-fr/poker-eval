/* Validate a Monker .tree/.mkr pair and emit a machine-readable audit. */
#include <poker_eval/solver/pe_monker.h>
#include <poker_eval/engine/solvers/cfr/mpf_tree.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *program)
{
    fprintf(stderr, "usage: %s --tree FILE [--mkr FILE] [--strategy NAME] "
                    "[--expected-classes N] [--json FILE] [--tree-json FILE|-]\n"
                    "       --tree-json exports the tree topology as mpf JSON "
                    "and skips the strategy audit when --mkr is absent\n",
            program);
}

static const char *game_name(enum_game_t game)
{
    if (game == game_holdem) return "holdem";
    if (game == game_omaha) return "plo4";
    if (game == game_omaha5) return "plo5";
    if (game == game_omaha6) return "plo6";
    return "unknown";
}

static size_t decision_nodes(const mpf_tree_def_t *tree)
{
    size_t count = 0u;
    if (!tree) return 0u;
    for (int i = 0; i < tree->node_count; ++i)
        if (tree->nodes[i].action_count > 0) ++count;
    return count;
}

static const char *find_strategy(const pe_monker_mkr_t *archive)
{
    if (!archive) return NULL;
    for (size_t i = 0u; i < archive->count; ++i)
        if (strncmp(archive->entries[i].name, "storedstrategy", 14u) == 0)
            return archive->entries[i].name;
    return NULL;
}

int main(int argc, char **argv)
{
    const char *tree_path = NULL;
    const char *mkr_path = NULL;
    const char *strategy_name = NULL;
    const char *json_path = NULL;
    const char *tree_json_path = NULL;
    uint32_t expected_classes = 0u;
    int have_expected = 0;
    mpf_tree_def_t *tree = NULL;
    pe_monker_mkr_t archive;
    pe_monker_mkr_strategy_t strategy;
    pe_monker_tree_header_t header;
    pe_monker_range_set_t ranges;
    pe_monker_mkr_metadata_t metadata;
    pe_monker_combo_layout_t layout;
    mpf_tree_error_t tree_error;
    uint32_t class_count = 0u;
    size_t decisions;
    const char *selected_strategy;
    int binding_ok = 0;
    int iscount_ok = 0;
    int metadata_ok = 0;
    FILE *json = NULL;
    int result = 1;

    memset(&archive, 0, sizeof(archive));
    memset(&strategy, 0, sizeof(strategy));
    memset(&ranges, 0, sizeof(ranges));
    memset(&metadata, 0, sizeof(metadata));
    memset(&tree_error, 0, sizeof(tree_error));
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--tree") == 0 && i + 1 < argc) tree_path = argv[++i];
        else if (strcmp(argv[i], "--mkr") == 0 && i + 1 < argc) mkr_path = argv[++i];
        else if (strcmp(argv[i], "--strategy") == 0 && i + 1 < argc) strategy_name = argv[++i];
        else if (strcmp(argv[i], "--expected-classes") == 0 && i + 1 < argc) {
            expected_classes = (uint32_t)strtoul(argv[++i], NULL, 10);
            have_expected = 1;
        }
        else if (strcmp(argv[i], "--json") == 0 && i + 1 < argc) json_path = argv[++i];
        else if (strcmp(argv[i], "--tree-json") == 0 && i + 1 < argc) tree_json_path = argv[++i];
        else { usage(argv[0]); return 2; }
    }
    if (!tree_path || (!mkr_path && !tree_json_path)) { usage(argv[0]); return 2; }

    if (pe_monker_tree_read_header(tree_path, &header) != PE_MONKER_OK ||
        pe_monker_tree_load(tree_path, &tree) != PE_MONKER_OK || !tree) {
        fprintf(stderr, "cannot load Monker tree %s\n", tree_path);
        goto done;
    }
    if (tree_json_path && !mkr_path) {
        /* Topology-only export: no strategy audit without an archive. */
        size_t json_length = 0u;
        char *json_text = mpf_tree_serialize_json(tree, &json_length);
        FILE *tree_out = strcmp(tree_json_path, "-") == 0
                             ? stdout
                             : fopen(tree_json_path, "w");
        if (!json_text || !tree_out ||
            fwrite(json_text, 1u, json_length, tree_out) != json_length) {
            fprintf(stderr, "cannot export tree JSON\n");
            free(json_text);
            if (tree_out && tree_out != stdout) fclose(tree_out);
            goto done;
        }
        free(json_text);
        if (tree_out != stdout) fclose(tree_out);
        result = 0;
        goto done;
    }
    if (pe_monker_tree_read_ranges(tree_path, &ranges) != PE_MONKER_OK ||
        pe_monker_mkr_read(mkr_path, &archive) != PE_MONKER_MKR_OK) {
        fprintf(stderr, "cannot load Monker ranges or archive\n");
        goto done;
    }
    selected_strategy = strategy_name ? strategy_name : find_strategy(&archive);
    if (!selected_strategy ||
        pe_monker_mkr_read_strategy(&archive, selected_strategy, &strategy) != PE_MONKER_MKR_OK) {
        fprintf(stderr, "cannot read a stored strategy entry\n");
        goto done;
    }
    if (pe_monker_combo_layout_from_count(ranges.combo_count, &layout) != PE_MONKER_OK) {
        fprintf(stderr, "unsupported Monker combo count: %u\n", ranges.combo_count);
        goto done;
    }
    decisions = decision_nodes(tree);
    /* The public binder requires storage for the mapping. Allocate it after the
       cheap shape check so a malformed archive cannot cause an oversized read. */
    if (strategy.slot_count > 0u) {
        int32_t *binding = (int32_t *)calloc(strategy.slot_count, sizeof(*binding));
        if (binding) {
            binding_ok = pe_monker_mkr_bind_strategy(tree, &strategy, binding,
                                                     strategy.slot_count) == PE_MONKER_MKR_OK;
            free(binding);
        }
    }
    if (pe_monker_mkr_strategy_class_count(tree, &strategy, &class_count) != PE_MONKER_MKR_OK)
        goto done;
    if (have_expected && class_count != expected_classes) goto report;
    iscount_ok = !metadata_ok;
    if (pe_monker_mkr_read_metadata(&archive, &metadata) == PE_MONKER_MKR_OK) {
        metadata_ok = 1;
        iscount_ok = metadata.iscount <= 0 ||
                     metadata.iscount == (int64_t)(decisions * (size_t)class_count);
    }
report:
    json = json_path ? fopen(json_path, "w") : stdout;
    if (!json) goto done;
    fprintf(json, "{\"schema\":\"pe-monker-validation/v1\",\"game\":\"%s\","
                 "\"tree_nodes\":%d,\"decision_nodes\":%zu,\"range_combo_count\":%u,"
                 "\"strategy\":\"%s\",\"class_count\":%u,\"binding_ok\":%s,"
                 "\"metadata_ok\":%s,\"iscount_ok\":%s,\"codec_status\":\"%s\"}\n",
            game_name(layout.game), tree->node_count, decisions, ranges.combo_count,
            selected_strategy, class_count, binding_ok ? "true" : "false",
            metadata_ok ? "true" : "false", iscount_ok ? "true" : "false",
            layout.game == game_omaha ? "exact-plo4" : "structural-only-reference-required");
    if (json_path) { fclose(json); json = NULL; }
    result = binding_ok && iscount_ok && (!have_expected || class_count == expected_classes) ? 0 : 1;
done:
    pe_monker_mkr_metadata_free(&metadata);
    pe_monker_mkr_strategy_free(&strategy);
    pe_monker_mkr_free(&archive);
    pe_monker_range_set_free(&ranges);
    mpf_tree_free(tree);
    if (json && json != stdout) fclose(json);
    return result;
}
