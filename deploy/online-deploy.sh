#!/bin/sh
# Host side of .github/workflows/deploy-online.yml: starts deploy/update-host.sh
# detached on the host and reports on it, so the workflow's runner only has to
# poll. The workflow pipes this file over SSH (`sh -s -- <command> ...`), so the
# version that runs is the one from the workflow's commit, not the host's
# checkout. It can also be run by hand on the host.
#
#   online-deploy.sh start  <env-file> <commit> <id>   refuse if a deploy runs; fetch and
#                                                      check out <commit>; start update-host.sh
#   online-deploy.sh status <env-file> <id>            key=value lines (state, exit, rollback, ...)
#   online-deploy.sh log    <env-file> <id> [lines]    the end of the deploy's log
#   online-deploy.sh smoke  <env-file> <id> [website]  platform_stack_smoke.py --attach
#   online-deploy.sh current <env-file> [commit]       deployed=<recorded revision>, busy=yes|no,
#                                                      contains=yes|no (deployed contains commit)
#
# Layout, all relative to the directory above the env file's directory (e.g.
# /opt/glob2 for /opt/glob2/config/staging.env): the checkout in src/
# (GLOB2_ONLINE_SRC overrides), the deployed-revision record update-host.sh
# keeps, and one directory per run in deploys/<id>/ with the commit, the log,
# the exit status and the smoke results. See docs/hosting/README.md,
# "Automatic deployment".
set -eu

command=${1:?usage: online-deploy.sh start|status|log|smoke|current <env-file> ...}
env_file=${2:?missing env file}
[ -f "$env_file" ] || { echo "online-deploy: no env file $env_file" >&2; exit 2; }
host=$(cd "$(dirname "$env_file")/.." && pwd)
src=${GLOB2_ONLINE_SRC:-$host/src}
runs=$host/deploys
lock=$host/deploy.lock
project=${GLOB2_ONLINE_PROJECT:-glob2-platform}

setting() {
	sed -n "s/^$1=//p" "$env_file" | tail -n 1
}
record=$(setting GLOB2_DEPLOYED_REVISION_FILE)
record=${record:-$host/deployed-revision}

# A deploy started by this script, or by hand as staging-ops does
# (`deploy/update-host.sh ...` runs as `/bin/sh deploy/update-host.sh ...`).
busy() {
	if pgrep -f '^/bin/sh ([^ ]*/)?deploy/update-host\.sh' >/dev/null 2>&1; then
		return 0
	fi
	if command -v flock >/dev/null 2>&1 && [ -e "$lock" ]; then
		if ! flock -n "$lock" true; then
			return 0
		fi
	fi
	return 1
}

run_dir() {
	id=${1:?missing run id}
	case "$id" in
		*[!A-Za-z0-9._-]*|.*) echo "online-deploy: bad run id $id" >&2; exit 2 ;;
	esac
	echo "$runs/$id"
}

case "$command" in
current)
	deployed=$(head -n 1 "$record" 2>/dev/null || true)
	echo "deployed=$deployed"
	if busy; then echo busy=yes; else echo busy=no; fi
	# With a commit: whether the deployed revision already contains it (the
	# same commit or an older one), so an automatic deploy can skip it.
	if [ -n "${3:-}" ] && [ -n "$deployed" ]; then
		cd "$src"
		git cat-file -e "$3^{commit}" 2>/dev/null || git fetch --quiet origin || true
		if git merge-base --is-ancestor "$3" "$deployed" 2>/dev/null; then
			echo contains=yes
		else
			echo contains=no
		fi
	fi
	;;

