/* Generate a bounded, machine-readable betting tree rooted at any street.
 *
 * The node schema is mpf_tree's: {"id","type":"player|terminal","street",
 * "player","bet_profile","range_profile","snapshot","actions":[{"type":
 * "fold|call|check|raise","size_index","next"}]}.  That is the schema
 * mpf_run_with_metrics parses, so a generated tree can be solved directly:
 *   mpf_run_with_metrics --tree out.json --rules <game> [--rangeN ...]
 * Betting semantics still come from the generic one-street state machine;
 * the tree it produces is what changed.  The root street is preflop unless
 * --street flop|turn|river says otherwise; a postflop root also requires
 * --board and starts from --pot/--to-call with no blind posting.
 */
#include <poker_eval/solver/pe_actions.h>
#include <poker_eval/solver/pe_betting_state.h>
#include <poker_eval/solver/pe_range.h>
#include <poker_eval/core/enumdefs.h>

#include <ctype.h>
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
    /* Two comma counters: one walks the node array, the other the action
       array of the node being emitted.  Sharing one produced `{,` or `}{`.
       The action counter is reset at the top of emit_node; the node counter
       runs for the whole document. */
    size_t node_emitted;
    size_t action_emitted;
    size_t max_combos;
    double raise_sizes[MAX_TREE_ACTIONS];
    size_t raise_count;
    char ranges[PE_BETTING_MAX_PLAYERS][256];
    /* "100%"/"random": the whole private range.  It has no combo list to
       emit (PLO6 is 20.4M hands), so the profile records that it is complete
       instead of materialising it. */
    int complete[PE_BETTING_MAX_PLAYERS];
    pe_range_t *compiled_ranges[PE_BETTING_MAX_PLAYERS];
    /* Root street: 0 preflop, 1 flop, 2 turn, 3 river.  Postflop roots carry
       a fixed board and their profile ids name the street. */
    int root_street;
    const char *board_text;
    int board_cards[5];
    int board_count;
} tree_context_t;

static const char *street_names[4] = {"PREFLOP", "FLOP", "TURN", "RIVER"};
static const int street_board_counts[4] = {0, 3, 4, 5};

static int street_board_count(int street)
{
    return street < 0 || street > 3 ? 0 : street_board_counts[street];
}

/* mpf card encoding: rank + 13*suit with suits ordered c,d,h,s -- exactly
   modern_cardmask's layout, so the integers written into snapshots are the
   ones mpf_update_board re-masks. */
static const char modern_rank_chars[] = "23456789TJQKA";
static const char modern_suit_chars[] = "cdhs";

static int modern_card_from_text(const char *text)
{
    int rank = -1;
    int suit = -1;
    if (!text || text[0] == '\0' || text[1] == '\0' || text[2] != '\0')
        return -1;
    for (int i = 0; i < 13; ++i)
        if (toupper((unsigned char)text[0]) == modern_rank_chars[i])
            rank = i;
    for (int i = 0; i < 4; ++i)
        if (tolower((unsigned char)text[1]) == modern_suit_chars[i])
            suit = i;
    if (rank < 0 || suit < 0)
        return -1;
    return rank + 13 * suit;
}

static int parse_board_text(const char *text, int out_cards[5], int *out_count)
{
    int count = 0;
    if (!text || !out_cards || !out_count)
        return 0;
    while (*text)
    {
        char token[3];
        int card;
        while (*text == ',' || *text == '/' || isspace((unsigned char)*text))
            ++text;
        if (!*text)
            break;
        if (text[1] == '\0')
            return 0;
        token[0] = text[0];
        token[1] = text[1];
        token[2] = '\0';
        text += 2;
        card = modern_card_from_text(token);
        if (card < 0 || count >= 5)
            return 0;
        for (int i = 0; i < count; ++i)
            if (out_cards[i] == card)
                return 0;
        out_cards[count++] = card;
    }
    *out_count = count;
    return count >= 1;
}

/* The board cards are dead for range parsing, the same way pe-preflop-solve
   deadens --board: a flop pattern must not expand onto cards already on the
   table.  mpf and StdDeck disagree on suit order (cdhs vs hdcs), so the
   shared rank+13*suit layout still needs a per-card translation. */
