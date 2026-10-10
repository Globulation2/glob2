# Back up and restore an instance

Preserve the database, blobs and signing keys, then verify restoration on a separate instance.

## Backups and restore

Back up the database and the blob volume together, plus the keys:

```sh
cd deploy
backup_date=$(date +%F)
docker compose exec -T postgres pg_dump -U glob2 -Fc glob2 > "glob2-$backup_date.dump"
docker compose run --rm --no-deps -T platform-worker tar czf - -C /var/lib/glob2 blobs > "blobs-$backup_date.tar.gz"
docker compose run --rm --no-deps -T init tar czf - -C /var/lib/glob2 keys relay engine-agent db > "keys-$backup_date.tar.gz"
```

`pg_dump` is consistent while the stack runs. Blobs are content-addressed and never
rewritten, so a blob archive taken right after the dump holds everything the dump
refers to. Store `keys-*.tar.gz` encrypted: it can sign tokens for any account.

To restore into a fresh stack (same `.env` and `instance.yaml`),
set `backup_date` to the date recorded in the database, blob and key archives
you created:

```sh
backup_date=YYYY-MM-DD
docker compose up -d --wait postgres
docker compose run --rm --no-deps -T init sh -c 'rm -f /var/lib/glob2/keys/*.pem /var/lib/glob2/relay/relay.key /var/lib/glob2/engine-agent/agent.key /var/lib/glob2/db/*/password; tar xzf - -C /var/lib/glob2' < "keys-$backup_date.tar.gz"
docker compose run --rm --no-deps -T platform-worker tar xzf - -C /var/lib/glob2 < "blobs-$backup_date.tar.gz"
docker compose exec -T postgres pg_restore -U glob2 -d glob2 --clean --if-exists --no-owner < "glob2-$backup_date.dump"
docker compose up -d --wait
```

`pg_restore --no-owner` leaves the restored objects owned by the superuser; `init`
hands them to `glob2_migrator` again, re-applies the grants, then applies any
migrations newer than the dump. Check the restore on a
separate host or project (`docker compose -p glob2-restore …` with other ports)
before relying on it.

### Scheduled backups

Run the setup and helper-script commands in this section from the repository root.
If you followed the manual archive procedure above, run `cd ..` first.

`deploy/backup-to-gcs.sh <env-file>` backs the database up to a Google Cloud
Storage bucket, and a systemd timer runs it every day at 03:17 UTC (plus up to ten
minutes; a run missed while the host was down happens at the next boot). Each run:

1. takes a `pg_dump -Fc` of the database and checks that `pg_restore` can read it;
2. lists the ids of deleted accounts (`<name>.deleted-accounts.txt`, for restores,
   below);
3. uploads both to `daily/glob2-<UTC time>.*`, and copies them to `weekly/` when the
   newest weekly backup is seven days old or more, and to `monthly/` when the
   calendar month (UTC) has none yet.

The bucket's lifecycle rules ([`deploy/gcs-backup-lifecycle.json`](../../deploy/gcs-backup-lifecycle.json))
do the rotation:

| Tier | Kept |
| --- | --- |
| `daily/` | 7 days (lifecycle deletion can lag by up to a day) |
| `weekly/` | 35 days, so about five weekly backups |
| `monthly/` | indefinitely, one per calendar month |

Object names are never reused, so the host only creates and lists objects; it
cannot delete or overwrite a backup. These backups hold the database only. The blob
volume (replays, records, maps) and the keys still need the manual archives above;
the blob volume is content-addressed, so an older blob archive plus a newer dump
restores everything except files added in between.

**Setting up** (once per deployment; the official instance uses the bucket
`glob2-backups-pharaoh-418820` in `northamerica-northeast2` and the service account
`glob2-staging-host`):

```sh
gcloud storage buckets create gs://BUCKET --location REGION --uniform-bucket-level-access \
    --public-access-prevention --soft-delete-duration 0 \
    --lifecycle-file deploy/gcs-backup-lifecycle.json
gcloud iam service-accounts create glob2-host
for role in roles/storage.objectCreator roles/storage.objectViewer; do
  gcloud storage buckets add-iam-policy-binding gs://BUCKET \
      --member serviceAccount:glob2-host@PROJECT.iam.gserviceaccount.com --role $role
done
# The VM must run as that account with a scope that allows writing to Cloud
# Storage (the default scopes are read-only). This needs the VM stopped:
gcloud compute instances stop glob2-host --zone ZONE
gcloud compute instances set-service-account glob2-host --zone ZONE \
    --service-account glob2-host@PROJECT.iam.gserviceaccount.com --scopes cloud-platform
gcloud compute instances start glob2-host --zone ZONE
```

The account has no project-wide role, so `cloud-platform` scope gives it nothing
beyond the bucket. Soft delete is off so that deleted backups are really gone when
the lifecycle rules remove them. Then, on the host, add
`GLOB2_BACKUP_BUCKET=BUCKET` to the env file and install the timer:

```sh
sudo deploy/install-backup-timer.sh /opt/glob2/config/staging.env
sudo systemctl start glob2-backup.service        # one backup now
journalctl -u glob2-backup.service -n 20         # its log
systemctl list-timers glob2-backup.timer         # the next run
gcloud storage ls -l gs://BUCKET/daily/ gs://BUCKET/weekly/ gs://BUCKET/monthly/
```

The units run the scripts from the checkout, so a deployment updates them; rerun
`install-backup-timer.sh` only when `deploy/systemd/` changes.

**Restoring.** `deploy/restore-backup.sh` restores into a new database next to
the live one and never writes to `glob2`:

```sh
deploy/restore-backup.sh /opt/glob2/config/staging.env \
    gs://BUCKET/daily/glob2-BACKUP_TIMESTAMP.dump glob2_restore_check
```

It creates the database, restores the dump into it, applies newer migrations, and
then **re-applies deletions**: every account listed in the newest
`*.deleted-accounts.txt` in the bucket, or deleted in the live database if that is
still running, that is not deleted in the restored copy is deleted there again
with `platform admin delete`, the same scrub as the original deletion. The one gap
is a deletion made after the newest backup when the live database is also lost:
nothing records it, so such an account comes back and has to be deleted again.

For the following Compose commands, change to `deploy/` first (`cd deploy`
from the repository root). Check the restored database (`docker compose exec postgres psql -U glob2 -d
glob2_restore_check`), then drop it, or put it into service:

```sh
docker compose stop platform-api platform-worker relay engine-agent
docker compose exec -T postgres psql -U glob2 -d postgres \
    -c 'ALTER DATABASE glob2 RENAME TO glob2_replaced' \
    -c 'ALTER DATABASE glob2_restore_check RENAME TO glob2'
docker compose up -d --wait       # init hands the objects to glob2_migrator and re-grants
```

Drop `glob2_replaced` once the instance works again.

[Hosting index](README.md) · [Documentation index](../README.md).
