from pathlib import Path
import json,hashlib,shutil,base64
p=Path(__file__).parent
r=json.loads((p/'verified/tests.json').read_text());out=p/'verified/screenshots';out.mkdir(parents=True,exist_ok=True)
cases=[]
def walk(s):
 for x in s.get('specs',[]):
  for t in x.get('tests',[]):
   cases.append({'title':x['title'],'project':t['projectName'],'status':t['status'],'results':[v.get('status') for v in t['results']]})
   for v in t['results']:
    for a in v.get('attachments',[]):
     if a.get('contentType')=='image/png' and a.get('path'):
      src=Path(a['path']);dst=out/(t['projectName']+'-'+a['name']+'.png')
      if src.exists():shutil.copyfile(src,dst)
     elif a.get('contentType')=='image/png' and a.get('body'):
      (out/(t['projectName']+'-'+a['name']+'.png')).write_bytes(base64.b64decode(a['body']))
 for sub in s.get('suites',[]):walk(sub)
for s in r['suites']:walk(s)
(p/'case-inventory.json').write_text(json.dumps({'stats':r['stats'],'cases':cases},indent=2)+'\n')
files={str(f):{'sha256':hashlib.sha256(f.read_bytes()).hexdigest(),'bytes':f.stat().st_size} for f in sorted(out.glob('*.png'))}
(p/'image-index.json').write_text(json.dumps(files,indent=2)+'\n');print(len(cases),'cases',len(files),'images')
