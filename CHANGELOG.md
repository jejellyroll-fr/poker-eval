# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed
- **Behaviour change.** Discounted CFR now discounts the cumulative regret
  once per iteration instead of once per infoset visit (EXT-07). The rule is
  `R_t = R_(t-1) * d(t) + r_t`, as in Brown & Sandholm; the previous code
  passed `d` to every per-node regret update, and a poker infoset is reached
  from many states in a single iteration, so it was scaled `d^N` — a discount
  exponentially stronger than the algorithm calls for, and one that varied
  with the shape of the tree rather than with the iteration.

  Solves with `cfr_config_t::enable_dcfr` set produce different numbers from
  this release on. Everything else is bit-identical: vanilla, linear
  averaging, ECFR and node-locked runs are unaffected, since their discount
  was already 1.0.

  The corrected discount is gentler, so a run may need more iterations to
  reach the same exploitability. `test_kuhn_multiway_openspiel` had its
  budget raised from 250 to 2000 iterations for that reason; its assertion is
  unchanged.

  `cfr_storage_scale_regrets()` is the new primitive that applies it.

### Added
- UniversalDeck delegation to the generalized deck layer (ISSUE-03 phase 2,
  #203): the internal implementation of `Universal_*` (mask ops, string
  conversions) now delegates to `pe_deck_spec_t` / `pe_card_mask_t` from
  `generalized_deck` under `USE_INT64`. The `UniversalCardMask` union gained a
  `pe_card_mask_t cards` alias of the same 64 bits, so standard/joker masks and
  the generalized mask share the dense bit layout and the delegation is exact.
  The public API, `ConvertStdToJoker` / `ConvertJokerToStd` semantics and the
  non-`USE_INT64` per-type branches are unchanged, keeping 100% backward
  compatibility; `Universal_*` is not on the 52-card hot path, so evaluation
  performance is unaffected. New `test_universal_deck` delegation tests assert
  `Universal_*` agrees bit-for-bit with `pe_deck_*`.

