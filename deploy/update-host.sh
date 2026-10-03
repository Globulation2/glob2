#!/bin/sh
# Upgrades a single-host deployment of deploy/compose.yaml from this checkout,
# optionally moving it to another git revision first:
#
#   deploy/update-host.sh <env-file> [git-ref]
#
# e.g. deploy/update-host.sh /opt/glob2/config/staging.env origin/multiplayer/staging
#
# Order, so that a failure never leaves a half-upgraded instance:
#   1. back up: a pg_dump of the database and an archive of the served web client
#      into GLOB2_BACKUP_DIR/<UTC time>/ (default: backups/ next to the env file's
#      directory; the newest GLOB2_BACKUP_KEEP, default 5, are kept);
#   2. build: the WebAssembly client (into the build tree, not yet served) and every
#      image with the checkout's sim version, while the old stack keeps running;
#      the running images are first tagged :previous;
#   3. swap: `compose up --wait` (init runs new migrations first);
#   4. install the new web client into GLOB2_WEB_CLIENT_DIR, last, so it never
#      talks to an older platform than the one it was built with.
# If the build fails, nothing running changed. If the new stack does not become
# healthy, the :previous images and the previous revision are started again and
# the web client is left as it was; the database is not rolled back automatically
# (migrations are forward-only and must keep the previous release working), but
# the script prints the restore command for the dump it took. See
# docs/hosting/README.md, "Upgrades".
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
env_file=$(cd "$(dirname "${1:?usage: deploy/update-host.sh <env-file> [git-ref]}")" && pwd)/$(basename "$1")
ref=${2:-}
cd "$root"

setting() {
	sed -n "s/^$1=//p" "$env_file" | tail -n 1
}
compose() {
	docker compose -f deploy/compose.yaml --env-file "$env_file" "$@"
}

previous=$(git rev-parse HEAD)
phase=checkout
finish() {
	status=$?
	if [ "$status" -ne 0 ] && { [ "$phase" = backup ] || [ "$phase" = build ]; }; then
		echo "update-host: the $phase failed; the running stack is unchanged (still $(git rev-parse --short "$previous"))" >&2
		git checkout --quiet --detach "$previous" || true
	fi
}
trap finish EXIT

if [ -n "$ref" ]; then
	git fetch --quiet origin
	git checkout --quiet --detach "$ref"
fi
echo "revision: $(git rev-parse --short HEAD) $(git log -1 --format=%s) (running: $(git rev-parse --short "$previous"))"

GLOB2_SIM_VERSION=$(python3 deploy/sim_version.py "$root")
export GLOB2_SIM_VERSION
echo "sim version: $GLOB2_SIM_VERSION"
web=$(setting GLOB2_WEB_CLIENT_DIR)

# ------------------------------------------------------------------ 1. backup
phase=backup
backups=$(setting GLOB2_BACKUP_DIR)
backups=${backups:-$(cd "$(dirname "$env_file")/.." && pwd)/backups}
keep=$(setting GLOB2_BACKUP_KEEP)
keep=${keep:-5}
backup="$backups/$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -p "$backup"
chmod 700 "$backups" "$backup"
echo "$previous" > "$backup/revision"
if [ -n "$(compose ps --status running -q postgres 2>/dev/null)" ]; then
	compose exec -T postgres pg_dump -U glob2 -Fc glob2 > "$backup/glob2.dump"
	echo "backup: database $(du -h "$backup/glob2.dump" | cut -f1) -> $backup/glob2.dump"
else
	echo "backup: no running database (first deployment?); no dump taken"
fi
if [ -n "$web" ] && [ -d "$web" ]; then
	tar czf "$backup/web-client.tar.gz" -C "$web" .
	echo "backup: web client -> $backup/web-client.tar.gz"
fi
# Keep the newest $keep backups.
count=$(find "$backups" -mindepth 1 -maxdepth 1 -type d | wc -l)
if [ "$count" -gt "$keep" ]; then
	find "$backups" -mindepth 1 -maxdepth 1 -type d | sort | head -n $((count - keep)) | while read -r old; do
		rm -rf "$old"
	done
fi

# ------------------------------------------------------------------- 2. build
phase=build
# The images running now, kept as :previous for a rollback.
images=$(compose config --images | sort -u)
for image in $images; do
	if docker image inspect "$image" >/dev/null 2>&1; then
		docker image tag "$image" "${image%:*}:previous"
	fi
done
if [ -n "$web" ]; then
	GLOB2_WEB_INSTALL=0 GLOB2_BUILD_JOBS=$(setting GLOB2_BUILD_JOBS) deploy/build-web-client.sh "$web"
fi
compose build

# -------------------------------------------------------------------- 3. swap
phase=swap
if ! compose up -d --wait --wait-timeout 300 --remove-orphans; then
	echo "update-host: the new stack did not become healthy; rolling back to $(git rev-parse --short "$previous")" >&2
	compose ps --format 'table {{.Service}}\t{{.State}}\t{{.Health}}' >&2 || true
	for image in $images; do
		if docker image inspect "${image%:*}:previous" >/dev/null 2>&1; then
			docker image tag "${image%:*}:previous" "$image"
		fi
	done
	git checkout --quiet --detach "$previous"
	GLOB2_SIM_VERSION=$(python3 deploy/sim_version.py "$root")
	export GLOB2_SIM_VERSION
	if compose up -d --no-build --wait --wait-timeout 300 --remove-orphans; then
		echo "update-host: rolled back; the previous release is running again" >&2
	else
		echo "update-host: the previous release did not become healthy either" >&2
	fi
	if [ -f "$backup/glob2.dump" ]; then
		cat >&2 <<EOF
update-host: the database was not rolled back. If the new release's migrations
broke the previous one, restore the dump taken before the upgrade:
  docker compose -f deploy/compose.yaml --env-file $env_file exec -T postgres \\
    pg_restore -U glob2 -d glob2 --clean --if-exists --no-owner < $backup/glob2.dump
EOF
	fi
	phase=done
	exit 1
fi

# ---------------------------------------------------------- 4. web client last
phase=install
if [ -n "$web" ]; then
	python3 deploy/install-web-client.py "$root/build/emscripten/client/release" "$web"
fi
phase=done
compose ps --format 'table {{.Service}}\t{{.State}}\t{{.Health}}'
docker image prune -f >/dev/null
echo "update-host: $(git rev-parse --short HEAD) is running; backup in $backup"
