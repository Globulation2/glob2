"""Validate actual native capture identities and explain catalog format conversion."""
from pathlib import Path
import json,hashlib,importlib.util
root=Path(__file__).resolve().parents[2];kit=Path(__file__).resolve().parent
load=lambda p:json.loads(p.read_text());sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def module(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
program=module('tp',root/'tools/localization/translation_program.py');bp=module('bp',root/'test/build_provenance.py')
native,errors=program.CHECKER.read_table(root/'data/texts.bn.txt');assert not errors
original={'native':native,'online':load(root/'platform/packages/i18n/locales/bn.json')}
paths=program.catalog_paths(kit,'bn');original_hashes={k:sha(p) for k,p in paths.items()}
converted={s:load(kit/'preview/bn'/(s+'.json')) for s in original};assert converted==original
converted_hashes={s:sha(kit/'preview/bn'/(s+'.json')) for s in original}
meta=load(kit/'preview/bn/metadata.json');assert meta['catalog_sha256']==original_hashes
assert meta['source_sha256']==load(kit/'manifest.json')['source_sha256']==program.sources()[4]
source=bp.source_identity();records=[]
for folder in ['native/desktop','native/phone','native/phone-text150','native/fresh-phone','font-matrix','font-probe-matrix']:
 for p in sorted((kit/folder).glob('*.json')):
  d=load(p);e=d.get('evidence_identity')
  if not e:continue
  assert not bp.build_issues(e['producer'],source),str(p)
  assert e['input_metadata']['catalog_sha256']==original_hashes
  assert e['input_metadata']['source_sha256']==meta['source_sha256']
  assert sha(Path(e['executable']))==e['executable_sha256']
  for rel,h in e['source_sha256'].items():assert sha(root/rel)==h,(p,rel)
  for f in e['runtime_fonts']:
   assert not f.get('missing'),f
   assert sha(Path(f['path']))==f['sha256']==sha(root/f['virtual_path'])
  if folder.startswith('native/'):
   assert e['catalog_sha256']==converted_hashes
   assert d['native_keys_overridden']==len(native)
  elif folder=='font-matrix':
   for s in original:
    subset=load(kit/'preview-pilot/bn'/(s+'.json'))
    assert all(original[s][k]==v for k,v in subset.items())
  else:assert e['input_metadata']['diagnostic_samples'] is True
  if 'samples' in d:
   for sample in d['samples']:
    assert sample['has_glyphs']
    assert all(l['drawn_fits_width'] and l['drawn_fits_height'] for l in sample['lines'])
  records.append({'path':str(p.relative_to(kit)),'sha256':sha(p),'producer_revision':e['producer']['revision'],'producer_source_tree_sha256':e['producer']['sourceTreeSha256'],'scope':'complete catalog overlay' if folder.startswith('native/') else 'selected pilot font diagnostic' if folder=='font-matrix' else 'synthetic shaping probes; not catalog translations'})
result={'source_revision':source['revision'],'source_identity':source,'source_sha256':meta['source_sha256'],'original_catalog_sha256':original_hashes,'converted_catalog_sha256':converted_hashes,'conversion':'The harness reads key/value JSON; its evidence_identity.catalog_sha256 hashes the converted preview files, not native alternating-line text. input_metadata.catalog_sha256 retains original installed catalog file hashes. Exact per-key equality is checked for both complete surfaces.','records':records,'limitations':'Only captures actually listed are validated. This binding does not assert complete screen coverage, linguistic approval, native test completion, shipping-release readiness or unexecuted platforms.'}
(kit/'preview-binding.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n');print('Bound',len(records),'actual records to',source['revision'])
