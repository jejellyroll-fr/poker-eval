/*
 * telemetry_callback.c - Telemetry to a caller-supplied function (CTR-04)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * Routes events to a function the caller owns. This is the adapter a host
 * application, a CLI or a test uses: it decides what a solve event becomes —
 * a log line, a progress bar, a counter — without the domain knowing.
 *
 * The context is caller-owned on purpose. Allocating it here would mean
 * deciding when to free it, and the solver stores a borrowed pointer, so the
 * lifetime belongs to whoever created it.
 */

#include <poker_eval/solver/pe_telemetry.h>

#include <stddef.h>

static void pe_telemetry_callback_emit(void *self, const pe_telemetry_event_t *event)
{
    const pe_telemetry_callback_ctx_t *ctx = (const pe_telemetry_callback_ctx_t *)self;

    /* pe_telemetry_emit already rejected a NULL event and filtered the level,
       but this function is also reachable through the ops pointer directly. */
    if (ctx == NULL || ctx->fn == NULL || event == NULL)
        return;

    ctx->fn(event, ctx->user);
}

int pe_telemetry_callback_init(pe_telemetry_callback_ctx_t *ctx,
                               pe_telemetry_callback_fn fn,
                               void *user,
                               pe_log_level_t max_level)
{
    if (ctx == NULL)
        return -1;

    /* Rejected rather than clamped: clamping an out-of-range level upwards
       would silently deliver more than the caller asked for. */
    if ((int)max_level < 0 || (int)max_level >= (int)PE_LOG_LEVEL_COUNT)
        return -1;

    ctx->fn = fn;
    ctx->user = user;
    ctx->max_level = max_level;
    return 0;
}

pe_telemetry_ops_t pe_telemetry_callback_ops(pe_telemetry_callback_ctx_t *ctx)
{
    pe_telemetry_ops_t ops;

    ops.flush = NULL;   /* nothing is buffered */
    ops.self = ctx;

    if (ctx == NULL)
    {
        /* A caller that never initialised its context gets silence rather than
           a jump through an uninitialised function pointer. */
        ops.emit = NULL;
        ops.max_level = PE_LOG_ERROR;
    }
    else
    {
        ops.emit = pe_telemetry_callback_emit;
        ops.max_level = ctx->max_level;
    }

    return ops;
}
