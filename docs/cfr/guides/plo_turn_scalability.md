# Turn Terminal Scalability: PLO4, PLO5, PLO6

Issue #285, phase B. Terminal: `include/poker_eval/solver/pe_omaha_turn.h`.
Benchmark: `tools/pe_omaha_turn_bench.c` (target `pe-omaha-turn-bench`).
Correctness background: [plo4_turn_chance.md](plo4_turn_chance.md).

This report answers one question with measurements: how much time and memory
does an exact turn-to-river terminal cost for 4-, 5- and 6-card private hands,
and how large may the ranges be before the answer is "not feasible"? Nothing
here is a solver run, a sample or an estimate of strategy quality; every figure
is one terminal call at a turn node, and every configuration is exact.

## Method

- Board `As Kd 7c 2h`, no dead cards, so the terminal prices 48 rivers.
- Ranges are drawn deterministically (LCG) from the deck the board leaves:
  distinct combos, every eleventh one with weight 0 and the rest spread over
  `[0.25, 1.0]`. Both ranges have the same size; heads-up unless stated.
- One configuration per process, so the reported peak RSS belongs to that
  configuration and not to a larger one that ran before it.
- The reported seconds are the median of 3 repetitions (1–2 for the largest
  configurations, where a single call already runs for seconds).
- A single-call timing is not repeatable on a busy machine: the same
  configuration measured twice here differed by up to 2× (PLO5, 814 combos:
  8.67 s at load 3 and 17.8 s at load 21). The frontier check reports every run
  with the load average it ran under and reads the one taken in a window whose
  speed was checked against the grid.
- The deal count is exact, not estimated: the terminal visits every legal
  tuple and every river such a tuple leaves in the deck, so
  `deals = tuples × (48 − holes × players)`. The benchmark reads the tuple
  count from `pe_omaha_deals_measure()` and reports the product.
-   Machine: Apple M5 (10 cores), macOS 27.0.1, Apple clang 17, `-O3 -DNDEBUG`,
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
| PLO4, heads-up, < 32 combos | deal walk | `O(density × n²)` |
| PLO4 multiway, all PLO5/PLO6 | deal walk | `O(density × n²)` |

The sweep is gated on `hole_cards == 4` in `omaha_river.c`, so **PLO5 and PLO6
always take the deal walk**: `density × n²` legal tuples, each paying one
product Omaha evaluation (`C(5,2)·C(5,3) = 100` hands for PLO5, `150` for
PLO6). That is the whole scalability story — PLO4 is linear in the range size,
PLO5/PLO6 are quadratic.

Measured tuple density (legal tuples / n², i.e. the chance that two private
hands do not share a card): **0.465** for PLO5, **0.353** for PLO6. It is a
property of the hand size, not of the range size or the board.

The measured `turn / river` ratio is 33–58 across the grid, i.e. one turn is
about 48 single-river terminals, as the design says it must be.

## Measured time

### PLO4: linear, 194,580 combos is the full turn

| Combos per range | Turn (s) | µs per combo | Peak RSS |
|---|---|---|---|
| 1,000 | 0.061 | 30.6 | 8.0 MiB |
| 10,000 | 0.589 | 29.5 | 18.0 MiB |
| 50,000 | 2.921 | 29.2 | 35.7 MiB |
| 194,580 (full) | 11.572 | 29.7 | 77.5 MiB |

µs per combo is `turn / (combos × players)` and stays flat, which is the
linear behaviour the sweep predicts: `T(n) ≈ 5.9e-5 · n` seconds, about 17,000
combos of range size per second.

### PLO5: quadratic

| Combos per range | Legal tuples | Multiway deals | Turn (s) | ns per deal | Peak RSS |
|---|---|---|---|---|---|
| 50 | 1,159 | 44,042 | 0.0345 | 784 | 6.5 MiB |
| 200 | 18,490 | 702,620 | 0.6013 | 856 | 6.6 MiB |
| 800 | 297,090 | 11,289,420 | 9.2345 | 818 | 6.6 MiB |
| 1,600 | 1,190,001 | 45,220,038 | 43.7172 | 967 | 6.7 MiB |

### PLO6: quadratic, on 50% more hand evaluations per deal

| Combos per range | Legal tuples | Deals | Turn (s) | ns per deal | Peak RSS |
|---|---|---|---|---|---|
| 25 | 206 | 7,416 | 0.0081 | 1,098 | 6.5 MiB |
| 100 | 3,471 | 124,956 | 0.1308 | 1,047 | 6.5 MiB |
| 400 | 56,513 | 2,034,468 | 2.3225 | 1,142 | 6.6 MiB |
| 800 | 225,689 | 8,124,804 | 9.1805 | 1,130 | 6.6 MiB |

