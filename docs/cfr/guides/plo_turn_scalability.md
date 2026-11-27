# Turn Terminal Scalability: PLO4, PLO5, PLO6

Issue #285, phase B. Terminal: `include/poker_eval/solver/pe_omaha_turn.h`.
Benchmark: `tools/pe_omaha_turn_bench.c` (target `pe-omaha-turn-bench`).
Correctness background: [plo4_turn_chance.md](plo4_turn_chance.md).

This report answers one question with measurements: how much time and memory
does an exact turn-to-river terminal cost for 4-, 5- and 6-card private hands,
and how large may the ranges be before the answer is "not feasible"? Nothing
here is a solver run, a sample or an estimate of strategy quality; every figure
is one terminal call at a turn node, and every configuration is exact.

Every figure below was measured again after the design A share-evaluation
hoist of issue #332 landed in `src/solver/domain/omaha_river.c` (the design is
specified in [plo_turn_pair_walk.md](plo_turn_pair_walk.md), and
`test_omaha_river_hoisted` is the oracle that pins it): the river terminal now
evaluates each combo once per river and the deal walk reuses that table, so a
visit costs tens of nanoseconds instead of a full showdown evaluation. The
PLO4 rows did not move in kind: heads-up PLO4 with 32 combos or more takes the
blocker-aware vector sweep, which design A did not touch.

## Method

- Board `As Kd 7c 2h`, no dead cards, so the terminal prices 48 rivers.
- Ranges are drawn deterministically (LCG) from the deck the board leaves:
  distinct combos, every eleventh one with weight 0 and the rest spread over
  `[0.25, 1.0]`. Both ranges have the same size; heads-up unless stated.
- One configuration per process, so the reported peak RSS belongs to that
  configuration and not to a larger one that ran before it.
- The reported seconds are the median of 3 repetitions (1 for the largest
  configurations, where a single call already runs for hundreds of
  milliseconds).
- A single-call timing is not repeatable on a busy machine: the same
  configuration measured twice before design A differed by up to 2× (PLO5,
  814 combos: 8.67 s at load 3 and 17.8 s at load 21). Every sample below was
  taken at load 3.5–4.5, and the frontier check reports each one with its load.
  The terminal is now cheap enough that this noise is a larger share of a
  figure than it was: read the milliseconds, not the fourth digit.
- The deal count is exact, not estimated: the terminal visits every legal
  tuple and every river such a tuple leaves in the deck, so
  `deals = tuples × (48 − holes × players)`. The benchmark reads the tuple
  count from `pe_omaha_deals_measure()` and reports the product.
- Machine: Apple M5 (10 cores), macOS 27.0.1, Apple clang 17, `-O3 -DNDEBUG`,
  single-threaded except in the lane section. Figures are machine-specific and
  are not thresholds any test asserts.
- `--threads N` fans the candidate rivers over N lanes and reports that time
  next to the serial call; the two are compared bit for bit, not approximately.

## Why the shapes differ

A turn prices one river terminal per candidate river, so its cost is
`48 × river_cost`. The river terminal's own shape is what changes:

| Private hands | River path | Cost per river |
|---|---|---|
| PLO4, heads-up, ≥ 32 combos | blocker-aware sweep (`pe_showdown_vector_mass`) | `O(n log n + 15n)` |
| PLO4, heads-up, < 32 combos | deal walk | `O(n) + O(density × n²)` |
| PLO4 multiway, all PLO5/PLO6 | deal walk | `O(n) + O(density × n²)` |

The sweep is gated on `hole_cards == 4` in `omaha_river.c`, so **PLO5 and PLO6
always take the deal walk**: `density × n²` legal tuples, and after design A
each of them is a table lookup rather than an evaluation, on top of the `n`
evaluations a river now performs once for its whole range. That is why the
walk is quadratic in visits but only linear in evaluations — and why the
per-deal cost below falls with the range size before it flattens.

Measured tuple density (legal tuples / n², i.e. the chance that two private
hands do not share a card): **0.465** for PLO5, **0.353** for PLO6. It is a
property of the hand size, not of the range size or the board.

The measured `turn / river` ratio is 44–53 for every configuration of a tenth
of a second or more, i.e. one turn is about 48 single-river terminals, as the
design says it must be. Below that the fixed per-call cost shows up and the
ratio drops (23.1 at PLO4 1,000 combos, 11.5 at PLO5 50).

## Measured time

### PLO4: linear, 194,580 combos is the full turn

| Combos per range | Turn (s) | µs per combo | Peak RSS |
|---|---|---|---|
| 1,000 | 0.054 | 27.2 | 8.1 MiB |
| 10,000 | 0.533 | 26.6 | 17.9 MiB |
| 50,000 | 2.559 | 25.6 | 35.4 MiB |
| 194,580 (full) | 10.102 | 26.0 | 74.9 MiB |

