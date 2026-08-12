# Hand Abstraction via Clustering (FEAT-04)

River CFR adapters previously abstracted hands by plain strength thresholding
(`--bucket-mode 0..3`): a hand class plus a linear cut of the `eval_t` offset.
That partition ignores how a hand actually plays against the opponent's range.
FEAT-04 replaces it with a **learned** abstraction: k-means clustering over a
per-hand feature vector, trained once and reused across solves.

## When to use it

| Mode | Mechanism | Use case |
|------|-----------|----------|
| 0 | action only | debug / no abstraction |
| 1 | + board class | board texture bucketing |
| 2 | + private class | coarse strength |
| 3 | + linear strength bin | legacy, weak |
| **4** | **k-means on hand features** | **the learned abstraction (FEAT-04)** |

Use mode 4 when you want a strategically meaningful, bounded number of private
buckets that track equity rather than nominal hand strength. It composes with
FEAT-02 board isomorphism: the clustering runs on the **canonicalized** board,
so two isomorphic boards share one bucket table.

## Feature vector

Every hand on a board is mapped to:

```
[ E[HS^2] , equity-distribution histogram (n_bins, default 8) ]
```

- `HS^2` (scalar) = expectation of squared hand strength over all opponent
  hands on the board (1.0 win, 0.5 chop, 0.0 loss). This is the classic OCHS
  scalar feature.
- The histogram is `n_bins` equal-width bins over `[0, 1]` strength. It is what
  separates polarized hands from merely-average ones, which `E[HS^2]` alone
  conflates.

For 2-card hole games the opponent hands are enumerated exhaustively
(`C(47,2) = 1081` rollouts on a river). For 4-card (Omaha) games the opponent
hands are sampled deterministically (seeded) when the combination count is
large.

## Training

```c
pe_hand_cluster_opts_t opts = {0};   /* all defaults */
opts.hole_cards = 2;                 /* or 4 for Omaha */
opts.seed       = 12345;             /* determinism */
opts.n_bins     = 8;

pe_bucket_table_t *table =
    pe_bucket_table_train_all(ctx, board, /*k=*/8, &opts);
```

`pe_bucket_table_train_all` covers every legal hole-card combination on the
board. For Hold'em (2-card holes) the opponent table is built **once** and
reused for every hand, so cost is `O(hands + opponents)`. For Omaha (4-card
holes) the opponent hands are rolled out per hero hand with two **independent**
budgets to keep cost bounded:

- `max_samples` — number of hero hands sampled (Omaha) / exhaustive for Hold'em.
- `opp_samples` — number of opponent hands rolled out *per* hero hand (Omaha
  only). This is separate from `max_samples` so the two dimensions do not
  multiply into an impractical cost. Total Omaha cost is
  `O(max_samples × opp_samples)`.

Centroids are seeded with k-means++ and refined with Lloyd iterations. Before
clustering, identical feature vectors are collapsed and `k` is clamped to the
number of distinct points, so every returned cluster is non-empty and the
table uses exactly the requested (or fewer) buckets. Finally clusters are
sorted by ascending mean equity, so bucket 0 is always the weakest and bucket
`k-1` the strongest — making bucket ids comparable across tables and readable
in strategy dumps.

## Serialization

A table is a `.pe_bkt` file (`PEBKT001` magic, version word, little-endian
fixed-width fields — same conventions as FEAT-09's `.pe_sol`/`.pe_tree`):

```c
pe_bucket_table_save(table, "river_ak742.pe_bkt");
pe_bucket_table_t *loaded = pe_bucket_table_load("river_ak742.pe_bkt");
```

A table is board-specific; `pe_bucket_table_load` rejects magic/version
mismatches and truncated payloads. Loading fails (returns NULL) if the board
does not match the current deal — the solver then retrains.

## Wiring into a solve

In the river adapters set `bucket_mode = 4` and point `bucket_table` at a
loaded (or freshly trained) table. The bucket id replaces the private-class /
coarse-strength bits in the 64-bit infoset key (8 bits at `<<48`, so `k` up to
256). A NULL table falls back to the mode-3 abstraction rather than collapsing
the tree.

Assignment validates the table against the current hand: it rejects a table
trained for a different board, a different hole-card count, or a hand that
overlaps the board, returning `-1` so the adapter falls back to the mode-3
abstraction instead of producing an invalid infoset key.

### `bench_cfr_omaha_river` flags

| Flag | Meaning |
|------|---------|
| `--bucket-mode 4` | enable the learned abstraction |
| `--cluster-k N` | number of buckets |
| `--cluster-bins N` | histogram bins (default 8) |
| `--cluster-seed N` | RNG seed for training/sampling |
| `--cluster-samples N` | cap on Omaha **hero-hand** sampling (`max_samples`) |
| `--cluster-opp-samples N` | per-hero opponent rollout budget (`opp_samples`) |
| `--bucket-table FILE` | load a prebuilt `.pe_bkt` |
| `--bucket-table-out FILE` | save the trained `.pe_bkt` (deal 0) |

Example:

```sh
bench_cfr_omaha_river --deals 200 --iters 5000 \
    --bucket-mode 4 --cluster-k 8 --bucket-table-out river.pe_bkt
# later, reuse across runs:
bench_cfr_omaha_river --deals 200 --iters 5000 \
    --bucket-mode 4 --bucket-table river.pe_bkt
```

## Determinism

k-means runs on an internal PCG32 seeded from `opts.seed`, so a given seed
always yields the same table. The CI relies on this (it runs the solver paths
under deterministic sanitizer/valgrind jobs).

## Caveats

- The abstraction is **approximate** (FEAT-02 is the lossless one) — expect a
  small EV gap versus the un-abstracted solve, traded off against a much smaller
  infoset count.
- Omaha features use deterministic sampling rather than exhaustive rollout, so
  the feature vector is a Monte-Carlo estimate; raise `--cluster-opp-samples`
  for stability at the cost of training time, and `--cluster-samples` to widen
  the hero-hand coverage.
