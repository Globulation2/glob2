#!/bin/sh
# Restores a database backup into a NEW database next to the live one, brings
# it to the checkout's schema, and re-applies every account deletion made since
# the backup was taken (see docs/hosting/backups.md):
#
#   deploy/restore-backup.sh <env-file> <backup> <new-database>
#
# <backup> is a gs:// object or a local file written by deploy/backup-to-gcs.sh
# or deploy/update-host.sh (pg_dump custom format). <new-database> must not
# exist; the script never writes to the live database (`glob2`). To put a
# restored database into service, follow docs/hosting/backups.md.
#
# Deletions to re-apply: the account ids in the newest *.deleted-accounts.txt
# in $GLOB2_BACKUP_BUCKET (any tier), plus the live database's deleted accounts
# when it is still running. Each account in that list that is not deleted in
# the restored database is deleted there again with `platform admin delete`,
# which scrubs it exactly as the original deletion did.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
usage="usage: deploy/restore-backup.sh <env-file> <backup> <new-database>"
env_file=$(cd "$(dirname "${1:?$usage}")" && pwd)/$(basename "$1")
source=${2:?$usage}
target=${3:?$usage}
cd "$root"

setting() {
	sed -n "s/^$1=//p" "$env_file" | tail -n 1
}
compose() {
	docker compose -f deploy/compose.yaml --env-file "$env_file" "$@"
}
psql_on() {
	db=$1
	shift
	compose exec -T postgres psql -U glob2 -d "$db" -AtX -v ON_ERROR_STOP=1 "$@" < /dev/null
}

case "$target" in
glob2 | postgres | template0 | template1)
	echo "restore-backup: refusing to restore into '$target'; name a new database" >&2
	exit 2
	;;
esac
if ! printf '%s' "$target" | grep -Eq '^[a-z][a-z0-9_]{0,62}$'; then
	echo "restore-backup: database names are lowercase letters, digits and _" >&2
	exit 2
fi
if [ -n "$(psql_on postgres -c "SELECT 1 FROM pg_database WHERE datname = '$target'")" ]; then
	echo "restore-backup: database '$target' exists; pick a new name" >&2
	exit 2
fi
gcloud=${GLOB2_GCLOUD:-gcloud}
bucket=$(setting GLOB2_BACKUP_BUCKET)
bucket=${bucket:+gs://${bucket#gs://}}

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
chmod 700 "$work"
case "$source" in
gs://*) "$gcloud" storage cp --quiet "$source" "$work/backup.dump" ;;
*) cp "$source" "$work/backup.dump" ;;
esac

# 1. Restore into the new database (objects owned by the superuser).
psql_on postgres -c "CREATE DATABASE $target"
compose exec -T postgres pg_restore -U glob2 -d "$target" --no-owner --exit-on-error \
	< "$work/backup.dump"
echo "restore: $source -> database $target"

# 2. Bring it to this checkout's schema (a no-op for a backup of the same release).
platform_on() {
	compose run --rm --no-deps -T \
		-e DATABASE_URL="postgres://glob2:$(setting POSTGRES_PASSWORD)@postgres:5432/$target" \
		-e DATABASE_PASSWORD_FILE= platform-api "$@"
}
platform_on node /app/platform/packages/db/src/cli.ts latest

# 3. Re-apply deletions.
: > "$work/deleted"
if [ -n "$bucket" ]; then
	newest=$("$gcloud" storage ls "$bucket/**.deleted-accounts.txt" 2>/dev/null |
		awk -F/ '{print $NF "\t" $0}' | sort | tail -n 1 | cut -f2)
	if [ -n "$newest" ]; then
		"$gcloud" storage cat "$newest" >> "$work/deleted"
		echo "restore: deletions from $newest"
	fi
fi
if [ -n "$(compose ps --status running -q postgres 2>/dev/null)" ] &&
	[ -n "$(psql_on postgres -c "SELECT 1 FROM pg_database WHERE datname = 'glob2'")" ]; then
	psql_on glob2 -c "SELECT id FROM accounts WHERE status = 'deleted'" >> "$work/deleted"
	echo "restore: deletions from the live database"
fi
sort -u "$work/deleted" | grep -E '^[0-9a-f-]{36}$' > "$work/ids" || true
reapplied=0
while read -r id; do
	status=$(psql_on "$target" -c "SELECT status FROM accounts WHERE id = '$id'")
	if [ -n "$status" ] && [ "$status" != deleted ]; then
		platform_on platform admin delete "$id" \
			--reason "re-applied after restoring a backup" < /dev/null
		reapplied=$((reapplied + 1))
	fi
done < "$work/ids"
echo "restore: re-applied $reapplied deletion(s) of $(wc -l < "$work/ids" | tr -d ' ') known"
echo "restore-backup: done; database $target is ready to inspect (the live database is untouched)"
