/*
 * string_compat.h - Cross-platform string helpers.
 *
 * Provides case-insensitive compare shims on Windows.
 */

#ifndef POKER_EVAL_STRING_COMPAT_H
#define POKER_EVAL_STRING_COMPAT_H

#include <string.h>

#if defined(_MSC_VER)
#ifndef strcasecmp
#define strcasecmp _stricmp
#endif
#ifndef strncasecmp
#define strncasecmp _strnicmp
#endif
#endif

#endif /* POKER_EVAL_STRING_COMPAT_H */