static StdDeck_CardMask board_dead_mask(const tree_context_t *ctx)
{
    static const int std_suits[4] = {StdDeck_Suit_CLUBS, StdDeck_Suit_DIAMONDS,
                                     StdDeck_Suit_HEARTS, StdDeck_Suit_SPADES};
    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);
    for (int i = 0; i < ctx->board_count; ++i) {
        int rank = ctx->board_cards[i] % 13;
        int suit = ctx->board_cards[i] / 13;
        StdDeck_CardMask_SET(dead, StdDeck_MAKE_CARD(rank, std_suits[suit]));
    }
    return dead;
}

static int parse_street_name(const char *text)
{
    if (!text || !*text) return 0;
    if (strcmp(text, "preflop") == 0) return 0;
    if (strcmp(text, "flop") == 0) return 1;
    if (strcmp(text, "turn") == 0) return 2;
    if (strcmp(text, "river") == 0) return 3;
    return -1;
}

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
    StdDeck_CardMask dead = board_dead_mask(ctx);
    for (int player = 0; player < (int)PE_BETTING_MAX_PLAYERS; ++player) {
        const char *range_text;
        if (!ctx->ranges[player][0]) continue;
        /* The whole range is a profile flag, not a combo list: PLO6 is 20.4M
           hands and cannot be materialised.  "random" is the historical CLI
           spelling of the same thing. */
        if (strcmp(ctx->ranges[player], "100%") == 0 ||
            strcmp(ctx->ranges[player], "random") == 0) {
            ctx->complete[player] = 1;
            continue;
        }
        range_text = ctx->ranges[player];
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
        if (!range && !ctx->complete[player]) continue;
        if (emitted++) fputc(',', ctx->out);
        if (!range) {
            /* Marked complete rather than expanded: the consumer can tell the
               range is every hand, which no combo list could say. */
            fprintf(ctx->out, "{\"id\":\"player%d-%s\",\"player\":%d,"
                    "\"street\":\"%s\",\"complete\":true,\"combos\":[]}",
                    player, street_names[ctx->root_street], player,
                    street_names[ctx->root_street]);
            continue;
        }
        fprintf(ctx->out, "{\"id\":\"player%d-%s\",\"player\":%d,\"street\":\"%s\",\"combos\":[",
                player, street_names[ctx->root_street], player,
                street_names[ctx->root_street]);
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

/* mpf_tree actions carry no amount: a raise names a size_index into the
   node's bet_sizes, which mpf reads as the increment above the call --
   exactly what --raises names and what the generic state machine charges for
   PE_ACTION_RAISE/PE_ACTION_BET.  bet/check/call/fold are the action types
   mpf_tree_parse_action_type accepts; a check with nothing to call and a
   call facing a bet both map onto "call", which is all mpf's adapter offers
   for staying in the hand. */
/* mpf_tree actions carry no amount: a raise names a size_index into the
   node's bet_sizes, which mpf reads as the increment above the call --
   exactly what --raises names and what the generic state machine charges for
   PE_ACTION_RAISE/PE_ACTION_BET.  check/call both map onto "call", which is
   all mpf's adapter offers for staying in the hand. */
static void emit_action(tree_context_t *ctx, size_t child_id,
                        pe_action_kind_t kind, size_t size_index)
{
    if (ctx->action_emitted++ != 0u) fputc(',', ctx->out);
    if (kind == PE_ACTION_FOLD)
        fprintf(ctx->out, "{\"type\":\"fold\",\"next\":\"n%zu\"}", child_id);
    else if (kind == PE_ACTION_RAISE || kind == PE_ACTION_BET)
        fprintf(ctx->out, "{\"type\":\"raise\",\"size_index\":%zu,\"next\":\"n%zu\"}",
                size_index, child_id);
    else
        fprintf(ctx->out, "{\"type\":\"call\",\"next\":\"n%zu\"}", child_id);
}

/* The snapshot carries the state the solver must not have to guess: who acts
   and how much is already in the middle.  Stacks are what the state machine
   was handed; invested/round_contrib/pot come from the same state, so the
   solver's blind posting stays switched off there (cfg.preflop.defined). */
static void emit_snapshot(tree_context_t *ctx, const pe_betting_state_t *state,
                          int players, int first_to_act)
{
    fprintf(ctx->out,
            ",\"snapshot\":{\"defined\":true,\"num_players\":%d,"
            "\"street\":\"%s\",\"to_act\":%d,\"first_to_act\":%d,"
            "\"pot\":%.6g,\"to_call\":%.6g,\"current_bet\":%.6g,"
            "\"raises_made\":%u,\"board\":[",
            players, street_names[ctx->root_street], state->to_act, first_to_act,
            state->pot, state->to_call, state->current_bet,
            (unsigned)state->raises_made);
    for (int i = 0; i < ctx->board_count; ++i) {
        if (i) fputc(',', ctx->out);
        fprintf(ctx->out, "%d", ctx->board_cards[i]);
    }
    fprintf(ctx->out, "],\"board_revealed\":%d,", ctx->board_count);
    fputs("\"stacks\":[", ctx->out);
    for (int p = 0; p < players; ++p) {
        if (p) fputc(',', ctx->out);
        fprintf(ctx->out, "%.6g", state->stack[p]);
    }
    fputs("],\"invested\":[", ctx->out);
    for (int p = 0; p < players; ++p) {
        if (p) fputc(',', ctx->out);
        fprintf(ctx->out, "%.6g", state->invested[p]);
    }
    fputs("],\"round_contrib\":[", ctx->out);
    for (int p = 0; p < players; ++p) {
        if (p) fputc(',', ctx->out);
        fprintf(ctx->out, "%.6g", state->round_contrib[p]);
    }
    fputs("],\"active\":[", ctx->out);
    for (int p = 0; p < players; ++p) {
        if (p) fputc(',', ctx->out);
        fprintf(ctx->out, "%d", state->active[p] ? 1 : 0);
    }
    fputs("],\"acted\":[", ctx->out);
    for (int p = 0; p < players; ++p) {
        if (p) fputc(',', ctx->out);
        fprintf(ctx->out, "%d", state->acted[p] ? 1 : 0);
    }
    fputs("]}", ctx->out);
}

static int emit_node(tree_context_t *ctx, size_t id, const pe_betting_state_t *state,
                     int players, int first_to_act)
{
    pe_action_t actions[MAX_TREE_ACTIONS];
    const char *labels[MAX_TREE_ACTIONS];
    pe_betting_state_t children[MAX_TREE_ACTIONS];
    size_t child_ids[MAX_TREE_ACTIONS];
    size_t count = 0u;
    size_t n = 0u;
    if (ctx->node_count > MAX_TREE_NODES) return -1;
    if (ctx->node_emitted++ != 0u) fputc(',', ctx->out);
    ctx->action_emitted = 0u;
    /* mpf ids are strings; the acting player is a separate field.  A terminal
       here is the end of this betting round, not the showdown: mpf's
       adapter stops following the tree there and hands over to its own
       streets, which is exactly the handoff this tool wants. */
    fprintf(ctx->out, "{\"id\":\"n%zu\",\"type\":\"%s\",\"street\":\"%s\",\"player\":%d",
            id, state->terminal || state->round_complete ? "terminal" : "player",
            street_names[ctx->root_street], state->to_act);
    fputs(",\"bet_profile\":\"default\"", ctx->out);
    emit_snapshot(ctx, state, players, first_to_act);
    fputs(",\"actions\":[", ctx->out);
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
        emit_action(ctx, child_ids[count], actions[i].kind, (size_t)actions[i].size_index);
        count++;
    }
    fputs("]}", ctx->out);
    for (size_t i = 0u; i < count; ++i)
        if (emit_node(ctx, child_ids[i], &children[i], players, first_to_act) != 0) return -1;
    return 0;
}

