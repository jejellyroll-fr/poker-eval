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
   cards, so the application never invents them or keeps a stale board.
3. Optionally provide the matching `.mkr`, then press **Solve this spot**.
   River trees are sent to `pe-vector-sim`; non-river trees are rejected by
   this first native vector workflow with an explicit message rather than a
   misleading result.

The runner field defaults to `pe-vector-sim`, so it can point at an installed
binary or at the build output on a developer machine. The next UI tranche can
replace the text output with the range matrix, tree canvas, and action/EV
panels without changing the setup or solver seams.
