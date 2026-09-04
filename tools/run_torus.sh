#!/bin/sh
# Run only this worktree's binary, with separate preferences, saves and replays.
set -eu
cd "$(dirname "$0")/.."
mkdir -p experiment/profile
export GLOB2_USER_DIR="$PWD/experiment/profile"
if [ ! -x build/src/glob2 ]; then
    scons release=1 -j4 build/src/glob2
fi
exec "$PWD/build/src/glob2" -g -F -s 1280x800 "$@"
