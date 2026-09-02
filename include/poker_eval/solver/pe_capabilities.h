/*
 * pe_capabilities.h - Solver capability bits (architecture v3, CTR-02)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * A capability is something the resolved execution plan can actually do:
 * carry values per combo, deal a three-card flop, prune by regret, evaluate
 * terminals on a GPU. The registry uses them for one purpose — refusing a
 * configuration the plan cannot honour, instead of silently downgrading it.
 *
 * The set below is the validation matrix of SOLVER_ARCHITECTURE_V3.md §5.
 * Values cross the public ABI: new capabilities are appended, existing bits
 * never move.
 *
 * The text form exists so a plan can be printed, logged and read back:
 *
 *     "VECTOR_FORM|PRIVATE_RANGES|CPU_PARALLEL"
 *
 * pe_caps_to_string() and pe_caps_parse() are exact inverses over the whole
 * uint64_t range — bits with no name yet round-trip through a hex token, so a
 * plan written by a newer build is never silently truncated by an older one.
 */

#ifndef POKER_EVAL_PE_CAPABILITIES_H
#define POKER_EVAL_PE_CAPABILITIES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ *
 * Game model
 * ------------------------------------------------------------------ */

/** Values are carried per combo rather than per state (lane A). */
#define PE_CAP_VECTOR_FORM             (UINT64_C(1) << 0)
/** Players hold ranges, not a single fixed hand. */
#define PE_CAP_PRIVATE_RANGES          (UINT64_C(1) << 1)
/** The preflop-to-flop transition is a real chance node over 3-card combinations. */
#define PE_CAP_FLOP_CHANCE             (UINT64_C(1) << 2)
/** Draw games: private chance on each drawing street. */
#define PE_CAP_DRAW_CHANCE             (UINT64_C(1) << 3)

/* ------------------------------------------------------------------ *
 * Guarantees
 * ------------------------------------------------------------------ */

/** More than two players are supported. */
#define PE_CAP_MULTIWAY                (UINT64_C(1) << 4)
/** The game is two-player zero-sum, so Nash and exploitability are meaningful. */
#define PE_CAP_ZERO_SUM_GUARANTEE      (UINT64_C(1) << 5)
/** Payoffs need not sum to zero (rake, bounties). */
#define PE_CAP_NON_ZERO_SUM            (UINT64_C(1) << 6)
/** Terminal utility is non-linear in chips (ICM, risk profiles). */
#define PE_CAP_NONLINEAR_UTILITY       (UINT64_C(1) << 7)

/* ------------------------------------------------------------------ *
 * Chance handling
 * ------------------------------------------------------------------ */

/** Chance outcomes can be enumerated exhaustively. */
#define PE_CAP_ENUMERATED_CHANCE       (UINT64_C(1) << 8)
/** Chance outcomes can be sampled directly, with an importance ratio. */
#define PE_CAP_DIRECT_CHANCE_SAMPLING  (UINT64_C(1) << 9)

/* ------------------------------------------------------------------ *
 * Abstraction and execution
 * ------------------------------------------------------------------ */

/** Strength buckets and texture filters are wired into the infoset key. */
#define PE_CAP_ABSTRACTION             (UINT64_C(1) << 10)
/** Boards are folded into suit-isomorphism classes. */
#define PE_CAP_SUIT_ISOMORPHISM        (UINT64_C(1) << 11)
/** Updates cross the compute port in batches rather than one node at a time. */
#define PE_CAP_BATCH_UPDATES           (UINT64_C(1) << 12)
/** The traversal runs on several CPU threads. */
#define PE_CAP_CPU_PARALLEL            (UINT64_C(1) << 13)

/* ------------------------------------------------------------------ *
 * GPU maturity levels (see architecture §9.2)
 * ------------------------------------------------------------------ */

/** GPU-1: terminal evaluation by batch. */
#define PE_CAP_GPU_TERMINAL_EVAL       (UINT64_C(1) << 14)
/** GPU-2: sorted showdown with blocker removal, on device. */
#define PE_CAP_GPU_VECTOR_SHOWDOWN     (UINT64_C(1) << 15)
/** GPU-3/4: strategy and regret update kernels. */
#define PE_CAP_GPU_REGRET_UPDATE       (UINT64_C(1) << 16)
/** GPU-6: the traversal itself runs on device. */
#define PE_CAP_GPU_TRAVERSAL           (UINT64_C(1) << 17)

/* ------------------------------------------------------------------ *
 * Solver features
 * ------------------------------------------------------------------ */

/** Regret-based pruning. */
#define PE_CAP_RBP                     (UINT64_C(1) << 18)
/** Infosets can be pinned to a fixed strategy. */
#define PE_CAP_LOCKED_STRATEGY         (UINT64_C(1) << 19)
/** Locked infosets are re-asserted periodically, with EV-loss measurement. */
#define PE_CAP_PERIODIC_RELOCK         (UINT64_C(1) << 20)
/** Subgames can be re-solved soundly (CFR-D gadget). */
#define PE_CAP_SUBGAME_RESOLVE         (UINT64_C(1) << 21)

