"""Light actual-schema check only; no bootstrap, plotting, games or full corpus scan."""
from pathlib import Path
import importlib.util,json,hashlib,math,symtable,builtins
O=Path(__file__).resolve().parent;F=O.parent;C=F/'campaign';D=C/'serial'
read=lambda p:json.loads(p.read_text())
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
plan=read(C/'plan.json');variants={v['role']:v for v in read(C/'resolved-variants.json')['variants']};script=O/'analyze-playground.py'
spec=importlib.util.spec_from_file_location('independent_parser',F/'independent-audit/audit-whole-games.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
checked=[]
for role in ('baseline','wake-only','local-completion-round-robin','weighted-fixed-no-steal','weighted-last','weighted-ewma'):
 seed=plan['seeds'][0];path=D/'Playground'/f'{seed}-{role}'/'receipt.json';r=read(path);v=variants[role];dec=next(x for x in plan['declared_commands']['serial']if(x['map'],x['seed'],x['role'])==('Playground',seed,role))
 assert r['command']==dec['command']and r['compute_requested']==dec['compute']and r['allocation']==dec['allocation']and len(r['allocation'])==32 and r['binary_sha256']==v['sha256']and r['cwd']==v['cwd']and r['fixture_sha256']==plan['fixtures']
 env=dict(v.get('env',{}));env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',GLOB2_USER_DIR=str(path.parent/'profile'),GLOB2_USER_DATA_DIR=str(path.parent/'profile'),GLOB2_TEST_SEED=str(seed),GLOB2_TEST_MAX_TICKS='2147483647',GLOB2_ASSET_DIR=v['cwd']);assert env==r['env_overrides']
 log=path.parent/'stdout.log';assert sha(log)==r['stdout_sha256'];status,ticks,winner,inner,data=m.parse_raw(log.read_text(errors='replace'),'Playground',r['status']=='cap',r['returncode'],32,32);assert(status,ticks,winner,data)==(r['status'],r['ticks'],r['winner'],r['reported'])and m.close(inner,r['inner_seconds']);assert m.close(r['total_cpu_seconds'],r['user_cpu_seconds']+r['system_cpu_seconds'])and m.close(r['cpu_per_tick'],r['total_cpu_seconds']/ticks)
 name=f'wave-0-Playground-{role}.json';w=read(D/name);assert read((D/name).with_suffix('.binding.json'))['sha256']==sha(D/name);assert(w['map'],w['wave'],w['role'],w['seed_indices'])==('Playground',0,role,[0]);assert w['started_monotonic']<=r['started_monotonic']<=r['finished_monotonic']<=w['finished_monotonic']and m.close(w['makespan_seconds'],w['finished_monotonic']-w['started_monotonic'])
 checked.append({'role':role,'seed':seed,'receipt_sha256':sha(path),'stdout_sha256':sha(log),'wave_sha256':sha(D/name),'status':status})
pairs=read(D/'pairs.json');sample=[x for x in pairs if x['seed']==plan['seeds'][0]];assert len(sample)==13 and all(x['reference']=='baseline'and x['map']=='Playground'and x['seed_index']==0 for x in sample)
orders=[x for x in plan['actual_order_inventory']['serial']if x['map']=='Playground'];assert len(orders)==32 and orders[0]['roles']==plan['roles']
st=symtable.symtable(script.read_text(),str(script),'exec');known={s.get_name()for s in st.get_symbols()if s.is_assigned()or s.is_imported()or s.is_namespace()}|set(dir(builtins))|{'__file__','__name__'};unknown=[]
def walk(table):
 for s in table.get_symbols():
  if s.is_referenced()and s.is_global()and s.get_name()not in known:unknown.append((table.get_name(),s.get_name()))
 for child in table.get_children():walk(child)
walk(st);assert not unknown,unknown
sentinel=C/'serial/Oazis';installation=read(O/'sentinel-installed.json');assert sentinel.is_file()and not sentinel.is_symlink()and str(sentinel)==installation['path']and sha(sentinel)==installation['sha256']
result={'state':'light_actual_schema_preflight_passed','script_sha256':sha(script),'sample_receipts':checked,'sample_pairs':13,'order_entries':32,'unresolved_global_names':unknown,'sentinel_sha256':sha(sentinel),'limits':'Only6existingfirstseedreceipts/windows and13pairs inspected; no448validation/bootstrap/plotimports/nativejobs. Localuse-before-assignmentflow manuallyreviewed; symtable checks unresolvedglobalnames only.'};(O/'preflight-result.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS:6actualreceipts/windows+13pairs+32orderrows+sentinel; no heavy work')
