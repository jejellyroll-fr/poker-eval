# Uncertainty-Aware Work Prioritisation

Issue #258. API: `include/poker_eval/solver/pe_work_priority.h`. Implementation:
`src/solver/domain/work_priority.c`. Test: `tests/test_work_priority.c`, against
the brute-force oracle `tests/support/pe_work_priority_oracle.h`.

Issue #271 is the second half: the layer had no call site, and now it has one.
`src/solver/domain/external_best_response.c` consumes the *bucket* of a decision
to interpolate that decision's sample cap. Test: `tests/test_br_priority.c`. See
"The call site" and "Measured: the cap in a solve" below.

The solve-level benchmarks issue #258 asks for are in
`scripts/benchmarks/bench_work_priority.py`; their output is tabulated under
"Benchmarks: the four solve-level workloads".

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

## Benchmarks: the four solve-level workloads

Issue #258 asks for the layer to be benchmarked at solve level on Hold'em
heads-up, PLO4 heads-up, PLO5 heads-up and a multiway Omaha solve.
`scripts/benchmarks/bench_work_priority.py` is that measurement, and the tables
below are its output, run twice with byte-identical deterministic columns.

The call site only decides how many draws the *measurement* spends, so the four
spots solve the same strategy under both policies and differ only in how they
measure it. The script refuses to publish a row that violates that: the report's
fields must agree outside the measurement's own numbers and the storage its own
traversal grows, the training's visits, updates and chance draws must agree
street by street, and the trained strategy's per-decision frequencies must agree.

That last check reads the report's hand table, and deliberately not `RANGE GRID`:
the tool emits the grid only for Hold'em, so a fingerprint taken from it is empty
on all three PLO spots, and two empty fingerprints compare equal whatever was
trained — the guard would have been vacuous exactly where the workloads are
hardest. The fingerprint is the multiset of `hand`, `node`, `actor` and
`frequencies`.

The hand is what binds a frequency vector to a decision: without it the
projection is a multiset of `(node, actor, frequencies)`, and two hands at the
same node and actor that exchange their vectors leave it unchanged — a
per-decision change the guard would wave through. The hand string is a canonical
representative over the 24 loose suit permutations of hole and board
(`preflop_op_infoset_key`), so it names a class of decisions rather than one, but
it is a deterministic function of the infoset: measured stable for the same
decision across the two policies and across two BR budgets, on 24 spot/seed pairs
including `plo4-3way`, where only 1,867-1,881 of 1,997 rows are shared between
two runs that solved the same strategy.

Keeping the hand costs a second restriction, and finding it was the point of
this paragraph. The report is emitted in two sweeps — rows that carry a strategy
first, the untouched ones after — and every untouched row holds the uniform
vector regret matching starts from. Which of them fill the leftover per-node
quota depends on storage-id order, and the measurement grows the storage, so
that tail churns: 135 of 1,998 rows on PLO4 heads-up at 500 iterations, *all of
them uniform*. A `(node, actor, frequencies)` multiset collapses them and
survives; adding the hand stops collapsing them and the guard refuses a
comparison it should accept. So the fingerprint covers only the decisions that
were actually trained. That costs nothing — a decision uniform in both arms is a
decision that did not change, and one that leaves the uniform start enters the
set and is seen. With every row the projection differs between the two policies
on three of the four spots at 500 iterations; restricted to the trained rows it
is identical in every regime tried, and still separates a 500-iteration strategy
from a 5,000-iteration one on all four spots.

The EV column is the measurement's own sampled view and the board column is the
sampled deal's runout; neither belongs to the strategy. A multiset rather than a
sequence, because `--report-rows` fills per-node quotas in storage-id order. An
empty fingerprint is refused rather than accepted, so a report format that
dropped the hand table fails the guard instead of disarming it — and so does a
budget too low to have trained anything, which is a real regime: at 500
iterations `plo5-hu` and `plo4-3way` have no trained decision at all, and the
counts at 5,000 are 331, 251, 35 and 147 decisions for the four spots. The
`plo5-hu` fingerprint therefore rests on 35 decisions, which is thin, and is why
that spot's other evidence is read with the caveat below.

The frequency column is the report's own `%.1f%%`, so the fingerprint resolves
0.1 percentage points — that is the granularity of the check, and it is the
report's limit, not the guard's. A divergence is caught as soon as one decision
moves by that much: the smallest divergence measured here (500 iterations against
5,000) moves 1% of the shared decisions on the least sensitive spot and 99% on
the most sensitive, so the granularity is well below the size of a real training
divergence. What the fingerprint does *not* catch is a change smaller than half a
percentage point on every decision at once, and no run of this benchmark has
produced one.

