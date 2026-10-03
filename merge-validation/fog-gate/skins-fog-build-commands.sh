#!/usr/bin/env bash
set -euo pipefail
export PATH="$PWD/artifacts/skins/build-tools/bin:$PATH"
GLOB2_SDL3_PREFIX="$PWD/artifacts/skins/sdk" scons release=1 -j10 build/linux/client/release/src/glob2 unit-tests engine-tests skin-game-preview skin-preview transport-test online-screens-probe
scons target=web release=1 -j10 web-serial web-threaded web-package
