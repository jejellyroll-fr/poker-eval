/*
 * test_pe_ports.c - CTR-04 driven ports, injection and telemetry adapters
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * The property this file defends: a solver created without telemetry behaves
 * exactly like one created with a sink, and neither ever dereferences NULL.
 * That is what allows every later emit call in the domain to be unconditional,
 * which is in turn what makes it realistic to remove the fprintf calls from
 * the traversal in EXT-03.
 *
 * The 100-event count the ticket asks for is here, but the more valuable
 * checks are the degenerate ones around it: a NULL ops, a NULL emit, a NULL
 * event, an uninitialised context. Each of those is a crash the domain must
 * never be able to cause by forgetting a guard.
 */

#include <poker_eval/solver/pe_ports.h>
#include <poker_eval/solver/pe_solver.h>
#include <poker_eval/solver/pe_solver_config.h>
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
 * A counting sink
 * ------------------------------------------------------------------ */

typedef struct {
    int count;
    pe_log_level_t last_level;
    char last_category[32];
    char last_message[64];
    uint64_t last_iteration;
} counter_t;

static void counting_callback(const pe_telemetry_event_t *event, void *user)
{
    counter_t *c = (counter_t *)user;

    if (c == NULL || event == NULL)
        return;

    c->count++;
    c->last_level = event->level;
    c->last_iteration = event->iteration;

    /* Copy rather than keep the pointers: the contract only promises they are
       valid for the duration of the call, and this test is the first thing
       that would notice an adapter breaking that promise. */
    c->last_category[0] = '\0';
    c->last_message[0] = '\0';
    if (event->category != NULL)
    {
        strncpy(c->last_category, event->category, sizeof(c->last_category) - 1);
        c->last_category[sizeof(c->last_category) - 1] = '\0';
    }
    if (event->message != NULL)
    {
        strncpy(c->last_message, event->message, sizeof(c->last_message) - 1);
        c->last_message[sizeof(c->last_message) - 1] = '\0';
    }
}

static pe_telemetry_event_t make_event(pe_log_level_t level, uint64_t iteration)
{
    pe_telemetry_event_t e;
    e.level = level;
    e.category = "test";
    e.message = "event";
    e.iteration = iteration;
    return e;
}

/* ------------------------------------------------------------------ *
 * The callback adapter delivers
 * ------------------------------------------------------------------ */

static void test_callback_counts_events(void)
{
    counter_t counter;
    pe_telemetry_callback_ctx_t ctx;
    pe_telemetry_ops_t ops;
    int i;

    memset(&counter, 0, sizeof(counter));
    CHECK(pe_telemetry_callback_init(&ctx, counting_callback, &counter, PE_LOG_TRACE) == 0,
          "callback init should succeed");
    ops = pe_telemetry_callback_ops(&ctx);

    for (i = 0; i < 100; ++i)
    {
        pe_telemetry_event_t e = make_event(PE_LOG_INFO, (uint64_t)i);
        pe_telemetry_emit(&ops, &e);
    }

    CHECK(counter.count == 100, "expected 100 events, counted %d", counter.count);
    CHECK(counter.last_iteration == 99, "last iteration is %llu, expected 99",
          (unsigned long long)counter.last_iteration);
    CHECK(strcmp(counter.last_category, "test") == 0,
          "last category is \"%s\"", counter.last_category);
    CHECK(strcmp(counter.last_message, "event") == 0,
          "last message is \"%s\"", counter.last_message);
}

static void test_callback_init_validates(void)
{
    pe_telemetry_callback_ctx_t ctx;

    CHECK(pe_telemetry_callback_init(NULL, counting_callback, NULL, PE_LOG_INFO) == -1,
          "a NULL context should be rejected");

    /* An out-of-range level is rejected, not clamped: clamping upwards would
       deliver more than the caller asked for. */
    CHECK(pe_telemetry_callback_init(&ctx, counting_callback, NULL,
                                     (pe_log_level_t)PE_LOG_LEVEL_COUNT) == -1,
          "an out-of-range level should be rejected");
    CHECK(pe_telemetry_callback_init(&ctx, counting_callback, NULL,
                                     (pe_log_level_t)-1) == -1,
          "a negative level should be rejected");
}

/* ------------------------------------------------------------------ *
 * Level filtering
 * ------------------------------------------------------------------ */

