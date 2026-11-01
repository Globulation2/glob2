"""Bind6253 historical actual readings +105 new four-role readings by exact values.
Composite batch author labels are summaries; explicit per-key identities are authoritative.
No rendering/platform gate is inferred or set from completed translations.
"""
from pathlib import Path
import copy,hashlib,importlib.util,json,os
KIT=Path(__file__).resolve().parent;ROOT=KIT.parents[1];OLD=KIT.with_name('bengali-localization-v4')
spec=importlib.util.spec_from_file_location('tp',ROOT/'tools/localization/translation_program.py');p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
read=lambda f:json.loads(f.read_text());sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
def write(name,x):
 f=KIT/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_text(json.dumps(x,ensure_ascii=False,indent=2)+'\n')
def evidence(f):return {'path':os.path.relpath(f,KIT),'sha256':sha(f)}
m=read(KIT/'manifest.json');assert p.sources()[4]==m['source_sha256'];paths=p.catalog_paths(KIT,'bn');hashes={s:sha(f) for s,f in paths.items()};native,errors=p.CHECKER.read_table(paths['native']);assert not errors;targets={'native':native,'online':read(paths['online'])};sources={s:read(KIT/'source'/(s+'.json')) for s in targets}
assert (ROOT/'data/texts.bn.txt').read_bytes()==paths['native'].read_bytes();assert (ROOT/'platform/packages/i18n/locales/bn.json').read_bytes()==paths['online'].read_bytes()
prior=read(OLD/'reviews/bn.json');oldpaths=p.catalog_paths(OLD,'bn');assert {s:sha(f) for s,f in oldpaths.items()}==prior['catalog_sha256'];oldrows={r['id']:r for r in prior['coverage_provenance']};assert len(oldrows)==6253;oldbatches={}
for b in prior['batches']:
 assert set(b['reviewed_keys'])==set(b['edited_keys'])==set(b['rechecked_keys'])==set(b['adversarial_reviewed_keys'])
 for k in b['reviewed_keys']:assert k not in oldbatches;oldbatches[k]=b
assert set(oldrows)==set(oldbatches)
for ident,r in oldrows.items():
 s,k=ident.split(':',1);assert (r['source'],r['target'])==(sources[s][k],targets[s][k]),ident
allkeys={s+':'+k for s,table in sources.items() for k in table};new=allkeys-set(oldrows);assert len(allkeys)==6358 and len(new)==105 and all(k.startswith('online:') for k in new)
assert new=={'online:'+k for k in read(KIT/'delta-105-sources.json')}
a=read(KIT/'author-output/author-delta.json');i=read(KIT/'semantic-initial.json');e=read(KIT/'editor-output/editor-delta.json');f=read(KIT/'semantic-final.json');v=read(KIT/'adversarial-delta.json');author_snapshot=read(KIT/'author-output/bn.json');editor_snapshot=read(KIT/'editor-output/bn.json')
assert a['agent']=='/root/bengali_author' and e['agent']=='/root/bengali_editor' and i['reviewer_identity']==f['reviewer_identity']=='/root/rendering_audit' and v['agent']=='/root/bengali_adversary'
for d in [a,i,e,f,v]:assert d['source_sha256']==m['source_sha256'] and not d.get('unresolved_uncertainty')
assert f['status']=='PASS' and v['status']=='pass' and not f['open_findings'];assert sha(KIT/'author-output/bn.json')==a['authored_catalog_sha256'];assert sha(KIT/'editor-output/bn.json')==e['editor_catalog_sha256']==f['final_online_sha256']==v['online_catalog_sha256'];assert editor_snapshot==targets['online']
# Author and initial reviewer really read the earlier authored wording, not retrospectively the seven edited values.
assert {'online:'+r['key'] for r in a['pairs']}==new
for r in a['pairs']:assert r['english']==sources['online'][r['key']] and r['authored']==author_snapshot[r['key']]
assert {r['id'] for r in i['pairs']}==new
for r in i['pairs']:assert r['source']==sources['online'][r['source']] and r['target']==author_snapshot[r['source']]
for rows,targetfield in [(e['entries'],'editor_final'),(f['pairs'],'target'),(v['freshly_reviewed_pairs'],'target')]:
 assert {r['id'] for r in rows}==new
 for r in rows:
  s,k=r['id'].split(':',1);assert r['source']==sources[s][k] and r[targetfield]==targets[s][k]
assert all(x['state']=='resolved' for x in f['findings'])
checks=p.check_draft(KIT,'bn');assert not checks['errors'] and not checks['untranslated'];accepted=copy.deepcopy(prior['accepted_english']);assert {x['key'] for x in accepted}==set(checks['same_as_english_review_queue']);assert len(accepted)==162
freshfiles=[KIT/'author-output/author-delta.json',KIT/'author-output/bn.json',KIT/'semantic-initial.json',KIT/'editor-output/editor-delta.json',KIT/'editor-output/bn.json',KIT/'semantic-final.json',KIT/'adversarial-delta.json']
historical=[OLD/'reviews/bn.json',OLD/'manifest.json',OLD/'assemble_review.py']
for item in prior['independent_review_evidence']:
 path=OLD/item['path'];assert sha(path)==item['sha256'];historical.append(path)
