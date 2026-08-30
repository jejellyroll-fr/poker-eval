#include <poker_eval/economics/fgs.h>
#include <poker_eval/utils/icm_calculator.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_values(const char *text, double *values, int max_values)
{
    char buffer[2048];
    char *cursor;
    int count = 0;
    if (!text || strnlen(text, sizeof(buffer)) >= sizeof(buffer)) return -1;
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

static int load_scenarios(const char *path, icm_tournament_t *scenarios,
                          double *probabilities, int players, int *count)
{
    FILE *file = fopen(path, "r");
    char line[2048];
    int used = 0;
    if (!file) return -1;
    while (fgets(line, sizeof(line), file) != NULL && used < 256) {
        double values[ICM_MAX_PLAYERS + 1];
        int n = parse_values(line, values, players + 1);
        if (n <= 0) continue;
        if (n != players + 1) { fclose(file); return -1; }
        if (!isfinite(values[0]) || values[0] < 0.0 || values[0] > 1.0) {
            fclose(file);
            return -1;
        }
        icm_tournament_init(&scenarios[used]);
        scenarios[used].num_active_players = players;
        for (int p = 0; p < players; ++p) {
            scenarios[used].players[p].chips = values[p + 1] < 0.0 ? 0u : (uint64_t)values[p + 1];
            scenarios[used].players[p].is_active = scenarios[used].players[p].chips > 0u;
        }
        probabilities[used++] = values[0];
    }
    fclose(file);
    *count = used;
    return used > 0 ? 0 : -1;
}

static void usage(const char *program)
{
    fprintf(stderr, "usage: %s --stacks 100,50 --payouts 70,30 --scenarios FILE [--json FILE]\n"
                    "       scenarios: probability,stack0,stack1,...\n"
                    "       %s --stacks 100,50 --payouts 70,30 --generate --win 0.5,0.5\n"
                    "              --pot 20 --depth 2 [--json FILE]\n", program, program);
}

int main(int argc, char **argv)
{
    const char *stacks_text = NULL, *payouts_text = NULL, *scenario_path = NULL, *json_path = NULL;
    const char *win_text = NULL;
    double stacks[ICM_MAX_PLAYERS], payouts[ICM_MAX_PLAYERS];
    double pot = 0.0;
    int depth = 0;
    int generate = 0;
    static icm_tournament_t base, scenarios[256];
    icm_payout_structure_t payout = {0};
    icm_result_t result = {0};
    double probabilities[256];
    int players, payout_count, scenario_count;
    FILE *out = stdout;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--stacks") == 0 && i + 1 < argc) stacks_text = argv[++i];
        else if (strcmp(argv[i], "--payouts") == 0 && i + 1 < argc) payouts_text = argv[++i];
        else if (strcmp(argv[i], "--scenarios") == 0 && i + 1 < argc) scenario_path = argv[++i];
        else if (strcmp(argv[i], "--json") == 0 && i + 1 < argc) json_path = argv[++i];
        else if (strcmp(argv[i], "--generate") == 0) generate = 1;
        else if (strcmp(argv[i], "--win") == 0 && i + 1 < argc) win_text = argv[++i];
        else if (strcmp(argv[i], "--pot") == 0 && i + 1 < argc) pot = strtod(argv[++i], NULL);
        else if (strcmp(argv[i], "--depth") == 0 && i + 1 < argc) depth = atoi(argv[++i]);
        else { usage(argv[0]); return 2; }
    }
    players = parse_values(stacks_text, stacks, ICM_MAX_PLAYERS);
    payout_count = parse_values(payouts_text, payouts, ICM_MAX_PLAYERS);
    if (players <= 0 || payout_count <= 0 || payout_count > players ||
        (!generate && !scenario_path)) {
        usage(argv[0]); return 2;
    }
    if (generate) {
        static pe_fgs_node_t nodes[4096];
        static pe_fgs_edge_t edges[16384];
        pe_fgs_scenario_input_t input;
        pe_fgs_tree_t tree;
        pe_fgs_result_t generated;
        memset(&input, 0, sizeof(input));
        input.num_players = players;
        input.num_payouts = payout_count;
        for (int p = 0; p < players; ++p) input.stacks[p] = stacks[p];
        for (int p = 0; p < payout_count; ++p) input.payouts[p] = payouts[p];
        if (parse_values(win_text, input.win_probability, ICM_MAX_PLAYERS) != players) {
            fprintf(stderr, "--win needs one probability per player\n"); return 2;
        }
        input.pot = pot;
        input.depth = depth;
        if (pe_fgs_generate_even_contribution(&input, nodes, 4096, edges, 16384, &tree) != 0) {
            fprintf(stderr, "FGS scenario generation failed\n"); return 1;
        }
        if (pe_fgs_calculate_tree(&tree, &generated) != 0) {
            fprintf(stderr, "FGS calculation failed on generated tree\n"); return 1;
        }
        if (json_path) out = fopen(json_path, "w");
        if (!out) { fprintf(stderr, "cannot open %s\n", json_path); return 1; }
        fprintf(out, "{\"schema\":\"pe-fgs-generated/v1\",\"leaves\":%zu,"
                     "\"probability\":%.9g,\"players\":[", generated.leaf_count,
                generated.probability);
        for (int p = 0; p < players; ++p) {
            if (p) fputc(',', out);
            fprintf(out, "{\"index\":%d,\"equity\":%.9g}", p, generated.ev[p]);
        }
        fputs("]}\n", out);
        if (json_path) fclose(out);
        return 0;
    }
    icm_tournament_init(&base);
    base.num_active_players = players;
    for (int p = 0; p < players; ++p) {
        base.players[p].chips = stacks[p] < 0.0 ? 0u : (uint64_t)stacks[p];
        base.players[p].is_active = base.players[p].chips > 0u;
    }
    payout.num_paid_positions = payout_count;
    payout.total_prize_pool = 1.0;
    for (int p = 0; p < payout_count; ++p) payout.payouts[p] = payouts[p];
    {
        double payout_sum = 0.0;
        for (int p = 0; p < payout_count; ++p) payout_sum += payout.payouts[p];
        if (!(payout_sum > 0.0)) { fprintf(stderr, "invalid payout structure\n"); return 2; }
        for (int p = 0; p < payout_count; ++p) payout.payouts[p] /= payout_sum;
    }
    base.payout_structure = payout;
    for (int s = 0; s < 256; ++s) scenarios[s].payout_structure = payout;
    if (load_scenarios(scenario_path, scenarios, probabilities, players, &scenario_count) != 0) {
        fprintf(stderr, "invalid scenario file\n"); return 1;
    }
    for (int s = 0; s < scenario_count; ++s) scenarios[s].payout_structure = payout;
    {
        icm_error_t fgs_error = icm_calculate_future_scenarios(&base, scenarios,
                                                               scenario_count, probabilities, &result);
        if (fgs_error != ICM_SUCCESS) {
            fprintf(stderr, "FGS calculation failed: %s\n", icm_error_string(fgs_error));
            return 1;
        }
    }
    if (json_path) out = fopen(json_path, "w");
    if (!out) { fprintf(stderr, "cannot open %s\n", json_path); return 1; }
    fprintf(out, "{\"schema\":\"pe-fgs/v1\",\"scenarios\":%d,\"players\":[", scenario_count);
    for (int p = 0; p < players; ++p) {
        if (p) fputc(',', out);
        fprintf(out, "{\"index\":%d,\"equity\":%.9g,\"finish_probability\":[", p, result.equity[p]);
        for (int pos = 0; pos < players; ++pos) {
            if (pos) fputc(',', out);
            fprintf(out, "%.9g", result.finish_probability[p][pos]);
        }
        fputs("]}", out);
    }
    fputs("]}\n", out);
    if (json_path) fclose(out);
    return 0;
}
