#include <poker_eval/utils/icm_calculator.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_values(const char *text, double *values, int max_values)
{
    char buffer[2048];
    char *cursor;
    int count = 0;
    if (!text || strlen(text) >= sizeof(buffer)) return -1;
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
                    "       scenarios: probability,stack0,stack1,...\n", program);
}

int main(int argc, char **argv)
{
    const char *stacks_text = NULL, *payouts_text = NULL, *scenario_path = NULL, *json_path = NULL;
    double stacks[ICM_MAX_PLAYERS], payouts[ICM_MAX_PLAYERS];
    icm_tournament_t base, scenarios[256];
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
        else { usage(argv[0]); return 2; }
    }
    players = parse_values(stacks_text, stacks, ICM_MAX_PLAYERS);
    payout_count = parse_values(payouts_text, payouts, ICM_MAX_PLAYERS);
    if (players <= 0 || payout_count <= 0 || payout_count > players || !scenario_path) {
        usage(argv[0]); return 2;
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
