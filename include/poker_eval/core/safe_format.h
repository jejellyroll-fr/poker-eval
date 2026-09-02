/*
 * safe_format.h - bounded formatting for internal diagnostic descriptors.
 *
 * The formatter accepts only the small set of conversions used by poker-eval
 * diagnostics. Each conversion is rendered through a constant libc format,
 * so a caller-provided string is never interpreted as a format specification.
 */
#ifndef POKER_EVAL_SAFE_FORMAT_H
#define POKER_EVAL_SAFE_FORMAT_H

#include <stdarg.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t pe_safe_vformat(char *out, size_t capacity, const char *format,
                       va_list args);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_SAFE_FORMAT_H */
