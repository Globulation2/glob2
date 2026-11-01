"""Prepare, never execute, a frozen three-arm adaptive prestige tournament."""
import argparse,copy,hashlib,json,os,shutil,sys
from collections import Counter,defaultdict
from pathlib import Path
ROOT=Path(__file__).resolve().parent;sys.path.insert(0,str(ROOT.parents[2]))
from tools.tournaments.results import Results
from tools.tournaments.bundles import inspect_bundle
from tools.tournaments.common import atomic_json,file_hash
from tools.tournaments.model import job,validate_experiment
GIB=1024**3;MAX_PREP=64*1024**2
ARMS={'baseline':(False,1),'single':(True,1),'adaptive':(True,4)}
RETAIN={'1v1:symmetric-arena:1004:0','ffa:islands:1009:1'}

def values(player):
 def value(v):return v=='true' if v in ('true','false') else int(v)
 return {k:value(v) for k,v in (x.split('=',1) for x in player['runtime_values'].split(','))}
def parameter_schema(bundle):
 schema=next(a['parameters'] for a in bundle['capabilities']['ais'] if a['name']=='maxima')
 if schema.get('schemaVersion')!=2:raise ValueError('Maxima parameter catalogue schemaVersion2 required')
 specs={x['key']:x for x in schema['parameters']};p=specs.get('prestige.max_pipelines')
 if not p or p.get('hardMinimum')!=1 or p.get('hardMaximum')!=4 or p.get('type')!='integer':raise ValueError('required prestige.max_pipelines integer legal range1..4 missing')
 return specs

