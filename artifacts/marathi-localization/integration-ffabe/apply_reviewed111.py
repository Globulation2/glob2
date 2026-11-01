from pathlib import Path
import json,hashlib,datetime,importlib.util,subprocess
b=Path('artifacts/marathi-localization/integration-ffabe');kit=b/'kit'
load=lambda n:json.loads((b/n).read_text())
s={x['key']:x for x in load('semantic-new111.json')['findings']};a={x['key']:x for x in load('adversarial-new111.json')['rows']};e={x['key']:x for x in load('editorial-new111.json')['decisions']};raw={x['key']:x for x in load('google-draft.json')['entries']}
assert set(s)==set(a)==set(e)==set(raw) and len(e)==111
chosen={k:x['recommended_target'] for k,x in e.items()};reasons={}
def take(k,role,why):
 chosen[k]={'semantic':s[k]['recommended_target'],'adversarial':a[k]['proposal']}[role];reasons[k]=why
for k,why in {
 'Rush':'Early attack is the defining AI strategy, not only fast attack; semantic/adversarial agree and HI/FR references explicitly mean early.',
 'Persistent state':'Name the retained state rather than the act of saving; avoid a promise that state is immutable/permanent.',
 'Save and resume':'Continue retained progress; avoid implying restart or the less natural पुढे सुरू ठेवा.',
 'Fair':'Matches existing Marathi Good-under/fair-under/poor-from network-quality sentence (साधारण).',
 'Check that building and combat feel distinct from calm.':'Use established Marathi mood names with usual curly quotation marks, not French guillemets.',
 'Honeycomb':'A cellular surface pattern, distinguished from the Swarm hive building; FR alvéoles corroborates individual cells.'
}.items():take(k,'semantic',why)
for k,why in {
 'Your input delay: from your click until it happens, for everyone. Half your ping plus the buffer your game holds against jitter.':'Explicit half-ping plus jitter-buffer SUM, with correct Marathi case ending and shared action timing.',
 'The server replayed this match and every player’s game agreed with it.':'Agreement means matching game state rather than anthropomorphic consent.',
 'A player’s game disagreed with the server’s replay of the match; the server’s result counts.':'States mismatch is an observed fact; server result authoritative, not a hypothetical concession.'
}.items():take(k,'adversarial',why)
chosen['Live payments']=s['Live payments']['recommended_target'];reasons['Live payments']='Real-money environment versus test; वास्तविक avoids the possible in-person/direct sense of प्रत्यक्ष.'
k='Listen for unexpected jumps in volume between moods.';chosen[k]='मूड बदलताना आवाजाची पातळी अचानक, अनपेक्षितपणे बदलते का ते ऐका.';reasons[k]='Abrupt unexpected volume changes rather than literal physical jumps or arbitrary gradual level changes.'
oldpath=Path('platform/packages/i18n/locales/mr.json');old=json.loads(subprocess.check_output(['git','show','HEAD:platform/packages/i18n/locales/mr.json'],text=True));assert len(old)==2836 and not(set(old)&set(chosen))
full=json.loads((kit/'drafts/mr/mr.json').read_text());assert len(full)==2947
for k,v in old.items():assert full[k]==v
for k,v in chosen.items():full[k]=v
content=json.dumps(full,ensure_ascii=False,indent=2)+'\n';(kit/'drafts/mr/mr.json').write_text(content);oldpath.write_text(content)
assert Path('data/texts.mr.txt').read_bytes()==(kit/'drafts/mr/texts.mr.txt').read_bytes()
rows=[]
for k in e:
 rows.append({'key':k,'source':raw[k]['source'],'raw_google':raw[k]['raw_google_target'],'semantic':s[k]['recommended_target'],'editorial':e[k]['recommended_target'],'adversarial':a[k]['proposal'],'selected':chosen[k],'selection_basis':reasons.get(k,'Editorial context/grammar selection; other independent proposals retained for final reread.'),'state':'selected_pending_three_final_rereads'})
r={'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'author':'/root','source_sha256':load('kit/manifest.json')['source_sha256'],'catalog_sha256':{'native':hashlib.sha256(Path('data/texts.mr.txt').read_bytes()).hexdigest(),'online':hashlib.sha256(oldpath.read_bytes()).hexdigest()},'reviewed_new_keys':111,'changed_from_google':sum(chosen[k]!=raw[k]['raw_google_target'] for k in chosen),'old_online_pairs_preserved':len(old),'native_catalog_bytes_unchanged':True,'independent_review_sha256':{n:hashlib.sha256((b/n).read_bytes()).hexdigest() for n in ['semantic-new111.json','editorial-new111.json','adversarial-new111.json']},'spelling_attestation':{'chosen':'अळंबी','note':'Both अळंबी and अळिंबी attested; selected editorial noun consistently with its short shape description. No claim alternate spelling is wrong or that the asset is edible/poisonous.','actual_opened_primary_sources':[{'url':'https://psvasai.zppalghar.gov.in/agriculture-places/','read_lines':'76–80','short_quote':'अळंबी','observation':'Government page explicitly identifies this as Alambi/Mushroom and lists अळिंबी/भूछत्र as synonyms.'},{'url':'https://vishwakosh.marathi.gov.in/28175/','read_lines':'59–60','short_quote':'इं. मशरूम,टोडस्टूल','observation':'Government Marathi encyclopedia explicitly associates the shape class with mushroom/toadstool.'}],'dictionary_fetch_failed':'https://shabdakosh.marathi.gov.in/ananya-glossary?page=4545 returned502; not used as read attestation.'},'rows':rows,'final_english_exceptions':[]}
(b/'adjudication-new111.json').write_text(json.dumps(r,ensure_ascii=False,indent=2)+'\n');(b/'final-targets111.json').write_text(json.dumps(chosen,ensure_ascii=False,indent=2)+'\n')
print(json.dumps({k:r[k] for k in ['source_sha256','catalog_sha256','reviewed_new_keys','changed_from_google','old_online_pairs_preserved']},indent=2))
