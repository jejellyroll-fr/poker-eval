# PLO4 River Exploitability Certificate

Issue #283, first tranche. API: `include/poker_eval/solver/pe_plo4_certification.h`.
Tool: `tools/pe_plo4_certify.c`. Test: `tests/test_plo4_certification.c`
(independent oracle). Nothing in this workflow is sampled.

`pe-plo4-certify` measures one fixed heads-up PLO4 river strategy against an
information-set-consistent best response for both players and prints a single
JSON document on stdout. The document either claims an exact certificate or
says why it cannot.

## Running it

```bash
cmake --build build --target pe-plo4-certify --parallel
./build/tools/pe-plo4-certify --combos 64 --strategy mixed --weighted
```

| Flag | Meaning | Default |
|---|---|---|
| `--board TEXT` | five-card board, e.g. `Ks9d4c2hTs` | `Ks9d4c2hTs` |
| `--pot CHIPS` | pot at the root, including prior bets | `63` |
| `--invested CHIPS` | each player's prior commitment, included in the pot | `31.5` |
| `--stack CHIPS` | chips behind for each player | `40` |
| `--bet-size CHIPS` | the single bet size offered at each decision | the smaller stack |
| `--big-blind CHIPS` | unit for the bb/mbb conversions | `2` |
| `--combos N` | four-card hands per range (max 4096) | `64` |
| `--max-iterations N` | BR fixed-point budget | `64` |
| `--strategy uniform\|mixed` | uniform, or a deterministic per-combo mix | `uniform` |
| `--weighted` | give every fifth generated combo a weight of 0.25 | off |

The tool generates both ranges deterministically from the board (the same
combo list for each player) and evaluates them with a deterministic strategy,
so two runs with the same flags produce the same document. Exit status is 0
when the measurement ran, 1 on evaluation failure and 2 on bad arguments. A
`not_certified` result still exits 0: the label, not the exit code, carries
the claim.

## What it measures

The declared game is a single river betting tree: check/check, check/bet with
fold or call, bet with fold or call, one fixed bet size, no rake. Private hands
are drawn from the declared ranges: both ranges may carry per-combo weights,
the legal ordered pairs are normalised by their total mass (`joint_mass`), and
blocked pairs are excluded. Terminal values are conditional on the hero's own
combo and net of that player's investment.

- `policy_value` is the EV of the fixed strategy, chips per hand under the
  joint range distribution.
- `br_value` is the value of the best response for that player against the
  opponent's fixed strategy.
- `br_gap` is `br_value - policy_value`, clamped at zero against round-off.
- `nashconv` is the sum of the two gaps.
- `exploitability` is `nashconv / 2`, the conventional heads-up zero-sum
  measure. `percent_of_reference_pot` divides it by the root pot.

The result is `certified` only when the declared tree was enumerated
completely and both best responses converged. `not_certified` reports the
reason (`not_converged`, `incomplete`, `invalid_game`, or `sampled`, reserved
for callers that add sampling later). A rejected input returns an error
instead of a document.

## Schema `pe-plo4-certification/v1`

