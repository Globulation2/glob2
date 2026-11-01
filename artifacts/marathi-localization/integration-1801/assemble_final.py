from pathlib import Path
import json,hashlib,importlib.util,shutil,copy,os,subprocess
root=Path.cwd();base=root/'artifacts/marathi-localization';epoch=base/'integration-1801';kit=epoch/'kit';oldkit=base/'integration-ffabe/kit'
s=importlib.util.spec_from_file_location('p',root/'tools/localization/translation_program.py');p=importlib.util.module_from_spec(s);s.loader.exec_module(p)
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
write=lambda f,d:f.write_text(json.dumps(d,ensure_ascii=False,indent=2)+'\n')
paths=p.catalog_paths(kit,'mr');shutil.copyfile(root/'data/texts.mr.txt',paths['native']);shutil.copyfile(root/'platform/packages/i18n/locales/mr.json',paths['online'])
manifest=json.loads((kit/'manifest.json').read_text());old=json.loads((oldkit/'reviews/mr.json').read_text());hashes={k:sha(v) for k,v in paths.items()};source=manifest['source_sha256']
oldn,err=p.CHECKER.read_table(oldkit/'drafts/mr/texts.mr.txt');newn,err2=p.CHECKER.read_table(paths['native']);assert not err and not err2 and all(newn[k]==v for k,v in oldn.items());assert set(newn)-set(oldn)=={'[AI for all computer colonies]'};assert (oldkit/'drafts/mr/mr.json').read_bytes()==paths['online'].read_bytes()
proof={'source_revision':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'integrated_master':'1801e6a832','source_sha256':source,'catalog_sha256':hashes,'native_old_pairs_equal':len(oldn),'online_catalog_byte_equal':True,'online_keys':2947,'one_new_native_key':'[AI for all computer colonies]','current_native_languages':40,'current_MR_index':39,'old_actual_runtime_shipping_revision':'e4811dcde89d6393918c36a99d34242ef3be8178','old_actual_runtime_validation_revision':'cdd634cebcde401f1266b6739f8ea21094eb4e56','scope':'Latest master adds Bengali and broad UI inspection/tooling. No latest native/Android/WASM full rebuild or full GUI rerun claimed. Prior measurements remain actual producers. One new target independently reviewed and raster-probed; font/matrix functions unchanged. Latest catalog contracts/focused tests run on integrated files.','unchanged_font_inputs':{f:sha(root/f) for f in ['libgag/src/TrueTypeFont.cpp','tools/TranslationPreviewHarness.cpp']}}
write(epoch/'source-target-carry.json',proof)
def ev(f):return {'path':os.path.relpath(f,kit),'sha256':sha(f)}
carry=ev(epoch/'source-target-carry.json');previous=ev(oldkit/'reviews/mr.json');fresh=[ev(epoch/f) for f in ['semantic-one-final-review.json','editorial-one-final-review.json','native-one-final-review.json']]
for f in fresh:
 d=json.loads((kit/f['path']).read_text());assert 'सर्व संगणकनियंत्रित वसाहतींसाठी एआय' in d.values(),d.keys()
r=copy.deepcopy(old);r.update(source_sha256=source,catalog_sha256=hashes,batches=[],finding_history=[previous,carry,*fresh],review_method='Exact3411 native and2947 online target carry from independently reviewed ffabe record; one new native key actual3final rereads. No fresh full-catalog reread.')
targets=p.batch_target_hashes(manifest,paths);oldkeys={k for b in old['batches'] for k in b['reviewed_keys']}
for batch in manifest['batches']:
 keys=batch['keys'];new=[k for k in keys if k not in oldkeys];assert set(new)<= {'native:[AI for all computer colonies]'}
 b={k:old['batches'][0][k] for k in ['author','reviewer','editor','rechecked_by','adversarial_reviewer']};b.update(id=batch['id'],source_sha256=source,target_sha256=targets[batch['id']]);b.update({k:keys for k in ['reviewed_keys','edited_keys','rechecked_keys','adversarial_reviewed_keys']});b['review_basis']={'previous_composite_record':previous,'exact_target_carry':carry,'fresh_keys':new,'final_signoffs':fresh if new else []};r['batches'].append(b)
r['review_epoch']={'revision':proof['source_revision'],'integrated_master':'1801e6a832','native_keys':3412,'online_keys':2947,'review_keys':6359,'text_variants':6386,'batch_count':len(r['batches']),'fresh_native_keys':1}
r['findings'].append({'key':'native:[AI for all computer colonies]','severity':'minor','state':'resolved','reason':'Clarified computer-controlled colonies in raw Google draft. Three independent actual final rereads accept concise AI terminology.'})
for name,g in r['gates'].items():
 g.update(source_sha256=source,catalog_sha256=hashes,evidence=[previous,carry]+fresh,scope='Prior gate scope carried only for exact unchanged translations/font inputs; actual producer IDs and all limitations remain in prior record. Latest integrated catalog/registry and focused checks separately attached. '+g.get('scope',''));g['limitations']=g.get('limitations',[])+['Latest1801 broad upstream UI changes are not a fresh full native/mobile/browser runtime build; no blanket latest GUI certification.']
refs=['native-strict.json','platform-contracts-result.json','platform-ui-result.json','python-tools-corrected-result.json','browser-font-carry.json','sim-revision-check.log']
for g in r['gates'].values():g['evidence'] += [ev(epoch/f) for f in refs]
write(kit/'reviews/mr.json',r);write(epoch/'review-assembly.json',{'source_sha256':source,'catalog_sha256':hashes,'review_keys':6359,'text_variants':6386,'batches':len(r['batches']),'carried_native_pairs':3411,'carried_online_keys':2947,'new_native_key':1,'scope':'Scoped evidence acceptance with explicit current unclassified threaded GUI failures and latest-runtime coverage limits; not all tests passed.'})
print(json.dumps({'source':source,'catalogs':hashes,'issues':p.audit_locale(kit,manifest,r,'mr')}))
