# The turn's pair walk: two designs for the terminal's tuple cost

Issue #285 phase B, follow-up #332. Design A is implemented and measured below;
design B is not. The frozen-strength probe the issue produced as its first
increment stays: it splits the terminal's walk into its precompute and its
visits, which is the unit the gate is stated in. The unit costs the designs
build on come from the phase profile of #331; every figure below is either
measured here or labelled as arithmetic on a measurement.

## What the profile settled

A turn call spends 99.99 % of its time in the river terminal: the per-river
filter is 0.0004 % of a PLO5 or PLO6 call and 0.16 % of a PLO4 one. So the
target is the terminal's tuple walk, and any work on the filter would buy a
thousandth of the call.

Measured per-deal cost **before design A**, board `AsKd7c2h`, heads-up unless
stated:

| Variant | Combos | Turn | Per deal | Deals per river |
|---|---|---|---|---|
| PLO5 | 1,994 | 52.68 s | 750 ns | 1,462,881 |
| PLO6 | 854 | 9.77 s | 1,052 ns | 193,442 |
| PLO6 | 1,600 | 35.45 s | 1,088 ns | 678,980 |
| PLO6 | 2,091 | 60.44 s | 1,087 ns | 1,158,104 |
| PLO6, 3 players | 150 | 7.56 s | 1,514 ns | 104,079 |

## What the walk did before design A