/* ------------------------------------------------------------------ *
 * Operations
 * ------------------------------------------------------------------ */

/** Backend-independent checkpoints can be written and restored. */
#define PE_CAP_CHECKPOINT              (UINT64_C(1) << 22)
/** Two runs with the same seed produce bit-identical results. */
#define PE_CAP_DETERMINISTIC           (UINT64_C(1) << 23)
/** Exploitability comes from an imperfect-information best response. */
#define PE_CAP_IMPERFECT_INFO_BR       (UINT64_C(1) << 24)

/* ------------------------------------------------------------------ *
 * Aggregates
 * ------------------------------------------------------------------ */

/** Number of named capabilities. */
#define PE_CAP_COUNT 25

/** Every named capability. Bits outside this mask have no name yet. */
#define PE_CAP_ALL (                    \
    PE_CAP_VECTOR_FORM               |  \
    PE_CAP_PRIVATE_RANGES            |  \
    PE_CAP_FLOP_CHANCE               |  \
    PE_CAP_DRAW_CHANCE               |  \
    PE_CAP_MULTIWAY                  |  \
    PE_CAP_ZERO_SUM_GUARANTEE        |  \
    PE_CAP_NON_ZERO_SUM              |  \
    PE_CAP_NONLINEAR_UTILITY         |  \
    PE_CAP_ENUMERATED_CHANCE         |  \
    PE_CAP_DIRECT_CHANCE_SAMPLING    |  \
    PE_CAP_ABSTRACTION               |  \
    PE_CAP_SUIT_ISOMORPHISM          |  \
    PE_CAP_BATCH_UPDATES             |  \
    PE_CAP_CPU_PARALLEL              |  \
    PE_CAP_GPU_TERMINAL_EVAL         |  \
    PE_CAP_GPU_VECTOR_SHOWDOWN       |  \
    PE_CAP_GPU_REGRET_UPDATE         |  \
    PE_CAP_GPU_TRAVERSAL             |  \
    PE_CAP_RBP                       |  \
    PE_CAP_LOCKED_STRATEGY           |  \
    PE_CAP_PERIODIC_RELOCK           |  \
    PE_CAP_SUBGAME_RESOLVE           |  \
    PE_CAP_CHECKPOINT                |  \
    PE_CAP_DETERMINISTIC             |  \
    PE_CAP_IMPERFECT_INFO_BR)

/**
 * Buffer size that always holds the text form of any uint64_t, including the
 * separators and a trailing hex token for unnamed bits. Callers that size a
 * buffer with this never have to check for truncation.
 */
#define PE_CAPS_STRING_MAX 448

/** Text used for an empty capability set, in both directions. */
#define PE_CAPS_NONE_TOKEN "NONE"

/* ------------------------------------------------------------------ *
 * Names
 * ------------------------------------------------------------------ */

/**
 * Name of a single capability.
 *
 * @param cap  Exactly one named capability bit.
 * @return The name, or NULL when `cap` is zero, has several bits set, or is
 *         not a named capability.
 */
const char *pe_cap_name(uint64_t cap);

/**
 * Capability bit for a name. The comparison is case-insensitive.
 *
 * @return The bit, or 0 when the name is unknown or NULL.
 */
uint64_t pe_cap_from_name(const char *name);

/**
 * Capability bit at `index` in declaration order, for iteration.
 *
 * @param index  In [0, PE_CAP_COUNT).
 * @return The bit, or 0 when `index` is out of range.
 */
uint64_t pe_cap_at(size_t index);

/* ------------------------------------------------------------------ *
 * Text form
 * ------------------------------------------------------------------ */

/**
 * Render `caps` as names joined by '|', in declaration order. An empty set
 * renders as PE_CAPS_NONE_TOKEN. Bits with no name are appended as a single
 * hexadecimal token so nothing is lost.
 *
 * Follows snprintf semantics: `buf` is always NUL-terminated when `buflen` is
 * non-zero, and the return value is the length the full text would have.
 *
 * @return The length the complete text would occupy, excluding the NUL.
 *         A value >= buflen means the output was truncated. Returns 0 when
 *         `buf` is NULL and `buflen` is non-zero.
 */
size_t pe_caps_to_string(uint64_t caps, char *buf, size_t buflen);

/**
 * Parse the text form. Accepts names in any case, the hex tokens produced by
 * pe_caps_to_string, PE_CAPS_NONE_TOKEN, and spaces around separators.
 *
 * @param text      Text to parse.
 * @param out_caps  Receives the capability set. Untouched on failure.
 * @return 0 on success.
 *         -1 when an argument is NULL.
 *         Otherwise the 1-based index of the first token that could not be
 *         recognised, so a caller can point at it.
 */
int pe_caps_parse(const char *text, uint64_t *out_caps);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_CAPABILITIES_H */
