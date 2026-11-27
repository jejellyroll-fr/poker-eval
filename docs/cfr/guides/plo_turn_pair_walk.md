# The turn's pair walk: two designs for the terminal's tuple cost

Issue #285 phase B, follow-up #332. Neither design is implemented. What *is*
measured is the quantity they turn on: the cost of a deal walk with its
evaluation taken out, by the frozen-strength probe that this issue produced as
its first increment. The unit costs the designs build on come from the phase
profile of #331; every figure below is either measured here or labelled as
arithmetic on a measurement.

## What the profile settled

A turn call spends 99.99 % of its time in the river terminal: the per-river
filter is 0.0004 % of a PLO5 or PLO6 call and 0.16 % of a PLO4 one. So the
target is the terminal's tuple walk, and any work on the filter would buy a
thousandth of the call.

Measured per-deal cost, board `AsKd7c2h`, heads-up unless stated:

| Variant | Combos | Turn | Per deal | Deals per river |
|---|---|---|---|---|
| PLO5 | 1,994 | 52.68 s | 750 ns | 1,462,881 |
| PLO6 | 854 | 9.77 s | 1,052 ns | 193,442 |
| PLO6 | 1,600 | 35.45 s | 1,088 ns | 678,980 |
| PLO6 | 2,091 | 60.44 s | 1,087 ns | 1,158,104 |
| PLO6, 3 players | 150 | 7.56 s | 1,514 ns | 104,079 |

## What the walk does today

`src/solver/domain/omaha_river.c` enumerates the legal deals and calls
`omaha_callback` once per deal; the callback runs `StdDeck_OmahaHi_EVAL` for
**every player on every deal**. One Omaha evaluation over a private hand and a
five-card board is `C(holes, 2) × C(5, 3)` five-card lookups: 150 for six holes,
100 for five, 60 for four. Dividing the measured per-deal cost by
`players × lookups` gives the same constant in every row:

- PLO6 2,091: 1,087 ns / (2 × 150) = 3.6 ns per lookup
- PLO6 854: 1,052 ns / (2 × 150) = 3.5 ns per lookup
- PLO5 1,994: 750 ns / (2 × 100) = 3.8 ns per lookup

Three variants agreeing on one constant is what the model looks like if a deal
is the repeated evaluation and little else; it does not prove it, because the
constant was fitted on that assumption, and the probe below is what decides. The
walk also tests `weight > 0` and disjointness per pair, distributes the pot
through `pe_pot_distribute`, and accumulates; whatever those cost is the
residual the model carries.

### The audit this issue asked for

- `pe_blockers_compatible_sum` has an O(n + m) per-card accumulator, but only
  for **two-card** hands; wider hands take the exact O(n · m) fallback
  (`pe_blockers_compatible_sum_pairwise`). It is a reference, not a fast path,
  and it cannot be lifted to PLO5/PLO6 aggregation as it stands.
- `pe_showdown_vector` and `pe_showdown_vector_mass` already implement wide-hand
  aggregation: sort both ranges by strength, accumulate opponent reach per
  nonempty card subset, and recover each hero combo's compatible mass by
  inclusion-exclusion. It runs for four-card ranges of at least
  `PE_SHOWDOWN_PLO4_MIN_OPPONENTS` (32) opponents and only heads-up
  (`slice_count == 1`, `eligible_mask == 0x03`), which is exactly why PLO5 and
  PLO6 still enumerate.
- So both designs extend machinery that exists and is tested, and neither adds
  a payoff model: the pot distribution, the slice policy and the terminal's
  conventions stay where they are.

## Design A — hoist the strength, keep the walk

Evaluate each declared combo **once per river** and hand the `HandVal` to the
pair loop, which keeps its current shape: same visit order, same weight, same
distribution, same accumulation. Nothing is re-associated, so the floating
point result is identical by construction — the same terms are added in the
same order, and each `HandVal` comes from the same function on the same inputs.

- Cheap units per river: `players × n` evaluations, i.e. `players × n ×
  C(holes,2) × 10` lookups — 600,000 at PLO6 n = 2,000 against 348 million for
  the same river today.
- Visits per river: unchanged, `density × n²` (1.06 million at n = 2,000), each
  now a mask test, a strength comparison and a share, with no evaluation.
- Memory: one `int64_t` and one `mask_t` per combo per player, 16 B per combo,
  against a 16 B combo table already there.

## Design B — aggregate the blockers, drop the walk

Generalise the four-card sweep to five and six holes: sort each range by
strength, accumulate reach per nonempty card subset (31 subsets for five holes,
63 for six), and take each combo's compatible mass by inclusion-exclusion, then
read the showdown value off the strength-ordered prefix sums.

