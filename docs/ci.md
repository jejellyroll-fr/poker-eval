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

## Omaha turn terminal regression

`omaha-turn-regression.yml` guards design A of issue #332: hoisting the
showdown out of the inner loop made the PLO6 turn terminal 47 times faster
while leaving its values bit-identical
(`docs/cfr/guides/plo_turn_pair_walk.md`), and nothing in the test suite would
notice a later change that gave that speed back. The job runs the check itself
on `ubuntu-latest`, in a Release build with the tests, the examples, the
bindings, the GPU backend and OpenMP off. A run takes a few minutes, most of it
in the configure and the build.

It runs on `pull_request` only, filtered to the solver, evaluator, deck, game,
the benchmark source, the probe, the build files and the workflow itself. There
is deliberately no base-branch filter: the work lands through stacked pull
requests, so a guard that only watched `master` would not see the branch that
introduces the regression until after it was merged. `workflow_dispatch` runs
it on demand.

### What it prices

One reference configuration, the one the guide quotes: PLO6, two thousand combos
per range, no dead cards, the board `AsKd7c2h`, every legal deal visited, three
repetitions, taken with `--profile` and `--frozen-strength`.
`benchmarks/solver/omaha_turn_probe.py` runs
`tools/pe_omaha_turn_bench` and validates its JSON report; the report is
uploaded as the `omaha-turn-bench` artifact whether the gate passes or fails,
so a red run can be read after the fact.

### What makes it fail

Two groups of checks, and the first one runs before any clock is trusted.

Exact, because a change of order, weight or payoff is a correctness bug rather
than a slow run:

| Field | Required |
|---|---|
| `values_fingerprint` | `ac3016522c620075` |
| `profile.deals` | `50 849 064` |
| `rivers_priced` | `48` |
| `weight_sum` | `19 821 384.246093750` within `1e-9` relative |
| `deterministic_across_repetitions` | `true` |
| `profile.counts_stable_across_repetitions` | `true` |
| `probe.matches_live` | `true` |
| `probe.deals` | `50 849 064` |
| `holes`, `combos_per_player`, `hilo8` | `6`, `2000`, `false` |

The fingerprint covers the values the terminal writes for every deal; the deal
count, the river count and the mass cover the work it does to write them; the
probe's `matches_live` covers the frozen-strength path against the live one. A
report taken without `--profile` or without `--frozen-strength` is rejected
outright, since the fields it needs would be missing.

Timed, with a 2x envelope on both figures:

| Field | Anchor | Budget at 2x |
|---|---|---|
| `turn_seconds.min` | 1.78 s | 3.56 s |
| `probe.nanoseconds_per_visit` | 32.1 ns | 64.2 ns |

Both figures are printed whether the gate passes or fails, with their anchor and
the envelope in force, so a runner that drifts says so in the log instead of
turning into a mystery.

The envelope is loose on purpose, and it has to be. Before design A the same
reference turn took **53.94 s and 1087 ns per deal**, so even 2x the anchor sits
fifteen to seventeen times below the regression being caught: the gate
watches for the gain being thrown away, not for a few percent of host noise. In
the other direction the two figures move together but not identically — a
slower host inflates both — so a raised visit cost with an unchanged terminal
phase is the signature worth reading closely.

### Where the anchors come from

They are the numbers of the runner pool, not of a workstation. The same commit
measured **1.12 s** on a developer machine and **1.785 s / 32.14 ns** and
**2.262 s / 40.91 ns** on two hosts of the `ubuntu-latest` pool — a spread of
27 % between two machines that both ran the reference configuration correctly.
Anchoring on the workstation would have failed every CI run; anchoring on the
faster host left the slower one with a third of the envelope still free. A
factor of 1.27 between two hosts of one pool is ordinary, and the envelope has
to cover it without covering a real regression.

### Recalibrating when the pool changes

GitHub replaces its `ubuntu-latest` images regularly, and a faster or slower
family of hosts moves both figures at once. Recalibrate when the gate fails on
timing alone — the fingerprint and the counters green, one or both of the two
printed figures past its budget — and when the drift is a step rather than a
spike.