start)
	commit=${3:?missing commit}
	dir=$(run_dir "${4:-}")
	case "$commit" in
		*[!0-9a-f]*) echo "online-deploy: $commit is not a full commit id" >&2; exit 2 ;;
	esac
	[ "${#commit}" -eq 40 ] || { echo "online-deploy: $commit is not a full commit id" >&2; exit 2; }
	# A retried SSH connection may repeat the start of the same run.
	if [ -e "$dir" ]; then
		if [ "$(cat "$dir/commit" 2>/dev/null)" = "$commit" ]; then
			echo "online-deploy: this run was already started"
			exit 0
		fi
		echo "online-deploy: $dir exists for another commit" >&2
		exit 2
	fi
	if busy; then
		echo "online-deploy: another deploy is running on this host; not starting" >&2
		exit 75
	fi
	cd "$src"
	git fetch --quiet origin
	if ! git cat-file -e "$commit^{commit}" 2>/dev/null; then
		git fetch --quiet origin "$commit"
	fi
	git cat-file -e "$commit^{commit}"
	# update-host.sh rolls back to the recorded revision; without a record it
	# would take the checkout's HEAD, which the pre-checkout below moves.
	if [ ! -s "$record" ]; then
		git rev-parse HEAD > "$record"
		echo "online-deploy: no deployed-revision record; recorded the running $(git rev-parse --short HEAD)"
	fi
	running=$(head -n 1 "$record")
	# Check out first, so the newest update-host.sh runs (it checks out again).
	git checkout --quiet --detach "$commit"
	mkdir -p "$dir"
	echo "$commit" > "$dir/commit"
	echo "$running" > "$dir/previous"
	date -u +%Y-%m-%dT%H:%M:%SZ > "$dir/started"
	# The deploy outlives this SSH session and the workflow run.
	# shellcheck disable=SC2016 # expanded by the inner shell
	nohup sh -c '
		exec 9>"$1"
		if command -v flock >/dev/null 2>&1 && ! flock -n 9; then
			echo "online-deploy: another deploy holds $1"
			echo 75 > "$5.tmp" && mv "$5.tmp" "$5"
			exit 75
		fi
		cd "$2" && deploy/update-host.sh "$3" "$4"
		status=$?
		date -u +%Y-%m-%dT%H:%M:%SZ > "$6"
		echo "$status" > "$5.tmp" && mv "$5.tmp" "$5"
		echo "DEPLOY EXIT $status"
	' online-deploy "$lock" "$src" "$env_file" "$commit" "$dir/exit" "$dir/finished" \
		> "$dir/log" 2>&1 < /dev/null &
	echo "$!" > "$dir/pid"
	echo "online-deploy: started $(git log -1 --format='%h %s' "$commit") (running: $(git rev-parse --short "$running")); log $dir/log"
	;;

status)
	dir=$(run_dir "${3:-}")
	[ -d "$dir" ] || { echo "state=missing"; exit 0; }
	if [ -f "$dir/exit" ]; then
		state='done'
		status=$(cat "$dir/exit")
	elif [ -f "$dir/pid" ] && kill -0 "$(cat "$dir/pid")" 2>/dev/null; then
		state=running
		status=
	else
		state=lost
		status=
	fi
	rollback=
	if [ "$state" = "done" ]; then
		if [ "$status" = 0 ]; then
			rollback=none
		elif grep -q 'update-host: rolled back; the previous release is running again' "$dir/log"; then
			rollback=rolled-back
		elif grep -q 'the previous release did not become healthy either' "$dir/log"; then
			rollback=failed
		elif grep -Eq 'update-host: the (backup|build) failed; the running stack is unchanged' "$dir/log" \
			|| ! grep -q '^revision: ' "$dir/log"; then
			rollback=unchanged
		else
			rollback=unknown
		fi
	fi
	echo "state=$state"
	echo "exit=$status"
	echo "rollback=$rollback"
	echo "commit=$(cat "$dir/commit")"
	echo "previous=$(cat "$dir/previous")"
	echo "deployed=$(head -n 1 "$record" 2>/dev/null || true)"
	echo "started=$(cat "$dir/started")"
	echo "finished=$(cat "$dir/finished" 2>/dev/null || true)"
	echo "backup=$(sed -n 's/^update-host: .* is running; backup in //p' "$dir/log" | tail -n 1)"
	echo "sim_version=$(sed -n 's/^sim version: //p' "$dir/log" | tail -n 1)"
	;;

log)
	dir=$(run_dir "${3:-}")
	tail -n "${4:-60}" "$dir/log"
	;;

smoke)
	dir=$(run_dir "${3:-}")
	website=${4:-}
	cd "$src"
	if [ "$(git rev-parse HEAD)" != "$(cat "$dir/commit")" ]; then
		echo "online-deploy: the checkout is not at this run's commit; not smoke testing" >&2
		exit 2
	fi
	set -- --attach "$project" --env-file "$env_file" --log-dir "$dir/smoke"
	if [ -n "$website" ]; then
		set -- "$@" --website "$website"
	fi
	status=0
	python3 tests/deployment/platform_stack_smoke.py "$@" || status=$?
	echo "$status" > "$dir/smoke-exit"
	exit "$status"
	;;

*)
	echo "online-deploy: unknown command $command" >&2
	exit 2
	;;
esac
