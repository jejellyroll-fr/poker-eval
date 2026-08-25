# poker-eval Studio

`poker-eval-studio` is the native C desktop shell for loading and checking a
Monker spot. It uses [NAppGUI](https://nappgui.com/) for native controls on
macOS, Linux and Windows; SDL remains available as the legacy fallback.

The NAppGUI source is intentionally an external dependency. Configure a
checkout of NAppGUI with:

```sh
cmake -S . -B build-studio \
  -DPE_NAPPGUI_ROOT=/path/to/nappgui_src \
  -DBUILD_TESTS=OFF -DBUILD_BINDINGS=OFF -DBUILD_EXAMPLES=OFF \
  -DENABLE_LTO=OFF
cmake --build build-studio --target poker-eval-studio
```

The application has two real tabs:

- **SETUP** contains the tree, board, ranges, runner and run controls.
- **RESULTS** contains the convergence summary, final metrics, an action-step
  report, per-observed-hand frequencies/EV table, and the raw run log.
  Starting a solve switches to this tab automatically; loading a new tree
  switches back to setup. **Load .mkr report** runs the Monker tree/strategy
  aggregation report against the selected `.tree` and `.mkr` and displays its
  JSON result in this tab.

The application workflow is strict:

1. Browse or paste a `.tree` path and inspect it. The binary header and
   embedded ranges determine the game, player count, street, and combo layout.
2. Enter the board required by that street. A `.tree` does not contain board
   cards, so the application never invents them or keeps a stale board. A
   preflop tree correctly requires zero board cards; loading one clears the
   board field and synchronises the game/player selectors with the header.
3. Choose one **Stop condition**: **Max iterations** or **Exploitability
   target**. They are mutually exclusive. **Convergence check every** only
   controls how often the empirical BR is measured and is not a third stop
   condition.
4. Press **Solve this spot**. The run is asynchronous: the results panel shows
   the current iteration, percentage, empirical exploitability and configured
   target while the child solver is running. **Stop run** is always visible and
   cancels the child process; the final result remains available in the
   **RESULTS** tab. River trees are sent to `pe-vector-sim`.
   Preflop trees are sent to `pe-preflop-solve --tree`, use `100%` for any
   range left empty, follow the imported Monker betting nodes, deal flop,
   turn and river, and settle the showdown. A rangeless tree is therefore a
   valid all-combos simulation; it is not rejected as a malformed tree.

The runner field defaults to `pe-vector-sim` for river spots. For preflop
spots the Studio selects `pe-preflop-solve` automatically and resolves it from
`build/tools` when running from a checkout. The current Lane B path samples
correlated private deals and public runouts. The output includes iteration,
fraction, empirical exploitability in mBB, and the configured target at each
measurement interval; increasing iterations improves coverage of the complete
board distribution. Loading a preflop tree clears the board input, changes its
placeholder to automatic runout, synchronises game/player selectors, and
materialises an empty external range as `100%`. The preflop solver emits a
`STRATEGY REPORT` with observed tree decision steps and one row per materialised
sampled infoset: hand, tree node, player, action frequencies, and empirical EV
for each action. Hold'em also gets a compact 13x13 highest-frequency action
grid; PLO4, PLO5 and PLO6 remain in the wider hand table because an Omaha cell
represents many more suit combinations. The EV is a rollout estimate under
the current Lane B policy, not an exact full-tree EV.
