#ifndef HAND_DISTRIBUTIONS_H
#define HAND_DISTRIBUTIONS_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <poker_eval/deck/deck_std.h>

// Only define TRACE and ASSERT if NDEBUG is not defined
#ifndef NDEBUG
#define TRACE(...) do { printf(__VA_ARGS__); } while (0)
#define ASSERT assert
#else
#define TRACE(...)
#define ASSERT(x) ((void)0)
#endif

/* Note: StdDeck_CardMask and related macros are now defined in deck_std.h
 * Do not redefine them here to avoid conflicts */

#endif // HAND_DISTRIBUTIONS_H