The storage exemption is the same kind of narrow: the measurement resolves
infosets the training never reached, and `storage_v2.c` derives the byte totals
from `slot_capacity` and `meta_capacity`, both of which grow by doubling. A pair
that straddles a doubling therefore moves `storage_bytes` and its parts while
solving the identical strategy — reproduced on PLO4 heads-up, seed 1, 500
iterations, `--br-samples 9000`, with the aware arm's cap pinned to 4 against
FIFO's 64: 22,993 infosets on 65,536 hash slots against 22,936 on 32,768. Those
byte totals are exempt; `recompute_calls` and `bytes_saved_vs_full` are not,
because they answer to the memory policy rather than to the measurement.

That is also what makes the comparison paired — both arms of a spot see the same
seed, hence the same solved strategy.

`pe-preflop-solve`, external MCCFR, 5,000 iterations, 4 showdown boards, 20,000
BR trajectories per player, `--br-min-samples 4 --br-max-samples 64`, seeds
100-104:

| Spot | Policy | BR terminal evals | draws/dec | early stop | cap hits | NashConv mBB | paired vs FIFO |
|---|---|---|---|---|---|---|---|
| `holdem-hu` | FIFO | 454,750 | 14.5 | 99.6% | 118 | 12,459.8 | — |
| | uncertainty-aware | 389,290 | 12.1 | 94.9% | 1,398 | 12,367.6 | −92.2 ± 152.5 |
| `plo4-hu` | FIFO | 633,019 | 18.9 | 100.0% | 0 | 20,861.9 | — |
| | uncertainty-aware | 540,728 | 15.9 | 89.5% | 3,239 | 20,981.8 | +119.9 ± 205.5 |
| `plo5-hu` | FIFO | 641,957 | 19.7 | 100.0% | 0 | 19,667.0 | — |
| | uncertainty-aware | 624,766 | 19.1 | 98.0% | 611 | 19,666.7 | −0.3 ± 265.5 |
| `plo4-3way` | FIFO | 1,574,437 | 27.3 | 95.4% | 2,565 | 51,339.2 | — |
| | uncertainty-aware | 1,245,611 | 21.3 | 85.7% | 7,938 | 51,117.8 | −221.4 ± 487.0 |

**The measurement costs 14.4%, 14.6%, 2.7% and 20.9% fewer terminal evaluations,
and the effect on the reported number is bounded.** What the table above may be
read as is the load-bearing question, and the answer is *not* "the difference is
zero". With five seeds, failing to reject that null at the 5% level would not be
evidence of equivalence: a small sample is too weak to reject anything, and a
modestly biased estimator passes the same test. So the script prints no
significance verdict. It prints the 95% confidence interval of the paired
difference, which is what bounds the effect, alongside each arm's own run-to-run
spread across seeds:

| Spot | paired mean | 95% CI (mBB) | CI half-width | baseline sd | verdict |
|---|---|---|---|---|---|
| `holdem-hu` | −92.2 | [−281.5, +97.1] | 1.52% | 5.30% | inside |
| `plo4-hu` | +119.9 | [−135.2, +375.0] | 1.22% | 1.31% | wider |
| `plo5-hu` | −0.3 | [−329.9, +329.3] | 1.68% | 0.82% | wider |
| `plo4-3way` | −221.4 | [−826.0, +383.2] | 1.18% | 1.56% | wider |

The yardstick is the **baseline arm's own** spread across seeds — the variation a
reader already lives with when they change the seed. It is deliberately *not* the
wider of the two arms': taking the maximum would let the judged arm widen its own
bound, since a policy that increases run-to-run variance would raise the bar it
has to clear. On the published run that defect was live, the aware arm setting the
yardstick on two of the four spots (`holdem-hu` 700.8 against 660.9, `plo5-hu`
250.3 against 160.7, an inflation of 56%); no verdict flipped there, but a spot
whose effect sat between the two spreads would have. The judgement is on the
*whole* interval, `|mean| + half-width <= yardstick`, because comparing the
half-width alone passes an interval that reaches past the bound, and did:
`plo4-hu` has an upper endpoint of +375.0 against a yardstick of 273.1, and
`plo4-3way` a lower endpoint of −826.0 against 801.5.

This is a **yardstick, not a pre-specified equivalence margin**: it is estimated
from the same five seeds being judged, and its own uncertainty is not accounted
for. What "inside" supports is the weaker, honest statement that the policy's
effect, at its widest, is smaller than the variation the baseline itself shows
across seeds — a practical-significance reading, not a test. **One of the four
spots is inside.** The other three support "no effect detected at this sample
size" only, and the table says so rather than leaving it to the reader.

