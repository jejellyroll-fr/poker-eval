/*
 * poker_eval.h - Complete Poker Evaluation Library API
 *
 * Copyright (C) 2025 poker-eval contributors
 *
 * Complete poker evaluation API. Include this for full functionality.
 * Use: #include <poker_eval/poker_eval.h>
 *
 * This is the main umbrella header that provides access to all
 * poker evaluation functionality organized by modules.
 */

#ifndef POKER_EVAL_H
#define POKER_EVAL_H

/* ===== Core Functionality ===== */
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/handval.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/eval.h>

/* ===== Deck Management ===== */
#include <poker_eval/deck/deck.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/deck/deck_joker.h>
#include <poker_eval/deck/deck_short.h>
#include <poker_eval/deck/deck_astud.h>

/* ===== Game Engine ===== */
#include <poker_eval/engine/engine_api.h>

/* ===== Betting Engine ===== */
#include <poker_eval/engine/betting_api.h>

/* ===== Game Rules ===== */
#include <poker_eval/games/game_std.h>
#include <poker_eval/games/game_astud.h>
#include <poker_eval/games/game_joker.h>
#include <poker_eval/games/game_short.h>
#include <poker_eval/games/rules_std.h>
#include <poker_eval/games/rules_astud.h>
#include <poker_eval/games/rules_joker.h>
#include <poker_eval/games/rules_short.h>

/* ===== Equity Calculations ===== */
#include <poker_eval/equity/RangeEquity.h>
#include <poker_eval/equity/batched_montecarlo.h>
#include <poker_eval/equity/enumerate_eedc.h>
#include <poker_eval/equity/enumord.h>

/* ===== Hand Distributions ===== */
#include <poker_eval/distributions/holdem_distributions.h>
#include <poker_eval/distributions/omaha_distributions.h>
#include <poker_eval/distributions/stud_distributions.h>
#include <poker_eval/distributions/plo_nomenclature.h>

/* ===== Open Face Chinese Poker ===== */
#include <poker_eval/ofc/ofc.h>

/* ===== Utilities ===== */
#include <poker_eval/utils/micro_optimizations.h>
#include <poker_eval/utils/poker_wrapper.h>
#include <poker_eval/core/universal_deck.h>

/* ===== Version Information ===== */
#define POKER_EVAL_API_VERSION_MAJOR 1
#define POKER_EVAL_API_VERSION_MINOR 0
#define POKER_EVAL_API_VERSION_PATCH 0
#define POKER_EVAL_API_VERSION_STRING "1.0.0-canonical"

/* ===== Convenience Macros ===== */

/* Initialize library (if needed for future extensions) */
#define POKER_EVAL_INIT() /* Currently no initialization required */

/* Cleanup library (if needed for future extensions) */
#define POKER_EVAL_CLEANUP() /* Currently no cleanup required */

#endif /* POKER_EVAL_H */
