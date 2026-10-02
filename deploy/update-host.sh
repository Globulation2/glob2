#!/bin/sh
# Builds and (re)starts a single-host deployment of deploy/compose.yaml from this
# checkout, optionally moving it to another git revision first:
#
#   deploy/update-host.sh <env-file> [git-ref]
#
# e.g. deploy/update-host.sh /opt/glob2/config/staging.env origin/multiplayer/staging
#
# It builds the WebAssembly client into GLOB2_WEB_CLIENT_DIR when the env file sets
# it, builds every image with the checkout's sim version, starts the stack and waits
# until every service is healthy. Data volumes are kept. See docs/hosting/README.md.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
env_file=$(cd "$(dirname "${1:?usage: deploy/update-host.sh <env-file> [git-ref]}")" && pwd)/$(basename "$1")
ref=${2:-}
cd "$root"

if [ -n "$ref" ]; then
	git fetch --quiet origin
	git checkout --quiet --detach "$ref"
fi
echo "revision: $(git rev-parse --short HEAD) $(git log -1 --format=%s)"

setting() {
	sed -n "s/^$1=//p" "$env_file" | tail -n 1
}

GLOB2_SIM_VERSION=$(python3 deploy/sim_version.py "$root")
export GLOB2_SIM_VERSION
echo "sim version: $GLOB2_SIM_VERSION"

web=$(setting GLOB2_WEB_CLIENT_DIR)
if [ -n "$web" ]; then
	GLOB2_BUILD_JOBS=$(setting GLOB2_BUILD_JOBS) deploy/build-web-client.sh "$web"
fi

compose() {
	docker compose -f deploy/compose.yaml --env-file "$env_file" "$@"
}
compose build
compose up -d --wait --wait-timeout 300 --remove-orphans
compose ps --format 'table {{.Service}}\t{{.State}}\t{{.Health}}'
docker image prune -f >/dev/null
