#!/usr/bin/env bash

set -eu

if [ "$#" -ne 2 ]; then
    echo "usage: $0 BASELINE.csv CURRENT.csv" >&2
    exit 2
fi

baseline=$1
current=$2

awk -F, '
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
        printf("FAIL: no baseline row for %s\n", key) > "/dev/stderr"
        failed = 1
        next
    }
    limit = baseline[key] * 0.90
    if (current[key] < limit) {
        printf("FAIL: %s elements/s %.3f below 90%% baseline %.3f\n",
               key, current[key], limit) > "/dev/stderr"
        failed = 1
    }
}
END {
    for (key in baseline)
        if (!(key in current)) {
            printf("FAIL: current benchmark is missing %s\n", key) > "/dev/stderr"
            failed = 1
        }
    if (!failed)
        print "PASS: compute benchmark is within the 10% regression budget"
    exit (failed ? 1 : 0)
}
' "$baseline" "$current"
