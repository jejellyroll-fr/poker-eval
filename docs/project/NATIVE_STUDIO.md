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

The application workflow is strict:

1. Browse or paste a `.tree` path and inspect it. The binary header and
   embedded ranges determine the game, player count, street, and combo layout.
2. Enter the board required by that street. A `.tree` does not contain board
   cards, so the application never invents them or keeps a stale board. A
   preflop tree correctly requires zero board cards; loading one clears the
   board field and synchronises the game/player selectors with the header.
3. Press **Solve this spot**. The run is asynchronous: the output panel is
   refreshed while the child solver is running, and the same button becomes
   **Stop solve** and cancels the child process. River trees are sent to `pe-vector-sim`.
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
board distribution. The next UI tranche can replace the text output with the
range matrix, tree canvas, and action/EV panels without changing the setup or
solver seams.