static void usage(const char *program)
{
    fprintf(stderr, "usage: %s --players N --stack BB [--stack BB ...] --raises a,b,c\n"
                    "       [--game holdem|plo4|plo5|plo6] [--max-combos N]\n"
                    "       [--street preflop|flop|turn|river --board CARDS]\n"
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
        else if (strcmp(argv[i], "--game") == 0 && i + 1 < argc)
        {
            /* Call parse_game unconditionally: folding its success into the
               test made every --game value (valid included) fall through to
               usage, so no game but the default could ever be selected. */
            if (!parse_game(argv[++i], &ctx.game)) { usage(argv[0]); return 2; }
        }
        else if (strcmp(argv[i], "--max-combos") == 0 && i + 1 < argc) ctx.max_combos = (size_t)strtoull(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--stack") == 0 && i + 1 < argc && stack_count < (int)PE_BETTING_MAX_PLAYERS)
            stacks[stack_count++] = strtod(argv[++i], NULL);
        else if (strcmp(argv[i], "--first-to-act") == 0 && i + 1 < argc) first = atoi(argv[++i]);
        else if (strcmp(argv[i], "--pot") == 0 && i + 1 < argc) pot = strtod(argv[++i], NULL);
        else if (strcmp(argv[i], "--to-call") == 0 && i + 1 < argc) to_call = strtod(argv[++i], NULL);
        else if (strcmp(argv[i], "--raises") == 0 && i + 1 < argc)
        {
            const char *source = argv[++i];
            size_t source_length = strnlen(source, 4096u);
            char *copy = source_length < 4096u
                ? (char *)malloc(source_length + 1u) : NULL;
            if (copy) {
                for (size_t j = 0u; j <= source_length; ++j)
                    copy[j] = source[j];
            }
            /* strtok's first argument is written through, so `copy` cannot be
               const; the walking pointer itself only reads. */
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
        else if (strcmp(argv[i], "--street") == 0 && i + 1 < argc)
        {
            /* Parsed after the loop like --game: folding its success into the
               test made any valid value fall through to usage once. */
            ++i;
            ctx.root_street = parse_street_name(argv[i]);
            if (ctx.root_street < 0)
            {
                fprintf(stderr, "unknown --street '%s' (want preflop, flop, turn or river)\n", argv[i]);
                return 2;
            }
        }
        else if (strcmp(argv[i], "--board") == 0 && i + 1 < argc) ctx.board_text = argv[++i];
        else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) output_path = argv[++i];
        else { usage(argv[0]); return 2; }
    }
    pe_betting_rules_default(&ctx.rules, players);
    if (players < 2u || players > PE_BETTING_MAX_PLAYERS || stack_count != (int)players ||
        first < 0 || first >= (int)players || !output_path || ctx.raise_count == 0u)
    {
        usage(argv[0]);
        return 2;
    }
    if (ctx.root_street == 0)
    {
        if (ctx.board_text)
        {
            fprintf(stderr, "--board needs a flop, turn or river --street\n");
            return 2;
        }
    }
    else
    {
        int need = street_board_count(ctx.root_street);
        if (!ctx.board_text)
        {
            fprintf(stderr, "--street %s needs --board CARDS\n", street_names[ctx.root_street]);
            return 2;
        }
        if (!parse_board_text(ctx.board_text, ctx.board_cards, &ctx.board_count))
        {
            fprintf(stderr, "invalid --board '%s' (want cards like AsKdQc)\n", ctx.board_text);
            return 2;
        }
        if (ctx.board_count != need)
        {
            fprintf(stderr, "--board must hold exactly %d cards for %s (e.g. AsKdQc)\n",
                    need, street_names[ctx.root_street]);
            return 2;
        }
    }
    if (pe_betting_state_init(&root, &ctx.rules, stacks, players, first, pot, to_call) != PE_BETTING_OK)
    {
        usage(argv[0]);
        return 2;
    }
    /* mpf_tree documents carry no game/players fields of their own: the spot
       is named by the run (--rules) and the snapshots.  What this tool adds
       over a hand-written tree is the snapshot and the range profiles. */
    if (!compile_ranges(&ctx)) { free_ranges(&ctx); return 1; }
    ctx.out = fopen(output_path, "w");
    if (!ctx.out) { fprintf(stderr, "cannot open %s\n", output_path); free_ranges(&ctx); return 1; }
    fputs("{\"version\":1,\"root\":\"n0\",\"betProfiles\":["
          "{\"id\":\"default\",\"sizes\":[", ctx.out);
    for (i = 0; (size_t)i < ctx.raise_count; ++i) {
        if (i) fputc(',', ctx.out);
        fprintf(ctx.out, "%.6g", ctx.raise_sizes[i]);
    }
    /* Absolute chips, the same reading mpf gives a raise's bet_size. */
    fputs("],\"pot_sizing\":false}]", ctx.out);
    fputs(",\"nodes\":[", ctx.out);
    ctx.node_count = 1u;
    ctx.node_emitted = 0u;
    ctx.action_emitted = 0u;
    if (emit_node(&ctx, 0u, &root, (int)players, first) != 0) { fclose(ctx.out); free_ranges(&ctx); return 1; }
    fputs("]", ctx.out);
    if (!emit_range_profiles(&ctx)) { fclose(ctx.out); free_ranges(&ctx); return 1; }
    fputs("}\n", ctx.out);
    fclose(ctx.out);
    free_ranges(&ctx);
    fprintf(stderr, "generated %zu nodes in %s\n", ctx.node_count, output_path);
    return 0;
}
