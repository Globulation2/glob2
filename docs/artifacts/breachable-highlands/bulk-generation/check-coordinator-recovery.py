"""Compare the reporting optimization with the pinned export-repair behavior.

Use a temporary, isolated ledger with one real success and one invalid request.
No worker RPC or engine is invoked. Check both initial recovery and removal of
individual exports, so faster polling cannot silently lose review evidence.
"""
import copy
import importlib.util
import uuid
import json
from pathlib import Path
import sqlite3
import sys
import tempfile
root=Path(sys.argv[1]).resolve()
sys.path.insert(0,str(root/'worker.pyz'))
from tournaments.coordinator import Coordinator
spec=importlib.util.spec_from_file_location('study_runner',Path(__file__).with_name('run-coordinator.py'))
runner=importlib.util.module_from_spec(spec);spec.loader.exec_module(runner)
source=sqlite3.connect(f'file:{root}/state.sqlite?mode=ro',uri=True)
records=[json.loads(source.execute('SELECT record FROM attempts WHERE state=? AND category=? LIMIT 1',('accepted',cat)).fetchone()[0]) for cat in ('success','invalid_request')]
source.close()
with tempfile.TemporaryDirectory(prefix='highlands-recovery-') as temp:
    p=Path(temp)
    manifest=json.loads((root/'experiment.json').read_text())
    manifest.update(id='highlands-reporting-recovery-test',jobs=[r['job'] for r in records])
    (p/'experiment.json').write_text(json.dumps(manifest))
    for name in ('attempts','results','failures'):(p/name).mkdir()
    c=Coordinator(p)
    try:
        for r in records:
            c.db.execute('INSERT INTO attempts VALUES (?,?,?,?,?,?,?,?,?)',
                (r['id'],r['job']['id'],r['host'],r['token'],0,'accepted',json.dumps(r),0,r['category']))
            c.db.execute('UPDATE jobs SET state=?,accepted=? WHERE id=?',('completed',r['id'],r['job']['id']))
        # A late reply retains its native outcome in the record while SQLite
        # records the expired transport lease. Success must not become a failure
        # export simply because that database category is transport_failure.
        for original in records:
            r=copy.deepcopy(original);r.update(id=uuid.uuid4().hex,token=uuid.uuid4().hex,accepted=False)
            c.db.execute('INSERT INTO attempts VALUES (?,?,?,?,?,?,?,?,?)',
                (r['id'],r['job']['id'],r['host'],r['token'],0,'late',json.dumps(r),0,'transport_failure'))
        c.db.commit()
        Coordinator.repair_exports(c)
        def exported():
            return {x.relative_to(p):x.read_bytes() for folder in ('attempts','results','failures') for x in (p/folder).glob('*.json')}
        expected=exported()
        for name in expected:(p/name).unlink()
        runner.repair_missing_exports(c)
        assert exported()==expected
        for name,data in expected.items():
            (p/name).unlink();runner.repair_missing_exports(c)
            assert exported()==expected,name
        print(f'Recovery parity passed: {len(expected)} exports; initial restoration and every single missing export.')
    finally:c.close()
