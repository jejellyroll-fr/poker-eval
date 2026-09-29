# Uncertainty-Aware Work Prioritisation

Issue #258. API: `include/poker_eval/solver/pe_work_priority.h`. Implementation:
`src/solver/domain/work_priority.c`. Test: `tests/test_work_priority.c`, against
the brute-force oracle `tests/support/pe_work_priority_oracle.h`.

A solve spends the same work on every decision it has to make. That is wasteful
in both directions: an action that wins by a mile is settled after a handful of
samples, while a near tie is still a coin flip after thousands. The
confidence-guided best response (issue #257) already measures, per decision, how
far the leader is ahead of the runner-up and how uncertain that difference is.
This module turns those two numbers into a priority, and a batch of them into a
service order.

## What the audit found, before any code was written

The issue asked for the prioritisation to go *into* the existing distributed
scheduling stack. It cannot, and the reason is a contract, not an oversight.

| Component | What it arbitrates | Carries a decision-level signal? |
|---|---|---|
| `pe_work_schedule()` (`work_scheduler.c`) | a count of interchangeable units across **backends**, in proportion to each backend's measured `units_per_s` | no — the units are identical by construction |
| `pe_work_assign()` | that allocation into contiguous index ranges | no |
| `pe_work_coordinator_schedule()` (`work_coordinator.c`) | the same count across **workers**, again by measured rate | no |
| `pe_work_unit_t` (`pe_work_unit.h`) | the serialised payload: public state, player, iteration range, boards, ranges, a regret snapshot | no — no score, no visit count, no gap |
| `pe_work_executor.h` | a worker's receive/execute/reply loop | no |
| `pe_sampling_policy_t` (#256) | replication of a chance draw, grouped by **street / chance depth** | no — the group is not a decision |

Two further facts settle it. First, nothing in `src/` outside the `work_*` files
and `cfr_work_adapter.c` calls the distributed stack at all: its only consumers
are `tests/test_pe_work_*.c`. It is scaffolding without a production call site.
Second, `pe_work_schedule()`'s determinism guarantee is *"allocation is
proportional to the measured update rate"* — giving it a per-decision notion of
value would not extend that contract, it would replace it.

**Conclusion: no production defect, and a partly wrong premise.** The
deliverable is therefore a read-only layer that computes the priority and
returns a permutation, usable by the coordinator once it is wired in, by the
adaptive sampler, or by anything else that holds per-decision metadata. Nothing
in `work_scheduler.c`, `work_coordinator.c`, `work_unit.c` or `work_executor.c`
was touched, and no existing public struct grew, so there is no SOVERSION bump.

## Selecting it

```c
pe_work_priority_config_t cfg;
memset(&cfg, 0, sizeof(cfg));            /* all defaults: FIFO, no change */
cfg.policy = PE_WORK_SCHED_UNCERTAINTY_AWARE;
cfg.min_visits = 4;
cfg.aging_interval = 64;
```

```c
pe_work_priority_resolve(&cfg, &resolved);   /* fills defaults, validates */
```

| Setting | Default (when 0) | Meaning |
|---|---|---|
| `policy` | `fifo` | `fifo` / `balanced` / `uncertainty-aware` |
| `min_visits` | 1 | coverage floor: below it, a decision is ranked ahead of every measured one |
| `epsilon` | 1e-9 | floor on the gap, so the score stays finite |
| `buckets` | 8 | geometric buckets; 1 or more than 32 is refused |
| `bucket_ratio` | 2.0 | bucket boundaries are `ratio^0`, `ratio^1`, …; 1.0 or less is refused |
| `aging_interval` | 0 (off) | epochs of waiting per promoted bucket |
| `assumed_stderr` | 1.0 | spread assumed for a decision that supplies none, in value units; must be positive |

`pe_work_priority_parse_option()` accepts the same fields as
`policy`, `min-visits`, `epsilon`, `buckets`, `bucket-ratio`,
`aging-interval` and `assumed-stderr`, for a command line or a config file. The issue's proposed
`uncertainty_refresh_interval` has no counterpart: the layer is a pure function
of the metadata it is handed, so *when* to recompute is the caller's decision,
not a setting.

## The metric

```
gap         = best - second_best
uncertainty = hypot(best_stderr, second_stderr)
score       = uncertainty / max(gap, epsilon)
```

- **settled** — a wide gap and a tight estimate — scores low;
- **fragile** — a narrow gap and a wide estimate — scores high;
- **unresolved** — the runner-up is not behind, or a value is not a number —
  scores `+infinity` and saturates the top bucket;
- **nothing to decide** — fewer than two actions — scores 0.

The two spreads combine in quadrature because the score is about the
*difference* of two estimates.

A caller with no uncertainty information passes a zero stderr for both actions.
The decision is then scored on its gap against `assumed_stderr`:

```
score = assumed_stderr / max(gap, epsilon)
```

so the gap still orders the queue. With the default of 1.0, the issue's two
examples without any spread land at opposite ends: raise 12.4 / call 6.1 scores
0.16 (bucket 0), raise 2.102 / call 2.097 scores 200 (the top bucket). Scoring a
missing spread as zero instead would rank every positive gap alike, and the
policy would fall back to FIFO for every caller without variance information,
which is exactly the case this fallback exists for (adaptive sampling, #256,
off). Set `assumed_stderr` to the typical noise of the values, in their units,
so a decision without a spread competes fairly with measured ones.

A spread measured as exactly zero (identical draws) reads the same way. With no
noise to weigh, the gap is the only signal left, and in a solve the values keep
moving as the strategy updates, so a narrow gap stays fragile. A single
positive spread is information and is used as is.

Zero and *cannot say* are not the same thing, and the layer keeps them apart. A
NaN spread reads as unresolved and saturates the top bucket, because a decision
whose spread could not be estimated has not been shown to be settled. Only the
bridge below produces a NaN, and only when it has a reason to.

`pe_work_priority_item_from_stats()` fills an item straight from two
`pe_online_stats_t` accumulators, so a caller that already keeps them does not
have to know the layout. The bridge is a convenience, not a requirement. An
accumulator with fewer than two observations yields a NaN standard error, not a
zero one: `pe_online_stats_std_error()` cannot estimate a spread from a single
sample, and reading that as certainty would drop a decision that has barely been
measured into the lowest bucket. The item then scores as unresolved and is
ranked first. A single action still scores zero however thin the measurement —
there is nothing to decide.

The score is unbounded, and a scheduler must not be. It is quantised into
`buckets` geometric buckets, and **the ordering only looks at the bucket**:
within a bucket the input order is preserved. That is deliberate — a
fine-grained priority queue would shatter the batches the vector and
accelerator backends exist to consume. Priority precision is traded for
granularity, exactly as the issue asks.

## Anti-starvation

Two rules, and the interaction between them is where the suite earned its keep.

**The coverage floor is a tier, not a bucket.** A decision serviced fewer than
`min_visits` times is ranked ahead of *every* decision that has been serviced
enough, whatever either scored. Within the tier the input order decides, so
prioritisation proper begins only once every decision has had its minimum.

The first implementation promoted a below-floor decision into the top bucket
instead. That is wrong, and the synthetic workload test caught it: an *aged*
decision also reaches the top bucket, and at equal bucket the input position
decides, so an early-served decision could be promoted over one that had never
been served at all. On a 40-decision queue the floor phase never completed, and
the high-index decisions were never serviced once. Making the floor a tier
fixed it and turned the test green; the regression is now pinned by
`test_coverage_floor()`.

**Aging** promotes a decision that has waited `age` epochs by
`age / aging_interval` buckets, capped at the top. It applies below the floor
tier, so it can never jump an unserved decision.

Both are off under FIFO, which ranks nothing at all.

### Measured: what each rule does

Three decisions — two settled, one fragile — serviced once per round for 200
rounds, one item per round. Rows are `test_starvation()`'s printed counts.

| Configuration | Services to settled #1 | settled #2 | fragile |
|---|---|---|---|
| uncertainty-aware, aging **off** | 1 | 1 | 198 |
| uncertainty-aware, aging 4 | 9 | 9 | 182 |

With aging off, the coverage floor gives each settled decision exactly one turn
and then the fragile decision takes every remaining round: the floor guarantees
coverage, it does not guarantee return. Aging is the rule that brings the
settled region back, and the counts show it working.

## Ordering and batching

The key, highest priority first, is: **the coverage-floor tier**, then **the
bucket**, then **the input position**. The result is a permutation, fully
determined by the metadata, so identical input gives identical output.

- `fifo` — the input order, unchanged. The default.
- `uncertainty-aware` — highest bucket first, drained before the next.
- `balanced` — the same buckets, but one item per non-empty bucket per round,
  walking down from the round's starting bucket and wrapping, which bounds the
  service ratio between the top and the bottom bucket by the number of
  non-empty buckets. It costs some top-bucket throughput.

  The round's starting bucket sweeps the occupied buckets, highest first, over
  a period of `buckets` epochs. Epoch `e` starts at the
  `((e mod buckets) × n / buckets)`-th of the `n` occupied buckets, so each
  occupied bucket leads its share of every period and empty buckets take no
  turn. That rotation is what makes the bound hold for the caller the policy
  exists for — one that services a prefix of the
  order and recomputes. Without it every call would put the same top bucket at
  position zero and the policy would do nothing the strict one does not. A
  caller that consumes the whole permutation sees the rotation only as a change
  of order within a round, so epoch 0 is the plain highest-first order.

  Two simpler rotations were tried and rejected:

  - Stepping through every bucket *number* let each empty bucket hand its
    turn to the next occupied one down. With buckets 7 and 0 alone, one of
    them led seven epochs in eight, and with 32 buckets the skew reached 31:1.
  - Indexing the occupied buckets by `e mod n` is fair only while `n` holds
    still. A caller that services a prefix changes `n` from epoch to epoch,
    and the index then jumps about.

The coverage tier comes first under all three non-FIFO policies.

## Telemetry

`pe_work_priority_stats_t` carries the queue depth per bucket, the score
distribution, the floor and aging activations, the unresolved count, the mean
score and the mean delay. `pe_work_priority_format_stats()` renders one line, in
the style of the solver's other counters — this is `test_stats()`'s printed
output:

```text
work_priority items=4 buckets=8 coverage_promotions=1 aging_promotions=1 unresolved=1 mean_score=16.7725 mean_delay=33.750 p50_bucket=0 p90_bucket=7 depth=0,0,1,0,0,0,1,2
```

`depth` is the queue depth per bucket, lowest bucket first, so a baseline run
and an uncertainty-aware run can be compared field by field. The counters are
counted under FIFO too — the buckets describe the workload even when nothing is
reordered — which is what makes the baseline comparable.

The score's own distribution is kept separately, in `score_depth`, and counted
*before* the coverage floor or aging move anything. That is the difference the
two histograms show above. Items 0 and 3 both supply no spread, so both score
`assumed_stderr / gap` = 0.16 and sit in score bucket 0; in `depth` item 0 is
aged from bucket 0 to 2 while item 3 is below the floor and lands in the top
bucket. `score_depth` therefore has 2 in bucket 0 where `depth` has none, and 0
in bucket 2 where `depth` has one. `depth` describes what the policy did;
`score_depth` describes the workload it was given.

`p50_bucket` and `p90_bucket` are percentiles of that score distribution, by
nearest rank over `score_depth` (`pe_work_priority_percentile_bucket()` takes
any fraction). The score is quantised on purpose — the ordering only looks at
the bucket — so a percentile of it is a **bucket**, exact for the quantised
distribution. The bucket's range follows the boundary convention the config
documents — bucket 0 holds scores below `ratio^0`, so `[0, ratio^0)`, and
bucket `k > 0` holds `[ratio^(k-1), ratio^k)`. The top bucket is saturated, so
it holds `[ratio^(buckets-2), +infinity)`, which is where an unresolved score
lands. With the defaults, `p90_bucket=7` therefore means "at least 64", not
"at least 128". A reader wanting a value rather than a rank reads the boundary
off the ratio.

## Measured: does the priority do what it claims?

`test_synthetic_workload()` builds 40 decisions, 10 of them near-indifferent
(`gap` 0.001 against a spread of 0.05, so a score near 50) and 30 settled (`gap`
of 9 or more against a spread of 0.01, so a score near 0.001), and services
2,000 of them one at a time.

| Policy | Share of work on the near-indifferent group | Worst delay between two services of one decision |
|---|---|---|
| FIFO (rotating) | 25.0% — the group is a quarter of the queue | 39 rounds |
| uncertainty-aware, aging 8 | **60.1%** | 80 rounds |
| balanced, aging 8 | — | 244 rounds |

The prioritised run spends **2.4×** the baseline share on the decisions that are
actually undecided, and every decision is still serviced: the worst delay is
80 rounds, finite and bounded, against 39 for the even baseline. That is the
trade the feature exists to make.

`balanced` bounds the service ratio between **buckets**, not the delay of a
single decision, and on this workload that makes its per-decision worst case
longer: 244 rounds, against 80 for the strict policy.

- With aging on, a decision climbs a bucket for every 8 epochs it waits. The
  high buckets therefore hold the decisions that have waited longest, and
  bucket 0 holds the ones that have just been serviced.
- A fair share of turns for bucket 0 is spent re-serving the same
  freshly-served decision: within a bucket the input order decides.

An earlier rotation stepped through bucket numbers and printed 65 rounds here.
It got that figure by accident, handing most of the turns to the high buckets,
and it broke the bucket bound the policy promises (see above). Use `balanced`
for its bucket-level guarantee. When the worst delay per decision matters, use
`uncertainty-aware` with aging.

## Benchmarks: what is and is not measured

The issue asks for solve-level benchmarks — wall clock to a target
exploitability on Hold'em heads-up, PLO4, PLO5 and a multiway Omaha solve. **They
are not produced, and producing them would be dishonest here.** The layer has no
call site in a solve (see the audit above), so a solve-level table would measure
whatever harness was invented to drive it, not the feature. The workload table
above is the honest substitute: it measures the allocation the layer makes, on a
synthetic workload whose composition is known in advance.

## The suite found a defect, and the guards bite

Every guard was broken on purpose and the failures counted. Each row is one
mutation of `work_priority.c`, rebuilt and run, then restored. The build is
proven to have taken effect — the object file is deleted before each build and
the solver library is hashed before and after — and each mutation is run three
times with the counts required to agree. Both precautions are there because an
earlier harness silently reported a *stale* binary's count for two of the rows:
CMake compares timestamps at one-second granularity, so a write followed by a
build inside the same second leaves the previous object in place.

| Mutation | Failing checks |
|---|---|
| the score ratio inverted (`/` → `*`) | 1,213 |
| the coverage tier removed | 5,024 |
| the spread no longer combined in quadrature | 257 |
| an unresolved ordering no longer saturates | 1,723 |
| aging neutralised | 387 |
| the bucket boundary made exclusive (`>=` → `>`) | 22 |
| the bridge accepts a single observation | 3 |
| the BALANCED rotation removed | 385 |
| the score histogram taken from the effective bucket | 4 |
| the percentile rank made exclusive (`>=` → `>`) | 7 |
| the coverage promotion removed | 3 |

The last row is `pe_work_priority_bucket()` no longer returning the top bucket
for an item below the floor. It needs its own fixture because aging reaches the
top bucket by a second path: with aging on, the promotion can disappear and the
same bucket still comes out, so a workload with aging on does not notice.
`test_coverage_promotion()` runs it with aging off, which is the 3.

Five rows are small on purpose, and small is not the same as weak. A score lands
on a bucket boundary only when it is exactly `ratio^k`, which the randomised
batches almost never produce, so `test_bucket_boundaries()` exists precisely to
cover it — that is the 22. The bridge's single-observation guard is covered by
one fixture, and the floor's bucket promotion by another, which is the two 3s;
the score histogram's independence from the policy by one fixture, which is the
4; and the percentile's rank convention by `test_percentile()`'s exact
distribution, which is the 7. Every one is non-zero, and each is caught by the
test written for it rather than by luck.

The source was restored byte-for-byte after each run. Beyond those mutations,
the suite caught a real design defect during development — the coverage floor
being preempted by aging, described above — which is the strongest evidence that
the tests are not tautological.

## The integration point

The order is meant to permute a `pe_work_unit_t` array before
`pe_work_coordinator_dispatch()` sends contiguous ranges. `test_unit_permutation()`
proves the composition: it builds four units, orders the metadata, applies the
permutation to pointers, and checks every unit still validates and still carries
its own state.

The module deliberately does not depend on `pe_work_unit.h`. It reads metadata
and returns a permutation, so it works whether or not the distributed stack is
in use, and it introduces no game-specific or backend-specific code — it never
mentions a backend at all.

## Scope

- **No production call site yet.** The layer is complete, tested and documented,
  but nothing in a solve calls it. Wiring it in is a separate change, and the
  audit above is the reason it is separate.
- **CFR training is untouched.** This module only orders work; it computes no
  regrets and averages no strategies.
- **`pe_work_schedule()` and `pe_work_coordinator_schedule()` are unchanged.**
  They arbitrate across backends and workers by measured rate, and still do.
