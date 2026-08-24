/* Generate a bounded, machine-readable preflop betting tree. */
#include <poker_eval/solver/pe_actions.h>
#include <poker_eval/solver/pe_betting_state.h>
#include <poker_eval/solver/pe_range.h>
#include <poker_eval/core/enumdefs.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TREE_ACTIONS 16u
#define MAX_TREE_NODES 200000u

typedef struct {
    FILE *out;
    pe_betting_rules_t rules;
    enum_game_t game;
    size_t node_count;
    size_t emitted;
    size_t max_combos;
    double raise_sizes[MAX_TREE_ACTIONS];
    size_t raise_count;
    char ranges[PE_BETTING_MAX_PLAYERS][256];
    pe_range_t *compiled_ranges[PE_BETTING_MAX_PLAYERS];
} tree_context_t;

static void json_string(FILE *out, const char *s);

static const char *game_name(enum_game_t game)
{
    if (game == game_holdem) return "holdem";
    if (game == game_omaha) return "plo4";
    if (game == game_omaha5) return "plo5";
    if (game == game_omaha6) return "plo6";
    return "unknown";
}

static int parse_game(const char *name, enum_game_t *out)
{
    if (!name || !out) return 0;
    if (strcmp(name, "holdem") == 0) *out = game_holdem;
    else if (strcmp(name, "plo4") == 0) *out = game_omaha;
    else if (strcmp(name, "plo5") == 0) *out = game_omaha5;
    else if (strcmp(name, "plo6") == 0) *out = game_omaha6;
    else return 0;
    return 1;
}

static int mask_to_hand(StdDeck_CardMask mask, char *out, size_t capacity)
{
    static const char ranks[] = "23456789TJQKA";
    static const char suits[] = "hdcs";
    size_t used = 0u;
    int cards = 0;
    for (int card = 0; card < StdDeck_N_CARDS; ++card) {
        if (!StdDeck_CardMask_CARD_IS_SET(mask, card)) continue;
        if (used + 2u >= capacity) return 0;
        out[used++] = ranks[StdDeck_RANK(card)];
        out[used++] = suits[StdDeck_SUIT(card)];
        ++cards;
    }
    if (used >= capacity) return 0;
    out[used] = '\0';
    return cards;
}

static int compile_ranges(tree_context_t *ctx)
{
    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);
    for (int player = 0; player < (int)PE_BETTING_MAX_PLAYERS; ++player) {
        const char *range_text;
        if (!ctx->ranges[player][0]) continue;
        /* Keep the historical CLI spelling while compiling it to the real
           complete range understood by the range adapter. */
        range_text = strcmp(ctx->ranges[player], "random") == 0 ? "100%" : ctx->ranges[player];
        if (pe_solver_range_parse(ctx->game, range_text, dead,
                                  &ctx->compiled_ranges[player]) != PE_SOLVER_OK ||
            !ctx->compiled_ranges[player]) {
            fprintf(stderr, "invalid range for player %d: %s\n", player,
                    ctx->ranges[player]);
            return 0;
        }
        if (ctx->compiled_ranges[player]->count > ctx->max_combos) {
            fprintf(stderr, "range for player %d has %zu combos; increase --max-combos (currently %zu)\n",
                    player, ctx->compiled_ranges[player]->count, ctx->max_combos);
            return 0;
        }
    }
    return 1;
}

static void free_ranges(tree_context_t *ctx)
{
    for (int player = 0; player < (int)PE_BETTING_MAX_PLAYERS; ++player) {
        pe_range_free(ctx->compiled_ranges[player]);
        ctx->compiled_ranges[player] = NULL;
    }
}

static int emit_range_profiles(tree_context_t *ctx)
{
    fputs(",\"rangeProfiles\":[", ctx->out);
    int emitted = 0;
    for (int player = 0; player < (int)PE_BETTING_MAX_PLAYERS; ++player) {
        const pe_range_t *range = ctx->compiled_ranges[player];
        if (!range) continue;
        if (emitted++) fputc(',', ctx->out);
        fprintf(ctx->out, "{\"id\":\"player%d-preflop\",\"player\":%d,\"street\":\"preflop\",\"combos\":[",
                player, player);
        for (size_t combo = 0u; combo < range->count; ++combo) {
            char hand[32];
            if (combo) fputc(',', ctx->out);
            if (!mask_to_hand(range->combos[combo].hand, hand, sizeof(hand))) return 0;
            fputs("{\"hand\":", ctx->out); json_string(ctx->out, hand);
            fprintf(ctx->out, ",\"weight\":%.17g}", range->combos[combo].weight);
        }
        fputs("]}", ctx->out);
    }
    fputs("]", ctx->out);
    return 1;
}

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
                    "       [--game holdem|plo4|plo5|plo6] [--max-combos N]\n"
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
    ctx.game = game_holdem;
    ctx.max_combos = 200000u;
    for (i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--players") == 0 && i + 1 < argc) players = (uint8_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "--game") == 0 && i + 1 < argc && !parse_game(argv[++i], &ctx.game)) { usage(argv[0]); return 2; }
        else if (strcmp(argv[i], "--max-combos") == 0 && i + 1 < argc) ctx.max_combos = (size_t)strtoull(argv[++i], NULL, 10);
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
    if (!compile_ranges(&ctx)) { free_ranges(&ctx); return 1; }
    ctx.out = fopen(output_path, "w");
    if (!ctx.out) { fprintf(stderr, "cannot open %s\n", output_path); free_ranges(&ctx); return 1; }
    fputs("{\"schema\":\"pe-preflop-tree/v2\",\"game\":", ctx.out);
    json_string(ctx.out, game_name(ctx.game));
    fputs(",\"players\":", ctx.out);
    fprintf(ctx.out, "%u,\"ranges\":[", players);
    for (i = 0; i < (int)players; ++i) { if (i) fputc(',', ctx.out); json_string(ctx.out, ctx.ranges[i]); }
    fputs("],\"nodes\":[", ctx.out);
    ctx.node_count = 1u;
    if (emit_node(&ctx, 0u, &root) != 0) { fclose(ctx.out); free_ranges(&ctx); return 1; }
    /* Keep the range profiles beside the tree nodes. MPF's parser accepts the
       same profile shape, so generated ranges can be consumed without a
       second hand-written import step. */
    fputs("]", ctx.out);
    if (!emit_range_profiles(&ctx)) { fclose(ctx.out); free_ranges(&ctx); return 1; }
    fputs("}\n", ctx.out);
    fclose(ctx.out);
    free_ranges(&ctx);
    fprintf(stderr, "generated %zu nodes in %s\n", ctx.node_count, output_path);
    return 0;
}
