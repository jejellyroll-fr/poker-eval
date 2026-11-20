# Continuous integration

The workflows live in `.github/workflows/`. Pull requests and pushes to
`master`/`development` run the blocking set: Linux (GCC and Clang), Windows
(MSVC and MinGW), the Intel macOS image `macos-15-intel`, the arm64 Linux image
`ubuntu-24.04-arm`, the wheels and the release build check.

A push that changes only Markdown files or `docs/` runs none of them: every
push-triggered workflow carries `paths-ignore` for `**.md` and `docs/**`, since
there is nothing to build and a pull request already ran the blocking set on the
same content. The filter is on `push` only, so a pull request that touches code
and documentation together still runs everything.

One file is exempt because a test reads it:
`docs/cfr/guides/plo4_river_certification.md` embeds the real
`pe-plo4-certify` output, and ctest's `plo4_certify_docs_example` fails when
the guide and the tool drift apart. `plo4-certification-docs.yml` runs that
single test on a push that touches the guide, so the exemption costs one short
job rather than waking the blocking set.

## macOS arm64 legs

`macos-arm64.yml` builds and tests on the hosted Apple Silicon image
(`macos-15`): `CMake Release (arm64)`, `Native Tests (arm64)` and
`Wheel (arm64)`.

They are kept out of `cmake.yml`, `ci.yml` and `release-check.yml` because a
hosted arm64 job that never gets a runner is cancelled before its first step
("The job was not acquired by Runner of type hosted even after multiple
attempts") and fails the run whatever `continue-on-error` says. Only the Intel
macOS legs block a pull request.

| Event | arm64 legs |
|---|---|
| `push` to `master`/`development`, nightly `schedule`, `workflow_dispatch` | always run, except a push that changes only Markdown files or `docs/` |
| `pull_request` | run only with the `macos-arm64` label |

Two pushes to the same ref supersede each other, so a stale run does not hold
the scarce pool while a newer commit waits behind it. A re-run must never do
that, and scoping the group by event is not enough: a re-run keeps
`github.event_name == 'push'`, so `gh run rerun` of an older commit joined the
push group and cancelled the run that was building. The group keys on
`github.run_attempt` instead — 1 for a fresh run, 2 and up for a re-run — so a
first attempt keeps the ref in the group and still supersedes the previous
push, while a re-run joins a group named after its own run id:

```yaml
group: "macos-arm64-${{ github.event_name }}-${{ github.run_attempt == 1 && github.ref || github.run_id }}"
```

### A `failure` conclusion without a single step

When GitHub has no hosted Apple Silicon runner to assign, the three jobs are
cancelled roughly twenty minutes after they are queued, none of them executes a
single step, and the run concludes `failure` even though nothing was built or
tested. Read the jobs rather than the run: a cancellation with an empty step
list and a "was not acquired by Runner of type hosted" annotation is a capacity
problem, not a code problem. Re-run that run (`gh run rerun <id>`) instead of
looking for a regression, and only start investigating when a job actually
failed a step.

`macos-arm64-capacity-rerun.yml` does that re-run on its own. It watches every
completed run of `macOS arm64` and re-runs it when the run concluded `failure`
and at least one job is `cancelled` although its step list is empty — the
signature of a pool that never assigned that job a runner. The `cancelled`
conclusion is part of the test on purpose: a job that dies during startup (an
unknown runner label, a rejected action) also has no steps, but it concludes
`failure`, and re-running it would add a delay without changing the outcome. The partial case is
the one that matters in practice: the pool often hands out its single runner to
one job while the others starve, for example `Wheel` succeeding while `CMake
Release` and `Native Tests` are cancelled after twenty minutes (run
`44497002211`). Such a run contains steps, so a "did anything run?" test passes
it off as a real failure even though two thirds of the matrix never executed.
Three things are left alone: a run where every job that was not skipped executed
a step (a real failure), a run that concluded `cancelled` (the concurrency group
replaced it with a newer one), and a retry that fails the same way, since only
the first attempt is retried. A starved pool therefore costs a delay, not a red
run needing attention.

### Why the native leg keeps `ENABLE_OPENMP=OFF`

`Native Tests (arm64)` could build against Homebrew's `libomp` and get the
parallel solver backend, but measuring it on run `44828726427` (branch
`ci/arm64-openmp-native`) showed the trade is negative. The hosted image ships no
libomp, so the leg first pays `brew install libomp` (14 s); the build then picks
up `-Xclang -fopenmp` and reports `cpu_par … ready`. With that in place the
suite's longest case, `pe_preflop_tree_flop_resolves_with_mpf`, took **555 s with
`--threads 4`** against 526 s without OpenMP. The exact traversal of a complete
PLO5 range never leaves one core regardless of the backend — a local OpenMP build
with `--threads 10` measured 528 s against 509 s single-threaded, at 99 % of one
CPU. OpenMP also enables the gated cases (376 → 390 tests) and four of them
(`joktest1`, `enumtest5`, `enumtest7`, `test_enum_basic`) time out on the small
runners, so the leg went red as well.

It stays single-threaded: the backend buys nothing for the cost that dominates,
adds an install step and breaks tests. The suite is parallelised by `ctest
--parallel` in the workflow instead, which spreads the 376 cases over the cores
and leaves the one long case as the floor.

That case and the other `slow`-tagged ones are dropped on pull requests: the
workflow passes `-LE slow` when the event is `pull_request`, which brings the
arm64 suite under three minutes, and pushes and the nightly still run every
case.

### The `macos-arm64` label

Add it when the change is worth verifying on Apple Silicon before merging — the
build files, the CMake presets, the native test suite or the Python binding —
and remove it when the arm64 pool is short of runners and the jobs sit queued or
come back `cancelled`. They are not required checks, so a pull request without
the label merges on the Intel legs alone.

Adding or removing the label re-runs the workflow (`labeled`/`unlabeled`), so
neither action needs a push.