static void test_level_filtering(void)
{
    counter_t counter;
    pe_telemetry_callback_ctx_t ctx;
    pe_telemetry_ops_t ops;
    pe_telemetry_event_t info;
    pe_telemetry_event_t trace;

    memset(&counter, 0, sizeof(counter));
    pe_telemetry_callback_init(&ctx, counting_callback, &counter, PE_LOG_WARN);
    ops = pe_telemetry_callback_ops(&ctx);

    CHECK(pe_telemetry_wants(&ops, PE_LOG_ERROR), "ERROR should pass a WARN filter");
    CHECK(pe_telemetry_wants(&ops, PE_LOG_WARN), "WARN should pass a WARN filter");
    CHECK(!pe_telemetry_wants(&ops, PE_LOG_INFO), "INFO should not pass a WARN filter");
    CHECK(!pe_telemetry_wants(&ops, PE_LOG_TRACE), "TRACE should not pass a WARN filter");

    info = make_event(PE_LOG_INFO, 0);
    trace = make_event(PE_LOG_TRACE, 0);
    pe_telemetry_emit(&ops, &info);
    pe_telemetry_emit(&ops, &trace);
    CHECK(counter.count == 0, "filtered events reached the callback (%d)", counter.count);

    info = make_event(PE_LOG_ERROR, 0);
    pe_telemetry_emit(&ops, &info);
    CHECK(counter.count == 1, "an ERROR should have been delivered");
}

/* ------------------------------------------------------------------ *
 * Nothing dereferences NULL
 * ------------------------------------------------------------------ */

static void test_null_safety(void)
{
    pe_telemetry_event_t e = make_event(PE_LOG_ERROR, 0);
    pe_telemetry_callback_ctx_t ctx;
    pe_telemetry_ops_t ops;
    const pe_telemetry_ops_t *sink = pe_telemetry_null();

    /* Each of these must be a no-op rather than a crash. Reaching the end of
       the function is the assertion. */
    pe_telemetry_emit(NULL, &e);
    pe_telemetry_emit(NULL, NULL);
    pe_telemetry_emit(sink, &e);
    pe_telemetry_emit(sink, NULL);
    pe_telemetry_flush(NULL);
    pe_telemetry_flush(sink);

    CHECK(!pe_telemetry_wants(NULL, PE_LOG_ERROR), "a NULL ops should want nothing");
    CHECK(!pe_telemetry_wants(sink, PE_LOG_ERROR),
          "the sink should want nothing, so callers skip formatting");

    /* An ops built from a context nobody initialised. */
    ops = pe_telemetry_callback_ops(NULL);
    pe_telemetry_emit(&ops, &e);
    CHECK(!pe_telemetry_wants(&ops, PE_LOG_ERROR),
          "ops from a NULL context should want nothing");

    /* An initialised context with no function. */
    pe_telemetry_callback_init(&ctx, NULL, NULL, PE_LOG_TRACE);
    ops = pe_telemetry_callback_ops(&ctx);
    pe_telemetry_emit(&ops, &e);

    /* The sink is shared and stable across calls. */
    CHECK(pe_telemetry_null() == sink, "the sink should be a single shared instance");
}

/* ------------------------------------------------------------------ *
 * Injection
 * ------------------------------------------------------------------ */

static void test_deps_default_is_all_ports_defaulted(void)
{
    pe_solver_deps_t deps = pe_solver_deps_default();
    pe_solver_deps_t zeroed;

    memset(&zeroed, 0, sizeof(zeroed));

    /* Unlike the configuration, zero IS the default here. */
    CHECK(memcmp(&deps, &zeroed, sizeof(deps)) == 0,
          "pe_solver_deps_default should equal a zeroed structure");
    CHECK(deps.telemetry == NULL, "telemetry should default to NULL, meaning the sink");
    CHECK(deps.compute == NULL, "compute should default to NULL");
    CHECK(deps.evaluator == NULL, "evaluator should default to NULL");
    CHECK(deps.storage == NULL, "storage should default to NULL");
    CHECK(deps.persist == NULL, "persist should default to NULL");
}

static void test_create_without_telemetry(void)
{
    pe_solver_config_t cfg = pe_solver_config_default();
    pe_solver_deps_t deps = pe_solver_deps_default();
    pe_solver_t *solver;

    /* deps.telemetry is NULL: the solver must install the sink and emit into
       it without dereferencing anything.

       Checking that it did not crash proves nothing here — emitting into a
       NULL adapter is just as silent as emitting into the sink. The claim only
       becomes testable by reading back what was resolved. */
    solver = pe_solver_create(&cfg, &deps);
    CHECK(solver != NULL, "creation with defaulted dependencies should succeed");
    CHECK(pe_solver_get_telemetry(solver) == pe_telemetry_null(),
          "a NULL telemetry port should have been resolved to the sink");
    pe_solver_destroy(solver);

    /* And with no dependencies at all. */
    solver = pe_solver_create(&cfg, NULL);
    CHECK(solver != NULL, "creation with NULL dependencies should succeed");
    CHECK(pe_solver_get_telemetry(solver) == pe_telemetry_null(),
          "absent dependencies should have been resolved to the sink");
    pe_solver_destroy(solver);

    /* The configuration is not optional. */
    CHECK(pe_solver_create(NULL, &deps) == NULL, "a NULL configuration should be refused");
    CHECK(pe_solver_create(NULL, NULL) == NULL, "a NULL configuration should be refused");

    /* Documented as safe. */
    pe_solver_destroy(NULL);
}

