"""Bind actual historical readings plus 16 actual new reviews; never invent approval."""
from pathlib import Path
import copy,hashlib,importlib.util,json,os
KIT=Path(__file__).resolve().parent;ROOT=KIT.parents[1];OLD=KIT.with_name('bengali-localization-v3')
spec=importlib.util.spec_from_file_location('translation_program',ROOT/'tools/localization/translation_program.py');p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
read=lambda f:json.loads(f.read_text());sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
def write(name,value):
 f=KIT/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n')
def evidence(f):return {'path':os.path.relpath(f,KIT),'sha256':sha(f)}
m=read(KIT/'manifest.json');assert p.sources()[4]==m['source_sha256'];paths=p.catalog_paths(KIT,'bn');hashes={s:sha(f) for s,f in paths.items()}
native,errors=p.CHECKER.read_table(paths['native']);assert not errors
targets={'native':native,'online':read(paths['online'])};sources={s:read(KIT/'source'/(s+'.json')) for s in targets}
assert (ROOT/'data/texts.bn.txt').read_bytes()==paths['native'].read_bytes();assert (ROOT/'platform/packages/i18n/locales/bn.json').read_bytes()==paths['online'].read_bytes()
prior=read(OLD/'reviews/bn.json');oldm=read(OLD/'manifest.json');oldpaths=p.catalog_paths(OLD,'bn')
assert prior['source_sha256']==oldm['source_sha256'];assert {s:sha(f) for s,f in oldpaths.items()}==prior['catalog_sha256']
rows={e['id']:e for e in prior['coverage_provenance']};assert len(rows)==len(prior['coverage_provenance'])==6237
prior_batches={}
for b in prior['batches']:
 ks=set(b['reviewed_keys']);assert ks==set(b['edited_keys'])==set(b['rechecked_keys'])==set(b['adversarial_reviewed_keys']);assert len({b[x] for x in ['author','reviewer','editor','adversarial_reviewer']})==4
 for ident in ks:assert ident not in prior_batches;prior_batches[ident]=b
assert set(rows)==set(prior_batches)
for ident,row in rows.items():
 s,key=ident.split(':',1);assert (row['source'],row['target'])==(sources[s][key],targets[s][key])
allkeys={s+':'+key for s,table in sources.items() for key in table};new=allkeys-set(rows)
assert new=={'native:'+key for key in read(KIT/'source-provenance.json')['new_native_entries']} and len(new)==16 and len(allkeys)==6253
historical=[OLD/'reviews/bn.json',OLD/'manifest.json',OLD/'assemble_review.py',OLD/'source-contract-compatibility.json']
for item in prior['independent_review_evidence']:
 f=OLD/item['path'];assert sha(f)==item['sha256'];historical.append(f)
checks=[('author-delta.json','author','/root/bengali_author','entries','final_authored'),('semantic-initial.json','reviewer','/root/rendering_audit','entries','target'),('editor-delta.json','editor','/root/bengali_online_author','entries','target'),('semantic-final.json','reviewer','/root/rendering_audit','entries','target'),('adversarial-delta.json','identity','/root/bengali_adversary','freshly_reviewed_pairs','target')]
fresh=[]
for name,who,identity,field,target in checks:
 f=KIT/name;d=read(f);assert d[who]==identity
 if name=='author-delta.json':assert d['status'].startswith('All16 delta pairs author-examined;')
 elif name.startswith('semantic-'):assert d['status'].startswith('PASS')
 else:assert d.get('status',d.get('verdict'))=='pass'
 assert d.get('source_sha256',d.get('source_manifest_sha256'))==m['source_sha256'];assert all(x.get('state')=='resolved' for x in d.get('findings',[]));assert not d.get('open_findings') and not d.get('unresolved_uncertainty') and not d.get('unresolved_uncertainties')
 assert d.get('final_native_sha256',d.get('actual_reviewed_native_sha256',d.get('native_catalog_sha256')))==hashes['native']
 examined={}
 for row in d[field]:
  ident=row.get('id',row.get('key'));ident=ident if ident.startswith('native:') else 'native:'+ident
  assert ident not in examined;s,key=ident.split(':',1);assert row['source']==sources[s][key] and row[target]==targets[s][key];examined[ident]=True
 assert set(examined)==new;fresh.append(f)
checked=p.check_draft(KIT,'bn');assert not checked['errors'] and not checked['untranslated'];accepted=copy.deepcopy(prior['accepted_english']);assert {e['key'] for e in accepted}==set(checked['same_as_english_review_queue']);assert len(accepted)==162
record={'locale':'bn','source_sha256':m['source_sha256'],'catalog_sha256':hashes,'certification':'Agent-reviewed; not native-human-certified','language_brief':copy.deepcopy(prior['language_brief']),'findings':copy.deepcopy(prior['findings']),'accepted_english':accepted,'batches':[],'coverage_provenance':[],'gates':{}}
for finding in read(KIT/'author-delta.json')['findings']:
 record['findings'].append({'key':finding.get('id',finding.get('key')),'severity':finding['severity'],'state':finding['state'],'reason':finding['reason']})
