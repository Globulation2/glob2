#!/usr/bin/env bash
# compare.sh OUTROOT LABEL=BINARY[:ENV=VALUE] ... : zoom+pan motion and 500% pan per variant, sequentially.
set -eo pipefail
dir="$(cd "$(dirname "$0")" && pwd)"
root="$1"; shift
for spec in "$@"; do
  label="${spec%%=*}" rest="${spec#*=}" binary="${rest%%:*}" extra=""
  [[ "$rest" == *:* ]] && extra="${rest#*:}"
  for run in motion pan5; do
    out="$root/$label-$run"
    if [[ $run == motion ]]; then
      summary=$(env $extra "$dir/motion.sh" "$binary" "$out" 1.25 | tail -1)
    else
      summary=$(env $extra CAMERA=PAN "$dir/motion.sh" "$binary" "$out" 5 | tail -1)
    fi
    echo "$label $run $summary"
  done
done
