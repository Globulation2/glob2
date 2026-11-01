from pathlib import Path
import os,sys,json,time,subprocess,hashlib,shutil,zipfile
root=Path.cwd();base=root/'artifacts/marathi-localization';firstpath=base/'android-build-restart-provenance.json';finalpath=base/'android-final-producer-provenance.json';output=root/'build/android/device/x86_64/24/client/release';first_supervisor=1656014
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
def git(*args):return subprocess.check_output(['git',*args],text=True).strip()
def objects():return {str(p.relative_to(output)):sha(p) for p in sorted((output/'obj').rglob('*.o'))}
print('Waiting for initial6e native/APK producer to finish before clean final incremental build',flush=True)
while True:
 first=json.loads(firstpath.read_text())
 if first.get('application_build_exit_code',0)!=0:raise RuntimeError('Initial Android build failed')
 if first.get('apk'):break
 time.sleep(5)
while Path(f'/proc/{first_supervisor}').exists():time.sleep(1)
shutil.copyfile(firstpath,base/'android-initial-producer-provenance.json')
assert not git('status','--porcelain'),'Final official build requires a clean source tree'
revision=git('rev-parse','HEAD');approval=json.loads((base/'android-final-head-approval.json').read_text());assert revision==approval['approved_final_revision'];assert git('diff','--name-only',approval['intermediate_resource_revision'],revision).splitlines()==approval['changed_paths'];assert approval['production_blob_trees_identical']
source_trees=git('ls-tree','HEAD','src','libgag','libusl','third_party','scons','mobile','data','SConstruct')
changes=git('diff','--name-only',first['current_revision'],revision,'--','src','libgag','libusl','third_party')
before=objects();(base/'android-object-inputs-before-final.json').write_text(json.dumps(before,indent=2)+'\n')
command=['python3','mobile/android.py','build','--arch','x86_64','--release','--jobs','6']
record={'initial_native_producer_revision':first['current_revision'],'initial_provenance':'android-initial-producer-provenance.json','initial_working_tree_status_capture':'Not captured at initial native build launch; initial HEAD is retained without asserting a clean producer snapshot','final_revision':revision,'intermediate_resource_revision':approval['intermediate_resource_revision'],'test_only_followup_approval':approval,'final_source_tree_status_before':git('status','--porcelain'),'final_source_git_trees':source_trees,'native_runtime_changes_since_initial_producer':changes.splitlines() if changes else [],'command':command,'state':'official final incremental build running','notes':['Earlier objects retain their earlier production history. The final official build may reuse them only after SCons validates current inputs; no older producer is relabeled.','Final configuration copies current Android sources/resources and reruns Gradle packaging.'],'recording_prefix':first['recording_prefix']};finalpath.write_text(json.dumps(record,indent=2)+'\n');print('Starting official incremental final build on clean',revision,flush=True)
with (base/'android-build-final-incremental.log').open('w') as log:status=subprocess.call(command,stdout=log,stderr=subprocess.STDOUT)
record['application_build_exit_code']=status;finalpath.write_text(json.dumps(record,indent=2)+'\n');assert status==0,'Final incremental Android build failed'
assert git('rev-parse','HEAD')==revision and not git('status','--porcelain'),'Source changed during final build'
after=objects();(base/'android-object-inputs-after-final.json').write_text(json.dumps(after,indent=2)+'\n');record['objects']={'before_count':len(before),'after_count':len(after),'unchanged_object_bytes':sum(after.get(n)==d for n,d in before.items()),'changed_or_new':[n for n,d in after.items() if before.get(n)!=d],'before_manifest_sha256':sha(base/'android-object-inputs-before-final.json'),'after_manifest_sha256':sha(base/'android-object-inputs-after-final.json')}
logtext=(base/'android-build-final-incremental.log').read_text()
record['final_action_counts']={'printed_compile_commands':sum(' -c ' in line and '-clang' in line for line in logtext.splitlines()),'note':'Unchanged object hashes establish byte identity, not whether the compiler was invoked; consult final official action log for actual reuse/recompilation.'}
apk=Path(first['apk']['path']);prefix=Path(first['recording_prefix']);licenses={str(p.relative_to(prefix/'share/licenses/recording')):sha(p) for p in (prefix/'share/licenses/recording').rglob('*') if p.is_file()}
with zipfile.ZipFile(apk) as archive:
 for name,digest in licenses.items():assert hashlib.sha256(archive.read('assets/glob2-bundle/licenses/recording/'+name)).hexdigest()==digest,name
record['apk']={'path':str(apk),'sha256':sha(apk),'recording_license_files_verified_sha256':licenses};record['staged_marathi_xml_sha256']=sha(output/'android-project/app/src/main/res/values-mr/strings.xml');assert record['staged_marathi_xml_sha256']==sha(root/'mobile/android/app/src/main/res/values-mr/strings.xml');record['final_source_tree_status_after']=git('status','--porcelain');record['state']='verified final clean official build';finalpath.write_text(json.dumps(record,indent=2)+'\n');print('Final clean official Android producer verified',revision,flush=True)
