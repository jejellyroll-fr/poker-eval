/*
 * pe_telemetry.h - Telemetry port (architecture v3, CTR-04)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * The domain's only way to say anything. Rule 1 of the hexagon is that the
 * domain knows nothing about stderr, files or a UI; today cfr_core.c breaks it
 * with fprintf calls in the middle of the traversal, and this port is what
 * EXT-03 routes them into.
 *
 * An event carries an already-formatted message. Formatting stays on the
 * emitting side so this header pulls in no <stdio.h>, and so a dropped event
 * costs nothing: pe_telemetry_wants() lets a caller skip building the message
 * at all when the level is filtered out. That matters because the traversal is
 * the hottest path in the solver and it is exactly where the current tracing
 * lives.
 *
 * Every entry point here is null-safe. A solver configured without telemetry
 * must behave identically to one configured with a sink that ignores
 * everything, and must never test for NULL at each call site.
 */

#ifndef POKER_EVAL_PE_TELEMETRY_H
#define POKER_EVAL_PE_TELEMETRY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ *
 * Levels
 * ------------------------------------------------------------------ */

/*
 * Ordered by decreasing severity, so a numeric comparison against
 * pe_telemetry_ops_t::max_level is the whole filter. PE_LOG_ERROR is zero:
 * a zeroed ops structure reports errors and nothing else, which is the safe
 * default for a partially initialised adapter.
 */
typedef enum {
    PE_LOG_ERROR = 0,
    PE_LOG_WARN,
    PE_LOG_INFO,
    PE_LOG_DEBUG,
    PE_LOG_TRACE,
    PE_LOG_LEVEL_COUNT
} pe_log_level_t;

/* ------------------------------------------------------------------ *
 * Events
 * ------------------------------------------------------------------ */

typedef struct {
    pe_log_level_t level;

    /* Stable identifier of the emitting component: "solver", "registry",
       "traversal", "storage", ... Expected to be a string literal, so an
       adapter may keep the pointer. Never NULL. */
    const char *category;

    /* Already formatted, never NULL. An adapter may keep the pointer only for
       the duration of the emit call. */
    const char *message;

    /* Iteration the event belongs to, or 0 when it is not iteration-scoped. */
    uint64_t iteration;
} pe_telemetry_event_t;

/* ------------------------------------------------------------------ *
 * The port
 * ------------------------------------------------------------------ */

typedef struct {
    /* Receives one event. May be NULL, which makes the adapter a sink. */
    void (*emit)(void *self, const pe_telemetry_event_t *event);

    /* Pushes anything buffered. May be NULL. */
    void (*flush)(void *self);

    /* Adapter state, passed back to emit and flush. */
    void *self;

    /* Events strictly more verbose than this are dropped before emit is
       called. */
    pe_log_level_t max_level;
} pe_telemetry_ops_t;

/**
 * Whether an event at `level` would be delivered.
 *
 * Call this before building a message when formatting it is not free — a
 * per-node trace, for instance. Safe with ops == NULL, which reports 0.
 */
static inline int pe_telemetry_wants(const pe_telemetry_ops_t *ops,
                                     pe_log_level_t level)
{
    if (ops == NULL || ops->emit == NULL)
        return 0;
    return (int)level <= (int)ops->max_level;
}

/**
 * Deliver one event. Safe with a NULL ops, a NULL emit or a NULL event, so
 * the domain never guards a call site.
 */
static inline void pe_telemetry_emit(const pe_telemetry_ops_t *ops,
                                     const pe_telemetry_event_t *event)
{
    if (event == NULL || !pe_telemetry_wants(ops, event->level))
        return;
    ops->emit(ops->self, event);
}

/** Flush a buffering adapter. Safe with a NULL ops or a NULL flush. */
static inline void pe_telemetry_flush(const pe_telemetry_ops_t *ops)
{
    if (ops == NULL || ops->flush == NULL)
        return;
    ops->flush(ops->self);
}

/**
 * Format a message and emit it (EXT-03).
 *
 * The convenience the domain actually needs: every call site it replaces was
 * an fprintf. Formatting happens here rather than in the header so callers
 * pull in no <stdio.h>, and it is skipped entirely when the level is filtered
 * out — which matters because the busiest call sites are inside the traversal.
 *
 * A message longer than the internal buffer is truncated rather than
 * allocated for: telemetry must never be able to fail a solve.
 */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 5, 6)))
#endif
void pe_telemetry_emitf(const pe_telemetry_ops_t *ops,
                        pe_log_level_t level,
                        const char *category,
                        uint64_t iteration,
                        const char *fmt, ...);

/* ------------------------------------------------------------------ *
 * Adapter: null
 * ------------------------------------------------------------------ */

/**
 * A sink that ignores everything. This is what a solver created without
 * telemetry installs, so the domain's emit path is identical whether or not
 * anyone is listening.
 *
 * The returned pointer is to immutable shared state: it is always valid, needs
 * no cleanup, and may be used by any number of solvers at once.
 */
const pe_telemetry_ops_t *pe_telemetry_null(void);

/* ------------------------------------------------------------------ *
 * Adapters: standard streams
 * ------------------------------------------------------------------ */

/**
 * Writes each message to stderr and flushes.
 *
 * This is what the legacy solver used directly, so it is what a caller who
 * asks for no telemetry gets when the code being migrated used to print: the
 * bytes on stderr stay exactly what they were. Messages carry their own
 * newline; the adapter adds nothing.
 *
 * Shared, immutable, always valid, needs no cleanup.
 */
const pe_telemetry_ops_t *pe_telemetry_stderr(void);

/** The same, on stdout, for output that is a result rather than a log. */
const pe_telemetry_ops_t *pe_telemetry_stdout(void);

/* ------------------------------------------------------------------ *
 * Adapter: callback
 * ------------------------------------------------------------------ */

/**
 * Receives one event. `user` is the pointer handed to pe_telemetry_callback_init.
 * The event and the strings it points at are valid for the duration of the
 * call only.
 */
typedef void (*pe_telemetry_callback_fn)(const pe_telemetry_event_t *event,
                                         void *user);

/**
 * State of a callback adapter. The caller owns it and must keep it alive for
 * as long as the solver using it — the solver stores a pointer, never a copy.
 */
typedef struct {
    pe_telemetry_callback_fn fn;
    void *user;
    pe_log_level_t max_level;
} pe_telemetry_callback_ctx_t;

/**
 * Initialise a callback adapter.
 *
 * @param ctx        Storage owned by the caller. Must outlive the ops.
 * @param fn         Receives the events. A NULL fn makes the adapter a sink.
 * @param user       Passed back to fn untouched.
 * @param max_level  Most verbose level to deliver. A value outside the
 *                   pe_log_level_t range is rejected rather than clamped: a
 *                   filter that silently widens is worse than one that fails.
 * @return 0 on success, -1 when ctx is NULL or max_level is out of range. The
 *         context is left untouched on failure.
 */
int pe_telemetry_callback_init(pe_telemetry_callback_ctx_t *ctx,
                               pe_telemetry_callback_fn fn,
                               void *user,
                               pe_log_level_t max_level);

/**
 * Ops for a context initialised by pe_telemetry_callback_init.
 *
 * With a NULL ctx the result is a sink, so a caller that forgot to initialise
 * gets silence rather than a crash.
 */
pe_telemetry_ops_t pe_telemetry_callback_ops(pe_telemetry_callback_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_TELEMETRY_H */
