#!/bin/sh
# Builds the WebAssembly client (scons target=web release=1) inside a container with
# the pinned Emscripten SDK, writes Brotli and gzip copies (browser/precompress.py),
# and installs the result into the directory Caddy serves at /play/
# (GLOB2_WEB_CLIENT_DIR) with deploy/install-web-client.py. Needs Docker and Python 3
# on the host; the SDK and the build cache persist in the glob2-web-toolchain volume
# between runs.
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
		apt-get install -y -qq --no-install-recommends ca-certificates git python3 scons xz-utils bzip2 libatomic1 brotli python3-venv cmake make >/dev/null
		git config --global --add safe.directory "*"
		python3 browser/setup.py
		# Both runtimes (serial and threaded), the loader and the page. The
		# runtime asset export installs its pinned image encoder (Pillow) into a
		# private environment under GLOB2_DEV_HOME, hence python3-venv.
		scons target=web release=1 -j"$JOBS" "$@" web-package
		python3 browser/precompress.py --require-brotli build/emscripten/client/release
		chown -R "$OWNER" build/emscripten
	' sh "$@"

python3 "$root/deploy/install-web-client.py" "$root/build/emscripten/client/release" "$output"
