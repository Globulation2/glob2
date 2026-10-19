"""Prepare only: exact qualified A16 candidate using unchanged active worker."""
import pathlib,sys,json,copy,collections,datetime,shutil
C=pathlib.Path(__file__).resolve().parent;D=C.parent;sys.path.insert(0,str(D/'launch-tool-source'))
from tools.adaptive_gradient_tournament import validate_plan,accepted_routing,ADAPTIVE_POLICY
from tools import adaptive_gradient_spatial as collector
from tools.resource_gradient_distributed import make_experiment
from tools.resource_gradient_tournament import map_identity,digest
from tools.tournaments.common import file_hash,store_artifact,atomic_json
from tools.tournaments.bundles import inspect_bundle
from tools.tournaments.coordinator import Coordinator
assert json.loads((D.parent/'COLLECTION-HOST-POLICY.json').read_text())['mac_collection_allowed'] is False
gate_path=C/'QUALIFIED-RESEARCH-CANDIDATE.json';gate=json.loads(gate_path.read_text());request=json.loads((C/'DEPLOYMENT-PREPARATION.json').read_text());assert gate['source_revision']==request['expected_native_source_revision'] and gate['sim_revision']==request['expected_sim_revision'] and gate['save_version']==request['expected_save_version'];assert gate['model_sha256']==request['expected_model_sha256'] and gate['header_sha256']==request['expected_header_sha256'];assert gate['status']=='QUALIFIED-FOR-RESEARCH-CANDIDATE-ONLY' and gate['threshold_milli']==16000 and gate['collector_contract']=='unified-gradient-spatial-h128-v3'
legacy_contract=json.loads((D.parent/'round-004/candidate-gen004/collection/experiment.json').read_text())['jobs'][0]['labels']['resource_gradient']['cohort']['contract']
metadata=json.loads((C/request['qualified_linux_bundle_metadata']).read_text());paths=[C/'bundles'/metadata[pool]['id'] for pool in ['small-pharaoh','middle-linux']];bundles=[inspect_bundle(p) for p in paths]
for b in bundles:
 o=b['build_options'];assert b['source_revision']==gate['source_revision'] and o['model_sha256']==gate['model_sha256'] and o['header_sha256']==gate['header_sha256'] and o['refresh_threshold_milli']==16000 and o['unified_model_trained']
 assert o['adaptive_gradient_contract_sha256']==digest(legacy_contract),'Exact legacy collector declaration required by frozen worker bridge'
 assert file_hash(pathlib.Path(b['directory'])/b['executable'])==gate['linux_binary_sha256']
plan=json.loads((D/'plan-256.json').read_text());validate_plan(plan);maps=json.loads((D/'accepted-maps.json').read_text());old=json.loads((D/'actual-routing.json').read_text());new=old;assert old==accepted_routing(plan,maps) and old['routing_mode']=='remote-only'
aliasdoc=json.loads((D/'LOCAL-MAP-ALIASES.json').read_text());assert aliasdoc['accepted_original_sha256']==file_hash(D/'accepted-maps.json');aliases={r['group']:r for r in aliasdoc['aliases']}
registry=json.loads((D/'map-splits.json').read_text())['assignments'];index={r['group']:r for r in maps};effective={r['group']:r for r in new['assignments']};slots=[r for r in plan['unified']['slots'] if r['role']=='candidate'];assert len(slots)==128 and set(collections.Counter(r['spec']['format'] for r in slots).values())=={16}
base=dict(ADAPTIVE_POLICY,threshold_milli=16000);policy=dict(**base,name='unified-adaptive16',model_sha256=gate['model_sha256'],seed=181105,intervals=[]);label_contract=copy.deepcopy(legacy_contract);label_contract.pop('refresh_threshold_milli',None)
cohort=dict(id='unified-h128-round005-candidate-gen005-a16-refit1024-remote-only',role='candidate',policy=policy,header_sha256=gate['header_sha256'],contract=copy.deepcopy(legacy_contract),label_contract_sha256=digest(label_contract),collector_declaration_note='Unchanged frozen legacy collector declaration retains historical128000field; policy.threshold_milli16000 is actual qualified behavior, verified by runtime metadata. Label semantics H128 unchanged.')
selected=[]
for r in slots:
 item=copy.deepcopy(index[r['group']]);assert map_identity(item['spec']) in registry;alias=aliases[r['group']];assert alias['canonical_remote_path']==item['path'] and alias['sha256']==item['sha256'] and file_hash(alias['local_path'])==item['sha256'];item['path']=alias['local_path'];item['cohort']=cohort;item['build']=next(b['id'] for b in bundles if b['build_options']['execution_pool']==effective[r['group']]['actual_pool']);selected.append(item)
subplan=dict(games=[r['spec'] for r in sorted(slots,key=lambda r:(-index[r['group']]['spec']['width']*index[r['group']]['spec']['height']*index[r['group']]['spec']['players'],r['group']))]);manifest=make_experiment(subplan,selected,bundles,cohort,ticks=90000,timeout_seconds=1800);manifest['settings']['process_attempts']=1;manifest['unified_routing']=copy.deepcopy(plan['unified']['routing']);manifest['unified_actual_routing']=copy.deepcopy(old)
for j in manifest['jobs']:
 label=j['labels']['resource_gradient'];label.update(persistent_split_registry_sha256=file_hash(D/'map-splits.json'),routing=copy.deepcopy(plan['unified']['routing']),actual_routing=copy.deepcopy(effective[j['id']]),actual_routing_sha256=digest(new),map_alias_manifest_sha256=file_hash(D/'LOCAL-MAP-ALIASES.json'),qualified_threshold_milli=16000,qualified_gate_sha256=file_hash(gate_path))
output=C/'collection'
for r in selected:assert store_artifact(r['path'],output/'artifacts',compress=False)['sha256']==r['sha256']
if not (output/'experiment.json').exists():
 c=Coordinator.submit(output,manifest,[str(p) for p in paths]);c.close()
shutil.copyfile(D/'exploration-remote-only/worker.pyz',output/'worker.pyz')
oldhosts=json.loads((D/'hosts-remote-only.json').read_text());poolids={b['build_options']['execution_pool']:b['id'] for b in bundles}
for h in oldhosts:
 pool='small-pharaoh' if h['name'].startswith('pharaoh') else 'middle-linux';h['builds']=sorted(set(h['builds'])|{poolids[pool]})
atomic_json(C/'hosts-common-budget.json',oldhosts)
c=Coordinator(output,repair=False);s=c.status();c.close();assert s['jobs']=={'pending':128}
atomic_json(C/'PREPARED-A16-NOT-DISPATCHED.json',dict(status='PREPARED-QUALIFIED-A16-NOT-DISPATCHED',utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),experiment=s['experiment'],cohort=cohort['id'],slots=128,threshold_milli=16000,source_revision=gate['source_revision'],model_sha256=gate['model_sha256'],header_sha256=gate['header_sha256'],bundle_ids=[b['id'] for b in bundles],qualified_gate_sha256=file_hash(gate_path),actual_routing_sha256=file_hash(D/'actual-routing.json'),hosts_common_budget_sha256=file_hash(C/'hosts-common-budget.json'),exact_existing_worker_archive_sha256=file_hash(output/'worker.pyz'),legacy_collector_contract_sha256=digest(legacy_contract),normalized_label_contract_sha256=digest(label_contract),remote_slots=48,mac_slots=0,interval_override=None,dispatch=False))
print(json.dumps(dict(experiment=s['experiment'],pending=128,threshold_milli=16000,dispatched=False)))
