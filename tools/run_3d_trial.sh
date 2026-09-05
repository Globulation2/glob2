#!/bin/sh
set -eu
trial_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$trial_root"
mkdir -p "$trial_root/.trial-user"
export GLOB2_USER_DIR="$trial_root/.trial-user"
export GLOB2_3D=1
if [ "${1:-}" = "--load" ]; then
    export GLOB2_3D_SAVE="$2"
    shift 2
fi
if [ "${1:-}" = "--quick-start" ]; then
    export GLOB2_3D_MAP="maps/A_big_pond.map"
    shift
fi
exec "$trial_root/build/src/glob2" -g -F -s 1280x800 -m "$@"
