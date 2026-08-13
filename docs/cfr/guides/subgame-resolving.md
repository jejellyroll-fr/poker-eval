# Subgame Re-solving (FEAT-05)

Re-solve a single subtree of an already-solved (blueprint) game without
re-solving the whole tree, holding the value at the subtree boundary fixed to
the blueprint. This is the **CFR-D / gadget re-solving** method of Burch,
Johanson & Bowling, *"Solving Imperfect Information Games Using
Decomposition"* (AAAI 2014).

Status: ✅ (2-player games; multiway uses the trunk-locked fallback)

## Why it exists

After a full tree is solved into a blueprint, you often want to refine a small
part of it — a new bet size, a corrected range, a spot you under-abstracted —
without paying for another full solve. Naively re-solving the subtree in
isolation is unsound: the opponent is never allowed to *avoid* the subtree, so
the refined strategy can be far more exploitable than the blueprint. The gadget
restores soundness with a value constraint at the boundary.

## Two modes

### 1. Trunk-locked re-solve (`pe_cfr_seed_resolve_storage`)

Lock every infoset **outside** the subgame to its blueprint strategy using the
FEAT-01 node-locking machinery, then run `cfr_solve()` on the *full* game. Only
the subgame trains. Cheap and simple, but **unsound**: the opponent cannot avoid
the subgame, so exploitability may rise. Use it for quick refinement, debugging,
or when the subgame is the whole remaining game.

```c
size_t locked = 0, free = 0;
pe_cfr_seed_resolve_storage(game, blueprint, resolve_storage,
                            subgame_root, user, &locked, &free);
/* now cfr_solve(game, resolve_storage, &cfg, &expl) trains the subgame only */
```

### 2. CFR-D gadget re-solve (`pe_cfr_resolve_subgame`)

A **gadget** is placed on top of the subgame root: in every boundary infoset
the opponent chooses between

- **Terminate** — take a fixed payoff equal to its blueprint
  counterfactual value (CFV) for that infoset, or
- **Follow** — enter the subgame and play it out.

The re-solver (the player whose strategy is being refined) must therefore keep
giving the opponent at least its blueprint CFV in every boundary infoset;
otherwise the opponent terminates and the re-solve is punished. That is the
value constraint that makes re-solving sound.

```c
pe_cfr_boundary_t boundary[1];
boundary[0].infoset = subgame_root_key;   /* boundary infoset key */
/* leave cfv/reach at 0 to let the resolver compute them from the blueprint */

pe_cfr_subgame_t sub = {
    .root_state_key = subgame_root_key,
    .resolve_player = 0,
    .boundary = boundary,
    .boundary_count = 1,
};

pe_cfr_resolve_config_t cfg = {0};
cfg.cfr.max_iterations = 2000;
cfg.cfr.enable_dcfr = 1;

pe_cfr_resolve_result_t result;
pe_cfr_resolve_subgame(game, blueprint, resolve_storage,
                       &sub, &cfg, user, &result);

/* result.constraints_satisfied == 1  => the value constraint holds
 * result.margins[i].follow_freq       => opponent's probability of entering */
```

## API

| Function | Purpose |
|----------|---------|
| `pe_cfr_blueprint_cfv` | Walk the full tree under the blueprint and fill each boundary infoset's CFV and reach. |
| `pe_cfr_subgame_infosets` | Collect (deduplicated) infoset keys reachable from the subgame root. |
| `pe_cfr_seed_resolve_storage` | Lock the trunk to the blueprint, leaving the subgame free. |
| `pe_cfr_gadget_create` / `pe_cfr_gadget_destroy` | Build / free the gadget game (a `cfr_game_t` decorator) if you want to drive `cfr_solve()` yourself. |
| `pe_cfr_gadget_follow_frequency` | Read the opponent's average probability of entering the subgame at a boundary. |
| `pe_cfr_resolve_subgame` | One-shot CFR-D re-solve with diagnostics. |

The gadget is a **decorator over `cfr_game_t`**: it forwards every vtable
callback to the inner game and intercepts only a small set of synthetic gadget
states, so it works with any adapter (multiway postflop, river adapters,
hand-rolled games) and with the FEAT-09 `.pe_sol` blueprint loader.

## Blueprint source

The blueprint is an ordinary solved `cfr_storage_t`. Load it from:

- a checkpoint — `cfr_storage_load_checkpoint`, or
- a FEAT-09 compact file — `pe_cfr_load_storage`.

Boundary CFVs are computed by `pe_cfr_blueprint_cfv`, or you can supply them
yourself when you already have them from a prior full solve.

## Limitations

- **2 players only for the gadget.** CFR-D's single-opponent CFV does not
  generalise to N>2 (non-zero-sum, no minimax value). For multiway, use the
  trunk-locked mode (`cfg.lock_trunk`), which `pe_cfr_resolve_subgame` also
  supports as a fallback. `pe_cfr_resolve_subgame` returns
  `PE_CFR_RESOLVE_UNSUPPORTED` for `num_players > 2` if you do not enable
  `lock_trunk`.
- **Range/bunching consistency across the boundary** is out of scope (see
  FEAT-14).
- The achieved per-infoset CFV after re-solving is reported approximately via
  the opponent's `follow_freq`; a fully converged gadget drives the opponent to
  `terminate` (follow ≈ 0) exactly when the constraint is tight.