r={'locale':'bn','source_sha256':m['source_sha256'],'catalog_sha256':hashes,'certification':'Agent-reviewed; not native-human-certified','language_brief':copy.deepcopy(prior['language_brief']),'findings':copy.deepcopy(prior['findings'])+copy.deepcopy(f['findings']),'accepted_english':accepted,'batches':[],'coverage_provenance':[],'gates':{}}
roles_by_key={}
for ident in allkeys:
 if ident in oldrows:roles_by_key[ident]={role:oldbatches[ident][role] for role in ['author','reviewer','editor','adversarial_reviewer']}
 else:roles_by_key[ident]={'author':'/root/bengali_author','reviewer':'/root/rendering_audit','editor':'/root/bengali_editor','adversarial_reviewer':'/root/bengali_adversary'}
 assert len(set(roles_by_key[ident].values()))==4,ident
batchhash=p.batch_target_hashes(m,paths)
for b in m['batches']:
 perkey={k:roles_by_key[k] for k in b['keys']};summary={}
 for role in ['author','reviewer','editor','adversarial_reviewer']:
  people=sorted({x[role] for x in perkey.values()});summary[role]=people[0] if len(people)==1 else 'Composite historical authors: '+' + '.join(people)
 assert len(set(summary.values()))==4
 r['batches'].append({'id':b['id'],'source_sha256':m['source_sha256'],'target_sha256':batchhash[b['id']],**summary,'per_key_role_provenance':perkey,'author_field_semantics':'Summary only; per-key identities retain actual historical authors. A composite author field is not a new person or a fresh reading.','reviewed_keys':b['keys'],'edited_keys':b['keys'],'rechecked_keys':b['keys'],'rechecked_by':summary['reviewer'],'adversarial_reviewed_keys':b['keys'],'carried_prior_actual_read_keys':[k for k in b['keys'] if k in oldrows],'fresh_delta_actual_read_keys':[k for k in b['keys'] if k in new]})
 for ident in b['keys']:
  s,k=ident.split(':',1);row={'id':ident,'source':sources[s][k],'target':targets[s][k],'roles':roles_by_key[ident],'basis':'Exact source-target carry of the actual V4/V3/V2/V1 reading chain; not a fresh read' if ident in oldrows else 'Actual105-key author and initial semantic reading, distinct editorial examination, original reviewer final reread, distinct adversarial examination','prior_batch':oldbatches[ident]['id'] if ident in oldrows else None}
  if ident in new:row.update(author_snapshot_target=author_snapshot[k],editor_changed=author_snapshot[k]!=targets[s][k],actual_new_role_evidence=[evidence(x) for x in freshfiles if x.suffix=='.json' and x.name!='bn.json'])
  r['coverage_provenance'].append(row)
r['coverage_summary']={'complete_keys':6358,'native_keys':3411,'online_keys':2947,'carried_actual_prior_read_pairs':6253,'freshly_reviewed_new_pairs':105,'initial_semantic_findings':2,'initial_findings_resolved':2,'actual_per_key_role_provenance':True,'mechanical_rebinding_is_not_fresh_review':True}
r['independent_review_evidence']=[evidence(x) for x in dict.fromkeys(historical+freshfiles+[KIT/'delta-105-sources.json',KIT/'manifest.json'])]
for name in ['language_brief','source_based_pilot']:
 g=prior['gates'][name];assert g['passed'];files=[]
 for item in g['evidence']:
  path=OLD/item['path'];assert sha(path)==item['sha256'];files.append(path)
 r['gates'][name]={'passed':True,'source_sha256':m['source_sha256'],'catalog_sha256':hashes,'basis':'Original source-based brief/pilot and6253 unchanged pairs carry exactly. The105 new metadata strings received actual four-role contextual review under the same brief; no new pilot or full old-catalog read invented.','evidence':[evidence(x) for x in dict.fromkeys(files+freshfiles)]}
for name in p.GATES:
 if name not in r['gates']:r['gates'][name]={'passed':False,'evidence':[],'reason':'Final source structural/runtime/platform attestations pending; no gate inferred from completed translations.'}
# Only explicit independently authored final attestations may replace pending gates.
for filename in ['structural-gate.json','release-gates.json']:
 path=KIT/filename
 if not path.exists():continue
 attestations=read(path);attestations={'structural':attestations} if filename=='structural-gate.json' else attestations
 for name,g in attestations.items():
  assert name in p.GATES and name not in ['language_brief','source_based_pilot'];assert g['passed'] and g['source_sha256']==m['source_sha256'] and g['catalog_sha256']==hashes and g['evidence'] and g.get('scope') and g.get('attested_by')
  for item in g['evidence']:assert sha(KIT/item['path'])==item['sha256']
  r['gates'][name]=g
write('reviews/bn.json',r);issues=p.audit_locale(KIT,m,r,'bn');expected=[name+' is pending or lacks evidence' for name in p.GATES if not r['gates'][name]['passed']];assert issues==expected,issues
write('assembly-audit.json',{'source_sha256':m['source_sha256'],'catalog_sha256':hashes,'coverage':r['coverage_summary'],'accepted_english':len(accepted),'canonical_batches':len(r['batches']),'composite_author_batches':[b['id'] for b in r['batches'] if b['author'].startswith('Composite')],'audit_issues':issues,'status':'Complete only when actual scoped gate attestations leave no pending issue'})
print(json.dumps({'keys':6358,'carried':6253,'actual_new_four_role_reviews':105,'accepted_english':162,'pending':issues},indent=2))
