#!/usr/bin/env bash
# Decide whether the hosted macOS arm64 pool is currently serving this
# repository, so an opportunistic arm64 leg is only queued when a runner is
# likely to pick it up.
#
# Why this exists: a hosted job that never gets a runner is cancelled after
# about 15 minutes with "The job was not acquired by Runner of type hosted even
# after multiple attempts". The job ends `cancelled` with zero steps and the run
# is reported as failed. `continue-on-error` does not cover that case: the key
# downgrades `failure`, and no step ever runs in a cancelled job, so nothing
# inside the job can rescue it. The remaining lever is to not queue the leg
# while the pool is starving, which is what this probe decides from the
# repository's own job history.
#
# Policy: coverage first. The legs run unless the history shows the arm64 pool
# starving right now. One starvation cancellation teaches the next runs to stay
# away, and the first job that is assigned again switches the legs back on, so
# the gate is self-healing rather than a switch somebody has to flip.
#
# Written for the ubuntu runner that runs the `capacity` job: it needs the
# GitHub REST API, curl, jq and python3.
set -euo pipefail

window_minutes=${STARVED_WINDOW_MINUTES:-45}
# Look a little further back than the window for runs that could still contain
# a fresh cancellation, without turning the probe into a full history scan.
lookback_minutes=$((window_minutes + 60))
max_pages=${RUNS_PAGES:-3}
api=${GITHUB_API_URL:-https://api.github.com}
repo=${GITHUB_REPOSITORY:?GITHUB_REPOSITORY must be set}
: "${GH_TOKEN:?GH_TOKEN or GITHUB_TOKEN must be set}"

# Hosted macOS arm64 labels are `macos-15`, `macos-latest`, ...; the Intel
# image is `macos-15-intel` and does not suffer from this capacity problem.
arm64_label_re='^macos-(latest|[0-9]+)$'

api_get() {
  curl -fsS --retry 2 \
    -H "Authorization: Bearer ${GH_TOKEN}" \
    -H 'Accept: application/vnd.github+json' \
    -H 'X-GitHub-Api-Version: 2022-11-28' \
    "$1"
}

utc_ago() {
  python3 - "$1" <<'PY'
import datetime as dt
import sys

now = dt.datetime.now(dt.timezone.utc)
print((now - dt.timedelta(minutes=int(sys.argv[1]))).strftime("%Y-%m-%dT%H:%M:%SZ"))
PY
}

finish() {
  local available=$1 reason=$2
  {
    echo "available=${available}"
    echo "reason=${reason}"
  } >> "${GITHUB_OUTPUT:?GITHUB_OUTPUT must be set}"
  echo "macOS arm64 available: ${available} (${reason})"
  if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
    {
      echo "### macOS arm64 pool"
      echo
      echo "- verdict: **${available}**"
      echo "- reason: ${reason}"
      echo "- starvation window: ${window_minutes} min, runs scanned: ${scanned} over the last ${lookback_minutes} min"
    } >> "${GITHUB_STEP_SUMMARY}"
  fi
}

cutoff=$(utc_ago "${lookback_minutes}")

# Collect the runs that could still be relevant, newest first. Bounded paging:
# a busy repository stops at the page that crosses the cutoff.
run_ids=''
fetch_failed=0
page=1
while [ "${page}" -le "${max_pages}" ]; do
  body=$(api_get "${api}/repos/${repo}/actions/runs?per_page=30&page=${page}" 2>/dev/null || true)
  if [ -z "${body}" ]; then
    fetch_failed=1
    break
  fi
  run_ids="${run_ids} $(printf '%s' "${body}" |
    jq -r --arg cutoff "${cutoff}" '.workflow_runs[] | select(.created_at >= $cutoff) | .id')"
  oldest=$(printf '%s' "${body}" | jq -r '.workflow_runs[-1].created_at // empty')
  page=$((page + 1))
  [ -n "${oldest}" ] || break
  # Runs come back newest first: once a page starts before the cutoff there is
  # nothing older worth a jobs call.
  if [[ "${oldest}" < "${cutoff}" ]]; then
    break
  fi
done

scanned=$(printf '%s' "${run_ids}" | wc -w | tr -d ' ')
if [ "${scanned}" = "0" ]; then
  # The probe guards against a GitHub-side outage mode, it is not a correctness
  # check: when it cannot tell, keep the coverage and let the legs try.
  if [ "${fetch_failed}" = "1" ]; then
    finish true "the probe could not read this repository's run history"
  else
    finish true "no workflow run started in the last ${lookback_minutes} min, nothing to learn from"
  fi
  exit 0
fi

last_assigned=''
last_starved=''
for run_id in ${run_ids}; do
  jobs=$(api_get "${api}/repos/${repo}/actions/runs/${run_id}/jobs?per_page=100" 2>/dev/null || true)
  [ -n "${jobs}" ] || continue
  # `steps > 0` is the only proof a runner was assigned: a job cancelled before
  # assignment reports started_at equal to created_at and no runner name.
  while IFS=$'\t' read -r kind stamp; do
    [ -n "${stamp}" ] || continue
    if [ "${kind}" = "assigned" ]; then
      if [ -z "${last_assigned}" ] || [[ "${stamp}" > "${last_assigned}" ]]; then
        last_assigned=${stamp}
      fi
    elif [ "${kind}" = "starved" ]; then
      if [ -z "${last_starved}" ] || [[ "${stamp}" > "${last_starved}" ]]; then
        last_starved=${stamp}
      fi
    fi
  done < <(printf '%s' "${jobs}" | jq -r --arg re "${arm64_label_re}" '
    .jobs[]
    | select((.labels // []) | any(test($re)))
    | if (.steps | length) > 0 then ["assigned", (.started_at // .completed_at // "")]
      elif .conclusion == "cancelled" then ["starved", (.completed_at // .created_at // "")]
      else empty end
    | @tsv')
done

if [ -n "${last_starved}" ] &&
  { [ -z "${last_assigned}" ] || [[ "${last_starved}" > "${last_assigned}" ]]; }; then
  age=$(python3 - "${last_starved}" <<'PY'
import datetime as dt
import sys

stamp = dt.datetime.fromisoformat(sys.argv[1].replace("Z", "+00:00"))
print(int((dt.datetime.now(dt.timezone.utc) - stamp).total_seconds()))
PY
)
  if [ "${age}" -lt $((window_minutes * 60)) ]; then
    finish false "the last macOS arm64 job was cancelled with no runner assigned ${age}s ago"
    exit 0
  fi
  finish true "the last starvation cancellation is ${age}s old, past the ${window_minutes}-minute window"
  exit 0
fi

if [ -n "${last_starved}" ]; then
  finish true "a runner was assigned after the last macOS arm64 starvation cancellation"
  exit 0
fi

finish true "no macOS arm64 job was cancelled without a runner in the last ${lookback_minutes} min"
