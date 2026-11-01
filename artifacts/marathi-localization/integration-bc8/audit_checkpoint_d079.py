"""Read-only audit of an exact prepared evidence Git tree; emit local audit JSON."""
from pathlib import Path
import codecs,gzip,hashlib,io,json,re,subprocess,sys,tarfile
from PIL import Image
root=Path.cwd();base='artifacts/marathi-localization/';ref=sys.argv[1] if len(sys.argv)>1 else 'refs/heads/codex/marathi-localization-evidence'
rev=subprocess.check_output(['git','rev-parse',ref],text=True).strip();manifest_name=base+'published-inventory.json';manifest_bytes=subprocess.check_output(['git','show',rev+':'+manifest_name]);manifest=json.loads(manifest_bytes);expected=manifest['sha256'];wanted=set(expected)|{manifest_name};actual=set();problems=[];sensitive=[];typed={};large_json=[];total=0
patterns={
 'private_key':re.compile(r'-----BEGIN (?:RSA |EC |OPENSSH |DSA |ENCRYPTED )?PRIVATE KEY-----'),
 'oauth_access_token':re.compile(r'ya29\.[A-Za-z0-9_-]{20,}'),
 'github_token':re.compile(r'(?:gh[pousr]_[A-Za-z0-9]{24,}|github_pat_[A-Za-z0-9_]{30,})'),
 'google_api_key':re.compile(r'AIza[0-9A-Za-z_-]{30,}'),
 'literal_bearer':re.compile(r'Authorization[\"\s:]{1,8}Bearer [A-Za-z0-9._-]{24,}',re.I),
 'literal_access_token_value':re.compile(r'"access_token"\s*:\s*"[A-Za-z0-9._-]{24,}"'),
}
def inspect_text(name,stream):
 dec=codecs.getincrementaldecoder('utf-8')('strict');tail='';size=0;head='';chunks=[];retain=True
 while True:
  data=stream.read(1024*1024)
  if not data:break
  size+=len(data)
  if size>512*1024**2:raise ValueError('Decoded text exceeds audit512MiB per-file cap')
  text=dec.decode(data);head=(head+text)[:32]
  if '\0' in text:raise ValueError('NUL bytes in allowed text file')
  scan=tail+text
  for kind,pattern in patterns.items():
   if pattern.search(scan):sensitive.append({'path':name,'category':kind,'values_disclosed':False})
  tail=scan[-512:]
  if size<=16*1024**2:chunks.append(text)
  else:retain=False;chunks=[]
 dec.decode(b'',final=True)
 suffix=name[:-3] if name.endswith('.gz') else name
 if suffix.endswith('.json'):
  if retain:json.loads(''.join(chunks));kind='parsed JSON'
  else:large_json.append({'path':name,'bytes':size,'syntax_check':'not parsed above16MiB; strict UTF8/NUL/secret scan completed'});kind='large JSON text stream'
 else:kind='strict UTF8 text'
 return kind,size
proc=subprocess.Popen(['git','archive','--format=tar',rev],stdout=subprocess.PIPE)
with tarfile.open(fileobj=proc.stdout,mode='r|') as archive:
 for entry in archive:
  if entry.isdir():continue
  name=entry.name;actual.add(name)
  if not entry.isfile():problems.append({'path':name,'issue':'non-regular Git file'});continue
  if name not in wanted:problems.append({'path':name,'issue':'outside explicit manifest'})
  if not name.startswith(base):problems.append({'path':name,'issue':'outside evidence prefix'})
  parts=set(Path(name).parts)
  if parts&{'node_modules','cache','private-web-store','native-deps','profiles','previous671'}:problems.append({'path':name,'issue':'forbidden SDK/cache/profile/baseline directory'})
  if '/validated-android-ba890/' in name and not name.endswith('/archive-manifest.json'):problems.append({'path':name,'issue':'old local Android binary archive must not be published'})
  suffix=Path(name).suffix
  if suffix not in {'.json','.log','.png','.txt','.md','.xml','.py','.cjs','.js'} and not name.endswith(('.json.gz','.log.gz','.txt.gz')):problems.append({'path':name,'issue':'unexpected executable/binary/extension'})
  data=archive.extractfile(entry).read();total+=len(data);digest=hashlib.sha256(data).hexdigest()
  if name in expected and digest!=expected[name]:problems.append({'path':name,'issue':'content hash differs from manifest'})
  try:
   if suffix=='.png':
    assert data.startswith(b'\x89PNG\r\n\x1a\n')
    with Image.open(io.BytesIO(data)) as image:image.verify()
    typed['verified PNG']=typed.get('verified PNG',0)+1
   else:
    stream=gzip.GzipFile(fileobj=io.BytesIO(data)) if name.endswith('.gz') else io.BytesIO(data)
    kind,size=inspect_text(name,stream);typed[kind]=typed.get(kind,0)+1
  except Exception as exc:problems.append({'path':name,'issue':'content type/encoding/syntax audit failed','error':str(exc)[:200]})
assert proc.wait()==0
if actual!=wanted:problems.append({'issue':'tree and explicit manifest inventory differ','missing':sorted(wanted-actual),'extra':sorted(actual-wanted)})
if total!=manifest['payload_bytes']+len(manifest_bytes):problems.append({'issue':'payload byte count differs from inventory'})
if total>512*1024**2:problems.append({'issue':'publication exceeds512MiB'})
report={'auditor':'/root/locale_integration','scope':'Independent exact prepared Git evidence tree inventory/hash/content-type and bounded credential-pattern audit; manual producer/scope review is separate. No claim of exhaustive secret detection.','evidence_commit':rev,'manifest_source_revision':manifest['source_revision'],'manifest_sha256':hashlib.sha256(manifest_bytes).hexdigest(),'file_count':len(actual),'total_bytes':total,'all_explicit_hashes_match':not any(p.get('issue')=='content hash differs from manifest' for p in problems),'content_types':typed,'large_json_stream_only':large_json,'secret_patterns_checked':list(patterns),'credential_pattern_findings':sensitive,'problems':problems,'result':'pass' if not problems and not sensitive else 'review required','audit_tool_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'does_not_mutate_source_or_evidence_tree':True}
out=root/base/'integration-bc8/evidence-tree-audit-d079.json';out.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:report[k] for k in ('evidence_commit','manifest_source_revision','file_count','total_bytes','result')},indent=2));print('problems',len(problems),'credential_pattern_findings',len(sensitive),'large_json_stream_only',len(large_json))
