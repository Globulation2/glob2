#!/usr/bin/env python3
from pathlib import Path
import json,shutil,hashlib,subprocess,os,shlex,difflib,re,time
os.sched_setaffinity(0,{15,31});here=Path(__file__).resolve().parent;repo=here.parents[2];base=repo/'artifacts/engine-history/snapshots/2026-10-11-9a08b91f4/source';sha=lambda p:hashlib.file_digest(Path(p).open('rb'),'sha256').hexdigest()
original_binary=base/'build/linux/client/release/src/glob2';assert sha(original_binary)=='3c23c53e1f0103eab935996ed4c7dd21b63f0464867346c9dcadc46926608a95'
logs=[p for p in base.parent.glob('build*.log')];line=next(s for p in logs for s in p.read_text(errors='replace').splitlines() if s.startswith('/usr/bin/ccache ') and s.endswith('src/map/gradient/MapGradientScheduling.cpp'));original_argv=shlex.split(line);records=[]
for chunk,scalar,metrics in [(64,False,False),(16,False,False),(128,False,False),(64,True,False),(64,False,True),(16,False,True),(128,False,True)]:
 label=f'chunk{chunk}'+('-scalar' if scalar else '')+('-metrics' if metrics else '');d=here/label;d.mkdir(exist_ok=True);headers=d/'field';headers.mkdir(exist_ok=True)
 for p in (base/'src/field').glob('*.h'):shutil.copyfile(p,headers/p.name)
 p=headers/'GradientRelaxation.h';old=p.read_text();new=old.replace('constexpr size_t CHUNK = 64;',f'constexpr size_t CHUNK = {chunk};');assert 'constexpr size_t CHUNK = '+str(chunk)+';' in new;p.write_text(new);(d/'chunk.patch').write_text(''.join(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='a/src/field/GradientRelaxation.h',tofile='b/src/field/GradientRelaxation.h')))
 if metrics:
  p=headers/'GradientBucket.h';old=p.read_text();new=old.replace('if (size + extra > cells.size())','K1_RESERVE_EVENT(extra,size + extra > cells.size());\n\t\tif (size + extra > cells.size())');assert new!=old;p.write_text(new);(d/'counters.patch').write_text(''.join(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='a/src/field/GradientBucket.h',tofile='b/src/field/GradientBucket.h')))
 a=list(original_argv);a[a.index('-o')+1]=str(d/'harness.o');a[-1]=str(here/'harness.cpp');a.insert(a.index('-c')+1,'-I'+str(headers));a.extend(['-MMD','-MF',str(d/'dependencies.d')]);
 if scalar:a.append('-DGLOB2_GRADIENT_SCALAR=1')
 if metrics:a.append('-DK1_WORK_COUNTERS=1')
 link=['g++-13','-o',str(d/'harness'),str(d/'harness.o'),'-L/usr/lib/gcc/x86_64-linux-gnu/15','-pthread']
 for name,cmd in [('compile',a),('link',link)]:
  with (d/(name+'.log')).open('w') as log:r=subprocess.run(['taskset','-c','15,31',*cmd],cwd=base,stdout=log,stderr=subprocess.STDOUT)
  if r.returncode:raise RuntimeError(f'{label} {name} failed: {d/(name+".log")}')
 with (d/'oracle.log').open('w') as log:r=subprocess.run(['taskset','-c','15,31',str(d/'harness')],cwd=base,stdout=log,stderr=subprocess.STDOUT)
 assert r.returncode==0,(label,(d/'oracle.log').read_text()[-1000:])
 if metrics:
  with (d/'metrics.log').open('w') as log:r=subprocess.run(['taskset','-c','15,31',str(d/'harness'),'--metrics'],cwd=base,stdout=log,stderr=subprocess.STDOUT)
  assert r.returncode==0
 ldd=subprocess.check_output(['ldd',str(d/'harness')],text=True);(d/'libraries.txt').write_text(ldd);libs={p:sha(p) for p in re.findall(r'(/[^\s]+)',ldd) if Path(p).is_file()}
 deps=shlex.split((d/'dependencies.d').read_text().replace('\\\n',' '))[1:];dependency_hashes={str(Path(p) if Path(p).is_absolute() else base/p):sha(Path(p) if Path(p).is_absolute() else base/p) for p in deps if Path(p).is_absolute() or (base/p).is_file()}
 records.append(dict(label=label,chunk=chunk,scalar=scalar,metrics=metrics,binary=str(d/'harness'),binary_sha256=sha(d/'harness'),compile_argv=a,link_argv=link,header_sha256=sha(headers/'GradientRelaxation.h'),copied_header_input_hashes={str(p.relative_to(base)):sha(p) for p in (base/'src/field').glob('*.h')},dependencies=dependency_hashes,shared_libraries=libs,oracle_summary=(d/'oracle.log').read_text().splitlines()[-1]))
 print('READY',label,flush=True)
# Output inventories must be byte-identical; final summary differs only in CHUNK.
oracles=[(here/r['label']/'oracle.log').read_text().splitlines()[:-1] for r in records];assert all(x==oracles[0] for x in oracles)
record=dict(base_commit='9a08b91f4350649862e788135e99bb1f5f469e3a',original_binary_sha256=sha(original_binary),original_binary_unchanged=True,original_compiler_argv=original_argv,harness_source_sha256=sha(here/'harness.cpp'),variants=records,oracle_inventories_identical=True,helper_cpu_affinity=[15,31],microtiming_launched=False,games_launched=False,scope='Exact current propagateFieldCPU binary-water fast path, swim0 uniform/3&6 weighted. Copied headers only. Changes CHUNK plus separate untimed counter hooks; scalar64 unchanged oracle control. No broad game build or tracked sources changed. Full-field equality only; no partial/resume/settled-layer/save/load/network/platform claim.',fixtures='128/256 swim0/3/6 sparse/clustered/barrier/deferred/none/allgoal plus1x1,1x32,32x1,2x17,17x2,2x2,7x13,4x5 caps0/42/300',timing_scope='Warm retained workspace; only kernel invocation wall/clock process CPU summed. Input copy, independent oracle, warmup, all-cell checking and output excluded. Cold/warm allocations/reservation capacity measured separately in metrics binaries. Synthetic fixture seed generation not actual captured game snapshot; no total game speedup claim.')
(here/'build.json').write_text(json.dumps(record,indent=2)+'\n');print('All variant oracles identical; no microtiming launched',flush=True)
