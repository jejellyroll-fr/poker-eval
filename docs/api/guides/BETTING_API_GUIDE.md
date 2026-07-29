# Betting Engine API Guide

The betting engine API exposes the decision layer that powers the internal game engine. It allows callers to query which actions are legal for a player, validate custom actions, and inspect sizing constraints without touching the private `GameState` structures.

## Creating the engine

```c
#include <poker_eval/engine/betting_api.h>
#include <poker_eval/engine/engine_api.h>

pe_betting_engine_t *betting = NULL;
pe_status_t st = pe_betting_engine_create(BETTING_STRUCTURE_NO_LIMIT, NULL, &betting);
if (st != PE_STATUS_OK) {
    /* handle error */
}
```

Passing `NULL` for the limits parameter applies the default structure-specific limits (e.g., min/max bet for limit and spread games). Custom `BettingLimits` can be supplied to override those defaults.

## Querying legal actions

```c
const BettingAction *actions = NULL;
int count = 0;
st = pe_betting_engine_get_valid_actions(betting, state, player_id, &actions, &count);
if (st == PE_STATUS_OK && actions) {
    for (int i = 0; i < count; ++i) {
        printf("%d) type=%d amount=%lld range[%lld,%lld]\n",
               i,
               actions[i].action,
               (long long)actions[i].amount,
               (long long)actions[i].min_amount,
               (long long)actions[i].max_amount);
    }
}
```

Returned actions cover:

* `ACTION_FOLD`, `ACTION_CHECK`, `ACTION_CALL`
* `ACTION_BET` or `ACTION_RAISE` (with min/max sizing)
* `ACTION_ALL_IN` when the player still has chips

The array is owned by the betting engine and remains valid until the next query or until the engine is destroyed.

## Validating an action

```c
BettingAction candidate = {
    .action = ACTION_RAISE,
    .amount = 450
};
bool valid = false;
pe_betting_engine_validate_action(betting, state, player_id, &candidate, &valid);
if (!valid) {
    /* reject or clamp the action */
}
```

Use the dedicated helpers to inspect sizing boundaries:

* `pe_betting_engine_get_min_bet`
* `pe_betting_engine_get_max_bet`
* `pe_betting_engine_get_call_amount`
* `pe_betting_engine_get_structure`
* `pe_betting_engine_get_limits`

## Supported structures

The engine ships with four betting structures:

| Enum                        | Description                                                      |
|-----------------------------|------------------------------------------------------------------|
| `BETTING_STRUCTURE_NO_LIMIT`   | Standard no-limit sizing (min bet = 1 big blind)              |
| `BETTING_STRUCTURE_POT_LIMIT`  | Pot-limit sizing (max raise = current pot + twice the call)   |
| `BETTING_STRUCTURE_FIXED_LIMIT`| Fixed-limit streets with capped raises                        |
| `BETTING_STRUCTURE_SPREAD_LIMIT`| Spread limit between `min_bet` and `max_bet` per street      |

Each structure respects the default limits provided in `BettingLimits`. Those values can be overridden when creating the engine.

## Example

See `tests/test_betting_api.c` for end-to-end samples covering no-limit, pot-limit, fixed-limit, and spread-limit scenarios. The same patterns can be copied into higher level tooling (bots, solvers, or training pipelines).
