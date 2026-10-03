#!/bin/sh
# Installs (or updates) the daily backup timer on a single-host deployment:
#
#   sudo deploy/install-backup-timer.sh <env-file> [user]
#
# Writes /etc/systemd/system/glob2-backup.{service,timer} from deploy/systemd/,
# running deploy/backup-to-gcs.sh from this checkout as <user> (default: the
# owner of the checkout, who must be able to run docker), and enables the timer.
# The env file must set GLOB2_BACKUP_BUCKET. See docs/hosting/backups.md.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
env_file=$(cd "$(dirname "${1:?usage: sudo deploy/install-backup-timer.sh <env-file> [user]}")" && pwd)/$(basename "$1")
user=${2:-$(stat -c %U "$root")}
units=${GLOB2_SYSTEMD_DIR:-/etc/systemd/system}

if ! grep -q '^GLOB2_BACKUP_BUCKET=.' "$env_file"; then
	echo "install-backup-timer: set GLOB2_BACKUP_BUCKET in $env_file first" >&2
	exit 2
fi
for unit in glob2-backup.service glob2-backup.timer; do
	sed -e "s|@USER@|$user|g" -e "s|@SOURCE@|$root|g" -e "s|@ENV_FILE@|$env_file|g" \
		"$root/deploy/systemd/$unit" > "$units/$unit.tmp"
	mv "$units/$unit.tmp" "$units/$unit"
done
if [ -z "${GLOB2_SYSTEMD_DIR:-}" ]; then
	systemctl daemon-reload
	systemctl enable --now glob2-backup.timer
	systemctl list-timers --no-pager glob2-backup.timer
fi
echo "install-backup-timer: daily backups of $env_file run as $user"
