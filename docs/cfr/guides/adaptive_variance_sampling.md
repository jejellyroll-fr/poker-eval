# Adaptive Variance Sampling

Issue #256. API: `include/poker_eval/solver/pe_sampling_policy.h` and
`include/poker_eval/solver/pe_online_stats.h`. Traversal:
`src/solver/domain/traversal_external.c`. Tests: `tests/test_pe_online_stats.c`,
`tests/test_adaptive_variance_sampling.c`.

`PE_SAMPLING_ADAPTIVE_VARIANCE` is a third sampling policy for Lane B external
sampling, next to `standard` and `street-balanced`. At every chance visit it
draws R times, and it chooses R from the **measured** spread of earlier visits
of the same kind. Estimates that are already resolved stop at the minimum.
Noisy ones get more draws, up to a maximum. Game semantics and the CFR
estimator do not change: only the amount of work does.

## Selecting it

```c
cfg.algorithm.sampling_policy = PE_SAMPLING_ADAPTIVE_VARIANCE;
cfg.algorithm.adaptive.max_samples = 16;          /* optional: zero = default */
cfg.algorithm.adaptive.absolute_tolerance = 0.5;
```

```sh
pe-preflop-solve --algorithm external-mccfr --sampling-policy adaptive-variance \
                 --adaptive-max-samples 16 --adaptive-absolute-tolerance 0.5 ...
mpf_run_with_metrics --lane-b --sampling-policy adaptive-variance ...
```

The command-line flags are `--adaptive-<key> <value>`. The keys are the ones
`pe_adaptive_sampling_parse_option()` accepts.

| Setting | Key | Default (when 0) | Meaning |
|---|---|---|---|
| `min_samples` | `min-samples` | 2 | draws per visit, at least; raised to 2 so every visit measures its own spread |
| `max_samples` | `max-samples` | 32 (ceiling 4096) | draws per visit at most, and also the most one trajectory may multiply to (see below) |
| `check_interval` | `check-interval` | 64 | visits of a group between two budget updates |
| `confidence_level` | `confidence` | 0.95 | two-sided confidence of the half-width |
| `absolute_tolerance` | `absolute-tolerance` | 0 | target half-width, in value units |
| `relative_tolerance` | `relative-tolerance` | 0.05 when both are 0 | target half-width as a fraction of \|mean\| |

A zero-initialised configuration is valid. It changes nothing unless the
adaptive policy is selected: `standard` and `street-balanced` are
bit-identical to before. The adaptive fields enter the checkpoint
compatibility hash only when the adaptive policy is selected, so existing
checkpoints keep their hash. The command-line tools refuse the adaptive
settings with any other policy, and the policy with outcome sampling, rather
than recording an effect that never happened.

The adaptive settings, the persistence structs' sampler-state fields and
the traversal context's adaptive state enlarge public structs. For that
reason, the solver library's SOVERSION moves from 5 to 6: a program built
against the older headers is refused at load time rather than handed layouts
it did not allocate.

## The budget rule

Draws at the same kind of chance visit share statistics. The target
half-width is:

```text
tolerance = max(absolute_tolerance, relative_tolerance * |mean|)
```

The number of draws for the next visits is the smallest R whose confidence
half-width `z * sqrt(variance / R)` meets that tolerance:

```text
R = ceil(z^2 * variance / tolerance^2),   clamped to [min_samples, max_samples]
```

- `variance` is the **pooled within-visit** variance: each visit's draws
  measure their spread around that visit's own mean. Differences between
  visits (another deal, another board) do not count as sampling noise.
- `mean` is the running mean of visit values. A zero variance needs the
  minimum. A zero tolerance with some variance needs the maximum.

A group starts at the minimum and keeps it for its first `check_interval`
visits, which serve as the pilot sample. After that, the budget is recomputed
every `check_interval` visits.

A checkpoint carries what the groups have learned (budgets, next checks,
means and pooled spreads), so a resumed solve continues at its learned
budgets instead of re-running the pilot. This matters for work split into
short resumed jobs. The state travels as a sampler-state section of the
checkpoint (format version 3). Version 2 checkpoints, which have no such
section, still load: they simply start with the pilot.