- Visits per river: **none**. The `n²` term disappears rather than getting
  cheaper.
- Expensive units per river: `63 × 2 × n` subset touches (252,000 at n = 2,000)
  plus two sorts of 2,000 entries, plus one mass lookup per combo.
- Exactness: exact in real arithmetic, **not bit-identical**. The
  inclusion-exclusion subtracts positives from a larger sum, and the summation
  order changes; the existing sweep documents a 1e-9 residue rule for the
  compatible mass, and its own header says results "otherwise agree with the
  pairwise oracle to floating-point rounding".

## What a visit costs, and what B would cost

The one quantity the profile cannot supply is the cost of a **visit with the
evaluation taken out**: the walk masks, compares, distributes and accumulates
around the evaluation, and the profile only ever sees the sum. Bracketing that
residual would be a guess dressed as a range, so it is measured first.

### The probe: freeze the evaluation

The walk, with its strength read from an array instead of from
`StdDeck_OmahaHi_EVAL`:

1. Before the clock starts, per river, fill `strength[player][combo]` with
   exactly the `HandVal` the evaluator returns for that combo on that board
   (`players × n` calls, 600,000 five-card lookups at PLO6 n = 2,000).
2. Run the deal walk with the callback reading that array. The mask test, the
   weight, `pe_pot_distribute`, the accumulation order and the reduction stay as
   they are.
3. Check the probe against the live path: the `values_fingerprint`, the deal
   mass and the river count must match bit for bit. If they differ, the probe is
   not measuring the same work and its number is void.
4. Report three figures from one process and one load condition:
   `probe.seconds.precompute`, `probe.seconds.walk` and the live `terminal`
   seconds. The per-visit cost is `walk / deals`, and `terminal − walk` is the
   evaluation share by measurement instead of by subtraction of a fitted
   constant.

One run gives both sides of the split, so a loaded machine slows both and the
ratio survives where absolute seconds would not.

This is implemented: `pe_omaha_deals_enumerate_indexed()` reports the combo each
player contributed, `pe_omaha_river_range_values_frozen()` and its turn
counterpart hoist the evaluation, and `--frozen-strength` prints the three
figures with `matches_live` beside them. The frozen path is Design A minus its
precompute, so it is also the correctness harness for that design.

### Measured: the probe, run

`--frozen-strength --repetitions 3` on the gate configuration, plus two
neighbours for the constant, board `AsKd7c2h`, load 3.3–3.9:

| Run | Live turn | Precompute | Walk | Frozen call | Per visit | Bit-identical |
|---|---|---|---|---|---|---|
| PLO6, n = 2,000 | 53.94 s | 0.11 s | 1.12 s | 1.23 s | 22 ns | yes |
| PLO6, n = 2,091 | 59.83 s | 0.11 s | 1.15 s | 1.26 s | 21 ns | yes |
| PLO5, n = 1,994 | 53.98 s | 0.08 s | 1.39 s | 1.47 s | 20 ns | yes |

`matches_live` is true in all three, and the PLO6 2,091 fingerprint is
`35f87d20638e3909`, the value master prints: the frozen walk is the walk, not a
lookalike. The per-visit cost comes out between **20 and 22 ns over the three
runs — a spread of 11 % about a mean of 20.9 ns** — and that band is what the
figure carries, not a fourth digit: each walk is the median of its three
repetitions in one process, so what the band shows is the run-to-run scatter of
the host as much as anything else. PLO5 and PLO6 agreeing to 11 % on the
per-visit cost, although they differ by 50 five-card lookups per evaluation, is
the consistency the model predicted.

So A costs **1.23 s per turn at n = 2,000 against 53.94 s, a factor of 44**, and
its two parts are the ones the model named: 0.11 s of precompute (168,000
evaluations, 25.2 million lookups at about 4 ns) and 1.12 s of visits. The
visit costs **22 ns**, the top of that band. Subtracting the fitted constant
would have put it near 7 ns, which is why the probe was worth building: the
15 ns the fit hid are the mask conversion to the deck representation and the
per-deal bookkeeping the live callback pays, per deal and per player, and that
a hoisted design would not.

| Path | Pair visits per turn | Five-card lookups per turn | Cost per turn | Against 53.9 s |
|---|---|---|---|---|
| Today, enumerated | 50,849,064 | 15.3 billion | 53.94 s (measured) | 1× |
| A, hoisted | 50,849,064 | 25.2 million | 1.23 s (measured) | 44× |
| B, aggregated | 0 | 0, 252,000 subset touches per river | bounded below | not quoted |

