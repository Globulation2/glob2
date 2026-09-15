"""Run the pinned tournament Coordinator with efficient progress/export reads.

The shared run/dispatch/accept/renew/expire/transfer methods own all work. Two
coordinator-only reporting overrides avoid repeatedly decoding every old native
map report under Python's GIL. Worker code, immutable requests, artifact checking,
leases and retries are unchanged. Full status is computed once at completion.
"""
import json
import sys
from pathlib import Path


def repair_missing_exports(coordinator):
    """Preserve repair_exports output, decoding only records needing an export.

    Most rows already have durable exports. Checking their small IDs first avoids
    loading megabytes of old terrain measurements on every host synchronization.
    Late attempts can have a transport category in SQLite but a successful native
    record; use the actual record category before writing a failure export, just
    as the shared implementation does.
    """
    from tournaments.common import atomic_json
    for row in coordinator.db.execute('SELECT id,category FROM attempts WHERE record IS NOT NULL'):
        attempt=coordinator.root/'attempts'/(row['id']+'.json')
        failure=coordinator.root/'failures'/(row['id']+'.json')
        if attempt.exists() and (row['category']=='success' or failure.exists()):
            continue
        record=json.loads(coordinator.db.execute('SELECT record FROM attempts WHERE id=?',(row['id'],)).fetchone()[0])
        if not attempt.exists():atomic_json(attempt,record)
        if record['category']!='success' and not failure.exists():atomic_json(failure,record)
    for row in coordinator.db.execute('SELECT j.id,a.id AS attempt FROM jobs j JOIN attempts a ON j.accepted=a.id'):
        path=coordinator.root/'results'/(row['id']+'.json')
        if not path.exists():
            record=json.loads(coordinator.db.execute('SELECT record FROM attempts WHERE id=?',(row['attempt'],)).fetchone()[0])
            atomic_json(path,record)


def main():
    root=Path(sys.argv[1]).resolve()
    sys.path.insert(0,str(root/'worker.pyz'))
    from tournaments.coordinator import Coordinator
    original_status=Coordinator.status
    original_repair=Coordinator.repair_exports
    # run() needs only job counts to decide when it has finished. Preserve full
    # per-host/resource accounting for the final status rather than every poll.
    Coordinator.status=lambda c: {'jobs':dict(c.db.execute('SELECT state,count(*) FROM jobs GROUP BY state').fetchall())}
    Coordinator.repair_exports=repair_missing_exports
    c=None
    try:
        c=Coordinator(root)
        c.settings.update(transfer_slots=16,heartbeat_seconds=2)
        if len(sys.argv)>3:c.settings["infrastructure_attempts"]=int(sys.argv[3])
        print(json.dumps({'execution_settings':c.settings,
            'coordinator_reporting':'job-count polling; repair missing exports only; full final status'}),flush=True)
        c.run(json.loads(Path(sys.argv[2]).read_text()))
        print(json.dumps(original_status(c)),flush=True)
    finally:
        Coordinator.status=original_status
        Coordinator.repair_exports=original_repair
        if c is not None:c.close()

if __name__=='__main__':main()
