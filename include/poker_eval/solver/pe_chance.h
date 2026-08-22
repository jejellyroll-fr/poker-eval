/*
 * pe_chance.h - What kind of chance a node is (architecture v3, CHN-01)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * The v2 model asked one question — is a card pending? — and answered it with
 * a boolean. That was enough while the only chance node dealt a single board
 * card. RNG-03 added a second kind and, with it, a second boolean; a third
 * would have made the pair of flags a state machine nobody had written down.
 *
 * Naming the kinds makes the differences between them checkable rather than
 * implied. They are not interchangeable:
 *
 *   PRIVATE_HANDS  one outcome per joint deal of the players' hands. The
 *                  outcome count is a property of the ranges, not of the deck.
 *   FLOP_THREE     one outcome per *combination* of three cards, never per
 *                  ordered sequence — dealing the same flop six ways would
 *                  weight it six times.
 *   BOARD_ONE      one outcome per remaining card. The only kind the
 *                  chance_children[52] cache in mpf_state_t is valid for,
 *                  because it is the only one whose outcomes are cards.
 *   DRAW_N         private cards replaced on a drawing street.
 *
 * Declared in its own header rather than in the game-rules port: that port
 * describes a whole variant and does not exist yet, and inventing it around
 * one enum would produce a surface the ticket that owns it would rewrite.
 */

#ifndef POKER_EVAL_PE_CHANCE_H
#define POKER_EVAL_PE_CHANCE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    /** Not a chance node. */
    PE_CHANCE_NONE = 0,
    /** Deal the private hands from the players' ranges. */
    PE_CHANCE_PRIVATE_HANDS,
    /** Deal three board cards as one combination. */
    PE_CHANCE_FLOP_THREE,
    /** Deal one board card. */
    PE_CHANCE_BOARD_ONE,
    /** Replace private cards on a drawing street. */
    PE_CHANCE_DRAW_N,
    PE_CHANCE_KIND_COUNT
} pe_chance_kind_t;

/** Human-readable name, for diagnostics and tests. NULL when out of range. */
const char *pe_chance_kind_name(pe_chance_kind_t kind);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_CHANCE_H */