batchhash=p.batch_target_hashes(m,paths)
for b in m['batches']:
 surface=b['id'].split('-',1)[0];people={'author':'/root/bengali_author' if surface=='native' else '/root/bengali_online_author','reviewer':'/root/rendering_audit','editor':'/root/bengali_online_author' if surface=='native' else '/root/bengali_editor','adversarial_reviewer':'/root/bengali_adversary'}
 for key in b['keys']:
  if key in rows:assert all(prior_batches[key][role]==identity for role,identity in people.items())
 record['batches'].append({'id':b['id'],'source_sha256':m['source_sha256'],'target_sha256':batchhash[b['id']],**people,'reviewed_keys':b['keys'],'edited_keys':b['keys'],'rechecked_keys':b['keys'],'rechecked_by':people['reviewer'],'adversarial_reviewed_keys':b['keys'],'carried_prior_actual_read_keys':[k for k in b['keys'] if k in rows],'fresh_delta_actual_read_keys':[k for k in b['keys'] if k in new],'coverage_note':'Prior actual V1/V2/V3/V3 readings carried only by exact source-target equality; 16 new entries directly read by all four distinct roles and rechecked after editing.'})
 for ident in b['keys']:
  s,key=ident.split(':',1);record['coverage_provenance'].append({'id':ident,'source':sources[s][key],'target':targets[s][key],'basis':'preserved V3 actual-reading chain by exact pair equality; no new full-reading claim' if ident in rows else 'fresh author, initial semantic, independent editor, final semantic reread and fresh adversarial reading','prior_batch':prior_batches[ident]['id'] if ident in rows else None})
record['coverage_summary']={'complete_keys':6253,'carried_actual_prior_read_pairs':6237,'freshly_reviewed_new_pairs':16,'native_keys':3411,'online_keys':2842,'mechanical_rebinding_is_not_fresh_review':True}
all_evidence=list(dict.fromkeys(historical+fresh+[KIT/'source-provenance.json',KIT/'installed-source-binding.json']))
record['independent_review_evidence']=[evidence(f) for f in all_evidence]
for gate in ['language_brief','source_based_pilot']:
 prior_gate=prior['gates'][gate];assert prior_gate['passed']
 files=[]
 for item in prior_gate['evidence']:
  f=OLD/item['path'];assert sha(f)==item['sha256'];files.append(f)
 record['gates'][gate]={'passed':True,'source_sha256':m['source_sha256'],'catalog_sha256':hashes,'basis':'Original source-based pilot and brief remain unchanged; exact prior pairs are preserved and 16 new stat labels received actual four-role contextual review under the same brief. No new pilot execution claimed.','evidence':[evidence(f) for f in dict.fromkeys(files+fresh)]}
# Validate actual integration checks by their concrete receipts, not assumed counts.
checks=read(KIT/'integration/static-checks.json')['checks']
required=['native_strict','native_translation_tests','translation_program_tests','cloud_draft_tests','upstream_google_draft_tests','online_catalog_and_generated_freshness','online_unit_tests','platform_typecheck','platform_format','bengali_draft','canonical_browser_generation','canonical_derivative_generation']
structural=[KIT/'check-draft.json',KIT/'installed-source-binding.json']
for name in required:
 check=checks[name];assert check['exit_code']==0,(name,check)
 f=KIT/'integration'/check['log'];assert sha(f)==check['log_sha256'];structural.append(f)
assert checks['native_strict']['structural_errors']==0
assert checks['native_translation_tests']['tests']==10 and checks['translation_program_tests']['tests']==13
assert checks['cloud_draft_tests']['tests']==16 and checks['upstream_google_draft_tests']['tests']==10
assert checks['online_catalog_and_generated_freshness']['locales']==38 and checks['online_catalog_and_generated_freshness']['source_keys']==2842
assert checks['online_unit_tests']['tests']==78 and checks['bengali_draft']['errors']==checks['bengali_draft']['untranslated']==0
record['gates']['structural']={'passed':True,'source_sha256':m['source_sha256'],'catalog_sha256':hashes,'basis':'Actual current38-language strict native validation, V4 completeness/token audit,10 native translation+13 translation-program+16 mocked cloud+10 upstream draft tests,38x2842 online contracts,78 i18n units,3 TypeScript projects, formatting and canonical browser/derivative generation pass. The later three-file alpha fix changes none of these translation/tooling/platform inputs. The separate long-running package roundtrip is not inferred passed here.','evidence':[evidence(f) for f in structural]}
for gate in p.GATES:
 if gate not in record['gates']:record['gates'][gate]={'passed':False,'evidence':[],'reason':'Pending final-source runtime/platform execution; old producers are not relabeled.'}
attestations=KIT/'release-gates.json'
if attestations.exists():
 spec=importlib.util.spec_from_file_location('bp',ROOT/'test/build_provenance.py');bp=importlib.util.module_from_spec(spec);spec.loader.exec_module(bp);current=bp.source_identity(ROOT)
 for name,gate in read(attestations).items():
  assert name in p.GATES and name not in ['language_brief','source_based_pilot','structural'];assert gate['passed'] and gate['source_sha256']==m['source_sha256'] and gate['catalog_sha256']==hashes and gate['code_identity']==current and gate['scope'] and gate['attested_by'] and gate['evidence']
  for item in gate['evidence']:assert sha(KIT/item['path'])==item['sha256']
  record['gates'][name]=gate
write('reviews/bn.json',record);issues=p.audit_locale(KIT,m,record,'bn');expected=[g+' is pending or lacks evidence' for g in p.GATES if not record['gates'][g]['passed']];assert issues==expected,issues
write('assembly-audit.json',{'source_sha256':m['source_sha256'],'catalog_sha256':hashes,'coverage':record['coverage_summary'],'accepted_english':162,'canonical_batches':len(record['batches']),'audit_issues':issues,'status':'Complete only when no release gates remain pending'})
print(json.dumps({'keys':6253,'native':3411,'online':2842,'actual_new_reviews':16,'carried_pairs':6237,'remaining_gates':issues},indent=2))
