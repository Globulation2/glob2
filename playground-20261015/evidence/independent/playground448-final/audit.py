from pathlib import Path
import json,hashlib,re,math,statistics,random
R=Path(__file__).resolve().parents[2];F=R/'coordination-redesign/full-design-comparison';C=F/'campaign';D=F/'playground-stop';O=Path(__file__).parent
read=lambda p:json.loads(p.read_text())
def sha(p):
 with p.open('rb')as f:return hashlib.file_digest(f,'sha256').hexdigest()
for pid in [460595,460600]:
 p=Path(f'/proc/{pid}/stat')
 assert not p.exists() or p.read_text().rsplit(')',1)[1].split()[0]=='Z'
p=read(C/'plan.json');variants={v['role']:v for v in read(C/'resolved-variants.json')['variants']};proof=read(D/'complete.json');report=read(D/'results/raw-audit.json');lookup={};roles=p['roles']
for rel,h in proof['receipt_hashes'].items():
 path=C/'serial'/rel;assert sha(path)==h;r=read(path);text=(path.parent/'stdout.log').read_text();assert sha(path.parent/'stdout.log')==r['stdout_sha256']
 rows=re.findall(r'^HISTORY_RESULT (.+)$',text,re.M);assert len(rows)==1;data=dict(z.split('=',1)for z in rows[0].split())
 for k in ['ticks','winner']:assert int(data[k])==r[k]
 assert int(data['ended'])==1 and r['status']=='winner' and r['returncode']==0
 assert int(data['compute_participants'])==int(data['eligible_cpus'])==32 and data['gradient_backend']=='cpu' and int(data['compute_requested'])==0
 v=variants[r['role']];decl=next(z for z in p['declared_commands']['serial']if(z['map'],z['seed'],z['role'])==('Playground',r['seed'],r['role']))
 assert r['binary_sha256']==v['sha256'] and r['cwd']==v['cwd'] and r['fixture_sha256']==p['fixtures'] and r['command']==decl['command']
 env=dict(v.get('env',{}));env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',GLOB2_USER_DIR=str(path.parent/'profile'),GLOB2_USER_DATA_DIR=str(path.parent/'profile'),GLOB2_TEST_SEED=str(r['seed']),GLOB2_TEST_MAX_TICKS='2147483647',GLOB2_ASSET_DIR=v['cwd']);assert r['env_overrides']==env
 assert r['allocation']==list(range(32)) and r['compute_requested']=='auto'
 assert math.isclose(float(re.search(r'HISTORY_TIMING run_wall_seconds=([\d.]+)',text)[1]),r['inner_seconds'])
 assert math.isclose(r['user_cpu_seconds']+r['system_cpu_seconds'],r['total_cpu_seconds'])
 assert math.isclose(r['finished_monotonic']-r['started_monotonic'],r['e2e_seconds']) and r['e2e_seconds']<3600
 assert r['checksum_files']==[] and r['observed_checksum']is None
 assert (r['seed'],r['role'])not in lookup;lookup[r['seed'],r['role']]=r
assert len(lookup)==448
for seed in p['seeds']:assert len({(lookup[seed,role]['ticks'],lookup[seed,role]['winner'])for role in roles})==1
prior=None
for i,seed in enumerate(p['seeds']):
 v=roles[(i//2)%14:]+roles[:(i//2)%14];v=v if i%2==0 else list(reversed(v))
 for role in v:
  r=lookup[seed,role]
  if prior is not None:assert r['started_monotonic']>=prior
  prior=r['finished_monotonic']
fields=['e2e_seconds','inner_seconds','total_cpu_seconds','user_cpu_seconds','system_cpu_seconds','peak_rss_kib']
for s in report['summary']:
 for field in fields:
  b=[lookup[seed,'baseline'][field]for seed in p['seeds']];w=[lookup[seed,s['role']][field]for seed in p['seeds']]
  assert math.isclose(100*(math.fsum(w)/math.fsum(b)-1),s['metrics'][field]['change_percent'],abs_tol=1e-10)
def contrast(reference,candidate):
 out={'reference':reference,'candidate':candidate,'metrics':{}}
 # Independent PRNG/linear quantiles: sensitivity CI, not canonical floor quantiles.
 rng=random.Random(20261015);draws=[[rng.randrange(32)for _ in range(32)]for _ in range(10000)]
 for field in ['e2e_seconds','total_cpu_seconds']:
  b=[lookup[s,reference][field]for s in p['seeds']];w=[lookup[s,candidate][field]for s in p['seeds']]
  bs=sorted(100*(math.fsum(w[i]for i in a)/math.fsum(b[i]for i in a)-1)for a in draws)
  def quant(q):z=q*9999;k=int(z);return bs[k]+(bs[min(k+1,9999)]-bs[k])*(z-k)
  out['metrics'][field]={'change_percent':100*(math.fsum(w)/math.fsum(b)-1),'CI95_independent_linear':[quant(.025),quant(.975)],'adverse_seeds':sum(y>x for x,y in zip(b,w)),'reference_mean':statistics.mean(b),'candidate_mean':statistics.mean(w)}
 return out
contrasts=[contrast('wake-only',r)for r in roles if r!='wake-only']+[contrast(r,'weighted-last')for r in ['weighted-bare-steal','weighted-clock-only','weighted-observe-only','weighted-fixed-no-steal','weighted-fixed-steal','weighted-two-choice','weighted-ewma']]
x={'state':'independently_verified_448_playground_only','runs':448,'independent_seeds':32,'roles':roles,'all_winners':448,'caps':0,'outcome_matched_seed_sets':32,'per_tick_timing_proof':False,'actual_participants':32,'actual_eligible_cpus':32,'backend':'cpu','order_pairwise_before_after':[16,16],'mean_position':6.5,'raw_point_estimates_match_report':True,'contrasts':contrasts,'bootstrap':'10000 paired-seed draws random.Random(20261015), ratio cohort sums/means, linear quantiles. Sensitivity calculation differs canonical seed605441/floorquantiles; all32independent seeds, no repeats.','report_sha256':sha(D/'results/raw-audit.json'),'stop_proof_sha256':sha(D/'complete.json'),'raw_receipt_hashes':proof['receipt_hashes'],'limits':['Onehost/Playground/unrestrictedauto32 only; user stopped before Oazis or concurrentwaves.','Timing telemetryOff: winner/tick agreement only, bounded280qualification perTick separate.','Exploratory many comparisons have no familywise adjustment; no universal optimality or adoption claim.','WholeprocessCPU includes startup/load/shutdown; no pure runCPU attribution.','Knownbaseline2enginefailures retained; originalbare suite timeout remains limitation.'],'source_sha256':sha(Path(__file__))}
(O/'review.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(contrasts,indent=2))