µs per combo is `turn / (combos × players)` and stays flat, which is the
linear behaviour the sweep predicts: `T(n) ≈ 5.2e-5 · n` seconds, about 19,300
combos of range size per second. These rows are unchanged by design A, which
is the point of measuring them again: they differ from the pre-design-A table
by −13 %, from 11.572 s to 10.102 s, which is the machine (load 4.2–5.9 then,
3.5–3.8 now) and not the terminal.

### PLO5: quadratic in visits, linear in evaluations

| Combos per range | Legal tuples | Deals | Turn (s) | ns per deal | Peak RSS |
|---|---|---|---|---|---|
| 50 | 1,159 | 44,042 | 0.00266 | 60.3 | 6.5 MiB |
| 200 | 18,490 | 702,620 | 0.02001 | 28.5 | 6.6 MiB |
| 800 | 297,090 | 11,289,420 | 0.23826 | 21.1 | 6.6 MiB |
| 1,600 | 1,190,001 | 45,220,038 | 0.89083 | 19.7 | 6.7 MiB |

### PLO6: the same, on 50 % more hand evaluations per combo

| Combos per range | Legal tuples | Deals | Turn (s) | ns per deal | Peak RSS |
|---|---|---|---|---|---|
| 25 | 206 | 7,416 | 0.00136 | 184.0 | 6.5 MiB |
| 100 | 3,471 | 124,956 | 0.00723 | 57.9 | 6.5 MiB |
| 400 | 56,513 | 2,034,468 | 0.06101 | 30.0 | 6.6 MiB |
| 800 | 225,689 | 8,124,804 | 0.20801 | 25.6 | 6.6 MiB |

**The per-deal cost is no longer flat in the range size, and that is the
signature of design A.** Before the hoist a deal paid one showdown evaluation
(750–1,514 ns) and the figure moved only with load. Now a deal pays a table
lookup plus, amortized over the `density · n²` deals of a river, the `n`
evaluations that river precomputed: `t(n) ≈ c + k / n`. The measured series
are PLO5 60.3, 28.5, 21.1, 20.9, 19.7 and 22.1 ns at 50, 200, 800, 814, 1,600
and 1,994 combos, and PLO6 184, 57.9, 30.0, 25.6, 24.6, 23.4, 23.2 and 21.7 ns
at 25, 100, 400, 800, 854, 1,200, 1,600 and 2,091. So PLO5 is flat by
800–1,600 (19.7–21.1 ns) and its 22.1 ns at the 1,994 frontier is the sample
that came in 5 % over its fit, not an edge of a plateau; PLO6 is still drifting
down at the largest size, from 30.0 ns at 400 to 21.7 at 2,091 with no plateau
visible yet — its band over 400–2,091 is 22–30 ns, and only the last three
points (23.4, 23.2, 21.7) look settled. The two terms are equal at about 96
combos for PLO5 and 182 for PLO6, so below that the precompute dominates and
above it the walk does.

The consequence for the ratio between variants changed too: at these sizes
PLO6 costs 3.0× PLO5 per deal at 25 versus 50 combos, not the 1.3× of the
pre-design-A table, because what is being compared at that size is the number
of evaluations per combo (150 against 100) and not the walk. At 800 combos the
ratio is 1.21, which is the walk.

## Filter or terminal: where a call goes

`--profile` splits the serial call instead of timing it whole. The turn has two
phases per candidate river: a filter that copies the combos not holding the
river card into a compact range, and one river terminal call on the five-card
board that filter makes. Both are timed with a monotonic clock inside the same
call, and the counters are exact, so the shares below are read against that
call's own wall clock:

| Variant | Combos | Call | Filter | Terminal | Unaccounted | Filter visit | Deal |
|---|---|---|---|---|---|---|---|
| PLO4 | 194,580 | 10.790 s | 17.8 ms | 10.772 s | 0.8 ms | 0.95 ns | — |
| PLO5 | 1,994 | 1.3454 s | 0.205 ms | 1.3452 s | 0.012 ms | 1.07 ns | 19.2 ns |
| PLO6 | 854 | 0.2297 s | 0.099 ms | 0.2296 s | 0.019 ms | 1.21 ns | 24.7 ns |
| PLO6 | 1,600 | 0.7402 s | 0.193 ms | 0.7400 s | 0.016 ms | 1.26 ns | 22.7 ns |
| PLO6 | 2,091 | 1.2445 s | 0.257 ms | 1.2443 s | 0.014 ms | 1.28 ns | 22.4 ns |
| PLO5, 3 players | 150 | 0.3154 s | 0.039 ms | 0.3154 s | 0.006 ms | 1.81 ns | 23.2 ns |
| PLO6, 3 players | 150 | 0.1436 s | 0.037 ms | 0.1435 s | 0.014 ms | 1.71 ns | 28.7 ns |

