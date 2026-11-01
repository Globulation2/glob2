import hashlib, importlib.util, json
from pathlib import Path
root=Path(__file__).resolve().parents[2]
kit=root/'artifacts/bengali-localization-v2'
old=root/'artifacts/bengali-localization'
spec=importlib.util.spec_from_file_location('program',root/'tools/localization/translation_program.py')
p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
native,errors=p.CHECKER.read_table(kit/'drafts/bn/texts.bn.txt')
assert not errors,errors
online=json.loads((kit/'drafts/bn/bn.json').read_text())
full=kit/'preview/bn';full.mkdir(parents=True,exist_ok=True)
for name,values in [('native.json',native),('online.json',online)]:
 (full/name).write_text(json.dumps(values,ensure_ascii=False,indent=2)+'\n')
metadata={'locale':'bn','source_sha256':json.loads((kit/'manifest.json').read_text())['source_sha256'],'catalog_sha256':{name:hashlib.sha256(path.read_bytes()).hexdigest() for name,path in p.catalog_paths(kit,'bn').items()}}
(full/'metadata.json').write_text(json.dumps(metadata,indent=2)+'\n')
pilot=json.loads((old/'author-pilot.json').read_text())
small=kit/'preview-pilot/bn';small.mkdir(parents=True,exist_ok=True)
ids=json.loads((old/'repaired-pilot-semantic-review.json').read_text())
manual=json.loads((old/'source-based-pilot.json').read_text())
selected={e['id'] for e in manual['entries']}
for surface,values in [('native',native),('online',online)]:
 subset={key:value for key,value in values.items() if surface+':'+key in selected}
 if surface=='native':
  subset.update({k:native[k] for k in ['[language-code]','[language]','[language-tr]','[language incomplete]']})
 (small/(surface+'.json')).write_text(json.dumps(subset,ensure_ascii=False,indent=2)+'\n')
(small/'metadata.json').write_text(json.dumps(metadata,indent=2)+'\n')
print(full,small)
