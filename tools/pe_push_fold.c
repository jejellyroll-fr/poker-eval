#include <poker_eval/economics/push_fold.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *program)
{
    fprintf(stderr, "usage: %s --pot N --hero-stack N --villain-stack N "
                    "--equity P [--iterations N] [--json FILE]\n", program);
}

int main(int argc, char **argv)
{
    pe_push_fold_input_t input = {0};
    pe_push_fold_result_t result;
    const char *json_path = NULL;
    FILE *out = stdout;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--pot") == 0 && i + 1 < argc) input.pot_before_push = strtod(argv[++i], NULL);
        else if (strcmp(argv[i], "--hero-stack") == 0 && i + 1 < argc) input.hero_stack = strtod(argv[++i], NULL);
        else if (strcmp(argv[i], "--villain-stack") == 0 && i + 1 < argc) input.villain_stack = strtod(argv[++i], NULL);
        else if (strcmp(argv[i], "--equity") == 0 && i + 1 < argc) input.hero_equity_when_called = strtod(argv[++i], NULL);
        else if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) input.iterations = atoi(argv[++i]);
        else if (strcmp(argv[i], "--json") == 0 && i + 1 < argc) json_path = argv[++i];
        else { usage(argv[0]); return 2; }
    }
    if (pe_push_fold_solve(&input, &result) != 0) { usage(argv[0]); return 2; }
    if (json_path) out = fopen(json_path, "w");
    if (!out) { fprintf(stderr, "cannot open %s\n", json_path); return 1; }
    fprintf(out, "{\"schema\":\"pe-push-fold/v1\",\"hero_push_frequency\":%.9g,"
                 "\"villain_call_frequency\":%.9g,\"hero_ev\":%.9g,"
                 "\"exploitability\":%.9g,\"iterations\":%d}\n",
            result.hero_push_frequency, result.villain_call_frequency,
            result.hero_ev, result.exploitability, result.iterations);
    if (json_path) fclose(out);
    return 0;
}
