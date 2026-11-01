"""Read-only scoped preflight; never publishes or claims an exact final Git audit."""
from pathlib import Path
import json,hashlib,re,ast,io,gzip,collections,time
from PIL import Image
root=Path.cwd();b=root/'artifacts/marathi-localization';out=b/'integration-bc8/stable-content-preflight.json';cache=b/'integration-bc8/stable-content-preflight-hashes.json';policy=b/'publish_evidence.py';tree=ast.parse(policy.read_text());ext=None
for n in ast.walk(tree):
 if isinstance(n,ast.Assign) and len(n.targets)==1 and isinstance(n.targets[0],ast.Name) and n.targets[0].id=='extensions':ext=ast.literal_eval(n.value)
assert ext
roots=[b/'integration-bc8',b/'integration-refresh']
roots += [p for p in (b/'browser-game').iterdir() if p.is_dir() and (p.name.startswith(('first-qa-','smoke-')) or p.name=='full-ec24-back-navigation-race')]
files=set();excluded=collections.Counter()
for folder in roots:
 for p in folder.rglob('*'):
  if not p.is_file():continue
  rel=p.relative_to(b);parts=set(p.parts)
  if parts&{'profiles','node_modules','cache','__pycache__','previous671','browser-fixture-baseline','baseline-browser-fixture'}:excluded['forbidden_directory']+=1;continue
  if any(str(rel).startswith(x+'/') for x in ['integration-bc8/native-baseline/master-snapshot','integration-bc8/caddy-qa/config','integration-bc8/caddy-qa/data']):excluded['snapshot_or_config']+=1;continue
  if str(rel).startswith('integration-bc8/preparation-kit/') and p.name!='manifest.json':excluded['preparation_nonmanifest']+=1;continue
  if p in (out,cache) or p.name.startswith('publication-'):continue
  if p.suffix in ext or p.name.endswith(('.json.gz','.log.gz','.txt.gz')):files.add(p)
  else:excluded['nonallowlisted_extension']+=1