The counters check the shape of each phase rather than assuming it: the filter
looked at `48 × players × combos` combos in every row (18,679,680 at PLO4
194,580, 200,736 at PLO6 2,091) and kept the ones that do not hold the river
card, so the filter is linear in the range. The terminal then walks the legal
tuples, which is why its cost is the quadratic one measured above.

**The filter is still not a target.** It is 0.015–0.026 % of a PLO5 or PLO6
call and 0.16 % of a PLO4 one, against a terminal that is the rest. Design A
made the terminal 40× cheaper and the filter's share grew by exactly that
much, from thousandths of a percent to hundredths: at 0.95–1.81 ns per combo
visit and 19.2–28.7 ns per deal, the terminal overtakes the filter after
**0.30 combos at PLO5 1,994 and 0.43 at PLO6 2,091** — the reported
`crossover_combos`, up from 0.0076 and 0.0082 before the hoist, and still a
figure from the first legal pair rather than a practical threshold. It comes
from equating the two measured costs,

```
n* = rivers × players × a / (density × deals_per_pair × b)
```

with a the cost of a combo visit, b the cost of a deal, `rivers` the 44
candidate rivers (48 minus the board's four) and `deals_per_pair` the 38 or 36
rivers a heads-up PLO5 or PLO6 pair can see. A third player makes the terminal
cubic, so no single crossing is reported for those rows.

What that leaves is still a single lever, and the lever is now cheaper to
move: the tuple walk. Any work on the terminal's cost — the sparse blocker
aggregation of issue #332 included — has to remove deals or make them cheaper,
because removing filter work would buy two thousandths of the call.

At three players the per-deal cost rises again — 23.2 ns for PLO5 and 28.7 ns
for PLO6, against 19.2 and 24.7 heads-up — because the multiway showdown path
runs. The residual in the table is a clock difference, not work: the benchmark
times the whole call with `mach_absolute_time` on macOS while the terminal
times its phases with `clock_gettime`, so `unaccounted` can go slightly
negative. Filter plus terminal account for 99.98 % of the call in every row,
which is the check the issue asked for. PLO4 heads-up at 194,580 takes the
vector sweep, which counts no deals at all, so `deals` is 0 and there is no
per-deal figure for it; its filter still copies 17.1 million combos (274 MB) in
17.8 ms, about 15 GB/s, which is memory bandwidth and still 0.16 % of the call.

## Multiway multiplies tuples

Three players, PLO5, 25 combos each: 2,067 legal triples, 68,211 deals,
0.00339 s (49.7 ns per deal). The tuple count grows with `n³` instead of `n²`
(here `0.13 · n³`, against `0.465 · n²` heads-up), so a third player is worth
far more than a wider range: this 25-combo three-way spot costs about what
heads-up PLO5 costs at 120 combos. Values summed to zero to 1e-13 across the
three players, which is the conservation check of the fixture.

## Parallel rivers: 2 to 16 lanes

The candidate rivers are independent, so the benchmark can price them in
parallel: `--threads N` runs N lanes, each with its own eval context and its own
compaction buffers, and each lane takes the cards `card % N == lane`. Per-river
results are collected by card and reduced in card order with the serial
terminal's own arithmetic, so a parallel run is not merely close to the serial
one: the benchmark compares the value fingerprint, the river count and the deal
mass bit for bit and reports `matches_serial`. It held in every run below.

| Variant | Combos | Serial | 2 lanes | 4 lanes | 8 lanes | Speedup at 8 |
|---|---|---|---|---|---|---|
| PLO5 | 1,994 | 1.429 s | 0.687 s | 0.371 s | 0.282 s | 4.80× |
| PLO6 | 854 | 0.227 s | 0.115 s | 0.062 s | 0.043 s | 5.72× |
| PLO6 | 1,600 | 0.778 s | 0.418 s | 0.195 s | 0.140 s | 5.45× |
| PLO6 | 2,091 | 1.262 s | 0.618 s | 0.354 s | 0.227 s | 5.36× |

Two lanes give 1.86–2.08×, four 3.41–3.79×, and eight 4.80–5.72×, so the last
lanes of this 10-core machine still do not carry a proportional share but lose
less than they did before design A (4.42–5.19× then). The figures are reported
as measured, not modelled; each run's serial column is the call timed inside
that same process, so the speedups do not mix sessions.

### 12 and 16 lanes

The PLO6 2,091-combo configuration was measured at 12 and 16 lanes as well,
with three repetitions per lane count, one process after another in the same
session (load 4.0–6.2, above the 3.5–4.5 of the table above, which is why
this sweep reads 0.314 s at 8 lanes against 0.227 s there):

| Lanes | Lane median | Serial, same process | Speedup | Karp-Flatt `e` |
|---|---|---|---|---|
| 2 | 0.7915 s | 1.5212 s | 1.92× | 0.041 |
| 4 | 0.4531 s | 1.5072 s | 3.33× | 0.068 |
| 8 | 0.3145 s | 1.5385 s | 4.89× | 0.091 |
| 12 | 0.3064 s | 1.5887 s | 5.19× | 0.119 |
| 16 | 0.2787 s | 1.5144 s | 5.43× | 0.130 |

Every lane count reproduced the serial value fingerprint bit for bit
(`35f87d20638e3909`) and reports `matches_serial` true, and the five samples
of the serial call span 1.507–1.589 s, so the denominators are comparable
within 5 per cent. The load was not held across that sweep, though: it read
3.95 before the first run and 6.22 after the last, so its later points are
cheaper for a reason that has nothing to do with lanes. Two interleaved
sessions were run to separate the two effects, 8, 16, 8, 16 and then
8, 12, 16, 8, 12, 16, with the load held to 4.3–4.9 and 3.87–4.12:

| Session | Lanes | Lane median | Serial | Speedup | Karp-Flatt `e` |
|---|---|---|---|---|---|
| A | 8 | 0.2737 s | 1.4351 s | 5.24× | 0.075 |
| A | 16 | 0.2904 s | 1.4455 s | 4.98× | 0.148 |
| A | 8 | 0.2838 s | 1.4393 s | 5.07× | 0.083 |
| A | 16 | 0.2783 s | 1.4475 s | 5.20× | 0.138 |
| B | 8 | 0.2863 s | 1.4268 s | 4.98× | 0.086 |
| B | 12 | 0.2721 s | 1.4417 s | 5.30× | 0.115 |
| B | 16 | 0.2630 s | 1.4645 s | 5.57× | 0.125 |
| B | 8 | 0.2819 s | 1.4408 s | 5.11× | 0.081 |
| B | 12 | 0.2895 s | 1.4477 s | 5.00× | 0.127 |
| B | 16 | 0.2662 s | 1.4825 s | 5.57× | 0.125 |

Sixteen lanes are not worth the eight they cost, but not because they do
nothing: pooled over four samples each, 8 lanes average 0.2814 s and 16 lanes
0.2745 s, a 2.5 per cent difference inside a 5 per cent spread, and the two
sessions disagree in sign — session A has 16 lanes 2 per cent slower, session
B has them 7 per cent faster, twice. Twelve lanes land between them at
5.00–5.30× with a 6 per cent spread of their own. What survives every sample
is the level, 5.0–5.6×; the sweep's 11 per cent at 16 lanes does not, because
that was the drifting load.

`e` is the other thing that survives. It sits at 0.075–0.086 for eight lanes in
both sessions and at 0.125–0.148 for sixteen, so it tracks the lane count and
not the order of the runs or the load. `e = (1/S - 1/p) / (1 - 1/p)` then runs
from 0.041 at 2 lanes to 0.125–0.148 at 16, and a constant serial fraction
would hold it flat: Amdahl does not describe this machine, and the lanes share
something that degrades as their number grows. The most plausible reading is
the core layout — four performance cores and six efficiency cores, so eight
lanes already fill the fast ones, four more land on the slow ones and two
oversubscribe them — but no run here isolates it, and an L2 or last-level
cache effect or scheduler contention would produce the same curve. Peak RSS is
not the cause: 6.77–6.86 MiB at 8 lanes, 7.45–7.61 MiB at 16.

The Amdahl extrapolation this report carried as a hypothesis is retired too:
`f = 0.091` taken from the sweep's 8-lane point predicts 6.3× at 12 lanes and
6.9× at 16, against 5.00–5.30× and 5.57× measured.

So the ceiling is 5.0–5.6×, reached around eight lanes: four more lane groups
cost 0.7 MiB of peak RSS and buy 2.5 per cent pooled, which no repeat has
confirmed. What remains is the serial fraction inside a lane.

The terminal still exposes one serial entry point, so the fan-out is the
caller's: one eval context and one compaction buffer per lane, both of which a
caller pools anyway. Eight contexts do not move the memory figure — peak RSS
stays at 6.6–7.2 MiB against 6.5–6.8 MiB serial — and sixteen contexts reach
7.47–7.61 MiB at PLO6 2,091 combos, still under 8 MiB.

## Sparse blocker aggregation: the sweep at PLO5 and PLO6

Issue #332, design B. The deal walk stays the certified path; this is the
measured alternative, reachable only through `--sparse-blockers` in the
benchmark (`pe_omaha_river_set_sparse_blockers(1)`, off by default, never set
by a solver).

The walk costs `density * n^2` visits per river whatever the implementation,
because every legal pair carries its own weight. The sweep does not visit
pairs at all: it sorts each range by strength, accumulates opponent reach per
card subset, and takes each combo's compatible mass and win mass by
inclusion-exclusion over its `2^holes - 1` nonempty subsets -- 31 at PLO5, 63
at PLO6, against the 15 the four-card path already used. The work per river
becomes `(2^holes - 1) * (opp_n + 2 * hero_n)` subset touches instead of
`hero_n * opp_n` visits.

Measured heads-up high-only on the gate board, `--repetitions 3`, one session,
each pair of runs in the same load condition:

| Variant | Combos | Deal walk | Sweep | Speedup | Pair visits | Subset touches | Fingerprint |
|---|---|---|---|---|---|---|---|
| PLO6 | 2,000 | 1.112-1.121 s | 0.350-0.368 s | 3.11x | 50,849,064 | 12.1 M | identical |
| PLO6 | 2,091 | 1.213-1.227 s | 0.384-0.391 s | 3.15x | 55,589,004 | 12.6 M | identical |
| PLO5 | 1,994 | 1.346-1.363 s | 0.191-0.192 s | 7.08x | 70,218,300 | 5.95 M | identical |

Subset touches are counted the way the implementation spends them: `2^holes -
1` per combo per side per river, so 48 rivers x 2 ranges x 2,000 x 63 at PLO6.
PLO5 is seven times faster rather than three because its hands carry 31
subsets instead of 63 while the walk it replaces is just as long.

Both of the criteria the issue fixed in advance are met at PLO6 n = 2,000: the
turn visits **no** pair, against 50.8 million, and it lands at **0.350-0.368 s**
against the 0.6 s line and the 1.112-1.121 s walk.

The equality in the last column is exactness, not a property of the method.
The benchmark draws weights that are dyadic, so every product and every sum in
both paths is exactly representable and the re-associated total lands on the
same double. With weights that are not exactly representable the last bits
move: on the same river with weights in thirds and sevenths, the compatible
mass differs by 5.4e-13 relative and the per-player values by about 5e-13.
That is the residue the four-card sweep documents as 1e-9, and it is why the
sweep stays a diagnostic path and does not replace the walk by default.

Two things this section owes the reader, both measured:

- The first version of the sweep forgot the opponents stronger than every
  hero, because its subset table was only filled as far as the hero cursor
  walked. The compatible mass came back short -- 4 per cent on one river at 40
  combos -- and the error vanished as the ranges grew, so it was invisible in
  the gate configuration. `test_wide_sweep` pins it with unequal range sizes
  and unequal strength ladders, and fails 163 checks when the tail is dropped.
- The same tail was missing from the four-card sweep's `out_mass`, which the
  PLO4 terminal multiplies into its net, so this fixes a latent error there as
  well. See the next paragraph for what it moved.

### What the PLO4 tail fix moves

Replaying every reference PLO4 configuration twice, once with the tail folded
in and once with it cut out again, shows the fix is silent everywhere the
sweep is used as documented:

| Configuration | Fields | Diffs |
|---|---|---|
| `pe-plo4-certify` defaults (64 combos, uniform) | 57 | none |
| `--combos 32 --strategy mixed --weighted` (guide example) | 57 | none |
| `--combos 128 --weighted --max-iterations 32` | 57 | none |
| `--combos 512 --weighted --max-iterations 16 --pot 100` | 57 | none |
| `pe-plo4-river-bench` (small, medium, larger) | 60 | none |
| turn, 4 holes, 2,000 combos | 37 | only the timing |
| turn, 4 holes, 2,091 combos | 37 | only the timing |

One configuration does move: the turn at four holes and 512 combos. There the
compatible mass grows by 7.5e-5 relative, the values by 4.2e-3, and the
fingerprint goes from `ee44ea62b89b0509` to `aaff858c1a112dfb`. The direction
is the one the bug predicts -- the mass can only grow when the strongest
opponents are folded in -- and the size is consistent with how few of the 512
combos sit above every hero on that board. At 2,000 and 2,091 combos no
opponent sits above every hero, so the tail is empty and the turn is
bit-identical to the pre-fix run. That is why the gate configuration, the
certifier and the river bench never saw the error.

`test_wide_sweep` is the check that pins it: its four-card case compares the
sweep against a direct pair enumeration, and with the tail cut it fails 163
checks. The 26 tests matching `wide_sweep`, `plo4`, `omaha` and `showdown` are
green with the tail in place, and PLO4 still reports `plo4_sweep`.

What this does not buy: Hi/Lo (the low half would need a second strength
dimension), three or more players (the multiway vector path is separate), or a
default. The sweep has to pass a fingerprint decision before it can displace
the walk, and with weights that are not dyadic it does not reproduce the
fingerprint.

## Measured memory

Peak RSS is **6.5–6.8 MiB for every PLO5 and PLO6 configuration above**, from 7
thousand deals up to 70 million, and it stays under 7.2 MiB with eight river
lanes running, each carrying its own eval context and compaction buffers. The
terminal does not build a deal table: the walk streams tuples to a callback, so
memory is `O(ranges)`, not `O(tuples)`. What it allocates for itself is one
compaction buffer per player, `combos × 16 B`, freed on return:

| Config | Range bytes | Terminal buffers | Peak RSS |
|---|---|---|---|
| PLO6, 800 combos | 25.6 kB | 25.6 kB | 6.6 MiB |
| PLO5, 1,600 combos | 51.2 kB | 51.2 kB | 6.7 MiB |

Design A did not change that budget: the evaluation table a river reuses is
sized by the combos of one filtered range, which is already inside the
compaction buffer it writes, and it lives for one river call.

PLO4 is the exception: its vector sweep allocates per-combo tables for each
river, so peak RSS grows with the range (`~190 B` per combo by the two extremes
of the table, against `~48 B` of live per-combo tables). The terminal
frees every one of them on return; the gap is allocator retention across the
48 rivers, not a leak. It is reported here as measured, not explained further.
At the full turn it is 74.9 MiB, against 77.5 MiB before design A, which is the
same sweep and the same allocator.

So the two variants fail differently: **PLO5/PLO6 are time-bound with flat
memory, PLO4 is memory-bound only at full range**.

## Feasible range sizes

Fitted from the tables above and the frontier runs, one exact turn node,
single-threaded. The walk now has two terms, `a · n + b · n²`, the first being
the per-combo evaluation each river performs once and the second the visits:

- PLO4: `T ≈ 5.2e-5 · n` seconds — linear.
- PLO5: `T ≈ 3.4e-5 · n + 3.55e-7 · n²` seconds — within 8 % of every measured
  point from 50 to 1,994 combos.
- PLO6: `T ≈ 4.7e-5 · n + 2.58e-7 · n²` seconds — within 2 % of every measured
  point from 25 to 2,091 combos.

The quadratic coefficient is 39× smaller than the pre-design-A PLO5 figure
(1.39e-5) and 52× smaller for PLO6 (1.34e-5), and a linear term that did not
exist before now governs the small sizes.

Maximum range size per player for a time budget, rounded down:

| Budget | PLO4 | PLO5 | PLO6 |
|---|---|---|---|
| 10 s | 192,000 | 5,200 | 6,100 |
| 60 s | 1,155,000 | 12,900 | 15,100 |
| 10 min | 11,557,000 | 41,000 | 48,100 |
| 1 h | 69,342,000 | 100,000 | 118,000 |

Splitting the rivers over eight lanes multiplies those budgets by the measured
speedup, which is not `sqrt` of the serial figures any more because of the
linear term; the columns below solve the two-term fit with the budget scaled:

| Budget | PLO5 serial | PLO5, 8 lanes | PLO6 serial | PLO6, 8 lanes |
|---|---|---|---|---|
| 60 s | 12,900 | 28,400 | 15,100 | 33,300 |
| 10 min | 41,000 | 90,000 | 48,100 | 105,500 |

The full-range verdict does not change, but its size does: with the design A
walk the PLO5 full range falls from ~1.3 years to ~11 days, and the PLO6 one
from ~65 years to ~1.3 years. With eight lanes those become ~2.3 days and ~87
days — still out of reach, and still not a memory problem.

A full range at a fixed five-card board is `C(48,4) = 194,580` for PLO4,
`C(48,5) = 1,712,304` for PLO5 and `C(48,6) = 12,271,512` for PLO6
(`pe_omaha_combo_count()`). That gives a clean verdict at full range:

| Variant | Full-range combos | Range memory (two ranges) | One turn node |
|---|---|---|---|
| PLO4 | 194,580 | 5.9 MiB | **10.1 s (measured)** |
| PLO5 | 1,712,304 | 52.3 MiB | ≈ 11 days (extrapolated) |
| PLO6 | 12,271,512 | 374.5 MiB | ≈ 1.3 years (extrapolated, 1.2–1.4 across the coefficient band) |

**Exact full-combo turn evaluation is feasible for PLO4 and not for PLO5 or
PLO6.** The blocker is time, not memory: a full PLO6 turn fits in 375 MiB and
would still not finish. Any claim that wider hands are "just more memory" is
wrong by the numbers above.

## Frontier check

The budgets were fitted from 50–1,600 combos for PLO5 and 25–800 for PLO6, so
each one was then measured at the sizes it claims instead of being trusted as a
fit. Board, ranges and state as in Method, one call per process,
`--repetitions 1`, every sample with the load average it ran under.

| Variant | Combos | Fit predicts | Measured (s) at load | Verdict |
|---|---|---|---|---|
| PLO5 | 814 | 0.246 | 0.2449, 3.90 | inside |
| PLO5 | 1,994 | 1.480 | 1.5512, 4.22 | 5 % over |
| PLO6 | 854 | 0.234 | 0.2280, 4.22 | inside |
| PLO6 | 1,200 | 0.440 | 0.4289, 4.24 | inside |
| PLO6 | 1,600 | 0.760 | 0.7570, 4.26 | inside |
| PLO6 | 2,091 | 1.270 | 1.2077, 4.27 | inside |

One point, PLO5 at 1,994, came in 5 % over its fit; every other frontier
configuration landed at or under it, at 6.6–6.8 MiB. That single overshoot is
the reason the budget columns above are rounded down rather than rounded to the
fit, and it is the honest bound on them: a budget computed from the two-term
model should be read as ±10 % at these sizes. Contention still moves these
numbers more than range size does, and none of them is a threshold.

Peak RSS was 6.6–6.8 MiB for every frontier configuration, so nothing about the
wider ranges changes the memory story above.

## Execution policies

Stated as rules, with the guarantee each one carries:

1. **Exact, full combos — PLO4 turn.** Up to and including the full 194,580
   combos in ~10 s and ~75 MiB. This is the regime the phase A fixtures and
   the river certificate of issue #283 describe; a value computed here is
   exact over the enumerated chance space.
2. **Exact, sparse combos — PLO5/PLO6 turn.** Exact for the combos given, with
   ranges up to ~13,000 (PLO5) or ~15,000 (PLO6) per player inside a minute and
   ~41,000 or ~48,000 inside ten minutes, at ~7 MiB. This is the only exact
   regime for wider hands and it is the one to use when a spot is naturally
   sparse (a nut range, a single suit shape, a solved subgame's reach support).
   Design A moved this line by an order of magnitude; it did not remove it.
3. **Sampled — everything above that.** Beyond `O(10⁴)` combos per range the
   exact terminal is out of budget, so the options are sampling private tuples
   (an empirical, not exact, payoff) or bucketing combos with
   `pe_hand_features` and pricing the bucket representatives. Neither is
   implemented in this terminal; whichever is chosen must be labelled
   empirical in the solver output, exactly as the turn has no certificate
   today.
4. **Parallel rivers — measured, 4.8–5.7× at eight lanes.** The 48 rivers
   are independent calls with no shared state, and the benchmark fans them
   over 2, 4 and 8 lanes with one eval context each: 1.86–2.08×, 3.41–3.79×
   and 4.80–5.72× on the four frontier configurations, every result
   bit-identical to the serial call. A minute then fits ~28,000 combos per
   range instead of ~13,000, at ~7 MiB. Sixteen lanes were measured too, in
   two interleaved sessions at held load: they buy 2.5 per cent pooled, inside
   a 5 per cent spread and with the two sessions disagreeing in sign, so the
   ceiling is 5.0–5.6×. The terminal still exposes one serial entry point, so
   a caller owns the fan-out; that is the cheapest exact lever there is, and it
   saturates at eight lanes.

## What is not covered

- **The GPU decision gate (phase C) is untouched.** No kernel, profile or
  proposal is made here; the numbers above are CPU-only, which is exactly what
  phase C needs as its baseline — and design A lowered that baseline by ~40×,
  so the speedup a kernel would have to reach is ~40× higher than the same
  estimate made from the pre-design-A figures.
- **Sparse blocker-aggregation designs** (issue #332) are not implemented or
  measured. The cost model says what they would have to beat:
  `density · n²` tuple visits per river at ~20–26 ns each, plus the `n`
  evaluations per river that design A already removed from the walk.
- **No turn betting tree and no turn certificate**, as in phase A: these are
  showdown terminals, not two-street solves.
- **Hi/Lo** and dead-card variants are not measured; `dead` only shortens the
  river list. The benchmark has `--hilo`, and the Hi/Lo expansion, the filter
  and the reduction are unchanged by design A, but no figure here is Hi/Lo.
- **Machine-dependent.** The serial figures are single-threaded and vary with
  what else the machine is doing, which matters more now that a call is
  hundreds of milliseconds: PLO5 at 800 combos moved 8 % between two runs at
  load 3.5–4.5. Every figure comes from a sample whose load is reported, so a
  loaded machine is slower, never faster.
- Ranges above 4,096 combos are not checked for duplicate combos (the check is
  quadratic and a repeated point only shifts one combo's weight); heads-up at
  small sizes is deduplicated.

## Reproducing

```bash
cmake --build build --target pe-omaha-turn-bench --parallel
./build/tools/pe-omaha-turn-bench --holes 4 --combos 1000 --repetitions 3
./build/tools/pe-omaha-turn-bench --holes 4 --combos 10000 --repetitions 3
./build/tools/pe-omaha-turn-bench --holes 4 --combos 50000 --repetitions 3
./build/tools/pe-omaha-turn-bench --holes 4 --combos 194580 --repetitions 1
./build/tools/pe-omaha-turn-bench --holes 5 --combos 50 --repetitions 3
./build/tools/pe-omaha-turn-bench --holes 5 --combos 200 --repetitions 3
./build/tools/pe-omaha-turn-bench --holes 5 --combos 800 --repetitions 3
./build/tools/pe-omaha-turn-bench --holes 5 --combos 1600 --repetitions 1
./build/tools/pe-omaha-turn-bench --holes 6 --combos 25 --repetitions 3
./build/tools/pe-omaha-turn-bench --holes 6 --combos 100 --repetitions 3
./build/tools/pe-omaha-turn-bench --holes 6 --combos 400 --repetitions 3
./build/tools/pe-omaha-turn-bench --holes 6 --combos 800 --repetitions 1
./build/tools/pe-omaha-turn-bench --holes 5 --combos 814 --repetitions 3
./build/tools/pe-omaha-turn-bench --holes 5 --combos 1994 --repetitions 1
./build/tools/pe-omaha-turn-bench --holes 6 --combos 854 --repetitions 1
./build/tools/pe-omaha-turn-bench --holes 6 --combos 1200 --repetitions 1
./build/tools/pe-omaha-turn-bench --holes 6 --combos 1600 --repetitions 1
./build/tools/pe-omaha-turn-bench --holes 6 --combos 2091 --repetitions 1
./build/tools/pe-omaha-turn-bench --holes 5 --combos 25 --players 3 --repetitions 3
./build/tools/pe-omaha-turn-bench --holes 5 --combos 1994 --threads 2 --repetitions 1
./build/tools/pe-omaha-turn-bench --holes 5 --combos 1994 --threads 4 --repetitions 1
./build/tools/pe-omaha-turn-bench --holes 5 --combos 1994 --threads 8 --repetitions 1
./build/tools/pe-omaha-turn-bench --holes 6 --combos 854 --threads 2 --repetitions 1
./build/tools/pe-omaha-turn-bench --holes 6 --combos 854 --threads 4 --repetitions 1
./build/tools/pe-omaha-turn-bench --holes 6 --combos 854 --threads 8 --repetitions 1
./build/tools/pe-omaha-turn-bench --holes 6 --combos 1600 --threads 2 --repetitions 1
./build/tools/pe-omaha-turn-bench --holes 6 --combos 1600 --threads 4 --repetitions 1
./build/tools/pe-omaha-turn-bench --holes 6 --combos 1600 --threads 8 --repetitions 1
./build/tools/pe-omaha-turn-bench --holes 6 --combos 2091 --threads 2 --repetitions 1
./build/tools/pe-omaha-turn-bench --holes 6 --combos 2091 --threads 4 --repetitions 1
./build/tools/pe-omaha-turn-bench --holes 6 --combos 2091 --threads 8 --repetitions 1
./build/tools/pe-omaha-turn-bench --holes 6 --combos 2091 --threads 12 --repetitions 3
./build/tools/pe-omaha-turn-bench --holes 6 --combos 2091 --threads 16 --repetitions 3
# interleaved, twice over, for the load to affect every lane count equally
./build/tools/pe-omaha-turn-bench --holes 6 --combos 2091 --threads 8 --repetitions 3
./build/tools/pe-omaha-turn-bench --holes 6 --combos 2091 --threads 16 --repetitions 3
./build/tools/pe-omaha-turn-bench --holes 6 --combos 2091 --threads 8 --repetitions 3
./build/tools/pe-omaha-turn-bench --holes 6 --combos 2091 --threads 16 --repetitions 3
./build/tools/pe-omaha-turn-bench --holes 6 --combos 2091 --threads 8 --repetitions 3
./build/tools/pe-omaha-turn-bench --holes 6 --combos 2091 --threads 12 --repetitions 3
./build/tools/pe-omaha-turn-bench --holes 6 --combos 2091 --threads 16 --repetitions 3
./build/tools/pe-omaha-turn-bench --holes 6 --combos 2091 --threads 8 --repetitions 3
./build/tools/pe-omaha-turn-bench --holes 6 --combos 2091 --threads 12 --repetitions 3
./build/tools/pe-omaha-turn-bench --holes 6 --combos 2091 --threads 16 --repetitions 3
./build/tools/pe-omaha-turn-bench --holes 4 --combos 194580 --profile --repetitions 1
./build/tools/pe-omaha-turn-bench --holes 5 --combos 1994 --profile --repetitions 1
./build/tools/pe-omaha-turn-bench --holes 6 --combos 2091 --profile --repetitions 1
./build/tools/pe-omaha-turn-bench --holes 5 --combos 150 --players 3 --profile --repetitions 1
./build/tools/pe-omaha-turn-bench --holes 6 --combos 150 --players 3 --profile --repetitions 1
```

Each run prints one JSON document (`pe-omaha-turn-bench/v1`) with the wall
clock, the river count, the exact tuple and deal counts, the per-deal cost,
the single-river reference (including which showdown path it took) and the
process peak RSS. With `--profile` the document also carries a `profile` object
with the two phase times, their shares, the combos the filter examined and kept,
the bytes it copied, the terminal's deal count and the crossing above; without
it that field is null and the call runs with no counters at all.
