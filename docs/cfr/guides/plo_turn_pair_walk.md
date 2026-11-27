# The turn's pair walk: two designs for the terminal's tuple cost

Issue #285 phase B, follow-up #332. This is a **specification**: neither design
is implemented, and none of the projections below is a measurement. The unit
costs they build on are measured, in the phase profile of #331; every figure
under "expected cost" is arithmetic on those units and is labelled as such.

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

## Expected cost

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
ratio survives where absolute seconds would not. A `--frozen-strength` flag on
`pe-omaha-turn-bench` reporting those three fields is the whole first increment,
and it doubles as the correctness harness for Design A, of which it is the code
path minus the precompute.

### The arithmetic, with the unknown left as a symbol

At PLO6 n = 2,000 heads-up, 48 rivers, 1.06 million visits per river and 3.6 ns
per five-card lookup:

- precompute: `48 × 600,000` lookups × 3.6 ns ≈ **0.10 s**
- visits: `48 × 1.06 million × c`, with `c` the probe's per-visit cost
- today: **60.4 s** measured, from 48 × 347.4 million lookups

So A costs `0.10 s + 50.9 million × c`, and the 2× gate of 30.2 s holds for
every `c` below **592 ns**. That is the only bound the note needs: a visit today
costs 1,087 ns end to end, and the constant above was fitted on the assumption
that the evaluation is the whole deal, so it can neither confirm nor refute a
residual — that circularity is precisely what the probe breaks. The gate is
insensitive to the answer: it holds for any visit costing less than 54 % of
today's 1,087 ns deal, and the walk it measures has dropped 300 five-card
lookups from that deal.

| Path | Pair visits per river | Five-card lookups per river | Cost per turn |
|---|---|---|---|
| Today, enumerated | 1,158,104 | 347,400,000 | 60.4 s (measured) |
| A, hoisted | 1,060,000 | 600,000 | `0.10 s + 50.9M × c`, probe pending |
| B, aggregated | 0 | 0, 252,000 subset touches instead | bounded, see below |

### Bounded, not projected, for B

B has no pair visits and no five-card lookups, so this model does not price it,
and inventing a cost for a subset touch would be the same mistake in a different
place. The only measured instance of the machinery is the four-card sweep the
PLO4 row already uses: 194,580 combos heads-up cost 10.13 s over 48 rivers, 211
ms per river, and that figure contains its own precompute as well as the subset
accumulation over 15 nonempty subsets. Scaling the sweep part to 63 subsets and
2,000 combos puts B in the region of 5 ms per river, so **about 0.3–0.4 s per
turn, an order of magnitude under even the 2× gate of 30.2 s** — an
extrapolation across two axes and a different variant, not a projection. That
is a bound on the time and deliberately not a speedup factor: no multiple is
quoted for B until the same probe, with its subset table frozen and its
accumulator untouched, has measured it.

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
   --repetitions 3` must report the frozen walk, the precompute and the live
   terminal in one process, with the fingerprint of the frozen run bit-identical
   to the live one. Until that number exists, no design is judged on speed and
   no multiple is quoted for either.
1. **Speed.** At `--holes 6 --combos 2000 --profile --repetitions 3`, the
   terminal phase must be at most **half** of its value before the change, on
   the same host and in the same load band. For A that means a measured `c`
   below the 592 ns break-even; 2× is a floor, not an aspiration.
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
