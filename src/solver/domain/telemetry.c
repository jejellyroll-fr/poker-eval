/*
 * telemetry.c - Formatting helper over the telemetry port (v3, EXT-03)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * One function, and its job is to let a call site that used to be an fprintf
 * become a telemetry emit without the caller learning about events, buffers or
 * <stdio.h>.
 *
 * vsnprintf into a fixed buffer is formatting, not I/O: nothing here opens or
 * writes to anything. Truncation is deliberate — telemetry that could fail an
 * allocation would be telemetry that can fail a solve.
 */

#include <poker_eval/solver/pe_telemetry.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Long enough for the widest existing trace line with room to spare. */
#define PE_TELEMETRY_MESSAGE_MAX 512

void pe_telemetry_emitf(const pe_telemetry_ops_t *ops,
                        pe_log_level_t level,
                        const char *category,
                        uint64_t iteration,
                        const char *fmt, ...)
{
    char message[PE_TELEMETRY_MESSAGE_MAX];
    pe_telemetry_event_t event;
    va_list args;

    /* Ask before formatting: the busiest call sites are per-node traces, and
       building a message the filter would drop is the cost this avoids. */
    if (!pe_telemetry_wants(ops, level) || fmt == NULL)
        return;

    va_start(args, fmt);
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
    vsnprintf(message, sizeof(message), fmt, args); /* NOSONAR: callers provide internal format strings. */
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
    va_end(args);

    event.level = level;
    event.category = (category != NULL) ? category : "solver";
    event.message = message;
    event.iteration = iteration;
    memset(&event.counters, 0, sizeof(event.counters));

    pe_telemetry_emit(ops, &event);
}

void pe_telemetry_emit_counters(const pe_telemetry_ops_t *ops,
                                pe_log_level_t level,
                                const char *category,
                                uint64_t iteration,
                                const pe_telemetry_counters_t *counters,
                                const char *message)
{
    pe_telemetry_event_t event;

    if (!pe_telemetry_wants(ops, level))
        return;
    memset(&event, 0, sizeof(event));
    event.level = level;
    event.category = category != NULL ? category : "solver";
    event.message = message != NULL ? message : "compute counters\n";
    event.iteration = iteration;
    if (counters != NULL)
        event.counters = *counters;
    pe_telemetry_emit(ops, &event);
}
