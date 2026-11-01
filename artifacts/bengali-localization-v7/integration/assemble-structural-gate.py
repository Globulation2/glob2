import pathlib,json,hashlib,subprocess,datetime,os,re
root=pathlib.Path.cwd();kit=root/'artifacts/bengali-localization-v7';read=lambda p:json.load(open(p));sha=lambda p:hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest();expected=read(kit/'expected-producer.json');assert subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()==expected['revision'];assert not subprocess.check_output(['git','status','--porcelain=v1'],text=True).strip();result=sorted((kit/'integration').glob('static-all-*/result.json'))[-1];r=read(result);assert r['source_revision']==expected['revision'] and r['all_executed_checks_passed'] and r['source_stable_after_check'];assert r['git_tree']==subprocess.check_output(['git','rev-parse','HEAD^{tree}'],text=True).strip();required={'pinned-script-fonts','native-font-coverage','native-strict','native-translation-tests','translation-program-tests','cloud-draft-tests','google-draft-tests','bengali-draft','online-contract','online-units','platform-typecheck','scoped-format'};assert {c['name'] for c in r['checks']}==required
files=[result,kit/'installed-source-binding.json' if (kit/'installed-source-binding.json').exists() else kit/'expected-producer.json'];counts={}
for c in r['checks']:
 assert c['exit_code']==0;f=result.parent/c['log'];assert sha(f)==c['sha256'];files.append(f);text=f.read_text();match=re.search(r'Ran (\d+) tests?',text)
 if match:counts[c['name']]=int(match.group(1))
 if c['name']=='online-units':assert 'Tests  82 passed (82)' in text
 if c['name']=='online-contract':assert '39 languages × 2947 messages' in text
 if c['name']=='native-strict':assert '0 structural errors' in text
 if c['name']=='bengali-draft':
  x=json.loads(text);assert not x['errors'] and not x['untranslated']
assert counts=={'native-font-coverage':1,'native-translation-tests':10,'translation-program-tests':13,'cloud-draft-tests':16,'google-draft-tests':10}
for p,h in expected['catalog_sha256'].items():
 f=root/('data/texts.bn.txt' if p=='native' else 'platform/packages/i18n/locales/bn.json');assert sha(f)==h
additional=kit/'additional-current-checks.json'
if additional.exists():
 a=read(additional);assert a['code_identity']==expected['code_identity'];files.append(additional)
 for c in a['checks']:
  assert c['exit_code']==0;f=kit/c['log'];assert sha(f)==c['log_sha256'];files.append(f)
manifest=read(kit/'manifest.json');assert r['source_manifest_sha256']==manifest['source_sha256']==expected['source_sha256']
g={'passed':True,'attested_by':'/root/rendering_audit','attested_at':datetime.datetime.now(datetime.timezone.utc).isoformat(),'source_sha256':expected['source_sha256'],'catalog_sha256':expected['catalog_sha256'],'code_identity':expected['code_identity'],'scope':'Actual clean4d77 structural checks: strict native39-language inventory and no Bengali gaps,6358-entry draft/token/plural checks,50 Python tests including actual bundled-SDL native font coverage,13 pinned script-font+OFL asset hashes,39×2947 online catalog/source and generated-browser freshness,82 i18n units,3 TypeScript projects and scoped formatting. Separate actual25 browser language/Unicode bridge units and10 API page/font/CSP/invite cases also passed. No full image roundtrip, renderer, startup, OS IME or platform execution inferred.','actual_python_test_counts':counts,'actual_online_unit_tests':82,'evidence':[{'path':os.path.relpath(f,kit),'sha256':sha(f)} for f in files],'limits':['The old V4 broad package/image roundtrip retains its own source/inputs; changed catalogs require separate fresh sealed payload extraction.','The mocked cloud ResourceWarning from a fake403 HTTPError is retained;16 mocked cloud tests passed.','V6 online command initially used Node22 without TS stripping and failed before reading catalogs; current actual Node24 contract pass is separate.']};(kit/'structural-gate.json').write_text(json.dumps(g,ensure_ascii=False,indent=2)+'\n');print('Actual structural gate PASS',expected['revision'])
