#!/usr/bin/env bash

set -eu

warn_only=0
if [ "$#" -eq 3 ] && [ "$1" = "--warn-only" ]; then
    warn_only=1
    shift
fi
if [ "$#" -ne 2 ]; then
    echo "usage: $0 [--warn-only] BASELINE.csv CURRENT.csv" >&2
    exit 2
fi

baseline=$1
current=$2

awk -v warn_only="$warn_only" -F, '
NR == FNR {
    if (FNR == 1) next
    key = $1 "," $2 "," $3 "," $4 "," $5
    baseline[key] = $7 + 0.0
    next
}
FNR == 1 { next }
{
    key = $1 "," $2 "," $3 "," $4 "," $5
    current[key] = $7 + 0.0
    if (!(key in baseline)) {
        if (warn_only) {
            printf("WARN: no baseline row for %s\n", key) > "/dev/stderr"
        } else {
            printf("FAIL: no baseline row for %s\n", key) > "/dev/stderr"
            failed = 1
        }
        next
    }
    limit = baseline[key] * 0.90
    if (current[key] < limit) {
        if (warn_only) {
            printf("WARN: %s elements/s %.3f below 90%% baseline %.3f\n",
                   key, current[key], limit) > "/dev/stderr"
        } else {
            printf("FAIL: %s elements/s %.3f below 90%% baseline %.3f\n",
                   key, current[key], limit) > "/dev/stderr"
            failed = 1
        }
    }
}
END {
    for (key in baseline)
        if (!(key in current)) {
            if (warn_only) {
                printf("WARN: current benchmark is missing %s\n", key) > "/dev/stderr"
            } else {
                printf("FAIL: current benchmark is missing %s\n", key) > "/dev/stderr"
                failed = 1
            }
        }
    if (!failed) {
        if (warn_only)
            print "PASS: compute benchmark completed (advisory mode)"
        else
            print "PASS: compute benchmark is within the 10% regression budget"
    }
    exit (failed ? 1 : 0)
}
' "$baseline" "$current"