### Which visits share statistics

The groups are the four **streets** when the adapter tags its chance draws.
Otherwise they are the **chance depth** along the trajectory: the root deal is
depth 0, the next draw is depth 1, and so on, with depth 3 and deeper sharing
one group. The two poker adapters behave differently:

- the preflop game behind `pe-preflop-solve` tags its chance draws, so its
  statistics use the street groups;
- the multiway postflop adapter behind `mpf_run_with_metrics` does not tag
  them, so depth is what makes the policy work on it, with no change to the
  adapter.

Each updating player keeps its own statistics, since values are seen from its
side.

### Nested chance visits

Replicates multiply down a trajectory: 8 root deals times 8 turns times 8
rivers is 512. `max_samples` therefore also caps that product. A visit below
others that already drew R1, R2, … draws at most
`max_samples / (R1 × R2 × …)`, and may then fall below `min_samples`. Without
this cap, a PLO5 flop root with a dealt turn and river could draw 32 × 32 × 32
trajectories per iteration.

## Why the estimator stays unbiased

There are two reasons.

1. **R is decided before the visit draws anything.** It depends only on
   statistics of earlier, independent visits, and the trajectory cap depends
   only on the replicate counts above the visit, which were also fixed before
   its draws. Given R, the mean of R unbiased draws is unbiased, so the
   visit's value is too. A policy that stopped a visit when its *own* draws
   looked resolved would bias it (optional stopping). This one never looks at
   the draws it is deciding about.
2. **Each replicate's updates enter the batch at weight 1/R.** In
   expectation, a visit contributes exactly what one standard draw
   contributes. R lowers the variance of the update, never its expectation or
   its weight across iterations. This is the deliberate difference from
   `street-balanced`, which weights every replicate fully so that deep streets
   get R times more updates.

The tests check both points on a game with a known answer:

- the average-strategy mass of an iteration is exactly 1, as under standard
  sampling, whatever R is;
- over 20,000 iterations, the mean regret matches its exact expectation of 0.5
  within four standard errors for standard, fixed-R and adaptive sampling;
- removing the 1/R weight inflates it to about 2 at R = 4 and 8 under the
  adaptive policy, and fails the suite.

## Telemetry

With the adaptive policy, a Lane B run reports one line per active group:

```text
adaptive_stats group=chance-depth:1 estimates=446 samples=5344 avg_samples=11.982
  min_hits=128 max_hits=318 resolved=7 variance=66.9 std_error=2.36 half_width=4.63
sampling_totals policy=adaptive-variance terminal_evaluations=59957
```

The fields are:

- `estimates`: visits;
- `samples` and `avg_samples`: the effective budget;
- `min_hits` / `max_hits`: visits at the minimum, or cut by the maximum or the
  trajectory cap;
- `resolved`: visits whose own half-width met the tolerance, which is the
  early-resolution rate;
- the pooled variance, and the standard error and half-width at the average
  budget.

`sampling_totals` gives every external-sampling run's terminal evaluations.
`mpf_run_with_metrics` prints these lines when `--sampling-policy` is given,
so policies can be compared. The counters are plain increments on the chance
path.

`pe_online_stats_t` (Welford) and `pe_pooled_variance_t` are reusable on their
own. They are allocation-free and numerically stable: with values around
`1e9`, the sum-of-squares formula loses every digit of the variance, and
Welford keeps it. `pe_online_stats_side()` reports whether a confidence
interval lies entirely above or below a decision boundary.

## When it helps

Adaptive sampling spends **fewer samples than a fixed replicate count** where
the variance is low. It never spends fewer than standard sampling, which
draws once. The choice is between fixed replication and adaptive replication.
Adaptive keeps the variance reduction where it is needed and drops it where it
is not. In the solver-level test, fixed R = 8 and adaptive sampling reach the
same strategy (P(better action) = 0.998), and adaptive uses 4,880 terminal
evaluations against 8,480.

## Known limitation: the multiway postflop adapter

