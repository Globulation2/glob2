"""Read-only bounded extraction of changed locale/font payloads from sealed web packages.
Does not rerun or claim the broad image roundtrip. No generated source or package writes.
"""
import argparse,pathlib,subprocess,json,hashlib,datetime,re
P=argparse.ArgumentParser(__doc__);P.add_argument('--revision',required=True);P.add_argument('--package',type=pathlib.Path,required=True);P.add_argument('--producer',type=pathlib.Path,required=True);P.add_argument('--kit',type=pathlib.Path,required=True);P.add_argument('--output',type=pathlib.Path,required=True);A=P.parse_args();root=pathlib.Path.cwd();pkg=A.package.resolve();kit=A.kit.resolve();assert A.output.resolve().is_relative_to(root/'artifacts');assert not A.output.exists(),'Preserve prior receipts'
def sha(b):return hashlib.sha256(b).hexdigest()
def file(p):return pathlib.Path(p).read_bytes()
def git(*args):return subprocess.check_output(['git',*args],text=True).strip()
assert git('rev-parse','HEAD')==A.revision and not git('status','--porcelain=v1')
producer=json.loads(file(A.producer));assert producer['package_production_input_revision']==A.revision
manifest_text=(pkg/'asset-manifest.js').read_text();manifest=json.JSONDecoder().raw_decode(manifest_text.split('??=',1)[1].lstrip())[0];entries={};part_checks=[]
for package in manifest['packages']:
 for part in package['parts']:
  rel=pathlib.PurePosixPath(part['url']);assert not rel.is_absolute() and '..' not in rel.parts
  path=pkg/str(rel);assert path.is_file() and path.stat().st_size==part['size'];assert part['size']<64*1024*1024
  # Stream each bounded part once. Every part hash proves the sealed member identity;
  # only selected text/font byte ranges are read into memory below.
  h=hashlib.sha256()
  with path.open('rb') as f:
   while block:=f.read(1024*1024):h.update(block)
  digest=h.hexdigest();name=re.search(r'\.([a-f0-9]{16,64})\.data$',part['url']);assert name and digest.startswith(name.group(1))
  part_checks.append({'package':package['name'],'url':part['url'],'bytes':part['size'],'sha256':digest})
  for virtual,start,end in part['files']:
   assert 0<=start<=end<=part['size'];key=(package['name'],virtual);assert key not in entries;entries[key]=(path,start,end)
rows=[]
def check(package,virtual,expected):
 path,start,end=entries[(package,virtual)];assert end-start<16*1024*1024
 with path.open('rb') as f:f.seek(start);actual=f.read(end-start)
 assert actual==expected,(package,virtual,sha(actual),sha(expected))
 rows.append({'package':package,'virtual_path':virtual,'bytes':len(actual),'sha256':sha(actual),'exact_source_bytes':True})
# Full native catalogs, not merely the language-name stubs in core.
listing=(root/'data/texts.list.txt').read_text().split();native=[]
for name in listing:
 if name.endswith('keys.txt'):continue
 source=root/name;assert source.is_file(),source
 package='core' if name=='data/texts.en.txt' else 'translations';check(package,'/'+name,file(source));native.append(name)
for name in ['data/texts.keys.txt','data/texts.list.txt','data/texts.incomplete.txt']:check('core','/'+name,file(root/name))
online=[];canonical=root/'platform/packages/i18n/locales';expected_names={p.name for p in canonical.glob('*.json')};actual_names={p.name for p in (pkg/'locales').glob('*.json')};assert actual_names==expected_names
for name in sorted(expected_names):
 expected=file(canonical/name);actual=file(pkg/'locales'/name);assert actual==expected,name;online.append({'locale':name[:-5],'sha256':sha(actual),'messages':len(json.loads(actual)),'exact_canonical_bytes':True})
assert len(native)==len(online)==39,(len(native),len(online));bn=json.loads(file(pkg/'locales/bn.json'));assert len(bn)==2947
font_manifest=json.loads(file(root/'data/fonts/scripts/manifest.json'));http_fonts=[]
check('core','/data/fonts/scripts/manifest.json',file(root/'data/fonts/scripts/manifest.json'))
for font in font_manifest['fonts']:
 name=font['name']+'-Regular.ttf';assert pathlib.Path(name).name==name;expected=file(root/'data/fonts/scripts'/name);assert sha(expected)==font['sha256'];check('core','/data/fonts/scripts/'+name,expected);actual=file(pkg/'fonts'/name);assert actual==expected;http_fonts.append({'name':name,'sha256':sha(actual),'exact_source_bytes':True})
check('core','/data/fonts/LICENSE-Noto.txt',file(root/'data/fonts/LICENSE-Noto.txt'));assert file(pkg/'fonts/LICENSE-Noto.txt')==file(root/'data/fonts/LICENSE-Noto.txt')
check('core','/data/fonts/sans.ttf',file(root/'browser/assets/sans-core.ttf'));check('font-cjk','/data/fonts/sans.ttf',file(root/'data/fonts/sans.ttf'))
assert file(pkg/'i18n.js')==file(root/'browser/i18n.js')
identities=[]
for name in ['identity.json','threaded/identity.json']:
 x=json.loads(file(pkg/name));assert x['target']=='web' and x['client_profile']=='full';identities.append({'file':name,'sha256':sha(file(pkg/name)),'identity':x})
manifest_kit=json.loads(file(kit/'manifest.json'));bound={p:sha(file(root/p)) for p in manifest_kit['source_files']};assert bound==manifest_kit['source_files']
assert git('rev-parse','HEAD')==A.revision and not git('status','--porcelain=v1')
r={'status':'PASS','reviewer_identity':'/root/rendering_audit','observed_at':datetime.datetime.now(datetime.timezone.utc).isoformat(),'actual_source_revision':A.revision,'source_manifest_sha256':manifest_kit['source_sha256'],'git_tree':git('rev-parse','HEAD^{tree}'),'producer_receipt_sha256':sha(file(A.producer)),'asset_manifest_sha256':sha(file(pkg/'asset-manifest.js')),'package_directory':str(pkg),'native_catalog_count':len(native),'online_catalog_count':len(online),'bengali_online_messages':len(bn),'checked_extractions':rows,'online_http_catalogs':online,'http_script_fonts':http_fonts,'all_part_hashes':part_checks,'runtime_identities':identities,'licence_sha256':sha(file(root/'data/fonts/LICENSE-Noto.txt')),'limits':['Bounded actual locale/font extraction and sealed package content hashes, not a rerun of the broad3400-image roundtrip.','No actual startup, renderer appearance, OS IME, mobile binary or simulation determinism claim.','Runtime/compiler/dependency identities remain those in the producer receipt; this does not relabel old binaries.']};A.output.parent.mkdir(parents=True,exist_ok=True);A.output.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps({'status':'PASS','native_catalogs':len(native),'online_catalogs':len(online),'bn_messages':len(bn),'font_payloads':len(http_fonts)}))