# Mirror bounded publishermatrixpolicy; do not inspect unselected PNGpayloads.
matrix=sorted(p for p in files if p.suffix=='.png' and p.parent.name=='matrix');groups={}
for p in matrix:groups.setdefault((p.parent,p.name.split('-page-')[0]),[]).append(p)
selected=set()
for g in groups.values():selected.update((g[0],g[len(g)//2],g[-1]))
def refs(v,parent):
 if isinstance(v,dict):
  if isinstance(v.get('path'),str) and v['path'].endswith('.png'):
   p=Path(v['path']);p=p if p.is_absolute() else root/p if v['path'].startswith('artifacts/') else parent/p
   if p in files:selected.add(p)
  for c in v.values():refs(c,parent)
 elif isinstance(v,list):
  for c in v:refs(c,parent)
for folder in roots:
 for p in folder.rglob('visual-inspections.json'):refs(json.loads(p.read_text()),p.parent)
files.difference_update(matrix);files.update(selected)
patterns={'private_key':re.compile(r'-----BEGIN (?:RSA |EC |OPENSSH |DSA |ENCRYPTED )?PRIVATE KEY-----'),'oauth':re.compile(r'ya29\.[A-Za-z0-9_-]{20,}'),'github':re.compile(r'(?:gh[pousr]_[A-Za-z0-9]{24,}|github_pat_[A-Za-z0-9_]{30,})'),'google':re.compile(r'AIza[0-9A-Za-z_-]{30,}'),'literal_bearer':re.compile(r'Authorization["\s:]{1,8}Bearer [A-Za-z0-9._-]{24,}',re.I),'literal_access_token':re.compile(r'"access_token"\s*:\s*"[A-Za-z0-9._-]{24,}"')}
assert sum(p.stat().st_size for p in files)<512*1024**2
before={p:(p.stat().st_size,p.stat().st_mtime_ns) for p in files};hashes={};types=collections.Counter();findings=[];secrets=[];total=0
for p in sorted(files):
 name=str(p.relative_to(root));assert not p.is_symlink();data=p.read_bytes();total+=len(data);hashes[name]=hashlib.sha256(data).hexdigest()
 try:
  if p.suffix=='.png':
   assert data.startswith(b'\x89PNG\r\n\x1a\n')
   with Image.open(io.BytesIO(data)) as im:im.verify()
   types['PNG_verified']+=1
  else:
   if p.name.endswith('.gz'):
    with gzip.GzipFile(fileobj=io.BytesIO(data)) as z:data=z.read(256*1024**2+1)
    assert len(data)<=256*1024**2
   assert not data.startswith((b'\x7fELF',b'PK\x03\x04',b'MZ',b'\0asm')),'native/archive/rawbinarymagic'
   text=data.decode('utf-8');assert '\0' not in text,'NULinallowedtext'
   for kind,pattern in patterns.items():
    if pattern.search(text):secrets.append({'path':name,'kind':kind,'secret_value_disclosed':False})
   if p.name.endswith(('.json','.json.gz')):json.loads(text);types['JSON_parsed']+=1
   else:types['UTF8_text_verified']+=1
 except Exception as e:findings.append({'path':name,'issue':str(e)[:180]})
assert all((p.stat().st_size,p.stat().st_mtime_ns)==s for p,s in before.items()),'Stableinputchanged; discard/retrystableonly'
cache.write_text(json.dumps({'scope':'STABLESCOPEDPREFLIGHTONLY, notfinalGitinventory','files':hashes},indent=2)+'\n')
report={'reviewer':'/root/locale_integration','scope':'Readonly content/magic/credential preflight of stable currentnative/Android/metadatafollowup, stablekits/reviews/website/caddyproof and retainedbrowserfailureattempts. No publish/sourceedit/browserrun.','source_head_at_scan':__import__('subprocess').check_output(['git','rev-parse','HEAD'],text=True).strip(),'policy_sha256':hashlib.sha256(policy.read_bytes()).hexdigest(),'included_roots':[str(p.relative_to(root)) for p in roots],'exclusions':dict(excluded),'explicit_active_browser_exclusion':'browser-game/chromium/firefox/webkit currentengine/variant/device frames, reports/logs/qa-run/top-levelbrowserlogs andenglish-map-comparisonexcluded entirely; activebrowseroutsidechosenrootsneverread/hashed.','matrix_policy':{'local_png_candidates':len(matrix),'selected_png_payloads_scanned':len(set(matrix)&selected),'unselected_png_payloads_not_read':len(matrix)-len(set(matrix)&selected)},'files_scanned':len(files),'payload_bytes_scanned':total,'types':dict(types),'credential_patterns':list(patterns),'credential_findings':secrets,'content_findings':findings,'stability':'Every selectedfile stat(size,mtime_ns) exactsameaftercontentscan. Newfilescreatedelsewhere notpartofscope.','cache_path':str(cache.relative_to(root)),'cache_sha256':hashlib.sha256(cache.read_bytes()).hexdigest(),'result':'pass' if not findings and not secrets else 'review required','limits':['Notexactfinalpreparedcommit/inventory/gatesaudit; mustrepeatmatchingcandidatehashes andinspectnewfinalbrowseroutputs.','No exhaustive secret-detectionclaim; boundedknowncredentialpatterns only.','Oldremote2968alreadyindependentlyaudited; thisscopedscan doesnotrepeatallunchangedoldremote/historicaltraceZIPpayloads.','SuffixallowlistselectedonlyPNG/UTF8text/gzippedtext; localAPK/ELF/SDK/profile/rawtracefilesexcluded.','PNGsyntaxverificationdoesnotconstitutenewmanualvisualreview.']}
out.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:report[k] for k in ['files_scanned','payload_bytes_scanned','types','result','content_findings','credential_findings']},indent=2));print('reportsha',hashlib.sha256(out.read_bytes()).hexdigest())
