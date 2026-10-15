import pathlib,json,shutil,hashlib,importlib.util,copy,sys
P=pathlib.Path('/home/bradley/.codex/worktrees/a630/glob2/artifacts/october-regression/coordination-redesign'); T=P/'review-evidence/portable-tools'; Q=T/'qualification'; D=Q/'relocated-bundle'
D.mkdir(); F=D/'evidence/confirmation/final-cohort'; F.mkdir(parents=True); (D/'tools').mkdir()
shutil.copyfile(T/'recompute-final.py',D/'tools/recompute-final.py')
for name in ['plan.json','launcher.py']:shutil.copyfile(P/'confirmation/final-cohort'/name,F/name)
R=D/'evidence/confirmation/wake-only-allcore';R.mkdir();shutil.copyfile(P/'confirmation/wake-only-allcore/latest-resolve-complete.json',R/'latest-resolve-complete.json')
for regime in ['serial','waves']:
 src=P/'confirmation/final-cohort'/regime
 files=[src/'complete.json',src/'analysis.json',*src.glob('wave-*.json'),*src.glob('*/*/receipt.json'),*src.glob('*/*/stdout.log')]
 for s in files:
  d=F/regime/s.relative_to(src);d.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(s,d)
spec=importlib.util.spec_from_file_location('portable',T/'recompute-final.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
plan,c,rows,_=m.load_regime(F,'serial'); checks=[]
for status in ['cap','no_winner']:
 cc=copy.deepcopy(c);rr=copy.deepcopy(rows);r=next(x for x in rr if x['map']=='Playground' and x['role']=='wake-only');r['status']=status;r['winner']=None
 pair=next(x for x in cc['pairs'] if x['map']==r['map'] and x['seed']==r['seed']);pair.update(both_winners=False,endpoints_match=None,eligible_winner_timing_pair=False)
 out=m.recompute(plan,cc,rr,'serial');a=out['maps'][0];assert a['metrics']['e2e_seconds']['eligible']==31
 checks.append({'case':status,'result':'passed','eligible':31})
rr=copy.deepcopy(rows)
for r in rr:r['voluntary_context_switches']=0
out=m.recompute(plan,c,rr,'serial');v=out['maps'][0]['metrics']['voluntary_context_switches'];assert v['ratio_of_means_change'] is None and v['paired_bootstrap95'] is None and v['undefined_zero_reference_seed_draws']==10000
checks.append({'case':'zero reference','result':'passed'})
text='HISTORY_RESULT width=128 height=128 teams=8 controllers=9 compute_participants=32 eligible_cpus=32 gradient_backend=cpu ticks=10 ended=1 winner=0 checksum=a\n'
assert m.parse_log(text,'Playground',32,32,0,True)['status']=='cap';checks.append({'case':'cap overrides terminal winner','result':'passed'})
# Preserve intentional invalid fixtures and confirm fail-closed before statistics.
p=next((F/'serial').glob('*/*/receipt.json'));old=p.read_bytes();p.write_bytes(old+b' ')
try:m.load_regime(F,'serial');raise RuntimeError('tampered receipt accepted')
except AssertionError:checks.append({'case':'tampered receipt hash','result':'rejected as required'})
p.write_bytes(old)
(Q/'edge-cases.json').write_text(json.dumps({'state':'passed','scope':'Parser and estimator edge cases use copied real rows; no invented game runs. Cap/no-winner pairs excluded from conditional metrics. Hash corruption rejected before bootstrap.','checks':checks},indent=2)+'\n')
# Audit hook denies original-worktree content opens while relocated utility executes.
(D/'tools/relocation-check.py').write_text('''import sys,os,runpy,pathlib\nb=pathlib.Path(__file__).resolve().parents[1]\ndef audit(event,args):\n if event=='open' and isinstance(args[0],(str,bytes,os.PathLike)):\n  p=pathlib.Path(os.fsdecode(args[0])).absolute()\n  if str(p).startswith('/home/bradley/.codex/worktrees/') and not p.is_relative_to(b):raise RuntimeError('Original-worktree open forbidden: '+str(p))\nsys.addaudithook(audit)\nos.chdir(b)\nsys.argv=[str(b/'tools/recompute-final.py'),'--regime','both','--compare-retained','--output',str(b/'relocated-result.json')]\nrunpy.run_path(str(b/'tools/recompute-final.py'),run_name='__main__')\n''')
print('prepared',D)
