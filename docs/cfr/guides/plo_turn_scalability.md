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
- The deal count is exact, not estimated: the terminal visits every legal
  tuple and every river such a tuple leaves in the deck, so
  `deals = tuples × (48 − holes × players)`. The benchmark reads the tuple
  count from `pe_omaha_deals_measure()` and reports the product.
- Machine: Apple M5 (10 cores), macOS 27.0.1, Apple clang 17, `-O3 -DNDEBUG`,
  single-threaded. Figures are machine-specific and are not thresholds any
  test asserts.

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

The per-deal cost is flat in the range size: PLO5 ≈ 0.86 µs, PLO6 ≈ 1.11 µs.
Their ratio (1.3) is the ratio of hand evaluations per deal (150/100), which
says the terminal's time is the showdown evaluation and not bookkeeping.

### Multiway multiplies tuples

Three players, PLO5, 25 combos each: 2,067 legal triples, 68,211 deals,
0.0849 s (1,244 ns per deal). The tuple count grows with `n³` instead of `n²`
(here `0.13 · n³`, against `0.465 · n²` heads-up), so a third player is worth
far more than a wider range: this 25-combo three-way spot costs about what
heads-up PLO5 costs at 75 combos. Values summed to zero to 1e-13 across the
three players, which is the conservation check of the fixture.

## Measured memory

Peak RSS is **6.5–6.7 MiB for every PLO5 and PLO6 configuration**, from 7
thousand deals up to 45 million. The terminal does not build a deal table: the
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
- PLO5: `T ≈ 1.51e-5 · n²` seconds — measured 1.38e-5 to 1.71e-5 per point.
- PLO6: `T ≈ 1.37e-5 · n²` seconds — measured 1.30e-5 to 1.45e-5 per point.

Maximum range size per player for a time budget:

| Budget | PLO4 | PLO5 | PLO6 |
|---|---|---|---|
| 10 s | 169,000 | 814 | 854 |
| 60 s | 1,008,000 | 1,994 | 2,091 |
| 10 min | 10,089,000 | 6,307 | 6,612 |
| 1 h | 60,535,000 | 15,448 | 16,196 |

A full range at a fixed five-card board is `C(48,4) = 194,580` for PLO4,
`C(48,5) = 1,712,304` for PLO5 and `C(48,6) = 12,271,512` for PLO6
(`pe_omaha_combo_count()`). That gives a clean verdict at full range:

| Variant | Full-range combos | Range memory (two ranges) | One turn node |
|---|---|---|---|
| PLO4 | 194,580 | 5.9 MiB | **11.6 s (measured)** |
| PLO5 | 1,712,304 | 52.3 MiB | ≈ 1.4 years (extrapolated) |
| PLO6 | 12,271,512 | 374.5 MiB | ≈ 67 years (extrapolated) |

**Exact full-combo turn evaluation is feasible for PLO4 and not for PLO5 or
PLO6.** The blocker is time, not memory: a full PLO6 turn fits in 375 MiB and
would still not finish. Any claim that wider hands are "just more memory" is
wrong by the numbers above.

## Execution policies

Stated as rules, with the guarantee each one carries:

1. **Exact, full combos — PLO4 turn.** Up to and including the full 194,580
   combos in ~12 s and ~78 MiB. This is the regime the phase A fixtures and
   the river certificate of issue #283 describe; a value computed here is
   exact over the enumerated chance space.
2. **Exact, sparse combos — PLO5/PLO6 turn.** Exact for the combos given, with
   ranges up to ~2,000 per player inside a minute and ~6,300 inside ten
   minutes, at ~7 MiB. This is the only exact regime for wider hands and it
   is the one to use when a spot is naturally sparse (a nut range, a single
   suit shape, a solved subgame's reach support).
3. **Sampled — everything above that.** Beyond `O(10³)` combos per range the
   exact terminal is out of budget, so the options are sampling private tuples
   (an empirical, not exact, payoff) or bucketing combos with
   `pe_hand_features` and pricing the bucket representatives. Neither is
   implemented in this terminal; whichever is chosen must be labelled
   empirical in the solver output, exactly as the turn has no certificate
   today.
4. **Parallel rivers — available, unmeasured.** The 48 rivers are independent
   calls with no shared state, so the cheapest scaling lever is to split them
   across threads: near-linear for PLO5/PLO6 up to memory bandwidth, with
   memory per worker of `O(ranges)`. This was not measured here and is not
   claimed as a speedup; it is the step to try before any GPU work, because it
   needs no new kernel and keeps the exact guarantee.

## What is not covered

- **The GPU decision gate (phase C) is untouched.** No kernel, profile or
  proposal is made here; the numbers above are CPU-only, which is exactly what
  phase C needs as its baseline.
- **Sparse blocker-aggregation designs** (the other half of the phase B
  bullet) are not implemented or measured. The cost model says what they would
  have to beat: `density · n²` tuple visits per river at ~0.9–1.1 µs each.
- **No turn betting tree and no turn certificate**, as in phase A: these are
  showdown terminals, not two-street solves.
- **Hi/Lo** and dead-card variants are not measured; `dead` only shortens the
  river list.
- Ranges above 4,096 combos are not checked for duplicate combos (the check is
  quadratic and a repeated point only shifts one combo's weight); heads-up at
  small sizes is deduplicated.

## Reproducing

```bash
cmake --build build --target pe-omaha-turn-bench --parallel
./build/tools/pe-omaha-turn-bench --holes 5 --combos 800 --repetitions 3
./build/tools/pe-omaha-turn-bench --holes 6 --combos 800 --repetitions 3
./build/tools/pe-omaha-turn-bench --holes 4 --combos 194580 --repetitions 1
```

Each run prints one JSON document (`pe-omaha-turn-bench/v1`) with the wall
clock, the river count, the exact tuple and deal counts, the per-deal cost,
the single-river reference (including which showdown path it took) and the
process peak RSS.
