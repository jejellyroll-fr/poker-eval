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

The concurrency group is scoped by event as well as by ref
(`macos-arm64-${{ github.event_name }}-${{ github.ref }}`), so a re-run and a
push to the same branch wait for each other instead of cancelling each other;
two pushes to the same ref still supersede each other.

### A `failure` conclusion without a single step

When GitHub has no hosted Apple Silicon runner to assign, the three jobs are
cancelled roughly twenty minutes after they are queued, none of them executes a
single step, and the run concludes `failure` even though nothing was built or
tested. Read the jobs rather than the run: a cancellation with an empty step
list and a "was not acquired by Runner of type hosted" annotation is a capacity
problem, not a code problem. Re-run that run (`gh run rerun <id>`) instead of
looking for a regression, and only start investigating when a job actually
failed a step.

### The `macos-arm64` label

Add it when the change is worth verifying on Apple Silicon before merging — the
build files, the CMake presets, the native test suite or the Python binding —
and remove it when the arm64 pool is short of runners and the jobs sit queued or
come back `cancelled`. They are not required checks, so a pull request without
the label merges on the Intel legs alone.

Adding or removing the label re-runs the workflow (`labeled`/`unlabeled`), so
neither action needs a push.
