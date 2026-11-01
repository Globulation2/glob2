from pathlib import Path
import subprocess,json,re,hashlib
ROOT=Path.cwd()
OUT=ROOT/'artifacts/bengali-localization-v4/integration'
HEAD='7fcb8a2216a8220dd753bba1f6cd04d1f8934fc9'
MASTER='b34bcc19a7767d770b2b828a2d49e7aa9db53d0a'
TREE='a62223080637266764f28bec9ffb706c0f894682'
PAT=re.compile(r'^<<<<<<< .*?\n(.*?)^=======\n(.*?)^>>>>>>> .*?\n',re.M|re.S)
def show(rev,path):return subprocess.check_output(['git','show',rev+':'+path],text=True)
def save(path,text):
 p=OUT/'resolved'/path;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(text)
def conflicts(path,resolve):
 text=show(TREE,path)
 index=0
 def sub(match):
  nonlocal index
  index+=1
  return resolve(index,*match.groups())
 text=PAT.sub(sub,text)
 assert not re.search(r'^(<<<<<<<|=======|>>>>>>>)',text,re.M),path
 save(path,text)
 return text
for path in ['data/texts.incomplete.txt','data/texts.list.txt']:
 conflicts(path,lambda i,a,b:a+b)
conflicts('libgag/src/FontKerningTest.cpp',lambda i,a,b:a.replace('"bn",','"bn", "nb",'))
conflicts('platform/packages/i18n/src/locales.ts',lambda i,a,b:a+"    dir: 'ltr',\n  },\n  {\n"+b)
conflicts('platform/packages/i18n/test/runtime.test.ts',lambda i,a,b:a)
def system(i,a,b):
 if i==3:return '\t\t// The first preference must stay unsupported as new catalogs are added.\n'+a
 return a+'\t}\n\n'+b
conflicts('src/app/SystemLanguageTest.cpp',system)
def web(i,a,b):
 if i in (1,2):return b
 assert i==3
 return a+'});\n\n'+b
conflicts('platform/apps/web/e2e/i18n.spec.ts',web)
for path,field,key in [('platform/packages/i18n/shared-values.json',None,'nb'),('test/translation_shared_values.json','shared_vocabulary','texts.nb.txt')]:
 a=json.loads(show(HEAD,path)); b=json.loads(show(MASTER,path))
 if field:a[field][key]=b[field][key]
 else:a[key]=b[key]
 save(path,json.dumps(a,ensure_ascii=False,indent=2)+'\n')
# This metadata is provisional. Regenerate it canonically AFTER the real merge.
save('browser/assets/sources.json',show(MASTER,'browser/assets/sources.json'))
# New Norwegian catalog needs precisely our six reviewed AI-tag source additions.
nb=json.loads(show(MASTER,'platform/packages/i18n/locales/nb.json'))
impact=json.loads((ROOT/'artifacts/bengali-localization-v3/ci/new-master-localization-impact.json').read_text())
for item in impact['new_Norwegian_missing_current_six']:
 assert item['english'] not in nb
 nb[item['english']]=item['upstream_native_target']
nb=dict(sorted(nb.items()))
save('platform/packages/i18n/locales/nb.json',json.dumps(nb,ensure_ascii=False,indent=2)+'\n')
# Preserve independently reviewed final V4 translation bytes.
native=(ROOT/'artifacts/bengali-localization-v4/drafts/bn/texts.bn.txt').read_bytes()
assert hashlib.sha256(native).hexdigest()=='6a08cb16cf5a19f8858e59b7e54147d3d48f7a6121d395c01cbaa4d945c08201'
p=OUT/'resolved/data/texts.bn.txt';p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(native)
report={'source_head':HEAD,'frozen_master':MASTER,'merge_tree':TREE,'provisional_generated_metadata':['browser/assets/sources.json'],'regenerate_after_live_merge':['browser/i18n.js','browser/locales/*.json','browser/assets/sources.json'],'shadow_files':{str(p.relative_to(OUT/'resolved')):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((OUT/'resolved').rglob('*')) if p.is_file()}}
(OUT/'preparation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