A wider spread would be a regression in its own right, whatever the mean did, so
the aware arm's spread is printed beside the yardstick, with the ratio. It is
mixed rather than uniformly worse: larger on `holdem-hu` (700.8 against 660.9,
ratio 1.06) and `plo5-hu` (250.3 against 160.7, ratio 1.56), smaller on `plo4-hu`
(204.3 against 273.1) and `plo4-3way` (419.6 against 801.5). Five seeds estimate a
standard deviation loosely — with four and four degrees of freedom an F-test needs
a variance ratio near 6.4 to say anything — so this is reported as "no consistent
inflation", not as a measured equality, and it does not enter the yardstick.

The interval bounds the *effect*; it does not by itself say the aware arm's
estimate is as close to the truth, which is a separate question and needs a
reference. Each arm was therefore also scored against a run at ten times the
budget — 200,000 BR trajectories per player — paired by seed. The reference has
to share the arm's seed: the seed drives the *solve*, so a reference at a
disjoint seed scores a different strategy rather than the same one measured
better. On `holdem-hu` a disjoint-seed reference reports a mean absolute error of
1308.7 where a shared-seed one reports 101.3, a factor of thirteen, all of it
strategy variation. A shared seed is not neutral either: the tool seeds its
best-response RNG once and consumes trajectories in order
(`external_best_response.c:1206-1215`), so a run at N is a *prefix* of the same
policy's run at ten times N, and FIFO's error can cancel against FIFO's own
stream while the aware arm — a different policy, hence a different stream — gets
no such cancellation. So each arm is scored against *both* policies'
higher-budget runs, and the symmetric comparison — each arm against its own — is
separated from the crossed one:

| Spot | FIFO@20k vs FIFO@200k | aware@20k vs aware@200k | closer |
|---|---|---|---|
| `holdem-hu` | 80.4 | 136.7 | FIFO |
| `plo4-hu` | 253.5 | 187.2 | aware |
| `plo5-hu` | 178.9 | 193.5 | FIFO |
| `plo4-3way` | 492.7 | 615.4 | FIFO |

| Spot | FIFO@20k vs aware@200k | aware@20k vs FIFO@200k | closer | floor |
|---|---|---|---|---|
| `holdem-hu` | 78.4 | 137.1 | FIFO | 36.4 |
| `plo4-hu` | 234.0 | 183.1 | aware | 93.6 |
| `plo5-hu` | 154.7 | 272.0 | FIFO | 90.4 |
| `plo4-3way` | 836.8 | 272.6 | aware | 613.5 |

The single table this replaces mixed the two readings: FIFO's *symmetric* error
against the aware arm's *crossed* one. That is exactly the comparison the
seed-sharing prefix makes unfair, and on `plo4-3way` it decides the answer — a
220 mBB margin for the aware arm. Compare like with like and it reverses:
symmetric, FIFO wins that spot by 123 mBB; crossed, the aware arm wins it by 564.
The symmetric reading is the one to report, and on it the aware arm is the more
accurate estimator on `plo4-hu` alone; the two readings agree on every other
spot, which is why the defect needed a spot to be measured on rather than argued
about.

`plo4-3way` is also where neither reading should be trusted. Its two
higher-budget runs disagree by 613.5 mBB on the *same* strategy — the same order
as every error in either table — so an estimate at 20,000 trajectories is no more
informative there than the disagreement between two estimates at ten times the
budget. The floor is tabulated for that reason, and the other three spots sit at
36.4, 93.6 and 90.4: small enough, against errors of 78 to 272, for the
comparison to carry. Five seeds estimate a mean absolute error loosely, so what
all of this rules out is a *uniform* accuracy loss, not a per-spot one. The two
arms also reproduce the published paired differences exactly (−92.2 ± 152.5,
+119.9 ± 205.5, −0.3 ± 265.5, −221.4 ± 487.0), which is the internal consistency
check that the reference is measuring the same thing.

Criterion (1) is therefore supported as a bound rather than as an absence of
evidence: the policy shifts the reported exploitability by at most 2.3% of the
reported value, while costing 2.7-20.9% fewer terminal evaluations against a
baseline yardstick of 0.8-5.3%. The low end is `plo5-hu`, and it belongs in the
range: the four workloads save 14.4%, 14.6%, 2.7% and 20.9%, and quoting only
the three large ones would overstate the cheapest case by a factor of five. The
bound is the **farthest interval endpoint**, not the
half-width, and that is the same distinction the verdict above turns on: the
half-widths are 1.2-1.7%, but the intervals are not centred on zero, so the
largest shift the data admits is 2.26% on `holdem-hu` (its interval reaching
−281.5 against a reference of 12,459.8), 1.80% on `plo4-hu`, 1.68% on `plo5-hu`
and 1.61% on `plo4-3way`. Two readings have to be kept apart, and the table gives
both. No spot detects an effect: all four intervals contain zero, so none of
them licenses "the policy moved the answer". Only `holdem-hu` has its whole
interval inside the baseline's own spread. On the other three the
interval reaches past it and the data supports only "no effect detected
at this sample size" — for `plo5-hu` that is the whole story (smallest saving,
2.7%, and both its accuracy and its spread are the worse for the aware policy),
while `plo4-hu` and `plo4-3way` combine the largest savings with too few seeds
to narrow the interval further. `plo4-hu` is also the one spot where the aware
arm is the more accurate estimator; on `plo4-3way` that comparison is
unresolvable, so the interval is all there is to read.

