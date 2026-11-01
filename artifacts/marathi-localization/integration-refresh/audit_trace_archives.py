from pathlib import Path
import subprocess,json,zipfile,io,hashlib,re,collections
from urllib.parse import urlparse
from PIL import Image
from fontTools.ttLib import TTFont
ref='e2c5f87070f86fb938109119a973ced4c795bfa2';base='artifacts/marathi-localization/';get=lambda n:subprocess.check_output(['git','show',ref+':'+n]);manifest=json.loads(get(base+'published-inventory.json'));patterns={'oauth':re.compile(rb'ya29\.[A-Za-z0-9_-]{20,}'),'github':re.compile(rb'(?:gh[pousr]_[A-Za-z0-9]{24,}|github_pat_[A-Za-z0-9_]{30,})'),'api_key':re.compile(rb'AIza[0-9A-Za-z_-]{30,}'),'private_key':re.compile(rb'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----'),'bearer':re.compile(rb'Authorization["\s:]{1,8}Bearer [A-Za-z0-9._-]{24,}',re.I)};results=[]
for name in manifest['sha256']:
 if not name.endswith('.zip'):continue
 data=get(name);z=zipfile.ZipFile(io.BytesIO(data));assert z.testzip() is None;types=collections.Counter();files=[];findings=[];headers=collections.Counter();hosts=set();cookie_names=set();auth_headers=[]
 assert sum(e.file_size for e in z.infolist())<16*1024*1024
 for e in z.infolist():
  assert not e.is_dir() and not e.filename.startswith('/') and '..' not in Path(e.filename).parts
  raw=z.read(e);kind=''
  for k,p in patterns.items():
   if p.search(raw):findings.append({'member':e.filename,'category':k,'secret_value_disclosed':False})
  if raw.startswith((b'wOFF',b'wOF2')):kind='WOFF font signature (not decoded)'
  elif raw.startswith((b'\x00\x01\x00\x00',b'OTTO')):
   font=TTFont(io.BytesIO(raw));assert font.getBestCmap();font.close();kind='parsed font asset'
  elif raw.startswith(b'\x89PNG') or raw.startswith(b'\xff\xd8\xff') or raw.startswith(b'RIFF') and raw[8:12]==b'WEBP':
   with Image.open(io.BytesIO(raw)) as im:im.verify()
   kind='verified image asset'
  else:
   try:
    text=raw.decode('utf-8');assert '\0' not in text;kind='UTF8 trace/source/web resource text'
    if e.filename.endswith(('.trace','.network','.stacks')):
     for line in text.splitlines():
      d=json.loads(line)
      request=d.get('snapshot',{}).get('request',{})
      if request:
       hosts.add(urlparse(request.get('url','')).netloc)
       for h in request.get('headers',[]):
        hn=h['name'].lower();headers[hn]+=1
        if hn=='authorization':auth_headers.append({'member':e.filename,'header_present':True,'scheme':h['value'].split(' ',1)[0],'value_length':len(h['value'])})
        if hn=='cookie':
         for cookie in h['value'].split(';'):cookie_names.add(cookie.strip().split('=',1)[0])
   except (UnicodeError,AssertionError):kind='unrecognized binary';findings.append({'member':e.filename,'category':kind,'magic_hex':raw[:16].hex()})
  types[kind]+=1;files.append({'member':e.filename,'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest(),'type':kind})
 results.append({'path':name,'sha256':hashlib.sha256(data).hexdigest(),'compressed_bytes':len(data),'decoded_bytes':sum(e.file_size for e in z.infolist()),'members':files,'types':dict(types),'credential_pattern_findings':findings,'request_hosts':sorted(hosts),'request_header_names':dict(headers),'cookie_names_only':sorted(cookie_names),'authorization_headers':auth_headers})
out={'auditor':'/root/locale_integration','evidence_commit':ref,'archives':results,'scope':'All members CRC verified; every text member UTF8/scanned and trace JSONL parsed; images verified, TTF font assets parsed with existing fontTools4.64.0, WOFF2 signatures classified without Brotli decoding. No values of cookies or authorization are disclosed in this report. These are preserved failed-run traces, not success evidence.'};Path(base+'integration-refresh/trace-archive-audit.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps([{k:v for k,v in r.items() if k not in ('members','request_header_names')} for r in results],indent=2))
