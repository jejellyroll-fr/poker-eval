/* Generate a bounded, machine-readable preflop betting tree. */
#include <poker_eval/solver/pe_actions.h>
#include <poker_eval/solver/pe_betting_state.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TREE_ACTIONS 16u
#define MAX_TREE_NODES 200000u

typedef struct {
    FILE *out;
    pe_betting_rules_t rules;
    size_t node_count;
    size_t emitted;
    double raise_sizes[MAX_TREE_ACTIONS];
    size_t raise_count;
    char ranges[PE_BETTING_MAX_PLAYERS][256];
} tree_context_t;

static void json_string(FILE *out, const char *s)
{
    fputc('"', out);
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p)
    {
        if (*p == '"' || *p == '\\') { fputc('\\', out); fputc(*p, out); }
        else if (*p < 32u) fputc(' ', out);
        else fputc(*p, out);
    }
    fputc('"', out);
}

static size_t build_actions(const tree_context_t *ctx, const pe_betting_state_t *state,
                            pe_action_t *actions, const char **labels)
{
    size_t n = 0u;
    if (state->to_call > ctx->rules.epsilon)
    {
        actions[n] = (pe_action_t){PE_ACTION_FOLD, PE_AMOUNT_NONE, 0.0, 0}; labels[n++] = "fold";
        actions[n] = (pe_action_t){PE_ACTION_CALL, PE_AMOUNT_NONE, 0.0, 0}; labels[n++] = "call";
        for (size_t i = 0u; i < ctx->raise_count && n < MAX_TREE_ACTIONS; ++i)
        {
            actions[n] = (pe_action_t){PE_ACTION_RAISE, PE_AMOUNT_CHIPS, ctx->raise_sizes[i], (int)i};
            labels[n++] = "raise";
        }
    }
    else
    {
        actions[n] = (pe_action_t){PE_ACTION_CHECK, PE_AMOUNT_NONE, 0.0, 0}; labels[n++] = "check";
        for (size_t i = 0u; i < ctx->raise_count && n < MAX_TREE_ACTIONS; ++i)
        {
            actions[n] = (pe_action_t){PE_ACTION_BET, PE_AMOUNT_CHIPS, ctx->raise_sizes[i], (int)i};
            labels[n++] = "bet";
        }
    }
    return n;
}

static int emit_node(tree_context_t *ctx, size_t id, const pe_betting_state_t *state)
{
    pe_action_t actions[MAX_TREE_ACTIONS];
    const char *labels[MAX_TREE_ACTIONS];
    pe_betting_state_t children[MAX_TREE_ACTIONS];
    size_t child_ids[MAX_TREE_ACTIONS];
    size_t count = 0u;
    size_t n = 0u;
    if (ctx->node_count > MAX_TREE_NODES) return -1;
    if (ctx->emitted++ != 0u) fputc(',', ctx->out);
    fprintf(ctx->out, "{\"id\":%zu,\"type\":\"%s\",\"player\":%d,\"pot\":%.6g,\"to_call\":%.6g,\"actions\":[",
            id, state->terminal || state->round_complete ? "terminal" : "decision",
            state->to_act, state->pot, state->to_call);
    if (state->terminal || state->round_complete)
    {
        fputs("]}", ctx->out);
        return 0;
    }
    n = build_actions(ctx, state, actions, labels);
    for (size_t i = 0u; i < n; ++i)
    {
        if (pe_betting_apply_action(state, &ctx->rules, &actions[i], &children[count]) != PE_BETTING_OK)
            continue;
        child_ids[count] = ctx->node_count++;
        if (count++ != 0u) fputc(',', ctx->out);
        fputs("{\"label\":", ctx->out); json_string(ctx->out, labels[i]);
        fprintf(ctx->out, ",\"amount\":%.6g,\"child\":%zu}", actions[i].amount, child_ids[count - 1u]);
    }
    fputs("]}", ctx->out);
    for (size_t i = 0u; i < count; ++i)
        if (emit_node(ctx, child_ids[i], &children[i]) != 0) return -1;
    return 0;
}

static void usage(const char *program)
{
    fprintf(stderr, "usage: %s --players N --stack BB [--stack BB ...] --raises a,b,c\n"
                    "       [--first-to-act N] [--pot BB] [--to-call BB] [--range PLAYER TEXT]\n"
                    "       --output FILE\n", program);
}

int main(int argc, char **argv)
{
    tree_context_t ctx;
    pe_betting_state_t root;
    double stacks[PE_BETTING_MAX_PLAYERS] = {0.0};
    uint8_t players = 0u;
    int first = 0;
    double pot = 0.0;
    double to_call = 0.0;
    const char *output_path = NULL;
    int stack_count = 0;
    int i;
    memset(&ctx, 0, sizeof(ctx));
    for (i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--players") == 0 && i + 1 < argc) players = (uint8_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "--stack") == 0 && i + 1 < argc && stack_count < (int)PE_BETTING_MAX_PLAYERS)
            stacks[stack_count++] = strtod(argv[++i], NULL);
        else if (strcmp(argv[i], "--first-to-act") == 0 && i + 1 < argc) first = atoi(argv[++i]);
        else if (strcmp(argv[i], "--pot") == 0 && i + 1 < argc) pot = strtod(argv[++i], NULL);
        else if (strcmp(argv[i], "--to-call") == 0 && i + 1 < argc) to_call = strtod(argv[++i], NULL);
        else if (strcmp(argv[i], "--raises") == 0 && i + 1 < argc)
        {
            const char *source = argv[++i];
            char *copy = (char *)malloc(strlen(source) + 1u);
            if (copy) strcpy(copy, source);
            char *part = copy ? strtok(copy, ",") : NULL;
            while (part && ctx.raise_count < MAX_TREE_ACTIONS) { ctx.raise_sizes[ctx.raise_count++] = strtod(part, NULL); part = strtok(NULL, ","); }
            free(copy);
        }
        else if (strcmp(argv[i], "--range") == 0 && i + 2 < argc)
        {
            int player = atoi(argv[++i]);
            if (player >= 0 && player < (int)PE_BETTING_MAX_PLAYERS) snprintf(ctx.ranges[player], sizeof(ctx.ranges[player]), "%s", argv[++i]);
            else ++i;
        }
        else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) output_path = argv[++i];
        else { usage(argv[0]); return 2; }
    }
    pe_betting_rules_default(&ctx.rules, players);
    if (players < 2u || players > PE_BETTING_MAX_PLAYERS || stack_count != (int)players ||
        first < 0 || first >= (int)players || !output_path || ctx.raise_count == 0u ||
        pe_betting_state_init(&root, &ctx.rules, stacks, players, first, pot, to_call) != PE_BETTING_OK)
    {
        usage(argv[0]); return 2;
    }
    ctx.out = fopen(output_path, "w");
    if (!ctx.out) { fprintf(stderr, "cannot open %s\n", output_path); return 1; }
    fputs("{\"schema\":\"pe-preflop-tree/v1\",\"players\":", ctx.out);
    fprintf(ctx.out, "%u,\"ranges\":[", players);
    for (i = 0; i < (int)players; ++i) { if (i) fputc(',', ctx.out); json_string(ctx.out, ctx.ranges[i]); }
    fputs("],\"nodes\":[", ctx.out);
    ctx.node_count = 1u;
    if (emit_node(&ctx, 0u, &root) != 0) { fclose(ctx.out); return 1; }
    fputs("]}\n", ctx.out);
    fclose(ctx.out);
    fprintf(stderr, "generated %zu nodes in %s\n", ctx.node_count, output_path);
    return 0;
}