Against the gate, 22 ns sits 27 times under the 592 ns break-even, which is the
figure that number was for: `0.11 s + 50.9 million × c ≤ 30.2 s` holds for every
`c` below 592 ns, and the measurement lands far inside it. What remains in A is
the `n²` term itself — 50.8 million visits, now merely cheap — which is the
structural difference to B.

### Bounded, not projected, for B

B has no pair visits and no five-card lookups, so this model does not price it,
and inventing a cost for a subset touch would be the same mistake in a different
place. The only measured instance of the machinery is the four-card sweep the
PLO4 row already uses: 194,580 combos heads-up cost 10.13 s over 48 rivers, 211
ms per river — and that 211 ms is the whole path, its own precompute (2 ×
194,580 evaluations) and the subset accumulation over 15 nonempty subsets
together. Splitting the two would need the fitted per-lookup constant again, so
this bound does not split them. It takes the whole per-combo cost, 1.08 µs at
194,580 combos, applies it to 2,000 combos, and multiplies by 63/15 for the
wider subset table: about 9 ms per river, **roughly 0.45 s per turn, an order of
magnitude under even the 2× gate of 30.2 s**. That charges the precompute 4.2
times over, on purpose.

So it is a bound and not a projection: it does not rest on a split it cannot
measure, it over-counts one of its parts deliberately, and it is not a speedup
factor. No multiple is quoted for B until the same probe, with its subset table
frozen and its accumulator untouched, has measured it.

What holds without any timing is structural: B removes the `n²` term, A only
divides it.

## Scope

- Heads-up first, because that is the shape the existing sweep covers. Three or
  more players stay on the enumeration path until `pe_showdown_multiway_vector`
  gains the same aggregation; the measured 1,514 ns per deal at three-way PLO6
  is what such a design would have to beat, and it is out of this tranche.
- No Hi/Lo in either design: `StdDeck_OmahaHiLow8_EVAL` returns a high and a low
  value, so an aggregation over strengths would need a second dimension.
- No change to chance weights, to the filter or to the turn's reduction.

## The gate

The wording in #332 is "at least 2x fewer tuple visits at n = 2000". That
wording is not attainable by any exact design and is restated here: the visit
count is `density × n²` per river whatever the implementation, because every
legal pair carries its own weight and contributes a distinct award. Design A
keeps the visits and divides their cost; Design B removes the visits at the
price of a different summation. So:

0. **Probe first.** `--holes 6 --combos 2000 --frozen-strength
   --repetitions 3` reports the frozen walk, the precompute and the live
   terminal in one process, with the fingerprint of the frozen run bit-identical
   to the live one. It is implemented and its output is in the table above; a
   design is not judged on speed before that run.
1. **Speed.** At `--holes 6 --combos 2000 --repetitions 3`, the terminal phase
   must be at most **half** of its value before the change, on the same host and
   in the same load band. Measured, A's visit is 20–22 ns against the 592 ns
   break-even — 27 times under it at the top of the band — and its turn is 44×
   faster, so the floor is cleared by more than an order of magnitude; B still
   has to show its own.
2. **Exactness, A.** Bit-identical: the `values_fingerprint`, the deal mass and
   the river count must match the pre-change run exactly, and
   `test_plo4_turn_oracle` must stay green. A change that moves any fingerprint
   is rejected, and that includes a change of summation order.
3. **Exactness, B.** Agreement with A to 1e-9 of the accumulated reach on every
   measured configuration, plus an explicit label in the report. B cannot
   replace the certified path without an accepted fingerprint change, so it
   ships behind that decision or not at all.
4. **No regression on PLO4.** At 194,580 combos heads-up the run must still
   report `river_reference.path` as `plo4_sweep`; the sweep is the certified
   path and is not to be displaced by a new one.
5. **Determinism.** `--repetitions 3` must report
   `deterministic_across_repetitions: true` and identical profile counters in
   every repetition, as it does today.

## Stop conditions

- If the probe puts A's per-visit cost above 592 ns, the gate fails: the `n²`
  term is then in the distribution and the accumulation rather than in the
  evaluation, and B becomes the only path worth pursuing.
- If B's residue exceeds 1e-9 of the accumulated reach on any configuration, B
  does not ship as a default.
- If either design cannot keep PLO4 on `plo4_sweep`, stop and bring the numbers
  back before touching the shared kernel.

## Open question for the maintainer

Is a fingerprint change acceptable for the aggregate path, given that B is exact
in real arithmetic and differs only in rounding? If not, B stays a diagnostic
path and only A ships, which still clears the gate but leaves the `n²` term
standing.