Criterion (2) — better quality for the same compute budget — is **not** delivered
on the three heads-up spots, and the cap curve is the evidence:

```text
holdem-hu   cap curve  1x 383,006  2x 385,646  4x 386,910  8x 389,690  16x 392,856  (fifo at 1x: 452,678)
plo4-hu     cap curve  1x 541,937  2x 560,369  4x 584,595  8x 599,083  16x 619,203  (fifo at 1x: 633,843)
plo5-hu     cap curve  1x 626,925  2x 655,613  4x 698,269  8x 727,981  16x 754,677  (fifo at 1x: 643,357)
plo4-3way   cap curve  1x 1,242,466  2x 1,466,196  4x 1,774,180  8x 2,197,318  16x 2,647,162  (fifo at 1x: 1,573,060)
```

Raising the cap sixteen-fold buys 2.6% more work on `holdem-hu`: the confidence
rule, not the cap, is what stops most decisions, so the saving has nowhere to go
back to. On `plo4-3way` it does — the curve crosses the baseline at 4x, so there
the saving *is* reinvestable. The three-way spot is also the one where the
priority telemetry fills the ladder instead of the bottom two buckets
(`35008,2684,468,78,17,44,29,25` against `holdem-hu`'s `272,52,13,0,0,0,0,0`),
which is the same fact read from the other side.

The size of the saving is not a constant, and the reason is structural. Only a
*repeat* visit to a decision is capped: a decision below the coverage floor ranks
in the top bucket, so its first visit keeps the full cap. A budget that visits
most decisions once therefore has almost nothing to reallocate, and the revisit
curve says so:

```text
holdem-hu   revisit curve  2,000 13.4%  5,000 14.8%  20,000 15.4%
plo4-hu     revisit curve  2,000 1.9%   5,000 4.2%   20,000 14.5%
plo5-hu     revisit curve  2,000 0.1%   5,000 0.3%   20,000 2.6%
plo4-3way   revisit curve  2,000 1.8%   5,000 6.9%   20,000 21.0%
```

At the CLI's own default of 2,000 trajectories the saving is 0.1% on `plo5-hu`
and 1.8% on `plo4-3way`; at 20,000 it is 2.6% and 21.0%. The script therefore
defaults to 20,000 and prints the curve rather than quoting a single budget.
`plo5-hu` — 28,148 decisions against `holdem-hu`'s 337 — is the spot where a
given budget buys the fewest revisits, which is why it is also the spot where
the priority has the least to do.

What #271 measures as a demonstration, on one spot and 20 iterations, is below
under "Measured: the cap in a solve"; the workload table under "Measured: does
the priority do what it claims?" remains the measurement of the *allocation* the
layer makes, on a synthetic workload whose composition is known in advance.

## The suite found a defect, and the guards bite

Every guard was broken on purpose and the failures counted. Each row is one
mutation of `work_priority.c`, named by the code change it makes so that it can
be reproduced, rebuilt and run, then restored. The build is proven to have taken
effect — the object file is deleted before each build and the solver library is
hashed before and after — and each mutation is run three times with the counts
required to agree. Both precautions are there because an earlier harness
silently reported a *stale* binary's count for two of the rows: CMake compares
timestamps at one-second granularity, so a write followed by a build inside the
same second leaves the previous object in place.

| Mutation | Failing checks |
|---|---|
| `return uncertainty / gap` → `* gap` | 1,164 |
| the coverage tier dropped: below-floor items ranked by bucket like the rest | 4,172 |
| `hypot(best_stderr, second_stderr)` → a plain sum | 192 |
| the `+infinity` of an unresolved ordering → `0` | 1,930 |
| `promotion = age / aging_interval` → `promotion = 0` | 311 |
| `score >= threshold` → `score > threshold` in `score_bucket()` | 25 |
| `stats->n < 2` → `stats->n < 1` in `spread_of()` | 2 |
| the epoch's start bucket fixed at the top occupied one | 449 |
| `score_depth[score_bucket(score)]` → `score_depth[bucket]` | 5 |
| `running >= target` → `running > target` in the percentile | 7 |
| the `return top` for a below-floor item dropped | 3 |

Five rows are small on purpose, and small is not the same as weak. A score lands
on a bucket boundary only when it is exactly `ratio^k`, which the randomised
batches almost never produce, so `test_bucket_boundaries()` exists precisely to
cover it — that is the 25. The bridge's single-observation guard is covered by
one fixture, which is the 2; the floor's bucket promotion by another, which is
the 3; the score histogram's independence from the policy by one fixture, which
is the 5; and the percentile's rank convention by `test_percentile()`'s exact
distribution, which is the 7. Every one is non-zero, and each is caught by the
test written for it rather than by luck.

The floor's promotion needs a fixture of its own because aging reaches the top
bucket by a second path: with aging on, the promotion can disappear and the same
bucket still comes out, so a workload with aging on does not notice.
`test_coverage_promotion()` runs it with aging off, which is the 3.

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

## The call site

The audit above is about why the layer could not go *into* the distributed
scheduler. Issue #271 is about where it *can* go, and the answer came from the
same reading of the architecture: **the bucket, not the permutation.**

`pe_work_priority_order()` returns a permutation of a batch, and a permutation
changes the *quantity* of work served only when the caller consumes a prefix of
it or stops at a finite cap. Neither holds here:

| Consumer | What it does with a batch | Would a permutation change anything? |
|---|---|---|
| the sampled solve loop (`solver.c`) | runs its whole batch, every iteration | no — every item is served anyway |
| `pe_work_coordinator_dispatch()` | sends contiguous index ranges covering everything | no |

So `order()` has no consumer, and giving it one would have meant inventing the
prefix semantics first. `pe_work_priority_bucket()` is the primitive that does
have one: it is a pure function of a *single* decision's metadata, needs no
batch, and answers "how much does this one deserve".

The benchmarks confirm that reading from the outside. `PE_WORK_SCHED_BALANCED`
and `PE_WORK_SCHED_UNCERTAINTY_AWARE` differ only in the *permutation* they
return — `pe_work_priority_bucket()` ranks the two alike, because only FIFO is
special-cased in it — so a solve run under either measures byte-identical
terminal evaluations and NashConv. The two policies are indistinguishable at
this call site, which is exactly what "the bucket, not the permutation" predicts.

The site is `src/solver/domain/external_best_response.c`. In `br_rollout()`,
`pe_br_resolve_decision()` already receives its `pe_br_sampling_config_t` **by
call** and already spends a variable number of draws per decision — anywhere
between `min_samples` and `max_samples`, chosen by the confidence rule. Passing
a per-decision config is therefore a change to a number the function already
treats as variable, not a new concept. Deriving that number from the decision's
bucket turns the layer into an allocation:

```
cap = min_samples + (max_samples - min_samples) * bucket / (buckets - 1)
```

Bucket 0 — a settled decision — gets `min_samples`; the top bucket gets
`max_samples`. The historical behaviour is `max_samples` everywhere, so the
policy only ever spends *less* on a decision, never more. It is a cap and not a
budget, which is why a decision that resolves at its first look is untouched
whatever its bucket: there is nothing left to cap.

The first draft of the issue proposed the vector best response
(`best_response_ii.c`) instead. That was wrong, and the issue body records why.
`br_collect()` (line 675, called at line 838) is a recursive traversal of the
whole tree that serves **every** infoset in one pass, and the loop over
`table_capacity` (line 845) is an order-independent selection pass over the
results. The table is an accumulator, not a work queue: there is no batch to
order and no per-item work to allocate.

### Where the numbers come from

A decision's own *previous* measurement ranks it. `br_rollout()` keeps one
record per infoset — the game's infoset key, the last measured `gap` and the two
standard errors, the action count, the visit count and the epoch it was last
served — in an open-addressed table (`pe_rng_mix(key)`, power-of-two capacity,
256 slots to start, doubling). The record is read before the decision is
resolved and written after, so the second visit to a decision is allocated by
the first.

A game without an `infoset_key` callback has no record to allocate by, so with
the policy in effect `pe_external_best_response_sampled()` refuses it (`-1`)
rather than keying every decision to 0, which would allocate each one by
whichever decision ran last. Under FIFO, or with sampling off, such a game is
measured exactly as before.

Through the solver the refusal comes earlier. Lane B wraps the game before
measuring it, and the wrapper used to install a key callback that returned 0
when the base game had none, so the guard above never saw a missing key. The
wrapper is now installed only over a game that has the callback — the
traversals fall back to key 0 on their own, as the vector path's wrapper already
assumed — and `pe_solver_run_sampled()` returns `PE_SOLVER_ERR_INVALID_CONFIG`
**before the first iteration** when the sampled BR and a non-FIFO policy are
both on, the BR mode can reach the sampled evaluator, and the game has no key.
`PE_BR_EXACT` never runs it — the exact evaluator treats a keyless game's nodes
separately — so it is not refused there; `PE_BR_AUTO` may fall back to sampling,
so it is. `test_pe_solver_sampled.c` asserts the refusal at iteration 0, that
the same keyless game still trains and measures under FIFO, and that it runs
under exact BR with the policy configured.

The checkpoint adapter hashes the policy only where it is in effect — a non-FIFO
policy over a sampled evaluation that is on, reachable by the traversal, **and**
under a BR mode other than `PE_BR_EXACT` — so a checkpoint made under the
default, or under exact BR, still resumes after an inert `--br-priority-policy`
is added, while one made under an active policy refuses a resume under another.
The allocation lives inside the sampled best response, so the traversal clause
is the same one the sampling settings use: a full-tree traversal measures with
the vector best response and never reaches this layer.

The spread is fed as the layer's contract asks: the leader's and the
runner-up's **standard errors**, which `pe_br_resolve_decision()` reports in
`pe_br_decision_t.best_stderr` and `.runner_stderr`, and which the layer
combines in quadrature. The item is built as `best = gap`, `second_best = 0`,
`best_stderr = best_stderr`, `second_stderr = runner_stderr`.

`gap_half_width` is **not** a substitute, and an earlier draft of this call site
that fed it was wrong in two ways. It is `z * (SE_best + SE_runner)` — an L1
sum, not the quadrature — and its two terms are not equal in general: the
actions have their own variances and, after elimination, their own draw counts
(`test_br_sampling.c` pins a decision whose runner-up has no spread at all).
Worse, `z` is not a fixed 1.96: it is the sequential union bound over the
decision's looks and actions (`pe_br_sequential_z()`), so it moves with
`max_samples`, `check_interval`, `confidence` and the action count. The bucket
boundaries `ratio^0, ratio^1, ...` are absolute, so a factor of `z` (about 3 on
a three-action decision at min 4 / max 64) moves decisions across them — and
raising `max_samples` would have made every decision look less settled. The
monotone-rescaling argument only preserves the *ranking*, and the allocation
reads the *bucket*.

The consequence is a different threshold for "settled", and it is the layer's,
not the resolver's: bucket 0 is a gap wider than one standard error of the
difference, where the resolver separates two actions only once their `z`-wide
intervals clear. A decision can therefore be capped at `min_samples` before the
resolver would call it separated — the second fixture below is one.

A record that does not exist yet reads as "not tracked": the decision is scored
on the zeroed metadata it is handed, which is the coverage-floor path.

### Epochs and aging

One best-response trajectory is one epoch: `last_served = epoch`, and the age of
a decision is `epoch - last_served`. A decision infoset is served at most once
per trajectory, so the age is 1 and `aging_interval = 1` promotes it by exactly
one bucket — one bucket per trajectory waited, which is the tightest the rule
can be.

The promotions happen *during* the traversal, so the snapshot taken after the
last trajectory reports `aging_promotions = 0` even with aging on: at that
moment every record has just been served. `test_br_priority.c` therefore asserts
the promotion where it is observable — the cap it buys — and pins the zero with
a comment, so a reader cannot mistake the snapshot for a record of the
promotions.

### Selecting it

```c
config.br_sampling.max_samples = 64;      /* issue #257; already the default (issue #274) */
config.br_sampling.min_samples = 4;
config.br_priority.policy = PE_WORK_SCHED_UNCERTAINTY_AWARE;
config.br_priority.buckets = 8;           /* the default */
```

Both CLI drivers expose it, as `--br-priority-KEY VALUE` on `pe-preflop-solve`
and `--br-priority-<key> <value>` on `mpf_run_with_metrics`. The option is
parsed *before* the `--br-` branch, which would otherwise claim the prefix. It
needs the sampled best response on, which is the default since issue #274; the
policy is inert under `--br-max-samples 0`, which selects the historical
one-rollout estimator, and both help texts say so.

An all-zero config is FIFO, and FIFO leaves every decision at `max_samples`.
Measured on the shipped CLI (`pe-preflop-solve --iterations 20
--br-min-samples 4 --br-max-samples 64`), three runs byte-identical to each
other:

| Run | Output SHA-256 (first 16 hex digits) |
|---|---|
| no `--br-priority-*` option | `430e300a9a5b2bb7` |
| `--br-priority-policy fifo` | `430e300a9a5b2bb7` |
| every `--br-priority-*` key spelled out at its documented default | `430e300a9a5b2bb7` |

### The ABI

The three public structs the new settings and totals live in all grew:

| Struct | Before | After |
|---|---|---|
| `pe_solver_config_t` | 320 bytes | 368 |
| `pe_external_br_config_t` | 80 bytes | 128 |
| `pe_external_br_result_t` | 216 bytes | 784 |
| `pe_br_decision_t` | 56 bytes | 72 |

Most of the third one is the two 32-entry histograms inside
`pe_work_priority_stats_t`. The last one gained the leader's and the
runner-up's standard errors, which the call site feeds the layer (see above);
it is written by `pe_br_resolve_decision()` into the caller's struct, so a
caller compiled against the smaller layout would be overrun too. `pe_solver_config_default()` writes the whole
struct, so a caller compiled against the smaller layout would have its buffer
overrun — the same reasoning as issue #257, which grew the same three structs.
The solver ABI therefore bumps to **SOVERSION 8**.

## Measured: the cap in a solve

End to end on the shipped CLI — Hold'em heads-up, 20 iterations, seed fixed,
`--br-min-samples 4 --br-max-samples 64`:

| | terminal evaluations |
|---|---|
| FIFO (the default) | 9,491 |
| uncertainty-aware | 7,475 |

That is **21.2% fewer** terminal evaluations for the same measurement, and the
priority telemetry reports what it was spent on:

```text
work_priority items=205 buckets=8 coverage_promotions=0 aging_promotions=0 unresolved=0 mean_score=0.307877 mean_delay=94.429 p50_bucket=0 p90_bucket=0 depth=194,10,1,0,0,0,0,0
```

205 decision infosets, 194 of them in bucket 0 (settled, so capped at
`min_samples`), 10 in bucket 1 and 1 in bucket 2 — nothing above. On this spot
the decisions separate cleanly, which is why the saving is a reallocation rather
than a reshuffle: the work taken from the settled decisions is not handed to
anyone else, it is simply not spent. The resolver's own telemetry shows the
other side of the threshold difference above: `max_budget_hits` rises from 3 to
87 — decisions stopped by their (lowered) cap before their `z`-wide intervals
cleared.

This is a demonstration and not the four solve-level workloads, which are
measured above: one spot, one game, 20 iterations. The numbers are
machine-independent because they are sample *counts* rather than wall clock, but
they are not a convergence study.

### The unit test's numbers

`tests/test_br_priority.c` uses a one-decision toy game — three actions, one
infoset, 400 trajectories, seed 9, min 4 / max 64 — and asserts against the
counts it prints (Debug, this machine):

| Fixture (leader / runner-up / noise) | FIFO draws | prioritised draws | bucket |
|---|---|---|---|
| 10.0 / 0.0 / 0.1 — resolves at the first look | 6,400 | 6,400 | 0 |
| 3.0 / 0.0 / 2.25 — does not resolve at the first look | 15,332 | 6,540 | 0 |
| 1.0 / 0.999 / 1.0 — near tie | — | — | 0 to 7, mean 1.3 over 16 seeds |
| 1.0 / 1.0 / 1.0 — exact tie | — | — | 0 to 2, mean 0.7 over 16 seeds |

Four readings:

- The cap is inert where a decision resolves at its first look. The first row's
  two totals are equal, and they are equal because the confidence rule already
  stops at 6,400 draws, not because the policy is off: the test asserts that the
  infoset was tracked (`items == 1`) and that it landed in bucket 0.
- Where the decision does not resolve, the cap bites: 15,332 → 6,540 draws, a
  57.3% reduction, while the estimate stays on the exact value (asserted to
  within 0.1 of 3.0) because a cap limits the *budget*, it does not stop the
  fresh-draw re-estimate. The layer puts it in bucket 0 — its gap is several
  standard errors wide — although the resolver's `z`-wide intervals do not clear
  at the first look: the threshold difference described above.
- The classification is the layer's, not the test's, and it is asserted on
  average. One end-of-run snapshot of a tie is a single draw of a noisy
  statistic — a tie's measured gap exceeds one standard error of the difference
  about a third of the time, which is bucket 0 — so the test runs each fixture
  over 16 seeds and asserts that both ties' summed buckets exceed the settled
  decision's, which is 0 on every seed.
- Aging is observable only through the cap. On the second row, aging off draws
  6,540 and `aging_interval = 1` draws 11,828 — a larger cap, as designed, and
  still below FIFO's 15,332, so the promotion cannot undo the cap.

## The guards bite at the call site

Same protocol as the #258 table above, applied to
`src/solver/domain/external_best_response.c`: each row is one mutation, named by
the code change it makes, rebuilt and run three times with the counts required
to agree.

| Mutation | Failing checks |
|---|---|
| `br_priority_cap` returns the configured maximum (the policy is inert) | 2 |
| the measured gap is not recorded | 24 |
| the leader's standard error is not recorded | 1 |
| the runner-up's standard error is not recorded | 1 |
| the visit count is not advanced (the coverage floor never lifts) | 24 |
| the record is never looked up (`create = 0`) | 41 |
| the gate is inverted (the policy is never on) | 47 |
| the telemetry snapshot is not taken | 39 |
| the last-served epoch is not recorded (aging sees no wait) | 1 |
| the action count is not recorded (every decision looks single-action) | 2 |
| the epoch never advances | 1 |
| a keyless game is not refused (the NULL key is called) | crash (SIGSEGV) |
| the table is never freed (a leak, not a wrong answer) | 0 |

The counts are larger than in the first version of this table because the
classification check now runs each fixture over 16 seeds and asserts per seed.
The two standard-error rows were **0** each before `test_both_spreads` existed:
with the same noise on every action, dropping one of two equal spreads only
scales the score by `sqrt(2)`, which the fixtures could not see. The fixture
that sees it puts noise on a single action and sets `assumed_stderr` absurdly
wide, so a decision that lost its only spread lands in the top bucket.

The checkpoint hash is guarded in `test_pe_checkpoint_v2.c`: hashing the policy
whenever it is not FIFO, whether or not the sampled evaluation is on, fails the
"inert policy resumes" check; hashing it on a full-tree traversal — the library
default — fails the same check through the traversal clause, and reading the
configured traversal instead of the preset-expanded one fails the check that a
preset overriding it still hashes a live policy.

Two rows need a word.

The **0** is the correct measurement, and it is not a weak guard: the test
suite cannot see a leak, because a leaked table produces the same numbers as a
freed one. What catches it is CI's `asan-ubsan` job, which runs the full suite
on `ubuntu-latest` with `ASAN_OPTIONS=detect_leaks=1` and does not skip this
test (its labels are `solver;sampling;best-response;priority`, not
`sanitizer-skip`). Locally the check is not available — ASan's leak detection
reports `detect_leaks is not supported on this platform` on this macOS, and the
instrumented binary is killed before it prints anything.

The **1** on "the epoch never advances" is the same 1 the "last-served epoch is
not recorded" row scores, and that is not a coincidence: both mutations make the
age zero, and the one assertion that sees an age is the aging row's
`r_on.samples > r_off.samples`. A guard that only one assertion can see is worth
naming, which is why the mutation is listed rather than folded away.

The **plan validation** is guarded by a CTest rather than by the unit test, so
it is measured separately. `plan.c` refuses a configuration the layer would
reject, and the refusal has to arrive *before* the solve starts. Mutating the
block away moves the failure from `preflop solve failed: status=5` with no
`progress` line at all, to `status=7` **after** `progress iteration=1` — so the
negative CTest's `status=5` plus `FAIL_REGULAR_EXPRESSION "br_decisions"` is
exactly what distinguishes the two, and the guard is live rather than dead code
the measurement path would have covered anyway.

### The harness had a bug of its own, and it was the instructive kind

Two rows first reported `LIBRARY UNCHANGED` — a claim that the build had not
taken effect — and the row it happened to was different on each run. The
protocol above guards against a *stale compile* by deleting the object file
before each build. That is not enough: deleting the object forces the compile,
but **not the link**. `make` decides the shared library is up to date when the
fresh object carries the same mtime *second* as the existing library, and an
iteration of the harness fits inside a second. The build output says the object
was rebuilt, the library still holds the previous mutation's code, and the row
reports the previous row's count — or, when the previous count was zero,
`LIBRARY UNCHANGED`. Deleting the library files as well as the object file fixed
it, and the table became reproducible: three consecutive runs produced
byte-identical tables.

The lesson generalises: "the build took effect" has two steps, compile and link,
and the one-second granularity bites both.

## Scope

- **A production call site exists** (issue #271). The sampled best response
  allocates its per-decision sample cap from the layer's bucket. The layer is
  still a read-only module: it is given metadata and returns a bucket, and it
  knows nothing about best responses.
- **`pe_work_priority_order()` still has no consumer.** Nothing in a solve
  consumes a permutation, for the reason in the audit: the solve loop drains its
  batch. The distributed scheduler is still not wired. The solve-level
  benchmarks above measure the *bucket*, which is the half that has a consumer;
  what would justify wiring the ordering half — a caller that services a prefix
  of a batch under a finite budget — does not exist yet, and inventing it is not
  part of #258.
- **CFR training is untouched.** The layer only orders work, and the call site
  only changes how many draws a *measurement* spends. It computes no regrets and
  averages no strategies.
- **`pe_work_schedule()` and `pe_work_coordinator_schedule()` are unchanged.**
  They arbitrate across backends and workers by measured rate, and still do.
