#!/bin/bash
# Execute only after root's explicit canonical-catalog freeze signal.
set -u
cd /app/platform
mkdir -p /evidence/logs
printf '%s\n' 'step,exit_code,started_utc,finished_utc' > /evidence/steps.csv
run_step() {
    local step="$1"; shift
    local started finished code
    started=$(date -u +%Y-%m-%dT%H:%M:%SZ)
    printf 'Starting %s at %s\n' "$step" "$started"
    timeout --signal=TERM --kill-after=30s 3600 "$@" > "/evidence/logs/$step.log" 2>&1
    code=$?
    finished=$(date -u +%Y-%m-%dT%H:%M:%SZ)
    printf '%s,%s,%s,%s\n' "$step" "$code" "$started" "$finished" >> /evidence/steps.csv
    printf 'Finished %s: exit %s at %s\n' "$step" "$code" "$finished"
}
# Equivalent full npm check stages, with explicit Vitest worker bound.
# Independent stages continue to retain all failure evidence in one run.
run_step lint npm run lint
run_step typecheck npm run typecheck
run_step i18n npm run i18n:check
run_step vitest npm test -- --maxWorkers=3
awk -F, 'NR>1 && $2!=0 {failed=1} END {exit failed}' /evidence/steps.csv
