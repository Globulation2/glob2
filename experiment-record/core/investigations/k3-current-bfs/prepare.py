#!/usr/bin/env python3
from pathlib import Path
import json,shutil,hashlib,subprocess,os,shlex,re,difflib
os.sched_setaffinity(0,{15,31});h=Path(__file__).resolve().parent;repo=h.parents[2];base=repo/'artifacts/engine-history/snapshots/2026-10-11-9a08b91f4/source';sha=lambda p:hashlib.file_digest(Path(p).open('rb'),'sha256').hexdigest();orig=base/'build/linux/client/release/src/glob2';original_hash=sha(orig);assert original_hash=='3c23c53e1f0103eab935996ed4c7dd21b63f0464867346c9dcadc46926608a95'
a0=shlex.split(next(s for p in base.parent.glob('build*.log') for s in p.read_text(errors='replace').splitlines() if s.startswith('/usr/bin/ccache ') and s.endswith('src/ai/shared_runtime/GradientBFS.cpp')));records=[]
for batch in [0,32,128]:
 for counters in [False,True]:
  label=('baseline' if batch==0 else 'batch'+str(batch))+('-counters' if counters else '');d=h/label;d.mkdir(exist_ok=True);headers=d/'field';headers.mkdir(exist_ok=True)
  for n in ['Grid.h','Frontier.h','UniformTraversal.h']:shutil.copyfile(base/'src/field'/n,headers/n)
  if counters:
   p=headers/'UniformTraversal.h';old=p.read_text();new=old.replace('const auto nextValue = values[current] + 1;', 'K3_VISIT_EVENT(current);\n\t\t\t\tconst auto nextValue = values[current] + 1;');assert new!=old;p.write_text(new);(d/'visit-hook.patch').write_text(''.join(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='a/src/field/UniformTraversal.h',tofile='b/src/field/UniformTraversal.h')))
   p=headers/'Frontier.h';old=p.read_text();new=old.replace('head = (head + 1) & (cells.size() - 1);','K3_POP_EVENT(head+1==cells.size());\n\t\thead = (head + 1) & (cells.size() - 1);').replace('if (count == cells.size())','const bool grew=count==cells.size();\n\t\tif (count == cells.size())').replace('++count;','++count;\n\t\tK3_PUSH_EVENT(count,grew);');p.write_text(new);(d/'counter-hook.patch').write_text(''.join(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='a/src/field/Frontier.h',tofile='b/src/field/Frontier.h')))
  a=list(a0);a[a.index('-o')+1]=str(d/'harness.o');a[-1]=str(h/'harness.cpp');a.insert(a.index('-c')+1,'-I'+str(headers));a.extend(['-DK3_BATCH='+str(batch),'-MMD','-MF',str(d/'dependencies.d')]);
  if counters:a.append('-DK3_COUNTERS=1')
  link=['g++-13','-o',str(d/'harness'),str(d/'harness.o'),'-L/usr/lib/gcc/x86_64-linux-gnu/15','-pthread']
  for stage,cmd in [('compile',a),('link',link)]:
   with (d/(stage+'.log')).open('w') as f:r=subprocess.run(['taskset','-c','15,31',*cmd],cwd=base,stdout=f,stderr=subprocess.STDOUT)
   if r.returncode:raise RuntimeError(str(d/(stage+'.log')))
  with (d/'oracle.log').open('w') as f:r=subprocess.run(['taskset','-c','15,31',str(d/'harness')],cwd=base,stdout=f,stderr=subprocess.STDOUT)
  assert r.returncode==0,(label,(d/'oracle.log').read_text()[-1000:])
  if counters:
   with (d/'metrics.log').open('w') as f:r=subprocess.run(['taskset','-c','15,31',str(d/'harness'),'--metrics'],cwd=base,stdout=f,stderr=subprocess.STDOUT)
   assert r.returncode==0
  ldd=subprocess.check_output(['ldd',str(d/'harness')],text=True);(d/'libraries.txt').write_text(ldd);libs={p:sha(p) for p in re.findall(r'(/[^\s]+)',ldd) if Path(p).is_file()};deps=shlex.split((d/'dependencies.d').read_text().replace('\\\n',' '))[1:];dep_hash={str(Path(p) if Path(p).is_absolute() else base/p):sha(Path(p) if Path(p).is_absolute() else base/p) for p in deps if Path(p).is_absolute() or (base/p).is_file()}
  records.append(dict(label=label,batch=batch,counters=counters,binary=str(d/'harness'),binary_sha256=sha(d/'harness'),compile_argv=a,link_argv=link,dependencies=dep_hash,shared_libraries=libs,oracle_summary=(d/'oracle.log').read_text().splitlines()[-1]));print('READY',label,flush=True)
oracles=[(h/r['label']/'oracle.log').read_text().splitlines()[:-1] for r in records];assert all(x==oracles[0] for x in oracles)
work=[]
for r in records:
 if r['counters']:
  for line in (h/r['label']/'metrics.log').read_text().splitlines():
   if line.startswith('METRICS '):c=dict(re.findall(r'(\w+)=([^\s]+)',line));c['variant']=r['label'];work.append(c)
(h/'allocation-work.json').write_text(json.dumps(dict(cases=work,untimed=True,scope='Separate instrumented binaries. Kernel-only operator-new requests, FIFO pushes/pops/grows/head wraps/peak pending and retained capacity; inputcopy/seeding/oracle excluded. Fixed batch stack bytes separate. Visit digest checked against independent FIFO oracle, which must also match original baseline digest.'),indent=2)+'\n')
record=dict(base_commit='9a08b91f4350649862e788135e99bb1f5f469e3a',original_binary_sha256=sha(orig),original_binary_unchanged=sha(orig)==original_hash,harness_sha256=sha(h/'harness.cpp'),source_inputs={str(p.relative_to(base)):sha(p) for p in [base/'src/ai/shared_runtime/GradientBFS.cpp',base/'src/ai/shared_runtime/Gradient.cpp',base/'src/field/UniformTraversal.h',base/'src/field/Frontier.h',base/'src/field/Grid.h']},original_compile_argv=a0,variants=records,all_cell_and_visit_order_inventories_identical=True,helpers=[15,31],timing_launched=False,games_launched=False,scope='Narrow current no-stop/no-admission uniform AI entry; std::int16_t values,0unvisited/1obstacle/2source, Surrounding order. Copied fixed stack indices before any neighbor pushes, retaining exactFIFO. Existing public API still executes one pop per item. Genericearly-stop/resume/weighted terrain unchanged and not established. Exception pending-frontier contents differ after batch predrain if allocation fails; no genericexception compatibility claim. Synthetic input obstacle profiles only, no actual gameplay source capture or seeding policy proof.')
(h/'build.json').write_text(json.dumps(record,indent=2)+'\n');print('PASS all-cell and actual visitation digests; no microtiming or engines',flush=True)
