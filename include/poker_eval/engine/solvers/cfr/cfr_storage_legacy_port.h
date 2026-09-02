/*
 * cfr_storage_legacy_port.h - The v2 storage behind the v3 port (STO-04)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * Declared here rather than in pe_storage_port.h because the adapter lives in
 * poker_engine: it wraps cfr_storage_t, so it needs engine symbols, and
 * poker_engine already links poker_solver. Putting it in the port header would
 * promise poker_solver consumers a symbol their library does not carry.
 *
 * That split is the rule, not an exception: a port header declares the
 * adapters that ship with the port, and an adapter that wraps another module
 * is declared by that module.
 */

#ifndef POKER_EVAL_CFR_STORAGE_LEGACY_PORT_H
#define POKER_EVAL_CFR_STORAGE_LEGACY_PORT_H

#include <poker_eval/solver/pe_storage_port.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The v2 hash storage behind pe_storage_ops_t.
 *
 * The bridge of the migration: new code reads and writes through the port
 * while the game model still fills a cfr_storage_t. Scalar only, and it serves
 * regret and average alone — see max_combo_count and value_arrays on the
 * returned ops.
 *
 * Shared, immutable, always valid.
 */
const pe_storage_ops_t *pe_storage_legacy_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_CFR_STORAGE_LEGACY_PORT_H */
