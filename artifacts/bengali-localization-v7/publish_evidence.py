"""Curated V7 evidence publication using the established separate-index snapshot pattern."""
from pathlib import Path
import subprocess,os,json,hashlib
ROOT=Path.cwd();KIT=ROOT/'artifacts/bengali-localization-v7';head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip();assert head=='4d77f0315f62504afd2669cb7dd12369f8e0b43f';assert not subprocess.check_output(['git','status','--porcelain=v1'],text=True).strip();audit=json.loads((KIT/'assembly-audit.json').read_text());assert audit['audit_issues']==[];tool=json.loads((KIT/'final-tool-status.json').read_text());assert tool['source_current'] and tool['locales']['bn']==[]
readme=KIT/'EVIDENCE.md';readme.write_text(f'''# Bengali localization evidence\n\nPR: https://github.com/Globulation2/glob2/pull/1315\n\nFinal product source: `{head}`. Source manifest: `822cf8613451177b0e2fa803451eaf2308168f21f4d04b7af2aa36cfe64bc3fc`. Native catalog: `6a08cb16cf5a19f8858e59b7e54147d3d48f7a6121d395c01cbaa4d945c08201`; online catalog: `488dd5ff63f1d7ec86b6c1d4b98b10011a51fe9fd00486c093a78f9cee9209d1`.\n\nAll6,358 pairs are covered:3,411 native and2,947 online. The6,253 original source/target pairs retain their actual historical reading chain;105 new pairs received actual distinct author, semantic, editorial, final semantic and adversarial readings. Mixed batches preserve per-key author identities. Both initial semantic findings are resolved;162 individual accepted-English explanations carry exactly. Agent review is not native-human certification.\n\nActual final checks include50 Python tests,82 i18n units,25 browser units,10 API page/font/CSP/invite tests, strict39-language and39×2,947 online contracts, font pins/coverage, three TypeScript projects and formatting. Native97 cases passed(92 headless+5 explicitX11); production website18 cases passed. Browser software serial/threaded desktop/phone plus threadedWebGL2 five cold cases, the tracked alpha regression and320×640150% Tools/statistics/map-peek guidance checks passed. WebGL opaque-panel contrast difference is0. Current39-native/39-online catalog and13-script-font package extraction passed. Independent visual reviews cover25 native+48 website+44 browser PNGs. Other retained diagnostic images are not silently called approved.\n\n`release-gates.json` attests available-check scopes, not all-current-platform binaries. Windows actualfda and macOS actual6c4b affected63-case executions and Android/iOS actual942 release/runtime evidence keep those identities. Current native Windows/macOS/Android/iOS binaries, native mobile compact paths, physical devices and physical Bengali IMEs remain explicitly unexecuted. Current source/asset/packaging comparisons and available final Linux/browser checks supplement those old executions. Prior known hosted fixture failures remain failures; pending current hosted checks are not passes.\n\nAll owned final local runtime processes exited and161 sealed browser members remained unchanged. Standalone-brand404/capture limits and historical failed setup attempts are retained explicitly. Google draft accounting records a conservative totalUS$3.82852 belowUS$10; no credential or SDK is published.\n\n`final-tool-status.json` reports `source_current:true` and no Bengali audit issues. `published-files.json` hashes the exact payload snapshots, including this guide; the manifest excludes itself. Publishing evidence does not relabel any producer or waive the listed coverage limits.\n''')
selected=set();blocked={'node_modules','package','profiles','profile','extracted','runtime','sdk','prefix','__pycache__','.venv'}
def add(f):
 f=f.resolve()
 if not f.is_file():return
 assert f.is_relative_to(ROOT/'artifacts'),f
 parts=f.relative_to(KIT).parts if f.is_relative_to(KIT) else f.relative_to(ROOT/'artifacts').parts
 if any(x in blocked or x.startswith('profile-') or x.endswith('-profile') or x.endswith('-profiles') for x in parts):return
 if f.name in ['private-session.json','published-files.json','publication-plan.json','published-receipt.json'] or f.name.startswith('publish-index'):return
 assert f.stat().st_size<40*1024*1024,f;selected.add(f)
