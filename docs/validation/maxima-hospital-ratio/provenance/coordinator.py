import json,pathlib,subprocess,time
LOCAL=pathlib.Path('/Users/bradley/glob2-maxima-defense-lab/artifacts/defense-study');REMOTE='/home/bradley/glob2-maxima-defense-study-20260919/artifacts/defense-study';ROOT=pathlib.Path('/Users/bradley/glob2-pr-help-5');design=json.loads((LOCAL/'hospital-macos-initials/design.json').read_text());variants=design['variants'];cases=design['cases'];started=False;stopped=False;uploaded=set();extra=None
assert len(cases)==6

def remote(code):return subprocess.check_output(['ssh','therig.local','python3 -'],input=code,text=True).strip()
def done(v,c):
 p=LOCAL/'hospital-macos-games'/v/c['id'];f=p/'execution.json'
 return f.exists() and json.loads(f.read_text())['exit']==0 and (p/'final.game.gz').exists() and not list(p.glob('*.game'))
while True:
 if not stopped:
  response=remote(f'''import pathlib,json,os,signal,subprocess
root=pathlib.Path({REMOTE!r});batch=root/'hospital-ratio';d=json.loads((batch/'design.json').read_text());cases=[c for c in d['cases'] if c['seed']<4019];good=True
for v in d['variants']:
 for c in cases:
  p=batch/v/c['id'];f=p/'execution.json'
  if not f.exists() or json.loads(f.read_text())['exit']!=0 or not (p/'final.game.gz').exists() or list(p.glob('*.game')):good=False
if good:
 rows=[l.split(None,2) for l in subprocess.check_output(['ps','-eo','pid,ppid,args'],text=True).splitlines()[1:]]
 parents=[int(r[0]) for r in rows if int(r[0])==2709482 and r[2]=='python3 artifacts/defense-study/hospital-ratio-run.py']
 for pid in parents:
  os.kill(pid,signal.SIGTERM)
  for r in rows:
   if int(r[1])==pid:
    try:os.kill(int(r[0]),signal.SIGTERM)
    except ProcessLookupError:pass
 (root/'hospital-linux-part-complete.json').write_text(json.dumps(dict(games=210,allocated_cases=42,reason='Remaining six paired cases assigned to Mac before completion; stop redundant Linux work.')))
 print('complete')
else:print('running')
''')
  if response=='complete':stopped=True;print('Linux assigned 210 games complete',flush=True)
 for c in cases:
  if c['id'] in uploaded or not all(done(v,c) for v in variants):continue
  state=remote(f'''import pathlib,json
b=pathlib.Path({REMOTE!r})/'hospital-ratio';case={c['id']!r};vs={variants!r}
print('linux' if any((b/v/case).exists() and (not (b/v/case/'execution.json').exists() or json.loads((b/v/case/'execution.json').read_text()).get('platform')!='macOS arm64') for v in vs) else 'ready')
''')
  if state=='linux' and not stopped:continue
  for v in variants:
   source=LOCAL/'hospital-macos-games'/v/c['id'];dest=f'{REMOTE}/hospital-macos-transfer/{c["id"]}/{v}'
   subprocess.run(['ssh','therig.local','mkdir','-p',dest],check=True)
   subprocess.run(['rsync','-a',str(source)+'/',f'therig.local:{dest}/'],check=True)
  print(remote(f'''import pathlib,json,time
r=pathlib.Path({REMOTE!r});case={c['id']!r}
for v in {variants!r}:
 d=r/'hospital-ratio'/v/case;s=r/'hospital-macos-transfer'/case/v
 if d.exists():
  old=r/'hospital-duplicate-linux'/v/(case+'-'+str(time.time_ns()));old.parent.mkdir(parents=True,exist_ok=True);d.rename(old)
 d.parent.mkdir(parents=True,exist_ok=True);s.rename(d)
print('Uploaded complete Mac case '+case)
'''),flush=True);uploaded.add(c['id'])
 if stopped and len(uploaded)==6:break
 time.sleep(20)
print('All 240 allocated games complete',flush=True)
