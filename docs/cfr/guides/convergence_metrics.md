# Multiplayer convergence metrics (issue #234)

This guide formalises the metrics poker-eval uses to describe the quality of a
solved strategy profile, especially for multiplayer (multiway) games where a
single scalar exploitability number is not meaningful. The authoritative
definitions live in `include/poker_eval/solver/pe_solver.h`
(`pe_metrics_t`) and `include/poker_eval/solver/pe_best_response.h`.

## Definitions

For a profile `sigma`, let `BR_i(sigma_-i)` be player `i`'s best response to
the other players' strategies.

| Metric | Definition | Where |
|--------|------------|-------|
| `br_gap[i]` | `u_i(BR_i(sigma_-i), sigma_-i) - u_i(sigma)`, clamped at 0 | `pe_metrics_t::br_gap` |
| `nash_conv` | `sum_i br_gap[i]` — the NashConv of the profile | `pe_metrics_t::nash_conv` |
| `max_br_gap` | `max_i br_gap[i]` — the worst single player | `pe_metrics_t::max_br_gap` |
| `mean_br_gap` | `nash_conv / num_players` | `pe_metrics_t::mean_br_gap` |
| `exploitability_raw` | legacy alias of `nash_conv` (kept for ABI compatibility) | `pe_metrics_t::exploitability_raw` |

`nash_conv` is the total unilateral improvement still available across all
players. The aggregate alone can hide one badly exploitable player — a
3-player profile with gaps `[0.01, 0.01, 5.0]` has the same NashConv as one
with `[1.67, 1.67, 1.68]` — which is why `max_br_gap` and `mean_br_gap` are
always reported alongside it, and why per-player `br_gap` is never collapsed
away for multiplayer measurements.

### Companion diagnostics

Two further caller-supplied diagnostics are carried in the same raw currency:

- `cce_gap` — the deviation gain available under a coarse correlated
  equilibrium (deviations permitted by a correlating device). The built-in
  measurement paths do not compute it yet and report `0.0`; external callers
  who compute it must pass a non-negative value in the same raw currency.
- `utility_imbalance` — a non-negative measure of how unbalanced the realised
  utility vector is (how far the profile's payoffs are from symmetric/balanced
  play). Same convention: `0.0` from the built-in paths, caller-supplied
  otherwise.

## Units and normalisation

Raw metrics (`nash_conv`, `br_gap[i]`, `max_br_gap`, `mean_br_gap`) are in the
game's own currency, per game. The unit is stated explicitly through
`pe_metrics_t::nash_conv_unit` (see `pe_metric_unit_name()`):

| Unit | Name | Conversion |
|------|------|------------|
| chips/game | `chips/game` | raw value |
| bb/game | `bb/game` | `raw / big_blind` |
| mBB/game | `mbb/game` | `raw / big_blind * 1000` |

`big_blind` is the `execution.big_blind` configuration value; it is echoed in
`pe_metrics_t::big_blind`. Pre-normalised variants are provided:
`nash_conv_bb_per_game` and `nash_conv_mbb_per_game`. Every printed output
(CLI stdout, JSON report, Studio) states the unit alongside the value.

## Guarantees: exact vs sampled vs no-regret

A measured value must never be confused with a guarantee. Two fields separate
the concerns:

- `br_mode` (`pe_br_mode_t`) — how the measurement was produced:
  - `PE_BR_EXACT`: deterministic full traversal. The numbers are
    ground truth (up to floating-point round-off).
  - `PE_BR_SAMPLED`: empirical estimate from random trajectories.
  - `PE_BR_AUTO`: resolved to one of the two; the result reports the mode
    actually used.
- `guarantee` (`pe_guarantee_t`) — what the measurement certifies:
  - `PE_GUARANTEE_NASH`: exact measurement of a two-player zero-sum game;
    `nash_conv == 0` certifies a Nash equilibrium.
  - `PE_GUARANTEE_NO_REGRET_ONLY`: exact measurement of a multiway (or
    otherwise non-Nash-certifying) zero-sum game. A zero NashConv certifies a
    coarse-correlated / no-regret outcome, **not** a Nash equilibrium.
  - `PE_GUARANTEE_EMPIRICAL`: the measurement sampled. No equilibrium claim
    is licensed, regardless of how small the value is. A low sampled value
    alone must never be read as an equilibrium guarantee.
  - `PE_GUARANTEE_UNSPECIFIED`: no game topology was available to classify
    the measurement.

## Sampling metadata

When `br_mode == PE_BR_SAMPLED`, `pe_metrics_t` also carries:

- `sample_count` — total trajectories drawn by the measurement.
- `seed` — the RNG stream used (a deterministic function of the solve seed
  and the measurement iteration), so a measurement can be replayed.
- `measurement_iteration` — the solve iteration at which the measurement was
  taken. The metrics of a *previous* measurement stay visible until the next
  check runs; this field disambiguates them.
- `standard_error`, `confidence_interval_95` — reserved statistics of
  `nash_conv` in its raw unit. **Zero means "not computed", not "no error".**
  The current sampled best response does not expose per-trajectory payoffs,
  so no honest interval can be derived and both stay `0.0`.

On an exact measurement `sample_count`, `seed` and both statistics are `0`.

## Multiplayer stopping criteria

In addition to the historical `target_exploitability_mbb`, the configuration
accepts:

- `target_nash_conv_mbb` — stop when `nash_conv_mbb_per_game <= target`.
- `target_max_br_gap_mbb` — stop when the worst player's BR gap, in mBB/game,
  is at or below the target.

A stop fires only when **every** enabled (non-zero) target is satisfied by the
same BR measurement. All targets require `execution.big_blind > 0` and
`exploitability_interval > 0`.

Guarantee semantics carry over to stopping: when the measurement is sampled,
reaching a threshold is an **empirical** observation about an estimate, never
a certificate. Only a `PE_BR_EXACT` measurement of a two-player zero-sum game
promotes "target reached" into a guarantee.

## CLI / Studio

- `pe_preflop_solve` accepts `--target-nash-conv-mbb N` and
  `--target-max-br-gap-mbb N`; its final `guarantee=...` line appends
  `nash_conv_raw`, `nash_conv_mbb`, `max_br_gap_mbb`, `mean_br_gap_mbb`,
  `unit`, `measurement_iteration` and `sample_count`; the JSON report carries
  the same fields under `metrics`.
- The Studio final summary shows the guarantee, the measurement mode, the
  aggregate and the worst player's gap, so a small sum cannot hide one badly
  exploitable player.