Replicating a chance draw means visiting its parent state again after one
replicate's subtree has been traversed. The multiway postflop adapter (MPF,
behind `mpf_run_with_metrics`) keeps **one cached state per tree node and
thread**. A `pe-preflop-tree` flop tree describes flop betting only, so turn
and river betting resolve to the same tree nodes. A deeper state then
overwrites an ancestor that is still in use.

This is a pre-existing bug, and plain standard sampling hits it too. On master,
a 40,000-iteration Hold'em flop solve fails at iteration 4,024. Replication
reaches the corrupted parent much sooner: fixed R = 8 fails at iteration 332.
The policy itself is correct; the adapter's cache is not re-entrant. Until
that is fixed, prefer the preflop game (`pe-preflop-solve`) for replicated
sampling. That is where the benchmarks below run.

## Benchmarks

`scripts/benchmarks/bench_adaptive_sampling.py --build <dir>` solves four
spots with `pe-preflop-solve`, all with full private ranges, so the
strategies are genuinely mixed:

- Hold'em heads-up;
- PLO4 heads-up;
- PLO5 heads-up;
- PLO4 three-way.

Each spot runs under three policies, 2,000 iterations each, over 5 seeds:

- **standard**;
- **fixed R = 8**: the adaptive machinery with min = max = 8, so the estimator
  and its 1/R weights are the same;
- **adaptive**: min 2, max 8.

Quality is the empirical NashConv of the final average strategy (4,000
best-response samples), as mean ± standard deviation over the seeds.

**Calibrating the tolerance.** The preflop game's chance draws carry the deal
sampler's importance weights, so their values have the game's own, large
scale: a per-draw spread of about 1e12 in PLO4. An absolute tolerance in chips
means nothing there, and a relative one fails too, because these values
average near zero. The script therefore calibrates the absolute tolerance per
spot. A 300-iteration pilot at two draws measures the spread, and the script
sets the tolerance that spread would meet with about four draws.

Measured on Apple silicon, Debug build:

| Spot | Policy | Terminal evals | Avg draws | NashConv (mBB/game) |
|---|---|---|---|---|
| Hold'em HU | standard | 3,581 | 1.00 | 18,228 ± 439 |
| | fixed R = 8 | 27,159 | 8.00 | 8,561 ± 670 |
| | adaptive | 13,356 | 3.84 | 11,192 ± 703 |
| PLO4 HU | standard | 3,503 | 1.00 | 21,120 ± 455 |
| | fixed R = 8 | 28,537 | 8.00 | 23,680 ± 712 |
| | adaptive | **16,596 (−42%)** | 4.67 | **22,334 ± 312** |
| PLO5 HU | standard | 3,508 | 1.00 | 19,930 ± 296 |
| | fixed R = 8 | 28,091 | 8.00 | 20,799 ± 392 |
| | adaptive | **16,978 (−40%)** | 4.84 | **20,103 ± 209** |
| PLO4 3-way | standard | 3,828 | 1.00 | 55,462 ± 759 |
| | fixed R = 8 | 30,818 | 8.00 | 55,595 ± 1,262 |
| | adaptive | **16,139 (−48%)** | 4.19 | **54,665 ± 1,717** |

What the numbers say:

- **Adaptive versus fixed replication.** On all three Omaha workloads, the
  adaptive policy reaches the same or a slightly lower NashConv than fixed
  R = 8 with 40–48% fewer terminal evaluations. That is the saving the policy
  exists for.
- **Hold'em behaves differently.** Replicating the private deal pays off a lot
  there (standard 18,228, R = 8 8,561), so an adaptive budget near 4 draws
  gives some of that back (11,192) for half the evaluations. A tighter
  tolerance moves it towards R = 8.
- **Standard is best in Omaha at equal iterations.** In these Omaha spots, one
  draw per visit scores as well as or better than any replication, for about
  a seventh of the evaluations. Replication is a variance-reduction tool with
  a cost. The policy makes that cost smaller, but whether to replicate at all
  depends on the spot.
- **One group only.** The preflop game has a single chance group (the private
  deal). There, adaptivity amounts to choosing R from the measured spread
  rather than guessing it. Games with several chance streets let it allocate
  different budgets per group, as the unit tests exercise, but the multiway
  postflop adapter, where that would matter most, has the cache limitation
  described above.