static void test_create_uses_the_injected_adapter(void)
{
    pe_solver_config_t cfg = pe_solver_config_default();
    pe_solver_deps_t deps = pe_solver_deps_default();
    pe_telemetry_callback_ctx_t ctx;
    pe_telemetry_ops_t ops;
    counter_t counter;
    pe_solver_t *solver;

    memset(&counter, 0, sizeof(counter));
    pe_telemetry_callback_init(&ctx, counting_callback, &counter, PE_LOG_TRACE);
    ops = pe_telemetry_callback_ops(&ctx);
    deps.telemetry = &ops;

    solver = pe_solver_create(&cfg, &deps);
    CHECK(solver != NULL, "creation should succeed");

    /* The proof that injection actually took: an adapter handed in receives
       the solver's first event. Its absence would mean the deps were dropped. */
    CHECK(counter.count == 1, "the injected adapter received %d events, expected 1",
          counter.count);
    CHECK(strcmp(counter.last_category, "solver") == 0,
          "unexpected category \"%s\"", counter.last_category);
    CHECK(pe_solver_get_telemetry(solver) == &ops,
          "the solver should hold the adapter it was handed, not a copy or the sink");

    pe_solver_destroy(solver);
}

static void test_config_is_copied_not_borrowed(void)
{
    pe_solver_config_t cfg = pe_solver_config_default();
    const pe_solver_config_t *stored;
    pe_solver_t *solver;

    solver = pe_solver_create(&cfg, NULL);
    CHECK(solver != NULL, "creation should succeed");

    /* The caller scribbles on their copy the moment create returns. Doing it
       is not the test — reading the solver's copy back afterwards is. Without
       that read, a solver that kept a borrowed pointer would pass unnoticed. */
    memset(&cfg, 0xAB, sizeof(cfg));

    stored = pe_solver_get_config(solver);
    CHECK(stored != NULL, "a live solver should expose its configuration");
    if (stored != NULL)
    {
        CHECK(stored->algorithm.traversal == PE_TRAVERSAL_FULL_SCALAR,
              "the stored traversal was overwritten by the caller's scribble");
        CHECK(stored->algorithm.regret == PE_REGRET_VANILLA,
              "the stored regret mode was overwritten");
        CHECK(stored->execution.precision == PE_PREC_F64,
              "the stored precision was overwritten");
        CHECK(stored->max_iterations == 1000,
              "the stored iteration limit was overwritten");
    }

    pe_solver_destroy(solver);

    CHECK(pe_solver_get_config(NULL) == NULL, "a NULL solver has no configuration");
    CHECK(pe_solver_get_telemetry(NULL) == NULL, "a NULL solver has no telemetry");
}

static void test_metrics_require_completed_solve(void)
{
    pe_solver_config_t cfg = pe_solver_config_default();
    pe_solver_t *solver = pe_solver_create(&cfg, NULL);
    pe_metrics_t metrics;

    CHECK(solver != NULL, "creation should succeed before metrics query");
    if (solver != NULL)
    {
        CHECK(pe_solver_metrics(solver, &metrics) == PE_SOLVER_ERR_INVALID_STATE,
              "metrics must not be reported before a solve completes");
        CHECK(pe_solver_metrics(solver, NULL) == PE_SOLVER_ERR_NULL_ARGUMENT,
              "NULL output must still be rejected");
        pe_solver_destroy(solver);
    }
    CHECK(pe_solver_metrics(NULL, &metrics) == PE_SOLVER_ERR_NULL_ARGUMENT,
          "NULL solver must still be rejected");
}

int main(void)
{
    test_callback_counts_events();
    test_callback_init_validates();
    test_level_filtering();
    test_null_safety();
    test_deps_default_is_all_ports_defaulted();
    test_create_without_telemetry();
    test_create_uses_the_injected_adapter();
    test_config_is_copied_not_borrowed();
    test_metrics_require_completed_solve();

    if (g_failures != 0)
    {
        fprintf(stderr, "test_pe_ports: %d failure(s)\n", g_failures);
        return 1;
    }

    printf("test_pe_ports: injection and telemetry adapters verified\n");
    return 0;
}
