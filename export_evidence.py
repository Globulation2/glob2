#!/usr/bin/env python3
"""One-time compact export; writes only the explicitly owned /tmp evidence root."""
import collections,copy,csv,gzip,hashlib,json,os,shutil,sys,time
from pathlib import Path
ROOT=Path('/home/bradley/.codex/worktrees/b60a/glob2');AREA=ROOT/'artifacts/maxima-food-fixes';C=AREA/'broad-confirmatory-20261102';OUT=Path('/tmp/maxima-staffing-cap-evidence');OUT.mkdir(exist_ok=True);sys.path.insert(0,str(C/'worker.pyz'))
from tournaments.results import Results
from tournaments.analysis import observations
from tournaments.probability import log_integral_summary

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def put(path,data):
 p=OUT/path;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
def zput(path,data):put(path,gzip.compress(data,compresslevel=9,mtime=0))
def js(path,value):put(path,(json.dumps(value,indent=2,allow_nan=False)+'\n').encode())
def copyfile(source,target,compress=False):
 if not source.is_file():return False
 if compress:zput(target,source.read_bytes())
 else:put(target,source.read_bytes())
 return True

def main():
 s=Results(C);manifest=s.manifest;bundleid=manifest['design']['bundle'];bundlefile=C/'builds'/bundleid/'bundle.json';bundle=json.loads(bundlefile.read_text());sourcefile=AREA/'rebased-release/release-source.json';assert sha(sourcefile)==bundle['dirty_identity'];records=list(s);games=[r for r in records if r['job']['type']=='game' and r['category']=='success'];assert len(games)==3432
 for name in ['experiment.json','expected-inventory.json','freeze-validation.json','root-launch-validation.json','root-handoff.json','supervisor-finished.json','dispatch-cutoff.json','worker-stop.json','analysis-completion.json','scripts-validation.json','initial-live-score-validation.json']:
  copyfile(C/name,'protocol/'+name+'.gz',True)
 copyfile(C/'worker.pyz','provenance/worker.pyz');copyfile(bundlefile,'provenance/release-bundle-manifest.json.gz',True);copyfile(sourcefile,'provenance/frozen-experimental-source.json.gz',True);copyfile(AREA/'overnight-20261102/rebased-source-identity.json','provenance/frozen-source-identity.json.gz',True)
 for name in ['analyze.py','supervise.py','control.py','food-analysis.py','food-analysis.provenance.json','food-analysis.validation.json']:
  copyfile(C/'scripts'/name,'frozen-scripts/'+name)
 copyfile(ROOT/'docs/.work/write-maxima-confirmatory-final.py','frozen-scripts/write-final-report.py')
 for subdir in ['overnight','food-common-window','final-review']:
  for f in sorted((C/'reports'/subdir).iterdir()):
   if not f.is_file():continue
   compress=f.suffix in('.json','.csv');copyfile(f,'reports/'+subdir+'/'+f.name+('.gz' if compress else ''),compress)
 # All successful model curves and exact source endpoints, omitting unrelated performance/history dumps.
 stream=OUT/'data/score-inputs.jsonl.gz';stream.parent.mkdir(exist_ok=True);casevalues=[];artifactindex=[];recomputed=0
 keep=('team','alliance','outcome','alive','eliminated_tick','prestige','units','buildings','warrior_attack','warrior_hp','warriors','win_probability','win_probability_history')
 old=json.loads((C/'reports/overnight/summary.json').read_text());expected={g['id']:g for g in old['games']}
 with stream.open('wb') as rawout,gzip.GzipFile(fileobj=rawout,mode='wb',mtime=0,compresslevel=9) as out:
  for r in games:
   j=r['job']
   with s.open_artifact(r,'result.json') as f:raw=json.load(f)
   result={k:raw[k] for k in ('schema_version','status','ticks','termination','winning_teams','winning_alliances','win_probability_permille') if k in raw};result['teams']=[];result['players']=[{k:p[k] for k in ('player','team','ai') if k in p} for p in raw.get('players',[])]
   for team in raw['teams']:
    t={k:team[k] for k in keep if k in team};t['win_probability_log_integral']=log_integral_summary(team,raw['ticks'],j['config']['ticks']);assert t['win_probability_log_integral']['available'];result['teams'].append(t)
   compact={'job_id':j['id'],'result':result,'initialChecksum':raw.get('initialChecksum'),'finalChecksum':raw.get('finalChecksum'),'checksum_algorithm':raw.get('checksum_algorithm'),'source_result_artifact':next(a for a in r['artifacts'] if a['path']=='result.json')}
   obs=observations([{'category':'success','job':j,'result':result}],'win_probability')[0];score=obs['trajectory_scores'][obs['groups'].index(obs['subject_group'])];assert score['available'] and abs(score['score']-expected[j['id']]['trajectory_score']['score'])<1e-12
   out.write((json.dumps(compact,sort_keys=True,separators=(',',':'))+'\n').encode());recomputed+=1
   l=j['labels'];casevalues.append({'job_id':j['id'],'variant':l['variant'],'pair':l['pair'],'format':l['format'],'generator':l['generator'],'map_seed':l['map_seed'],'rotation':l['rotation'],'roster_id':l['roster_id'],'score':score['score'],'ticks':raw['ticks'],'termination':raw['termination'],'subject_group':obs['subject_group'],'initial_checksum':raw.get('initialChecksum'),'final_checksum':raw.get('finalChecksum'),'source_raw_result_sha256':compact['source_result_artifact']['raw_sha256']})
 assert recomputed==3432
 text=[]
 import io
 buffer=io.StringIO();w=csv.DictWriter(buffer,fieldnames=list(casevalues[0]));w.writeheader();w.writerows(casevalues);zput('data/per-game-values.csv.gz',buffer.getvalue().encode())
 # Exact generated map bytes are small and useful for input audit; preserve transport encodings.
 for r in records:
  if r['job']['type']!='generate_map':continue
  g=r['job'];entry={'job_id':g['id'],'config':g['config'],'seeds':g['seeds'],'category':r['category'],'diagnostic':(r.get('result') or {}).get('diagnostic'),'artifacts':[]}
  if r['category']=='success':
   for a in r['artifacts']:
    if not a['path'].endswith('.map.gz'):continue
    blob=C/'artifacts'/a['sha256'];assert sha(blob)==a['sha256'];destination='maps/artifacts/'+a['sha256'];copyfile(blob,destination);entry['artifacts'].append(a|{'evidence_path':destination})
  artifactindex.append(entry)
 js('maps/index.json',artifactindex)
 js('provenance/identity.json',{'engine_bundle_id':bundleid,'engine_source_revision':bundle['source_revision'],'engine_dirty_identity':bundle['dirty_identity'],'frozen_source_file_sha256':sha(sourcefile),'engine_options':bundle['build_options'],'platform':bundle['platform'],'engine_executable_sha256':next(f['sha256'] for f in bundle['files'] if f['path']==bundle['executable']),'worker_archive_sha256':sha(C/'worker.pyz'),'framework_package_id':manifest['package_id'],'manifest_sha256':sha(C/'experiment.json'),'original_experiment_root':str(C),'source_snapshot_exact_dirty_identity_match':True})
 # Preserve the decision to skip the separately predeclared optional cohort, not an empty synthetic report.
 secondary=AREA/'broad-full-horizon-20261102';copyfile(secondary/'experiment.json','protocol/optional-full-horizon-manifest.json.gz',True)
 gatefiles=[p for p in AREA.glob('*full-horizon*') if p.is_file() and p.suffix=='.json']
 for f in gatefiles:copyfile(f,'protocol/'+f.name)
 js('EXPORT-VALIDATION.json',{'successful_games':len(games),'official_scores_recomputed_from_exported_compact_curves_and_match_report':recomputed,'planned_jobs':len(manifest['jobs']),'generated_map_requests':len(artifactindex),'successful_map_requests':sum(x['category']=='success' for x in artifactindex),'map_artifacts':sum(len(x['artifacts']) for x in artifactindex),'preserved_worker_archive_byte_identical':sha(OUT/'provenance/worker.pyz')==sha(C/'worker.pyz'),'frozen_source_snapshot_matches_bundle_dirty_identity':True,'no_original_files_changed_or_deleted':True,'excluded':['engine executable/assets bundle','all replay/save files','all raw stdout/stderr logs','worker cache/spool','coordinator database','raw development/hypothesis notes'],'score_reproduction':'Run recompute_scores.py from this directory using the preserved archive; no engine or raw logs required.','food_reproduction_limit':'Per-case common-window measurements and original summary/coverage are retained. Aggregates can be checked from these measured sufficient data; raw stdout extraction cannot be rerun after raw logs are deleted.'})
 print(json.dumps({'exported_games':recomputed,'output':str(OUT),'maps':sum(len(x['artifacts']) for x in artifactindex),'bytes':sum(p.stat().st_size for p in OUT.rglob('*') if p.is_file())}))
if __name__=='__main__':main()
