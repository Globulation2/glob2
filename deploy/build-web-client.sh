#!/bin/sh
# Builds the WebAssembly client (scons target=web release=1) inside a container with
# the pinned Emscripten SDK, and copies the result to the directory Caddy serves at
# /play/ (GLOB2_WEB_CLIENT_DIR). Needs only Docker on the host; the SDK and the build
# cache persist in the glob2-web-toolchain volume between runs.
#
#   deploy/build-web-client.sh <output-dir> [scons arguments...]
#
# Extra arguments go to scons, e.g. official_instance=https://play.example.org.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
output=${1:?usage: deploy/build-web-client.sh <output-dir> [scons arguments...]}
shift
jobs=${GLOB2_BUILD_JOBS:-4}
image=${GLOB2_WEB_BUILD_IMAGE:-ubuntu:24.04}

docker run --rm \
	-v "$root":/source \
	-v glob2-web-toolchain:/toolchain \
	-e GLOB2_DEV_HOME=/toolchain/dev \
	-e JOBS="$jobs" \
	-e OWNER="$(id -u):$(id -g)" \
	-w /source \
	"$image" sh -euc '
		export DEBIAN_FRONTEND=noninteractive
		apt-get update -qq
		apt-get install -y -qq --no-install-recommends ca-certificates git python3 scons xz-utils bzip2 >/dev/null
		git config --global --add safe.directory "*"
		python3 browser/setup.py
		scons target=web release=1 -j"$JOBS" "$@" build/emscripten/client/release/index.html
		chown -R "$OWNER" build/emscripten
	' sh "$@"

release="$root/build/emscripten/client/release"
mkdir -p "$output"
# Replace each file by rename inside the served directory (Caddy bind-mounts the
# directory itself, so swapping the directory would hide the update). index.html
# goes last, after the files it loads.
for f in index.data index.wasm index.js index.html; do
	cp "$release/$f" "$output/.$f.new"
	mv -f "$output/.$f.new" "$output/$f"
done
echo "web client: $output"
