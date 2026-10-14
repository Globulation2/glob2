"""Compatibility adapters use the same durable coordinator/worker as new scripts."""
import concurrent.futures
from pathlib import Path
import shutil
import tempfile

from .bundles import register_bundle
from .common import canonical, digest, file_hash, read_json, store_artifact
from .coordinator import Coordinator
from .cli import configure_hosts
from .resources import plan_allocations
from .model import job
from .results import Results
from .transport import Transport


def supplied_binary(binary, data_root, cache):
    """Bundle an already-built local executable and its supplied data, never build it."""
    binary, data_root, cache = Path(binary).resolve(), Path(data_root).resolve(), Path(cache).resolve()
    cache.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='glob2-supplied-') as temp:
        source=Path(temp)
        shutil.copy2(binary,source/'glob2')
        shutil.copytree(data_root/'data',source/'data')
        manifest=register_bundle(source,cache,'glob2','supplied-local',dirty_identity=file_hash(binary))
    return cache/manifest['id']


def execute_jobs(directory, jobs, bundle, slots=1, *, cpu_ids=None, participants_per_game=None):
    directory=Path(directory).resolve()
    manifest={'schema_version':1,'id':'local-'+digest([j['id'] for j in jobs])[:24], 'jobs':jobs,
              'settings':{'heartbeat_seconds':1,'lease_seconds':300}}
    host={'name':'localhost','transport':'local','directory':str(directory/'worker'),'slots':slots}
    host=configure_hosts([host],cpu_ids=cpu_ids,participants_per_game=participants_per_game)[0]
    if host.get('cpu_ids') is not None:
        plan=plan_allocations(host['cpu_ids'],slots,participants_per_game=participants_per_game).record()
        manifest['settings']['caller_allocation_plan']=plan
        manifest['id']='local-'+digest({'jobs':[j['id'] for j in jobs],'allocation':plan})[:24]
    coordinator=Coordinator.submit(directory,manifest,[bundle])
    try:
        status=coordinator.run([host])
        if status['jobs'].get('pending') or status['jobs'].get('active'):
            raise RuntimeError('local execution did not finish')
        return Results(directory)
    finally:
        transport=Transport(host,directory/'worker.pyz')
        try: transport.rpc('stop')
        finally: coordinator.close()


def run_job(binary, data_root, directory, kind, *, config, seeds, inputs=None, outputs=None, timeout=3600, labels=None,
            cpu_ids=None, participants_per_game=None):
    configure_hosts([{'slots':1}],cpu_ids=cpu_ids,participants_per_game=participants_per_game)
    directory=Path(directory).resolve()
    bundle=supplied_binary(binary,data_root,Path(data_root)/'.cache/tournament-bundles')
    artifacts={name:store_artifact(path,directory/'artifacts') for name,path in (inputs or {}).items()}
    value=job(kind,bundle.name,config=config,seeds=seeds,inputs=artifacts,outputs=outputs,
              limits={'timeout_seconds':timeout},labels=labels)
    source=execute_jobs(directory,[value],bundle,cpu_ids=cpu_ids,participants_per_game=participants_per_game)
    records=list(source)
    if records:
        return records[0],source
    attempts=[r for r in source.attempts() if r['job']['id']==value['id']]
    if not attempts: raise RuntimeError('job produced no attempt records')
    return attempts[-1],source


def export_artifacts(source, record, destination):
    destination=Path(destination)
    destination.mkdir(parents=True,exist_ok=True)
    from .common import copy_decoded, inside
    for artifact in record['artifacts']:
        copy_decoded(source.root/'artifacts'/artifact['sha256'],inside(destination,artifact['path']),artifact)


def parallel_map(work, tasks, slots):
    """Only compatibility callers need a synchronous map over durable jobs."""
    with concurrent.futures.ThreadPoolExecutor(max_workers=slots) as pool:
        yield from pool.map(work,tasks)
