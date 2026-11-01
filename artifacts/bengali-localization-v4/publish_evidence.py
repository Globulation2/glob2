"""Publish bounded, credential-free review evidence through a separate Git index."""
from pathlib import Path
import subprocess,os,json,hashlib
ROOT=Path.cwd();KIT=ROOT/'artifacts/bengali-localization-v4';selected=set()
def add(f):
 if f.is_file():
  assert f.resolve().is_relative_to(ROOT/'artifacts') and f.stat().st_size<40*1024*1024,f
  selected.add(f.resolve())
for f in KIT.iterdir():
 if f.suffix in ['.json','.py','.cjs','.log','.xml','.patch'] and not f.name.startswith('publish-index'):add(f)
for folder in ['source','batches','drafts/bn','reviews','cloud/bn','preview/bn','preview-new/bn','integration','web-screens','website-visual-work','webgl-text-audit','native-interactive','native-interactive-phone','native-interactive-phone-small','font-matrix','native-screens','ci']:
 for f in (KIT/folder).rglob('*'):
  parts=f.relative_to(KIT/folder).parts
  if any(x in ['node_modules','package','profiles','profile','extracted','runtime','sdk','prefix'] or x.startswith('profile-') for x in parts):continue
  if f.name=='private-session.json' or f.name.endswith('.tar.gz'):continue
  if f.suffix in ['.json','.png','.log','.gz','.xml','.py','.cjs','.yml','.patch','.txt']:add(f)
record=json.loads((KIT/'reviews/bn.json').read_text())
for item in record.get('independent_review_evidence',[]):add((KIT/item['path']).resolve())
for gate in record['gates'].values():
 for item in gate.get('evidence',[]):add((KIT/item['path']).resolve())
# Verify structured evidence never includes actual OAuth credentials.
def inspect(value,f):
 if isinstance(value,dict):
  for key,item in value.items():
   if key.lower() in ['access_token','refresh_token','private_key','id_token']:assert item in [None,'','***','REDACTED'],f
   inspect(item,f)
 elif isinstance(value,list):
  for item in value:inspect(item,f)
for f in selected:
 if f.suffix=='.json':
  try:inspect(json.loads(f.read_text()),f)
  except (ValueError,UnicodeDecodeError):pass
head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip();audit=json.loads((KIT/'assembly-audit.json').read_text())
readme=KIT/'EVIDENCE.md';readme.write_text(f'''# Bengali localization evidence

PR: https://github.com/Globulation2/glob2/pull/1315
Source branch at publication: `{head}`. Actual build and runtime receipts preserve their own producer identities.

The complete 6,253-entry catalog contains 3,411 native and 2,842 online entries. V4 preserves all 6,237 previously reviewed English/target pairs exactly and records actual author, semantic, editorial, final semantic and adversarial readings of the 16 new master statistics/terrain entries. There are 162 individual English-identical explanations. The result is agent-reviewed, not native-human-certified. Source/catalog identities are in `installed-source-binding.json`; exact snapshot composition is in `source-provenance.json`.

Cloud drafting conservatively reserves 189,106 submitted characters over 64 attempts, US$3.78212 without free credits, below US$10. All immutable ledgers are named in `combined-cloud-accounting.json`; credentials are excluded.

Current strict catalogs have zero structural errors and zero Bengali gaps. Integration evidence records 49 translation/tooling tests, 78 i18n units, 38×2,842 online contracts, three TypeScript projects, canonical browser/derivative generation, formatting and the stable full-package roundtrip. A separate current API page/font/CSP/invitation run has 10 passing cases. Website production build and 18 desktop/phone cases across Chromium/Firefox/WebKit pass; `website-visual-review.json` records the actual 48-image inspection and its findings. A DOM-test pass does not close a separate visual finding.

The old 7f serial WebGL negative control changes by 64 RGB levels over black/white backgrounds, proving opaque panel alpha leakage. The minimal WebGL2 source-over-alpha fix and hosted responsive regression are in the source branch. See `webgl-text-audit` for the executed negative control, corrected causal model and actual positive results when present; no missing result is inferred.

Historical V1/V2/V3 builds, images and platform records retain real source/catalog/font identities. In particular, old mobile runtime evidence is never relabeled as a final whole-platform binary. Source-bound component carry proofs and scope limitations govern any reuse. SDKs, dependency trees, app archives, private sessions, profiles and compiled binaries are excluded from this evidence branch.

Pending audit gates at publication: {json.dumps(audit['audit_issues'])}. `reviews/bn.json` and explicit release attestations are authoritative; publication alone does not establish readiness. Physical-device and keyboard/IME coverage is not claimed unless a receipt explicitly records execution.

`published-files.json` hashes the exact evidence blobs selected for this publication. Prior directories remain available for the original actual-reading chain and historical execution evidence.
''');add(readme)
branch='codex/bengali-localization-evidence-31efbe8';ref='refs/heads/'+branch;parent=subprocess.check_output(['git','rev-parse',ref],text=True).strip();env=dict(os.environ,GIT_INDEX_FILE=str(KIT/'publish-index'));subprocess.run(['git','read-tree',parent],env=env,check=True)
# Hash each byte snapshot once and stage that exact Git blob, avoiding live-log races.
rows=[]
for f in sorted(selected):
 raw=f.read_bytes();relative=str(f.relative_to(ROOT));blob=subprocess.check_output(['git','hash-object','-w','--stdin'],input=raw).decode().strip();subprocess.run(['git','update-index','--add','--cacheinfo','100644',blob,relative],env=env,check=True);rows.append({'path':relative,'sha256':hashlib.sha256(raw).hexdigest(),'bytes':len(raw)})
index=KIT/'published-files.json';index.write_text(json.dumps(rows,indent=2)+'\n');subprocess.run(['git','add','-f','--',str(index.relative_to(ROOT))],env=env,check=True)
tree=subprocess.check_output(['git','write-tree'],env=env,text=True).strip();commit=subprocess.check_output(['git','commit-tree',tree,'-p',parent],input='Publish source-bound Bengali V4 reviews and executed verification evidence\n',text=True).strip();subprocess.run(['git','update-ref',ref,commit,parent],check=True);subprocess.run(['git','push','origin',ref],check=True);print(json.dumps({'commit':commit,'files':len(rows),'bytes':sum(x['bytes'] for x in rows)},indent=2))
