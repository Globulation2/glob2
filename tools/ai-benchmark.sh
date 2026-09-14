#!/usr/bin/env bash
# Compatible flags; execution, retry and artifacts are owned by tools/tournaments.
set -eu
cd "$(dirname "$0")/.."
exec python3 -m tools.tournaments.benchmark "$@"
