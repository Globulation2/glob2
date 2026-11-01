"""Prepare a bounded evidence inventory; publish only with explicit --publish.
Uses an independent Git index. Never touches the product index, checkout or PR state.
"""
import argparse,pathlib,json,hashlib,subprocess,os,datetime
P=argparse.ArgumentParser(__doc__);P.add_argument('--kit',type=pathlib.Path,required=True);P.add_argument('--revision',required=True);P.add_argument('--evidence-branch',default='codex/bengali-localization-evidence-31efbe8');P.add_argument('--publish',action='store_true');A=P.parse_args();root=pathlib.Path.cwd();kit=A.kit.resolve();assert kit.is_relative_to(root/'artifacts')
def git(*args):return subprocess.check_output(['git',*args],text=True).strip()
assert git('rev-parse','HEAD')==A.revision and not git('status','--porcelain=v1'),'Need exact clean source'
record=json.load(open(kit/'reviews/bn.json'));manifest=json.load(open(kit/'manifest.json'));selected=set();MAX_FILE=40*1024*1024;MAX_TOTAL=180*1024*1024
blocked={'node_modules','package','profiles','profile','extracted','runtime','sdk','prefix','.venv','__pycache__'}
def add(p):
 p=p.resolve();assert p.is_relative_to(root/'artifacts'),p
 if not p.is_file():return
 parts=p.relative_to(kit).parts if p.is_relative_to(kit) else p.relative_to(root/'artifacts').parts
 if any(n in blocked or n.startswith('profile-') or n.endswith('-profile') or n.endswith('-profiles') for n in parts) or p.name=='private-session.json':return
 allowed=p.suffix in ['.json','.png','.py','.cjs','.yml','.patch','.txt','.xml','.log'] or p.name.endswith('.log.gz')
 if not allowed:return
 assert p.stat().st_size<MAX_FILE,p;selected.add(p)
for p in kit.rglob('*'):add(p)
for item in record.get('independent_review_evidence',[]):add(kit/item['path'])
for gate in record['gates'].values():
 for item in gate.get('evidence',[]):add(kit/item['path'])
# Fail closed on actual credential-shaped structured values. No redaction mutates receipts.
def inspect(x,p):
 if isinstance(x,dict):
  for k,v in x.items():
   if k.lower() in ['access_token','refresh_token','private_key','id_token']:assert v in [None,'','***','REDACTED'],p
   inspect(v,p)
 elif isinstance(x,list):
  for v in x:inspect(v,p)
rows=[];snapshots={};total=0
for p in sorted(selected):
 raw=p.read_bytes();total+=len(raw);assert total<=MAX_TOTAL,'Evidence budget exceeded'
 if p.suffix=='.json':
  try:x=json.loads(raw)
  except (ValueError,UnicodeDecodeError):pass
  else:inspect(x,p)
 relative=str(p.relative_to(root));rows.append({'path':relative,'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest()});snapshots[relative]=raw
pending=[k for k,v in record['gates'].items() if v.get('passed') is not True]
plan={'source_revision':A.revision,'prepared_at':datetime.datetime.now(datetime.timezone.utc).isoformat(),'mode':'publish' if A.publish else 'inventory_only','evidence_branch':A.evidence_branch,'source_manifest_sha256':manifest['source_sha256'],'pending_gate_names':pending,'files':rows,'bytes':total,'limits':'No SDKs, app archives, executable binaries, profiles, credentials or private sessions. Actual producer/source identities and unexecuted gate limits remain authoritative.'}
plan_path=kit/'publication-plan.json';plan_path.write_text(json.dumps(plan,indent=2)+'\n')
if not A.publish:print(json.dumps({'mode':'inventory_only','files':len(rows),'bytes':total,'pending_gates':pending}));raise SystemExit(0)
# Explicit publishing may preserve pending evidence; it never marks readiness.
assert git('rev-parse','HEAD')==A.revision and not git('status','--porcelain=v1')
ref='refs/heads/'+A.evidence_branch;parent=git('rev-parse',ref);env=dict(os.environ,GIT_INDEX_FILE=str(kit/'publication-index'));subprocess.run(['git','read-tree',parent],env=env,check=True)
readme=f'''# Bengali localization verification evidence\n\nPR: https://github.com/Globulation2/glob2/pull/1315\n\nSource at publication: `{A.revision}`. Individual build/runtime receipts retain their actual producer revisions.\n\nThe source inventory contains {manifest['native_keys']} native and {manifest['online_keys']} online keys. Review records distinguish actual independent readings from exact source-target carry. Agent review is not native-human certification.\n\n`ci/platform-coverage-summary.json` and final release attestations state actual platform scopes and omissions. Prior Windows/macOS/mobile binaries are never relabelled as current whole-platform execution. Selected, queued, skipped and unavailable checks are not passes. Physical mobile/IME coverage is not claimed without an executed receipt.\n\nPending gate names at publication: {json.dumps(pending)}. Publication alone does not establish merge readiness.\n\n`publication-plan.json` identifies the exact retained evidence snapshots and hashes. SDKs, dependency trees, binary archives, profiles, credentials and private sessions are excluded.\n'''
snapshots[str((kit/'EVIDENCE.md').relative_to(root))]=readme.encode();snapshots[str(plan_path.relative_to(root))]=plan_path.read_bytes()
for path,raw in snapshots.items():
 blob=subprocess.check_output(['git','hash-object','-w','--stdin'],input=raw).decode().strip();subprocess.run(['git','update-index','--add','--cacheinfo','100644',blob,path],env=env,check=True)
tree=subprocess.check_output(['git','write-tree'],env=env,text=True).strip();commit=subprocess.check_output(['git','commit-tree',tree,'-p',parent],input='Publish final source-bound Bengali review and verification evidence\n',text=True).strip();subprocess.run(['git','update-ref',ref,commit,parent],check=True);subprocess.run(['git','push','origin',ref],check=True);print(json.dumps({'commit':commit,'files':len(snapshots),'bytes':total,'pending_gates':pending}))
