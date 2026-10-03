#!/bin/sh
# Scheduled database backup of a single-host deployment to Google Cloud Storage
# (run daily by deploy/systemd/glob2-backup.timer; see docs/hosting/README.md, "Scheduled backups"):
#
#   deploy/backup-to-gcs.sh <env-file>
#
# Takes a pg_dump (custom format) of the platform database, checks that
# pg_restore can read it, and uploads it with the list of deleted account ids
# (which a restore re-applies, see deploy/restore-backup.sh) to
#
#   gs://$GLOB2_BACKUP_BUCKET/daily/glob2-<UTC time>.dump
#   gs://$GLOB2_BACKUP_BUCKET/daily/glob2-<UTC time>.deleted-accounts.txt
#
# and copies both to weekly/ when the newest weekly backup is 7 days old or
# more (or there is none), and to monthly/ when this calendar month (UTC) has
# none yet. The bucket's lifecycle rules (deploy/gcs-backup-lifecycle.json)
# delete daily/ objects after 7 days and weekly/ after 35 (about five), and keep
# monthly/ for good. Object names are never reused, so the host only needs to
# create and list objects, not delete them.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
env_file=$(cd "$(dirname "${1:?usage: deploy/backup-to-gcs.sh <env-file>}")" && pwd)/$(basename "$1")
cd "$root"

setting() {
	sed -n "s/^$1=//p" "$env_file" | tail -n 1
}
compose() {
	docker compose -f deploy/compose.yaml --env-file "$env_file" "$@"
}

bucket=$(setting GLOB2_BACKUP_BUCKET)
if [ -z "$bucket" ]; then
	echo "backup-to-gcs: set GLOB2_BACKUP_BUCKET in $env_file" >&2
	exit 2
fi
bucket=gs://${bucket#gs://}
gcloud=${GLOB2_GCLOUD:-gcloud}

# Scratch space for the dump before it is checked and uploaded.
backups=$(setting GLOB2_BACKUP_DIR)
backups=${backups:-$(cd "$(dirname "$env_file")/.." && pwd)/backups}
scratch="$backups/scheduled"
mkdir -p "$scratch"
chmod 700 "$backups" "$scratch"
# GLOB2_BACKUP_NOW (YYYYMMDDTHHMMSSZ) stands in for the clock in tests.
stamp=${GLOB2_BACKUP_NOW:-$(date -u +%Y%m%dT%H%M%SZ)}
name="glob2-$stamp"
dump="$scratch/$name.dump"
deleted="$scratch/$name.deleted-accounts.txt"
trap 'rm -f "$dump" "$deleted"' EXIT

compose exec -T postgres pg_dump -U glob2 -Fc glob2 > "$dump"
# A truncated or empty dump fails here, before anything is uploaded.
compose exec -T postgres pg_restore --list < "$dump" > /dev/null
compose exec -T postgres psql -U glob2 -d glob2 -AtX \
	-c "SELECT id FROM accounts WHERE status = 'deleted' ORDER BY id" > "$deleted"
echo "backup: database $(du -h "$dump" | cut -f1), $(wc -l < "$deleted" | tr -d ' ') deleted account(s)"

"$gcloud" storage cp --quiet "$dump" "$deleted" "$bucket/daily/"
echo "backup: uploaded $bucket/daily/$name.dump"

# Promotes today's backup to another tier (a copy inside the bucket).
promote() {
	"$gcloud" storage cp --quiet "$bucket/daily/$name.dump" "$bucket/daily/$name.deleted-accounts.txt" "$bucket/$1/"
	echo "backup: kept as $bucket/$1/$name.dump"
}
# Newest backup time in a tier, as YYYYMMDD (empty when there is none).
newest() {
	"$gcloud" storage ls "$bucket/$1/" 2>/dev/null |
		sed -n 's|.*/glob2-\([0-9]\{8\}\)T[0-9]\{6\}Z\.dump$|\1|p' | sort | tail -n 1
}

today=${stamp%%T*}
week_ago=$(python3 -c 'import datetime, sys
day = datetime.datetime.strptime(sys.argv[1], "%Y%m%d") - datetime.timedelta(days=7)
print(day.strftime("%Y%m%d"))' "$today")
last_weekly=$(newest weekly)
if [ -z "$last_weekly" ] || [ "$last_weekly" -le "$week_ago" ]; then
	promote weekly
fi
last_monthly=$(newest monthly)
if [ "${last_monthly%??}" != "${today%??}" ]; then
	promote monthly
fi
echo "backup-to-gcs: done ($name)"