PLO5 stays between 0.72 and 0.97 µs per deal from 50 combos to 1,994, and PLO6
between 1.00 and 1.15 µs from 25 to 2,091: both leave the per-deal cost flat in
the range size. At the small sizes the PLO5/PLO6 ratio is 1.3, the ratio of
hand evaluations per deal (150/100), which says the terminal's time is the
showdown evaluation and not bookkeeping. The same configurations report up to
twice that per-deal cost while the machine is busy — PLO5 at 814 combos costs
0.74 µs each at load 3 and 1.52 µs at load 18; PLO6 at 2,091 costs 1.04 µs at
load 4 and 1.20–1.49 µs at load 16–21 — so a per-deal figure is only worth
reading next to the load average it was taken under.

### Multiway multiplies tuples

Three players, PLO5, 25 combos each: 2,067 legal triples, 68,211 deals,
0.0849 s (1,244 ns per deal). The tuple count grows with `n³` instead of `n²`
(here `0.13 · n³`, against `0.465 · n²` heads-up), so a third player is worth
far more than a wider range: this 25-combo three-way spot costs about what
heads-up PLO5 costs at 75 combos. Values summed to zero to 1e-13 across the
three players, which is the conservation check of the fixture.

### Parallel rivers: 2, 4 and 8 lanes

The candidate rivers are independent, so the benchmark can price them in
parallel: `--threads N` runs N lanes, each with its own eval context and its own
compaction buffers, and each lane takes the cards `card % N == lane`. Per-river
results are collected by card and reduced in card order with the serial
terminal's own arithmetic, so a parallel run is not merely close to the serial
one: the benchmark compares the value fingerprint, the river count and the deal
mass bit for bit and reports `matches_serial`. It held in all twelve runs below.

| Variant | Combos | Serial | 2 lanes | 4 lanes | 8 lanes | Speedup at 8 |
|---|---|---|---|---|---|---|
| PLO5 | 1,994 | 52.9 s | 28.1 s | 14.9 s | 12.0 s | 4.42× |
| PLO6 | 854 | 10.0 s | 4.95 s | 3.04 s | 1.93 s | 5.19× |
| PLO6 | 1,600 | 34.8 s | 17.7 s | 10.1 s | 7.02 s | 4.95× |
| PLO6 | 2,091 | 60.5 s | 30.3 s | 17.6 s | 12.2 s | 4.96× |

Two lanes are the only point that scales cleanly (1.94–1.98×); four give
3.41–3.52×, and eight add only about another 1.4× on top, so the last lanes of
this 10-core machine do not carry a proportional share. The figures are
reported as measured, not modelled. The short PLO6 run scales best because it
spends the least time queueing for the memory bus, which is also why the two
larger PLO6 configurations land within 4% of each other.

The terminal still exposes one serial entry point, so the fan-out is the
caller's: one eval context and one compaction buffer per lane, both of which a
caller pools anyway. Eight contexts do not move the memory figure — peak RSS
stays at 6.6–6.7 MiB against 6.5–6.7 MiB serial.

## Measured memory

Peak RSS is **6.5–7.1 MiB for every PLO5 and PLO6 configuration**, from 7
thousand deals up to 70 million, and it stays there with eight river lanes
running, each carrying its own eval context and compaction buffers. The terminal does not build a deal table: the
walk streams tuples to a callback, so memory is `O(ranges)`, not
`O(tuples)`. What it allocates for itself is one compaction buffer per player,
`combos × 16 B`, freed on return:

| Config | Range bytes | Terminal buffers | Peak RSS |
|---|---|---|---|
| PLO6, 800 combos | 25.6 kB | 25.6 kB | 6.6 MiB |
| PLO5, 1,600 combos | 51.2 kB | 51.2 kB | 6.7 MiB |

PLO4 is the exception: its vector sweep allocates per-combo tables for each
river, so peak RSS grows with the range (`~190 B` per combo by the two extremes
of the table, against `~48 B` of live per-combo tables). The terminal
frees every one of them on return; the gap is allocator retention across the
48 rivers, not a leak. It is reported here as measured, not explained further.

So the two variants fail differently: **PLO5/PLO6 are time-bound with flat
memory, PLO4 is memory-bound only at full range**.

## Feasible range sizes

Fitted from the tables, one exact turn node, single-threaded:

- PLO4: `T ≈ 5.9e-5 · n` seconds — linear.
- PLO5: `T ≈ 1.39e-5 · n²` seconds — measured 1.31e-5 to 1.71e-5 per point.
- PLO6: `T ≈ 1.34e-5 · n²` seconds — measured 1.27e-5 to 1.45e-5 per point,
  2,091 combos included (1.33e-5 there; Frontier check).

Maximum range size per player for a time budget:

| Budget | PLO4 | PLO5 | PLO6 |
|---|---|---|---|
| 10 s | 169,000 | 814 | 854 |
| 60 s | 1,008,000 | 1,994 | 2,091 |
| 10 min | 10,089,000 | 6,307 | 6,612 |
| 1 h | 60,535,000 | 15,448 | 16,196 |

Both quadratic columns are the fit rounded down, and every frontier point
measured below landed at or under its budget, so neither column needed a
correction.

Splitting the rivers over eight lanes multiplies those budgets by the measured
speedup, which is `sqrt` of the figures above because the cost is quadratic in
the range size:

| Budget | PLO5 serial | PLO5, 8 lanes | PLO6 serial | PLO6, 8 lanes |
|---|---|---|---|---|
| 60 s | 1,994 | 4,460 | 2,091 | 4,650 |
| 10 min | 6,307 | 14,100 | 6,612 | 14,700 |

The full-range verdict does not change, but its size does: with eight lanes the
PLO5 full range falls from ~1.3 years to ~107 days, and the PLO6 one from ~65
years to ~13.

A full range at a fixed five-card board is `C(48,4) = 194,580` for PLO4,
`C(48,5) = 1,712,304` for PLO5 and `C(48,6) = 12,271,512` for PLO6
(`pe_omaha_combo_count()`). That gives a clean verdict at full range:

| Variant | Full-range combos | Range memory (two ranges) | One turn node |
|---|---|---|---|
| PLO4 | 194,580 | 5.9 MiB | **11.6 s (measured)** |
| PLO5 | 1,712,304 | 52.3 MiB | ≈ 1.3 years (extrapolated) |
| PLO6 | 12,271,512 | 374.5 MiB | ≈ 65 years (extrapolated, 61–69 across the coefficient band) |

**Exact full-combo turn evaluation is feasible for PLO4 and not for PLO5 or
PLO6.** The blocker is time, not memory: a full PLO6 turn fits in 375 MiB and
would still not finish. Any claim that wider hands are "just more memory" is
wrong by the numbers above.

## Frontier check

The budgets were fitted from 50–1,600 combos for PLO5 and 25–800 for PLO6, so
each one was then measured at the combos it claims instead of being trusted as
a fit. Board, ranges and state as in Method, one call per process,
`--repetitions 1`. The machine was carrying other work for most of this
session, so every sample is listed with the load average it ran under.

| Variant | Combos | Fit predicts | Samples (s) and their load average | Verdict |
|---|---|---|---|---|
| PLO5 | 814 (10 s) | 10.0 | 8.67, 3.1–3.5; 8.8 and 17.8, 16–21 | inside |
| PLO5 | 1,994 (60 s) | 60.0 | 52.5, 3.1–3.5; 55.2 and 90.7, 16–21 | inside |
| PLO6 | 854 (10 s) | 10.0 | 9.31–10.04, 2.5–4.9; 9.8 and 10.6, 16–21 | inside |
| PLO6 | 1,200 | 19.7 | 18.4, 2.5–4.6; 21.1, 16–21 | inside |
| PLO6 | 1,600 | 35.1 | 34.3, 2.5–4.6; 44.3 and 61.5, 16–21 | inside |
| PLO6 | 2,091 (60 s) | 60.0 | 58.0, 3.7–4.9; 66.6, 77.2 and 82.7, 16–21 | inside |

Two things follow:

- **Every budget holds, and every point was read in a window that was checked,
  not assumed.** Each frontier configuration was re-run with a configuration
  the grid already covers on either side of it: 800 combos for PLO5 (9.23 s in
  the grid) and 854 for PLO6 (10.5 s when the grid's 800-combo point is scaled
  to the same size). In those windows the calibrations came back at 8.10–8.59 s
  and 9.31–10.04 s, both faster than the grid, so the machine was no slower
  than the one it came from. There, PLO5 took 8.67 s at 814 combos and 52.5 s
  at 1,994; PLO6 took 9.31 s at 854, 18.36 s at 1,200, 34.29 s at 1,600 and
  58.0 s at 2,091. Every one lands at or under its budget or its fit, at 0.74
  µs and 1.00–1.05 µs per deal, inside the per-deal band of the fitted
  configurations. Neither fit needed correcting: the loaded samples, up to
  2.5× slower, are contention.
- **Contention, not range size, moves these numbers.** The same PLO5
  configuration at 814 combos ran 8.67 s at load 3 and 17.8 s at load 21 with
  nothing in the code changed — 78% over its budget on the loaded sample —
  while every memory figure stayed put (6.9–7.1 MiB). Verdicts therefore rest
  on a sample taken in a checked window, and none of these numbers is a
  threshold.

Peak RSS was 6.9–7.1 MiB for every frontier configuration, so nothing about the
wider ranges changes the memory story above.

## Execution policies

Stated as rules, with the guarantee each one carries:

