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

`pe_work_priority_parse_option()` accepts the same fields as
`policy`, `min-visits`, `epsilon`, `buckets`, `bucket-ratio` and
`aging-interval`, for a command line or a config file. The issue's proposed
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
*difference* of two estimates. A caller with no uncertainty information passes a
zero stderr, and the decision is scored on its gap alone: a wide gap with no
spread recorded is the lowest priority, which is the honest reading, since
nothing observed suggests the ordering is fragile. That is the documented
fallback when adaptive sampling (#256) is off.

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

  The round starts at the top bucket on epoch 0 and one bucket lower on each
  later epoch, wrapping at the bottom. That rotation is what makes the bound
  hold for the caller the policy exists for — one that services a prefix of the
  order and recomputes. Without it every call would put the same top bucket at
  position zero and the policy would do nothing the strict one does not. A
  caller that consumes the whole permutation sees the rotation only as a change
  of order within a round, so epoch 0 is the plain highest-first order.

The coverage tier comes first under all three non-FIFO policies.

## Telemetry

`pe_work_priority_stats_t` carries the queue depth per bucket, the floor and
aging activations, the unresolved count, the mean score and the mean delay.
`pe_work_priority_format_stats()` renders one line, in the style of the solver's
other counters — this is `test_stats()`'s printed output:

```text
work_priority items=4 buckets=8 coverage_promotions=1 aging_promotions=1 unresolved=1 mean_score=16.6667 mean_delay=33.750 depth=0,0,1,0,0,0,1,2
```

`depth` is the queue depth per bucket, lowest bucket first, so a baseline run
and an uncertainty-aware run can be compared field by field. The counters are
counted under FIFO too — the buckets describe the workload even when nothing is
reordered — which is what makes the baseline comparable.

## Measured: does the priority do what it claims?

`test_synthetic_workload()` builds 40 decisions, 10 of them near-indifferent
(`gap` 0.001 against a spread of 0.05, so a score near 50) and 30 settled (`gap`
of 9 or more against a spread of 0.01, so a score near 0.001), and services
2,000 of them one at a time.

| Policy | Share of work on the near-indifferent group | Worst delay between two services of one decision |
|---|---|---|
| FIFO (rotating) | 25.0% — the group is a quarter of the queue | 39 rounds |
| uncertainty-aware, aging 8 | **60.1%** | 80 rounds |
| balanced, aging 8 | — | 65 rounds |

The prioritised run spends **2.4×** the baseline share on the decisions that are
actually undecided, and every decision is still serviced: the worst delay is
80 rounds, finite and bounded, against 39 for the even baseline. That is the
trade the feature exists to make. `balanced` trades some of the concentration
for a tighter worst case — 65 rounds rather than 80 — which is what the epoch
rotation buys.

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
| the score ratio inverted (`/` → `*`) | 1,211 |
| the coverage tier removed | 5,024 |
| the spread no longer combined in quadrature | 257 |
| an unresolved ordering no longer saturates | 1,719 |
| aging neutralised | 386 |
| the bucket boundary made exclusive (`>=` → `>`) | 22 |
| the bridge accepts a single observation | 3 |
| the BALANCED rotation removed | 385 |

Two rows are small on purpose, and small is not the same as weak. A score lands
on a bucket boundary only when it is exactly `ratio^k`, which the randomised
batches almost never produce, so `test_bucket_boundaries()` exists precisely to
cover it — that is the 22. The bridge's single-observation guard is covered by
one fixture, which is the 3. Both are non-zero, and each is caught by the test
written for it rather than by luck.

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