- Generalized deck specification abstraction (ISSUE-03, #159): a new additive
  module `src/core/generalized_deck.c` + `include/poker_eval/deck/generalized_deck.h`
  describes any deck of up to 64 cards through a dynamic `pe_deck_spec_t`
  descriptor (num_cards, num_ranks, num_suits, active_rank_mask 2..A, num_jokers,
  name) and a 64-bit `pe_card_mask_t`. It ships 7 predefined presets
  (`royal_20`, `spanish_32`, `short_36`, `std_52`, `joker_53`, `joker_54`,
  `california_60`) plus `pe_deck_create_custom`, and bitmask operations
  (set/unset/is_set/count, full-mask) and card/mask string conversions that
  stay exact for any deck size in [20..64]. The existing StdDeck/JokerDeck/
  UniversalDeck APIs are untouched and remain 100% backward compatible; a
  Unity test (`test_generalized_deck`) validates presets, round-tripping and
  the 64-card edge case.

- Analytical Game Benchmark Suite & External Oracles (ISSUE-09, #165): a
  self-contained mathematical qualification layer for the CFR core lives in
  `tests/game_theory/`. It solves small analytically-characterized games and
  checks the solver against independent oracles implemented without any pokenum
  code: Chen & Ankenman AKQ / first-action Kuhn closed-form equilibria (first
  player value exactly -1/18), a self-contained dense two-phase simplex exact
  matrix-game LP (seqform_lp_solve), and a from-scratch full-tree enumeration.
  New CTest binaries (label `game_theory`) `test_akq_game`, `test_kuhn_openspiel`,
  `test_leduc_openspiel` and `test_gambit_exact_lp` solve each game as a
  `cfr_game_t` vtable and assert (a) the converged policy value equals an
  independent full-tree enumeration, (b) it matches the exact analytical/LP
  oracle (AKQ/Kuhn → -1/18 ± 2e-3; Gambit 2×2 → 0.2 ± 1e-4), and (c) the
  zero-sum mirror (P2 value = -P1 value). The exact-LP oracle was hardened to
  handle negative-valued games by internally shifting the payoff matrix. The
  suite documents that `cfr_solve` returns the perfect-information best-response
  exploitability (a `double`, not an error code), which is an upper bound that
  stays positive at equilibrium for imperfect-information games, so the tests
  gate on policy-value agreement rather than exploitability.

- Folded-range card bunching effect estimator (FEAT-14, #150): when players
  fold preflop their unknown cards are statistically removed from the stub
  deck, biasing the distribution of the turn/river runout. A new optional
  chance-deal weighting hook (`cfr_game_t::get_chance_weight`, default NULL =
  uniform 1.0) lets a game steer the solver's chance-node distribution; the
  solver (`cfr_traverse_recursive`, `best_response_recursive(_multiway)`,
  `policy_value_recursive`, CFV walk in `cfr_resolve.c`) normalizes the
  per-outcome weights to sum to 1 at every chance node. The multiway
  postflop adapter implements the estimator: each preflop-folded player with
  a provided range distribution (`mpf_config_t::folded_range_provided` /
  `folded_range_prob[player][card]`, e.g. from a preflop raise/fold model)
  and an unspecified hole contributes a per-card survival factor
  `(1 - f_p(c))`, and the deal weight of card `c` is its survival normalized
  over all currently unseen cards (`mpf_bunching_compute_survival` exposes
  the same factors from a config). Players with a fully-specified hole keep
  their deterministic removal and are excluded from the range factor;
  disabling the estimator (`enable_card_bunching = 0`) recovers uniform
  deals exactly. The solver unit tests get `test_cfr_bunching.c` (weight
  unit test vs the analytic survival formula, best-response vs an
  independent manual weighted expectation, and a 2-folded-players bias/EV
  shift/zero-sum check). `bench_cfr_holdem_multi` gains `--bunching`,
  `--chance` and `--fold-range "P1:AA,KK|P2:QQ,JJ"` options (folded seats
  marked with `--preflop-active 0`) to smoke-test the estimator end to end.
- Sparse state indexer for multiway asymmetrical stacks (FEAT-10, #146):
  a hash-mapped sparse table (`mpf_stack_index_t`, new
  `mpf_stack_index.h/.c`) maps the canonicalized *active* stack configuration
  of a state to a dense 32-bit `cfg_id`, so equivalent committed-stack
  structures reached via different action orders deduplicate instead of
  blowing up the state space. `mpf_state_t` now carries `stack_index` /
  `stack_cfg_id` (root-owned, freed in `mpf_state_cleanup`); the root creates
  the index in `mpf_build_game`, child states inherit it and resolve their id
  in `mpf_apply_action_wrapper`, and `mpf_infoset_key` folds the id into the
  hash under `keyed_mode`. A compact, bounds-checked `mpf_reach_map_t` stores
  per-(cfg_id, player) reach weights without the fixed `MPF_MAX_PLAYERS`
  stride. The content-derived infoset key (`mpf_infoset_key`) now folds the
  *stack-configuration hash* directly into the key (canonicalized active
  mask + per-player round_contrib/remaining via `mpf_stack_config_from_arrays` /
  `mpf_stack_config_hash`), so asymmetrical stack structures map to
  distinct-but-deduplicated infosets across the whole multiway solve. The
  index is wired into storage lookups by `get_infoset_key` (active whenever not
  already in `keyed_mode`), and `mpf_node_storage_key` keeps the tree-exporter /
  locked-strategy bookkeeping aligned with the new key. New unit
  `test_mpf_stack_index.c` covers id assignment/dedup, inactive-player masking,
  idempotency, reach-map bounds, rehash determinism, an infoset-key-includes
  stacks assertion, a 6-player asymmetric integration smoke test, and a
  memory-ratio benchmark (6 distinct stacks vs symmetric stays < 1.5x infoset
  count, satisfying the FEAT-10 acceptance criterion).
- Multi-street board-texture & strength abstraction bucketing (FEAT-13, #149):
  a postflop node-abstraction layer in the style of MonkerSolver's
  "Strength Buckets + Texture Filter" settings, intended to shrink the
  multi-street PLO/Hold'em state space by merging isomorphic / similar streets.
  Two new modules:
    - `board_texture.{c,h}` classifies a board of any size (3 = flop, 4 = turn,
      5 = river) into structural properties (paired / monotone / two-tone /
      rainbow / connected / high-card) and folds them into a coarse texture
      *filter level* (`PE_TEXTURE_FILTER_NONE / SMALL / MEDIUM / LARGE /
      PERFECT`, matching MonkerSolver's None→Perfect ladder) plus a
      `pe_board_texture_id()` helper that returns a stable merge key so that
      texture-indistinguishable boards share one CFR infoset. Density estimates
      (`pe_board_texture_density`) translate a filter level into a per-street
      abstraction budget.
    - `strength_bucketing.{c,h}` buckets hands on the classic 2-D EHS/EHS2
      abstraction (Gilpin & Sandholm): `pe_strength_features` scores a hand by
      E[HS] and E[HS^2] against the opponent range (exhaustive for 2-card holes,
      deterministic sample for 4-card Omaha), and `pe_strength_table_train` /
      `_train_all` k-means-cluster (`pe_sbk_cluster`, k-means++ + Lloyd,
      deterministic PCG32) those points into `n_buckets` strength buckets. The
      resulting table is serializable (`.pe_sbk`, same magic/version/LE
      conventions as FEAT-09) and assignable on the CFR hot path via
      `pe_strength_table_assign_cached` (memoized per (hole, board)). Centroids
      are sorted ascending so bucket 0 is always weakest.
  The two halves compose into the full FEAT-13 abstraction: combine
  `pe_strength_table_assign` (strength bucket) with `pe_board_texture_id`
  (texture-merged board) to realize street-by-street node abstraction. The
  configuration knobs `cfr_config_t::strength_buckets_per_street` and
  `cfr_config_t::texture_filter_level` are added so solvers can opt into the
  abstraction; both default to 0 (disabled), preserving existing behaviour.
  New units `test_board_texture.c` (monotone/rainbow/paired/connected/turn-river
  card counts, density monotonicity, texture-id merging) and
  `test_strength_bucketing.c` (feature range, train+assign ordering,
  deterministic training + save/load round-trip, AA-stronger-than-air
  hierarchy) cover the pair.
- Wire FEAT-13 abstraction into the Omaha HU river adapter (FEAT-13 suite, #190):
  `omaha_river_state_t` gains `strength_table` (FEAT-13 `pe_strength_table_t *`,
  board-specific, not owned by the state) and `texture_level` (the configured
  `pe_texture_filter_level_t`). `omaha_infoset_key` gains three bucket modes
  that consume the FEAT-13 primitives alongside the existing FEAT-04 mode 4:
    - `bucket_mode == 5` : strength buckets (EHS/EHS2) — the FEAT-13 strength
      bucket id replaces the private-hand class + coarse strength bin, exactly
      like FEAT-04's learned clusters but with the 2-D EHS/EHS2 abstraction.
    - `bucket_mode == 6` : board-texture merging — `pe_board_texture_id` of the
      board REPLACES the board class in the infoset key (compact id < 16, packed
      across the former b_cls and bucket-id fields) so texture-equivalent boards
      share one node at the chosen filter level (Small/Medium/Large).
    - `bucket_mode == 7` : both — strength bucket (bits 48..55) combined with
      texture merge (tex_hi at <<56, tex_lo at <<44), the MonkerSolver "Strength
      Buckets + Texture Filter" pairing.
  `bench_cfr_omaha_river.c` exposes `--buckets-per-street`, `--texture-filter`
  (0..3) and `--shared-storage` (one storage reused across deals so texture
  merging actually collapses infosets that occur on different texture-equivalent
  boards) and populates `cfr_config_t::strength_buckets_per_street` /
  `texture_filter_level`. With `--shared-storage`, mode 6 (Small) collapses the
  river infoset count from 105 (baseline mode 3) to 42 across 80 random deals —
  a 60% reduction; the ≥90% target from #149 is multi-street (turn→river) and is
  exercised once the adapters drive the abstraction from `cfr_config_t` in a
  tree solve, which is follow-up work.
- Drive FEAT-13 abstraction from cfr_config_t in the multi-street (turn→river)
  solver (FEAT-13 suite, #192): the Hold'em HU **turn** adapter now consumes the
  same FEAT-13 primitives the river adapter gained in #190, so the abstraction
  applies across streets. `holdem_river_state_t` gains `strength_table`
  (`pe_strength_table_t *`) and `texture_level` (matching the Omaha river state),
  and `hr_infoset_key` gains the same `bucket_mode` 5 (strength buckets EHS/EHS2),
  6 (board-texture merging via `pe_board_texture_id`) and 7 (combined
  Strength+Texture) as the Omaha adapter. `bench_cfr_holdem_turn.c` exposes
  `--buckets-per-street` and `--texture-filter` (0..3) and trains a per-deal
  `pe_strength_table_t` on the sampled river board, propagating the FEAT-13
knobs onto `river_state` exactly as the Omaha turn/river path does.
   The modes are verified to run without fault in the multi-street benchmark
   (modes 5/6/7 produce valid infoset counts per deal). `--shared-storage`
   (one `cfr_storage_t` reused across deals) is now available from the turn
   benchmark too, and the board-texture modes also merge the *turn* board:
   `holdem_river_state_t` gains `turn_board`, propagated by the turn adapter,
   so `hr_infoset_key` modes 6/7 key turn nodes by their own texture id instead
   of the per-deal detailed features — the key change that lets texture-
   equivalent turn boards reached from different deals collapse onto shared
   infosets. Measured on 1000 random turn deals (20 iters/deal, HU hold'em):
   baseline `bucket_mode 3` accumulates **2030** infosets while
   `bucket_mode 6 --texture-filter 1` (SMALL) plateaus at **60** infosets,
   a **~97% state-space reduction**, meeting the ≥90% target of #149 in the
   multi-street turn→river setting.
- Wire FEAT-13 abstraction into the Omaha8 (hi/lo 4-card) and Short Deck
  Hold'em HU river adapters (FEAT-13 suite, #190): `omaha8_river_state_t` and
  `shortdeck_river_state_t` gain `strength_table` (board-specific
  `pe_strength_table_t *`, not owned by the state) and `texture_level` (the
  configured `pe_texture_filter_level_t`), and both infoset keys gain the same
  `bucket_mode` 5 (strength buckets EHS/EHS2), 6 (board-texture merging via
  `pe_board_texture_id`) and 7 (combined Strength+Texture) key layouts as the
  Omaha adapter from #190. `bench_cfr_omaha8_river.c` and
  `bench_cfr_shortdeck_river.c` expose `--buckets-per-street`,
  `--texture-filter` (0..3) and `--shared-storage`, and train a per-deal
  `pe_strength_table_t` on the sampled board (hole_cards 4 for Omaha8, 2 for
  Short Deck), populating `cfr_config_t::strength_buckets_per_street` /
  `texture_filter_level`. With `--shared-storage` across 20 random deals
  (200 iters/deal), board-texture merging (mode 6, SMALL) collapses the river
  infoset count from **2645** (baseline mode 3) to **819** for Short Deck
  (−69%) and from 649 to 585 for Omaha8 (−10%); modes 5/7 run cleanly and
  produce the expected per-deal counts. Fixes a pre-existing Short Deck bench
  bug: random deals sampled card indices `16..51`, but the 36-card 6+ deck
  (per `create_deck_mask(EVAL_DECK_SHORT)`) only contains
  `{4..12, 17..25, 30..38, 43..51}`, so 9 of the sampled cards were rejected
  by the evaluator (degenerate all-tie showdowns in mode 3, and
  `pe_strength_table_train_all` failures in modes 5/7); the bench now draws
  from the actual deck via a `shortdeck_cards[36]` pool.
  building (FEAT-07, #143): the range parser now tokenizes `$cb` (c-bet spot
  range), `SPR>x` / `SPR<x`, `POS=IP` / `POS=OOP`, `BET` and `AUTO` as metadata
  carried on the parsed range (`arp_range_t.spot_filters`), exposed via
  `ARP_GetSpotFilters`, `ARP_ValidateSpotSyntax` and `ARP_EvaluateSpotFilters`.
  The MPF tree loader parses the same syntax out of `rangeProfile.combos`
  (`mpf_tree_spot_rule_t`), evaluates the conditional SPR/position gates against
  each node's snapshot during profile application (`mpf_tree_evaluate_spot_rules`,
  cached on `mpf_tree_node_t.spot_rules_pass`), and resolves `$cb` to the
  previous street aggressor's active range via `mpf_tree_resolve_cb_range`. New
  unit `test_spot_filter.c` covers both layers.
- Subgame re-solving / CFR-D gadget (FEAT-05, #122): re-solve a subtree of an
  already-solved blueprint without re-solving the whole tree, holding the
  boundary value fixed via the CFR-D gadget (Burch, Johanson & Bowling 2014).
  New unit `cfr_resolve.c` (header `cfr_resolve.h`) provides
  `pe_cfr_resolve_subgame`, `pe_cfr_seed_resolve_storage` (trunk-locked mode
  reusing the FEAT-01 locking machinery), `pe_cfr_blueprint_cfv`,
  `pe_cfr_subgame_infosets`, and a `pe_cfr_gadget_t` decorator over
  `cfr_game_t` so the gadget works with any adapter and with FEAT-09 `.pe_sol`
  blueprints. 2-player games are fully supported; multiway uses the
  trunk-locked fallback (`PE_CFR_RESOLVE_UNSUPPORTED` otherwise).
- Learned hand abstraction via k-means clustering (FEAT-04, #121): per-hand
  feature vectors of `E[HS^2]` + equity-distribution histogram, k-means++
  seeded k-means with deterministic (PCG) training, and a serializable
  `.pe_bkt` bucket table (`pe_bucket_table_train` / `pe_bucket_table_load`,
  magic `PEBKT001`). River adapters accept `bucket_mode = 4` to abstract
  private hands into learned buckets, composed after FEAT-02 board isomorphism.
- Compact binary storage format for solved trees: `pe_cfr_save_storage()` /
  `pe_cfr_load_storage()` plus memory-mapped read-only `.pe_sol` views
  (`pe_sol_open_mmap`) and a `.pe_tree` binary serializer (`pe_tree_save` /
  `pe_tree_load`). Strategy weights are quantized to 16-bit fixed point, giving
  >= 4x smaller files than a JSON dump with < 0.01% EV loss (#145).
- Pineapple Hi/Lo (`game_pineapple8`): 8-or-better pot split with the best two
  hole cards chosen independently for the high and low hands; `pokenum -pa8`
  (#42).
- Modern Equity API (`poker_eval_modern_equity.c`), game-aware preflop helper,
  behind `POKER_EVAL_EXPERIMENTAL` (#38).
- Range parser `@` weight syntax (e.g. `AA@60%, KK@25%, AKo@15%`), equivalent
  to the `{}` brace form, plus `ARP_ExportRangeMatrix()` visualization (#45).
- Badugi 4-card hands in `pe_range_parse` (#41).
- GPU range-vs-range Monte Carlo: per-player `StdDeck_CardMask` ranges in the
  CUDA and OpenCL backends (#37).

### Changed
- Monte Carlo `iterations_if_montecarlo` is treated as a total budget for the
  whole range-vs-range call, divided across matchups, in the single-threaded
  path as well as the MT variants (#63).

### Fixed
- `bench_cfr_holdem_multi` dealt Short Deck hands and boards from the full
  52-card deck: 9 of the sampled indices (ranks 2..5, e.g. 16, 26..29, 39..42)
  are not part of the 36-card 6+ deck the evaluator accepts, producing
  `EVAL_INVALID` showdowns and degenerate solves. Random deals are now drawn
  from the valid 36-card pool, and explicit `--board` / `--hands` cards outside
  the deck are rejected (same fix as the Short Deck river bench in #195, #196).
- `bench_cfr_holdem_multi` `--board` and `--hands` were silently ignored: the
  card parser checked `StdDeck_stringToCard(...) != 1` although that function
  returns 2 on success, so every token was rejected and the board/hands were
  always dealt randomly. The return check is fixed and parsed cards are now
  converted from StdDeck indices to the modern mask indices the adapter
  consumes (StdDeck suits h,d,c,s -> modern c,d,h,s).
- GPU Monte Carlo hole-card dealing drew cards by rejection sampling, which
  could exhaust its attempts and silently leave a player with a short hand when
  a range was tight. The CUDA and OpenCL backends now collect the
  range-allowed available cards and deal uniformly from them (#37).

## [1.2.0] - 2026-08-02

Minor rather than patch: three features landed since 1.1.0.

Anyone evaluating low hands should upgrade. Ace-to-five lows on six and seven
cards were wrong in several independent ways, so razz, 7studnsq, and the hi/lo
games could award the low half of a pot to a hand that had no qualifying low.

### Added
- Game-aware preflop lookup table, with Omaha support (#36).
- Omaha range parser Phase 2: range operators and a rewritten percentage-tier
  model (#33).
- Stud multi-street range parser, covering 4th through 7th street (#35).

### Fixed -- low hand evaluation
- Ace-to-five low evaluation on six and seven cards no longer refuses a no-pair
  low when a rank appears twice. A stud hand such as 4-5-5-6-6-7-8 now plays its
  8-7-6-5-4 instead of being scored as a pair. Affected `pe_eval_low_a5` and
  every caller: razz, 7studnsq, and the hi/lo games through the Python binding.
- `_findBestLowballHand` now packs the selected cards highest-first, matching the
  five-card path and `LowHandVal`'s layout. Two lows sharing their bottom cards
  were previously compared in the wrong order (A-2-3-4-8 beat A-2-3-5-6).
- `pe_low_qualify5` rejects paired hands. It only inspected `TOP_CARD`, so a low
  holding a pair of deuces passed the 8-or-better qualifier.
- The Python binding evaluates hi/lo lows with `StdDeck_Lowball8_EVAL`, as the
  library itself does, instead of the ace-to-five evaluator plus a qualifier.
  `winners()` awarded the low half of `7stud8` and `holdem8` pots to hands with
  no qualifying low.
- The OpenCL low evaluator carried the same three defects, so results depended on
  which backend ran. Its five-card path also returned "no low" for any paired
  hand, where the CPU evaluates it as a low. All four are now aligned, checked
  exhaustively over the 2598960 five-card hands.
- Every OpenCL kernel file now compiles on its own, and their shared macro blocks
  are kept consistent by a test.

### Fixed -- equity and ranges
- `is_monte_carlo == 0` is the automatic heuristic the header documents, not a
  request for exhaustive enumeration. A caller passing a zero-initialised options
  struct on an incomplete board took the exact path, which preflop expands to
  C(48,5) boards times the product of the range sizes. The previous code never
  selected the exact path either, so `result->exact` could not be 1 on an
  incomplete board.
- Hash deduplication in the advanced range parser.
- `enum_result_t` ordering histograms, and combo buffers when a later range is
  empty, are no longer leaked (#56).

### Fixed -- build and portability
- FreeBSD: `immintrin.h` is included whenever `__AVX2__` is defined, and `alloca`
  comes from a portable header.
- macOS with GCC: Apple's OpenCL deprecation no longer fails the build.
- Windows on ARM64: DLL symbol export.
- 32-bit wheel builds are skipped rather than failing.

### CI
- Automated GitHub Release workflow, covering multi-arch native builds, FreeBSD,
  and Python wheels.
- Sanitizers run the full suite weekly, UBSan failures fail the job, and the
  suppression denylist is empty.
- Actions moved to the Node 24 runtime.

## [1.1.0] - 2026-07-30

### Documentation & Translation
- Audited and updated 30+ Markdown documentation files across API, equity, games, GPU, optimization, parsing, testing, and source modules.
- Synchronized all API code snippets, struct definitions, enum values, and header paths (`<poker_eval/...>`) with C codebase contracts.
- Translated all French markdown documentation, code comments, prose, and section titles into technical English.

### Fixes & Stability
- **Memory Safety**: Added bounds checks to `strcpy`, `strcat`, `strlen` (using `strnlen`), and guaranteed NUL termination for `strncpy`.
- **Memory Safety**: Replaced `sprintf` with bounded formatting and used `sizeof(destination)` for `memcpy` bounds.
- **Portability**: Restored MSVC and MinGW compiler portability.
- **Concurrency**: Eliminated Linux solver race conditions and crash regressions.
- **Evaluators**: Corrected Debug assertion failures across evaluators and reported best five cards from stable C API.
- **Python Bindings**: Required Python >= 3.11 for wheel builds and updated packaging metadata.
- **CI / CMake**: Improved CMake preset resolution (`cmake.yml`) and enabled GPU tree compilation in CI.

## [1.0.0] - 2024-07-29

### Added
- Modernized CMake build system with modular target definitions.
- Installable Python bindings via `pyproject.toml` and `scikit-build-core`.
- GPU acceleration support (CUDA/OpenCL) for batched evaluation.
- SIMD optimizations for batch card manipulation and evaluation.
- Multi-threaded Monte Carlo simulation engine.
- Comprehensive Unity unit testing framework.