1. Read the run's log for the two `terminal phase:` and `per visit:` lines, or
   run the job by hand with
   `gh workflow run omaha-turn-regression.yml --ref <branch>` then
   `gh run view --log`. Re-run it once on a second host before believing a
   single sample: the pool is not homogeneous, and the 27 % spread above is the
   range to expect between two hosts of the same family.
2. Set `TURN_SECONDS_ANCHOR` and `VISIT_NS_ANCHOR` in
   `benchmarks/solver/omaha_turn_probe.py` to the **faster** of the samples
   seen, rounded down a little, so the slower host of the pool still fits under
   the envelope. Keep the two consistent: their ratio is the number of visits
   one run makes, about 55 million, so a ratio far from that means one of the
   two was transcribed from a different run.
3. Leave `ENVELOPE` at `2.0` unless the pool itself became twice as noisy: the
   envelope exists to absorb a heterogeneous pool, and raising it to cover a
   real regression would defeat the gate.
4. Note the change in the probe's comment block, which records each measured
   pair and the run it came from, and quote the figures in the commit message.
   A recalibration with no measurement behind it is indistinguishable from
   quietly widening the gate.

Check a change to the probe on a developer machine before pushing it, with a
report the bench already produced:

```sh
python3 benchmarks/solver/omaha_turn_probe.py \
  --bench build/soko/tools/pe-omaha-turn-bench \
  --output build/soko/turn-gate.json
python3 benchmarks/solver/omaha_turn_probe.py \
  --report build/soko/turn-gate.json --envelope 0.5   # must fail
```

The first command must pass — a workstation is faster than the pool, so its
figures are inside the envelope — and the second must fail on the timing
checks, proving the comparison is live rather than vacuous. The exact checks are
tested the same way: edit a copy of the report's `values_fingerprint` or
`deals` and confirm `--report` refuses it.

## Stacked pull requests and their base branches

A stacked pull request lives on a base that is another pull request's head
branch. GitHub does not keep that child open when its base disappears:
`gh pr merge <parent> --squash --delete-branch` deletes the branch the child
was based on, and GitHub then **closes the child** and reports it
`CONFLICTING` against a `baseRefName` that no longer exists. It looks like a
merge conflict and is not one — the child's own commits are untouched.

Neither recovery command works on its own in that state: `gh pr reopen` fails
with `Could not open the pull request`, because there is no base to open it
against, and `gh pr edit --base master` refuses with `Cannot change the base
branch of a closed pull request`. Restore the base first, from the tip it had
before the merge — it is still reachable from the child's history:

```sh
git push origin <old base tip>:refs/heads/<base>
gh pr reopen <child>
gh pr edit <child> --base master
```

The child is now open against `master` but still conflicts, because the squash
rewrote its parent's commits into one commit that `master` has and the branch
does not, even though both trees hold the same text: git compares edits against
a merge base that predates the squash, sees the same lines changed on both
sides, and cannot tell that they already agree. No rebase is needed, and none
should be used on a pushed branch, since it would need a force-push. Merge
`master` into the branch and keep the branch's side of every conflict — the
parent's content is by definition already in the child:

```sh
git merge origin/master
git checkout --ours -- <conflicted files>
git add <conflicted files> && git commit
```

Verify the result before pushing: `git diff --cached <child head before the
merge>` must be empty. An empty diff proves the merge commit carries exactly the
tree the checks already ran on, so the new CI run is a formality rather than a
first test of merged content. Then push normally, let the checks pass on the
merge commit and squash the child as usual.

When a stack is still open, merge the parent without `--delete-branch` and
delete its branch once the child is merged too; that avoids the whole sequence.

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