for f in KIT.rglob('*'):
 if f.suffix in ['.json','.png','.jpg','.jpeg','.py','.cjs','.yml','.patch','.txt','.xml','.log'] or f.name.endswith('.log.gz') or f.name=='bengali-unicode-name.game.gz':add(f)
record=json.loads((KIT/'reviews/bn.json').read_text())
for item in record['independent_review_evidence']:add(KIT/item['path'])
for gate in record['gates'].values():
 for item in gate.get('evidence',[]):add(KIT/item['path'])
add(readme)
def inspect(value,f):
 if isinstance(value,dict):
  for key,item in value.items():
   if key.lower() in ['access_token','refresh_token','private_key','id_token']:assert item in [None,'','***','REDACTED'],f
   inspect(item,f)
 elif isinstance(value,list):
  for item in value:inspect(item,f)
for f in selected:
 if f.suffix=='.json':
  try:x=json.loads(f.read_text())
  except (ValueError,UnicodeDecodeError):pass
  else:inspect(x,f)
assert sum(f.stat().st_size for f in selected)<180*1024*1024,'Publication budget exceeded'
branch='codex/bengali-localization-evidence-31efbe8';ref='refs/heads/'+branch;parent=subprocess.check_output(['git','rev-parse',ref],text=True).strip();assert parent=='58c63d212a3459ce098b7c4250c13769e15277e6',parent;remote=subprocess.check_output(['git','ls-remote','origin',ref],text=True).split()[0];assert remote==parent
for gate in record['gates'].values():
 for item in gate.get('evidence',[]):assert hashlib.sha256((KIT/item['path']).read_bytes()).hexdigest()==item['sha256']
env=dict(os.environ,GIT_INDEX_FILE=str(KIT/'publish-index'));subprocess.run(['git','read-tree',parent],env=env,check=True);rows=[];snapshots={}
for f in sorted(selected):
 raw=f.read_bytes();relative=str(f.relative_to(ROOT));blob=subprocess.check_output(['git','hash-object','-w','--stdin'],input=raw).decode().strip();subprocess.run(['git','update-index','--add','--cacheinfo','100644',blob,relative],env=env,check=True);rows.append({'path':relative,'sha256':hashlib.sha256(raw).hexdigest(),'bytes':len(raw),'git_blob':blob});snapshots[relative]=blob
index=KIT/'published-files.json';index.write_text(json.dumps(rows,indent=2)+'\n');subprocess.run(['git','add','-f','--',str(index.relative_to(ROOT))],env=env,check=True);tree=subprocess.check_output(['git','write-tree'],env=env,text=True).strip();commit=subprocess.check_output(['git','commit-tree',tree,'-p',parent],input='Publish final Bengali V7 reviews and scoped executed evidence\n',text=True).strip()
# Verify every committed payload against the manifest snapshot before exposing the ref.
for row in rows:assert subprocess.check_output(['git','rev-parse',commit+':'+row['path']],text=True).strip()==row['git_blob']
assert json.loads(subprocess.check_output(['git','show',commit+':'+str(index.relative_to(ROOT))]))==rows
subprocess.run(['git','update-ref',ref,commit,parent],check=True);subprocess.run(['git','push','origin',ref],check=True);remote=subprocess.check_output(['git','ls-remote','origin',ref],text=True).split()[0];assert remote==commit;assert subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()==head and not subprocess.check_output(['git','status','--porcelain=v1'],text=True).strip()
result={'commit':commit,'parent':parent,'branch':branch,'files':len(rows),'bytes':sum(x['bytes'] for x in rows),'manifest_sha256':hashlib.sha256(index.read_bytes()).hexdigest(),'all_indexed_git_blobs_verified':True,'remote_ref_verified':True,'product_head_clean':head,'current_reviewed_PNGs':117,'published_PNGs':sum(x['path'].endswith('.png') for x in rows),'readme_url':f'https://github.com/Globulation2/glob2/blob/{commit}/artifacts/bengali-localization-v7/EVIDENCE.md','manifest_url':f'https://github.com/Globulation2/glob2/blob/{commit}/artifacts/bengali-localization-v7/published-files.json'};(KIT/'published-receipt.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