`src/solver/domain/omaha_river.c` enumerated the legal deals and called its deal
callback once per deal; the callback ran `StdDeck_OmahaHi_EVAL` for **every
player on every deal**. One Omaha evaluation over a private hand and a
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
residual the model carries. Design A below replaces the repeated evaluation and
leaves the rest of that description as it stands.

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
  C(holes,2) × 10` lookups — 600,000 at PLO6 n = 2,000 against the 348
  million the same river cost before A.
- Visits per river: unchanged, `density × n²` (1.06 million at n = 2,000), each
  now a mask test, a strength comparison and a share, with no evaluation.
- Memory: one `int64_t` per combo per player, 16 B for Hi/Lo's second table,
  against a 16 B combo table already there.

Implemented in `src/solver/domain/omaha_river.c` as `omaha_hoisted_walk()`: each
range is evaluated once into an `int64_t` table, Hi/Lo's low into a second, and
the indexed enumeration reads them back. Every enumeration path takes it, Hi/Lo
included; the heads-up four-card vector sweep already hoisted its evaluation
and keeps its own kernel. The frozen entry point stayed as the timed twin of
the same walk.

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

### Measured: design B

B was implemented as the wide inclusion-exclusion sweep of
`showdown_vector.c` (`pe_showdown_wide_mass`), reachable from the river
terminal only through `pe_omaha_river_set_sparse_blockers()` and from the
benchmark through `--sparse-blockers`; the deal walk stays the default. On the
gate board, heads-up high-only, `--repetitions 3`: PLO6 n = 2,000 goes from
1.112-1.121 s to 0.350-0.368 s (3.11x), PLO6 n = 2,091 from 1.213-1.227 s to
0.384-0.391 s (3.15x), PLO5 n = 1,994 from 1.346-1.363 s to 0.191-0.192 s
(7.08x), with no pair visits in any of them. The `values_fingerprint` is
identical on those runs because the benchmark's weights are dyadic and every
sum is exact; with weights in thirds the residue is 5.4e-13 on the compatible
mass and about 5e-13 on the values. The full table, the subset-touch count and
the tail bug the first version had are in
[plo_turn_scalability.md](plo_turn_scalability.md#sparse-blocker-aggregation-the-sweep-at-plo5-and-plo6).

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

On Hi/Lo the evaluator returns both halves from one call, so the terminal fills
both tables from that call. The probe fills them in two passes instead -- one
`StdDeck_OmahaHi_EVAL` for the high table, one `StdDeck_OmahaHiLow8_EVAL` for
the low -- and times the second apart: `low_precompute_seconds` is what asking
for the low table costs on top of the high one, while the terminal's own
precompute is the combined call, which in practice is the high pass because the
evaluator computes the low half anyway. `--hilo` selects the Hi/Lo payoff; the
chance expansion, the per-river filter and the reduction stay the high-only
ones, so the two runs differ only in the payoff and in that extra pass.

This is implemented: `pe_omaha_deals_enumerate_indexed()` reports the combo each
player contributed, and `pe_omaha_river_range_values_frozen()` and its turn
counterpart call the same walk with the precompute and the visits timed apart,
printing them with `matches_live` beside them. Hi/Lo has its own pair of
entries, `pe_omaha_hilo8_river_range_values_frozen()` and
`pe_omaha_hilo8_turn_range_values()`, which share the walk and the turn and add
the low table's pass. Design A is that walk in the
terminal itself, so the probe now decomposes the terminal rather than standing
in for it; `matches_live` therefore compares the terminal with a clocked copy of
itself, and the independent check is `test_omaha_river_hoisted`, which prices
the same river deal by deal with its own payoff, its own mass sum and
hand-declared side pots, one of them three-way, and compares the values, the
deal count and the mass bit for bit.  The side-pot case also carries values
counted by hand -- the short stack wins only the main pot -- so that case is
pinned rather than merely agreed with the terminal.

### Measured: design A, before and after

`--frozen-strength --profile --repetitions 3` on the gate configuration, plus
two neighbours for the constant, board `AsKd7c2h`, load 2.9–4.1:

| Run | Before A | After A | Precompute | Walk | Per visit | Bit-identical |
|---|---|---|---|---|---|---|
| PLO6, n = 2,000 | 53.94 s | 1.13 s | 0.10 s | 1.03 s | 20.2 ns | yes |
| PLO6, n = 2,091 | 59.83 s | 1.22 s | 0.11 s | 1.11 s | 20.0 ns | yes |
| PLO5, n = 1,994 | 53.98 s | 1.37 s | 0.07 s | 1.30 s | 18.5 ns | yes |

The three fingerprints (`ac3016522c620075`, `35f87d20638e3909` and
`89808ba1d1d56085`, the middle one the value master prints), the deal masses
(19,821,384.246093750, 21,703,585.209960938 and 27,371,295.908203125), the
48 priced rivers and the deal counts (50,849,064, 55,589,004 and 70,218,300)
are all exactly those of the pre-change runs: the walk visits the same deals,
with the same weights, in the same order, and only the evaluations moved.
`matches_live` is true in all three.

Hi/Lo was first checked against the binary of the commit before A, and that run
is now a fixture in `test_omaha_hilo_river`: a 400-combo PLO6 Hi/Lo river on
board `2c4d7hJsQc`, weights in quarters, prints `0.58949573829817625` and
`-0.58949573829817625` for the two players, a deal mass of `26251.25`, `67134`
deals and the fingerprint `cd22f660d31fbdd3` from both binaries, byte for byte.

### Measured: Hi/Lo, with `--hilo`

The same probe on the Hi/Lo payoff, same board, load 2.9–4.1, `--hilo
--profile --frozen-strength --repetitions 3`:

| Run | Terminal | High pass | Low pass | Walk | Per visit |
|---|---|---|---|---|---|
| PLO6, n = 2,000 | 1.38 s | 0.10 s | 0.11 s | 1.27 s | 24.9 ns |
| PLO5, n = 1,994 | 1.75 s | 0.08 s | 0.08 s | 1.67 s | 23.8 ns |

The deal count, the deal mass and the 48 rivers are those of the high-only runs
above (50,849,064 and 19,821,384.246093750 at PLO6, 70,218,300 and
27,371,295.908203125 at PLO5): the split moves the values, not what the walk
visits. `matches_live` is true on both, the values are `0.072183988416` and
`0.044791908311` for player 0 (fingerprints `3f735577efef69fb` and
`c6be68645969f6d3`), and they have to differ from the high-only ones, which is
what `test_plo4_turn_oracle` checks on a fixture whose hero always has a low.

A Hi/Lo turn walks the tuples at every range size -- the four-card sweep is a
high-only path -- so `--hilo` reports the tuple fields wherever the walk itself
is affordable: `legal_pairs`, `pair_density` and `deals_total` are measured
while the square of the range stays under 4e7, `legal_pairs_measured` says
whether the document carries them, and `rivers_per_pair` is reported whenever
the walk runs.  The count is a cross-check of the walk as well as a density: at
PLO4 heads-up n = 40 `deals_total` is 38,080, which is exactly the profile's
`deals`, and at PLO6 n = 2,000 both read 50,849,064.

The low pass costs what the high pass costs, 0.11 s against 0.10 s, because
`StdDeck_OmahaHiLow8_EVAL` computes both halves whether or not the caller keeps
the low. That is also why the terminal can afford both tables from one call,
and why the probe's two passes bracket the terminal's precompute rather than
adding to it. The visit is 4.7 ns more expensive than high-only (24.9 against
20.2 ns): the low comparison and the split are in the walk, and A did not move
them.

The per-visit cost lands between **18.5 and 20.2 ns across the three runs —
9 % about a mean of 19.6 ns** — and that band is what the figure carries, not
a fourth digit: each walk is the median of its three repetitions in one
process, so what the band shows is the run-to-run scatter of the host as much
as anything else. PLO5 and PLO6 agreeing to 9 % on the per-visit cost,
although they differ by 50 five-card lookups per evaluation, is the
consistency the model predicted.

So A turns **53.94 s into 1.13 s at n = 2,000, a factor of 47.6**, and its two
parts are the ones the model named: 0.10 s of precompute (168,000 evaluations,
25.2 million lookups at about 3.9 ns) and 1.03 s of visits. Subtracting the
fitted constant would have put the visit near 7 ns, which is why the probe was
worth building: the 13 ns the fit hid are the mask conversion to the deck
representation and the per-deal bookkeeping the per-deal callback paid, per deal
and per player, and that the hoisted walk does not.

| Path | Pair visits per turn | Five-card lookups per turn | Cost per turn | Against 53.9 s |
|---|---|---|---|---|
| Before A, evaluating per deal | 50,849,064 | 15.3 billion | 53.94 s (measured) | 1× |
| A, hoisted (shipped) | 50,849,064 | 25.2 million | 1.13 s (measured) | 47.6× |
| B, aggregated | 0 | 0, 252,000 subset touches per river | bounded below | not quoted |

Against the gate, the measured visit sits 29 times under the 592 ns break-even:
0.10 s of precompute plus 50.9 million visits at 20 ns is 1.13 s, against the
30.2 s a 2× cut of the 60.44 s run allows, and the same sum would still fit at
592 ns per visit. What remains in A is the `n²` term itself — 50.8 million
visits, now merely cheap — which is the structural difference to B.

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
- No Hi/Lo in B: `StdDeck_OmahaHiLow8_EVAL` returns a high and a low value, so
  an aggregation over strengths would need a second dimension. A hoists both,
  because one call returns both.
- No change to chance weights, to the filter or to the turn's reduction.

## The gate

The wording in #332 is "at least 2x fewer tuple visits at n = 2000". That
wording is not attainable by any exact design and is restated here: the visit
count is `density × n²` per river whatever the implementation, because every
legal pair carries its own weight and contributes a distinct award. Design A
keeps the visits and divides their cost; Design B removes the visits at the
price of a different summation. So:

0. **Probe first.** `--holes 6 --combos 2000 --frozen-strength
   --repetitions 3` reports the precompute, the walk and the live terminal in
   one process, with `matches_live` true; `--hilo` adds the low table's pass and
   runs the same check on the Hi/Lo payoff. It ran before A was written, and
   every pre-change second in the table above is its output.
1. **Speed.** At `--holes 6 --combos 2000 --repetitions 3`, the terminal phase
   must be at most **half** of its value before the change, on the same host and
   in the same load band. Measured after A: 1.13 s against 53.94 s, 24 times
   under the 26.97 s half-way line, with the visit at 18.5–20.2 ns against the
   592 ns break-even; B still has to show its own.
2. **Exactness, A.** Bit-identical: the `values_fingerprint`, the deal mass and
   the river count must match the pre-change run exactly, and
   `test_plo4_turn_oracle` must stay green. Met on all three runs above, with
   the PLO4 river, Hi/Lo river, deal-enumeration and hoisted-walk tests green.
   A change that moves any fingerprint is rejected, and that includes a change
   of summation order.
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
  evaluation, and B becomes the only path worth pursuing. The measurement put
  it at 18.5–20.2 ns, so this did not fire.
- If B's residue exceeds 1e-9 of the accumulated reach on any configuration, B
  does not ship as a default.
- If either design cannot keep PLO4 on `plo4_sweep`, stop and bring the numbers
  back before touching the shared kernel.

## Open question for the maintainer

Is a fingerprint change acceptable for the aggregate path, given that B is exact
in real arithmetic and differs only in rounding? If not, B stays a diagnostic
path and only A ships, which still clears the gate but leaves the `n²` term
standing.