1. **Exact, full combos — PLO4 turn.** Up to and including the full 194,580
   combos in ~12 s and ~78 MiB. This is the regime the phase A fixtures and
   the river certificate of issue #283 describe; a value computed here is
   exact over the enumerated chance space.
2. **Exact, sparse combos — PLO5/PLO6 turn.** Exact for the combos given, with
   ranges up to ~2,000 (PLO5) or ~2,100 (PLO6) per player inside a minute and
   ~6,300 or ~6,600 inside ten minutes, at ~7 MiB. This is the only exact
   regime for wider hands and it is the one to use when a spot is naturally
   sparse (a nut range, a single suit shape, a solved subgame's reach support).
3. **Sampled — everything above that.** Beyond `O(10³)` combos per range the
   exact terminal is out of budget, so the options are sampling private tuples
   (an empirical, not exact, payoff) or bucketing combos with
   `pe_hand_features` and pricing the bucket representatives. Neither is
   implemented in this terminal; whichever is chosen must be labelled
   empirical in the solver output, exactly as the turn has no certificate
   today.
4. **Parallel rivers — measured, 4.4–5.2×.** The 48 rivers are independent
   calls with no shared state, and the benchmark fans them over 2, 4 and 8
   lanes with one eval context each: 1.94–1.98×, 3.41–3.52× and 4.42–5.19× on
   the four frontier configurations, every result bit-identical to the serial
   call. A minute then fits ~4,500 combos per range instead of ~2,100, at
   ~6.7 MiB. The terminal still exposes one serial entry point, so a caller
   owns the fan-out; that is the cheapest exact lever there is, and on this
   machine it stops at about 5×, so the next step before any GPU work is the
   profile rather than more lanes.

## What is not covered

- **The GPU decision gate (phase C) is untouched.** No kernel, profile or
  proposal is made here; the numbers above are CPU-only, which is exactly what
  phase C needs as its baseline.
- **Sparse blocker-aggregation designs** (the other half of the phase B
  bullet) are not implemented or measured. The cost model says what they would
  have to beat: `density · n²` tuple visits per river at 0.72–1.05 µs each.
- **No turn betting tree and no turn certificate**, as in phase A: these are
  showdown terminals, not two-street solves.
- **Hi/Lo** and dead-card variants are not measured; `dead` only shortens the
  river list.
- **Machine-dependent.** The serial figures are single-threaded and vary by up
  to 2.5× between runs depending on what else the machine is doing (PLO5 at 814
  combos: 8.67 s at load 3, 17.8 s at load 21). The lane table was taken at load
  3.8–6.2, and its speedups are ratios from the same run, so they are steadier
  than its absolute seconds. Every figure comes from a sample whose window was
  checked against the grid, so a loaded machine is slower, never faster.
- Ranges above 4,096 combos are not checked for duplicate combos (the check is
  quadratic and a repeated point only shifts one combo's weight); heads-up at
  small sizes is deduplicated.

## Reproducing

```bash
cmake --build build --target pe-omaha-turn-bench --parallel
./build/tools/pe-omaha-turn-bench --holes 5 --combos 800 --repetitions 3
./build/tools/pe-omaha-turn-bench --holes 6 --combos 800 --repetitions 3
./build/tools/pe-omaha-turn-bench --holes 4 --combos 194580 --repetitions 1
./build/tools/pe-omaha-turn-bench --holes 5 --combos 1994 --repetitions 1
./build/tools/pe-omaha-turn-bench --holes 6 --combos 854  --repetitions 1
./build/tools/pe-omaha-turn-bench --holes 6 --combos 1200 --repetitions 1
./build/tools/pe-omaha-turn-bench --holes 6 --combos 854  --repetitions 1
./build/tools/pe-omaha-turn-bench --holes 6 --combos 1600 --repetitions 1
./build/tools/pe-omaha-turn-bench --holes 6 --combos 854  --repetitions 1
./build/tools/pe-omaha-turn-bench --holes 6 --combos 2091 --repetitions 1
./build/tools/pe-omaha-turn-bench --holes 6 --combos 854  --repetitions 1
./build/tools/pe-omaha-turn-bench --holes 5 --combos 1994 --threads 2 --repetitions 1
./build/tools/pe-omaha-turn-bench --holes 5 --combos 1994 --threads 4 --repetitions 1
./build/tools/pe-omaha-turn-bench --holes 5 --combos 1994 --threads 8 --repetitions 1
./build/tools/pe-omaha-turn-bench --holes 6 --combos 2091 --threads 8 --repetitions 1
```

Each run prints one JSON document (`pe-omaha-turn-bench/v1`) with the wall
clock, the river count, the exact tuple and deal counts, the per-deal cost,
the single-river reference (including which showdown path it took) and the
process peak RSS.