| Field | Type | Meaning |
|---|---|---|
| `schema` | string | `pe-plo4-certification/v1` |
| `generated_by` | string | `pe-plo4-certify` |
| `issue` | number | `283` |
| `board` | string | board in rank order, e.g. `2h4c9dTsKs` |
| `game_scope` | string | the exact tree, rake policy and range scope the claim applies to |
| `mode` | `"exact"` \| `"not_certified"` | whether the full declared tree was measured |
| `guarantee` | `"nash"` \| `"none"` | `nash` only for a certified two-player zero-sum measurement |
| `verdict` | `"certified"` \| `"not_certified"` | the claim |
| `reason` | string | `none` when certified, otherwise why not |
| `converged` | boolean | both best responses converged |
| `sampled` | boolean | always `false` in this version |
| `showdown_path` | string | `plo4`, `pairwise`, `sorted`, `multiway` or `enumeration` |
| `rake` | number | `0`; the declared game has no rake |
| `pot_reference` | number | root pot used for the percentage |
| `big_blind` | number | unit for bb/mbb |
| `combos.declared` | `[n0, n1]` | live hands declared per range |
| `combos.width` | number | padded combo width the vector lane measured |
| `combos.live` | `[n0, n1]` | hands with a non-zero weight |
| `combos.joint_mass` | number | total weight of the legal ordered pairs |
| `br_iterations` | `[n0, n1]` | fixed-point iterations used per best response |
| `policy_value` | object | per player: `chips`, `bb`, `mbb_per_game` arrays |
| `br_value` | object | same units for the best-response values |
| `br_gap` | object | same units for the unilateral gaps |
| `nashconv` | object | `chips`, `bb`, `mbb_per_game` scalars |
| `exploitability` | object | `chips`, `bb`, `mbb_per_game`, `percent_of_reference_pot` |
| `input` | object | pot, investments, stacks, bet size, strategy mode, weighted flag, iteration budget |

## Certified example

Command:

```bash
./build/tools/pe-plo4-certify --combos 32 --strategy mixed --weighted \
    --max-iterations 32
```

Output on board `Ks9d4c2hTs`:

```json
{
  "schema": "pe-plo4-certification/v1",
  "generated_by": "pe-plo4-certify",
  "issue": 283,
  "board": "2h4c9dTsKs",
  "game_scope": "heads-up PLO4 river, one fixed bet size, no rake, declared private ranges",
  "mode": "exact",
  "guarantee": "nash",
  "verdict": "certified",
  "reason": "none",
  "converged": true,
  "sampled": false,
  "showdown_path": "plo4",
  "rake": 0,
  "pot_reference": 63,
  "big_blind": 2,
  "combos": {"declared": [32, 32], "width": 32, "live": [32, 32], "joint_mass": 521},
  "br_iterations": [3, 2],
  "policy_value": {"chips": [3.2988006482370089, -3.2988006482370142], "bb": [1.6494003241185045, -1.6494003241185071], "mbb_per_game": [1649.4003241185044, -1649.4003241185071]},
  "br_value": {"chips": [14.522466810620601, 12.895166113510335], "bb": [7.2612334053103007, 6.4475830567551675], "mbb_per_game": [7261.2334053103004, 6447.5830567551675]},
  "br_gap": {"chips": [11.223666162383592, 16.193966761747348], "bb": [5.6118330811917962, 8.0969833808736738], "mbb_per_game": [5611.8330811917958, 8096.9833808736739]},
  "nashconv": {"chips": 27.417632924130942, "bb": 13.708816462065471, "mbb_per_game": 13708.816462065472},
  "exploitability": {"chips": 13.708816462065471, "bb": 6.8544082310327354, "mbb_per_game": 6854.4082310327358, "percent_of_reference_pot": 21.760026130262652},
  "input": {"pot": 63, "invested": [31.5, 31.5], "stacks": [40, 40], "bet_size": 40, "strategy": "mixed", "weighted": true, "max_iterations": 32}
}
```

The policy values are opposite because both players follow the declared
strategy on the same zero-sum tree. The best-response values are not opposite:
each is a separate maximum against the same policy, and their sum minus the
policy values is the NashConv.

## Not-certified example

The same command with `--max-iterations 1`:

```json
  "mode": "not_certified",
  "guarantee": "none",
  "verdict": "not_certified",
  "reason": "not_converged",
  "converged": false,
  "sampled": false,
```

The financial fields are still emitted with the same units; they are simply
not certified. An invalid board, a bet larger than a stack, an unnormalised
strategy row or a range with no live weight makes the tool exit with status 2
and no document.

## Scope

This version measures one fixed bet size on one street, with no rake, and
accepts at most 4096 generated combos. The full 178,365-combo range under
explicit memory and time budgets is tranche 2 of the issue. The API adds an
optional `combo_prior` callback to `pe_vector_game_t`, so a weighted range is
measured with the same root prior in the policy and the best response.
