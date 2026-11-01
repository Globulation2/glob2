from pathlib import Path
import hashlib,json,subprocess,sys,shutil
root=Path.cwd();out=root/'artifacts/marathi-localization/integration-bc8/native-baseline';snapshot=out/'master-snapshot';rev='bc8fb2c2bda69af14a7a6c5ed5ca1dfed7575ddb';cap=64*1024**2;reportcap=16*1024**2
assert shutil.disk_usage(out).free>10*1024**3
get=lambda name:subprocess.check_output(['git','show',rev+':'+name]);sha=lambda data:hashlib.sha256(data).hexdigest();inputs={}
listing=get('data/texts.list.txt').decode().splitlines();paths=set(listing)|{'data/texts.list.txt','data/texts.incomplete.txt','data/texts.pending.txt','data/check_translations.py','data/themes/index.json','data/rulesets.json','tools/tutorials/content.py','scripts/tutorials/catalog.json'}
catalog=json.loads(get('scripts/tutorials/catalog.json'));paths.update(e['definition'] for e in catalog)
tracked=set(subprocess.check_output(['git','ls-tree','-r','--name-only',rev],text=True).splitlines())
if 'tools/tutorials/__init__.py' in tracked:paths.add('tools/tutorials/__init__.py')
for name in sorted(paths):
 data=get(name);inputs[name]={'sha256':sha(data),'bytes':len(data),'git_blob':subprocess.check_output(['git','rev-parse',rev+':'+name],text=True).strip()};dest=snapshot/name;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data)
assert sum(v['bytes'] for v in inputs.values())<cap//2
suffixes={'.cpp','.h','.hpp','.cc','.cxx'}
def shipping():
 result={};excluded=[]
 for directory in ('src','libgag'):
  for path in sorted((root/directory).rglob('*')):
   if path.suffix not in suffixes:continue
   rel=str(path.relative_to(root))
   if 'Test' in path.stem or 'Harness' in path.stem:excluded.append(rel);continue
   actual=path.read_bytes();expected=get(rel);assert actual==expected,'Shipping source differs from exactmaster: '+rel;result[rel]=sha(actual)
 return result,excluded
before,excluded=shipping()
for directory in ('src','libgag'):
 link=snapshot/directory
 if link.is_symlink():assert link.resolve()==root/directory
 else:assert not link.exists();link.symlink_to(root/directory,target_is_directory=True)
commands={'master':['python3',str(snapshot/'data/check_translations.py'),'--strict','--json'],'merged':['python3','data/check_translations.py','--strict','--json']};reports={};codes={}
for label,command in commands.items():
 p=subprocess.run(command,capture_output=True,cwd=root);assert len(p.stdout)<=reportcap and len(p.stderr)<=reportcap,'Report byte budget exceeded; no success/truncation';(out/(label+'-strict.json')).write_bytes(p.stdout);(out/(label+'-stderr.log')).write_bytes(p.stderr);reports[label]=json.loads(p.stdout);codes[label]=p.returncode
 assert p.returncode==1,'Expected strict known-baseline failure status'
after,_=shipping();assert after==before,'Shipping source changed while checker ran'
master=reports['master'];merged=reports['merged'];assert len(master['errors'])==len(merged['errors'])==29;assert master['errors']==merged['errors'],'PR structural errors differ from exactmaster'
for path,report in master['languages'].items():assert merged['languages'][path]==report,'Non-MR catalog coverage changed: '+path
mr=merged['languages']['data/texts.mr.txt'];assert all(not mr[field] for field in ('missing','untranslated','pending','extra'))
counts={}
for error in master['errors']:
 name=error.split(':',1)[0];counts[name]=counts.get(name,0)+1
assert counts=={'data/texts.hi.txt':4,'data/texts.ur.txt':4,'data/texts.sw.txt':4,'data/texts.nb.txt':17},counts
assert before and 'src/app/SystemLanguageTest.cpp' in excluded and 'libgag/src/FontKerningTest.cpp' in excluded
assert master['coverage']['native_source_files']==merged['coverage']['native_source_files']==len(before);assert master['coverage']['native_key_references']==merged['coverage']['native_key_references']
proof={'reviewer':'/root/locale_integration','master_revision':rev,'scope':'Exactmaster nativecatalogs/inventories/checker/tutorial-helper/data inputs; shared shippingC++ symlinks only after per-file byte identity to exactmaster, verified again after both read-only strictchecks. No asset/build copy and no builds.','snapshot_inputs':inputs,'shipping_cpp_file_count':len(before),'shipping_cpp_sha256':before,'checker_skips_tests_harnesses':excluded,'authorized_cpp_test_differences_excluded':['libgag/src/FontKerningTest.cpp','src/app/SystemLanguageTest.cpp'],'commands':commands,'exit_codes':codes,'structural_errors':{'master':29,'merged':29,'introduced_by_PR':0,'exact_ordered_diagnostics_equal':True,'per_language':counts,'NB_detail':'16 missing newkeys plus incomplete language marked complete; HI/UR/SW four missing terrainkeys each'},'marathi':{'keys':3411,'missing':0,'untranslated':0,'pending':0,'obsolete':0},'other_language_coverage_exact':True,'source_coverage_equal':True,'diagnostics':master['errors'],'report_sha256':{name:{'json':sha((out/(name+'-strict.json')).read_bytes()),'stderr':sha((out/(name+'-stderr.log')).read_bytes())} for name in commands},'storage_cap_bytes':cap,'report_per_file_cap_bytes':reportcap,'tracked_files_modified':False,'claim_limit':'Baseline reproduction establishes these29native checker findings are present on exactmaster and unchanged in merged source; it does not establish all engine/runtime/otherplatform checks.'}
(out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n');used=sum(p.stat().st_size for p in out.rglob('*') if p.is_file() and not p.is_symlink());assert used<cap
lines=['Exactmaster '+rev,'Master strict exit1:29structural errors','Merged strict exit1:29structural errors','Exact ordered diagnostics match; PR introduced0structural errors','HI4missingterrain + UR4missingterrain + SW4missingterrain + NB16missingkeys+1incorrectcompleteflag','Marathi3411:missing0/untranslated0/pending0/obsolete0','No builds or trackedfile changes','Storage bytes '+str(used)+'/'+str(cap),'Proof SHA256 '+sha((out/'proof.json').read_bytes())];(out/'result.log').write_text('\n'.join(lines)+'\n');print('\n'.join(lines))