def prepare(source,bundle):
 specs=parameter_schema(bundle);caps=bundle['capabilities']
 if caps['save_version']!=193 or caps['sim_version']['netProtocol']!=75:raise ValueError('adaptive experiment requires save193/net75 bundle')
 revision=bundle.get('build_options',{}).get('sim_revision')
 if revision is not None and revision!=117:raise ValueError('adaptive experiment requires SIM117')
 records={r['job']['id']:r for r in source};base=[r for r in records.values() if r['job']['type']=='game' and r['job']['labels']['variant']=='baseline']
 if len(base)!=96 or any(r['category']!='success' for r in base):raise ValueError('96 original successful baseline map configurations required')
 models={t['win_probability']['model'] for r in base for t in r['result']['teams']}
 if len(models)!=1:raise ValueError('mixed original model')
 model=next(iter(models));needle=model.encode();found=False;carry=b''
 with (Path(bundle['directory'])/bundle['executable']).open('rb') as f:
  for b in iter(lambda:f.read(1024**2),b''):data=carry+b;found |= needle in data;carry=data[-len(needle):]
 if not found:raise ValueError('fixed model identity absent from executable')
 oldbundle=ROOT.parent/'bundles'/base[0]['job']['build']/'bundle.json'
 if not oldbundle.exists():raise ValueError('original stock catalogue manifest required')
 oldcatalog=json.loads(oldbundle.read_text())['capabilities']['building_catalog_hash']
 if caps['building_catalog_hash']!=oldcatalog:raise ValueError('stock building catalogue changed')
 jobs=[];objects={};mapping=[]
 for r in base:
  old=r['job'];labels=old['labels'];pair=labels['pair'];ref=old['inputs']['map'];parent=records[ref['job']]
  meta=next(x for x in parent['artifacts'] if x['path']==ref['artifact']);obj=source.root/'artifacts'/meta['sha256']
  if not meta['path'].endswith('.gz') or obj.stat().st_size!=meta['bytes'] or file_hash(obj)!=meta['sha256']:raise ValueError('invalid original compressed map metadata/object')
  h=hashlib.sha256();size=0
  with source.open_artifact(parent,meta['path'],'rb') as stream:
   for block in iter(lambda:stream.read(1024**2),b''):h.update(block);size+=len(block)
  if h.hexdigest()!=meta['raw_sha256'] or size!=meta['raw_bytes']:raise ValueError('decoded map mismatch')
  objects[meta['sha256']]={'source':obj,'meta':copy.deepcopy(meta)}
  original_params={str(p['team']):values(p) for p in r['result']['players']}
  for p,config in original_params.items():
   missing=set(specs)-set(config)-{'prestige.max_pipelines'}
   if missing:raise ValueError('new schema parameters lack frozen original values: '+str(sorted(missing)))
   for key,value in config.items():
    if key not in specs:raise ValueError('original frozen parameter absent from new schema: '+key)
    spec=specs[key]
    if spec['type']=='integer' and not spec['hardMinimum']<=value<=spec['hardMaximum']:raise ValueError('original parameter outside new legal domain: '+key)
  for arm,(enabled,maximum) in ARMS.items():
   config=copy.deepcopy(old['config']);config['ai_params']=copy.deepcopy(original_params)
   for p,params in config['ai_params'].items():params.update({'prestige.enabled':enabled if p=='0' else False,'prestige.max_pipelines':maximum if p=='0' else 1})
   assert config['ticks']==90000 and config['win_probability_permille']==0 and old['seeds']=={'game':19}
   outputs={'result':'full','telemetry':['maxima'],'log_mode':'telemetry'}
   if pair in RETAIN:outputs.update(replay=True,saves=['initial','final'])
   lab=copy.deepcopy(labels);lab.update(variant=arm,arm=arm)
   fresh=job('game',bundle['id'],inputs={'map':copy.deepcopy(meta)},seeds=copy.deepcopy(old['seeds']),config=config,outputs=outputs,limits={'timeout_seconds':3600,'artifact_bytes':128*1024**2,'log_bytes':32*1024**2},labels=lab)
   jobs.append(fresh);mapping.append({'job':fresh['id'],'arm':arm,'pair':pair,'original_map_config_job':old['id'],'map_generation_job':ref['job'],'map':copy.deepcopy(meta)})
 if len(jobs)!=288 or len(objects)!=96 or len({x['labels']['pair'] for x in jobs})!=96:raise ValueError('three-arm design support changed')
 manifest=validate_experiment({'schema_version':1,'id':'maxima-adaptive-prestige-v2','kind':'ablations','jobs':jobs,'settings':{'artifact_budget_bytes':3*GIB,'disk_reserve_bytes':2*GIB},'design':{'arms':{arm:{'enabled':enabled,'max_pipelines':maximum} for arm,(enabled,maximum) in ARMS.items()},'games':288,'pairs':96,'seed_blocks_per_format':16,'all_original_rotations_retained':True,'original_manifest_sha256':file_hash(source.root/'experiment.json'),'frozen_model':model,'building_catalog_hash':oldcatalog,'weights':'every original reported runtime parameter is explicitly supplied; only enabled/max_pipelines differ by arm','sim_revision':117,'save_version':193,'net_protocol':75,'stopping_rule':'all288success; no efficacy early stopping; cap90000; probabilityearly0; naturalprestige','diagnostic_pairs':sorted(RETAIN),'diagnostic_games':6,'no_historical_record_used_as_control':True}})
 return manifest,objects,mapping

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--bundle',type=Path,required=True);p.add_argument('--source',type=Path,default=ROOT.parent/'results-final');p.add_argument('--output',type=Path,default=ROOT);p.add_argument('--validate-only',action='store_true');a=p.parse_args()
 source=Results(a.source);bundle=inspect_bundle(a.bundle);manifest,objects,mapping=prepare(source,bundle);out=a.output.resolve();cpus=sorted(os.sched_getaffinity(0))
 if not set(range(16))<=set(cpus):raise ValueError('CPUs0..15 unavailable')
 hosts=[{'name':f'localhost-adaptive-{i}','transport':'local','directory':str(out/f'worker-{i}'),'slots':8,'cpu_ids':list(range(i*8,(i+1)*8)),'participants_per_game':1,'transport_timeout_seconds':300,'attempt_budget_bytes':128*1024**2,'log_budget_bytes':32*1024**2,'spool_budget_bytes':2*GIB,'cache_budget_bytes':3*GIB,'disk_reserve_bytes':2*GIB} for i in range(2)]
 receipt={'bundle':bundle['id'],'bundle_directory':bundle['directory'],'source_manifest_sha256':file_hash(source.root/'experiment.json'),'jobs':mapping,'rotated_maps':len(objects),'games':288,'map_transport_bytes':sum(o['meta']['bytes'] for o in objects.values()),'cpu_ids_verified':cpus,'import_metadata_includes_path':True}
 estimate=len(json.dumps(manifest).encode())+len(json.dumps(receipt).encode())+receipt['map_transport_bytes']
 if estimate>MAX_PREP:raise ValueError('64MiB preparation allowance exceeded')
 if not a.validate_only:
  out.mkdir(parents=True,exist_ok=True)
  if shutil.disk_usage(out).free<2*GIB+estimate:raise ValueError('2GiB reserve unavailable')
  if (out/'results/experiment.json').exists():raise ValueError('cohort already submitted; immutable')
  if (out/'planned.json').exists() and json.loads((out/'planned.json').read_text())!=manifest:raise ValueError('existing plan differs; new cohort directory required')
  artifacts=out/'results/artifacts';artifacts.mkdir(parents=True,exist_ok=True)
  for identity,o in objects.items():
   dst=artifacts/identity
   if not dst.exists():shutil.copyfile(o['source'],dst)
   if file_hash(dst)!=identity:raise ValueError('import checksum mismatch')
  atomic_json(out/'planned.json',manifest);atomic_json(out/'hosts.json',hosts);atomic_json(out/'provenance.json',receipt)
 print(json.dumps({'bundle':bundle['id'],'games':288,'rotated_maps':len(objects),'diagnostic_replay_save_games':6,'preparation_bytes':estimate,'validated_only':a.validate_only,'output':str(out)},indent=2))
if __name__=='__main__':main()
