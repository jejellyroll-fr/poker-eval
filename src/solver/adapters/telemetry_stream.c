/*
 * telemetry_stream.c - stderr / stdout adapters (v3, EXT-03)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * The adapters that keep the migration honest. Every fprintf removed from the
 * solver is replaced by an emit, and this is what turns that emit back into
 * the same bytes on the same stream — which is how EXT-03 can require that a
 * traced solve produce output identical to before.
 *
 * Messages arrive already formatted and already carrying their newline, so
 * the adapter writes them verbatim and adds nothing of its own. Anything else
 * — a level prefix, a timestamp — would be a change of output disguised as a
 * refactor.
 */

#include <poker_eval/solver/pe_telemetry.h>

#include <stddef.h>
#include <stdio.h>

static void pe_telemetry_stderr_emit(void *self, const pe_telemetry_event_t *event)
{
    (void)self;
    if (event == NULL || event->message == NULL)
        return;
    fputs(event->message, stderr);
    /* The legacy call sites flushed after every line, and trace output is only
       useful if it survives the crash it is diagnosing. */
    fflush(stderr);
}

static void pe_telemetry_stdout_emit(void *self, const pe_telemetry_event_t *event)
{
    (void)self;
    if (event == NULL || event->message == NULL)
        return;
    fputs(event->message, stdout);
}

static void pe_telemetry_stdout_flush(void *self)
{
    (void)self;
    fflush(stdout);
}

static const pe_telemetry_ops_t k_stderr_ops = {
    pe_telemetry_stderr_emit,
    NULL,           /* every line is already flushed */
    NULL,
    PE_LOG_TRACE    /* the legacy code printed whatever it was asked to */
};

static const pe_telemetry_ops_t k_stdout_ops = {
    pe_telemetry_stdout_emit,
    pe_telemetry_stdout_flush,
    NULL,
    PE_LOG_TRACE
};

const pe_telemetry_ops_t *pe_telemetry_stderr(void)
{
    return &k_stderr_ops;
}

const pe_telemetry_ops_t *pe_telemetry_stdout(void)
{
    return &k_stdout_ops;
}
