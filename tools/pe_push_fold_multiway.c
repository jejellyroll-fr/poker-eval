#include <poker_eval/economics/push_fold_multiway.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_values(const char *text, double *values, int max_values)
{
    char buffer[2048], *cursor;
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

static void usage(const char *program)
{
    fprintf(stderr, "usage: %s --pot N --hero-stack N --villain-stacks a,b "
                    "--equities mask0,mask1,... [--iterations N]\n"
                    "       equities are ordered by caller bitmask (2^villains values)\n", program);
}

int main(int argc, char **argv)
{
    const char *stacks_text = NULL, *equities_text = NULL;
    pe_push_fold_multiway_input_t input = {0};
    pe_push_fold_multiway_result_t result;
    double stacks[PE_PUSH_FOLD_MAX_VILLAINS], equities[PE_PUSH_FOLD_MAX_PROFILES];
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--pot") == 0 && i + 1 < argc) input.pot_before_push = strtod(argv[++i], NULL);
        else if (strcmp(argv[i], "--hero-stack") == 0 && i + 1 < argc) input.hero_stack = strtod(argv[++i], NULL);
        else if (strcmp(argv[i], "--villain-stacks") == 0 && i + 1 < argc) stacks_text = argv[++i];
        else if (strcmp(argv[i], "--equities") == 0 && i + 1 < argc) equities_text = argv[++i];
        else if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) input.iterations = atoi(argv[++i]);
        else { usage(argv[0]); return 2; }
    }
    input.num_villains = parse_values(stacks_text, stacks, PE_PUSH_FOLD_MAX_VILLAINS);
    if (input.num_villains <= 0 || input.num_villains > PE_PUSH_FOLD_MAX_VILLAINS ||
        parse_values(equities_text, equities, 1 << input.num_villains) != (1 << input.num_villains)) {
        usage(argv[0]); return 2;
    }
    for (int i = 0; i < input.num_villains; ++i) input.villain_stacks[i] = stacks[i];
    for (int mask = 0; mask < (1 << input.num_villains); ++mask)
        input.hero_equity_by_call_mask[mask] = equities[mask];
    if (pe_push_fold_multiway_solve(&input, &result) != 0) return 1;
    printf("{\"schema\":\"pe-push-fold-multiway/v1\",\"hero_push_frequency\":%.9g,\"hero_ev\":%.9g,\"exploitability\":%.9g,\"villain_call_frequency\":[",
           result.hero_push_frequency, result.hero_ev, result.exploitability);
    for (int i = 0; i < input.num_villains; ++i) {
        if (i) putchar(',');
        printf("%.9g", result.villain_call_frequency[i]);
    }
    printf("],\"iterations\":%d}\n", result.iterations);
    return 0;
}
