"""Bind completed final-source captures; never substitute historical producers."""
from pathlib import Path
import json,hashlib,importlib.util
ROOT=Path(__file__).resolve().parents[2];KIT=Path(__file__).resolve().parent
read=lambda p:json.loads(p.read_text());sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def module(name,path):
 spec=importlib.util.spec_from_file_location(name,path);obj=importlib.util.module_from_spec(spec);spec.loader.exec_module(obj);return obj
p=module('tp',ROOT/'tools/localization/translation_program.py');bp=module('bp',ROOT/'test/build_provenance.py')
source=bp.source_identity(ROOT);build=read(KIT/'native-app-build.json');assert build['exit_code']==0 and build['code_identity']==source
complete=read(KIT/'native-captures-complete.json');assert complete['exit_code']==0 and complete['code_identity']==source
native,errors=p.CHECKER.read_table(ROOT/'data/texts.bn.txt');assert not errors
original={'native':native,'online':read(ROOT/'platform/packages/i18n/locales/bn.json')}
hashes={s:sha(f) for s,f in p.catalog_paths(KIT,'bn').items()}
converted={s:read(KIT/'preview/bn'/(s+'.json')) for s in original};assert converted==original
converted_hashes={s:sha(KIT/'preview/bn'/(s+'.json')) for s in original}
meta=read(KIT/'preview/bn/metadata.json');assert meta['catalog_sha256']==hashes
assert meta['source_sha256']==read(KIT/'manifest.json')['source_sha256']==p.sources()[4]
rows=[]
for folder in ['native-screens/desktop','native-screens/phone','native-screens/phone150','font-matrix/desktop','font-matrix/phone']:
 for f in sorted((KIT/folder).glob('*.json')):
  d=read(f);e=d.get('evidence_identity');assert e,(f,'missing identity')
  assert not bp.build_issues(e['producer'],source),(f,'stale producer')
  assert e['input_metadata']['catalog_sha256']==hashes and e['input_metadata']['source_sha256']==meta['source_sha256']
  executable=Path(e['executable']);assert sha(executable)==e['executable_sha256']
  assert any(x['path']==str(executable) and x['sha256']==e['executable_sha256'] for x in build['executables'])
  for rel,h in e['source_sha256'].items():assert sha(ROOT/rel)==h,(f,rel)
  for font in e['runtime_fonts']:
   assert not font.get('missing') and sha(Path(font['path']))==font['sha256']==sha(ROOT/font['virtual_path'])
  if folder.startswith('native-screens'):
   assert e['catalog_sha256']==converted_hashes and d['native_keys_overridden']==3395
  else:
   for surface in original:
    subset=read(KIT/'preview-new/bn'/(surface+'.json'));assert all(original[surface][key]==value for key,value in subset.items())
   for sample in d.get('samples',[]):
    assert sample['has_glyphs'] and all(line['drawn_fits_width'] and line['drawn_fits_height'] for line in sample['lines'])
  rows.append({'path':str(f.relative_to(KIT)),'sha256':sha(f),'producer_revision':e['producer']['revision'],'producer_source_tree_sha256':e['producer']['sourceTreeSha256'],'scope':'complete catalog overlay on selected production screen' if folder.startswith('native-screens') else 'two new stat entries at multiple font sizes and scales'})
assert len(list((KIT/'native-screens/desktop').glob('*.json')))==8
assert len(list((KIT/'native-screens/phone').glob('*.json')))==8
assert len(list((KIT/'native-screens/phone150').glob('*.json')))==3
images=[{'path':str(f.relative_to(KIT)),'sha256':sha(f)} for folder in ['native-screens','font-matrix'] for f in sorted((KIT/folder).rglob('*.png'))]
result={'code_identity':source,'source_sha256':meta['source_sha256'],'catalog_sha256':hashes,'converted_catalog_sha256':converted_hashes,'records':rows,'images':images,'conversion':'Harness key/value JSON is verified exactly equal to the installed alternating-line native catalog. Input metadata retains original catalog hashes; evidence catalog hashes identify converted files. Frozen source_revision metadata is not the executable producer identity.','scope':'Actual final-source desktop-engine captures at desktop and compact100/150percent settings; selected new-entry font matrix. Physical mobile platforms and complete catalog pixel coverage are not asserted.'}
(KIT/'preview-binding.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n');print('Bound',len(rows),'capture records and',len(images),'images to',source['revision'])
