from pathlib import Path
import subprocess,json,hashlib,posixpath,re,ast
old='d0795ed53c0e6fdd9a9ee17a7b483dbe1e589304';new='2968b18a356e7a17f00c31f1dfa892281d4693af';base='artifacts/marathi-localization/';get=lambda r,n:subprocess.check_output(['git','show',r+':'+n]);load=lambda r,n:json.loads(get(r,base+n));a=load(old,'published-inventory.json');b=load(new,'published-inventory.json')
def tree(revision):
 rows={}
 for item in subprocess.check_output(['git','ls-tree','-r','-l','-z',revision]).split(b'\0'):
  if not item:continue
  meta,path=item.split(b'\t',1);mode,kind,oid,size=meta.decode().split();assert kind=='blob' and mode in ('100644','100755');rows[path.decode()]={'oid':oid,'bytes':int(size)}
 return rows
t1=tree(old);t2=tree(new);names=set(b['sha256']);assert set(t2)==names|{base+'published-inventory.json'};assert sum(v['bytes'] for v in t2.values())==230555331<512*1024**2;assert sum(t2[n]['bytes'] for n in names)==b['payload_bytes'];changes={'added':sorted(names-set(a['sha256'])),'removed':sorted(set(a['sha256'])-names),'changed':sorted(n for n in names&set(a['sha256']) if a['sha256'][n]!=b['sha256'][n])};scanned=[];cred=[]
patterns=[re.compile(x) for x in [r'ya29\.[A-Za-z0-9_-]{20,}',r'(?:gh[pousr]_[A-Za-z0-9]{24,}|github_pat_[A-Za-z0-9_]{30,})',r'AIza[0-9A-Za-z_-]{30,}',r'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----',r'"access_token"\s*:\s*"[A-Za-z0-9._-]{24,}"']]
for n in names:
 if n in a['sha256'] and a['sha256'][n]==b['sha256'][n]:assert t1[n]['oid']==t2[n]['oid']
 else:
  data=get(new,n);assert hashlib.sha256(data).hexdigest()==b['sha256'][n];text=data.decode('utf-8');assert '\0' not in text
  if n.endswith('.json'):json.loads(text)
  if n.endswith('.py'):ast.parse(text)
  for p in patterns:
   if p.search(text):cred.append(n)
  scanned.append(n)
assert not cred;assert not any('/browser-fixture-baseline/' in n for n in names);assert all('/browser-fixture-baseline/' in n for n in changes['removed']);assert changes['changed']==[base+'EVIDENCE.md',base+'publish_evidence.py']
for name in ['integration-bc8/kit/reviews/mr.json','final-verification.json','integration-bc8/source-target-carry.json']:
 assert get(old,base+name)==get(new,base+name)
review=load(new,'integration-bc8/kit/reviews/mr.json');refs=0
for gate,v in review['gates'].items():
 if v.get('passed'):
  for e in v['evidence']:
   path=posixpath.normpath(base+'integration-bc8/kit/'+e['path']);assert b['sha256'][path]==e['sha256'];refs+=1
final=load(new,'final-verification.json')
for e in final['stable_evidence']:assert b['sha256'][e['path']]==e['sha256']
proof=load(new,'integration-bc8/browser-fixture-baseline-proof.json');repro=load(new,'integration-bc8/browser-baseline-reproduction.json');assert repro['tests']==repro['failures']==3 and repro['exit_code']==1 and repro['temporary_inputs_removed'];assert len(repro['source_files'])==7;assert repro['input_bytes']==2814149<repro['input_byte_cap']==64*1024**2
rows={e['path']:e for e in proof['files']}
for e in repro['source_files']:
 actual=subprocess.check_output(['git','show',repro['master']+':'+e['path']]);assert hashlib.sha256(actual).hexdigest()==e['sha256']==rows[e['path']]['sha256'];assert len(actual)==e['bytes']
log=get(new,base+'integration-bc8/browser-baseline-reproduction.log');assert hashlib.sha256(log).hexdigest()==repro['log_sha256'];s=log.decode();assert '# tests 3' in s and '# fail 3' in s and '# skipped 0' in s and 'ENOENT' not in s;assert '186 is outside 187..191' in s and s.count('186 !== 191')==2
for n in names:
 assert not n.startswith(base+'integration-bc8/android/') and not n.startswith(base+'integration-bc8/native/') and not n.startswith(base+'integration-bc8/browser-game/');assert not n.endswith(('.apk','.so','.o','.a','.wasm','.ttf','.woff2','.exe','.dll')) and not n.endswith('/caddy') and '/master-snapshot/' not in n
record={'auditor':'/root/locale_integration','result':'accepted for explicit incomplete current64 checkpoint publication; no material issue','evidence_commit':new,'previous_independently_accepted_commit':old,'source_revision':b['source_revision'],'file_count':len(t2),'total_bytes':230555331,'inventory_exact_to_git_tree':True,'payload_hash_verification':'Every added/changed payload SHA recomputed from exact Git blob; all unchanged payload SHA values and Gitobject identities match independently auditedd079. Exact tree byte count matches.','delta':changes,'delta_text_files_actual_read_scanned':scanned,'credential_pattern_matches':cred,'current_gates_unchanged':{'passed':3,'pending':6,'passed_gate_ref_hashes_verified':refs,'final_stable_ref_hashes_verified':len(final['stable_evidence'])},'baseline_reproduction_review':'Actual reproscript read: seven immutable recordedbc8Gitblobs,64MiB input/1MiB output/60s bound, temporary cleanup, no source/network changes. Actual TAP has exact expected three fixture/version assertion failures, no missingfiles. All7sourceblobs independently rehashed against exactmaster including2.785MBreplay asset. Partial publicsource snapshot omitted; actual proof/logs retained.','repro_inputs_verified':7,'repro_input_bytes':2814149,'same_current_claims':'Current64/sourceb7/native540/online0a/66batches, websites12pass and baseline29 remain unchanged; native/Android/WASMgame runtime truly pending. Historical671/ba/ffbd scopes remain explicit.','binary_exclusions':'NoCaddyGo/nativeobjects/APKs/SDKs/currentactivebuildoutputs/thinsnapshot; oldboundedPlaywright traces/knownsparsefailurelog remain unchanged acceptedexceptions.','old_audit_records_preserved':True,'main_source_or_prepared_tree_mutated':False,'limits':['Unchanged payload content/type/credential assessment carried only by exact SHAandGitobjectidentity from independently auditedd079','Bounded credential-pattern audit, not exhaustive secretcertification','Accepted incomplete publication checkpoint, not currentnative/Android/browser-game completion or overallmergegate']};p=Path(base+'integration-bc8/evidence-delta-audit-2968.json');p.write_text(json.dumps(record,indent=2)+'\n');print(json.dumps({'result':record['result'],'files':record['file_count'],'bytes':record['total_bytes'],'new_or_changed_texts':len(scanned),'ref_hashes':refs,'report_sha256':hashlib.sha256(p.read_bytes()).hexdigest()},indent=2))
