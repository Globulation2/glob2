"""Once-only user-authorized A16 research candidate; preserves shared48 budget."""
import pathlib,sys,json,fcntl,subprocess,datetime
C=pathlib.Path(__file__).resolve().parent;D=C.parent;sys.path.insert(0,str(D/'launch-tool-source'));lock=(C/'candidate-launch.lock').open('a');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
from tools.tournaments.common import atomic_json,file_hash
from tools.tournaments.coordinator import Coordinator
assert not (C/'LAUNCHED-A16-CANDIDATE-128.json').exists(),'Already launched; never duplicate coordinator'
assert not (C/'A16-LAUNCH-INTENT.json').exists(),'Unresolved prior launchintent; failclosed'
prepared=json.loads((C/'PREPARED-A16-NOT-DISPATCHED.json').read_text());request=json.loads((C/'DEPLOYMENT-PREPARATION.json').read_text());gate=json.loads((C/'QUALIFIED-RESEARCH-CANDIDATE.json').read_text());assert prepared['qualified_gate_sha256']==file_hash(C/'QUALIFIED-RESEARCH-CANDIDATE.json')
assert gate['source_revision']==request['expected_native_source_revision']==prepared['source_revision'] and gate['threshold_milli']==16000 and gate['sim_revision']==90 and gate['save_version']==180
assert gate['model_sha256']==prepared['model_sha256']==request['expected_model_sha256'] and gate['header_sha256']==prepared['header_sha256']==request['expected_header_sha256']
assert json.loads((D.parent/'COLLECTION-HOST-POLICY.json').read_text())['mac_collection_allowed'] is False
oldhosts=json.loads((D/'hosts-remote-only.json').read_text());hosts=json.loads((C/'hosts-common-budget.json').read_text());assert len(hosts)==4 and sum(h['slots'] for h in hosts)==48
for h in hosts:
 old=next(x for x in oldhosts if x['name']==h['name']);assert h.get('transport')!='local' and {k:v for k,v in h.items() if k!='builds'}=={k:v for k,v in old.items() if k!='builds'} and set(old['builds'])<=set(h['builds'])
assert file_hash(C/'hosts-common-budget.json')==prepared['hosts_common_budget_sha256']
assert file_hash(C/'collection/worker.pyz')==file_hash(D/'exploration-remote-only/worker.pyz')==prepared['exact_existing_worker_archive_sha256']
manifest=json.loads((C/'collection/experiment.json').read_text());assert len(manifest['jobs'])==128 and manifest['settings']['process_attempts']==1
for j in manifest['jobs']:
 assert j['limits']['timeout_seconds']==1800 and j['config']['ticks']==90000 and not j['config']['resource_gradient_collection'].get('exploration') and j['labels']['resource_gradient']['cohort']['policy']['threshold_milli']==16000
post=json.loads((C/request['post_qualification_receipt']).read_text());assert post['status']==request['expected_post_qualification_status'];assert post['source']==request['expected_native_source_revision']
c=Coordinator(C/'collection',repair=False);s=c.status();c.close();assert s['jobs']=={'pending':128}
command=[sys.executable,'-m','tools.tournaments','run',str(C/'collection'),'--hosts',str(C/'hosts-common-budget.json')];atomic_json(C/'A16-LAUNCH-INTENT.json',dict(utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),experiment=s['experiment'],command=command,qualified_gate_sha256=prepared['qualified_gate_sha256'],prepared_sha256=file_hash(C/'PREPARED-A16-NOT-DISPATCHED.json'),model_sha256=prepared['model_sha256'],threshold_milli=16000,no_automatic_retry=True))
with (C/'coordinator.log').open('a') as log:p=subprocess.Popen(command,cwd=D/'launch-tool-source',stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
atomic_json(C/'LAUNCHED-A16-CANDIDATE-128.json',dict(pid=p.pid,experiment=s['experiment'],utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),command=command,cwd=str(D/'launch-tool-source'),source_revision=prepared['source_revision'],model_sha256=prepared['model_sha256'],header_sha256=prepared['header_sha256'],threshold_milli=16000,qualified_gate_sha256=prepared['qualified_gate_sha256'],intent_sha256=file_hash(C/'A16-LAUNCH-INTENT.json'),remote_slots=48,mac_slots=0,existing_exploration_unchanged=True,shared_worker_archive_sha256=prepared['exact_existing_worker_archive_sha256']))
print(p.pid,s['experiment'])
