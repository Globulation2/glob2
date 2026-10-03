#!/bin/sh
# The image's data hash (computed from its data/ at build time) until the
# binary reports its own with --sim-version; the agent refuses to start if the
# two ever disagree.
set -eu
if [ -z "${ENGINE_DATA_HASH:-}" ] && [ -z "${ENGINE_SIM_VERSION:-}" ]; then
  ENGINE_DATA_HASH="$(cat /opt/glob2/sim-data-hash)"
  export ENGINE_DATA_HASH
fi
: "${ENGINE_BUILD:=glob2-$(cat /opt/glob2/sim-version)}"
export ENGINE_BUILD
exec "$@"