The parallel run they failed in has not been reproduced since, and its two
timeouts fall in two windows in which that runner made no progress at all, not
in its scheduling. Run `44828726427` timed them out at 66.9 s and 74.2 s, with
the suite totalling 1040.66 s, `test_montecarlo_convergence_unity` at 79.47 s,
and `joktest1` having already printed its last line (`joktest1: all tests
passed`) when ctest killed it. No case completes in that log between 22:26:16
and 22:27:22 (66 s), nor between 22:27:23 and 22:28:37 (74 s), and every case
caught in a window is charged the whole of it: `joktest1` 66.90 s, `enumtest5`
66.26 s and `test_montecarlo_convergence_unity` 79.47 s (it started 12.57 s
earlier) for the first; `test_enum_basic` 74.24 s, `enumtest7` 74.41 s and
`razz` 74.25 s for the second. `razz` settles it: a 0.01 s case in runs
`44888589983` and `44895169230`, it was held for 74.25 s here and passed only
because its own budget is 120 s. ctest's deadline is otherwise sharp — 30.04 s
and 180.06 s on run `44891011141` — so the budgets are not what failed. The
`CMake Release (arm64)` job of the same run, on the same image, kept completing
cases through the first window
(#21 to #199 between 22:26:20 and 22:27:07), so the stall belonged to that
runner rather than to the image or the build. What stalls the VM is not
established: the log records the gap, not its cause. The nightly arm64 leg
now prints `vm_stat`, the load average and the busiest processes every two
seconds into the suite's own log, which survives a job that reaches its time
limit, on the nightly and on a manual dispatch alike, so the next stall is
read from the system state rather than from ctest's timings.

A watcher reads those logs rather than a person: `macos-arm64-stall-check.yml`
runs on `ubuntu-latest` — no arm64 slot — and downloads the log of every
completed nightly or manual arm64 run. It fails when two consecutive samples
are more than ten seconds apart, reporting the load, `vm_stat` and the
processes sampled just before the window and naming the cases it caught —
those ctest launched between its two samples, and those still running when it
opened, from its timestamped `Start N:` and finished `Test #N:` lines; when the
first or last sample does
not bracket the suite, whose bounds are its first `Start N:` line and its
`Total Test time`; when samples exist but `Total Test time` does not, which is
what a job stopped at its time limit leaves behind; and when a nightly or
manual run has no sample at all. A run of another event, or one whose
native-test job never started, is reported and left green. Those verdicts are
locked by the fixtures under `scripts/tests/fixtures/arm64-stall-check/` and
the job in `arm64-stall-check-tests.yml`.

The log gives no slot count either: its completion lines arrive in the chunks
the runner reads, 84 of them stamped inside 4 ms at 22:28:41 with case times of
0.01 s to 1.65 s, so a burst of lines is output flushed together and not cases
launched together. The run asked for `--parallel "$(getconf
_NPROCESSORS_ONLN)"`, the runner's 3 cores, and its batches agree: the three
cases that start at 22:27:22 are the next three to complete, at 22:28:37, and
nothing starts after them until they exit. Forcing `--parallel 12` on the same 3
cores (run `44891011141`) inflated the CPU-bound cases by about five times —
`joktest1` 7.70 s,
`test_montecarlo_convergence_unity` 74.21 s, and `test_cfr_chance_nodes`,
`test_mpf_hilo8` and `test_pe_hand_features` timed out — while
`test_enum_basic` stayed at 0.01 s. Five times is not the 44 times and the 7000
times that 66.9 s and 74.2 s would need, so concurrency does not account for
them. Replaying the configuration on run `44888589983` — arm64 Debug, OpenMP
on, `--parallel` at the runner's 3 cores — passed all 390 cases in 764.46 s,
with `joktest1` at 1.52 s and `test_enum_basic` at 0.01 s. Neither test was
changed.

It stays single-threaded: the backend buys nothing for the cost that dominates,
adds an install step and breaks tests. The suite is parallelised by `ctest
--parallel` in the workflow instead, which spreads the 376 cases over the cores
and leaves the one long case as the floor. That value is the constant 4 rather
than `getconf _NPROCESSORS_ONLN`: four keeps one case per core plus one queued
on the pool's 3-core runners, and it holds that bound whatever the VM answers —
12 concurrent cases on the same 3 cores put three other cases into timeout and
slowed the rest about five times (see above).

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
