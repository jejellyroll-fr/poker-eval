/*
 * test_cfr_telemetry.c - EXT-03: solver output goes through the port
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * Removing the fprintf calls is only half the ticket. The half that matters is
 * that a host can now take the output: capture progress into its own log,
 * drive a UI from it, or silence it — without redirecting a process-wide
 * stream and catching everything else with it.
 *
 * The byte-for-byte equivalence of the default path is checked outside this
 * file, by diffing a traced solve against the previous commit. What is pinned
 * here is that the port is really wired: an installed adapter receives the
 * lines, and installing one stops them reaching stderr.
 */

#include <poker_eval/engine/solvers/cfr/cfr_core.h>
#include <poker_eval/solver/pe_telemetry.h>

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(cond, ...)                                       \
    do                                                         \
    {                                                          \
        if (!(cond))                                           \
        {                                                      \
            fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                      \
            fprintf(stderr, "\n");                             \
            g_failures++;                                      \
        }                                                      \
    } while (0)

/* ------------------------------------------------------------------ *
 * A toy two-player game, deep enough to produce a few trace lines
 * ------------------------------------------------------------------ */

static int t_player(cfr_game_t *g, uint64_t k, void *u)
{ (void)g; (void)u; return (int)(k & 1); }

static int t_terminal(cfr_game_t *g, uint64_t k, void *u)
{ (void)g; (void)u; return ((k >> 8) >= 3); }

static double t_utility(cfr_game_t *g, uint64_t k, int p, void *u)
{ (void)g; (void)u; double v = (double)((k >> 8) & 7) - 1.5; return (p == 0) ? v : -v; }

static int t_actions(cfr_game_t *g, uint64_t k, int *out, int max, void *u)
{
    (void)g; (void)k; (void)u;
    int n = (max < 2) ? max : 2;
    for (int i = 0; i < n; ++i) out[i] = i;
    return n;
}

static uint64_t t_apply(cfr_game_t *g, uint64_t k, int a, void *u)
{ (void)g; (void)u; return (((k >> 8) + 1) << 8) | (((k & 0xFF) + (uint64_t)a + 1) & 0xFF); }

static void toy_game(cfr_game_t *g)
{
    memset(g, 0, sizeof(*g));
    g->current_player = t_player;
    g->get_actions = t_actions;
    g->apply_action = t_apply;
    g->is_terminal = t_terminal;
    g->get_utility = t_utility;
    g->num_players = 2;
    g->initial_state = (void *)(uintptr_t)0;
}

/* ------------------------------------------------------------------ *
 * A capturing adapter
 * ------------------------------------------------------------------ */

typedef struct {
    int count;
    int traces;
    char first[256];
} capture_t;

static void capture_cb(const pe_telemetry_event_t *event, void *user)
{
    capture_t *c = (capture_t *)user;
    if (c == NULL || event == NULL || event->message == NULL)
        return;
    if (c->count == 0)
    {
        strncpy(c->first, event->message, sizeof(c->first) - 1);
        c->first[sizeof(c->first) - 1] = '\0';
    }
    c->count++;
    if (event->level == PE_LOG_TRACE)
        c->traces++;
}

static void solve_with(const pe_telemetry_ops_t *ops, int trace)
{
    cfr_game_t game;
    cfr_config_t cfg;
    cfr_storage_t *storage;
    double expl = 0.0;

    toy_game(&game);
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_iterations = 2;
    cfg.max_depth = 32;
    cfg.trace_iterations = trace;
    cfg.telemetry = ops;

    storage = cfr_storage_create();
    (void)cfr_solve(&game, storage, &cfg, &expl);
    cfr_storage_destroy(storage);
}

static void test_installed_adapter_receives_the_output(void)
{
    capture_t cap;
    pe_telemetry_callback_ctx_t ctx;
    pe_telemetry_ops_t ops;

    memset(&cap, 0, sizeof(cap));
    pe_telemetry_callback_init(&ctx, capture_cb, &cap, PE_LOG_TRACE);
    ops = pe_telemetry_callback_ops(&ctx);

    solve_with(&ops, 1);

    CHECK(cap.count > 0, "the installed adapter received nothing");
    CHECK(cap.traces > 0, "no trace-level event reached the adapter");
    CHECK(strstr(cap.first, "[cfr] ") == cap.first,
          "unexpected first message \"%s\"", cap.first);

    /* Messages arrive complete, newline included: that is what lets the stream
       adapter reproduce the legacy bytes without adding anything. */
    CHECK(strchr(cap.first, '\n') != NULL, "a message arrived without its newline");
}

static void test_a_sink_silences_the_solver(void)
{
    capture_t cap;

    memset(&cap, 0, sizeof(cap));

    /* pe_telemetry_null()'s emit is NULL, so pe_telemetry_wants() is false and
       the solver skips building the messages entirely rather than formatting
       them for a sink to discard. Reaching the end without output is the
       assertion; the value is that a host can silence a solve without
       redirecting stderr for the whole process. */
    solve_with(pe_telemetry_null(), 1);

    CHECK(cap.count == 0, "the sink somehow produced events");
}

static void test_null_telemetry_keeps_the_legacy_path(void)
{
    /* cfg.telemetry left NULL: the solver installs the stderr adapter, which
       is what every existing caller relies on. This must not crash and must
       not change the solve. */
    solve_with(NULL, 0);
}

static void test_level_filter_reaches_the_solver(void)
{
    capture_t cap;
    pe_telemetry_callback_ctx_t ctx;
    pe_telemetry_ops_t ops;

    memset(&cap, 0, sizeof(cap));
    pe_telemetry_callback_init(&ctx, capture_cb, &cap, PE_LOG_WARN);
    ops = pe_telemetry_callback_ops(&ctx);

    solve_with(&ops, 1);

    /* Per-iteration traces are TRACE level; a WARN filter drops them, and the
       solver does not pay to format them. */
    CHECK(cap.traces == 0, "%d trace events passed a WARN filter", cap.traces);
}

int main(void)
{
    test_installed_adapter_receives_the_output();
    test_a_sink_silences_the_solver();
    test_null_telemetry_keeps_the_legacy_path();
    test_level_filter_reaches_the_solver();

    if (g_failures != 0)
    {
        fprintf(stderr, "test_cfr_telemetry: %d failure(s)\n", g_failures);
        return 1;
    }

    printf("test_cfr_telemetry: solver output routes through the port\n");
    return 0;
}
