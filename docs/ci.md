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
runners, so the leg went red as well. The two `enumtest` cases were the
enumeration sweeps' output rather than the evaluator, and they now run quiet
(see below). `joktest1` and `test_enum_basic` are a different matter: they print
27 and 432 bytes, so the sweeps' explanation does not carry over, and OpenMP
alone does not reproduce their timeouts. Measured on run `44886497194` — the
same arm64 image, a Debug build, `libomp` 23.1.6 and `-Xclang -fopenmp` — they
take **1.89 s and 0.02 s** run alone, against 1.61 s and 0.01 s without OpenMP,
and the same under ctest.

The parallel run they failed in is no longer reproducible either. Run
`44828726427` timed them out at 66.9 s and 74.2 s, with the suite totalling
1040 s, `test_montecarlo_convergence_unity` at 79.5 s, and `joktest1` having
already printed its last line (`joktest1: all tests passed`) when ctest killed
it. That runner also handed out far more parallel slots than the pool has
cores: its log starts 13 cases in the suite's first second and 17 in one second
later on, which is why the 12-way run below is the closest comparison available.
Replaying the configuration on run `44888589983` — arm64 Debug, OpenMP on,
`--parallel` at the runner's 3 cores — passed all 390 cases in 764.46 s, with
`joktest1` at 1.52 s and `test_enum_basic` at 0.01 s. Forcing `--parallel 12` on
the same 3 cores (run `44891011141`) inflated the CPU-bound cases by about five
times — `joktest1` 7.70 s, `test_montecarlo_convergence_unity` 74.21 s, and
`test_cfr_chance_nodes`, `test_mpf_hilo8` and `test_pe_hand_features` timed out
— while `test_enum_basic` stayed at 0.01 s. Five times is not the 44 times and
the 7000 times that 66.9 s and 74.2 s would need, so starvation does not account
for them: those four timeouts are unreproduced and unexplained, and neither test
was changed.

It stays single-threaded: the backend buys nothing for the cost that dominates,
adds an install step and breaks tests. The suite is parallelised by `ctest
--parallel` in the workflow instead, which spreads the 376 cases over the cores
and leaves the one long case as the floor. That value is the constant 4 rather
than `getconf _NPROCESSORS_ONLN`: the pool has served runners that report, and
launch, far more concurrent cases than they can run, and 12 concurrent cases on
a 3-core runner put three other cases into timeout (see above). Four keeps one
case per core plus one on the pool's 3-core runners.

That case is tagged `slow`, together with `plo4_certify_full_range` (about
150 s), `test_cfr_chance_nodes` (about 55 s) and `test_akq_game` (about 40 s).
Each became the floor of the pull request suite as the previous one was dropped:
the big solve alone left 216 s (run `44832047898`), and the big solve plus the
certifier left 183.85 s (run `44833628714`). The workflow passes `-LE slow` when
the event is `pull_request`, which brings the pull request suite to **113.08 s
over 372 cases** (run `44838377928`). Pushes and the nightly still run every
case: run `44839069290` ran all 376, the 505 s preflop solve included.

The same rule covers the blocking Linux and Windows legs, where the group costs
more than it does on arm64: `ci.yml` and `cmake.yml` also pass `-LE slow` when
the event is `pull_request`. Measured on runs `44842004130` and `44842005156`
against `44838376949`/`44838377012` on the same runners: Native Tests
(ubuntu-24.04-arm) 1090 s → **270 s**, Native Tests (macos-15-intel) 2195 s
ending in a timeout → **401 s, green**, Release Build & Tests 530 s → **356 s**,
and the CMake legs 423/538/568/554/901 s → 265/230/255/370/611 s, the suite
itself going from 317.40 s to 139.33 s on Linux GCC. The `enumtest5` and
`enumtest7` timeouts that used to fail those legs had nothing to do with the
exclusion; see below.

### The enumeration sweeps and ctest's output capture

`enumtest5` and `enumtest7` compare `Hand_EVAL_N` with `Hand_EVAL_X5`/`_X7` over
every five-card hand — 2,598,960 and 2,118,760 of them. The default run prints
one line per hand so that `tests/run.in` can checksum the whole sweep, which is
33 MB and 29 MB of text.

The sweep is not what was timing out. Measured on run `44880538551`, in the same
job on a 4-vCPU runner, `enumtest5` took 0.42 s with its stdout on `/dev/null`
and 32.56 s under `ctest`, of which **26.5 s was system time**: the cost is the
output, not the comparison. Run `44881898213` shows the same split (0.56 s to
`/dev/null`, 8.28 s through a pty, 11.6 s of it system time) and the checksums
of the default output still match `run.in` on the runner. Locally, the same
binary takes 0.35 s writing to a file and 3.0 s through a pty.

So the tests were close to the 30 s default `TIMEOUT` on any leg where that 33 MB
costs more than a buffered write. The legs that went red were Linux GCC, Linux
Clang and Windows MinGW; Windows MSVC stayed green, and macOS does not register
the two tests at all without OpenMP. *Why* ctest's capture costs that much on
those legs was not established: the measured facts are the 0.42 s → 32.56 s
split and its 26.5 s of system time, and a pty gives the same signature (one
write syscall per line, 8.28 s and mostly system time on run `44881898213`).
The per-line write behind the capture is the likely reading, not something this
document can show. Both tests now accept `--quiet`, which keeps the
full sweep and the comparison but drops the per-hand line, and
`tests/CMakeLists.txt` registers `enumtest5 --quiet` and `enumtest7 --quiet`.
Under ctest on the runner they now take 0.13 s and 0.09 s; run `44881898213` is
green on all five legs with the whole suite, `slow` group included. A
disagreement between the two evaluators now returns 1 — it used to print the
hand and `exit(0)`, so the registered test could not fail at all.

`tests/run.in` still runs the binaries without arguments, so the checksums there
are untouched: `74c0f134…` for `enumtest5` and `02d54844…` for `enumtest7`,
verified on the runner and in a local Release build.

### The `macos-arm64` label

Add it when the change is worth verifying on Apple Silicon before merging — the
build files, the CMake presets, the native test suite or the Python binding —
and remove it when the arm64 pool is short of runners and the jobs sit queued or
come back `cancelled`. They are not required checks, so a pull request without
the label merges on the Intel legs alone.

Adding or removing the label re-runs the workflow (`labeled`/`unlabeled`), so
neither action needs a push.
