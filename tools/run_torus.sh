#!/bin/sh
# Run only this worktree's binary, with separate preferences, saves and replays.
set -eu
cd "$(dirname "$0")/.."
mkdir -p experiment/profile
export GLOB2_USER_DIR="$PWD/experiment/profile"
torus_binary=$(python3 -c 'from tools.build_paths import native_binary; print(native_binary())')
if [ ! -x "$torus_binary" ]; then
    torus_build_dir=$(python3 -c 'from tools.build_paths import native_build_directory; print(native_build_directory())')
    python3 tools/dev_build.py --build="$torus_build_dir" release=1 -j4 glob2
fi
exec "$torus_binary" play --renderer gpu --no-fullscreen --window-size 1280x800 "$@"
